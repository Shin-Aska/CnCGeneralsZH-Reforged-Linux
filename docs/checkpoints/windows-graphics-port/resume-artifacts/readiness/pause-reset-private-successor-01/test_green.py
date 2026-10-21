from __future__ import annotations
import _thread
import hashlib
import json
from dataclasses import asdict, dataclass
from pathlib import Path
import sys
import threading
import time
from control_monitor import ControlPlan
from isolation import launch_isolated
from runtime import reserve_control_port
from pause_reset import make_pause_monitor, make_reset_monitor

@dataclass(frozen=True,slots=True)
class Case:
    name: str
    kind: str
    mode: str
    expected: str | None = None

CASES = (
    Case("pause-healthy","pause","healthy"),Case("reset-healthy","reset","healthy"),
    Case("pause-malformed","pause","malformed","not a native BMP"),
    Case("pause-blank","pause","blank","blank or clipped"),
    Case("pause-existing","pause","existing","unexpected existing screenshot inventory"),
    Case("pause-multiple","pause","multiple","multiple fresh screenshots"),
    Case("pause-incomplete","pause","incomplete","deadline expired"),
    Case("pause-disconnect","pause","disconnect","control/log response failed"),
    Case("pause-interrupt","pause","interrupt","KeyboardInterrupt"),
    Case("reset-stale","reset","stale-group","deadline expired"),
    Case("reset-no-reuse","reset","no-reuse","natural ObjectID reuse"),
)


def run_case(spec: Case, output: Path, fixture: Path, whole: Path) -> dict[str,object]:
    output.mkdir()
    user = output/"user"
    user.mkdir()
    port = reserve_control_port()
    command = [sys.executable,"-B",str(fixture),str(port),spec.mode,str(output)]
    budget = 2.5 if spec.mode in ("incomplete","stale-group") else 6.0
    (output/"registration.json").write_text(json.dumps({"argv":command,"port":port,"timeout":budget,"scope":"HELPER not Game","worker":"owned-image-N finally stop+join","interruptWatcher":spec.mode=="interrupt"}),encoding="utf-8")
    callback = (make_pause_monitor(output/"run.log",output/"events.jsonl",user,whole,"WaterFixtureLow",412,280,760,280,(8,4))
        if spec.kind=="pause" else make_reset_monitor(output/"run.log",output/"events.jsonl",user,whole,"WaterFixtureLow",412,280,760,280))
    stop = threading.Event()
    watcher: threading.Thread | None = None
    if spec.mode=="interrupt":
        def interrupt_when_waiting() -> None:
            until = time.monotonic()+5
            while not stop.wait(0.005) and time.monotonic()<until:
                if (output/"in-file-wait").is_file():
                    (output/"interrupt-signal.json").write_text(json.dumps({"wall":time.monotonic(),"event":"actual _thread.interrupt_main after real wire file-wait marker"}),encoding="utf-8")
                    _thread.interrupt_main()
                    return
        watcher = threading.Thread(target=interrupt_when_waiting,name="owned-interrupt-watcher")
        watcher.start()
    result = None
    interrupted = False
    try:
        result = launch_isolated(command,output,budget,{},output,ControlPlan(port,12,()),monitor=callback)
    except KeyboardInterrupt:
        interrupted = True
        assert spec.mode=="interrupt"
    finally:
        stop.set()
        if watcher is not None:
            watcher.join(timeout=1)
            assert not watcher.is_alive()
    wire = [json.loads(row) for row in (output/"wire-events.jsonl").read_text().splitlines()]
    assert sum(row["event"]=="accepted" for row in wire)==1
    events = [json.loads(row) for row in (output/"events.jsonl").read_text().splitlines()]
    if interrupted:
        assert result is None and not (output/"launch-result.json").exists()
        assert "RESOURCE_CLEANED" in (output/"resources.log").read_text()
        assert (output/"interrupt-signal.json").is_file()
        assert not any(row["event"]=="request" and row["command"]=="quit" for row in events)
        actual: dict[str,object] = {"case":spec.name,"classification":"EXPECTED_REAL_INTERRUPT","returnedOutcome":False,"resourcesFinally":True,"fixtureFinallyObserved":any(row["event"]=="released" for row in wire)}
    else:
        assert result is not None
        monitor = json.loads((output/"control-monitor.json").read_text())
        assert json.loads((output/"launch-result.json").read_text())==json.loads(json.dumps(asdict(result)))
        deadline_case = spec.mode in ("incomplete","stale-group")
        if not deadline_case:
            assert result.returncode==0 and not result.timed_out,asdict(result)
        state_path = output/"served-state.json"
        state = json.loads((state_path if state_path.exists() else output/"state-latest.json").read_text())
        if not result.timed_out:
            assert wire[-1]["event"]=="released" and state["workerJoined"] is True
        else:
            assert deadline_case and result.control_error is not None and "deadline expired" in result.control_error
            assert "RESOURCE_CLEANED" in (output/"resources.log").read_text()
        if spec.expected is None:
            assert result.control_error is None and result.control_quit_requested and monitor["outcome_returned"] is True,asdict(result)
            assert state["mutationBeforeHealthy"] is False
            health = [row for row in events if row["event"]=="healthy-native"]
            assert health and health[0]["proof"]["system_provenance"][1]=="canonical-live-observed-not-in-initial-inspector-inventory"
            if spec.kind=="pause":
                complete = [row for row in events if row["event"]=="screenshot-complete"]
                requests = [row for row in events if row["event"]=="request" and row["command"]=="key KEY_ESC"]
                plateau = next(row for row in events if row["event"]=="plateau")
                assert len(complete)==2 and len(requests)==2 and complete[0]["wall"]<requests[1]["wall"]
                assert plateau["wallSeconds"]>=0.25 and plateau["samples"]>=2
                assert state["pausedCompleteBeforeResume"] is True and state["screenshotRequests"]==2
                assert complete[1]["logic"]>complete[0]["logic"]
                assert any(row["event"]=="post-resume-native" for row in events)
                for row in complete:
                    assert hashlib.sha256(Path(row["path"]).read_bytes()).hexdigest()==row["observation"]["sha256"]
            else:
                assert state["match"]==2 and state["oldQueryNewMatch"] is True and state["newOwner"] is True
                reuse = next(row for row in events if row["event"]=="natural-reuse-observed")
                assert reuse["old"]==[41,71] and reuse["new"]==[41,72]
                assert len(health)==2 and health[-1]["proof"]["binding"]["drawable_id"]==72
                assert any(row["event"]=="outside-observed" for row in events)
                assert any(row["event"]=="new-match-old-numeric-query" and row["group"]["owner"] is None for row in events)
        else:
            assert result.control_error is not None and spec.expected in result.control_error,asdict(result)
            assert not result.control_quit_requested and monitor["outcome_returned"] is False
            assert monitor["error"]==result.control_error and monitor["outcome_scope"]=="isolation-default-no-outcome-returned"
            if spec.kind=="pause":
                assert state["escCount"]==1
            if spec.mode=="stale-group":
                assert state["newOwner"] is False
        actual = {"case":spec.name,"classification":"PASS" if spec.expected is None else "EXPECTED_TYPED_REFUSAL","result":asdict(result),"state":state,"wireEvents":len(wire),"scope":"real wire/files only, not Game/menu/native/GPU"}
    (output/"proof.json").write_text(json.dumps(actual,indent=2),encoding="utf-8")
    return actual
