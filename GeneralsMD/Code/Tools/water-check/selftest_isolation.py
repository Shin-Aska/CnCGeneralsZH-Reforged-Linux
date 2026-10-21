from __future__ import annotations

import json
import os
import subprocess
import sys
import time
from pathlib import Path

from commands import build_command
from isolation import launch_isolated, prepare_output
from models import Platform
from runtime import (
    load_game_data_fragments,
    materialize_farm,
    materialize_overlay,
    materialize_windows_inputs,
    resolve_game_data_base,
    sha256,
    stage_scene_game_data,
    stage_windows_user_map,
)
from schema import load_scenes
from suites import _cleanup_game_data, _suite_outcome


class IsolationSelfTestFailure(RuntimeError):
    pass


def _require(condition: bool, message: str) -> None:
    if not condition:
        raise IsolationSelfTestFailure(message)


def test_master_isolation(root: Path) -> None:
    master = root / "master"
    master.mkdir()
    (master / "payload.big").write_bytes(b"read-only-resource")
    (master / "generals.exe").write_bytes(b"executable-copy")
    overlay_master = root / "overlay-master"
    overlay_master.mkdir()
    (overlay_master / "fixture.ini").write_bytes(b"fixture-copy")
    inventory_before = tuple(sorted(path.relative_to(master) for path in master.rglob("*") if path.is_file()))
    hashes_before = tuple((path, sha256(master / path)) for path in inventory_before)
    farm = root / "private-farm"
    private_overlay = root / "private-overlay"
    materialize_farm(master, farm)
    materialize_overlay(overlay_master, private_overlay)
    write_script = (
        "from pathlib import Path; "
        "Path('Ping.txt').write_text('telemetry'); "
        "Path('payload.big').write_bytes(b'changed-private-payload'); "
        "Path('generals.exe').write_bytes(b'changed-private-copy'); "
        "Path(__import__('sys').argv[1]).write_bytes(b'changed-private-overlay')"
    )
    call = root / "master-isolation-call"
    call.mkdir()
    result = launch_isolated(
        [sys.executable, "-c", write_script, str((private_overlay / "fixture.ini").resolve())],
        call, 5.0, {}, farm,
    )
    _require(result.returncode == 0, "farm mutation child failed")
    inventory_after = tuple(sorted(path.relative_to(master) for path in master.rglob("*") if path.is_file()))
    hashes_after = tuple((path, sha256(master / path)) for path in inventory_after)
    _require(inventory_after == inventory_before, "private farm changed master inventory")
    _require(hashes_after == hashes_before, "private farm changed master file bytes")
    _require((overlay_master / "fixture.ini").read_bytes() == b"fixture-copy", "private overlay changed source fixture")


def test_windows_input_materialization(root: Path) -> None:
    master = root / "windows-master"
    master.mkdir()
    (master / "payload.big").write_bytes(b"immutable-payload")
    external_executable = root / "external-generals.exe"
    external_executable.write_bytes(b"selected-executable")
    fixture = root / "windows-fixture"
    map_path = fixture / "Maps" / "Water Validation" / "Water Validation.map"
    carrier_path = fixture / "Data" / "INI" / "Object" / "WaterCaptureCarrier.ini"
    map_path.parent.mkdir(parents=True)
    carrier_path.parent.mkdir(parents=True)
    map_path.write_bytes(b"authored-map")
    (map_path.parent / "map.ini").write_bytes(b"WaterHeight = 10\n")
    carrier_path.write_bytes(b"Object WaterCaptureCarrier\nEnd\n")
    source_hashes = {
        path.relative_to(fixture): sha256(path)
        for path in (map_path, map_path.parent / "map.ini", carrier_path)
    }
    master_hash = sha256(master / "payload.big")
    farm = root / "windows-farm"
    materialize_farm(master, farm)
    executable, child_hashes = materialize_windows_inputs(external_executable, fixture, farm)
    _require(executable == farm / "external-generals.exe", "Windows executable was not placed at farm root")
    _require(sha256(executable) == sha256(external_executable), "Windows farm executable bytes changed")
    _require(child_hashes == {path.as_posix(): digest for path, digest in source_hashes.items()}, "child fixture hashes differ")
    _require(all(sha256(farm / path) == digest for path, digest in source_hashes.items()), "farm fixture bytes changed")
    _require(sha256(master / "payload.big") == master_hash, "Windows staging changed the master")
    _require(all(sha256(fixture / path) == digest for path, digest in source_hashes.items()), "Windows staging changed fixture source")

    user = root / "windows-user"
    user.mkdir()
    staged_map, staged_hashes = stage_windows_user_map(
        "Maps/Water Validation/Water Validation.map", fixture, user
    )
    expected_map = user / "Maps" / "Water Validation" / "Water Validation.map"
    expected_ini = user / "Maps" / "Water Validation" / "map.ini"
    _require(staged_map == expected_map.resolve(), "Windows user map path was not absolute and private")
    _require(expected_map.read_bytes() == b"authored-map", "Windows user map bytes changed")
    _require(expected_ini.read_bytes() == b"WaterHeight = 10\n", "adjacent map.ini was not staged")
    _require(
        staged_hashes == {
            "Maps/Water Validation/Water Validation.map": sha256(map_path),
            "Maps/Water Validation/map.ini": sha256(fixture / "Maps/Water Validation/map.ini"),
        },
        "Windows user map manifest is incomplete",
    )


def test_game_data_materialization(root: Path) -> None:
    immutable = root / "game-data-master"
    member_source = root / "game-data-member"
    member = member_source / "Data" / "INI" / "GameData.ini"
    member.parent.mkdir(parents=True)
    base_bytes = b"GameData\r\n  MaxShellScreens = 8\r\nEnd\r\n"
    member.write_bytes(base_bytes)
    immutable.mkdir()
    tool = Path(__file__).resolve().parent.parent / "bigfile.py"
    subprocess.run(
        [sys.executable, str(tool), "pack", str(immutable / "INIZH.big"), str(member_source)],
        check=True,
        stdout=subprocess.DEVNULL,
    )
    expected_hash = sha256(member)
    resolved = resolve_game_data_base(immutable, expected_hash, len(base_bytes))
    _require(resolved.data == base_bytes, "effective BIG member bytes changed")
    _require(resolved.source_kind == "inizh_big_member", "wrong effective GameData source selected")
    _require(resolved.member == "Data\\INI\\GameData.ini", "wrong BIG member selected")

    fixtures = root / "game-data-fixtures"
    fixtures.mkdir()
    fragment = fixtures / "WaterValidationGameData.fragment.ini"
    fragment.write_bytes(
        b"GameData\n  VertexWaterAvailableMaps4 = \"Maps\\Water Validation\\Water Validation.map\"\nEnd\n"
    )
    source_hash = sha256(immutable / "INIZH.big")
    fragment_hash = sha256(fragment)
    farm = root / "game-data-farm"
    farm.mkdir()
    catalog = load_scenes(Path(__file__).resolve().parent / "scenes.json")
    retained = load_game_data_fragments(catalog.scenes, fixtures)
    authored_fragment = retained["water-fixture"]
    receipt = stage_scene_game_data(farm, authored_fragment, resolved)
    target = farm / "Data" / "INI" / "GameData.ini"
    normalized_fragment = (
        b"GameData\r\n  VertexWaterAvailableMaps4 = \"Maps\\Water Validation\\Water Validation.map\"\r\nEnd\r\n"
    )
    _require(target.read_bytes() == base_bytes + normalized_fragment, "generated GameData order/boundary is wrong")
    _require(receipt["generated_sha256"] == sha256(target), "generated GameData hash receipt is wrong")
    _require(receipt["base_sha256"] == expected_hash, "base hash receipt is wrong")
    _require(receipt["fragment_sha256"] == fragment_hash, "fragment hash receipt is wrong")

    stock = stage_scene_game_data(farm, None, resolved)
    _require(not target.exists(), "stock scene inherited authored loose GameData")
    _require(stock["mode"] == "archive_base_no_loose_override", "stock archive mode is untruthful")
    stage_scene_game_data(farm, authored_fragment, resolved)
    stage_scene_game_data(farm, None, resolved)
    _require(not target.exists(), "stock/authored reuse left stale GameData")

    loose_root = root / "game-data-loose-master"
    loose = loose_root / "Data" / "INI" / "GameData.ini"
    loose.parent.mkdir(parents=True)
    loose_bytes = b"GameData\r\n  MaxShellScreens = 9\r\nEnd\r\n"
    loose.write_bytes(loose_bytes)
    shutil_archive = loose_root / "INIZH.big"
    shutil_archive.write_bytes((immutable / "INIZH.big").read_bytes())
    loose_base = resolve_game_data_base(loose_root, expected_hash, len(base_bytes))
    _require(loose_base.source_kind == "loose_override", "effective loose override was ignored")
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_bytes(b"stale-authored")
    stage_scene_game_data(farm, None, loose_base)
    _require(target.read_bytes() == loose_bytes, "stock scene did not restore effective loose base")

    try:
        resolve_game_data_base(immutable, "0" * 64, len(base_bytes))
    except ValueError:
        pass
    else:
        raise IsolationSelfTestFailure("wrong GameData source hash was accepted")
    bad_fragment = fixtures / "bad.ini"
    bad_fragment.write_bytes(b"NotGameData\nEnd\n")
    before = target.read_bytes()
    try:
        bad_catalog_data = json.loads((Path(__file__).resolve().parent / "scenes.json").read_text(encoding="utf-8"))
        next(scene for scene in bad_catalog_data["scenes"] if scene["id"] == "water-fixture")["game_data_append"] = bad_fragment.name
        bad_catalog_path = root / "bad-game-data-scenes.json"
        bad_catalog_path.write_text(json.dumps(bad_catalog_data), encoding="utf-8")
        bad_catalog = load_scenes(bad_catalog_path)
        load_game_data_fragments(bad_catalog.scenes, fixtures)
    except ValueError:
        pass
    else:
        raise IsolationSelfTestFailure("malformed GameData fragment was accepted")
    _require(target.read_bytes() == before, "malformed fragment changed caller farm")

    interruption = target.with_name("GameData.ini.water-check.tmp")
    interruption.write_bytes(b"foreign-interruption-marker")
    try:
        stage_scene_game_data(farm, authored_fragment, loose_base)
    except FileExistsError:
        pass
    else:
        raise IsolationSelfTestFailure("pre-existing staging temp was overwritten")
    _require(target.read_bytes() == before, "interrupted staging changed current GameData")
    _require(interruption.read_bytes() == b"foreign-interruption-marker", "foreign staging temp was removed")
    _require(sha256(immutable / "INIZH.big") == source_hash, "staging changed immutable archive")
    _require(sha256(fragment) == fragment_hash, "staging changed committed fragment")

    fragment.write_bytes(b"changed-after-preflight")
    interruption.unlink()
    stage_scene_game_data(farm, authored_fragment, loose_base)
    _require(
        target.read_bytes() == loose_bytes + normalized_fragment,
        "staging reread a fragment changed after preflight",
    )

    fragment.unlink()
    try:
        load_game_data_fragments(catalog.scenes, fixtures)
    except FileNotFoundError:
        pass
    else:
        raise IsolationSelfTestFailure("missing GameData fragment passed preflight")

    cleanup_farm = root / "cleanup-success-farm"
    cleanup_farm.mkdir()
    stage_scene_game_data(cleanup_farm, authored_fragment, resolved)
    cleanup = _cleanup_game_data(cleanup_farm, resolved)
    _require(cleanup["classification"] == "passed", "suite cleanup rejected a valid restore")
    _require(
        not (cleanup_farm / "Data/INI/GameData.ini").exists(),
        "suite cleanup left the authored override behind",
    )

    blocked_farm = root / "cleanup-failure-farm"
    blocked_target = blocked_farm / "Data" / "INI" / "GameData.ini"
    blocked_target.mkdir(parents=True)
    failed_cleanup = _cleanup_game_data(blocked_farm, resolved)
    _require(failed_cleanup["classification"] == "failed", "cleanup filesystem failure was hidden")
    classification, exit_code = _suite_outcome(True, "prior scene failure", failed_cleanup)
    _require(classification == "failed" and exit_code == 1, "combined failures produced a pass")
    _require(bool(failed_cleanup.get("failure_reason")), "cleanup failure reason was not retained")

    interrupted_farm = root / "cleanup-interrupted-farm"
    interrupted_farm.mkdir()
    stage_scene_game_data(interrupted_farm, authored_fragment, resolved)
    interrupted_call = root / "cleanup-interrupted-call"
    interrupted_call.mkdir()
    interrupted = launch_isolated(
        [sys.executable, "-c", "raise SystemExit(7)"],
        interrupted_call,
        5.0,
        {},
    )
    interrupted_cleanup = _cleanup_game_data(interrupted_farm, resolved)
    interrupted_outcome = _suite_outcome(interrupted.returncode != 0, None, interrupted_cleanup)
    _require(interrupted.returncode == 7, "real failure child did not fail")
    _require(interrupted_outcome == ("failed", 1), "child failure produced a successful suite outcome")
    _require(
        not (interrupted_farm / "Data/INI/GameData.ini").exists(),
        "child failure bypassed suite cleanup",
    )

    clean_outcome = _suite_outcome(False, None, cleanup)
    _require(clean_outcome == ("passed", 0), "clean suite outcome was not successful")


def test_command_lifecycle(root: Path) -> None:
    catalog = load_scenes(Path(__file__).resolve().parent / "scenes.json")
    ordinary = next(scene for scene in catalog.scenes if not scene.requires_motion)
    motion = next(scene for scene in catalog.scenes if scene.requires_motion)
    ordinary_command = build_command(ordinary, root / "generals.exe", root, root / "overlay", 0, Platform.WINDOWS)
    private_map = (root / "user/Maps/Water Validation/Water Validation.map").resolve()
    motion_command = build_command(
        motion,
        root / "generals.exe",
        root,
        root / "overlay",
        43123,
        Platform.WINDOWS,
        map_path=str(private_map),
    )
    _require("-control" not in ordinary_command, "ordinary screenshot command incorrectly owns a control socket")
    _require(
        motion_command[motion_command.index("-control") + 1] == "43123",
        "motion command omitted its controlled termination port",
    )
    _require(
        motion_command[motion_command.index("-map") + 1] == str(private_map),
        "Windows authored command omitted its absolute private user map path",
    )


def test_process_isolation(root: Path) -> None:
    existing = root / "existing"
    existing.mkdir()
    try:
        prepare_output(existing)
    except FileExistsError:
        rejected = True
    else:
        rejected = False
    _require(rejected, "pre-existing output root was accepted")

    marker = "water-check-parent-value"
    prior = os.environ.get("ZH_WATER_CAPTURE_CLOCK")
    prior_driver = os.environ.get("ZH_DX11_DRIVER")
    os.environ["ZH_WATER_CAPTURE_CLOCK"] = marker
    os.environ["ZH_DX11_DRIVER"] = "must-not-reach-child"
    try:
        for mode, seconds, timeout, expected_timeout in (
            ("success", 0.0, 5.0, False),
            ("failure", 0.0, 5.0, False),
            ("timeout", 30.0, 0.15, True),
        ):
            out = root / mode
            prepare_output(out)
            script = (
                "import json,os,sys,time; "
                "json.dump({'clock':os.getenv('ZH_WATER_CAPTURE_CLOCK'),'driver':os.getenv('ZH_DX11_DRIVER')},open(sys.argv[2],'w')); "
                f"time.sleep({seconds}); "
                "sys.exit(7 if sys.argv[1] == 'failure' else 0)"
            )
            child_environment = (out / "child-environment.json").resolve()
            result = launch_isolated(
                [sys.executable, "-c", script, mode, str(child_environment)], out, timeout,
                {"ZH_WATER_CAPTURE_CLOCK": "picture"},
            )
            _require(result.timed_out is expected_timeout, f"wrong timeout result for {mode}")
            _require(os.environ.get("ZH_WATER_CAPTURE_CLOCK") == marker, f"caller environment changed on {mode}")
            observed = json.loads(child_environment.read_text(encoding="utf-8"))
            _require(observed == {"clock": "picture", "driver": None}, f"child environment was contaminated on {mode}")
            if result.pid > 0:
                time.sleep(0.02)
                try:
                    os.kill(result.pid, 0)
                except OSError:
                    alive = False
                else:
                    alive = True
                _require(not alive, f"child {result.pid} survived {mode}")
    finally:
        if prior is None:
            os.environ.pop("ZH_WATER_CAPTURE_CLOCK", None)
        else:
            os.environ["ZH_WATER_CAPTURE_CLOCK"] = prior
        if prior_driver is None:
            os.environ.pop("ZH_DX11_DRIVER", None)
        else:
            os.environ["ZH_DX11_DRIVER"] = prior_driver
