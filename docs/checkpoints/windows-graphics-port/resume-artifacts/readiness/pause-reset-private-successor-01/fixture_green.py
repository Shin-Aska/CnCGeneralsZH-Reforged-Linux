from __future__ import annotations
import base64
import hashlib
import json
from pathlib import Path
import socket
import sys
import threading
import time
sys.path.insert(0,str(Path(__file__).resolve().parents[2]))
from process_identity import identity_json
from fixture_protocol import receive, send
from fixture_rows import bmp, group, render
port,mode,out = int(sys.argv[1]),sys.argv[2],Path(sys.argv[3])
(out/"child-identity.json").write_text(identity_json(),encoding="utf-8")
wire = (out/"wire-events.jsonl").open("x",encoding="utf-8")
lock = threading.Lock()
stop,release = threading.Event(),threading.Event()
workers: list[threading.Thread] = []
state = {"match":1,"escCount":0,"screenshotRequests":0,"inspectionRequests":0,"pausedCompleteBeforeResume":False,"oldQueryNewMatch":False,"newOwner":False,"mutationBeforeHealthy":False,"imageCompleted":False,"workerJoined":False}
logic,statuses,frame,oid,did = 0,0,100,41,71
live,paused,present,waiting = False,False,False,False
polls,file_polls = 0,0
action = ""


def event(kind: str, value: object = None) -> None:
    with lock:
        wire.write(json.dumps({"event":kind,"value":value,"wall":time.monotonic()})+"\n")
        wire.flush()
        (out/"state-latest.json").write_text(json.dumps(state),encoding="utf-8")


def image_worker(path: Path, data: bytes) -> None:
    event("image-worker-start",str(path))
    try:
        while not release.wait(0.01):
            if stop.is_set():
                return
        if not stop.is_set():
            with path.open("ab") as stream:
                stream.write(data[37:])
            state["imageCompleted"] = True
            event("image-complete",{"path":str(path),"sha256":hashlib.sha256(path.read_bytes()).hexdigest(),"bytes":path.stat().st_size})
    finally:
        event("image-worker-finally",str(path))


try:
    with socket.socket() as listener:
        listener.bind(("127.0.0.1",port))
        listener.listen(1)
        listener.settimeout(6)
        event("listening",port)
        with listener.accept()[0] as client:
            client.settimeout(6)
            event("accepted",1)
            request = b""
            while b"\r\n\r\n" not in request:
                chunk = client.recv(4096)
                if not chunk:
                    raise ConnectionError("handshake ended")
                request += chunk
            key = next(row.split(b":",1)[1].strip() for row in request.split(b"\r\n") if row.lower().startswith(b"sec-websocket-key:"))
            accept = base64.b64encode(hashlib.sha1(key+b"258EAFA5-E914-47DA-95CA-C5AB0DC85B11").digest())
            client.sendall(b"HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: "+accept+b"\r\n\r\n")
            while True:
                command = receive(client)
                event("command",command)
                if not command:
                    break
                verb = command.split()[0]
                if verb in ("key","leave") and waiting:
                    state["mutationBeforeHealthy"] = True
                if command == "status":
                    statuses += 1
                    if statuses == 3:
                        live = True
                        event("startup-live")
                    if live and not paused:
                        logic += 1
                    if waiting:
                        polls += 1
                        if polls == 3:
                            frame += 1
                            print(render(logic,frame,oid,did,state["match"]),end="",flush=True)
                            event("native-release",{"logic":logic,"frame":frame})
                            waiting = False
                    if state["screenshotRequests"] and not state["imageCompleted"]:
                        file_polls += 1
                        if file_polls == 2:
                            (out/"in-file-wait").write_text("actual status reached while partial BMP",encoding="utf-8")
                            event("in-file-wait",mode)
                            if mode == "disconnect":
                                event("deliberate-disconnect")
                                break
                        if file_polls == 3 and mode != "incomplete":
                            release.set()
                    send(client,json.dumps({"ok":True,"frame":logic,"inGame":live}))
                elif command == "key KEY_ESC":
                    state["escCount"] += 1
                    paused = not paused
                    if not paused:
                        state["pausedCompleteBeforeResume"] = state["imageCompleted"]
                        waiting,polls = True,0
                    elif mode == "existing":
                        (out/"user"/"sshot099.bmp").write_bytes(bmp("healthy"))
                        event("preexisting-inventory")
                    event("pause-changed",{"paused":paused,"logic":logic})
                    send(client,'{"ok":true,"pressed":true}')
                elif command == "screenshot":
                    state["screenshotRequests"] += 1
                    count = state["screenshotRequests"]
                    release.clear()
                    state["imageCompleted"] = False
                    file_polls = 0
                    data = bmp(mode)
                    path = out/"user"/f"sshot{count:03}.bmp"
                    path.write_bytes(data[:37])
                    event("image-prefix",{"path":str(path),"bytes":37})
                    if mode == "multiple":
                        (out/"user"/"sshot888.bmp").write_bytes(data)
                    worker = threading.Thread(target=image_worker,args=(path,data),name=f"owned-image-{count}")
                    workers.append(worker)
                    event("worker-registered",worker.name)
                    worker.start()
                    send(client,'{"ok":true,"screenshot":"requested"}')
                elif command == "leave":
                    live,present,logic = False,False,0
                    event("outside-transition")
                    send(client,'{"ok":true,"leaving":true}')
                elif command.startswith("skirmish 2 1 ") and not live:
                    whole = command[len("skirmish 2 1 "):]
                    assert '"' not in whole and Path(whole).is_absolute() and Path(whole).is_file()
                    state["match"] += 1
                    live,logic = True,0
                    event("new-match",whole)
                    if mode == "stale-group":
                        # Saved completed group predates the callback's new-phase offset.
                        print(group(900,1,41,71,True,41,71,""),end="",flush=True)
                        event("stale-group-before-query")
                    send(client,'{"ok":true,"starting":true}')
                elif verb == "watercapturespawn":
                    present = True
                    if state["match"] == 2:
                        oid,did = (42 if mode == "no-reuse" else 41),72
                        state["newOwner"] = True
                    action = f"water-capture-lifecycle: operation=5 name=WaterFixtureLow object={oid} drawable={did} x=412 y=280 z=29 hidden=0 shrouded=0\n"
                    event("spawn-served",{"object":oid,"drawable":did,"match":state["match"]})
                    send(client,'{"ok":true,"queued":true}')
                elif verb == "watercapturemove":
                    event("move-served")
                    send(client,'{"ok":true,"queued":true}')
                elif verb == "watercaptureinspect":
                    state["inspectionRequests"] += 1
                    qo,qd = map(int,command.split()[-2:])
                    if state["match"] == 2 and not present:
                        state["oldQueryNewMatch"] = (qo,qd)==(41,71)
                    send(client,'{"ok":true,"queued":true}')
                    if not (mode == "stale-group" and state["match"] == 2):
                        print(group(logic,state["match"],oid,did,present,qo,qd,action),end="",flush=True)
                    action = ""
                    waiting,polls = present,0
                elif command == "quit":
                    event("quit-served")
                    send(client,'{"ok":true,"quitting":true}')
                    break
                else:
                    send(client,'{"ok":false}')
except (ConnectionError,OSError) as error:
    event("socket-ended",str(error))
finally:
    stop.set()
    release.set()
    for worker in workers:
        worker.join(timeout=1)
    state["workerJoined"] = all(not worker.is_alive() for worker in workers)
    (out/"served-state.json").write_text(json.dumps(state,indent=2),encoding="utf-8")
    event("released",{"port":port,"workersJoined":state["workerJoined"]})
    wire.close()
