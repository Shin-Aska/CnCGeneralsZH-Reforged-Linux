from __future__ import annotations

import json
import struct
import tempfile
from pathlib import Path

from bitmap import inspect_bmp
from comparison import compare_manifests
from evidence import (
    authored_world_valid,
    scenario_commands_valid,
    scenario_report_valid,
    vessel_route_valid,
    water_draw_valid,
)
from isolation import prepare_output
from manifest import compare_identity
from models import CaptureIdentity, ExpectedImage, FixtureRoute, Rect, RoutePose
from runtime import inherited_overrides
from schema import load_scenes
from selftest_archive import test_capture_archive
from selftest_control import test_controlled_termination
from selftest_isolation import (
    test_command_lifecycle,
    test_game_data_materialization,
    test_master_isolation,
    test_process_isolation,
    test_windows_input_materialization,
)
from selftest_suite import test_fixture_suite_finalization


class SelfTestFailure(RuntimeError):
    pass


def _require(condition: bool, message: str) -> None:
    if not condition:
        raise SelfTestFailure(message)


def _write_bmp(path: Path, width: int, height: int, pixels: list[tuple[int, int, int]]) -> None:
    row_bytes = (width * 3 + 3) & ~3
    body = bytearray(row_bytes * height)
    for y in range(height):
        for x in range(width):
            red, green, blue = pixels[y * width + x]
            at = (height - 1 - y) * row_bytes + x * 3
            body[at : at + 3] = bytes((blue, green, red))
    header = struct.pack("<2sIHHI", b"BM", 54 + len(body), 0, 0, 54)
    dib = struct.pack("<IiiHHIIiiII", 40, width, height, 1, 24, 0, len(body), 0, 0, 0, 0)
    path.write_bytes(header + dib + body)


def _test_images(root: Path) -> None:
    expected = ExpectedImage(Rect(0, 0, 8, 8), min_water_pixels=16, min_luma=0.03, max_luma=0.95)
    water = root / "water.bmp"
    _write_bmp(water, 8, 8, [(15, 80, 145)] * 32 + [(80, 130, 180)] * 32)
    _require(inspect_bmp(water, expected).water_pixels >= 16, "water BMP was rejected")
    brown = root / "brown-water.bmp"
    _write_bmp(brown, 8, 8, [(82, 76, 48)] * 32 + [(126, 104, 62)] * 32)
    _require(inspect_bmp(brown, expected).water_pixels >= 16, "brown reflective water BMP was rejected")
    for name, pixels in (
        ("blank", [(0, 0, 0)] * 64),
        ("non-water", [(110, 145, 30)] * 32 + [(150, 90, 20)] * 32),
    ):
        candidate = root / f"{name}.bmp"
        _write_bmp(candidate, 8, 8, pixels)
        try:
            inspect_bmp(candidate, expected)
        except ValueError:
            rejected = True
        else:
            rejected = False
        _require(rejected, f"{name} BMP was accepted")
    missing = root / "missing.bmp"
    try:
        inspect_bmp(missing, expected)
    except FileNotFoundError:
        rejected = True
    else:
        rejected = False
    _require(rejected, "missing BMP was accepted")
    hud_only = root / "black-world-with-hud.bmp"
    hud_expected = ExpectedImage(Rect(0, 0, 100, 100), min_water_pixels=100, min_luma=0.0, max_luma=1.0)
    _write_bmp(hud_only, 100, 100, [(0, 0, 0)] * 9200 + [(15, 80, 145)] * 800)
    try:
        inspect_bmp(hud_only, hud_expected)
    except ValueError:
        rejected = True
    else:
        rejected = False
    _require(rejected, "black world with a small blue HUD was accepted as water evidence")


def _test_identity() -> None:
    base = CaptureIdentity("abc", "data", "scene", 240.0, "d3d11", "modern")
    _require(compare_identity(base, base) == (), "matching identity was rejected")
    variants = (
        CaptureIdentity("stale", "data", "scene", 240.0, "d3d11", "modern"),
        CaptureIdentity("abc", "other", "scene", 240.0, "d3d11", "modern"),
        CaptureIdentity("abc", "data", "scene", 241.0, "d3d11", "modern"),
        CaptureIdentity("abc", "data", "other", 240.0, "d3d11", "modern"),
        CaptureIdentity("abc", "data", "scene", 240.0, "vulkan", "modern"),
        CaptureIdentity("abc", "data", "scene", 240.0, "d3d11", "classic"),
    )
    for variant in variants:
        _require(bool(compare_identity(base, variant)), "mismatched identity was accepted")


def _test_schema(root: Path) -> None:
    missing_template = root / "missing-template.json"
    missing_template.write_text(
        '{"schema_version":1,"scenes":[{"id":"fixture","map_path":"Maps/F/F.map",'
        '"camera":{"x":1,"y":2},"screenshot_frame":10,"max_frames":20,'
        '"water_roi":{"x":0,"y":0,"width":4,"height":4},"scenario":"route",'
        '"required_assets":[],"expected":{"min_water_pixels":1,"min_luma":0.01,'
        '"max_luma":0.99,"requires_motion":true,"requires_vessel":true}}]}',
        encoding="utf-8",
    )
    try:
        load_scenes(missing_template)
    except ValueError:
        rejected = True
    else:
        rejected = False
    _require(rejected, "fixture scene without a route/template was accepted")
    catalog_path = Path(__file__).resolve().parent / "scenes.json"
    catalog_data = json.loads(catalog_path.read_text(encoding="utf-8"))
    authored = next(scene for scene in catalog_data["scenes"] if scene["id"] == "water-fixture")
    _require(
        authored.get("game_data_append") == "WaterValidationGameData.fragment.ini",
        "authored scene omitted its fixed fixture-root GameData append",
    )
    authored["game_data_append"] = "../outside.ini"
    unsafe_append = root / "unsafe-game-data-append.json"
    unsafe_append.write_text(json.dumps(catalog_data), encoding="utf-8")
    try:
        load_scenes(unsafe_append)
    except ValueError:
        rejected = True
    else:
        rejected = False
    _require(rejected, "traversing GameData append path was accepted")
    authored["game_data_append"] = "WaterValidationGameData.fragment.ini"
    authored["id"] = "other-vessel"
    wrong_selector = root / "wrong-game-data-selector.json"
    wrong_selector.write_text(json.dumps(catalog_data), encoding="utf-8")
    try:
        load_scenes(wrong_selector)
    except ValueError:
        rejected = True
    else:
        rejected = False
    _require(rejected, "non-authored vessel scene accepted a GameData append")


def _test_scenario_evidence(root: Path) -> None:
    del root
    accepted = "HEADLESS SCENARIO: 5 actions, 5 ran, 0 refused, 1 units spawned"
    rejected = "HEADLESS SCENARIO: 5 actions, 4 ran, 1 refused, 1 units spawned"
    _require(scenario_report_valid(accepted), "accepted scenario summary was rejected")
    _require(not scenario_report_valid(rejected), "refused scenario action was accepted")
    route_log = (
        "SCENARIO: loaded 'Scenarios\\route.txt', 7 actions, 0 lines refused\n"
        "water-capture-command: frame=180 requested=90 action=0 player=USA index=0 allowed=1 success=1 object=42 x=260.000 y=280.000\n"
        "water-capture-command: frame=240 requested=120 action=1 player=USA index=0 allowed=1 success=1 object=42 x=760.000 y=280.000\n"
        "water-capture-command: frame=390 requested=195 action=4 player=USA index=0 allowed=1 success=1 object=42 x=0.000 y=0.000\n"
        "water-capture-command: frame=450 requested=225 action=1 player=USA index=0 allowed=1 success=1 object=42 x=300.000 y=280.000\n"
        "water-capture-command: frame=540 requested=270 action=4 player=USA index=0 allowed=1 success=1 object=42 x=0.000 y=0.000\n"
        "water-capture-command: frame=600 requested=300 action=1 player=USA index=0 allowed=1 success=1 object=42 x=860.000 y=280.000\n"
        "water-capture-command: frame=840 requested=420 action=4 player=USA index=0 allowed=1 success=1 object=42 x=0.000 y=0.000\n"
    )
    declared = FixtureRoute(
        "USA", "WaterCaptureCarrier",
        (RoutePose(180, 260.0, 280.0, "spawn"), RoutePose(390, 760.0, 280.0, "stop"), RoutePose(540, 300.0, 280.0, "reverse-stop")),
    )
    _require(vessel_route_valid(route_log, declared), "declared vessel route was rejected")
    _require(not vessel_route_valid(route_log.replace("x=300.000", "x=301.000"), declared), "wrong route coordinate passed")
    _require(scenario_commands_valid(route_log, declared), "accepted scenario commands were rejected")
    _require(not scenario_commands_valid(route_log.replace("success=1", "success=0", 1), declared), "failed command passed")
    _require(water_draw_valid("-dx11 drew: abc engine:classictrapezoidwater:def -> 12 draws"), "native water draw was rejected")
    _require(not water_draw_valid("SCENE SPLIT frame 20: 200 draws"), "ordinary draws passed as water")
    _require(authored_world_valid("camera held; players alive"), "healthy authored world was rejected")
    _require(not authored_world_valid("Failed to find initial camera position waypoint InitialCameraPosition"), "missing camera passed")
    _require(not authored_world_valid("Marking AI player player1 as defeated"), "defeated fixture player passed")


def _test_override_rejection(root: Path) -> None:
    del root
    rejected = inherited_overrides({
        "PATH": "safe", "ZH_WATER_CAPTURE_CLOCK": "picture", "ZH_DX11_DRIVER": "warp",
        "SDL_GPU_DRIVER": "vulkan",
    })
    _require(
        rejected == ("SDL_GPU_DRIVER", "ZH_DX11_DRIVER", "ZH_WATER_CAPTURE_CLOCK"),
        "water/backend override inventory was incomplete",
    )


def _test_compare_preconditions(root: Path) -> None:
    identity = {
        "data_sha256": "data", "scene_id": "fixture", "picture_time": 240.0,
        "settings": {"resolution": [1280, 720]}, "backend": "native", "water_style": "modern",
        "capture_clock_actual_verified": True,
    }
    left = root / "compare-left.json"
    right = root / "compare-right.json"
    left.write_text(json.dumps(identity), encoding="utf-8")
    right.write_text(json.dumps(identity | {"picture_time": 241.0}), encoding="utf-8")
    _require(compare_manifests(left, right, root / "compare-failed") == 1, "time mismatch passed comparison")
    right.write_text(json.dumps(identity), encoding="utf-8")
    _require(
        compare_manifests(left, right, root / "compare-unavailable") == 3,
        "unimplemented image comparison fabricated a pass",
    )
    right.write_text(json.dumps(identity | {"capture_clock_actual_verified": None}), encoding="utf-8")
    _require(compare_manifests(left, right, root / "compare-clock-unknown") == 1, "unknown capture clock passed comparison")


def run_selftests(evidence_root: Path | None = None) -> tuple[str, ...]:
    tests = (
        _test_images, _test_schema, _test_scenario_evidence,
        _test_override_rejection, _test_compare_preconditions, test_process_isolation,
        test_master_isolation, test_command_lifecycle,
        test_windows_input_materialization, test_controlled_termination, test_capture_archive,
        test_game_data_materialization, test_fixture_suite_finalization,
    )
    passed: list[str] = ["_test_identity"]
    _test_identity()

    def run(root: Path) -> None:
        for test in tests:
            test(root)
            passed.append(test.__name__)
    if evidence_root is not None:
        prepare_output(evidence_root)
        run(evidence_root)
        return tuple(passed)
    with tempfile.TemporaryDirectory(prefix="water-check-selftest-") as temporary:
        run(Path(temporary))
    return tuple(passed)
