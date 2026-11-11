#!/bin/bash
# Version and settings benchmark on the sandbox skirmish maps (fresh starts load in every version).
cd /mnt/sandisk-2tb/worktrees/water-tempo-ab/build-linux/ab
MAIN=/mnt/sandisk-2tb/CnCGeneralsZH-Reforged-Linux/build-linux
V351=/mnt/sandisk-2tb/worktrees/bench-v351/build-linux; V342=/mnt/sandisk-2tb/worktrees/bench-v342/build-linux
CLR=$PWD/clarity-off
run(){ # label map build [PREOV=] [OPTS=] env...
  local L=$1 M=$2 B=$3; shift 3
  MAP=$M DELAY=15 COUNT=600 GEN=$B/generals OV=$B/overlay tools/gputime8.sh b-$L "$@" | grep -E "== |GPU"; }
for M in LightingSandboxUSA02 LightingSandboxSuperweaponGeneral; do s=${M:16:3}
  # A: v3.5.1 vs today's old water vs new water
  run $s-v351-1 $M $V351; run $s-old-1 $M $MAIN ZH_WATER_CLEAN=0; run $s-new-1 $M $MAIN
  run $s-new-2 $M $MAIN; run $s-old-2 $M $MAIN ZH_WATER_CLEAN=0; run $s-v351-2 $M $V351
  # B: v3.4.2 underwater clarity on vs off
  run $s-342on-1 $M $V342; PREOV=$CLR run $s-342off-1 $M $V342; PREOV=$CLR run $s-342off-2 $M $V342; run $s-342on-2 $M $V342
  # C: settings on the new water
  OPTS="WaterFftResolution=0" run $s-fft128 $M $MAIN
  OPTS="WaterRefractionQuality=1" run $s-refrsimple $M $MAIN
  OPTS="WaterRefractionQuality=2" run $s-refroff $M $MAIN
  OPTS="WaterReflection=1" run $s-reflssr $M $MAIN
  OPTS="WaterReflectionEnabled=no" run $s-refloff $M $MAIN
  OPTS="WaterReflectionDetail=0" run $s-refllow $M $MAIN
  run $s-nofoam $M $MAIN ZH_WATER_CLEAN_PROFILE=nofoam
  run $s-noshore $M $MAIN ZH_WATER_CLEAN_PROFILE=noshore
  run $s-shorelm $M $MAIN ZH_WATER_CLEAN_SHORE_BANDS=long_medium
  run $s-nodepth $M $MAIN ZH_WATER_CLEAN_PROFILE=nodepth
  run $s-nomips $M $MAIN ZH_WATER_CLEAN_PROFILE=nomips
  run $s-nosss $M $MAIN ZH_WATER_CLEAN_PROFILE=nosss
  run $s-new-3 $M $MAIN
done
echo CAMPAIGN DONE
