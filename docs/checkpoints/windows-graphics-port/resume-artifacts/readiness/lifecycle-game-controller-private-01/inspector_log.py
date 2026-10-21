from __future__ import annotations
import math
import re
from dataclasses import dataclass
from typing import NewType
from control_monitor import ControlMonitorError

ObjectID = NewType("ObjectID", int)
DrawableID = NewType("DrawableID", int)

@dataclass(frozen=True, slots=True)
class Owner:
    object_id: ObjectID
    drawable_id: DrawableID
    drawable_found: int
    destroyed: int
    dead: int
    x: float
    y: float
    z: float
    hidden: int
    shrouded: int

@dataclass(frozen=True, slots=True)
class System:
    system_id: int
    object_id: int
    drawable_id: int
    start_frame: int

@dataclass(frozen=True, slots=True)
class Action:
    operation: int
    object_id: ObjectID
    destroyed: int
    x: float | None
    y: float | None

@dataclass(frozen=True, slots=True)
class Inspection:
    logic: int
    object_found: int
    object_destroyed: int
    drawable_found: int
    owner: Owner | None
    systems: tuple[System, ...]
    actions: tuple[Action, ...]


def fields(line: str, expected: str) -> dict[str, str]:
    """Parse exact native key sets, rejecting duplicate or unknown tokens."""
    if ": " not in line:
        raise ControlMonitorError("inspect: missing row separator")
    pairs = [token.split("=", 1) for token in line.split(": ", 1)[1].split()]
    if any(len(pair) != 2 for pair in pairs):
        raise ControlMonitorError("inspect: malformed field")
    result = dict(pairs)
    if len(result) != len(pairs) or set(result) != set(expected.split()):
        raise ControlMonitorError("inspect: duplicate/missing/unexpected field")
    return result


def integer(word: str, low: int = 0, high: int = 2147483647) -> int:
    if len(word) > 11 or not re.fullmatch(r"-?[0-9]+", word, flags=re.ASCII):
        raise ControlMonitorError("inspect: malformed integer")
    value = int(word)
    if not low <= value <= high:
        raise ControlMonitorError("inspect: integer out of range")
    return value


def real(word: str) -> float:
    try:
        value = float(word)
    except ValueError as error:
        raise ControlMonitorError("inspect: malformed position") from error
    if not math.isfinite(value) or abs(value) > 1000000:
        raise ControlMonitorError("inspect: invalid position")
    return value


def parse_complete(raw: bytes, query: tuple[int, int], name: str) -> Inspection | None:
    """One fresh bounded contiguous native group; an incomplete suffix never completes it."""
    if len(raw) > 65536:
        raise ControlMonitorError("inspect: fresh log byte budget exceeded")
    lines = raw[:raw.rfind(b"\n") + 1].decode("utf-8", errors="strict").splitlines()
    header: dict[str, str] | None = None
    owner: Owner | None = None
    systems: list[System] = []
    actions: list[Action] = []
    complete = False
    for line in lines:
        if not line.startswith(("water-capture-inspect", "water-capture-lifecycle:")):
            continue
        if complete:
            raise ControlMonitorError("inspect: duplicate/trailing group")
        prefix = line.split(":", 1)[0]
        match prefix:
            case "water-capture-lifecycle":
                if header is not None:
                    raise ControlMonitorError("inspect: interleaved lifecycle row")
                if " registry=" in line:
                    a = fields(line, "operation object registry destroyed")
                    if integer(a["registry"], 0, 1) != 1:
                        raise ControlMonitorError("inspect: unobserved destroy mark")
                    actions.append(Action(integer(a["operation"], 4, 4), ObjectID(integer(a["object"], 1)), integer(a["destroyed"], 1, 1), None, None))
                else:
                    a = fields(line, "operation name object drawable x y z hidden shrouded")
                    if a["name"] != name:
                        raise ControlMonitorError("inspect: action name mismatch")
                    integer(a["drawable"], 1)
                    real(a["z"])
                    integer(a["hidden"], 0, 1)
                    integer(a["shrouded"], 0, 1)
                    actions.append(Action(integer(a["operation"], 1, 5), ObjectID(integer(a["object"], 1)), 0, real(a["x"]), real(a["y"])))
            case "water-capture-inspect":
                if header is not None:
                    raise ControlMonitorError("inspect: duplicate header")
                header = fields(line, "logic client query-object object-found object-destroyed query-drawable drawable-found named-found game-birth object-birth drawable-birth current-view")
                if (integer(header["query-object"], 1), integer(header["query-drawable"], 1)) != query:
                    raise ControlMonitorError("inspect: query identity mismatch")
                for key in ("game-birth", "object-birth", "drawable-birth", "current-view"):
                    if header[key] != "UNKNOWN":
                        raise ControlMonitorError("inspect: unexpected synthesized birth/view")
                integer(header["logic"])
                integer(header["client"])
                integer(header["named-found"], 0, 1)
                integer(header["object-found"], 0, 1)
                integer(header["drawable-found"], 0, 1)
                integer(header["object-destroyed"], -1, 1)
            case "water-capture-inspect-owner":
                if header is None or owner is not None or systems:
                    raise ControlMonitorError("inspect: owner ordering/duplication")
                o = fields(line, "name object destroyed dead player template drawable drawable-found x y z hidden shrouded")
                if o["name"] != name or o["template"] != "WaterCaptureCarrier":
                    raise ControlMonitorError("inspect: owner not qualified")
                integer(o["player"], 0)
                draw_found = integer(o["drawable-found"], 0, 1)
                draw_id = integer(o["drawable"])
                if draw_found and draw_id == 0:
                    raise ControlMonitorError("inspect: found drawable has zero ID")
                owner = Owner(ObjectID(integer(o["object"], 1)), DrawableID(draw_id), draw_found, integer(o["destroyed"], 0, 1), integer(o["dead"], 0, 1), real(o["x"]), real(o["y"]), real(o["z"]), integer(o["hidden"], 0 if draw_found else -1, 1), integer(o["shrouded"], 0 if draw_found else -1, 1))
            case "water-capture-inspect-system":
                if header is None or len(systems) >= 64:
                    raise ControlMonitorError("inspect: system ordering/bound")
                s = fields(line, "system attached-object attached-drawable system-start-frame game-birth native-issue")
                if s["game-birth"] != "UNKNOWN" or s["native-issue"] != "UNKNOWN":
                    raise ControlMonitorError("inspect: synthesized system proof")
                system = System(integer(s["system"], 1), integer(s["attached-object"]), integer(s["attached-drawable"]), integer(s["system-start-frame"]))
                ids = query if owner is None else (*query, int(owner.object_id), int(owner.drawable_id))
                if system.object_id not in ids[::2] and system.drawable_id not in ids[1::2]:
                    raise ControlMonitorError("inspect: unrelated system attachment")
                if any(old.system_id == system.system_id for old in systems):
                    raise ControlMonitorError("inspect: duplicate system")
                systems.append(system)
            case "water-capture-inspect-systems":
                if header is None:
                    raise ControlMonitorError("inspect: summary before header")
                summary = fields(line, "manager matched reported truncated")
                if integer(summary["manager"], 0, 1) != 1 or integer(summary["truncated"], 0, 1) != 0 or integer(summary["matched"], 0, 64) != len(systems) or integer(summary["reported"], 0, 64) != len(systems):
                    raise ControlMonitorError("inspect: missing manager/truncated/incomplete inventory")
                complete = True
            case _:
                raise ControlMonitorError("inspect: unknown row prefix")
    if not complete or not raw.endswith(b"\n"):
        return None
    if header is None or (owner is not None) != bool(integer(header["named-found"], 0, 1)):
        raise ControlMonitorError("inspect: missing or unexpected named owner")
    if integer(header["object-found"], 0, 1) == 0 and integer(header["object-destroyed"], -1, 1) != -1:
        raise ControlMonitorError("inspect: inconsistent missing-object flag")
    return Inspection(integer(header["logic"]), integer(header["object-found"], 0, 1), integer(header["object-destroyed"], -1, 1), integer(header["drawable-found"], 0, 1), owner, tuple(systems), tuple(actions))
