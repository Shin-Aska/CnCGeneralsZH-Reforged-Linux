from __future__ import annotations

import json
import os
import shutil
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import assert_never

from bitmap import inspect_bmp
from capture_archive import (
    ArchiveError,
    attach_archive_locations,
    seal_new_capture_videos,
)
from commands import build_command
from control_monitor import ControlPlan
from evidence import (
    authored_world_valid,
    scenario_commands_valid,
    vessel_route_valid,
    water_draw_valid,
)
from isolation import launch_isolated, prepare_output
from models import Platform, ResultClass, RunPaths, Scene
from runtime import (
    GameDataBase,
    asset_digest,
    backend_name,
    inherited_overrides,
    load_game_data_fragments,
    materialize_farm,
    materialize_overlay,
    materialize_windows_inputs,
    merge_private_files,
    merge_standard_map_cache,
    nearby_baseline_revision,
    reserve_control_port,
    resolve_game_data_base,
    sha256,
    source_identity,
    stage_scene_game_data,
    stage_windows_user_map,
)
from schema import load_scenes


@dataclass(frozen=True, slots=True)
class EvidenceError(RuntimeError):
    scene_id: str
    reason: str

    def __str__(self) -> str:
        return f"{self.scene_id}: {self.reason}"


@dataclass(frozen=True, slots=True)
class ContaminatedEnvironmentError(RuntimeError):
    variables: tuple[str, ...]

    def __str__(self) -> str:
        return "default evidence rejects inherited overrides: " + ", ".join(self.variables)


DEFAULT_OPTIONS = (
    "Resolution = 1280 720\n"
    "StaticGameLOD = High\n"
    "DynamicLOD = no\n"
    "TextureReduction = 0\n"
    "ShowSoftWaterEdge = yes\n"
    "VSync = no\n"
    "MSAA = 0\n"
)

EXPECTED_ZH_GAME_DATA_SHA256 = "da7ab8448b91a04cedb6affe1f6acda28960b82e643b4e94f843df4e511319cd"
EXPECTED_ZH_GAME_DATA_LENGTH = 21181


def _copy_engine_log(farm: Path, scene_id: str, call: Path) -> Path | None:
    candidates = sorted(farm.glob(f"water_{scene_id}_*DebugLogFile.txt"))
    if not candidates:
        return None
    target = call / "engine.log"
    shutil.copy2(candidates[-1], target)
    return target


def _validate_scene_evidence(scene: Scene, user: Path) -> list[dict[str, object]]:
    images = sorted(user.rglob("sshot*.bmp")) + sorted(user.rglob("frame*.bmp"))
    if scene.requires_motion and len(images) < 2:
        raise EvidenceError(scene.scene_id, "motion evidence needs at least two native BMP frames")
    if not scene.requires_motion and len(images) != 1:
        raise EvidenceError(scene.scene_id, "expected one native BMP screenshot")
    selected = images if scene.requires_motion else images[:1]
    observations: list[dict[str, object]] = [
        {**asdict(inspect_bmp(path, scene.expected_image)), "path": str(path)} for path in selected
    ]
    if scene.requires_motion and len({observation["sha256"] for observation in observations}) < 2:
        raise EvidenceError(scene.scene_id, "motion frames are identical")
    return observations


def _pose_bindings(
    scene: Scene, user: Path, requested_frames: tuple[int, ...]
) -> list[dict[str, int | float | str]]:
    if scene.route is None:
        return []
    expected = scene.route.poses[1:]
    screenshots = sorted(user.rglob("sshot*.bmp"))
    if len(screenshots) != len(expected) or requested_frames != tuple(pose.frame for pose in expected):
        raise EvidenceError(scene.scene_id, "native screenshots do not match declared stopped-pose frames")
    return [
        {
            "frame": pose.frame, "x": pose.x, "y": pose.y, "phase": pose.phase,
            "path": str(path), "sha256": sha256(path),
        }
        for pose, path in zip(expected, screenshots)
    ]


def _cleanup_game_data(farm: Path, base: GameDataBase) -> dict[str, object]:
    try:
        receipt = stage_scene_game_data(farm, None, base)
    except (OSError, RuntimeError, ValueError) as error:
        return {
            "classification": ResultClass.FAILED,
            "failure_reason": f"{type(error).__name__}: {error}",
        }
    return {"classification": ResultClass.PASSED, "receipt": receipt}


def _suite_outcome(
    scene_failed: bool, orchestration_failure: str | None, cleanup: dict[str, object]
) -> tuple[ResultClass, int]:
    failed = (
        scene_failed
        or orchestration_failure is not None
        or cleanup.get("classification") != ResultClass.PASSED
    )
    return (ResultClass.FAILED, 1) if failed else (ResultClass.PASSED, 0)


def run_fixture_selfcheck(paths: RunPaths, platform: Platform, timeout_seconds: float) -> int:
    contaminated = inherited_overrides(os.environ)
    if contaminated:
        raise ContaminatedEnvironmentError(contaminated)
    prepare_output(paths.output)
    helper_root = Path(__file__).resolve().parent
    catalog = load_scenes(helper_root / "scenes.json")
    farm = paths.output / "farm"
    materialize_farm(paths.root, farm)
    try:
        relative_executable = paths.executable.relative_to(paths.root)
        executable = farm / relative_executable
    except ValueError:
        executable = paths.executable
    data_hash, asset_count = asset_digest(paths.root)
    repository = helper_root.parents[3]
    source = source_identity(repository)
    fixture_source = paths.overlay or (helper_root / "fixtures" / "overlay")
    fixture_fragment = helper_root / "fixtures" / "WaterValidationMapCache.ini"
    game_data_base = resolve_game_data_base(
        paths.root, EXPECTED_ZH_GAME_DATA_SHA256, EXPECTED_ZH_GAME_DATA_LENGTH
    )
    game_data_fragments = load_game_data_fragments(catalog.scenes, helper_root / "fixtures")
    private_overlay: Path | None = None
    child_fixture_assets: dict[str, str] = {}
    if fixture_source.is_dir():
        private_overlay = paths.output / "overlay"
        materialize_overlay(fixture_source, private_overlay)
        if platform is Platform.WINDOWS:
            executable, child_fixture_assets = materialize_windows_inputs(paths.executable, private_overlay, farm)
        else:
            scenarios = private_overlay / "Scenarios"
            if scenarios.is_dir():
                merge_private_files(scenarios, farm / "Scenarios")
    merged_cache = merge_standard_map_cache(farm, fixture_fragment)
    for scene in catalog.scenes:
        for asset in scene.required_assets:
            expected_root = farm if platform is Platform.WINDOWS else private_overlay
            if expected_root is None or not (expected_root / Path(asset)).is_file():
                raise EvidenceError(scene.scene_id, f"required fixture asset is missing: {asset}")
    fixture_assets = {
        str(path.relative_to(helper_root)): sha256(path)
        for path in sorted((helper_root / "fixtures").rglob("*"))
        if path.is_file() and "__pycache__" not in path.parts
    }
    fixture_assets["farm/Maps/MapCache.ini"] = sha256(merged_cache)
    scene_results: list[dict[str, object]] = []
    failed = False
    orchestration_failure: str | None = None
    pending_interrupt: BaseException | None = None
    game_data_cleanup: dict[str, object]
    try:
        for scene in catalog.scenes:
            call = paths.output / "scenes" / scene.scene_id
            call.mkdir(parents=True)
            user = call / "user"
            user.mkdir()
            game_data_staging = stage_scene_game_data(
                farm, game_data_fragments.get(scene.scene_id), game_data_base
            )
            command_map_path: str | None = None
            user_map_assets: dict[str, str] = {}
            if platform is Platform.WINDOWS and scene.requires_vessel and private_overlay is not None:
                staged_map, user_map_assets = stage_windows_user_map(scene.map_path, private_overlay, user)
                command_map_path = str(staged_map)
            style = "0" if platform is Platform.WINDOWS else "1"
            (user / "Options.ini").write_text(DEFAULT_OPTIONS + f"WaterStyle = {style}\n", encoding="utf-8")
            port = reserve_control_port() if scene.requires_motion else 0
            overrides = {"ZH_USER_DATA_DIR": str(user), "ZH_UNATTENDED": "1", "ZH_AUDIO_BACKEND": "null"}
            if scene.requires_motion:
                overrides["ZH_WATER_CAPTURE_CLOCK"] = "picture"
                overrides["PATH"] = str(farm)
            if scene.requires_vessel:
                overrides["ZH_WATER_CAPTURE_FIXTURE"] = "carrier"
            match platform:
                case Platform.WINDOWS:
                    pass
                case Platform.LINUX:
                    overrides["SDL_GPU_DRIVER"] = "vulkan"
                case Platform.MACOS:
                    overrides["SDL_GPU_DRIVER"] = "metal"
                case unreachable:
                    assert_never(unreachable)
            command = build_command(
                scene, executable, farm, private_overlay, port, platform, map_path=command_map_path
            )
            control_plan = None
            if scene.requires_motion and scene.route is not None:
                control_plan = ControlPlan(port, scene.max_frames, tuple(pose.frame for pose in scene.route.poses[1:]))
            result = launch_isolated(command, call, timeout_seconds, overrides, farm, control_plan)
            engine_log = _copy_engine_log(farm, scene.scene_id, call)
            failure_reason: str | None = result.control_error
            try:
                images = _validate_scene_evidence(scene, user)
                engine_text = engine_log.read_text(errors="replace") if engine_log is not None else ""
                run_text = (call / "run.log").read_text(errors="replace")
                runtime_text = engine_text + "\n" + run_text
                scenario_ok = scene.route is None or scenario_commands_valid(runtime_text, scene.route)
                vessel_ok = scene.route is None or vessel_route_valid(runtime_text, scene.route)
                world_ok = water_draw_valid(engine_text) and (scene.route is None or authored_world_valid(engine_text))
                pose_bindings = _pose_bindings(scene, user, result.screenshot_frames)
                passed = (
                    result.returncode == 0 and not result.timed_out and result.control_error is None
                    and scenario_ok and vessel_ok and world_ok
                )
                if not passed and failure_reason is None:
                    failure_reason = "process, scenario, or vessel evidence gate failed"
                capture_archives = ()
                if passed:
                    capture_archives = seal_new_capture_videos(user, {
                        "scene_id": scene.scene_id,
                        "platform": platform.value,
                        "backend": backend_name(platform),
                        "executable_sha256": sha256(executable),
                        "data_sha256": data_hash,
                        "native_capture": True,
                    })
                    attach_archive_locations(images, capture_archives)
            except (ArchiveError, EvidenceError, FileNotFoundError, ValueError) as error:
                images = []
                pose_bindings = []
                capture_archives = ()
                passed = False
                failure_reason = str(error)
            failed = failed or not passed
            scene_results.append({
                "scene_id": scene.scene_id, "classification": ResultClass.PASSED if passed else ResultClass.FAILED,
                "returncode": result.returncode, "timed_out": result.timed_out, "elapsed_seconds": result.elapsed_seconds,
                "control_port": port, "evidence_frame": result.evidence_frame,
                "control_quit_requested": result.control_quit_requested,
                "capture_clock": {
                    "requested": overrides.get("ZH_WATER_CAPTURE_CLOCK") == "picture",
                    "eligible_predicate": scene.requires_motion and "-video" in command and port > 0,
                    "actual_verified": None,
                    "verification": "unknown_no_runtime_attestation",
                },
                "images": images, "pose_bindings": pose_bindings, "failure_reason": failure_reason,
                "user_map_assets": user_map_assets,
                "command_map_path": command_map_path or scene.map_path,
                "game_data_staging": game_data_staging,
                "capture_archives": capture_archives,
            })
    except (OSError, RuntimeError, ValueError) as error:
        orchestration_failure = f"{type(error).__name__}: {error}"
    except (KeyboardInterrupt, SystemExit) as error:
        orchestration_failure = f"{type(error).__name__}: interrupted"
        pending_interrupt = error
    finally:
        game_data_cleanup = _cleanup_game_data(farm, game_data_base)
    classification, exit_code = _suite_outcome(failed, orchestration_failure, game_data_cleanup)
    manifest = {
        "schema_version": 1, "classification": classification,
        "platform": platform, "backend": backend_name(platform), "executable": str(paths.executable),
        "executable_sha256": sha256(paths.executable), "data_sha256": data_hash, "asset_count": asset_count,
        "runtime_baseline_identity": {
            "executable_sha256": sha256(paths.executable), "data_sha256": data_hash,
            "source_revision": nearby_baseline_revision(paths.root),
        },
        "helper_source_identity": asdict(source), "root": str(paths.root),
        "fixture_assets": fixture_assets,
        "native_child_fixture_assets": child_fixture_assets,
        "overlay": str(paths.overlay) if paths.overlay else None, "scenes": scene_results,
        "orchestration_failure": orchestration_failure,
        "game_data_cleanup": game_data_cleanup,
    }
    (paths.output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    if pending_interrupt is not None:
        raise pending_interrupt
    return exit_code


def write_unavailable(paths: RunPaths, platform: Platform, suite: str, reason: str) -> int:
    prepare_output(paths.output)
    manifest = {"schema_version": 1, "classification": ResultClass.UNAVAILABLE, "platform": platform, "suite": suite, "reason": reason}
    (paths.output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    return 3
