from __future__ import annotations
import json
import math
import re
import subprocess
import time
from collections.abc import Callable, Mapping
from dataclasses import asdict
from enum import StrEnum
from pathlib import Path
from typing import TextIO, assert_never
from control_client import Control, ControlError
from control_monitor import ControlMonitorError, ControlOutcome, ControlPlan, _set_response_timeout, _status, response_timeout_seconds
from inspector_log import Inspection, Owner, parse_complete
from canonical_gate import NativeBinding, wait_native

class Case(StrEnum):
    TELEPORT = "teleport"
    HIDDEN = "hidden"
    SHROUDED = "presentation-shrouded"
    REPLACE = "destroy-replacement"

class Session:
    """Mutable per-callback cursor; one connection and one outstanding world request."""
    def __init__(self, game: Control, process: subprocess.Popen[bytes], plan: ControlPlan, deadline: float, log: Path, record: TextIO, name: str) -> None:
        self.game, self.process, self.plan, self.deadline = game, process, plan, deadline
        self.log, self.record, self.name = log, record, name
        self.last_logic = -1

    def live(self) -> None:
        if time.monotonic() >= self.deadline:
            raise ControlMonitorError("inspect: deadline expired awaiting fresh state")
        if self.process.poll() is not None:
            raise ControlMonitorError("inspect: owned process exited")

    def send(self, payload: str, field: str, offset: int = -1) -> None:
        self.live()
        _set_response_timeout(self.game, self.plan, self.deadline)
        self.record.write(json.dumps({"event": "request", "wall": time.monotonic(), "command": payload, "offsetBeforeSend": offset}) + "\n")
        self.record.flush()
        response = self.game.send(payload)
        self.record.write(json.dumps({"event": "reply", "wall": time.monotonic(), "reply": response}) + "\n")
        self.record.flush()
        if not isinstance(response, Mapping) or response.get("ok") is not True or response.get(field) is not True:
            raise ControlMonitorError("inspect: request refused")

    def status(self) -> int:
        self.live()
        _set_response_timeout(self.game, self.plan, self.deadline)
        response = self.game.status()
        frame, live = _status(response)
        self.record.write(json.dumps({"event": "status", "wall": time.monotonic(), "response": response}) + "\n")
        self.record.flush()
        if not live:
            raise ControlMonitorError("inspect: match ended during fixed lifecycle")
        return frame

    def inspect(self, query: tuple[int, int], world: str | None = None) -> Inspection:
        frame = self.status()
        while frame <= self.last_logic:
            time.sleep(0.01)
            frame = self.status()
        identity = self.log.stat()
        offset = identity.st_size
        with self.log.open("rb") as stream:
            if offset:
                stream.seek(offset - 1)
                if stream.read(1) != b"\n":
                    raise ControlMonitorError("inspect: pre-send log ends in partial row")
        # Capture byte offset BEFORE the causal world send, including its real action row.
        if world is not None:
            self.send(world, "queued", offset)
        self.send(f"watercaptureinspect player:PlyrCivilian {self.name} {query[0]} {query[1]}", "queued", offset)
        while True:
            self.live()
            current = self.log.stat()
            if (identity.st_dev, identity.st_ino) != (current.st_dev, current.st_ino) or current.st_size < offset:
                raise ControlMonitorError("inspect: log replaced/truncated")
            with self.log.open("rb") as stream:
                stream.seek(offset)
                raw = stream.read(65537)
            group = parse_complete(raw, query, self.name)
            if group is not None:
                if group.logic < frame or group.logic <= self.last_logic:
                    raise ControlMonitorError("inspect: stale/duplicate logic group")
                self.last_logic = group.logic
                self.record.write(json.dumps({"event": "fresh-group", "wall": time.monotonic(), "offset": offset, "end": offset + len(raw), "group": asdict(group)}) + "\n")
                self.record.flush()
                return group
            time.sleep(0.01)


def owner_of(group: Inspection) -> Owner:
    owner = group.owner
    if owner is None or owner.destroyed or owner.dead or not owner.drawable_id or not owner.drawable_found:
        raise ControlMonitorError("inspect: positive live named identity missing")
    return owner


def action_is(group: Inspection, operation: int, object_id: int) -> None:
    if len(group.actions) != 1 or group.actions[0].operation != operation or group.actions[0].object_id != object_id:
        raise ControlMonitorError("inspect: missing/ambiguous actual lifecycle action")


def make_lifecycle_monitor(case: str, log: Path, events: Path, name: str, x: float, y: float, *, evidence: bool = False) -> Callable[[subprocess.Popen[bytes], ControlPlan, float], ControlOutcome]:
    """Bind one literal fresh-process registry flow; caller owns real coordinates and render QA."""
    try:
        selected = Case(case)
    except ValueError as error:
        raise ControlMonitorError("inspect: unsupported fixed lifecycle case") from error
    if not re.fullmatch(r"[A-Za-z0-9_-]{1,63}", name, flags=re.ASCII) or not all(math.isfinite(v) and abs(v) <= 1000000 for v in (x, y)):
        raise ControlMonitorError("inspect: invalid fixture name/coordinates")
    if not log.is_absolute() or not events.is_absolute() or events.exists():
        raise ControlMonitorError("inspect: invalid/occupied fixture paths")

    def monitor(process: subprocess.Popen[bytes], plan: ControlPlan, deadline: float) -> ControlOutcome:
        _ = response_timeout_seconds(plan, deadline, time.monotonic())
        if plan.screenshot_frames:
            raise ControlMonitorError("inspect: fixed lifecycle requires empty screenshot plan")
        game: Control | None = None
        while game is None and time.monotonic() < deadline:
            if process.poll() is not None:
                raise ControlMonitorError(f"process exited before control connected: {process.returncode}")
            try:
                game = Control(plan.port, timeout=min(1.0, max(0.1, deadline - time.monotonic())))
            except (TimeoutError, ConnectionError, OSError, ControlError):
                time.sleep(0.02)
        if game is None:
            raise ControlMonitorError("control socket did not become ready before the deadline")
        try:
            with game, events.open("x", encoding="utf-8") as record:
                session = Session(game, process, plan, deadline, log, record, name)
                record.write(json.dumps({"event": "discovery-dummy", "query": [1, 1], "scope": "not old identity"}) + "\n")
                record.flush()
                if evidence:
                    while True:
                        session.live()
                        _set_response_timeout(game, plan, deadline)
                        startup = game.status()
                        record.write(json.dumps({"event": "startup-live", "response": startup}) + "\n")
                        record.flush()
                        if _status(startup)[1]:
                            break
                        time.sleep(0.01)
                initial = session.inspect((1, 1)) # DUMMY lookup only; never an old-owner identity.
                old = owner_of(initial)
                query = (int(old.object_id), int(old.drawable_id))
                base = f" player:PlyrCivilian {name}"
                def rendered(group: Inspection, presentation: bool = False) -> None:
                    if evidence:
                        owner = owner_of(group)
                        binding = NativeBinding(owner.object_id, owner.drawable_id, tuple(s.system_id for s in group.systems))
                        offset = log.stat().st_size
                        record.write(json.dumps({"event": "native-wait", "offset": offset, "logic": group.logic}) + "\n")
                        record.flush()
                        proof = wait_native(log, offset, binding, group.logic, presentation, session.status)
                        record.write(json.dumps({"event": "native-proof", "proof": asdict(proof)}) + "\n")
                        record.flush()
                rendered(initial)
                match selected:
                    case Case.TELEPORT:
                        group = session.inspect(query, "watercaptureteleport" + base + f" {x:g} {y:g}")
                        action_is(group, 1, old.object_id)
                        current = owner_of(group)
                        if (current.object_id, current.drawable_id) != query or (current.x, current.y) != (x, y) or (group.actions[0].x, group.actions[0].y) != (x, y):
                            raise ControlMonitorError("inspect: actual teleport XY/identity mismatch")
                        # Later physics may bob Z; never compare it to an older inspector sample.
                        rendered(group)
                    case Case.HIDDEN | Case.SHROUDED:
                        hidden = selected == Case.HIDDEN
                        verb, operation = ("watercapturehidden", 2) if hidden else ("watercaptureshrouded", 3)
                        for flag in (1, 0):
                            group = session.inspect(query, verb + base + f" {flag}")
                            action_is(group, operation, old.object_id)
                            current = owner_of(group)
                            actual = current.hidden if hidden else current.shrouded
                            if (current.object_id, current.drawable_id) != query or actual != flag:
                                raise ControlMonitorError("inspect: actual presentation flag/identity mismatch")
                            rendered(group, presentation=bool(flag))
                    case Case.REPLACE:
                        marked = session.inspect(query, "watercapturedestroy" + base)
                        action_is(marked, 4, old.object_id)
                        if marked.actions[0].destroyed != 1:
                            raise ControlMonitorError("inspect: destroyed mark was not observed")
                        missing = marked
                        while missing.object_found or missing.drawable_found:
                            missing = session.inspect(query)
                        if missing.owner is not None:
                            raise ControlMonitorError("inspect: authored name remains reserved")
                        replacement = session.inspect(query, "watercapturespawn" + base + f" {x:g} {y:g}")
                        new = owner_of(replacement)
                        action_is(replacement, 5, new.object_id)
                        if new.object_id == old.object_id or new.drawable_id == old.drawable_id or replacement.object_found or replacement.drawable_found:
                            raise ControlMonitorError("inspect: replacement did not establish distinct actual IDs")
                        rendered(replacement)
                    case unreachable:
                        assert_never(unreachable)
                reached = session.status()
                while reached < plan.quit_frame:
                    time.sleep(0.01)
                    reached = session.status()
                session.send("quit", "quitting")
                return ControlOutcome(reached, (), True)
        except (TimeoutError, ConnectionError, OSError, ControlError, UnicodeError) as error:
            raise ControlMonitorError(f"inspect: control/log response failed: {error}") from error
    return monitor
