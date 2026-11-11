#!/bin/bash
# Penalty benchmark for upstream: upstream/main vs today's clean FFT water (and today's old water), and
# underwater clarity with vs without its code, on the sandbox skirmish maps. Short method, alternating order.
cd /mnt/sandisk-2tb/worktrees/water-tempo-ab/build-linux/ab
UP=/mnt/sandisk-2tb/worktrees/bench-upstream/build-linux; MAIN=/mnt/sandisk-2tb/CnCGeneralsZH-Reforged-Linux/build-linux
run(){ local L=$1 M=$2 G=$3 O=$4; shift 4
  MAP=$M DELAY=15 COUNT=600 GEN=$G OV=$O tools/gputime8.sh p-$L "$@" | grep -E "== |draws|GPU"; }
for M in LightingSandboxUSA02 LightingSandboxSuperweaponGeneral; do s=${M:16:3}
  run $s-up-1 $M $UP/generals $UP/overlay
  run $s-new-1 $M $MAIN/generals-withclarity $MAIN/overlay
  run $s-old-1 $M $MAIN/generals-withclarity $MAIN/overlay ZH_WATER_CLEAN=0
  run $s-old-2 $M $MAIN/generals-withclarity $MAIN/overlay ZH_WATER_CLEAN=0
  run $s-new-2 $M $MAIN/generals-withclarity $MAIN/overlay
  run $s-up-2 $M $UP/generals $UP/overlay
  run $s-clr-1 $M $MAIN/generals-withclarity $MAIN/overlay
  run $s-noclr-1 $M $MAIN/generals-noclarity $MAIN/overlay
  run $s-noclr-2 $M $MAIN/generals-noclarity $MAIN/overlay
  run $s-clr-2 $M $MAIN/generals-withclarity $MAIN/overlay
done
echo CAMPAIGN DONE
