from __future__ import annotations
import json
from collections.abc import Mapping
from dataclasses import asdict
from pathlib import Path
import sys
from typing import TypeAlias
from control_monitor import ControlPlan
from isolation import launch_isolated
from runtime import reserve_control_port
from old_fixed_controller import make_pause_reset_monitor

JsonValue: TypeAlias = str | int | float | bool | None | list["JsonValue"] | dict[str, "JsonValue"]

class DesiredBehaviorRED(AssertionError):
    """A real missing P/R evidence barrier after healthy unchanged callback completion."""

def run_old_case(output: Path, fixture: Path, mode: str, whole_map: Path) -> Mapping[str, JsonValue]:
    output.mkdir()
    port = reserve_control_port()
    command = [sys.executable, "-B", str(fixture), str(port), mode, str(output)]
    (output / "registration.json").write_text(json.dumps({"port":port,"argv":command,"deadlineSeconds":8,
        "scope":"actualunchanged9935 HELPER notGame","teardown":"canonicalhandlefinally; fixtureclient/listenerfinally"}),encoding="utf-8")
    callback = make_pause_reset_monitor(whole_map,output / "controller-events.jsonl")
    result = launch_isolated(command,output,8.0,{},output,ControlPlan(port,900,()),monitor=callback)
    raw = [json.loads(row) for row in (output / "wire-events.jsonl").read_text().splitlines()]
    state = json.loads((output / "served-state.json").read_text())
    identity = json.loads((output / "child-identity.json").read_text())
    assert result.pid in (identity["pid"],identity["parent"]["pid"])
    assert result.returncode == 0 and not result.timed_out and result.control_error is None and result.control_quit_requested,asdict(result)
    assert sum(row["event"] == "accepted" for row in raw) == 1
    assert raw[-1]["event"] == "released"
    assert json.loads((output / "launch-result.json").read_text()) == json.loads(json.dumps(asdict(result)))
    assert json.loads((output / "control-monitor.json").read_text())["outcome_returned"] is True
    witness = {"actualResult":asdict(result),"actualServedState":state,"wireEvents":len(raw),"scope":"HELPER only"}
    (output / "actual-proof.json").write_text(json.dumps(witness,indent=2),encoding="utf-8")
    return witness

def test_unchanged_fixed_flow_pin(output: Path, fixture: Path, whole_map: Path) -> None:
    # Given unchanged9935 and a real local socket fixture.
    actual = run_old_case(output,fixture,"fixed-pin",whole_map)
    # When the callback completes actual ESC/leave/newmatch/quit.
    state = actual["actualServedState"]
    # Then its existing transport semantics remain healthy; no new P/R evidence inferred.
    assert state["match"] == 2 and state["escCount"] == 2

def test_paused_file_before_resume_red(output: Path, fixture: Path, whole_map: Path) -> None:
    # Given an actual healthy unchanged callback result and real served storage state.
    actual = run_old_case(output,fixture,"fixed-red-pause",whole_map)
    state = actual["actualServedState"]
    # When resume actually arrives on the wire, the desired completed paused BMP must exist.
    try:
        assert state["pausedImageCompleteBeforeResume"] is True, "resume occurred with no complete fresh paused screenshot"
    except AssertionError as error:
        assert state["screenshotRequests"] == 0 and state["files"] == []
        raise DesiredBehaviorRED(str(error)) from error

def test_old_numeric_new_match_group_red(output: Path, fixture: Path, whole_map: Path) -> None:
    # Given an actual outside/new-live transition, not a mocked return value.
    actual = run_old_case(output,fixture,"fixed-red-reset",whole_map)
    state = actual["actualServedState"]
    # When the desired reset validation completes, same-old-number queries and new group must be observed.
    try:
        assert state["oldQueryInNewMatch"] is True and state["newOwnerObserved"] is True, "new match quit without fresh old-ID/new-owner inspection"
    except AssertionError as error:
        assert state["inspectionRequests"] == 0 and state["match"] == 2 and state["owner"] is None
        raise DesiredBehaviorRED(str(error)) from error
