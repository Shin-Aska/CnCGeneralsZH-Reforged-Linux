#!/usr/bin/env python3
"""Exercise the real Classic-water splash and cosmetic building-collapse consumers.

Uses read-only game data and an existing overlay, with a private profile/map per
run. Installs nothing. --prepare-only records fixtures and commands without
starting a renderer. On/off comparisons report image differences and require
identical simulation CRCs; images still need visual review. No production test
switch or capability override is used.
"""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import subprocess
import time

HERE = Path(__file__).resolve().parent
MAP = "Maps/PortableCosmeticsCheck/PortableCosmeticsCheck.map"
OPTIONS = """Resolution = 960 600
ClassicGraphics = no
WaterSplashes = yes
StructureCollapse = yes
GroundMarks = no
StaticGameLOD = Custom
IdealStaticGameLOD = High
DynamicLOD = no
TextureReduction = 0
ShadowQuality = 0
UseShadowVolumes = yes
UseShadowDecals = yes
ShowTrees = yes
TreeSway = no
MaxParticleCount = 3000
SmoothMotion = no
ShowNetBox = no
VSync = no
MSAA = 0
WaterStyle = 0
CloudStyle = 0
LightingStyle = 0
"""
# The same ordinary death FX reaches FXList::doFXPosNow in every impact case.
# A true TerrainScorch nugget supplies its blast radius; nothing calls splashInWater
# directly. The map's integer water plane is at 37, and the deep bed is at 10.
IMPACT_INI = """FXList FX_PortableCosmeticsImpact
 TerrainScorch
  Type = SCORCH_1
  Radius = 15
 End
End
Object PortableCosmeticsImpact
 EditorSorting = SYSTEM
 KindOf = IMMOBILE NO_COLLIDE
 VisionRange = 0
 ShroudClearingRange = 0
 Geometry = SPHERE
 GeometryMajorRadius = 1
 GeometryIsSmall = Yes
 AddModule ModuleTag_PortableFixture
  Draw = W3DModelDraw ModuleTag_Draw
   DefaultConditionState
    Model = None
   End
  End
  Body = ActiveBody ModuleTag_Body
   MaxHealth = 1
   InitialHealth = 1
  End
  Behavior = LifetimeUpdate ModuleTag_Life
   MinLifetime = 2000
   MaxLifetime = 2000
  End
  Behavior = FXListDie ModuleTag_FX
   DeathFX = FX_PortableCosmeticsImpact
   OrientToObject = No
  End
  Behavior = DestroyDie ModuleTag_Destroy
  End
 End
End
"""
BUILDING_INI = """Object AmericaPowerPlant
 AddModule ModuleTag_PortableFixtureLife
  Behavior = LifetimeUpdate ModuleTag_PortableLife
   MinLifetime = 4000
   MaxLifetime = 4000
  End
 End
End
Object PortableCosmeticsInvisible
 EditorSorting = SYSTEM
 KindOf = STRUCTURE IMMOBILE NO_COLLIDE
 VisionRange = 0
 ShroudClearingRange = 0
 Geometry = BOX
 GeometryMajorRadius = 20
 GeometryMinorRadius = 20
 GeometryHeight = 50
 AddModule ModuleTag_PortableInvisible
  Draw = W3DModelDraw ModuleTag_Draw
   DefaultConditionState
    Model = None
   End
  End
  Body = ActiveBody ModuleTag_Body
   MaxHealth = 1
   InitialHealth = 1
  End
  Behavior = LifetimeUpdate ModuleTag_Life
   MinLifetime = 4000
   MaxLifetime = 4000
  End
  Behavior = DestroyDie ModuleTag_Destroy
  End
 End
End
"""
CASES = {
    "water": ("PortableCosmeticsImpact", 850, 450, 0, 150),
    "land": ("PortableCosmeticsImpact", 850, 700, 0, 150),
    "above-water": ("PortableCosmeticsImpact", 850, 450, 100, 150),
    "shrouded-water": ("PortableCosmeticsImpact", 350, 450, 0, 150),
    "collapse": ("AmericaPowerPlant", 640, 640, 0, 300),
    "low-model": ("AmericaPowerPlant", 640, 640, 0, 300),
    "no-model": ("PortableCosmeticsInvisible", 640, 640, 0, 300),
}


def write_json(path, value):
    path.write_text(json.dumps(value, indent=2) + "\n")


def fixture_map(case):
    spec = importlib.util.spec_from_file_location("portable_cosmetics_map", HERE / "cloud_preview/generate_map.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    template, x, y, z, _ = CASES[case]
    waypoint = module.write_waypoint

    def write_objects(writer, number, wx, wy):
        waypoint(writer, number, wx, wy)
        if number != 2:
            return
        with writer.chunk("Object", 3):
            for value in (x, y, z, 0):
                writer.real(value)
            writer.integer(0)
            writer.ascii(template)
            writer.dictionary({"objectInitialHealth": 100, "objectEnabled": True,
                               "objectPowered": True, "objectRecruitableAI": True,
                               "originalOwner": "teamPlyrCivilian", "uniqueID": "CosmeticsSubject",
                               "objectDestructible": True, "objectSellable": True,
                               "objectRepairable": True})
    module.write_waypoint = write_objects
    return module.build_map(inspection_yard=True)


def prepare(args, case, variant):
    name = case + "-" + variant
    out = args.out / name
    out.mkdir(parents=True, exist_ok=False)
    user, overlay = out / "user", out / "overlay"
    user.mkdir()
    overlay.mkdir()
    for entry in args.overlay.iterdir():
        if entry.name not in ("Maps", "Scenarios"):
            (overlay / entry.name).symlink_to(entry, target_is_directory=entry.is_dir())
    folder = overlay / Path(MAP).parent
    folder.mkdir(parents=True)
    (folder / "PortableCosmeticsCheck.map").write_bytes(fixture_map(case))
    building = BUILDING_INI.replace("Object AmericaPowerPlant\n", "Object AmericaPowerPlant\n GeometryHeight = 4\n" if case == "low-model" else "Object AmericaPowerPlant\n")
    if args.quiet_collapse:
        # Keep the real StructureBody/DestroyDie death and all draw modules. Only
        # this private map removes death debris/FX that hide the sinking model.
        building = building.replace("Object AmericaPowerPlant\n", "Object AmericaPowerPlant\n" + "".join(
            f" RemoveModule ModuleTag_{tag}\n" for tag in ("11", "12", "13", "16", "17")))
    (folder / "map.ini").write_text(IMPACT_INI + building)
    scenarios = overlay / "Scenarios"
    scenarios.mkdir()
    scout = (900, 750) if case == "shrouded-water" else (850, 700) if case == "land" else (850, 450) if "water" in case else (640, 640)
    # Real vision, rather than only hiding the drawn shroud. The shrouded impact
    # is 626 units away from the scout, outside its ordinary vision radius.
    (scenarios / "portable_cosmetics.txt").write_text(
        f"5 spawn 0 AmericaVehicleComanche 1 {scout[0]} {scout[1]}\n"
        "30 tally 0 *\n120 tally 0 *\n180 tally 0 *\n")
    options = OPTIONS
    if variant == "off":
        options = options.replace("WaterSplashes = yes", "WaterSplashes = no").replace("StructureCollapse = yes", "StructureCollapse = no")
    elif variant in ("classic", "classic-off"):
        options = options.replace("ClassicGraphics = no", "ClassicGraphics = yes")
        if variant == "classic-off":
            options = options.replace("WaterSplashes = yes", "WaterSplashes = no").replace("StructureCollapse = yes", "StructureCollapse = no")
    elif variant in ("modern", "modern-off"):
        options = options.replace("WaterStyle = 0", "WaterStyle = 1")
        if variant == "modern-off":
            options = options.replace("WaterSplashes = yes", "WaterSplashes = no")
    (user / "Options.ini").write_text(options)
    shot = args.frame or CASES[case][4]
    command = [str(args.generals), "-root", str(args.root), "-overlay", str(overlay),
               "-offscreen", "-multiInstance", "-noaudio", "-win", "-xres", "960", "-yres", "600",
               "-msaa", "0", "-quickstart", "-noshellmap", "-nologo", "-novideo", "-noFPSLimit",
               "-camera", str(CASES[case][1]), str(CASES[case][2]), "-turbo", "-screenshot", str(shot), "-maxframes", "540",
               "-logPrefix", "cosmetics_", "-map", MAP, "-autoskirmish", "2", "-takeover", "-seed", "1",
               "-side", "0", "FactionAmerica", "-side", "1", "FactionChina", "-observer",
               "-scenario", "portable_cosmetics"]
    overrides = dict(ZH_USER_DATA_DIR=str(user), ZH_UNATTENDED="1", ZH_AUDIO_BACKEND="null",
                     SDL_VIDEODRIVER="offscreen", SDL_GPU_DRIVER="vulkan", ZH_OFFSCREEN_HZ="0",
                     VK_DRIVER_FILES="/usr/share/vulkan/icd.d/nvidia_icd.json",
                     ZH_SUN_SHADOW_CAPTURE_CLOCK="logic", ZH_WATER_CAPTURE_CLOCK="picture")
    manifest = dict(case=case, variant=variant, command=command, environment=overrides,
                    subject=dict(template=CASES[case][0], x=CASES[case][1], y=CASES[case][2], height_offset=CASES[case][3]),
                    prepared_only=args.prepare_only, quiet_collapse=args.quiet_collapse, screenshot_frame=shot, simulation_limit=540,
                    executable_sha256=hashlib.sha256(args.generals.read_bytes()).hexdigest())
    write_json(out / "manifest.json", manifest)
    return out, user, manifest


def run(args, case, variant):
    out, user, manifest = prepare(args, case, variant)
    if args.prepare_only:
        return manifest
    env = dict(os.environ)
    env.pop("ZH_GPU_DEBUG", None)
    env.update(manifest["environment"])
    start = time.monotonic()
    with (out / "run.log").open("w") as log:
        result = subprocess.run(manifest["command"], cwd=out, env=env, stdout=log,
                                stderr=subprocess.STDOUT, timeout=args.timeout)
    manifest.update(returncode=result.returncode, seconds=round(time.monotonic() - start, 2))
    for engine in (user / "Logs/cosmetics_DebugLogFile.txt", args.generals.parent / "cosmetics_DebugLogFile.txt"):
        if engine.exists():
            text = engine.read_text(errors="replace")
            (out / "engine.log").write_text(text)
            manifest["crc"] = re.findall(r"HEADLESS CRC AT LIMIT:.*", text)
            manifest["scenario"] = [line for line in text.splitlines() if "SCENARIO:" in line]
            manifest["map_parse_ok"] = "does not parse, the rest of it is ignored" not in text
            break
    images = list(user.glob("sshot*.bmp"))
    manifest["screenshots"] = [str(path) for path in images]
    if images:
        from PIL import Image
        Image.open(images[0]).save(out / "screenshot.png")
    manifest["passed_runtime"] = result.returncode == 0 and len(images) == 1 and bool(manifest.get("crc")) and manifest.get("map_parse_ok", False)
    write_json(out / "manifest.json", manifest)
    print(case, variant, "PASS" if manifest["passed_runtime"] else "FAIL", flush=True)
    return manifest


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--root", type=Path, required=True)
    p.add_argument("--overlay", type=Path, required=True)
    p.add_argument("--generals", type=Path, required=True)
    p.add_argument("--out", type=Path, required=True)
    p.add_argument("--case", choices=CASES, action="append")
    p.add_argument("--variant", choices=("on", "off", "classic", "classic-off", "modern", "modern-off"), action="append")
    p.add_argument("--frame", type=int)
    p.add_argument("--timeout", type=int, default=240)
    p.add_argument("--prepare-only", action="store_true")
    p.add_argument("--quiet-collapse", action="store_true", help="Remove private-map death debris/FX for unobscured sinking captures")
    args = p.parse_args()
    for attr in ("root", "overlay", "generals", "out"):
        setattr(args, attr, getattr(args, attr).resolve())
    args.out.mkdir(parents=True, exist_ok=True)
    manifests = [run(args, case, variant) for case in (args.case or list(CASES))
                 for variant in (args.variant or ["on", "off"])]
    comparisons = []
    if not args.prepare_only:
        from PIL import Image, ImageChops
        for case in (args.case or list(CASES)):
            group = [m for m in manifests if m["case"] == case]
            for left, right in (("on", "off"), ("classic", "classic-off"), ("modern", "modern-off")):
                a = next((m for m in group if m["variant"] == left), None)
                b = next((m for m in group if m["variant"] == right), None)
                if a and b:
                    diff = ImageChops.difference(Image.open(args.out / f"{case}-{left}/screenshot.png").convert("RGB"),
                                                Image.open(args.out / f"{case}-{right}/screenshot.png").convert("RGB"))
                    changed = sum(pixel != (0, 0, 0) for pixel in diff.getdata())
                    expected_change = left == "on" and case in ("water", "collapse")
                    comparisons.append(dict(case=case, variants=[left, right], changed_pixels=changed,
                                            expected_visual_change=expected_change,
                                            passed_visual_expectation=(changed > 0) == expected_change,
                                            identical_simulation_crc=a.get("crc") == b.get("crc")))
        write_json(args.out / "comparisons.json", comparisons)
    write_json(args.out / "summary.json", dict(runs=manifests, comparisons=comparisons))
    return 0 if args.prepare_only or (all(m["passed_runtime"] for m in manifests)
               and all(c["identical_simulation_crc"] and c["passed_visual_expectation"] for c in comparisons)) else 1


if __name__ == "__main__":
    raise SystemExit(main())
