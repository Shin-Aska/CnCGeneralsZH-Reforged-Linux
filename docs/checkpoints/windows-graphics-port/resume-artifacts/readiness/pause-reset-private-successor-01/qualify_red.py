# /// script
# requires-python = ">=3.11"
# dependencies = []
# ///
from __future__ import annotations
import hashlib
import json
import os
from pathlib import Path
import socket
import sys
from typing import Final
HERE: Final = Path(__file__).resolve().parent
OUT: Final = HERE / "red"
TOOLS: Final = Path("<REPO>/GeneralsMD/Code/Tools/water-check")
for pin in json.loads((HERE / "red-freeze.json").read_text()):
    data = Path(pin["path"]).read_bytes()
    assert len(data) == pin["bytes"] and hashlib.sha256(data).hexdigest() == pin["sha256"],pin["path"]
sys.path.insert(0,str(TOOLS))
from process_identity import identity_json
from test_red import DesiredBehaviorRED,test_unchanged_fixed_flow_pin,test_paused_file_before_resume_red,test_old_numeric_new_match_group_red
(OUT / "python-identity.json").write_text(identity_json(),encoding="utf-8")
fixture = OUT / "scratch" / "fixture.py"
fixture.write_bytes((HERE / "fixture_red.py.txt").read_bytes())
whole = Path("<REPO>/.omo/evidence/windows-modern-water-d3d11/20261016152026/task-8/readiness/particle-consumer-binding-01/assets/overlay/Maps/Water Positive Fixture v1/Water Positive Fixture v1.map")
env_before = hashlib.sha256(json.dumps(sorted(os.environ.items())).encode()).hexdigest()
cwd_before = Path.cwd()
results = []
try:
    test_unchanged_fixed_flow_pin(OUT / "pin",fixture,whole)
    results.append({"case":"unchanged9935","verdict":"PIN_PASS"})
    for name,test in (("pause",test_paused_file_before_resume_red),("reset",test_old_numeric_new_match_group_red)):
        try:
            test(OUT / name,fixture,whole)
        except DesiredBehaviorRED as error:
            results.append({"case":name,"verdict":"EXPECTED_BEHAVIOR_RED","primary":str(error)})
        else:
            raise AssertionError("desired missing behavior unexpectedly passed")
finally:
    ports = []
    for name in ("pin","pause","reset"):
        path = OUT / name / "registration.json"
        if path.is_file():
            port = json.loads(path.read_text())["port"]
            with socket.socket() as connection:
                connection.settimeout(0.2)
                ports.append({"port":port,"connectExAfter":connection.connect_ex(("127.0.0.1",port))})
    after = hashlib.sha256(json.dumps(sorted(os.environ.items())).encode()).hexdigest()
    (OUT / "fixture-cleanup.json").write_text(json.dumps({"ports":ports,"environmentUnchanged":env_before == after,
        "cwdUnchanged":Path.cwd() == cwd_before,"scope":"registeredfixturesonly;10035aloneinconclusive; sourcefinallyandnormalreap required"},indent=2),encoding="utf-8")
    assert all(row["connectExAfter"] != 0 for row in ports)
    assert env_before == after and Path.cwd() == cwd_before
for pin in json.loads((HERE / "red-freeze.json").read_text()):
    assert hashlib.sha256(Path(pin["path"]).read_bytes()).hexdigest() == pin["sha256"],pin["path"]
proof = {"classification":"QUALIFIED_BEHAVIOR_RED","matchedCases":len(results),"PIN":1,"desiredRED":2,"unexpectedFailures":0,
    "cases":results,"scope":"realoneconnectionPopen/TCPhelper, notGame/menu/registry/nativeevidence","newPRBehaviorAuthored":False}
assert len(results) == 3
(OUT / "proof.json").write_text(json.dumps(proof,indent=2),encoding="utf-8")
print(json.dumps(proof,indent=2),flush=True)
raise SystemExit(1)
