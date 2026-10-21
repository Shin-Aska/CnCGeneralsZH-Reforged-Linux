# /// script
# requires-python = ">=3.12"
# dependencies = []
# ///
# How to run: qualified cached Python -B fixture_game.py PORT MODE OUTPUT
from __future__ import annotations
import base64
import hashlib
import json
import socket
import struct
import sys
import time
from pathlib import Path
HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent / "lifecycle-controller-green-01"))
from process_identity import identity_json
port, mode, out = int(sys.argv[1]), sys.argv[2], Path(sys.argv[3])
(out / "child-identity.json").write_text(identity_json(), encoding="utf-8")
wire = (out / "wire-events.jsonl").open("x", encoding="utf-8")
oid, did, x, y, hidden, shrouded, present, destroyed = 41, 71, 10.0, 20.0, 0, 0, True, 0
logic, inspection, waiting, polls, native_frame = 0, 0, False, 0, 100
pending = ""

def event(kind: str, value: str | int) -> None:
    wire.write(json.dumps({"event": kind, "value": value, "wall": time.monotonic()}) + "\n")
    wire.flush()

def exact(client: socket.socket, size: int) -> bytes:
    data = b""
    while len(data) < size:
        part = client.recv(size - len(data))
        if not part:
            return b""
        data += part
    return data

def receive(client: socket.socket) -> str:
    header = exact(client, 2)
    if not header or header[0] & 15 == 8:
        return ""
    length = header[1] & 127
    if length == 126:
        length = struct.unpack(">H", exact(client, 2))[0]
    if length == 127:
        length = struct.unpack(">Q", exact(client, 8))[0]
    mask, payload = exact(client, 4), exact(client, length)
    return bytes(byte ^ mask[index % 4] for index, byte in enumerate(payload)).decode("ascii")

def send(client: socket.socket, payload: str) -> None:
    data = payload.encode("ascii")
    client.sendall((bytes((129, len(data))) if len(data) < 126 else bytes((129, 126)) + struct.pack(">H", len(data))) + data)

def group(qo: int, qd: int) -> str:
    found, drawn = int(present and qo == oid), int(present and qd == did)
    text = f"water-capture-inspect: logic={logic} client={logic} query-object={qo} object-found={found} object-destroyed={destroyed if found else -1} query-drawable={qd} drawable-found={drawn} named-found={int(present)} game-birth=UNKNOWN object-birth=UNKNOWN drawable-birth=UNKNOWN current-view=UNKNOWN\n"
    if present:
        text += f"water-capture-inspect-owner: name=WaterFixtureLow object={oid} destroyed={destroyed} dead={destroyed} player=2 template=WaterCaptureCarrier drawable={did} drawable-found=1 x={x} y={y} z=29 hidden={hidden} shrouded={shrouded}\n"
        for system in (() if mode == "empty-inventory" else ((oid + 100,) if mode == "later-wave" else (oid + 100, oid + 200))):
            text += f"water-capture-inspect-system: system={system} attached-object=0 attached-drawable={did} system-start-frame=5 game-birth=UNKNOWN native-issue=UNKNOWN\n"
    count = (0 if mode == "empty-inventory" else (1 if mode == "later-wave" else 2)) if present else 0
    return text + f"water-capture-inspect-systems: manager=1 matched={count} reported={count} truncated=0\n"

def render() -> None:
    global native_frame
    native_frame += 1
    event("render_release", inspection)
    frame, capture = native_frame, native_frame - 90
    emit_owner = 99 if mode == "wrong-owner" else oid
    emit_logic = 1 if mode == "stale" else logic
    mirror = int(mode == "mirror")
    key = f"frame={frame} generation=1 capture={capture} colour=123 depth=456 view={capture} target=(1280,720,10,1)"
    if mode == "incomplete-key":
        key = key.replace(" depth=456", "")
    if mode == "malformed":
        key = key.replace("generation=1", "generation=oops")
    admitted = int(not hidden and not shrouded)
    text = f"water-device: frame={frame} patch=17 software=0 swapchain=1\nwater-ownership: frame={frame} generation=1 capture={capture} patch=17 ownership={3 if admitted else 0} vessel-y=1 atlas-level=25 water-level=25 channels=7 mirror=0\nwater-route: event=issued attempt=1 frame={frame} patch=17 capture={capture} commands=1 batches=1 coverage=1 ownership-eligible=1 admissions-before=0 admissions-after={admitted} black=000001 white=000002\n"
    for index, (template, texture, sid) in enumerate((("AmphibWaveRest", "EXShockWav.tga", oid + 100), ("AmphibWave", "EXWave01.tga", oid + 200))):
        record = 512 if mode == "capped" else (native_frame - 101) * 2 + index + 1
        text += f"water-particle-observe: phase=main-decision template={template} texture={texture} system={sid} owner={emit_owner} drawable={did} object=0 logic={emit_logic} render={frame-1} mirror={mirror} ordinary=1 particles=2 built=0 skipped={admitted} admitted={admitted} view-before=1 view-after=1 view-stable=1 {key} owner-position=({x},{y},29) water-at-owner=25 stats-sampled=0 stats-active=(0,0) stats-render=(0,0) pipelines=(0,0) draws-made=(0,0) draws-refused=(0,0) native-rs-query=0 native-rs-fill=UNKNOWN native-rs-enum=0 record={record}/512\n"
    if mode == "complete-burst":
        text = ("water-grid-route: event=submit prepared=1 submitted=1 admissions=1 classic-fallback=0\n" * 900) + text
    if mode == "unfinished":
        text = "water-particle-observe: " + "X" * 65537
    if mode in ("no-eligible", "interrupt", "disconnect"):
        text = ""
    # Real partial native row stream; release receipt precedes bytes, never a success ACK.
    print(text[:37], end="", flush=True)
    print(text[37:], end="", flush=True)

try:
    with socket.socket() as listener:
        listener.bind(("127.0.0.1", port))
        listener.listen(1)
        event("listening", port)
        with listener.accept()[0] as client:
            event("accepted", 1)
            request = b""
            while b"\r\n\r\n" not in request:
                request += client.recv(4096)
            key = next(row.split(b":", 1)[1].strip() for row in request.split(b"\r\n") if row.lower().startswith(b"sec-websocket-key:"))
            accept = base64.b64encode(hashlib.sha1(key + b"258EAFA5-E914-47DA-95CA-C5AB0DC85B11").digest())
            client.sendall(b"HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: " + accept + b"\r\n\r\n")
            while True:
                command = receive(client)
                event("command", command)
                if not command:
                    break
                verb = command.split()[0]
                if waiting and verb not in ("status", "watercaptureinspect"):
                    event("mutation_before_render", command)
                match verb:
                    case "status":
                        logic += 1
                        if waiting:
                            polls += 1
                            if polls == 3:
                                (out / "in-wait").write_text("actual same socket polled during render wait", encoding="utf-8")
                                if mode == "disconnect":
                                    event("disconnect_during_wait", inspection)
                                    break
                                render()
                                waiting = False
                        send(client, json.dumps({"ok": True, "frame": logic, "inGame": mode != "startup" or logic > 3}))
                    case "quit":
                        send(client, '{"ok":true,"quitting":true}')
                        break
                    case "watercaptureinspect":
                        inspection += 1
                        event("inspection", inspection)
                        qo, qd = map(int, command.split()[-2:])
                        send(client, '{"ok":true,"queued":true}')
                        text = pending + group(qo, qd)
                        pending = ""
                        print(text[:47], end="", flush=True)
                        print(text[47:], end="", flush=True)
                        # Registry completion is real, render remains unavailable until three status exchanges.
                        waiting, polls = present, 0
                    case "watercaptureteleport":
                        x, y = map(float, command.split()[-2:])
                        pending = f"water-capture-lifecycle: operation=1 name=WaterFixtureLow object={oid} drawable={did} x={x} y={y} z=30 hidden=0 shrouded=0\n"
                        send(client, '{"ok":true,"queued":true}')
                    case "watercapturehidden" | "watercaptureshrouded":
                        flag = int(command.split()[-1])
                        if verb == "watercapturehidden":
                            hidden, op = flag, 2
                        else:
                            shrouded, op = flag, 3
                        pending = f"water-capture-lifecycle: operation={op} name=WaterFixtureLow object={oid} drawable={did} x={x} y={y} z=29 hidden={hidden} shrouded={shrouded}\n"
                        send(client, '{"ok":true,"queued":true}')
                    case "watercapturedestroy":
                        destroyed, present = 1, False
                        pending = f"water-capture-lifecycle: operation=4 object={oid} registry=1 destroyed=1\n"
                        send(client, '{"ok":true,"queued":true}')
                    case "watercapturespawn":
                        oid, did, present, destroyed = 42, 72, True, 0
                        x, y = map(float, command.split()[-2:])
                        pending = f"water-capture-lifecycle: operation=5 name=WaterFixtureLow object={oid} drawable={did} x={x} y={y} z=29 hidden=0 shrouded=0\n"
                        send(client, '{"ok":true,"queued":true}')
                    case _:
                        send(client, '{"ok":false}')
finally:
    event("released", port)
    wire.close()
    (out / "served-state.json").write_text(json.dumps({"object": oid, "drawable": did, "hidden": hidden, "shrouded": shrouded, "inspections": inspection, "scope": "HELPER not Game"}), encoding="utf-8")
