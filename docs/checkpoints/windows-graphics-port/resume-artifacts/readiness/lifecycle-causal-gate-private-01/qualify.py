# /// script
# requires-python = ">=3.12"
# dependencies = []
# ///
# How to run: cached Python -B qualify.py red|green
from __future__ import annotations
import _thread
import json
import os
import socket
import subprocess
import sys
import threading
import time
from dataclasses import asdict
from pathlib import Path
HERE = Path(__file__).resolve().parent
OLD = HERE.parent / "lifecycle-game-controller-private-01"
TOOLS = Path("<REPO>/GeneralsMD/Code/Tools/water-check")
sys.path[:0] = [str(HERE), str(OLD), str(TOOLS), str(HERE.parent / "lifecycle-controller-green-01")]
from control_monitor import ControlOutcome, ControlPlan
from isolation import launch_isolated
from lifecycle_monitor import make_lifecycle_monitor as original
from process_identity import identity_json
from runtime import reserve_control_port
phase = sys.argv[1]
parent = HERE / phase
parent.mkdir()
(parent / "supervisor-identity.json").write_text(identity_json(), encoding="utf-8")
before_env, before_cwd = dict(os.environ), Path.cwd()

def run_case(case: str, mode: str = "healthy", *, compatibility: bool = False, desired: bool = True) -> None:
    # Given real helper process/listener, current Control and actual fresh partial log writes.
    output = parent / (case + "-" + mode + ("-compat" if compatibility else ""))
    output.mkdir()
    port = reserve_control_port()
    command = [sys.executable, "-B", str(HERE / "fixture_game.py"), str(port), mode, str(output)]
    (output / "registration.json").write_text(json.dumps({"argv": command, "port": port, "cap": 4.0, "scope": "HELPER not Game", "teardown": "original Popen finally reaps, original socket/listener finally closes"}), encoding="utf-8")
    if phase in ("red", "red-r1"):
        callback = original(case, output / "run.log", output / "events.jsonl", "WaterFixtureLow", 500.0, 300.0)
    else:
        from lifecycle_causal import make_lifecycle_monitor
        callback = make_lifecycle_monitor(case, output / "run.log", output / "events.jsonl", "WaterFixtureLow", 500.0, 300.0, evidence=not compatibility)
    stop = threading.Event()
    def interrupt() -> None:
        while not stop.wait(0.005):
            if (output / "in-wait").exists():
                (output / "interrupt-sent.json").write_text(json.dumps({"event": "actual interrupt_main", "wall": time.monotonic()}), encoding="utf-8")
                _thread.interrupt_main()
                return
    worker = threading.Thread(target=interrupt) if mode == "interrupt" else None
    def bounded(process: subprocess.Popen[bytes], plan: ControlPlan, deadline: float) -> ControlOutcome:
        return callback(process, plan, deadline)
    if worker is not None:
        worker.start()
    result = None
    interrupted = False
    try:
        # When using one real persistent Control, pass callback through actual F5B6 isolation.
        result = launch_isolated(command, output, 4.0, {}, HERE, ControlPlan(port, 30, ()), monitor=bounded)
    except KeyboardInterrupt:
        interrupted = True
        (output / "interrupt-error.json").write_text(json.dumps({"error": "KeyboardInterrupt", "outcome_returned": False, "propagated": True}), encoding="utf-8")
    finally:
        stop.set()
        if worker is not None:
            worker.join(1)
            assert not worker.is_alive()
    wire = [json.loads(row) for row in (output / "wire-events.jsonl").read_text().splitlines()]
    commands = [row["value"] for row in wire if row["event"] == "command"]
    assert sum(row["event"] == "accepted" for row in wire) == 1
    with socket.socket() as check:
        check.bind(("127.0.0.1", port))
    resources = (output / "resources.log").read_text()
    assert "RESOURCE_CREATED" in resources and "RESOURCE_CLEANED" in resources
    if mode == "interrupt":
        assert interrupted and result is None and "quit" not in commands
        return
    assert result is not None
    durable = json.loads((output / "control-monitor.json").read_text())
    assert durable["error"] == result.control_error
    (output / "actual-result.json").write_text(json.dumps(asdict(result)), encoding="utf-8")
    # Then errors never return an outcome or quit; healthy transitions follow actual release events.
    if mode not in ("healthy", "startup", "later-wave", "empty-inventory", "complete-burst"):
        assert result.control_error is not None and not durable["outcome_returned"]
        assert not result.control_quit_requested and "quit" not in commands
        return
    assert result.returncode == 0 and not result.timed_out and durable["outcome_returned"] and result.control_quit_requested, f"actual live later Wave did not release gate: {asdict(result)}"
    if not desired or compatibility:
        return
    early = [row for row in wire if row["event"] == "mutation_before_render"]
    assert not early, f"actual mutation/restoration/quit preceded fresh native render: {early}"
    released = [row for row in wire if row["event"] == "render_release"]
    assert len(released) >= (3 if case in ("hidden", "presentation-shrouded") else 2)
    if case in ("hidden", "presentation-shrouded"):
        verb = "watercapturehidden" if case == "hidden" else "watercaptureshrouded"
        on = next(row for row in wire if row["event"] == "command" and row["value"].startswith(verb) and row["value"].endswith(" 1"))
        off = next(row for row in wire if row["event"] == "command" and row["value"].startswith(verb) and row["value"].endswith(" 0"))
        assert any(on["wall"] < row["wall"] < off["wall"] for row in released)

if phase == "red-stream":
    run_case("teleport", "complete-burst")
    raise AssertionError("old aggregate cap unexpectedly drained complete burst")

if phase == "red-system":
    run_case("teleport", "later-wave")
    raise AssertionError("old frozen-system gate unexpectedly accepted later Wave")

failures = []
if phase == "red":
    run_case("teleport", compatibility=True, desired=False)
    print("PIN old fixed callback completed real inspection/teleport/quit", flush=True)
    for case in ("teleport", "hidden", "presentation-shrouded"):
        try:
            run_case(case)
        except AssertionError as error:
            assert "actual mutation/restoration/quit preceded" in str(error), str(error)
            failures.append({"case": case, "error": str(error)})
    (parent / "behavior-red.json").write_text(json.dumps({"expectedBehaviorFailures": failures, "fixture": "HELPER not Game"}, indent=2), encoding="utf-8")
    assert len(failures) == 3, failures
    print(json.dumps(failures), flush=True)
    print("RED: all three desired causal ordering assertions failed on actual old wire", flush=True)
else:
    for case in ("teleport", "hidden", "presentation-shrouded", "destroy-replacement"):
        run_case(case)
        print("PASS", case, flush=True)
    run_case("teleport", compatibility=True)
    run_case("teleport", "startup")
    run_case("teleport", "later-wave")
    run_case("teleport", "empty-inventory")
    run_case("teleport", "complete-burst")
    for mode in ("wrong-owner", "stale", "mirror", "incomplete-key", "no-eligible", "malformed", "capped", "unfinished", "disconnect", "interrupt"):
        run_case("teleport", mode)
        print("PASS", mode, flush=True)
    print("GREEN: 19 actual-wire cases", flush=True)
assert dict(os.environ) == before_env and Path.cwd() == before_cwd
(parent / "closure.json").write_text(json.dumps({"environmentUnchanged": True, "cwdUnchanged": True, "bytecodeAbsent": not (HERE / "__pycache__").exists(), "fixture": "HELPER not Game"}), encoding="utf-8")
