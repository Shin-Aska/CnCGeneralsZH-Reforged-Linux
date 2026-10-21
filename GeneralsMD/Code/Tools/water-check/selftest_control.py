from __future__ import annotations

import sys
from pathlib import Path

from control_monitor import ControlPlan
from isolation import launch_isolated
from runtime import reserve_control_port


class ControlSelfTestFailure(RuntimeError):
    pass


FAKE_GAME = r'''
import base64, hashlib, json, socket, struct, sys

port = int(sys.argv[1])
listener = socket.socket()
listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
listener.bind(("127.0.0.1", port))
listener.listen(1)
client, _ = listener.accept()
request = b""
while b"\r\n\r\n" not in request:
    request += client.recv(4096)
key = next(line.split(b":", 1)[1].strip() for line in request.split(b"\r\n") if line.lower().startswith(b"sec-websocket-key:"))
accept = base64.b64encode(hashlib.sha1(key + b"258EAFA5-E914-47DA-95CA-C5AB0DC85B11").digest())
client.sendall(b"HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: " + accept + b"\r\n\r\n")

def receive():
    head = client.recv(2)
    if len(head) != 2:
        return ""
    length = head[1] & 127
    if length == 126:
        length = struct.unpack(">H", client.recv(2))[0]
    elif length == 127:
        length = struct.unpack(">Q", client.recv(8))[0]
    mask = client.recv(4)
    payload = b""
    while len(payload) < length:
        payload += client.recv(length - len(payload))
    return bytes(byte ^ mask[index % 4] for index, byte in enumerate(payload)).decode()

def send(value):
    payload = json.dumps(value).encode()
    if len(payload) < 126:
        client.sendall(bytes((129, len(payload))) + payload)
    else:
        client.sendall(bytes((129, 126)) + struct.pack(">H", len(payload)) + payload)

frame = 0
while True:
    command = receive()
    if command == "status":
        frame += 300
        send({"ok": True, "frame": frame, "inGame": True, "players": []})
    elif command == "screenshot":
        send({"ok": True, "screenshot": "requested"})
    elif command == "quit":
        send({"ok": True, "quitting": True})
        break
    elif not command:
        break
    else:
        send({"ok": False, "error": "unexpected"})
client.close()
listener.close()
'''


def test_controlled_termination(root: Path) -> None:
    call = root / "controlled-termination"
    call.mkdir()
    server = (call / "fake_game.py").resolve()
    server.write_text(FAKE_GAME, encoding="utf-8")
    port = reserve_control_port()
    result = launch_isolated(
        [sys.executable, str(server), str(port)], call, 5.0, {}, call,
        ControlPlan(port, 900, (300, 600)),
    )
    if result.returncode != 0 or result.timed_out:
        raise ControlSelfTestFailure("controlled process did not exit cleanly")
    if result.evidence_frame < 900 or result.screenshot_frames != (300, 600):
        raise ControlSelfTestFailure("controlled process quit before all evidence milestones")
    if not result.control_quit_requested or result.control_error is not None:
        raise ControlSelfTestFailure("controlled process lacked a successful quit receipt")
