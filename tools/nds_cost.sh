#!/bin/bash
# Estimates the DS ARM9 cost of the core: builds it for ARMv5 soft-float (no FPU, like the DS) and counts instructions under qemu-user.
# Needs: gcc-arm-linux-gnueabi, libc6-dev-armel-cross, qemu-user. usage: tools/nds_cost.sh [stage level]   (prints instructions per step)
cd "$(dirname "$0")/.."; ST=${1:-0}; LV=${2:-0}; T=$(mktemp -d)
for F in "" "-DPNJ_FLOAT"; do
  arm-linux-gnueabi-gcc -std=c99 -O2 -march=armv5te -mfloat-abi=soft -static $F core/pnj_core.c tools/bench_host.c -lm -o $T/b || exit 1
  for DRAW in 0 1; do
    n() { qemu-arm -cpu arm926 -singlestep -d exec -D /dev/stderr $T/b $ST $LV $1 $DRAW 2>&1 >/dev/null | grep -c "^Trace"; }
    a=$(n 100); b=$(n 400)
    echo "stage $((ST+1)) level $LV ${F:-double} draw=$DRAW: $(( (b - a) / 300 )) instructions per step"
  done
done
