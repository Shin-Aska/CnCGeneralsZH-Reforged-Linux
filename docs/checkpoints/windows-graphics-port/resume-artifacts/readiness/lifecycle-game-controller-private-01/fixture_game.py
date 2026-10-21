from __future__ import annotations
import base64
import hashlib
import json
import socket
import struct
import sys
import threading
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent / "lifecycle-controller-green-01"))
from process_identity import identity_json

port, mode, out = int(sys.argv[1]), sys.argv[2], Path(sys.argv[3])
(out / "child-identity.json").write_text(identity_json(), encoding="utf-8")
wire = (out / "wire-events.jsonl").open("x", encoding="utf-8")
state = {"object": 41, "drawable": 71, "x": 10.0, "y": 20.0, "hidden": 0, "shrouded": 0, "destroyed": 0, "present": True}
frame = 0
inspections = 0
pending = ""
workers: list[threading.Thread] = []
stop = threading.Event()
ready = threading.Event()
ready.set()

def event(kind: str, value: str | int | bool) -> None:
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
    encoded = payload.encode("ascii")
    client.sendall((bytes((129, len(encoded))) if len(encoded) < 126 else bytes((129, 126)) + struct.pack(">H", len(encoded))) + encoded)

def group(oid: int, did: int) -> str:
    global inspections
    inspections += 1
    found = int(bool(state["present"]) and oid == state["object"])
    draw_found = int(bool(state["present"]) and did == state["drawable"])
    text = f"water-capture-inspect: logic={frame} client={frame} query-object={oid} object-found={found} object-destroyed={state['destroyed'] if found else -1} query-drawable={did} drawable-found={draw_found} named-found={int(bool(state['present']))} game-birth=UNKNOWN object-birth=UNKNOWN drawable-birth=UNKNOWN current-view=UNKNOWN\n"
    if state["present"]:
        text += f"water-capture-inspect-owner: name=WaterFixtureLow object={state['object']} destroyed={state['destroyed']} dead={state['destroyed']} player=2 template=WaterCaptureCarrier drawable={state['drawable']} drawable-found=1 x={state['x']} y={state['y']} z=29.0 hidden={state['hidden']} shrouded={state['shrouded']}\n"
        text += f"water-capture-inspect-system: system={100 + int(state['object'])} attached-object=0 attached-drawable={state['drawable']} system-start-frame=5 game-birth=UNKNOWN native-issue=UNKNOWN\n"
    count = int(bool(state["present"]))
    text += f"water-capture-inspect-systems: manager=1 matched={count} reported={count} truncated=0\n"
    return text

def emit(text: str) -> None:
    # A real delayed file stream after ACK. No subsequent wire request may arrive before completion.
    event("group_begin", inspections)
    split = min(47, len(text)) # split INSIDE header, never at a complete first group
    print(text[:split], end="", flush=True)
    stop.wait(0.04)
    event("group_complete", inspections)
    ready.set()
    print(text[split:], end="", flush=True)

if mode == "stale":
    print(group(1, 1), end="", flush=True)

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
                if not ready.is_set():
                    event("premature_request", command)
                match command.split()[0]:
                    case "status":
                        frame += 300
                        send(client, json.dumps({"ok": True, "frame": frame, "inGame": True}))
                    case "screenshot":
                        send(client, '{"ok":true,"screenshot":"requested"}')
                    case "quit":
                        send(client, '{"ok":true,"quitting":true}')
                        break
                    case "watercaptureinspect":
                        oid, did = map(int, command.split()[-2:])
                        text = group(oid, did)
                        if mode == "duplicate":
                            text += text
                        if mode == "malformed":
                            text = text.replace("query-object=", "query-object=bad")
                        if mode == "truncated":
                            text = text[:text.index("water-capture-inspect-systems:")]
                        if mode == "stale":
                            text = ""
                        if mode == "missing-manager":
                            text = text.replace("manager=1", "manager=0")
                        if mode == "summary-truncated":
                            text = text.replace("truncated=0", "truncated=1")
                        if mode == "bad-id":
                            text = text.replace("query-object=1 ", "query-object=2147483648 ")
                        ready.clear()
                        send(client, '{"ok":true,"queued":true}')
                        worker = threading.Thread(target=emit, args=(pending + text,))
                        pending = ""
                        workers.append(worker)
                        worker.start()
                    case "watercaptureteleport":
                        state["x"], state["y"] = map(float, command.split()[-2:])
                        pending = f"water-capture-lifecycle: operation=1 name=WaterFixtureLow object=41 drawable=71 x={state['x']} y={state['y']} z=30.0 hidden=0 shrouded=0\n"
                        send(client, '{"ok":true,"queued":true}')
                    case "watercapturehidden" | "watercaptureshrouded":
                        kind = "hidden" if command.startswith("watercapturehidden ") else "shrouded"
                        state[kind] = int(command.split()[-1])
                        op = 2 if kind == "hidden" else 3
                        pending = f"water-capture-lifecycle: operation={op} name=WaterFixtureLow object=41 drawable=71 x=10 y=20 z=29 hidden={state['hidden']} shrouded={state['shrouded']}\n"
                        send(client, '{"ok":true,"queued":true}')
                    case "watercapturedestroy":
                        state["destroyed"] = 1
                        pending = "water-capture-lifecycle: operation=4 object=41 registry=1 destroyed=1\n"
                        send(client, '{"ok":true,"queued":true}')
                        state["present"] = False
                    case "watercapturespawn":
                        if state["present"]:
                            send(client, '{"ok":false}')
                        else:
                            state.update(object=42, drawable=72, destroyed=0, present=True)
                            state["x"], state["y"] = map(float, command.split()[-2:])
                            pending = f"water-capture-lifecycle: operation=5 name=WaterFixtureLow object=42 drawable=72 x={state['x']} y={state['y']} z=29 hidden=0 shrouded=0\n"
                            send(client, '{"ok":true,"queued":true}')
                    case _:
                        send(client, '{"ok":false}')
finally:
    stop.set()
    for worker in workers:
        worker.join(1.0)
        assert not worker.is_alive()
    (out / "served-state.json").write_text(json.dumps({"state": state, "inspections": inspections, "scope": "HELPER, not C++/Game registry"}), encoding="utf-8")
    event("released", port)
    wire.close()
