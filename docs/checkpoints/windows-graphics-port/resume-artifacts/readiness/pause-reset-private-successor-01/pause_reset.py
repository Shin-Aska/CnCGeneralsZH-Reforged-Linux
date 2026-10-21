from __future__ import annotations
import json
import math
import re
import subprocess
import time
from collections.abc import Callable
from dataclasses import asdict
from pathlib import Path
from typing import Literal
from control_client import ControlError
from control_monitor import ControlMonitorError, ControlOutcome, ControlPlan
from lifecycle_monitor import Session, action_is, owner_of
from canonical_gate import NativeBinding, NativeProof, wait_native
from pr_transport import connect, shot, wait_state


def healthy(session: Session, query: tuple[int,int], x: float, y: float) -> NativeProof:
    offset = session.log.stat().st_size
    moved = session.inspect(query, f"watercapturemove player:PlyrCivilian {session.name} {x:g} {y:g}")
    owner = owner_of(moved)
    if (owner.object_id,owner.drawable_id) != query:
        raise ControlMonitorError("P/R: moved owner identity changed")
    binding = NativeBinding(owner.object_id,owner.drawable_id,tuple(system.system_id for system in moved.systems))
    proof = wait_native(session.log,offset,binding,moved.logic,False,session.status)
    session.record.write(json.dumps({"event":"healthy-native","wall":time.monotonic(),"proof":asdict(proof),"birth":"UNKNOWN"})+"\n")
    session.record.flush()
    return proof


def spawn(session: Session, query: tuple[int,int], x: float, y: float) -> tuple[int,int]:
    group = session.inspect(query,f"watercapturespawn player:PlyrCivilian {session.name} {x:g} {y:g}")
    owner = owner_of(group)
    action_is(group,5,owner.object_id)
    return int(owner.object_id),int(owner.drawable_id)


def pause(session: Session, user: Path, size: tuple[int,int], proof: NativeProof) -> None:
    known: set[Path] = set()
    session.send("key KEY_ESC","pressed")
    paused = session.status()
    started = time.monotonic()
    samples = 1
    while time.monotonic()-started < 0.25:
        if session.status() != paused:
            raise ControlMonitorError("pause: logic advanced during plateau")
        samples += 1
        time.sleep(0.01)
    session.record.write(json.dumps({"event":"plateau","wall":time.monotonic(),"logic":paused,"wallSeconds":time.monotonic()-started,"samples":samples})+"\n")
    session.record.flush()
    shot(session,user,size,known,paused)
    offset = session.log.stat().st_size
    session.send("key KEY_ESC","pressed")
    resumed = session.status()
    while resumed <= paused:
        time.sleep(0.01)
        resumed = session.status()
    shot(session,user,size,known,None)
    after = wait_native(session.log,offset,proof.binding,paused+1,False,session.status)
    session.record.write(json.dumps({"event":"post-resume-native","wall":time.monotonic(),"proof":asdict(after)})+"\n")
    session.record.flush()


def reset(session: Session, whole_map: Path, old: tuple[int,int], x: float, y: float, mx: float, my: float) -> Session:
    session.send("leave","leaving")
    outside = wait_state(session,False)
    session.record.write(json.dumps({"event":"outside-observed","logic":outside,"registry":"UNKNOWN"})+"\n")
    session.record.flush()
    session.send(f"skirmish 2 1 {whole_map}","starting")
    wait_state(session,True)
    # Cursor reset is authorized only by the actual outside -> live boundary above.
    fresh = Session(session.game,session.process,session.plan,session.deadline,session.log,session.record,session.name)
    missing = fresh.inspect(old)
    if missing.owner is not None or missing.drawable_found:
        raise ControlMonitorError("reset: old named owner/drawable remains in new match")
    fresh.record.write(json.dumps({"event":"new-match-old-numeric-query","query":old,"group":asdict(missing),"birth":"UNKNOWN"})+"\n")
    fresh.record.flush()
    new = spawn(fresh,old,x,y)
    if new[0] != old[0] or new[1] == old[1]:
        raise ControlMonitorError("reset: natural ObjectID reuse with distinct Drawable not observed")
    healthy(fresh,new,mx,my)
    fresh.record.write(json.dumps({"event":"natural-reuse-observed","old":old,"new":new,"scope":"cross-match only; no birth or same-match ABA assertion"})+"\n")
    fresh.record.flush()
    return fresh


def make_monitor(kind: Literal["pause","reset"], log: Path, events: Path, user: Path, whole_map: Path,
                 name: str, x: float, y: float, mx: float, my: float, size: tuple[int,int]=(1280,720)) -> Callable[[subprocess.Popen[bytes],ControlPlan,float],ControlOutcome]:
    if kind not in ("pause","reset") or re.fullmatch(r"[A-Za-z0-9_-]{1,63}",name,flags=re.ASCII) is None:
        raise ControlMonitorError("P/R: invalid fixed case/name")
    if not all(math.isfinite(value) and abs(value)<=1000000 for value in (x,y,mx,my)):
        raise ControlMonitorError("P/R: invalid coordinates")
    if not all(path.is_absolute() for path in (log,events,user,whole_map)) or events.exists() or not whole_map.is_file() or not user.is_dir():
        raise ControlMonitorError("P/R: invalid or occupied paths")
    if any(type(value) is not int or value<=0 for value in size):
        raise ControlMonitorError("P/R: invalid image size")

    def monitor(process: subprocess.Popen[bytes], plan: ControlPlan, deadline: float) -> ControlOutcome:
        if plan.screenshot_frames:
            raise ControlMonitorError("P/R: fixed flow requires empty screenshot plan")
        game = connect(process,plan,deadline)
        try:
            with events.open("x",encoding="utf-8") as record:
                session = Session(game,process,plan,deadline,log,record,name)
                wait_state(session,True)
                old = spawn(session,(1,1),x,y)
                proof = healthy(session,old,mx,my)
                match kind:
                    case "pause":
                        pause(session,user,size,proof)
                    case "reset":
                        session = reset(session,whole_map,old,x,y,mx,my)
                reached = session.status()
                while reached < plan.quit_frame:
                    time.sleep(0.01)
                    reached = session.status()
                session.send("quit","quitting")
                # Screenshots are file-bound, not exact logic-frame-plan captures.
                return ControlOutcome(reached,(),True)
        except (TimeoutError,ConnectionError,OSError,ControlError,UnicodeError) as error:
            raise ControlMonitorError(f"P/R: control/log response failed: {error}") from error
        finally:
            game.close()
    return monitor


def make_pause_monitor(log: Path, events: Path, user: Path, whole_map: Path, name: str,
                       x: float,y: float,mx: float,my: float,size: tuple[int,int]=(1280,720)) -> Callable[[subprocess.Popen[bytes],ControlPlan,float],ControlOutcome]:
    return make_monitor("pause",log,events,user,whole_map,name,x,y,mx,my,size)


def make_reset_monitor(log: Path, events: Path, user: Path, whole_map: Path, name: str,
                       x: float,y: float,mx: float,my: float) -> Callable[[subprocess.Popen[bytes],ControlPlan,float],ControlOutcome]:
    return make_monitor("reset",log,events,user,whole_map,name,x,y,mx,my)
