from __future__ import annotations
import ast
import hashlib
import json
import os
import socket
import sys
from pathlib import Path
HERE = Path(__file__).resolve().parent
TOOLS = Path("<REPO>/GeneralsMD/Code/Tools/water-check")
sys.path.insert(0, str(TOOLS))
sys.path.insert(0, str(HERE.parent / "lifecycle-controller-green-01"))
sys.path.insert(0, str(HERE))
from process_identity import identity_json
from test_lifecycle import run_case

phase = sys.argv[1]
out = HERE / phase
out.mkdir()
(out / "python-identity.json").write_text(identity_json(), encoding="utf-8")
pins = json.loads((HERE / (phase + "-freeze.json")).read_text())
for pin in pins:
    assert hashlib.sha256(Path(pin["path"]).read_bytes()).hexdigest().upper() == pin["sha256"]
before = hashlib.sha256(json.dumps(sorted(os.environ.items())).encode()).hexdigest()
cwd = Path.cwd()
quality = []
for module in ("lifecycle_monitor.py", "inspector_log.py", "fixture_game.py", "test_lifecycle.py", "qualify.py"):
    source = (HERE / module).read_text()
    tree = ast.parse(source, filename=module, type_comments=True)
    pure = sum(bool(line.strip()) and not line.lstrip().startswith("#") for line in source.splitlines())
    assert pure <= 250 and not tree.type_ignores
    assert not any(isinstance(node, ast.ExceptHandler) and node.type is None for node in ast.walk(tree))
    quality.append({"module": module, "pureLOC": pure, "syntax": "PASS"})
completed: list[str] = []
try:
    run_case(out, "default", fixed=False)
    completed.append("default-healthy-pin")
    if phase == "red":
        run_case(out, "teleport")
    else:
        for case in ("teleport", "hidden", "presentation-shrouded", "destroy-replacement"):
            run_case(out, case)
            completed.append(case)
        for mode in ("duplicate", "malformed", "truncated", "stale", "missing-manager", "summary-truncated", "bad-id"):
            run_case(out, "teleport", mode)
            completed.append(mode)
finally:
    ports = []
    for path in out.glob("*/registration.json"):
        port = json.loads(path.read_text())["port"]
        with socket.socket() as probe:
            probe.settimeout(0.2)
            ports.append({"port": port, "connectExAfter": probe.connect_ex(("127.0.0.1", port))})
    cleanup = {"ports": ports, "completed": completed, "environmentUnchanged": before == hashlib.sha256(json.dumps(sorted(os.environ.items())).encode()).hexdigest(), "cwdUnchanged": cwd == Path.cwd(), "scope": "HELPER only, no Game", "phase": phase}
    (out / "fixture-cleanup.json").write_text(json.dumps(cleanup, indent=2), encoding="utf-8")
    assert all(row["connectExAfter"] != 0 for row in ports)
    assert cleanup["environmentUnchanged"] and cleanup["cwdUnchanged"]
for pin in pins:
    assert hashlib.sha256(Path(pin["path"]).read_bytes()).hexdigest().upper() == pin["sha256"]
proof = {"phase": phase, "cases": completed, "syntaxAndSize": quality, "scope": "HELPER real Popen/WebSocket/stream; not C++ registry or Game/nativewater"}
(out / "proof.json").write_text(json.dumps(proof, indent=2), encoding="utf-8")
print(json.dumps(proof), flush=True)
