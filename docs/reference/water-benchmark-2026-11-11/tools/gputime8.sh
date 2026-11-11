#!/bin/bash
# gputime7.sh <label> [KEY=VALUE ...]: synced GPU timing on a sandbox skirmish map (MAP=LightingSandboxUSA02, extra args in XARGS),
# any build (GEN, OV), offscreen, unpaced. Fresh starts load in every version, unlike saves.
L=$1; shift
WT=/mnt/sandisk-2tb/worktrees/water-tempo-ab/build-linux; U=$WT/ab/gpu-$L
ROOT='/mnt/passport/E-Drive/Linux Steam Library/steamapps/common/Command & Conquer Generals - Zero Hour'
D="$HOME/.local/share/Command and Conquer Generals Zero Hour Data"
rm -rf "$U"; mkdir -p "$U/Save"; cp "$D/Options.ini" "$D/Hotkeys.ini" "$D/Registry.ini" "$U/"
[ -n "$OPTS" ] && for kv in $OPTS; do k=${kv%%=*}; v=${kv#*=}; sed -i "s/^$k = .*/$k = $v/" "$U/Options.ini"; done
env "$@" ZH_USER_DATA_DIR=$U/ ZH_OFFSCREEN_HZ=0 ZH_GPU_TIMING=${DELAY:-20},${COUNT:-600} ZH_GPU_TIMING_SYNC=1 ${GEN:-$WT/generals} -root "$ROOT" \
  ${PREOV:+-overlay $PREOV} -overlay "${OV:-/mnt/sandisk-2tb/CnCGeneralsZH-Reforged-Linux/build-linux/overlay}" -quickstart -noshellmap -multiInstance \
  -offscreen -noFPSLimit -overlay /mnt/sandisk-2tb/worktrees/water-tempo-ab/build-linux/ab/bench-maps -map "Maps\\$MAP\\$MAP.map" -autoskirmish 2 -observer -side 0 FactionAmerica -side 1 FactionChina -seed 7 $XARGS > $U/run.out 2>&1 &
PID=$!
for i in $(seq 1 300); do grep -q "timing: GPU" $U/run.out && break; kill -0 $PID 2>/dev/null || break; sleep 1; done
sleep 2; kill $PID 2>/dev/null; sleep 2; kill -9 $PID 2>/dev/null
echo "== $L"; grep -E "timing: (draws|GPU) " $U/run.out
