#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.11"
# dependencies = []
# ///

# ─── How to run ───
# 1. Install uv (if not installed):
#      curl -LsSf https://astral.sh/uv/install.sh | sh
# 2. Run directly (no venv, no pip install needed):
#      uv run water-check.py --platform windows --exe PATH --root PATH --suite fixture-selfcheck --out PATH
# 3. Or run with Python:
#      python water-check.py --platform windows --exe PATH --root PATH --suite fixture-selfcheck --out PATH
# ──────────────────

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

MODULES = Path(__file__).resolve().with_suffix("")
sys.path.insert(0, str(MODULES))

from comparison import compare_manifests
from models import Platform
from runtime import normalize_paths
from selftest import run_selftests
from suites import run_fixture_selfcheck, write_unavailable

SUITES = (
    "fixture-selfcheck", "defaults", "settings", "production", "lifecycle", "experiments",
    "failures", "classic", "gameplay", "performance",
)


def _parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Run isolated native water validation suites")
    parser.add_argument("--platform", choices=tuple(Platform))
    parser.add_argument("--exe", type=Path)
    parser.add_argument("--root", type=Path)
    parser.add_argument("--overlay", type=Path)
    parser.add_argument("--suite", choices=SUITES)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--compare", nargs=2, type=Path, metavar=("LEFT", "RIGHT"))
    parser.add_argument("--baseline-exe", type=Path)
    parser.add_argument("--repetitions", type=int)
    parser.add_argument("--timeout", type=float, default=600.0)
    parser.add_argument("--self-test", action="store_true")
    return parser


def _main() -> int:
    parser = _parser()
    args = parser.parse_args()
    if args.self_test:
        passed = run_selftests(args.out)
        (args.out / "results.json").write_text(json.dumps({"classification": "passed", "tests": passed}, indent=2) + "\n")
        print(json.dumps({"classification": "passed", "tests": len(passed), "out": str(args.out)}))
        return 0
    if args.compare is not None:
        return compare_manifests(args.compare[0], args.compare[1], args.out)
    if args.platform is None or args.exe is None or args.root is None or args.suite is None:
        parser.error("run mode requires --platform, --exe, --root and --suite")
    if args.timeout <= 0:
        parser.error("--timeout must be positive")
    if args.suite == "performance" and (args.baseline_exe is None or args.repetitions != 5):
        parser.error("performance requires --baseline-exe and --repetitions 5")
    paths = normalize_paths(args.exe, args.root, args.overlay, args.out)
    platform = Platform(args.platform)
    if args.suite == "fixture-selfcheck":
        return run_fixture_selfcheck(paths, platform, args.timeout)
    return write_unavailable(paths, platform, args.suite, "suite is declared but not implemented in Task 1")


if __name__ == "__main__":
    raise SystemExit(_main())
