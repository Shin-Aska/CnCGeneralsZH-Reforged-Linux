from __future__ import annotations
import json
import sys
import time
import subprocess
from dataclasses import asdict
from pathlib import Path
from collections.abc import Mapping
from control_monitor import ControlOutcome, ControlPlan
from isolation import launch_isolated
from runtime import reserve_control_port
from lifecycle_monitor import make_lifecycle_monitor


def run_case(parent: Path, case: str, mode: str = "healthy", *, fixed: bool = True) -> None:
    """Given one local helper, use the real wire then inspect served state and durable outcome."""
    output = parent / (case + "-" + mode)
    output.mkdir()
    port = reserve_control_port()
    command = [sys.executable, "-B", str(Path(__file__).with_name("fixture_game.py")), str(port), mode, str(output)]
    registration = {"argv": command, "port": port, "timeout": 6.0, "scope": "HELPER not Game", "teardown": "isolation original Popen wait/finally kill/reap, client/listener/stream thread finally close"}
    (output / "registration.json").write_text(json.dumps(registration), encoding="utf-8")
    plan = ControlPlan(port, 900, () if fixed else (300, 600))
    if fixed:
        monitor = make_lifecycle_monitor(case, output / "run.log", output / "controller-events.jsonl", "WaterFixtureLow", 500.0, 300.0)
        if mode in ("stale", "truncated"):
            def bounded(process: subprocess.Popen[bytes], actual_plan: ControlPlan, parent_deadline: float) -> ControlOutcome:
                return monitor(process, actual_plan, min(parent_deadline, time.monotonic() + 1.5))
            result = launch_isolated(command, output, 6.0, {}, parent, plan, monitor=bounded)
        else:
            result = launch_isolated(command, output, 6.0, {}, parent, plan, monitor=monitor)
    else:
        result = launch_isolated(command, output, 6.0, {}, parent, plan)
    actual = json.loads((output / "launch-result.json").read_text())
    assert actual == json.loads(json.dumps(asdict(result)))
    diagnostic = json.loads((output / "control-monitor.json").read_text())
    assert diagnostic["error"] == result.control_error
    assert result.returncode == 0 and not result.timed_out, actual
    wire = [json.loads(row) for row in (output / "wire-events.jsonl").read_text().splitlines()]
    served = json.loads((output / "served-state.json").read_text())
    assert sum(row["event"] == "accepted" for row in wire) == 1
    assert wire[-1]["event"] == "released"
    assert not any(row["event"] == "premature_request" for row in wire)
    if not fixed:
        assert result.control_error is None and result.control_quit_requested
        assert result.evidence_frame == 900 and result.screenshot_frames == (300, 600)
        assert served["inspections"] == 0
        return
    if mode != "healthy":
        assert result.control_error is not None and "inspect:" in result.control_error, actual
        assert not diagnostic["outcome_returned"] and not result.control_quit_requested
        return
    assert result.control_error is None and result.control_quit_requested, actual
    assert diagnostic["outcome_returned"] and result.evidence_frame >= plan.quit_frame
    assert served["inspections"] >= 2, "actual helper received no fresh inspection (canonical callback ignores fixed flow)"
    state = served["state"]
    match case:
        case "teleport":
            assert (state["x"], state["y"]) == (500.0, 300.0), state
        case "hidden":
            assert state["hidden"] == 0
            assert "operation=2" in (output / "run.log").read_text()
        case "presentation-shrouded":
            assert state["shrouded"] == 0
            assert "operation=3" in (output / "run.log").read_text()
        case "destroy-replacement":
            assert state["object"] == 42 and state["drawable"] == 72
            raw = (output / "run.log").read_text()
            assert raw.index("object-found=0 object-destroyed=-1 query-drawable=71 drawable-found=0") < raw.index("operation=5")
        case _:
            raise AssertionError(case)
    nonstatus = [row["value"] for row in wire if row["event"] == "command" and row["value"] != "status"]
    inspections = [value for value in nonstatus if value.startswith("watercaptureinspect ")]
    assert inspections[0].endswith(" 1 1")
    assert all(value.endswith(" 41 71") for value in inspections[1:])
    assert nonstatus[-1] == "quit"
    (output / "actual-result.json").write_text(json.dumps({"result": asdict(result), "served": served, "wireEvents": len(wire)}), encoding="utf-8")
