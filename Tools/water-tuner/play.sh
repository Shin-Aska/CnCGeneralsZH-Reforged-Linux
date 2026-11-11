#!/bin/bash
# Play this checkout's build with clean FFT water and the live water tuner.
# Usage: Tools/water-tuner/play.sh [extra game arguments, e.g. -loadsave 00000006]
# Uses its own settings folder (build-linux/play-tune), seeded once from your normal
# settings and saves, so your real profile is never touched. Tuning values live in
# build-linux/play-tune/water-tuning.ini and persist between runs.
WT=$(cd "$(dirname "$0")/../.." && pwd)
U=$WT/build-linux/play-tune
# Set ZH_ROOT to your Zero Hour install if it is not in Steam's default library.
ROOT=${ZH_ROOT:-"$HOME/.local/share/Steam/steamapps/common/Command & Conquer Generals - Zero Hour"}
OV=$WT/build-linux/overlay
D="$HOME/.local/share/Command and Conquer Generals Zero Hour Data"
PORT=${PORT:-8765}
if [ ! -d "$U" ]; then
  mkdir -p "$U/Save"
  cp "$D/Options.ini" "$D/Hotkeys.ini" "$D/Registry.ini" "$U/" 2>/dev/null
  cp "$D"/Save/*.sav "$U/Save/" 2>/dev/null
fi
# Options.ini's resolution wins once a match starts, so keep it at the window size too.
sed -i "s/^Resolution = .*/Resolution = ${XRES:-1440} ${YRES:-810}/" "$U/Options.ini" 2>/dev/null
[ -d "$ROOT" ] || { echo "Game data not found: $ROOT (set ZH_ROOT)"; exit 1; }
python3 "$WT/Tools/water-tuner/water_tuner.py" --file "$U/water-tuning.ini" --port "$PORT" &
TUNER=$!
trap 'kill $TUNER 2>/dev/null' EXIT
echo "Water tuner: http://127.0.0.1:$PORT  (game window ${XRES:-1440}x${YRES:-810}; put the tuner in the space beside it)"
ZH_USER_DATA_DIR=$U/ ZH_WATER_TUNING_FILE=$U/water-tuning.ini ZH_WATER_CLEAN=1 ZH_WATER_TUNING=1 "$WT/build-linux/generals" -root "$ROOT" \
  -overlay "$OV" -win -xres "${XRES:-1440}" -yres "${YRES:-810}" -quickstart -noshellmap -multiInstance "$@"
