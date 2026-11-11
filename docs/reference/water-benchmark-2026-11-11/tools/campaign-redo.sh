#!/bin/bash
# Redo settings runs whose draw count showed a skipped reflection pass; retry up to 3 times.
cd /mnt/sandisk-2tb/worktrees/water-tempo-ab/build-linux/ab
MAIN=/mnt/sandisk-2tb/CnCGeneralsZH-Reforged-Linux/build-linux
run(){ # label map wantdraws [env...]
  local L=$1 M=$2 W=$3; shift 3
  for t in 1 2 3; do
    MAP=$M DELAY=15 COUNT=600 GEN=$MAIN/generals OV=$MAIN/overlay tools/gputime8.sh x-$L "$@" >/dev/null
    d=$(awk '/timing: draws/{print int($7)}' gpu-x-$L/run.out); g=$(awk '/GPU \(fences\)/{print $5, $7}' gpu-x-$L/run.out)
    echo "$L try$t draws_p50 $d gpu_mean_p50 $g"
    [ "$d" -ge $((W-10)) ] && break
  done; }
U=LightingSandboxUSA02; S=LightingSandboxSuperweaponGeneral
for p in "$U 2520 USA" "$S 2784 SWG"; do set -- $p; M=$1; W=$2; s=$3
  run $s-base $M $W
  OPTS="WaterRefractionQuality=1" run $s-refrsimple $M $W
  OPTS="WaterRefractionQuality=2" run $s-refroff $M $W
  run $s-noshore $M $W ZH_WATER_CLEAN_PROFILE=noshore
  run $s-shorelm $M $W ZH_WATER_CLEAN_SHORE_BANDS=long_medium
  run $s-nodepth $M $W ZH_WATER_CLEAN_PROFILE=nodepth
  run $s-nomips $M $W ZH_WATER_CLEAN_PROFILE=nomips
  run $s-nosss $M $W ZH_WATER_CLEAN_PROFILE=nosss
  run $s-nofoam $M $W ZH_WATER_CLEAN_PROFILE=nofoam
  OPTS="WaterFftResolution=0" run $s-low $M $W ZH_WATER_CLEAN_PROFILE=noshore
  run $s-base2 $M $W
done
echo CAMPAIGN DONE
