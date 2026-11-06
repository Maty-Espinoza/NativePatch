#!/bin/sh
# Automatic test (GitHub, .github/workflows/ci.yml): no functional regression. Runs the capturer test on ARM under qemu
# (nativedraw/capture_test.c decides pass/fail), the log parsers against tests/run/, and builds both plugins in
# production (PROD=1, on a copy of nativelink so a dev build in the repo does not mix with it).
# Needs: tools/setup.sh done, and apt packages qemu-user and gcc-arm-linux-gnueabihf (the capturer test and bench use
# the Linux libc, which vitasdk does not have).
# Usage: sh tools/test_ci.sh
set -e
T=${T:-$HOME/tc}; . $T/env.sh
R=$(cd "$(dirname "$0")/.." && pwd); B=$R/build/test; rm -rf $B; mkdir -p $B/inc
for d in $VH/include/psp2* $VH/include/vitasdk*; do ln -s $d $B/inc/; done  # (only these: the whole include clashes with libc)

echo "== capturer"
(cd $R/nativedraw && arm-linux-gnueabihf-gcc -static -O2 -mthumb -mfpu=neon -mfloat-abi=hard -w -I$B/inc -o $B/prueba capture_test.c)
$(command -v qemu-arm-static || command -v qemu-arm) $B/prueba

echo "== translator: GPU and CPU paths draw the same triangles"
G=$(T=$T sh $R/tools/perf/tr_gv.sh); echo "$G" | grep -q "lines equal" || { echo "FAIL: tr_gv"; exit 1; }
# the crowd frame: draws joined by indices, thousands of draws arrive as hundreds of GXM calls
[ "$(echo "$G" | grep 'draws=3688' | sed 's/.*gxm_draws=\([0-9]*\).*/\1/' | head -1)" -lt 400 ] || { echo "FAIL: GXM draws in the crowd"; exit 1; }
# God of War combat: a shadow atlas 1024 wide, a glow chain in one row and more than 64K vertices, all in the two surfaces
echo "$G" | grep 'draws=6642' | grep -q 'result=ok' || { echo "FAIL: God of War combat frame not drawn"; exit 1; }
[ "$(echo "$G" | grep 'draws=6642' | sed 's/.*gpu_vertices=[0-9]*,[0-9]*,[0-9]*,\([0-9]*\).*/\1/')" -gt 50000 ] || { echo "FAIL: God of War type 0x1BE not on the GPU"; exit 1; }
echo "== capturer bench (builds and runs one scene)"
arm-linux-gnueabihf-gcc -static -O2 -mthumb -mfpu=neon -mfloat-abi=hard -w -I$B/inc -o $B/bench $R/tools/perf/capture_bench.c
$(command -v qemu-arm-static || command -v qemu-arm) $B/bench rr_race | grep -q "complete=38" || { echo "FAIL: capture_bench"; exit 1; }
echo "ok   capture_bench"
echo "== memory-reading lab (photo at the sync draws nothing wrong; today's capture drops the God of War frames)"
arm-linux-gnueabihf-gcc -static -O2 -mthumb -mfpu=neon -mfloat-abi=hard -w -I$B/inc -o $B/sync_lab $R/tools/perf/sync_lab.c
$(command -v qemu-arm-static || command -v qemu-arm) $B/sync_lab check

echo "== log parsers"
C=$R/tests/run; X=$B/corrida
python3 $R/tools/test_link.py $C $C/version.txt
sh $R/tools/test_local.sh $C $C
# A damaged copy of the run: each script must fail on it. A run with no game must pass.
cp -r $C $X
sed -i 's/fps=[0-9.]*/fps=50.0/; s/vita=yes/vita=NO/' $X/nativelink.log
sed -i 's/MISMATCHES=0/MISMATCHES=3/; s/resets=0/resets=2/; s/sec: frames=[0-9]*/sec: frames=20/' $X/nativedraw_capture.log
sed -i 's/no_new_frame=10/vertex_out=10/' $X/nativedraw.log
echo 'NativeDraw 9.9.9' > $X/version.txt
for t in "test_link.py $X $X/version.txt" "run_summary.py $X --check"; do
  n=$(python3 $R/tools/$t | grep -c FAIL || true); python3 $R/tools/$t >/dev/null && { echo "FAIL: $t did not fail"; exit 1; }
  echo "ok   $t: $n FAIL lines"
done
rm $X/nativelink.log; python3 $R/tools/test_link.py $X | grep 'no game'
python3 $R/tools/test_link.py $X >/dev/null || { echo "FAIL: a run with no game must pass"; exit 1; }

printf '  1000 prof psp: frame=60/16000/20000 game=60/9000/12000\n  2000 prof psp: frame=60/20000/30000 game=60/9000/12000\n' > $B/prof.log
python3 $R/tools/prof_summary.py $B/prof.log | grep -q 'game: 60.0 fps, frame 18.0 ms' || { echo "FAIL: prof_summary.py"; exit 1; }
echo "ok   prof_summary.py"

printf ' 1000 bottleneck: slots 40 drawn 20 repeated 5 behind 10 no_record 0 normal 0 | ms: translator 600 gpu_wait 100 | translator ms: check 5 textures 50 vertices 300 gxm 100 scenes 45 | gpu ms: adrenaline 40 normal 0 surface 500 intermediate 300 idle 100\n' > $B/bn.log
python3 $R/tools/bottleneck.py $B/bn.log | grep -q 'translator limits' || { echo "FAIL: bottleneck.py"; exit 1; }
mkdir $B/bn; mv $B/bn.log $B/bn/nativedraw.log
printf ' 1000   capture split complete: frames=20 commands=8000 draws=900 | us/f: total=10000 cpu=9000 events=500 flip=200 walk=9000 hit=500 own=800 draw=4000 record=3000 state=500 index=0 vertices=1000 blocks=0 rows=0 textures=1000 vflush=100\n' > $B/bn/nativedraw_capture.log
python3 $R/tools/bottleneck.py $B/bn | grep -q 'interpreter 3.7' || { echo "FAIL: bottleneck.py capture split"; exit 1; }
echo "ok   bottleneck.py"

echo "== nd_fa alpha threshold"
python3 - <<'PY' || { echo "FAIL: nd_fa threshold"; exit 1; }
# translator.c turns GE greater and greater or equal into a minimum on the 0-1 alpha: it must agree with the 8-bit comparison
import struct, sys
f = lambda x: struct.unpack('f', struct.pack('f', x))[0]
for r in range(256):
    for a in range(256):
        x = f(f(a) / f(255))
        if (x >= f(f(r + .5) / f(255))) != (a > r) or (x >= f(f(r - .5) / f(255))) != (a >= r): sys.exit(f'alpha {a} ref {r}')
PY
echo "ok   nd_fa threshold"

echo "== production build"
cp -r $R/nativelink $B/nativelink
make -s -C $B/nativelink clean ADRENALINE=$ADRENALINE
make -s -C $B/nativelink PROD=1 ADRENALINE=$ADRENALINE
(cd $R/nativedraw && O=$B/nativedraw PROD=1 sh build.sh >$B/nativedraw.log) || { cat $B/nativedraw.log; exit 1; }
cp -r $R/tests/nptest $B/nptest; make -s -C $B/nptest >/dev/null  # (the synthetic battery still builds)
ls -l $B/nativelink/nativelink.prx $B/nativedraw/nativedraw.suprx
ls -l $B/nptest/EBOOT.PBP
echo "all ok"
