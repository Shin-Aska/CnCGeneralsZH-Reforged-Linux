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
HERE = Path(__file__).resolve().parent
OUT = HERE/"green"
RD = HERE.parent
REPO = Path("<REPO>")
TOOLS = REPO/"GeneralsMD/Code/Tools/water-check"
for pin in json.loads((HERE/"green-freeze.json").read_text()):
    data = Path(pin["path"]).read_bytes()
    assert len(data)==pin["bytes"] and hashlib.sha256(data).hexdigest()==pin["sha256"],pin["path"]
for name in ("pause_reset.py","pr_transport.py","test_green.py","fixture_green.py","fixture_rows.py","fixture_protocol.py"):
    body = (HERE/name).read_text()
    compile(body,str(HERE/name),"exec")
    assert sum(bool(row.strip()) and not row.lstrip().startswith("#") for row in body.splitlines())<=250
for path in (TOOLS,RD/"lifecycle-game-controller-private-01",RD/"lifecycle-causal-gate-private-01"):
    sys.path.insert(0,str(path))
from process_identity import identity_json
from test_green import CASES,run_case
(OUT/"python-identity.json").write_text(identity_json(),encoding="utf-8")
fixture = OUT/"scratch"/"fixture.py"
fixture.write_bytes((HERE/"fixture_green.py").read_bytes())
whole = RD/"particle-consumer-binding-01/assets/overlay/Maps/Water Positive Fixture v1/Water Positive Fixture v1.map"
env_before = hashlib.sha256(json.dumps(sorted(os.environ.items())).encode()).hexdigest()
cwd_before = Path.cwd()
results: list[dict[str,object]] = []
try:
    for spec in CASES:
        actual = run_case(spec,OUT/spec.name,fixture,whole)
        results.append(actual)
        print(json.dumps({"case":spec.name,"classification":actual["classification"]}),flush=True)
finally:
    ports = []
    images = []
    for spec in CASES:
        output = OUT/spec.name
        path = output/"registration.json"
        if path.is_file():
            port = json.loads(path.read_text())["port"]
            with socket.socket() as connection:
                connection.settimeout(0.2)
                ports.append({"case":spec.name,"port":port,"connectExAfter":connection.connect_ex(("127.0.0.1",port))})
            cleaned = (output/"resources.log").is_file() and "RESOURCE_CLEANED" in (output/"resources.log").read_text()
            assert cleaned,"original Popen not reaped/closed"
            user = output/"user"
            for image in user.iterdir():
                assert image.is_file() and not image.is_symlink() and image.parent==user and image.name.startswith("sshot")
                data = image.read_bytes()
                images.append({"path":str(image),"bytes":len(data),"sha256":hashlib.sha256(data).hexdigest()})
    (OUT/"image-cleanup-intent.json").write_text(json.dumps(images,indent=2),encoding="utf-8")
    for image in images:
        path = Path(image["path"])
        assert hashlib.sha256(path.read_bytes()).hexdigest()==image["sha256"]
        path.unlink()
    for spec in CASES:
        user = OUT/spec.name/"user"
        if user.is_dir():
            user.rmdir()
    after = hashlib.sha256(json.dumps(sorted(os.environ.items())).encode()).hexdigest()
    (OUT/"fixture-cleanup.json").write_text(json.dumps({"ports":ports,"environmentUnchanged":env_before==after,"cwdUnchanged":Path.cwd()==cwd_before,
        "imageFilesRemoved":len(images),"originalPopenResourcesCleaned":True,"scope":"registeredfixturesonly;10035aloneinconclusive; actual originalhandle finalwait + exactbirthabsence/sourcefinally required"},indent=2),encoding="utf-8")
    assert all(row["connectExAfter"]!=0 for row in ports)
    assert env_before==after and Path.cwd()==cwd_before
for pin in json.loads((HERE/"green-freeze.json").read_text()):
    assert hashlib.sha256(Path(pin["path"]).read_bytes()).hexdigest()==pin["sha256"],pin["path"]
assert len(results)==11
proof = {"classification":"QUALIFIED_FOCUSED_GREEN","matchedCases":len(results),"healthyFlows":2,"typedRefusals":8,"realInterrupt":1,"unexpectedFailures":0,"cases":results,
    "scope":"HELPER actual oneconnectionwire+temporaryfiles; NOT Game/menu/registry/native/GPU proof; synthetic native rows only","sharedGate":"98F555DC397C8B28377C9547983AD8D28C8EF256CAE49D1EA27C005EC599B1CB","actualGAME":"UNRUN"}
(OUT/"proof.json").write_text(json.dumps(proof,indent=2),encoding="utf-8")
print(json.dumps(proof,indent=2),flush=True)
