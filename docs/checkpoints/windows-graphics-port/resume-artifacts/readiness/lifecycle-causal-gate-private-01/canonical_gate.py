# /// script
# requires-python = ">=3.12"
# dependencies = []
# ///
# How to run: import from the qualified private callback; no standalone service.
from __future__ import annotations
import math
import re
import time
from collections import deque
from collections.abc import Callable
from dataclasses import dataclass
from pathlib import Path
from control_monitor import ControlMonitorError
from inspector_log import DrawableID, ObjectID, fields

@dataclass(frozen=True, slots=True)
class NativeBinding:
    object_id: ObjectID
    drawable_id: DrawableID
    systems: tuple[int, ...]

@dataclass(frozen=True, slots=True)
class ViewKey:
    frame: int
    generation: int
    capture: int
    colour: int
    depth: int
    view: int
    target: tuple[int, int, int, int]

@dataclass(frozen=True, slots=True)
class NativeProof:
    binding: NativeBinding
    key: ViewKey
    logic: int
    render: int
    systems: tuple[int, int]
    position: tuple[float, float, float]
    patch: int
    end_offset: int
    presentation: bool
    system_provenance: tuple[str, str]

@dataclass(frozen=True, slots=True)
class Particle:
    key: ViewKey
    logic: int
    render: int
    system: int
    rest: bool
    position: tuple[float, float, float]

class NativeGateError(ControlMonitorError):
    """Fresh native eligibility UNKNOWN; never registry absence or water failure."""

PARTICLE_FIELDS = "phase template texture system owner drawable object logic render mirror ordinary particles built skipped admitted view-before view-after view-stable frame generation capture colour depth view target owner-position water-at-owner stats-sampled stats-active stats-render pipelines draws-made draws-refused native-rs-query native-rs-fill native-rs-enum record"
ROUTE_FIELDS = "event attempt frame patch capture commands batches coverage ownership-eligible admissions-before admissions-after black white"
OWNERSHIP_FIELDS = "frame generation capture patch ownership vessel-y atlas-level water-level channels mirror"
DEVICE_FIELDS = "frame patch software swapchain"

def uint(word: str, maximum: int = 18446744073709551615) -> int:
    if len(word) > 20 or re.fullmatch(r"[0-9]+", word, flags=re.ASCII) is None:
        raise NativeGateError("native: malformed unsigned field; UNKNOWN")
    value = int(word)
    if value > maximum:
        raise NativeGateError("native: numeric overflow; UNKNOWN")
    return value

def particle(row: str, binding: NativeBinding, min_logic: int, presentation: bool) -> Particle | None:
    f = fields(row, PARTICLE_FIELDS)
    record = f["record"].split("/")
    if len(record) != 2 or record[1] != "512" or not 1 <= uint(record[0], 512) <= 512:
        raise NativeGateError("native: invalid canonical cap field; UNKNOWN")
    owner, drawable, system = uint(f["owner"], 2147483647), uint(f["drawable"], 2147483647), uint(f["system"], 2147483647)
    if (owner, drawable) != (binding.object_id, binding.drawable_id) or system == 0:
        return None
    logic = uint(f["logic"])
    pair = (f["template"], f["texture"])
    if pair not in (("AmphibWaveRest", "EXShockWav.tga"), ("AmphibWave", "EXWave01.tga")):
        return None
    if f["phase"] != "main-decision" or f["mirror"] != "0" or f["ordinary"] != "1" or uint(f["particles"]) == 0 or logic < min_logic:
        return None
    if any(f[name] != "1" for name in ("view-before", "view-after", "view-stable")):
        return None
    admitted = (f["admitted"], f["skipped"])
    if admitted not in (("1", "1"), ("0", "0")) or (not presentation and admitted != ("1", "1")):
        return None
    target_words = f["target"].removeprefix("(").removesuffix(")").split(",")
    if len(target_words) != 4:
        raise NativeGateError("native: incomplete target; UNKNOWN")
    width, height, format_id, samples = (uint(w, 2147483647) for w in target_words)
    key = ViewKey(*(uint(f[n]) for n in ("frame", "generation", "capture", "colour", "depth", "view")), (width, height, format_id, samples))
    if not all((key.frame, key.generation, key.capture, key.colour, key.depth, key.view, *key.target)):
        return None
    xyz = f["owner-position"].removeprefix("(").removesuffix(")").split(",")
    if len(xyz) != 3:
        raise NativeGateError("native: incomplete position; UNKNOWN")
    try:
        x, y, z = (float(v) for v in xyz)
    except ValueError as error:
        raise NativeGateError("native: malformed position; UNKNOWN") from error
    if not all(math.isfinite(v) for v in (x, y, z)):
        raise NativeGateError("native: nonfinite position; UNKNOWN")
    return Particle(key, logic, uint(f["render"]), system, pair[0] == "AmphibWaveRest", (x, y, z))

def wait_native(log: Path, offset: int, binding: NativeBinding, min_logic: int, presentation: bool, poll: Callable[[], int]) -> NativeProof:
    """Wait at >= min_logic using caller's same-session deadline/liveness/status poll.

    Join full particle key to actual native issued frame/capture/patch plus ownership
    generation and hardware device row. Native rows do not expose colour/depth/view.
    Canonical rows resolve a live actual owner from system attachment in P.cpp;
    a later system may be outside the initial manager snapshot, never guessed/birth.
    Only the unconsumed buffer is capped64KiB; full raw log is never truncated/reset.
    """
    if not 0 <= len(binding.systems) <= 64 or any(not 1 <= n <= 2147483647 for n in (binding.object_id, binding.drawable_id, *binding.systems)):
        raise NativeGateError("native: invalid bound identities; UNKNOWN")
    identity = log.stat()
    if not 0 <= offset <= identity.st_size:
        raise NativeGateError("native: invalid fresh offset; UNKNOWN")
    cursor, buffer = offset, b""
    last_record = 0
    samples: deque[Particle] = deque(maxlen=64)
    routes: deque[tuple[int, int, int]] = deque(maxlen=128)
    owners: deque[tuple[int, int, int, int]] = deque(maxlen=128)
    devices: deque[tuple[int, int]] = deque(maxlen=128)
    while True:
        poll() # no retry/second connection; disconnect and KeyboardInterrupt propagate
        current = log.stat()
        if (identity.st_dev, identity.st_ino) != (current.st_dev, current.st_ino) or current.st_size < cursor:
            raise NativeGateError("native: replaced/truncated log; UNKNOWN")
        with log.open("rb") as stream:
            stream.seek(cursor)
            chunk = stream.read(65537 - len(buffer))
        cursor += len(chunk)
        buffer += chunk
        complete = buffer.split(b"\n")
        buffer = complete.pop()
        if len(buffer) > 65536 or any(len(row) > 65536 for row in complete):
            raise NativeGateError("native: incomplete-buffer overflow; UNKNOWN")
        for raw in complete:
            try:
                row = raw.decode("ascii").rstrip("\r")
                if row.startswith("water-particle-observe: "):
                    ordinal = uint(row.rsplit("record=", 1)[-1].split("/")[0], 512)
                    if ordinal <= last_record:
                        raise NativeGateError("native: duplicate/out-of-order canonical row; UNKNOWN")
                    last_record = ordinal
                    sample = particle(row, binding, min_logic, presentation)
                    if sample is not None:
                        samples.append(sample)
                if row.startswith("water-route: event=issued "):
                    f = fields(row, ROUTE_FIELDS)
                    if uint(f["commands"]) > 0 and f["coverage"] == "1" and f["ownership-eligible"] == "1":
                        routes.append(tuple(uint(f[n]) for n in ("frame", "capture", "patch")))
                if row.startswith("water-ownership: "):
                    f = fields(row, OWNERSHIP_FIELDS)
                    if f["mirror"] == "0" and (presentation or f["ownership"] == "3"):
                        owners.append(tuple(uint(f[n]) for n in ("frame", "generation", "capture", "patch")))
                if row.startswith("water-device: "):
                    f = fields(row, DEVICE_FIELDS)
                    if f["software"] == "0" and f["swapchain"] == "1":
                        devices.append((uint(f["frame"]), uint(f["patch"])))
            except (UnicodeError, ControlMonitorError) as error:
                raise NativeGateError(f"native: malformed fresh row; UNKNOWN: {error}") from error
        for rest in samples:
            if not rest.rest:
                continue
            for wave in samples:
                if wave.rest or (rest.key, rest.logic, rest.render, rest.position) != (wave.key, wave.logic, wave.render, wave.position):
                    continue
                for frame, capture, patch in routes:
                    key = rest.key
                    if (frame, capture) == (key.frame, key.capture) and (frame, key.generation, capture, patch) in owners and (frame, patch) in devices:
                        return NativeProof(binding, key, rest.logic, rest.render, (rest.system, wave.system), rest.position, patch, cursor - len(buffer), presentation, tuple("initial-inspector-inventory" if sid in binding.systems else "canonical-live-observed-not-in-initial-inspector-inventory" for sid in (rest.system, wave.system)))
        time.sleep(0.01)
