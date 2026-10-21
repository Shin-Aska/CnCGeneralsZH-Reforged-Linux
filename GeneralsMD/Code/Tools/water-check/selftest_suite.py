from __future__ import annotations

import json
import os
import shutil
import subprocess
import sys
from pathlib import Path


class SuiteSelfTestFailure(RuntimeError):
    pass


LAUNCHER = r"""
#include <process.h>
#include <stdlib.h>
int main(int argc, char **argv) {
    char *python = getenv("WATER_CHECK_ADAPTER_PYTHON");
    char *script = getenv("WATER_CHECK_ADAPTER_SCRIPT");
    char **args;
    int index;
    if (!python || !script) return 97;
    args = (char **)calloc((size_t)argc + 2, sizeof(char *));
    if (!args) return 98;
    args[0] = python;
    args[1] = script;
    for (index = 1; index < argc; ++index) args[index + 1] = argv[index];
    return _spawnv(_P_WAIT, python, (const char * const *)args);
}
"""


FAKE_ENGINE = r'''
import base64, hashlib, json, os, socket, struct, sys, time
from pathlib import Path

args = sys.argv[1:]
def value(name, default=None):
    return args[args.index(name) + 1] if name in args else default

mode = os.environ.get("WATER_CHECK_ADAPTER_MODE", "success")
prefix = value("-logPrefix", "water_unknown_")
scene = prefix.removeprefix("water_").removesuffix("_")
user = Path(os.environ["ZH_USER_DATA_DIR"])
user.mkdir(parents=True, exist_ok=True)

def bitmap(path, variant):
    width, height = 1280, 720
    left = bytes((145 + variant, 80 + variant, 15 + variant))
    right = bytes((180 + variant, 130 + variant, 80 + variant))
    row = (left + right) * (width // 2)
    body = row * height
    header = struct.pack("<2sIHHI", b"BM", 54 + len(body), 0, 0, 54)
    dib = struct.pack("<IiiHHIIiiII", 40, width, height, 1, 24, 0, len(body), 0, 0, 0, 0)
    path.write_bytes(header + dib + body)

if mode in ("timeout", "parent_interrupt"):
    time.sleep(5)

log = Path.cwd() / f"{prefix}DebugLogFile.txt"
log.write_text("-dx11 drew: fixture engine:classictrapezoidwater:test -> 12 draws\ncamera held; players alive\n")

if "-control" not in args:
    bitmap(user / "sshot001.bmp", 0)
    raise SystemExit(7 if mode == "scene_failure" and scene == "golden-oasis" else 0)

port = int(value("-control"))
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
    if len(head) != 2: return ""
    length = head[1] & 127
    if length == 126: length = struct.unpack(">H", client.recv(2))[0]
    elif length == 127: length = struct.unpack(">Q", client.recv(8))[0]
    mask = client.recv(4)
    payload = b""
    while len(payload) < length: payload += client.recv(length - len(payload))
    return bytes(byte ^ mask[index % 4] for index, byte in enumerate(payload)).decode()

def send(value):
    payload = json.dumps(value).encode()
    head = bytes((129, len(payload))) if len(payload) < 126 else bytes((129, 126)) + struct.pack(">H", len(payload))
    client.sendall(head + payload)

frames = iter((390, 540, 840, 960))
frame = 0
shot = 0
while True:
    command = receive()
    if command == "status":
        frame = next(frames, 960)
        send({"ok": True, "frame": frame, "inGame": True, "players": []})
    elif command == "screenshot":
        shot += 1
        bitmap(user / f"sshot{shot:03d}.bmp", shot)
        send({"ok": True, "screenshot": "saved"})
    elif command == "quit":
        send({"ok": True, "quitting": True})
        break
    elif not command:
        break
    else:
        send({"ok": False, "error": "unexpected"})
client.close()
listener.close()

print("SCENARIO: loaded 'Scenarios\\water_capture_carrier.txt', 7 actions, 0 lines refused")
for line in (
    "frame=180 requested=90 action=0 player=player1 index=0 allowed=1 success=1 object=42 x=260.000 y=280.000",
    "frame=240 requested=120 action=1 player=player1 index=0 allowed=1 success=1 object=42 x=760.000 y=280.000",
    "frame=390 requested=195 action=4 player=player1 index=0 allowed=1 success=1 object=42 x=0.000 y=0.000",
    "frame=450 requested=225 action=1 player=player1 index=0 allowed=1 success=1 object=42 x=300.000 y=280.000",
    "frame=540 requested=270 action=4 player=player1 index=0 allowed=1 success=1 object=42 x=0.000 y=0.000",
    "frame=600 requested=300 action=1 player=player1 index=0 allowed=1 success=1 object=42 x=860.000 y=280.000",
    "frame=840 requested=420 action=4 player=player1 index=0 allowed=1 success=1 object=42 x=0.000 y=0.000",
): print("water-capture-command: " + line)

if mode in ("restore_failure", "combined"):
    target = Path.cwd() / "Data" / "INI" / "GameData.ini"
    target.unlink(missing_ok=True)
    target.mkdir()
raise SystemExit(7 if mode == "combined" else 0)
'''


INTERRUPT_INJECTION = r'''
import os
if os.environ.get("WATER_CHECK_ADAPTER_PARENT_INTERRUPT") == "1":
    import suites
    original = suites.launch_isolated
    def interrupt_after_child(*args, **kwargs):
        original(*args, **kwargs)
        raise KeyboardInterrupt
    suites.launch_isolated = interrupt_after_child
'''


def _require(condition: bool, message: str) -> None:
    if not condition:
        raise SuiteSelfTestFailure(message)


def test_fixture_suite_finalization(root: Path) -> None:
    repository = Path(__file__).resolve().parents[4]
    helper = Path(__file__).resolve().parent
    adapter = root / "suite-adapter"
    runtime = adapter / "runtime"
    (runtime / "Data" / "INI").mkdir(parents=True)
    base = b"GameData\r\nEnd\r\n"
    (runtime / "Data" / "INI" / "GameData.ini").write_bytes(base)

    map_source = adapter / "map-source" / "Maps"
    map_source.mkdir(parents=True)
    (map_source / "MapCache.ini").write_text("\n".join(f"MapCache map{index}" for index in range(10)))
    bigfile = helper.parent / "bigfile.py"
    subprocess.run(
        [sys.executable, str(bigfile), "pack", str(runtime / "MapsZH.big"), str(adapter / "map-source")],
        check=True,
        stdout=subprocess.DEVNULL,
    )

    source = adapter / "launcher.c"
    executable = runtime / "fake-generals.exe"
    source.write_text(LAUNCHER, encoding="utf-8")
    subprocess.run(["gcc", str(source), "-O2", "-o", str(executable)], check=True)
    script = adapter / "fake_engine.py"
    script.write_text(FAKE_ENGINE, encoding="utf-8")
    (adapter / "sitecustomize.py").write_text(INTERRUPT_INJECTION, encoding="utf-8")

    cases = (
        ("success", 10.0, 0, "passed", "passed"),
        ("scene_failure", 10.0, 1, "failed", "passed"),
        ("timeout", 0.2, 1, "failed", "passed"),
        ("restore_failure", 10.0, 1, "failed", "failed"),
        ("combined", 10.0, 1, "failed", "failed"),
    )
    for mode, timeout, expected_exit, expected_class, expected_cleanup in cases:
        output = root / f"suite-cli-{mode}"
        environment = os.environ.copy()
        environment.update({
            "WATER_CHECK_ADAPTER_MODE": mode,
            "WATER_CHECK_ADAPTER_PYTHON": sys.executable,
            "WATER_CHECK_ADAPTER_SCRIPT": str(script.resolve()),
        })
        completed = subprocess.run(
            [
                sys.executable,
                str(helper.parent / "water-check.py"),
                "--platform", "windows",
                "--exe", str(executable),
                "--root", str(runtime),
                "--overlay", str(helper / "fixtures" / "overlay"),
                "--suite", "fixture-selfcheck",
                "--out", str(output),
                "--timeout", str(timeout),
            ],
            cwd=repository,
            env=environment,
            check=False,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
        )
        _require(completed.returncode == expected_exit, f"{mode} CLI exit was {completed.returncode}: {completed.stdout}")
        manifest = json.loads((output / "manifest.json").read_text(encoding="utf-8"))
        _require(manifest["classification"] == expected_class, f"{mode} manifest classification was untruthful")
        cleanup = manifest["game_data_cleanup"]
        _require(cleanup["classification"] == expected_cleanup, f"{mode} cleanup classification was untruthful")
        scene_failures = [scene for scene in manifest["scenes"] if scene["classification"] == "failed"]
        if mode in ("scene_failure", "timeout", "combined"):
            _require(bool(scene_failures), f"{mode} lost its scene failure")
        if mode == "combined":
            _require(bool(scene_failures) and cleanup["classification"] == "failed", "combined causes were not both retained")
        for receipt in output.rglob("resources.log"):
            text = receipt.read_text(encoding="utf-8")
            _require("RESOURCE_CREATED" in text and "RESOURCE_CLEANED" in text, f"{mode} child lacks cleanup receipt")
        if expected_cleanup == "passed":
            staged = output / "farm" / "Data" / "INI" / "GameData.ini"
            _require(staged.read_bytes() == base, f"{mode} did not restore effective loose GameData")

    interrupt_output = root / "suite-cli-parent-interrupt"
    interrupt_environment = os.environ.copy()
    interrupt_environment.update({
        "WATER_CHECK_ADAPTER_MODE": "success",
        "WATER_CHECK_ADAPTER_PYTHON": sys.executable,
        "WATER_CHECK_ADAPTER_SCRIPT": str(script.resolve()),
        "WATER_CHECK_ADAPTER_PARENT_INTERRUPT": "1",
        "PYTHONPATH": os.pathsep.join((str(adapter.resolve()), str(helper.resolve()))),
    })
    interrupted = subprocess.run(
        [
            sys.executable,
            str(helper.parent / "water-check.py"),
            "--platform", "windows",
            "--exe", str(executable),
            "--root", str(runtime),
            "--overlay", str(helper / "fixtures" / "overlay"),
            "--suite", "fixture-selfcheck",
            "--out", str(interrupt_output),
            "--timeout", "10",
        ],
        check=False,
        cwd=repository,
        env=interrupt_environment,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
    )
    resource = interrupt_output / "scenes" / "tournament-island" / "resources.log"
    _require(interrupted.returncode != 0, "parent interruption returned success")
    interrupt_manifest = json.loads((interrupt_output / "manifest.json").read_text(encoding="utf-8"))
    _require(interrupt_manifest["classification"] == "failed", "interruption manifest passed")
    _require(
        str(interrupt_manifest["orchestration_failure"]).startswith("KeyboardInterrupt"),
        f"interruption cause was lost: {interrupted.stdout}",
    )
    _require(interrupt_manifest["game_data_cleanup"]["classification"] == "passed", "interruption skipped cleanup")
    _require("RESOURCE_CLEANED" in resource.read_text(encoding="utf-8"), "interrupted child lacks cleanup receipt")

    shutil.rmtree(adapter)
