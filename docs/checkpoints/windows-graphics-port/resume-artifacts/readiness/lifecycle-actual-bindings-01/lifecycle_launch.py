# /// script
# requires-python = ">=3.11"
# dependencies = []
# ///
# How to run: pinned cached Python -B run_teleport.py --go, only after independent review/root GO.
from __future__ import annotations
from dataclasses import asdict, dataclass
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import sys
import time
from typing import Final
from binding_inputs import PINS, Pin, SOURCE_MANIFEST_SHA256, STAGE_SHA256

R: Final = Path("<REPO>")
E: Final = R / ".omo/evidence/windows-modern-water-d3d11/20261016152026"
TOOLS: Final = R / "GeneralsMD/Code/Tools/water-check"
VIDEO: Final = E / "task-8/readiness/motion-clock-binding-01"
FIXTURE: Final = E / "task-8/readiness/named-motion-fixtures-01"
OVERLAY: Final = FIXTURE / "assets/overlay"
FARM: Final = E / "task-1/helper/authored-user-map-path-8/farm"
CASE_GUIDS: Final = {"teleport":"a9555d31-864b-47f2-b07a-f79dd65a8285","hidden":"820dedb5-cfcb-4949-85f5-c44310c3e532","presentation-shrouded":"cc37727e-044d-44ab-911d-5e12c0fca139","destroy-replacement":"48e8be07-2cbf-4187-85af-367af80f6e42"}
MAP_RELATIVE: Final = "Maps/Water Named Union Fixture/Water Named Union Fixture.map"
EXE: Final = FARM / "generals-task8-probe-green-1.exe"
sys.path.insert(0, str(TOOLS))
sys.path.insert(0, str(VIDEO))
from bitmap import inspect_bmp
from evidence import LOADED_SCENARIO
from isolation import launch_isolated
from control_monitor import ControlPlan
from models import ExpectedImage, ImageObservation, LaunchResult, Rect
from native_video_task8 import NativeVideoRequest, materialize_native_frames
from producer_observation import Identity, analyze
from runtime import merge_standard_map_cache, reserve_control_port, stage_windows_user_map
sys.path.insert(0, str(E / "task-8/readiness/lifecycle-game-controller-private-01"))
sys.path.insert(0, str(E / "task-8/readiness/lifecycle-causal-gate-private-01"))
from lifecycle_causal import Case, make_lifecycle_monitor

@dataclass(slots=True)
class GuardError(RuntimeError):
    code: str
    path: str = ""
    def __str__(self) -> str:
        return f"{self.code}: {self.path}"

@dataclass(frozen=True, slots=True)
class ScenarioLoadReceipt:
    path: str
    actions: int
    parse_refused: int
    execution_summary: str = "unavailable_controlled_mode"
    all_actions_ran: str = "UNPROVED"

def require_scenario_load(text: str) -> ScenarioLoadReceipt:
    """Attest the exact parsed fixture only; never equate loaded actions with executed actions."""
    rows = tuple(line for line in text.splitlines() if line.startswith("SCENARIO: loaded "))
    if len(rows) != 1:
        raise GuardError("scenario-load-cardinality")
    row = rows[0]
    match = LOADED_SCENARIO.fullmatch(row)
    path = "Scenarios\\water_named_union.txt"
    if match is None or row != f"SCENARIO: loaded '{path}', 2 actions, 0 lines refused":
        raise GuardError("scenario-load-invalid")
    return ScenarioLoadReceipt(path, int(match.group(1)), int(match.group(2)))

@dataclass(frozen=True, slots=True)
class CaptureWindow:
    pictures: tuple[int, ...]
    terminal: int

@dataclass(frozen=True, slots=True)
class CaptureProof:
    crc: str
    images: tuple[Pin, ...]
    observations: tuple[ImageObservation, ...]

WINDOW: Final = CaptureWindow((180,181,182),300)
SANITY: Final = ExpectedImage(Rect(0,0,1280,480),1,0.0,1.0)

def actual_pin(path: Path) -> Pin:
    data = path.read_bytes()
    return Pin(str(path), len(data), hashlib.sha256(data).hexdigest())

def verify_pin(pin: Pin) -> None:
    observed = actual_pin(Path(pin.path))
    if (observed.bytes,observed.sha256.lower()) != (pin.bytes,pin.sha256.lower()):
        raise GuardError("stale-pin",pin.path)

def verify_inputs() -> None:
    for pin in PINS:
        verify_pin(pin)

def require_vacant(paths: tuple[Path, ...]) -> None:
    for path in paths:
        if path.exists():
            raise GuardError("namespace-collision",str(path))

def require_go(go: bool) -> None:
    if not go:
        raise GuardError("GAME-held")

def native_command(port: int, user: Path, case: Case) -> tuple[str, ...]:
    token = CASE_GUIDS[case.value]
    video = f"water-lifecycle-{case.value}-{token[:8]}"
    prefix = f"w8lc_{token[:8]}_"
    if port < 1 or port > 65535:
        raise GuardError("unresolved-control-port")
    return (str(EXE),"-root",str(FARM),"-mod",str(OVERLAY),"-multiInstance","-noaudio","-win","-xres","1280","-yres","720","-msaa","0","-quickstart","-noshellmap","-nologo","-novideo","-noFPSLimit","-map",str(user / MAP_RELATIVE),"-autoskirmish","2","-seed","1","-side","0","FactionAmerica","-side","1","FactionChina","-observer","-freecam","900","-600","3600","90","-70","-control",str(port),"-scenario","water_named_union","-video","180","182",video,"-recordfps","60","-maxframes","300","-logPrefix",prefix)

def capture_proof(result: LaunchResult, request: NativeVideoRequest, window: CaptureWindow) -> CaptureProof:
    if result.returncode != 0 or result.timed_out or result.control_error is not None or not result.control_quit_requested or result.evidence_frame < window.terminal:
        raise GuardError("process-not-normal")
    match = re.search(r"(?m)^HEADLESS CRC AT LIMIT: (0x[0-9A-Fa-f]{8}) at frame " + str(window.terminal) + r"\r?$", request.runtime_text)
    if match is None:
        raise GuardError("terminal-CRC-missing")
    native = request.user_data / "Videos" / request.video_name
    if tuple(p.name for p in sorted(native.glob("frame*.bmp"))) != ("frame000000.bmp","frame000001.bmp","frame000002.bmp"):
        raise GuardError("native-three-BMP-required")
    frames = materialize_native_frames(request,expected_pictures=window.pictures)
    observations = tuple(inspect_bmp(frame.path,SANITY) for frame in frames)
    if any(image.width != 1280 or image.height != 720 for image in observations):
        raise GuardError("capture-dimensions")
    return CaptureProof(match.group(1),tuple(actual_pin(frame.path) for frame in frames),observations)

def remove_owned(pin: Pin, root: Path) -> None:
    path = Path(pin.path)
    if path.is_symlink() or not path.resolve().is_relative_to(root.resolve()):
        raise GuardError("cleanup-escape",str(path))
    verify_pin(pin)
    path.unlink()

def run(case: Case, *, go: bool = False) -> None:
    require_go(go)
    token = CASE_GUIDS[case.value]
    OUTPUT = E / f"task-8/native/lifecycle-{case.value}-{token}"
    prefix = f"w8lc_{token[:8]}_"
    video = f"water-lifecycle-{case.value}-{token[:8]}"
    verify_inputs()
    cache = FARM / "Maps/MapCache.ini"
    require_vacant((OUTPUT,cache,FARM / "Data/INI/GameData.ini",*tuple(FARM.glob(prefix+"*DebugLogFile.txt"))))
    if any(re.match(r"^ZH_(WATER_|DX11_)",name) for name in os.environ):
        raise GuardError("inherited-override")
    if shutil.disk_usage(OUTPUT.parent).free < 268435456:
        raise GuardError("capture-headroom")
    OUTPUT.mkdir()
    user = OUTPUT / "user"
    user.mkdir()
    (user / "Options.ini").write_text("WaterStyle = 1\r\n",encoding="utf-8")
    port = reserve_control_port()
    command = native_command(port,user,case)
    environment = {"ZH_USER_DATA_DIR":str(user),"ZH_UNATTENDED":"1","ZH_AUDIO_BACKEND":"null","ZH_DX11_DRIVER":"hardware","ZH_WATER_CAPTURE_CLOCK":"picture","ZH_WATER_CAPTURE_FIXTURE":"carrier"}
    environment.update({"ZH_WATER_"+name+"_TRACE":"1" for name in ("CAPTURE_CLOCK","PAIR","GRID","ROUTE","NATIVE_RESOURCE","INIT","SHORE","HULL","VESSEL_STATE","FOAM","PARTICLE")})
    (OUTPUT / "launch-intent.json").write_text(json.dumps({"argv":command,"environment":environment,"cwd":str(FARM),"timeoutSeconds":600,"port":port,"pid":os.getpid(),"gameAuthorizedByExplicitGo":True,"case":case.value,"ownedWrites":[str(cache),str(user / MAP_RELATIVE),str(user / "Maps/Water Named Union Fixture/map.ini")],"teardown":"original isolation Popen/socket finally; pin-checked owned cache/user loose files only; exact native PID/birth/port closure independently required"},indent=2),encoding="utf-8")
    cleanup: list[Pin] = []
    try:
        stage_windows_user_map(MAP_RELATIVE,OVERLAY,user)
        try:
            cache = merge_standard_map_cache(FARM,FIXTURE / "assets/WaterNamedUnionFixtureMapCache.ini")
        except (FileNotFoundError,RuntimeError,ValueError):
            if cache.is_file():
                cleanup.append(actual_pin(cache))
            raise
        cleanup.append(actual_pin(cache))
        started_ns = time.time_ns()
        plan = ControlPlan(port,quit_frame=300,screenshot_frames=())
        callback = make_lifecycle_monitor(case.value,OUTPUT / "run.log",OUTPUT / "lifecycle-events.jsonl","WaterFixtureUnion",1000.0,425.0,evidence=True)
        result = launch_isolated(command,OUTPUT,600,environment,working_directory=FARM,control_plan=plan,monitor=callback)
        engine_logs = tuple(path for path in FARM.glob(prefix+"*DebugLogFile.txt") if path.stat().st_mtime_ns >= started_ns)
        if len(engine_logs) != 1:
            raise GuardError("engine-log-identity",str(FARM))
        shutil.copy2(engine_logs[0],OUTPUT / "engine.log")
        text = (OUTPUT / "run.log").read_text() + "\n" + (OUTPUT / "engine.log").read_text()
        scenario_load = require_scenario_load(text)
        proof = capture_proof(result,NativeVideoRequest(user,video,OUTPUT / "unused-decode",text),WINDOW)
        identity = Identity(OUTPUT.name,SOURCE_MANIFEST_SHA256,STAGE_SHA256,"actual")
        producer = analyze(text,identity,identity,hashlib.sha256(text.encode()).hexdigest(),"observe","default","hardware")
        if producer["links"] == 0 or producer["driver"] != "hardware":
            raise GuardError("native-producer-positive-link-required")
        verify_inputs()
        (OUTPUT / "report.json").write_text(json.dumps({"verdict":"CONTROL_CAUSAL_CAPTURE_READY_FOR_INDEPENDENT_NATIVE_SEMANTIC_ADJUDICATION","process":asdict(result),"capture":asdict(proof),"producer":producer,"scenarioLoad":asdict(scenario_load),"case":case.value,"lifecycleEvents":asdict(actual_pin(OUTPUT / "lifecycle-events.jsonl")),"semanticAdjudication":"PENDING actual raw admission/skip/point-geometry, rendered pose/stamps, native issue, original identity/port closure and visuals; presentation mode is not observed admission; cap missing UNKNOWN; no birth/ABA claim"},indent=2),encoding="utf-8")
    finally:
        for relative in ("dx11shaders.cache","Maps/MapCache.ini",MAP_RELATIVE,"Maps/Water Named Union Fixture/map.ini","Replays/00000000.rep","ModelChecksums.txt"):
            path = user / relative
            if path.is_file():
                cleanup.append(actual_pin(path))
        (OUTPUT / "cleanup-intent.json").write_text(json.dumps([asdict(pin) for pin in cleanup],indent=2),encoding="utf-8")
        for pin in cleanup:
            remove_owned(pin,FARM if Path(pin.path)==cache else user)
        (OUTPUT / "cleanup.json").write_text(json.dumps({"removed":[asdict(pin) for pin in cleanup],"retained":"originalBMP/options/logs; nofullownerPASS"},indent=2),encoding="utf-8")

