#!/usr/bin/env bash
# Render the USA and China infantry paradrops and the original USA 2 campaign reinforcement wave.
# Usage: run_ragdoll_paradrop_check.sh <generals>, with ZH_DATA_DIR pointing to a folder holding zerohour/.
set -euo pipefail

generals="${1:?expected the generals executable}"
if [[ -z "${ZH_DATA_DIR:-}" || ! -d "$ZH_DATA_DIR/zerohour" ]]; then
    echo "skip: ragdoll_paradrop needs game data in ZH_DATA_DIR/zerohour"
    exit 77
fi
code="$(cd "$(dirname "$0")/.." && pwd)"
install="$(cd "$ZH_DATA_DIR/zerohour" && pwd -P)"
work="$(mktemp -d "${TMPDIR:-/tmp}/ragdoll-paradrop.XXXXXX")"
trap 'rm -rf -- "${work:?}"' EXIT

. "$code/Tools/install-guard.sh"
install_snapshot "$install" "$work/before"
install_python - "$install" "$work/root" <<'PY'
import os
import shutil
import sys

shutil.copytree(sys.argv[1], sys.argv[2], copy_function=os.symlink, ignore=shutil.ignore_patterns("._*"))
PY
"$code/Tools/stage-overlay.sh" "$code/Data" "$code/../Run" "$work/overlay"
mkdir -p "$work/overlay/Scenarios" "$work/user"
cp "$code/Data/Scenarios/ragdoll_paradrop.txt" "$work/overlay/Scenarios/"
# Development builds write their log next to the executable. Keep it private for this run.
ln "$generals" "$work/generals" 2>/dev/null || cp -p "$generals" "$work/generals"
printf 'ClassicGraphics = no\nRagdoll = yes\n' > "$work/user/Options.ini"
status=0
(
    cd "$work"
    export ZH_UNATTENDED=1 ZH_USER_DATA_DIR="$work/user" ZH_AUDIO_BACKEND=null ZH_OFFSCREEN_HZ=0
    game=(./generals -offscreen -root "$work/root" -overlay "$work/overlay" -quickstart -noshellmap
          -multiInstance -noaudio -noFPSLimit -turbo -xres 960 -yres 600 -seed 7)
    "${game[@]}" -logPrefix powers_ -camera 1470 3710 \
        -map 'Maps\Twilight Flame\Twilight Flame.map' -autoskirmish 3 -takeover -observer \
        -side 0 FactionAmerica -side 1 FactionChinaInfantryGeneral -side 2 FactionGLA \
        -team 0 0 -team 1 0 -team 2 1 -maxframes 1800 -scenario ragdoll_paradrop || exit $?
    # -file selects the campaign map. -autoskirmish lets the drawn run exit at its frame limit.
    "${game[@]}" -logPrefix usa02_ -camera 665 1335 -file 'Maps\MD_USA02\MD_USA02.map' \
        -autoskirmish 2 -maxframes 2500
) > "$work/console" 2>&1 || status=$?
install_verify "$install" "$work/before" "$work/after"
if [[ "$status" != 0 ]]; then
    cat "$work/console" >&2
    if grep -Eq 'SDL_CreateGPUDevice: (No supported SDL_GPU backend found|SDL_HINT_GPU_DRIVER [^ ]+ unsupported!)' "$work/console" &&
       ! grep -Fq 'PosixDevice9: offscreen,' "$work/console"; then
        echo "skip: no offscreen GPU backend"
        exit 77
    fi
    echo "FAIL: generals exited with status $status" >&2
    exit 1
fi

install_python - "$work/powers_DebugLogFile.txt" "$work/usa02_DebugLogFile.txt" <<'PY'
from pathlib import Path
import re
import sys

log = Path(sys.argv[1]).read_text(errors="replace")
live, enemy_spawn, combat = log.partition("spawn slot 2 'GLAVehicleQuadCannon' 2 of 2")
if not enemy_spawn or not re.search(r"^HEADLESS SCENARIO: (\d+) actions, \1 ran, 0 refused,", log, re.M):
    sys.exit("FAIL: the paradrop scenario did not complete both the landing and combat phases")

failures = []
for slot, unit, power in (
    (0, "AmericaInfantryRanger", "SuperweaponParadropAmerica"),
    (1, "Infa_ChinaInfantryMiniGunner", "Infa_SuperweaponInfantryParadrop"),
):
    seat = f"slot {slot} '{unit}'"
    if f" {power}: logic frame " not in live or not re.search(rf"{seat}: 0 alive,", live):
        failures.append(f"{unit}: expected an empty starting squad and the command center's paradrop")

    # The men are inside their parachutes while the chutes themselves are outside the plane.
    chute = re.search(rf"^HEADLESS TALLY: frame (\d+) slot {slot} 'AmericaParachute': 5 alive, .*, 0 inside,", live, re.M)
    full_squad = rf"{seat}: 5 alive, health ([1-9]\d*) of \1, worth \d+, "
    if not chute or not re.search(rf"^HEADLESS TALLY: frame {chute[1]} {full_squad}5 inside,", live, re.M):
        failures.append(f"{unit}: five healthy soldiers were not observed under deployed parachutes")
    arrived = re.search(rf"^HEADLESS ARRIVE: {seat} .*: 5 of 5 within ", log, re.M)
    if not re.search(full_squad + "0 inside,", live) or not arrived:
        failures.append(f"{unit}: expected five healthy survivors to land and walk to the rally point")

    start = f"RAGDOLL: start {unit},"
    early = live.count(start)
    deaths = len(re.findall(rf"^KILLXP frame=\d+ victim={unit} .* killer=GLAVehicleQuadCannon:", combat, re.M))
    late = combat.count(start)
    print(f"{unit}: {early} ragdolls before combat, {deaths} combat deaths, {late} combat ragdolls")
    if early != 0 or deaths != 5 or late != deaths:
        failures.append(f"{unit}: expected zero ragdolls while alive and one for each of five combat deaths")

mission = Path(sys.argv[2]).read_text(errors="replace")
if "HEADLESS RESULT: frame limit reached" not in mission:
    failures.append("USA 2: the opening did not play through")
for unit in ("AmericaInfantryRanger", "AmericaInfantryMissileDefender", "AmericaInfantryPathfinder"):
    # The first wave is already under fire as it lands. Compare each type to its own first death.
    death = re.search(rf"^KILLXP frame=\d+ victim={unit} ", mission, re.M)
    first_death = death.start() if death else len(mission)
    start = f"RAGDOLL: start {unit},"
    early = mission.count(start, 0, first_death)
    print(f"USA 2 {unit}: {early} ragdolls before its first recorded death")
    if early:
        failures.append(f"USA 2: {early} living {unit} started a ragdoll in the opening wave")
    if unit != "AmericaInfantryPathfinder" and start not in mission[first_death:]:
        failures.append(f"USA 2: expected to see a {unit} ragdoll after a real combat death")

if failures:
    sys.exit("\n".join("FAIL: " + message for message in failures))
PY
