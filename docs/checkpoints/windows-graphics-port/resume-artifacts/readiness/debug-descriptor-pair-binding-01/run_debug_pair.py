# /// script
# requires-python = ">=3.11"
# dependencies = ["typer>=0.19,<1"]
# ///
# How to run: pinned Python -B run_debug_pair.py --go --case normal|wireframe; separate root GO per fixed case.
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
from typing import Annotated, Final, assert_never
from enum import StrEnum
import typer
from bound_inputs import PINS, Pin, SOURCE_MANIFEST_SHA256, STAGE_SHA256

R: Final = Path("<REPO>")
E: Final = R / ".omo/evidence/windows-modern-water-d3d11/20261016152026"
TOOLS: Final = R / "GeneralsMD/Code/Tools/water-check"
VIDEO: Final = E / "task-8/readiness/motion-clock-binding-01"
FIXTURE: Final = E / "task-8/readiness/named-motion-fixtures-01"
OVERLAY: Final = FIXTURE / "assets/overlay"
FARM: Final = E / "task-1/helper/authored-user-map-path-8/farm"

MAP_RELATIVE: Final = "Maps/Water Named Twolevel Fixture/Water Named Twolevel Fixture.map"
EXE: Final = FARM / "generals-task8-debug-descriptor-1.exe"
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
    path = "Scenarios\\water_named_twolevel.txt"
    if match is None or row != f"SCENARIO: loaded '{path}', 4 actions, 0 lines refused":
        raise GuardError("scenario-load-invalid")
    return ScenarioLoadReceipt(path, int(match.group(1)), int(match.group(2)))

class Case(StrEnum):
    NORMAL = "normal"
    WIREFRAME = "wireframe"

@dataclass(frozen=True, slots=True)
class DescriptorProof:
    actual_driver: str
    rows: tuple[str, ...]
    scope: str = "native point descriptor only; owner/image joins require manual review; missing bridge keys UNKNOWN"

def pair_values(value: str) -> tuple[int, int]:
    match = re.fullmatch(r"\(([0-9]+),([0-9]+)\)",value)
    if match is None:
        raise GuardError("malformed-native-span",value)
    return int(match[1]),int(match[2])

def descriptor_proof(text: str, case: Case) -> DescriptorProof:
    """Require an actual returned native span; never infer state from requested flags."""
    expected = 3
    expected_fill = "SOLID"
    match case:
        case Case.NORMAL:
            expected = 3
        case Case.WIREFRAME:
            expected = 2
            expected_fill = "WIREFRAME"
        case unreachable:
            assert_never(unreachable)
    rows: list[str] = []
    for row in text.splitlines():
        if not row.startswith("water-particle-observe: "):
            continue
        fields = dict(re.findall(r"([a-z][a-z0-9-]*)=(\([^)]*\)|\S+)",row))
        if fields.get("phase") != "point-render-returned" or fields.get("native-rs-query") != "1":
            continue
        if fields.get("native-rs-enum") != str(expected) or fields.get("native-rs-fill") != expected_fill:
            continue
        required = {"mirror":"0","ordinary":"1","skipped":"0","admitted":"0","stats-sampled":"1","stats-active":"(1,1)"}
        if any(fields.get(key) != value for key,value in required.items()):
            continue
        made, refused, render = (pair_values(fields[key]) for key in ("draws-made","draws-refused","stats-render"))
        if int(fields["particles"]) > 0 and int(fields["built"]) > 0 and int(fields["owner"]) > 0 and made[1] == made[0]+1 and refused[1] == refused[0] and render[1] == render[0]:
            rows.append(row)
    if case is Case.WIREFRAME and not rows:
        raise GuardError("wireframe-native-descriptor-span-unobserved")
    devices = re.findall(r"(?m)^water-device: frame=[0-9]+ patch=[0-9]+ software=([01]) swapchain=([01])\r?$",text)
    actual_driver = "UNKNOWN"
    if devices:
        if any(software != "0" or swap != "1" for software,swap in devices):
            raise GuardError("observed-device-not-hardware-swapchain")
        actual_driver = "hardware"
    return DescriptorProof(actual_driver,tuple(rows))

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
    if port < 1 or port > 65535:
        raise GuardError("unresolved-control-port")
    extra: tuple[str, ...] = ()
    match case:
        case Case.NORMAL:
            extra = ()
        case Case.WIREFRAME:
            extra = ("-wireframe",)
        case unreachable:
            assert_never(unreachable)
    return (str(EXE),"-root",str(FARM),"-mod",str(OVERLAY),"-multiInstance","-noaudio","-win","-xres","1280","-yres","720","-msaa","0","-quickstart","-noshellmap","-nologo","-novideo","-noFPSLimit","-map",str(user / MAP_RELATIVE),"-autoskirmish","2","-seed","1","-side","0","FactionAmerica","-side","1","FactionChina","-observer","-freecam","900","-600","3600","90","-70","-control",str(port),"-scenario","water_named_twolevel","-video","180","182",case.value,"-recordfps","60","-maxframes","300","-logPrefix",f"w8debug{case.value}_") + extra

def capture_proof(result: LaunchResult, request: NativeVideoRequest, window: CaptureWindow) -> CaptureProof:
    if result.returncode != 0 or result.timed_out or result.control_error is not None:
        raise GuardError("process-not-normal")
    match = re.search(r"(?m)^HEADLESS CRC AT LIMIT: (0x[0-9A-Fa-f]{8}) at frame " + str(window.terminal) + r"\r?$", request.runtime_text)
    if match is None:
        raise GuardError("terminal-CRC-missing")
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

def run(case: Case) -> None:
    output = E / ("task-8/native/debug-descriptor-" + case.value + "-actual-01")
    verify_inputs()
    cache = FARM / "Maps/MapCache.ini"
    require_vacant((output,cache,FARM / "Data/INI/GameData.ini",*tuple(FARM.glob(f"w8debug{case.value}_*DebugLogFile.txt"))))
    if any(re.match(r"^ZH_(WATER_|DX11_)",name) for name in os.environ):
        raise GuardError("inherited-override")
    if shutil.disk_usage(output.parent).free < 268435456:
        raise GuardError("capture-headroom")
    output.mkdir()
    user = output / "user"
    user.mkdir()
    (user / "Options.ini").write_text("WaterStyle = 1\r\n",encoding="utf-8")
    port = reserve_control_port()
    command = native_command(port,user,case)
    environment = {"ZH_USER_DATA_DIR":str(user),"ZH_UNATTENDED":"1","ZH_AUDIO_BACKEND":"null","ZH_DX11_DRIVER":"hardware","ZH_WATER_CAPTURE_CLOCK":"picture","ZH_WATER_CAPTURE_FIXTURE":"carrier"}
    environment.update({"ZH_WATER_"+name+"_TRACE":"1" for name in ("CAPTURE_CLOCK","PAIR","GRID","ROUTE","NATIVE_RESOURCE","INIT","SHORE","HULL","VESSEL_STATE","FOAM","PARTICLE")})
    (output / "launch-intent.json").write_text(json.dumps({"argv":command,"environment":environment,"cwd":str(FARM),"timeoutSeconds":600,"port":port,"pid":os.getpid(),"gameAuthorizedByExplicitGo":True},indent=2),encoding="utf-8")
    cleanup: list[Pin] = []
    try:
        stage_windows_user_map(MAP_RELATIVE,OVERLAY,user)
        try:
            cache = merge_standard_map_cache(FARM,FIXTURE / "assets/WaterNamedTwolevelFixtureMapCache.ini")
        except (FileNotFoundError,RuntimeError,ValueError):
            if cache.is_file():
                cleanup.append(actual_pin(cache))
            raise
        cleanup.append(actual_pin(cache))
        started_ns = time.time_ns()
        plan = ControlPlan(port,quit_frame=300,screenshot_frames=())
        result = launch_isolated(command,output,600,environment,working_directory=FARM,control_plan=plan)
        engine_logs = tuple(path for path in FARM.glob(f"w8debug{case.value}_*DebugLogFile.txt") if path.stat().st_mtime_ns >= started_ns)
        if len(engine_logs) != 1:
            raise GuardError("engine-log-identity",str(FARM))
        shutil.copy2(engine_logs[0],output / "engine.log")
        text = (output / "run.log").read_text() + "\n" + (output / "engine.log").read_text()
        scenario_load = require_scenario_load(text)
        proof = capture_proof(result,NativeVideoRequest(user,case.value,output / "unused-decode",text),WINDOW)
        (output / "capture.json").write_text(json.dumps(asdict(proof),indent=2),encoding="utf-8")
        identity = Identity(output.name,SOURCE_MANIFEST_SHA256,STAGE_SHA256,"actual")
        descriptor = descriptor_proof(text,case)
        producer = None
        match case:
            case Case.NORMAL:
                producer = analyze(text,identity,identity,hashlib.sha256(text.encode()).hexdigest(),"observe","default","hardware")
            case Case.WIREFRAME:
                pass  # CLEAR_LINE may have no Modern snapshot/FFT.
            case unreachable:
                assert_never(unreachable)
        verify_inputs()
        (output / "report.json").write_text(json.dumps({"verdict":"CAPTURE_MODE_DESCRIPTOR_OBSERVED_MANUAL_OWNER_REVIEW_PENDING","process":asdict(result),"capture":asdict(proof),"producer":producer,"scenarioLoad":asdict(scenario_load),"descriptor":asdict(descriptor),"twoOwnersSettledVisibleCovered":"UNVERIFIED pending actualraw/manualreview"},indent=2),encoding="utf-8")
    finally:
        for relative in ("dx11shaders.cache","Maps/MapCache.ini",MAP_RELATIVE,"Maps/Water Named Twolevel Fixture/map.ini","Replays/00000000.rep","ModelChecksums.txt"):
            path = user / relative
            if path.is_file():
                cleanup.append(actual_pin(path))
        (output / "cleanup-intent.json").write_text(json.dumps([asdict(pin) for pin in cleanup],indent=2),encoding="utf-8")
        for pin in cleanup:
            remove_owned(pin,FARM if Path(pin.path)==cache else user)
        (output / "cleanup.json").write_text(json.dumps({"removed":[asdict(pin) for pin in cleanup],"retained":"originalBMP/options/logs; nofullownerPASS"},indent=2),encoding="utf-8")

app = typer.Typer(add_completion=False)
@app.command()
def main(case: Annotated[Case,typer.Option("--case")], go: Annotated[bool,typer.Option("--go")] = False) -> None:
    require_go(go)
    run(case)

if __name__ == "__main__":
    app()
