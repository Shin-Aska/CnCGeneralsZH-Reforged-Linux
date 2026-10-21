from __future__ import annotations
import json
import subprocess
import time
from dataclasses import asdict
from pathlib import Path
from typing import TextIO
from control_client import Control, ControlError
from control_monitor import ControlMonitorError, ControlPlan, _set_response_timeout, _status, response_timeout_seconds
from bitmap import InvalidImageError, inspect_bmp
from models import ExpectedImage, Rect
from lifecycle_monitor import Session


def connect(process: subprocess.Popen[bytes], plan: ControlPlan, deadline: float) -> Control:
    """Use the unchanged canonical startup policy; no retry after connecting."""
    _ = response_timeout_seconds(plan, deadline, time.monotonic())
    while time.monotonic() < deadline:
        if process.poll() is not None:
            raise ControlMonitorError("P/R: process exited before connecting")
        try:
            return Control(plan.port, timeout=min(1.0, max(0.1, deadline-time.monotonic())))
        except (TimeoutError, ConnectionError, OSError, ControlError):
            time.sleep(0.02)
    raise ControlMonitorError("P/R: control socket startup deadline expired")


def state(session: Session) -> tuple[int, bool]:
    """Read outside/live explicitly; only the reset callback permits this boundary."""
    session.live()
    _set_response_timeout(session.game, session.plan, session.deadline)
    raw = session.game.status()
    sample = _status(raw)
    session.record.write(json.dumps({"event":"boundary-status","wall":time.monotonic(),"response":raw})+"\n")
    session.record.flush()
    return sample


def wait_state(session: Session, desired_live: bool) -> int:
    frame, live = state(session)
    while live != desired_live:
        time.sleep(0.01)
        frame, live = state(session)
    return frame


def shot(session: Session, user: Path, size: tuple[int,int], known: set[Path], paused: int | None) -> Path:
    """Demand one fresh complete image; file counter is never a picture/logic ID."""
    existing = set(user.glob("sshot*"))
    if existing != known or any(path.is_symlink() for path in existing):
        raise ControlMonitorError("pause: unexpected existing screenshot inventory")
    session.live()
    _set_response_timeout(session.game,session.plan,session.deadline)
    reply = session.game.screenshot()
    match reply:
        case {"ok": True,"screenshot": "requested"}:
            pass
        case _:
            raise ControlMonitorError("pause: screenshot request refused")
    session.record.write(json.dumps({"event":"screenshot-requested","wall":time.monotonic(),"pausedLogic":paused,"existing":[str(p) for p in known]})+"\n")
    session.record.flush()
    width,height = size
    expected_bytes = 54 + ((width*3+3)&~3)*height
    expected = ExpectedImage(Rect(0,0,width,min(height,height*2//3 if height>=600 else height)),0,0.0,1.0)
    while True:
        frame = session.status()
        if paused is not None and frame != paused:
            raise ControlMonitorError("pause: logic advanced before paused file completion")
        candidates = set(user.glob("sshot*"))-known
        if len(candidates)>1:
            raise ControlMonitorError("pause: multiple fresh screenshots")
        if candidates:
            path = next(iter(candidates))
            if path.is_symlink() or not path.is_file() or path.suffix.lower() not in (".bmp",".tga"):
                raise ControlMonitorError("pause: invalid screenshot path")
            length = path.stat().st_size
            if length>expected_bytes:
                raise ControlMonitorError("pause: unexpected screenshot size")
            if length==expected_bytes:
                try:
                    observed = inspect_bmp(path,expected)
                except InvalidImageError as error:
                    raise ControlMonitorError(f"pause: invalid completed image: {error.reason}") from error
                if (observed.width,observed.height)!=size:
                    raise ControlMonitorError("pause: image dimensions differ")
                known.add(path)
                session.record.write(json.dumps({"event":"screenshot-complete","wall":time.monotonic(),"logic":frame,"path":str(path),"observation":asdict(observed),"picture":"UNKNOWN","render":"UNKNOWN"})+"\n")
                session.record.flush()
                return path
        time.sleep(0.01)
