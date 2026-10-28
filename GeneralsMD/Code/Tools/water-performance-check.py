#!/usr/bin/env python3
"""Check Modern Water work counters with short, deterministic preview runs."""
import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import time


ROOT = Path(__file__).resolve().parents[3]
WORK_RE = re.compile(r"^Total work: (.+)$", re.MULTILINE)
COUNT_RE = re.compile(r"([a-z_]+)=(\d+)")


def parse_work(log):
    matches = WORK_RE.findall(log)
    if not matches:
        raise ValueError("preview output did not contain a Total work line")
    counts = {key: int(value) for key, value in COUNT_RE.findall(matches[-1])}
    expected = {"draws", "fft_updates", "fft_passes", "fft_copies",
                "colour_captures", "depth_captures", "planar_renders",
                "path_prepares", "bank_injections"}
    if not expected.issubset(counts):
        raise ValueError("Total work line is missing counters")
    return counts


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--preview", type=Path, default=ROOT / "build-linux/water_preview",
                        help="water_preview executable (default: build-linux/water_preview)")
    parser.add_argument("--icd", type=Path, help="optional Vulkan ICD manifest")
    parser.add_argument("--output", type=Path, help="directory for per-run logs and report.json")
    parser.add_argument("--frames", type=int, default=2, choices=range(1, 11), metavar="1..10",
                        help="small frame count for each check (default: 2)")
    args = parser.parse_args()

    preview = args.preview.expanduser().resolve()
    if not preview.is_file():
        parser.error("preview executable does not exist: " + str(preview))
    icd = args.icd.expanduser().resolve() if args.icd else None
    if icd and not icd.is_file():
        parser.error("ICD manifest does not exist: " + str(args.icd))
    if args.output:
        output = args.output.expanduser().resolve()
        output.mkdir(parents=True, exist_ok=True)
    else:
        output = Path(tempfile.mkdtemp(prefix="water-performance-check-"))

    env = os.environ.copy()
    env["SDL_GPU_DRIVER"] = "vulkan"
    if icd:
        env["VK_ICD_FILENAMES"] = str(icd)
    report = {"preview": str(preview), "icd": str(icd) if icd else env.get("VK_ICD_FILENAMES"),
              "frames": args.frames, "output": str(output), "results": [], "passed": False}
    failures = []

    def run_case(label, options, checks, expected_log=()):
        cmd = [str(preview), "--offscreen", "--hide-controls", "--mode", "material",
               "--width", "640", "--height", "400", "--time", "18",
               "--frames", str(args.frames), *options]
        started = time.monotonic()
        try:
            result = subprocess.run(cmd, cwd=ROOT, env=env, stdout=subprocess.PIPE,
                                    stderr=subprocess.STDOUT, text=True, timeout=240)
            log = result.stdout
            returncode = result.returncode
        except subprocess.TimeoutExpired as exc:
            log = (exc.stdout or "")
            if isinstance(log, bytes):
                log = log.decode(errors="replace")
            log += "\nERROR: preview timed out after 240 seconds\n"
            returncode = -1
        except OSError as exc:
            log = f"ERROR: could not launch preview: {exc}\n"
            returncode = -1
        log_error = None
        try:
            (output / (label + ".log")).write_text(log, encoding="utf-8")
        except OSError as exc:
            log_error = str(exc)
            failures.append(f"{label}: could not save log: {exc}")
        row = {"name": label, "command": cmd, "returncode": returncode,
               "wall_seconds": round(time.monotonic() - started, 3), "checks": []}
        if log_error:
            row["log_save_error"] = log_error
        try:
            counts = parse_work(log)
            row["work"] = counts
            for key, expected in checks.items():
                actual = counts[key]
                ok = actual == expected
                row["checks"].append({"counter": key, "expected": expected, "actual": actual, "passed": ok})
                if not ok:
                    failures.append(f"{label}: {key} expected {expected}, got {actual}")
        except ValueError as exc:
            row["parse_error"] = str(exc)
            failures.append(f"{label}: {exc}")
        for expected_text in expected_log:
            ok = expected_text in log
            row["checks"].append({"log_contains": expected_text, "passed": ok})
            if not ok:
                failures.append(f"{label}: output did not contain {expected_text!r}")
        if returncode:
            failures.append(f"{label}: preview exited {returncode}")
        if label == "path-on" and "work" in row:
            counts = row["work"]
            fallback = "using Hybrid fallback" in log
            expected_planar = per_frame if fallback else 0
            expected_colour = per_frame if fallback else 0
            for key, expected in (("planar_renders", expected_planar),
                                  ("colour_captures", expected_colour)):
                actual = counts[key]
                ok = actual == expected
                row["checks"].append({"counter": key, "expected": expected,
                                      "actual": actual, "passed": ok,
                                      "path_fallback": fallback})
                if not ok:
                    failures.append(f"{label}: {key} expected {expected} for {'Hybrid fallback' if fallback else 'ready path'}, got {actual}")
        row["passed"] = returncode == 0 and "parse_error" not in row and not log_error and all(check["passed"] for check in row["checks"])
        report["results"].append(row)

    per_frame = args.frames
    for reflections in ("on", "off"):
        for wave in ("low", "high"):
            for refraction in ("full", "simple", "off"):
                expected = {"fft_updates": per_frame,
                            "fft_passes": per_frame * (23 if wave == "low" else 26),
                            "fft_copies": per_frame * (90 if wave == "low" else 102),
                            "depth_captures": per_frame}
                if reflections == "off":
                    expected.update(planar_renders=0, path_prepares=0)
                else:
                    expected["planar_renders"] = per_frame
                if refraction in ("full", "simple"):
                    expected["colour_captures"] = per_frame
                elif refraction == "off":
                    expected["colour_captures"] = 0 if reflections == "off" else per_frame
                    expected["depth_captures"] = per_frame
                run_case(f"hybrid-{reflections}-{wave}-{refraction}",
                         ["--reflection", "hybrid", "--scene-reflections", reflections,
                          "--wave-detail", wave, "--refraction", refraction], expected)

    for mode in ("legacy", "ssr", "path"):
        for reflections in ("on", "off"):
            expected = {}
            if reflections == "off":
                expected.update(planar_renders=0, path_prepares=0,
                                colour_captures=0, depth_captures=per_frame)
            if mode == "legacy":
                expected.update(colour_captures=0, depth_captures=per_frame)
            elif mode == "ssr" and reflections == "on":
                expected.update(colour_captures=per_frame, depth_captures=per_frame)
            elif mode == "path" and reflections == "on":
                expected.update(path_prepares=per_frame, depth_captures=per_frame)
            run_case(f"{mode}-{reflections}",
                     ["--reflection", mode, "--scene-reflections", reflections,
                      "--refraction", "off"], expected)

    # Compare mode declares Legacy, SSR and Hybrid demand together. With opaque
    # water, only the SSR/Hybrid consumers need snapshots; Legacy itself does not.
    run_case("compare-opaque-reflections-on",
             ["--reflection", "compare", "--scene-reflections", "on", "--refraction", "off"],
             {"colour_captures": per_frame, "depth_captures": per_frame})
    run_case("legacy-opaque-reflections-on",
             ["--reflection", "legacy", "--scene-reflections", "on", "--refraction", "off"],
             {"colour_captures": 0})
    run_case("compare-opaque-reflections-off",
             ["--reflection", "compare", "--scene-reflections", "off", "--refraction", "off"],
             {"colour_captures": 0, "depth_captures": per_frame,
              "planar_renders": 0, "path_prepares": 0})

    for reflections in ("on", "off"):
        expected = {"colour_captures": per_frame, "depth_captures": per_frame}
        if reflections == "off":
            expected.update(path_prepares=0, planar_renders=0)
        else:
            expected["path_prepares"] = per_frame
        run_case(f"path-full-{reflections}",
                 ["--reflection", "path", "--scene-reflections", reflections,
                  "--refraction", "full"], expected)

    for mode, target, size in (("legacy", "legacy", 256), ("hybrid", "hybrid", 512)):
        for wave in ("low", "high"):
            run_case(f"reflection-detail-low-{mode}-{wave}",
                     ["--reflection", mode, "--scene-reflections", "on", "--wave-detail", wave,
                      "--reflection-detail", "low", "--refraction", "full"],
                     {"fft_updates": per_frame,
                      "fft_passes": per_frame * (23 if wave == "low" else 26),
                      "fft_copies": per_frame * (90 if wave == "low" else 102),
                      "colour_captures": per_frame, "depth_captures": per_frame,
                      "planar_renders": per_frame, "path_prepares": 0},
                     [f"Synthetic planar target: {target} {size}x{size}."])

    report["passed"] = not failures
    report["failures"] = failures
    try:
        (output / "report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    except OSError as exc:
        failures.append(f"could not save report: {exc}")
        report["passed"] = False
        print(json.dumps({"passed": False, "runs": len(report["results"]),
                          "failures": failures, "report": str(output / "report.json")}, indent=2))
        return 1
    print(json.dumps({"passed": report["passed"], "runs": len(report["results"]),
                      "failures": failures, "report": str(output / "report.json")}, indent=2))
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    sys.exit(main())
