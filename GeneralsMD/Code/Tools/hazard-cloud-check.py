#!/usr/bin/env python3
"""Exercise production hazard clouds with private profiles, real objects and the generals executable.

Installs nothing. All cases run sequentially. A prepared game-data root and built
runtime overlay are required. --prepare-only writes fixtures and commands without
starting the game. Screenshots are retained for visual review, not treated as proof
that the appearance is correct.
"""
import argparse
import hashlib
import importlib.util
import json
import math
import os
from pathlib import Path
import re
import shutil
import socket
import struct
import subprocess
import time

from control_client import Control, ControlError

HERE = Path(__file__).resolve().parent
FLAT_MAP = "Maps/HazardCloudCheck/HazardCloudCheck.map"
PALETTES = """# Real cleanup hazards run their ordinary weapons, lifetime and damage.
30 spawn 0 PoisonFieldMedium 1 500 500
30 spawn 0 PoisonFieldUpgradedMedium 1 780 500
30 spawn 0 Chem_PoisonFieldGammaMedium 1 500 780
30 spawn 0 RadiationFieldMedium 1 780 780
45 spawn 0 AmericaInfantryRanger 4 350 500 12
60 move 0 AmericaInfantryRanger 670 500
"""
CASES = {
    "palettes": dict(map=FLAT_MAP, camera=(640, 640), shot=360, stop=420,
                     palettes=[0, 1, 2, 3], fixture=PALETTES),
    "nuclear": dict(map=FLAT_MAP, camera=(640, 640), shot=540, stop=660, palettes=[1],
                    fixture="""# A live radiation owner plus the nuclear initial plume share one support.
30 spawn 0 RadiationFieldLarge 1 640 640
31 particles 0 NukeRadiationInitial 1 640 640
"""),
    "overlap": dict(map=FLAT_MAP, camera=(640, 640), shot=360, stop=420, palettes=[0],
                    fixture="""# Intersecting hazards with different owners must retain independent volumes.
30 spawn 0 PoisonFieldMedium 1 580 640
30 spawn 0 PoisonFieldMedium 1 700 640
45 spawn 0 AmericaInfantryRanger 4 430 640 12
60 move 0 AmericaInfantryRanger 850 640
"""),
    "hillside": dict(map="Maps/Twilight Flame/Twilight Flame.map", camera=(1130, 3775),
                     shot=360, stop=420, palettes=[0, 1], fixture="""# The documented toxinslope mound falls 15–17 units at its rim.
30 spawn 0 PoisonFieldLarge 1 1075 3775
30 spawn 0 RadiationFieldMedium 1 1250 3775
"""),
    "water": dict(map="Maps/Tournament Island/Tournament Island.map", camera=(480, 1050),
                  shot=360, stop=420, palettes=[0, 1], fixture="""# Existing shoreline inspection position; modern water remains enabled.
30 spawn 0 PoisonFieldMedium 1 480 1050
30 spawn 0 RadiationFieldMedium 1 620 1050
30 spawn 0 AmericaVehicleComanche 1 600 1000
60 move 0 AmericaVehicleComanche 300 1000
"""),
    "budget": dict(map=FLAT_MAP, camera=(640, 640), shot=360, stop=420, palettes=[0],
                   fixture="30 spawn 0 PoisonFieldMedium 9 640 640 80\n"),
    "lifecycle": dict(map=FLAT_MAP, camera=(640, 640), shot=150, stop=480, palettes=[1],
                      fixture="30 spawn 0 RadiationFieldSmall 1 640 640\n"),
}
OPTIONS = """Resolution = 1280 800
StaticGameLOD = Custom
IdealStaticGameLOD = High
DynamicLOD = no
TextureReduction = 0
TextureFilter = 2
Anisotropy = 8
ShadowQuality = 1
UseShadowVolumes = yes
UseShadowVolumesForSkins = yes
UseShadowDecals = yes
ShadowsForProps = yes
ShadowsForProjectiles = yes
ShadowsForParticles = yes
ShowTrees = yes
ShowSoftWaterEdge = yes
UseCloudMap = yes
UseLightMap = yes
MaxParticleCount = 3000
ExtraAnimations = yes
SmoothMotion = no
ShowNetBox = no
VSync = no
MSAA = 0
WaterStyle = 1
"""
ERROR = re.compile(r"Validation Error|VUID-|hazard-cloud[^\n]*(?:FAIL|failed)|shader[^\n]*(?:compilation failed|compile failed)", re.I)
REPORT = re.compile(r"^hazard-cloud(?P<fallback>-fallback)?: (?P<body>frame=\d+ .*)$", re.M)
CRC = re.compile(r"HEADLESS CRC AT LIMIT:\s*(.*?)\s+at frame (\d+)")


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def write_json(path, value):
    path.write_text(json.dumps(value, indent=2) + "\n")


def records(text):
    result = []
    for match in REPORT.finditer(text):
        row = dict(re.findall(r"(\w+)=([^ ]+)", match["body"]))
        row["fallback"] = bool(match["fallback"])
        for key in ("frame", "owner", "palette", "bodies", "cache"):
            if key in row:
                row[key] = int(row[key])
        for key in ("radius", "fringe", "age", "left", "density", "formation"):
            if key in row:
                row[key] = float(row[key])
        result.append(row)
    return result


def map_crc(data):
    # Common/crc.h: rotate the 32-bit accumulator then add each byte.
    value = 0
    for byte in data:
        value = (((value << 1) | (value >> 31)) + byte) & 0xffffffff
    return value


def write_flat_map(overlay):
    spec = importlib.util.spec_from_file_location("hazard_fixture_map", HERE / "cloud_preview/generate_map.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    data = module.build_map()
    path = overlay / FLAT_MAP
    path.parent.mkdir(parents=True)
    path.write_bytes(data)
    # Release builds do not discover loose standard maps. Register only this
    # private fixture; the retail root and its MapCache.ini are never modified.
    (overlay / "Maps/MapCache.ini").write_text(f"""MapCache maps_5Chazardcloudcheck_5Chazardcloudcheck_2Emap
  fileSize = {len(data)}
  fileCRC = {map_crc(data)}
  timestampLo = 0
  timestampHi = 0
  isOfficial = yes
  isMultiplayer = yes
  numPlayers = 2
  extentMin = X:0 Y:0 Z:0
  extentMax = X:1280 Y:1280 Z:0
  nameLookupTag =
  Player_1_Start = X:180 Y:180 Z:0
  Player_2_Start = X:1100 Y:1100 Z:0
END
""")


def prepare(args, name, case, style=1, live=False, user=None, load=None):
    out = args.out / name
    out.mkdir()
    if user is None:
        user = out / "user"
        user.mkdir()
        (user / "Options.ini").write_text(OPTIONS + f"CloudStyle = {style}\n")
    overlay = out / "overlay"
    overlay.mkdir()
    for entry in args.overlay.iterdir():
        if entry.name not in ("Maps", "Scenarios"):
            (overlay / entry.name).symlink_to(entry, target_is_directory=entry.is_dir())
    (overlay / "Scenarios").mkdir()
    # The ordinary hazard FX and fade policy query real cell visibility. Turning
    # off the displayed shroud does not reveal those cells. Airborne scouts keep
    # each field visible without taking its ground-only hazard damage.
    centres = re.findall(r"^\d+ spawn 0 \S*(?:PoisonField|RadiationField)\S* \d+ (\d+) (\d+)", case["fixture"], re.M)
    vision = "".join(f"5 spawn 0 AmericaVehicleComanche 1 {x} {y}\n" for x, y in dict.fromkeys(centres))
    (overlay / "Scenarios/hazard_check.txt").write_text(vision + case["fixture"])
    if case["map"] == FLAT_MAP:
        write_flat_map(overlay)
    elif (args.overlay / "Maps").exists():
        (overlay / "Maps").symlink_to(args.overlay / "Maps", target_is_directory=True)
    prefix = "hazard_" + name.replace("-", "_") + "_"
    command = [str(args.generals), "-root", str(args.root), "-overlay", str(overlay),
               "-offscreen", "-multiInstance", "-noaudio", "-win", "-xres", "1280", "-yres", "800",
               "-msaa", "0", "-quickstart", "-noshellmap", "-nologo", "-novideo", "-noFPSLimit", "-noshroud",
               "-camera", *map(str, case["camera"]), "-logPrefix", prefix]
    if load:
        command += ["-loadsave", load]
    else:
        command += ["-map", case["map"], "-autoskirmish", "2", "-takeover", "-seed", "1",
                    "-side", "0", "FactionAmerica", "-side", "1", "FactionChina", "-observer",
                    "-scenario", "hazard_check"]
    if live:
        command += ["-control", str(args.port)]
    else:
        command += ["-turbo", "-screenshot", str(case["shot"]), "-maxframes", str(case["stop"])]
    overrides = {"ZH_USER_DATA_DIR": str(user), "ZH_UNATTENDED": "1", "ZH_AUDIO_BACKEND": "null",
                 "SDL_GPU_DRIVER": "vulkan", "ZH_OFFSCREEN_HZ": "0", "ZH_CLOUD_REPORT": "1",
                 "ZH_WATER_CAPTURE_CLOCK": "picture"}
    if args.gpu_debug:
        overrides["ZH_GPU_DEBUG"] = "1"
    if name == "water":
        overrides["ZH_WATER_PROFILE"] = "1"
    manifest = dict(case=name, command=command, environment=overrides, cloud_style=style,
                    exe_sha256=digest(args.generals), options_sha256=digest(user / "Options.ini"),
                    scenario_sha256=digest(overlay / "Scenarios/hazard_check.txt"),
                    prepared_only=args.prepare_only, user=str(user), log_prefix=prefix, load=load)
    if case["map"] == FLAT_MAP:
        manifest["map_sha256"] = digest(overlay / FLAT_MAP)
    write_json(out / "manifest.json", manifest)
    return out, user, manifest


def environment(manifest):
    env = dict(os.environ)
    for key in ("ZH_GPU_DEBUG", "ZH_TEST_SUN_SHADOW_FAILURE", "ZH_SUN_SHADOW_MODE",
                "ZH_CLOUD_PREVIEW_REBUILD_EVERY_FRAME", "ZH_GPU_TIMING", "ZH_WATER_PROFILE"):
        env.pop(key, None)
    env.update(manifest["environment"])
    return env


def finish(args, out, user, manifest, process):
    manifest["returncode"] = process.returncode
    log_name = manifest["log_prefix"] + "DebugLogFile.txt"
    # Packaged launches put logs in the isolated profile; development launches
    # may still write beside the executable. Prefer this run's private logs.
    for engine in (user / "Logs" / log_name, args.generals.parent / log_name):
        if engine.is_file():
            shutil.copyfile(engine, out / "engine.log")
            break
    text = (out / "run.log").read_text(errors="replace")
    engine_text = (out / "engine.log").read_text(errors="replace") if (out / "engine.log").exists() else ""
    manifest["diagnostics"] = records(text)
    manifest["water_profiles"] = [dict(re.findall(r"(\w+)=([^ ]+)", line)) for line in text.splitlines()
                                  if line.startswith("[water-profile] ")]
    manifest["errors"] = ERROR.findall(text)
    manifest["scenario_errors"] = [line for line in engine_text.splitlines()
        if "SCENARIO:" in line and any(term in line for term in ("refused", "no template", "cannot open", "no particle"))
        and "0 lines refused" not in line and not re.search(r"\b0 refused\b", line)]
    manifest["crc"] = [dict(crc=m[1], frame=int(m[2])) for m in CRC.finditer(engine_text)]
    manifest["screenshots"] = [dict(path=str(p), sha256=digest(p)) for p in sorted(user.rglob("sshot*.bmp"))]
    manifest["checks"] = []
    def check(name, condition):
        manifest["checks"].append(dict(name=name, passed=bool(condition)))
    check("process exited successfully", process.returncode == 0)
    check("no GPU/shader validation errors", not manifest["errors"])
    check("all production fixture actions accepted", not manifest["scenario_errors"])
    if not manifest["load"]:
        check("production scenario loaded", bool(re.search(r"SCENARIO: loaded .*?, [1-9]\d* actions, 0 lines refused", engine_text)))
    return check


def assert_clouds(check, manifest, case, style):
    rows = manifest["diagnostics"]
    active = [r for r in rows if not r["fallback"]]
    expected = set(case["palettes"])
    if style == 0:
        check("Classic emits no volumetric draws", not active)
        check("Classic retains the original hazard path", expected <= {r.get("palette") for r in rows if r.get("reason") == "classic"})
        return
    check("all requested palettes reached the production renderer", expected <= {r.get("palette") for r in active})
    check("fields are finite and bounded", bool(active) and all(
        all(math.isfinite(r.get(k, math.nan)) for k in ("radius", "fringe", "age", "left", "density", "formation"))
        and r["radius"] > 0 and r["fringe"] >= 0 and 0 <= r["density"] <= 1 and 0 <= r["formation"] <= 1
        for r in active))
    check("nuclear fades across its full footprint", all(r["formation"] == 1 for r in active if r["palette"] == 1))
    if manifest["case"] == "budget":
        check("only the expected budget fallback occurred", all(r.get("reason") == "budget" for r in rows if r["fallback"]))
        by_frame = {}
        for r in active:
            by_frame.setdefault(r["frame"], set()).add(r["owner"])
        check("eight owners admitted without exceeding the layer limit", bool(by_frame) and max(map(len, by_frame.values())) == 8)
        check("additional hazards retain the Classic fallback", any(r.get("reason") == "budget" for r in rows))
    else:
        check("all visible hazards stay volumetric", not any(r["fallback"] for r in rows))
    if manifest["case"] == "nuclear":
        check("initial and lingering nuclear systems share one owner", len({r["owner"] for r in active}) == 1)
        check("nuclear support preserves the 200-unit hazard radius", bool(active) and all(abs(r["radius"] - 200) < .01 for r in active))
    if manifest["case"] == "overlap":
        by_frame = {}
        for r in active:
            by_frame.setdefault(r["frame"], set()).add(r["owner"])
        check("overlapping owners retain distinct fields", any(len(v) >= 2 for v in by_frame.values()))
    if manifest["case"] == "hillside":
        check("uneven terrain uses the volumetric path", not any(r.get("reason") == "nonplanar" for r in rows))
    if manifest["case"] == "water":
        check("modern water rendered alongside the hazards", any(float(r.get("patches", 0)) > 0
              for r in manifest["water_profiles"]))
    if manifest["case"] == "lifecycle":
        samples = sorted({r["frame"]: r for r in active}.values(), key=lambda r: r["frame"])
        check("expired particles preserve one hazard identity", len(samples) >= 2 and len({r["owner"] for r in active}) == 1)
        check("cloud age does not restart after expiry", len(samples) >= 2 and all(b["age"] >= a["age"] for a, b in zip(samples, samples[1:])))
        peak = max(range(len(samples)), key=lambda i: samples[i]["density"]) if samples else 0
        check("short-lived nuclear field fades without rebounding", peak + 1 < len(samples)
              and all(b["density"] <= a["density"] for a, b in zip(samples[peak:], samples[peak+1:])))


def run_case(args, name, case, style=1):
    out, user, manifest = prepare(args, name, case, style)
    if args.prepare_only:
        return manifest
    start = time.monotonic()
    with (out / "run.log").open("w") as log:
        process = subprocess.Popen(manifest["command"], cwd=out, env=environment(manifest), stdout=log, stderr=subprocess.STDOUT)
        try:
            process.wait(timeout=args.timeout)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()
            manifest["timeout"] = True
    manifest["elapsed_seconds"] = time.monotonic() - start
    check = finish(args, out, user, manifest, process)
    check("reached the requested logic frame", any(r["frame"] == case["stop"] for r in manifest["crc"]))
    check("one screenshot captured", len(manifest["screenshots"]) == 1)
    assert_clouds(check, manifest, case, style)
    write_json(out / "manifest.json", manifest)
    print(name, "PASS" if all(c["passed"] for c in manifest["checks"]) else "FAIL", flush=True)
    return manifest


def connect(process, port, timeout):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if process.poll() is not None:
            raise RuntimeError("game exited before opening its control socket")
        try:
            return Control(port, timeout=5)
        except (OSError, ControlError):
            time.sleep(.1)
    raise TimeoutError("control socket did not open")


def send(game, command):
    reply = game.send(command)
    if not reply.get("ok"):
        raise RuntimeError(f"{command}: {reply}")
    return reply


def wait_frame(game, frame, timeout=45):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        status = game.status()
        if status.get("inGame") and status["frame"] >= frame:
            return status
        time.sleep(.025)
    raise TimeoutError(f"game did not reach frame {frame}")


def console(game, line):
    send(game, "key KEY_TICK")
    for char in line:
        send(game, "key KEY_SPACE" if char == " " else "key KEY_" + char.upper())
    send(game, "key KEY_ENTER")
    send(game, "key KEY_TICK")


def capture(game, user, target, timeout=10):
    before = {p: p.stat().st_mtime_ns for p in user.rglob("sshot*.bmp")}
    send(game, "screenshot")
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        new = [p for p in user.rglob("sshot*.bmp") if p.stat().st_mtime_ns != before.get(p)]
        if new:
            path = sorted(new)[0]
            with path.open("rb") as bitmap:
                header = bitmap.read(6)
            if len(header) == 6 and header[:2] == b"BM" and path.stat().st_size == struct.unpack("<I", header[2:])[0]:
                shutil.copyfile(path, target)
                return
        time.sleep(.05)
    raise TimeoutError("requested screenshot did not arrive")


def live_session(args, name, case, user=None, load=None):
    out, user, manifest = prepare(args, name, case, live=True, user=user, load=load)
    if args.prepare_only:
        return manifest, user
    game = None
    with (out / "run.log").open("w") as log:
        process = subprocess.Popen(manifest["command"], cwd=out, env=environment(manifest), stdout=log, stderr=subprocess.STDOUT)
        try:
            game = connect(process, args.port, args.timeout)
            status = wait_frame(game, 120 if not load else 1)
            start = status["frame"]
            wait_frame(game, start + 90)
            if load:
                capture(game, user, out / "restored.bmp")
                manifest["restored_frame"] = game.status()["frame"]
            else:
                capture(game, user, out / "volumetric-before.bmp")
                console(game, "set cloudstyle 0")
                classic = game.status()["frame"]
                wait_frame(game, classic + 150)
                capture(game, user, out / "classic.bmp")
                console(game, "set cloudstyle 1")
                modern = game.status()["frame"]
                wait_frame(game, modern + 150)
                capture(game, user, out / "volumetric-after.bmp")
                manifest["classic_interval"] = [classic + 30, modern - 30]
                manifest["modern_after"] = modern + 30
                manifest["save_frame"] = game.status()["frame"]
                send(game, "save hazard-cloud-check")
                manifest["save_files"] = [str(p) for p in user.rglob("hazard-cloud-check.sav")]
        except (OSError, RuntimeError, TimeoutError) as exc:
            manifest["exception"] = str(exc)
        finally:
            if game:
                try:
                    send(game, "quit")
                except (OSError, ControlError):
                    pass
                game.close()
            try:
                process.wait(timeout=10)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
    check = finish(args, out, user, manifest, process)
    check("control actions completed", "exception" not in manifest)
    active = [r for r in manifest["diagnostics"] if not r["fallback"]]
    if load:
        check("save restored live hazard objects", set(case["palettes"]) <= {r["palette"] for r in active})
        check("restored screenshot captured", (out / "restored.bmp").is_file())
    else:
        first, last = manifest.get("classic_interval", [0, 0])
        middle = [r for r in manifest["diagnostics"] if first <= r["frame"] <= last]
        check("live Classic removes volume draws", last > first and not any(not r["fallback"] for r in middle))
        check("live Classic preserves the original hazard path", any(r.get("reason") == "classic" for r in middle))
        check("switching back restores active hazards", any(r["frame"] >= manifest.get("modern_after", math.inf) for r in active))
        check("live choice persisted", bool(re.search(r"^CloudStyle\s*=\s*1\s*$", (user / "Options.ini").read_text(), re.M)))
        check("save file written", bool(manifest.get("save_files")))
    write_json(out / "manifest.json", manifest)
    print(name, "PASS" if all(c["passed"] for c in manifest["checks"]) else "FAIL", flush=True)
    return manifest, user


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("generals", "root", "overlay", "out"):
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument("--case", choices=list(CASES), action="append", help="repeat to select cases; default all")
    parser.add_argument("--live", action="store_true", help="also test live Classic switching and save/reload")
    parser.add_argument("--crc-pair", action="store_true", help="repeat palettes in Classic and require equal game-state CRC")
    parser.add_argument("--gpu-debug", action="store_true")
    parser.add_argument("--prepare-only", action="store_true")
    parser.add_argument("--timeout", type=int, default=180)
    parser.add_argument("--port", type=int)
    args = parser.parse_args()
    if args.timeout <= 0 or (args.port is not None and not 0 < args.port < 65536):
        parser.error("timeout must be positive and port must be between 1 and 65535")
    for name in ("generals", "root", "overlay", "out"):
        setattr(args, name, getattr(args, name).resolve())
    if not args.generals.is_file() or not args.root.is_dir() or not args.overlay.is_dir():
        parser.error("the executable, prepared data root and overlay must already exist")
    if args.port is None:
        with socket.socket() as probe:
            probe.bind(("127.0.0.1", 0))
            args.port = probe.getsockname()[1]
    args.out.mkdir(parents=True, exist_ok=False)
    results = []
    for name in dict.fromkeys(args.case or CASES):
        results.append(run_case(args, name, CASES[name]))
    if args.crc_pair:
        modern = next((r for r in results if r["case"] == "palettes"), None)
        if modern is None:
            modern = run_case(args, "palettes", CASES["palettes"])
            results.append(modern)
        classic = run_case(args, "palettes-classic", CASES["palettes"], 0)
        if not args.prepare_only:
            classic["checks"].append(dict(name="Classic and Volumetric preserve the same game-state CRC", passed=bool(modern["crc"]) and modern["crc"] == classic["crc"]))
            write_json(args.out / "palettes-classic/manifest.json", classic)
        results.append(classic)
    if args.live:
        live, user = live_session(args, "live", CASES["palettes"])
        results.append(live)
        if args.prepare_only or live.get("save_files"):
            loaded, _ = live_session(args, "load", CASES["palettes"], user, "hazard-cloud-check")
            results.append(loaded)
    summary = dict(prepared_only=args.prepare_only, results=results)
    summary["passed"] = args.prepare_only or all(all(c["passed"] for c in r.get("checks", [])) for r in results)
    write_json(args.out / "summary.json", summary)
    print(json.dumps(dict(summary=str(args.out / "summary.json"), passed=summary["passed"], prepared_only=args.prepare_only)))
    return 0 if summary["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
