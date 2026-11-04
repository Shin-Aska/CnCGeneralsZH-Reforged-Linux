#!/usr/bin/env python3
"""Exercise production hazard clouds with private profiles, real objects and the generals executable.

Installs nothing. All cases run sequentially. A prepared game-data root and built
runtime overlay are required. --prepare-only writes fixtures and commands without
starting the game. Screenshots are retained for visual review, not treated as proof
that the appearance is correct.
"""
import argparse
import csv
import hashlib
import importlib.util
import json
import math
import os
from pathlib import Path
import re
import shutil
import socket
import statistics
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


def load_fixture(templates, overlap=False, successive=False):
    # Unique centres avoid ambiguity in the legacy owner lookup. Simultaneous
    # births retain the original compact load fixtures. During successive births,
    # startup HazardFieldCoreWeapon can clean older owners within its 10-unit
    # blast plus their 40-unit object geometry, so keep those centres farther apart.
    # 56 also keeps the entire growth fixture inside the camera's collection box.
    spacing = 56 if successive else 6 if overlap else 36
    lines = ["# 128 real owners; ordinary cleanup, damage and lifetimes remain enabled."]
    for index in range(128):
        x = 640 + (index % 16 - 7.5) * spacing
        y = 640 + (index // 16 - 3.5) * spacing
        template = templates[index % len(templates)]
        frame = 30 + index if successive else 30
        lines.append(f"{frame} spawn 0 {template} 1 {x:g} {y:g}")
    return "\n".join(lines) + "\n"


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
                   owners=9, fixture="30 spawn 0 PoisonFieldMedium 9 640 640 80\n"),
    "gamma-budget": dict(map=FLAT_MAP, camera=(640, 640), shot=360, stop=420, palettes=[3],
                         owners=9, fixture="30 spawn 0 Chem_PoisonFieldGammaLarge 9 640 640 80\n"),
    "cache-budget": dict(map=FLAT_MAP, camera=(640, 640), shot=360, stop=420, palettes=[1],
                         owners=7, fixture="30 spawn 0 RadiationFieldLarge 7 640 640 40\n"),
    "lifecycle": dict(map=FLAT_MAP, camera=(640, 640), shot=150, stop=480, palettes=[1],
                      fixture="30 spawn 0 RadiationFieldSmall 1 640 640\n"),
    "domain-growth": dict(map=FLAT_MAP, camera=(640, 640), shot=660, stop=720,
                          palettes=[0, 1, 3], owners=3, shared=True, fixture="""# New supports grow the domain while an earlier source and its wake remain alive.
30 spawn 0 PoisonFieldMedium 1 430 640
45 spawn 0 AmericaInfantryRanger 4 320 640 12
60 move 0 AmericaInfantryRanger 560 640
150 spawn 0 Chem_PoisonFieldGammaMedium 1 850 640
270 spawn 0 RadiationFieldSmall 1 640 850
"""),
    "continuous-growth": dict(map=FLAT_MAP, camera=(640, 640), shot=360, stop=420,
                              palettes=[0], owners=128, shared=True,
                              fixture=load_fixture(("PoisonFieldMedium",), successive=True)),
}
for kind, templates, palettes in (
        ("mixed", ("PoisonFieldMedium", "PoisonFieldUpgradedMedium",
                   "Chem_PoisonFieldGammaMedium", "RadiationFieldMedium"), [0, 1, 2, 3]),
        ("nuclear", ("RadiationFieldMedium",), [1]),
        ("gamma", ("Chem_PoisonFieldGammaMedium",), [3])):
    for layout in ("distinct", "overlap"):
        CASES[f"load-128-{kind}-{layout}"] = dict(
            map=FLAT_MAP, camera=(640, 640), shot=360, stop=420,
            palettes=palettes, owners=128, shared=True,
            fixture=load_fixture(templates, overlap=layout == "overlap"))
OPTIONS = """Resolution = 1280 800
ClassicGraphics = no
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
SHARED_REPORT = re.compile(r"^hazard-cloud-shared: (?P<body>frame=\d+ .*)$", re.M)
CRC = re.compile(r"HEADLESS CRC AT LIMIT:\s*(.*?)\s+at frame (\d+)")


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def log_prefix(name):
    # The engine retains at most 30 prefix characters. Hash long names rather
    # than silently colliding after truncation or looking for a nonexistent log.
    prefix = "hazard_" + name.replace("-", "_") + "_"
    if len(prefix) > 30:
        prefix = prefix[:20] + "_" + hashlib.sha256(name.encode()).hexdigest()[:8] + "_"
    return prefix


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


def shared_records(text):
    result = []
    for match in SHARED_REPORT.finditer(text):
        row = dict(re.findall(r"([\w-]+)=([^\s]+)", match["body"]))
        for key in ("frame", "sources", "active", "baked", "pending", "max-slices", "cache"):
            if key in row:
                row[key] = int(row[key])
        if "age" in row:
            row["age"] = float(row["age"])
        result.append(row)
    return result


def timing_records(path):
    """Validate whole-engine frame evidence before calculating statistics."""
    with path.open(newline="") as stream:
        reader = csv.DictReader(stream, strict=True)
        fields = reader.fieldnames or []
        if not {"logic_frame", "frame_ms"} <= set(fields) or len(fields) != len(set(fields)):
            raise ValueError("timing CSV requires unique logic_frame and frame_ms columns")
        frames, values = [], []
        for row in reader:
            if None in row or any(value is None for value in row.values()):
                raise ValueError("timing CSV has an inconsistent column count")
            frames.append(int(row["logic_frame"]))
            value = float(row["frame_ms"])
            if not math.isfinite(value) or value <= 0:
                raise ValueError("timing CSV requires finite positive frame_ms")
            values.append(value)
    values.sort()
    result = dict(samples=len(values), basis="whole-engine present intervals; no serialized GPU fences")
    if values:
        result.update(mean_ms=statistics.mean(values), median_ms=statistics.median(values),
                      p95_ms=values[math.ceil(len(values) * .95) - 1],
                      p99_ms=values[math.ceil(len(values) * .99) - 1], max_ms=values[-1])
    return frames, result


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
    centres = re.findall(r"^\d+ spawn 0 \S*(?:PoisonField|RadiationField)\S* \d+ ([\d.]+) ([\d.]+)", case["fixture"], re.M)
    vision = "".join(f"5 spawn 0 AmericaVehicleComanche 1 {x} {y}\n" for x, y in dict.fromkeys(centres))
    (overlay / "Scenarios/hazard_check.txt").write_text(vision + case["fixture"])
    if case["map"] == FLAT_MAP:
        write_flat_map(overlay)
    elif (args.overlay / "Maps").exists():
        (overlay / "Maps").symlink_to(args.overlay / "Maps", target_is_directory=True)
    prefix = log_prefix(name)
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
    timing_window = [240, 359] if args.timing and name.startswith("load-128-") and not live else None
    if timing_window:
        overrides["ZH_GPU_TIMING_LOGIC"] = ",".join(map(str, timing_window))
        overrides["ZH_GPU_TIMING_CSV"] = str(out / "frames.csv")
        overrides["ZH_GPU_TIMING_SYNC"] = "0"
    if name == "water":
        overrides["ZH_WATER_PROFILE"] = "1"
    manifest = dict(case=name, command=command, environment=overrides, cloud_style=style,
                    exe_sha256=digest(args.generals), options_sha256=digest(user / "Options.ini"),
                    scenario_sha256=digest(overlay / "Scenarios/hazard_check.txt"),
                    prepared_only=args.prepare_only, user=str(user), log_prefix=prefix, load=load,
                    timing_window=timing_window)
    if case["map"] == FLAT_MAP:
        manifest["map_sha256"] = digest(overlay / FLAT_MAP)
    write_json(out / "manifest.json", manifest)
    return out, user, manifest


def environment(manifest):
    env = dict(os.environ)
    for key in list(env):
        if key.startswith("ZH_GPU_TIMING"):
            env.pop(key)
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
    manifest["shared_diagnostics"] = shared_records(text)
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
    if manifest.get("timing_window"):
        timing_path = out / "frames.csv"
        check("whole-engine timing CSV captured", timing_path.is_file())
        if timing_path.is_file():
            try:
                frames, manifest["timing"] = timing_records(timing_path)
            except (OSError, ValueError, csv.Error) as exc:
                manifest["timing_error"] = str(exc)
                check("timing CSV contains valid finite positive samples", False)
            else:
                first, last = manifest["timing_window"]
                check("timing covers every steady-state logic frame exactly once",
                      frames == list(range(first, last + 1)))
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
    check("all visible hazards stay volumetric", not any(r["fallback"] for r in rows))
    if case.get("owners"):
        owners = {r["owner"] for r in active}
        check("every requested owner reached the modern path", len(owners) == case["owners"])
        check("source saturation never selects a budget fallback", not any(
            r.get("reason") == "budget" for r in rows))
    shared = manifest.get("shared_diagnostics", [])
    if case.get("shared") or manifest["case"] in ("budget", "gamma-budget", "cache-budget"):
        check("SDL shared-volume diagnostics are present", bool(shared))
        check("all owners coexist in an active shared snapshot", bool(shared) and
              max(r.get("active", 0) for r in shared) >= case["owners"])
        check("all owners reach a published shared snapshot", bool(shared) and
              max(r.get("baked", 0) for r in shared) >= case["owners"])
    if shared:
        check("shared-volume cache stays within 192 MiB", all(
            0 < r.get("cache", 0) <= 192 * 1024 * 1024 for r in shared))
        check("injection work stays within eight slices per display frame", all(
            0 <= r.get("max-slices", -1) <= 8 for r in shared))
        check("published snapshot ages are finite and nonnegative", all(
            math.isfinite(r.get("age", math.nan)) and r["age"] >= 0 for r in shared))
        check("shared snapshot counts are consistent", all(
            0 <= r.get("active", -1) <= r.get("sources", -1)
            and r.get("baked", -1) >= 0 and 0 <= r.get("pending", -1) <= 24
            for r in shared))
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
    if manifest["case"] == "domain-growth":
        first = [r for r in active if r["palette"] == 0]
        check("original source age survives later domain growth", any(r["frame"] < 150 for r in first)
              and any(r["frame"] >= 300 and r["age"] >= 3 for r in first))
        check("domain expansion publishes the late source", any(
            r["frame"] >= 300 and r.get("baked", 0) >= 3 for r in shared))
    if manifest["case"] == "continuous-growth":
        # Scenario frames use the retail 30 Hz clock and scale to the engine's
        # 60 Hz logic clock: arrivals30..157 become60..314. Inspect a window
        # after initial emission but before the final arrival/publication.
        during_growth = [r for r in shared if 120 <= r["frame"] <= 300]
        check("partial snapshots publish during continuous source growth", any(
            0 < r.get("baked", 0) < 128 for r in during_growth))
        check("continuous growth publishes the complete final source set", any(
            r["frame"] >= 330 and r.get("baked", 0) == 128 for r in shared))


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
    shared = manifest["shared_diagnostics"]
    if load:
        check("save restored live hazard objects", set(case["palettes"]) <= {r["palette"] for r in active})
        check("restored screenshot captured", (out / "restored.bmp").is_file())
        check("save reload publishes restored sources in the shared volume", any(
            r.get("baked", 0) >= len(case["palettes"]) for r in shared))
    else:
        first, last = manifest.get("classic_interval", [0, 0])
        middle = [r for r in manifest["diagnostics"] if first <= r["frame"] <= last]
        check("live Classic removes volume draws", last > first and not any(not r["fallback"] for r in middle))
        check("live Classic preserves the original hazard path", any(r.get("reason") == "classic" for r in middle))
        check("switching back restores active hazards", any(r["frame"] >= manifest.get("modern_after", math.inf) for r in active))
        check("style reset republishes all active shared sources", any(
            r["frame"] >= manifest.get("modern_after", math.inf)
            and r.get("baked", 0) >= len(case["palettes"]) for r in shared))
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
    parser.add_argument("--timing", action="store_true", help="retain whole-engine frames 240..359 CSV/statistics for 128-owner cases")
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
