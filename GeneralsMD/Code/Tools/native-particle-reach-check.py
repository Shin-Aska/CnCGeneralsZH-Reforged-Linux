#!/usr/bin/env python3
"""Prepare/run isolated real particle and selected-defence evidence.

No installation, player profile change or original data modification. The default
is preparation only; --run requires an explicitly supplied immutable candidate.
Reported GPU counts come from existing runtime logs, never scenario intent.
Selected-defence clicks are recorded attempts; inspect images/accepted native
packets separately before claiming reach integration or fallback acceptance.
"""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import time

from control_client import Control, ControlError

HERE = Path(__file__).resolve().parent
CASES = {
    "particles100k": ("particles100k", "Maps/Alpine Assault/Alpine Assault.map", (760, 850), 900, 780),
    "mixed": ("particles1m_mixed", "Maps/Alpine Assault/Alpine Assault.map", (760, 850), 1800, 1680),
    "reach": ("rf_reach", "Maps/Alpine Assault/Alpine Assault.map", (760, 920), 900, 780),
    "reach-hills": ("reachhills", "Maps/Twilight Flame/Twilight Flame.map", (3400, 3700), 900, 780),
}
GPU = re.compile(r"GPUPARTICLES SDL: dispatches=(\d+) reused=(\d+) draws=(\d+) max-range=(\d+) slice-copies=(\d+) writes=(\d+) buffer-bytes=(\d+) array-bytes=(\d+)")
KEYS = ("dispatches", "reused", "draws", "maximum_evaluated", "slice_copies", "writes", "buffer_bytes", "array_bytes")


def save(path, value):
    path.write_text(json.dumps(value, indent=2) + "\n")


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def prepare(args):
    args.out.mkdir(parents=True, exist_ok=False)
    user, overlay, candidate = (args.out / x for x in ("user", "overlay", "candidate"))
    for folder in (user, overlay, candidate):
        folder.mkdir()
    executable = candidate / "generals"
    shutil.copy2(args.generals, executable)
    # Existing art/INI/data overlays remain readonly symlinks. Only one copied
    # upstream scenario and this run's options are authored privately.
    for entry in args.overlay.iterdir():
        if entry.name != "Scenarios":
            (overlay / entry.name).symlink_to(entry.resolve(), target_is_directory=entry.is_dir())
    scenarios = overlay / "Scenarios"
    scenarios.mkdir()
    scenario, map_name, camera, limit, shot = CASES[args.case]
    source = HERE.parent / "Data/Scenarios" / (scenario + ".txt")
    shutil.copy2(source, scenarios / source.name)
    spec = importlib.util.spec_from_file_location("cosmetic_fixture_options", HERE / "portable-cosmetics-check.py")
    cosmetics = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(cosmetics)
    options = cosmetics.OPTIONS.replace("GroundMarks = no", "GroundMarks = yes")
    if args.variant == "classic":
        options = options.replace("ClassicGraphics = no", "ClassicGraphics = yes")
    (user / "Options.ini").write_text(options)
    limit, shot = args.limit or limit, args.shot or shot
    gpu_capacity = 0 if args.variant == "cpu" else args.gpu_capacity
    command = [str(executable), "-root", str(args.root), "-overlay", str(overlay),
        "-offscreen", "-multiInstance", "-noaudio", "-win", "-xres", "960", "-yres", "600", "-msaa", "0",
        "-quickstart", "-noshellmap", "-nologo", "-novideo", "-noFPSLimit", "-noDynamicLOD", "-turbo",
        "-camera", str(camera[0]), str(camera[1]), "-screenshot", str(shot), "-maxframes", str(limit),
        "-particlecap", str(args.cpu_capacity), "-gpuparticles", str(gpu_capacity), "-dx11post", args.chain,
        "-logPrefix", "nativecheck_", "-map", map_name, "-autoskirmish", "2", "-takeover", "-seed", "1",
        "-side", "0", "FactionAmerica", "-side", "1", "FactionChina", "-scenario", scenario]
    reach = args.case.startswith("reach")
    if reach:
        command += ["-control", str(args.port)]
    else:
        command.append("-observer")
    if args.closed_form_check:
        command.append("-gpuparticlecheck")
    environment = dict(ZH_USER_DATA_DIR=str(user), ZH_UNATTENDED="1", ZH_AUDIO_BACKEND="null",
        SDL_VIDEODRIVER="offscreen", SDL_GPU_DRIVER="vulkan", VK_DRIVER_FILES="/usr/share/vulkan/icd.d/nvidia_icd.json",
        ZH_OFFSCREEN_HZ="0", ZH_SUN_SHADOW_CAPTURE_CLOCK="logic", ZH_WATER_CAPTURE_CLOCK="picture")
    if args.debug:
        environment["ZH_GPU_DEBUG"] = "1"
    if args.passive_trace:
        reach_trace = """set pagination off
set confirm off
set breakpoint pending on
break SdlReachOverlay::TryRelease
commands
silent
printf "NATIVE_TRACE_REACH batches=%llu uploads=%llu bytes=%llu cache-hits=%llu\\n", 'SdlReachOverlay::(anonymous namespace)::state'.Metrics.RecordedBatches, 'SdlReachOverlay::(anonymous namespace)::state'.Metrics.Uploads, 'SdlReachOverlay::(anonymous namespace)::state'.Metrics.OwnedTextureBytes, 'SdlReachOverlay::(anonymous namespace)::state'.Metrics.CacheHits
continue
end
run
"""
        particle_trace = """set pagination off
set confirm off
set breakpoint pending on
break SdlGpuParticles::TryReleaseDevice
commands
silent
printf "NATIVE_TRACE_PARTICLE dispatches=%llu draws=%llu max-range=%u buffers=%llu array=%llu\\n", 'SdlGpuParticles::(anonymous namespace)::state'.Metrics.Dispatches, 'SdlGpuParticles::(anonymous namespace)::state'.Metrics.Draws, 'SdlGpuParticles::(anonymous namespace)::state'.Metrics.MaximumEvaluated, 'SdlGpuParticles::(anonymous namespace)::state'.Metrics.BufferBytes, 'SdlGpuParticles::(anonymous namespace)::state'.Metrics.ArrayBytes
continue
end
run
"""
        script = args.out / "passive-trace.gdb"
        script.write_text(reach_trace if reach else particle_trace)
        command = ["gdb", "-nx", "--batch", "--return-child-result", "-x", str(script), "--args", *command]
    manifest = dict(case=args.case, variant=args.variant, command=command, environment=environment,
        executable_sha256=digest(executable), scenario_sha256=digest(source), profile_sha256=digest(user / "Options.ini"),
        screenshot_frame=shot, simulation_limit=limit, prepared_only=not args.run,
        cpu_capacity=args.cpu_capacity, gpu_capacity=gpu_capacity,
        acceptance_scope="Execution/CRC/work counts only. Images and actual native range path require separate review.")
    save(args.out / "manifest.json", manifest)
    return manifest, user, executable


def select_defence(process, args, manifest):
    # -control intentionally disables unattended screenshot/exit handling. Drive
    # both explicitly and retain the actual observed GameLogic frame of each.
    deadline = time.monotonic() + args.timeout
    selected = photographed = False
    while time.monotonic() < deadline and process.poll() is None:
        try:
            with Control(args.port, timeout=2) as game:
                while process.poll() is None and time.monotonic() < deadline:
                    status = game.status()
                    frame = status.get("frame", 0)
                    if not status.get("inGame"):
                        time.sleep(.01)
                        continue
                    if not selected and frame >= args.select_frame:
                        pixel = args.select_pixel or (480, 300)
                        manifest["selection"] = dict(status=status, pixel=pixel, reply=game.send(f"click {pixel[0]} {pixel[1]}"))
                        selected = True
                    if selected and not photographed and frame >= manifest["screenshot_frame"]:
                        manifest["capture"] = dict(status=status, reply=game.send("screenshot"))
                        photographed = True
                    if photographed and frame >= manifest["simulation_limit"]:
                        manifest["quit"] = dict(status=status, reply=game.send("quit"))
                        return
                    time.sleep(.005)
        except (OSError, ControlError):
            time.sleep(.03)
    raise RuntimeError("could not select, capture and quit while the controlled match was alive")


def execute(args, manifest, user, executable):
    environment = dict(os.environ)
    for key in tuple(environment):
        if key.startswith("ZH_TEST_") or key == "ZH_GPU_DEBUG":
            environment.pop(key)
    environment.update(manifest["environment"])
    started = time.monotonic()
    with (args.out / "run.log").open("w") as log:
        process = subprocess.Popen(manifest["command"], cwd=args.out, env=environment, stdout=log, stderr=subprocess.STDOUT)
        try:
            if args.case.startswith("reach"):
                select_defence(process, args, manifest)
            manifest["returncode"] = process.wait(timeout=args.timeout)
        except Exception as error:
            manifest["runner_error"] = str(error)
            process.terminate()
            try:
                process.wait(timeout=10)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
            manifest["returncode"] = process.returncode
    manifest["seconds"] = round(time.monotonic() - started, 3)
    text = (args.out / "run.log").read_text(errors="replace")
    for path in (user / "Logs/nativecheck_DebugLogFile.txt", executable.parent / "nativecheck_DebugLogFile.txt"):
        if path.exists():
            engine = path.read_text(errors="replace")
            (args.out / "engine.log").write_text(engine)
            text += "\n" + engine
            break
    manifest["crc"] = re.findall(r"HEADLESS CRC AT LIMIT:.*", text)
    manifest["particle_work"] = [dict(zip(KEYS, map(int, row))) for row in GPU.findall(text)]
    manifest["particle_census"] = [line for line in text.splitlines() if "GPUPARTICLES frame" in line or "HEADLESS PARTICLECOST" in line or "HEADLESS FRAMETIME" in line or "GPUPARTICLECHECK" in line]
    manifest["validation_errors"] = [line for line in text.splitlines() if "Validation Error" in line or "VUID-" in line]
    manifest["screenshots"] = [str(p) for p in sorted(user.glob("sshot*.bmp"))]
    if manifest["screenshots"]:
        from PIL import Image
        Image.open(manifest["screenshots"][0]).save(args.out / "screenshot.png")
    manifest["reach_work"] = [int(value) for value in re.findall(r"NATIVE_TRACE_REACH batches=(\d+)", text)]
    manifest["passive_particle_work"] = [dict(zip(("dispatches", "draws", "maximum_evaluated", "buffer_bytes", "array_bytes"), map(int, row))) for row in re.findall(r"NATIVE_TRACE_PARTICLE dispatches=(\d+) draws=(\d+) max-range=(\d+) buffers=(\d+) array=(\d+)", text)]
    manifest["observed_gpu_work"] = any(row["dispatches"] > 0 and row["draws"] > 0 for row in manifest["particle_work"] + manifest["passive_particle_work"])
    manifest["passed_execution"] = manifest["returncode"] == 0 and bool(manifest["crc"]) and len(manifest["screenshots"]) == 1 and not manifest["validation_errors"] and not manifest.get("runner_error")
    if args.require_gpu_work:
        manifest["passed_execution"] &= manifest["observed_gpu_work"]
    if args.require_reach_work:
        manifest["passed_execution"] &= any(manifest["reach_work"])
    if args.case.startswith("reach"):
        manifest["selection_before_screenshot"] = manifest.get("selection", {}).get("status", {}).get("frame", 2**31) < manifest.get("capture", {}).get("status", {}).get("frame", 0)
        manifest["passed_execution"] &= manifest["selection_before_screenshot"] and bool(manifest.get("quit"))
    save(args.out / "manifest.json", manifest)
    print(json.dumps({key: manifest.get(key) for key in ("case", "variant", "returncode", "seconds", "crc", "particle_work", "passed_execution")}))
    return 0 if manifest["passed_execution"] else 1


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("root", "overlay", "generals", "out"):
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument("--case", choices=CASES, required=True)
    parser.add_argument("--variant", choices=("on", "cpu", "classic"), default="on")
    parser.add_argument("--run", action="store_true")
    parser.add_argument("--debug", action="store_true")
    parser.add_argument("--chain", default="off")
    parser.add_argument("--gpu-capacity", type=int, default=1048576)
    parser.add_argument("--cpu-capacity", type=int, default=3000)
    parser.add_argument("--closed-form-check", action="store_true")
    parser.add_argument("--require-gpu-work", action="store_true")
    parser.add_argument("--passive-trace", action="store_true", help="read existing module counters at release through GDB; no inferior calls/writes")
    parser.add_argument("--require-reach-work", action="store_true", help="require positive accepted reach packet count from --passive-trace")
    parser.add_argument("--limit", type=int)
    parser.add_argument("--shot", type=int)
    parser.add_argument("--port", type=int, default=8896)
    parser.add_argument("--select-frame", type=int, default=80)
    parser.add_argument("--select-pixel", nargs=2, type=int)
    parser.add_argument("--timeout", type=int, default=300)
    args = parser.parse_args()
    for name in ("root", "overlay", "generals", "out"):
        setattr(args, name, getattr(args, name).resolve())
    if args.case.startswith("reach") and args.variant == "cpu":
        parser.error("GPU particle capacity does not disable reach; use actual fallback-path evidence separately")
    if args.require_reach_work and not args.passive_trace:
        parser.error("--require-reach-work needs --passive-trace")
    manifest, user, executable = prepare(args)
    return execute(args, manifest, user, executable) if args.run else 0


if __name__ == "__main__":
    raise SystemExit(main())
