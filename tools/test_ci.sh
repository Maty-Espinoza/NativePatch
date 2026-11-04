#!/bin/sh
# Automatic test (GitHub, .github/workflows/ci.yml): no functional regression. Runs the capturer test on ARM under qemu
# (nativedraw/capture_test.c decides pass/fail), the log parsers against tests/run/, and builds both plugins in
# production (PROD=1, on a copy of nativelink so a dev build in the repo does not mix with it).
# Needs: tools/setup.sh done, and apt packages qemu-user and gcc-arm-linux-gnueabihf (the capturer test uses
# the Linux libc, which vitasdk does not have).
# Usage: sh tools/test_ci.sh
set -e
T=${T:-$HOME/tc}; . $T/env.sh
R=$(cd "$(dirname "$0")/.." && pwd); B=$R/build/test; rm -rf $B; mkdir -p $B/inc
for d in $VH/include/psp2* $VH/include/vitasdk*; do ln -s $d $B/inc/; done  # (only these: the whole include clashes with libc)

echo "== capturer"
(cd $R/nativedraw && arm-linux-gnueabihf-gcc -static -O2 -mthumb -mfpu=neon -mfloat-abi=hard -w -I$B/inc -o $B/prueba capture_test.c)
$(command -v qemu-arm-static || command -v qemu-arm) $B/prueba

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
printf ' 1000   capture split complete: frames=20 commands=8000 draws=900 | us/f: total=10000 cpu=9000 events=500 flip=200 walk=9000 hit=500 own=800 draw=4000 record=3000 state=500 vertices=1000 textures=1000 vflush=100\n' > $B/bn/nativedraw_capture.log
python3 $R/tools/bottleneck.py $B/bn | grep -q 'interpreter 3.7' || { echo "FAIL: bottleneck.py capture split"; exit 1; }
echo "ok   bottleneck.py"

echo "== production build"
cp -r $R/nativelink $B/nativelink
make -s -C $B/nativelink clean ADRENALINE=$ADRENALINE
make -s -C $B/nativelink PROD=1 ADRENALINE=$ADRENALINE
(cd $R/nativedraw && O=$B/nativedraw PROD=1 sh build.sh >$B/nativedraw.log) || { cat $B/nativedraw.log; exit 1; }
cp -r $R/tests/nptest $B/nptest; make -s -C $B/nptest >/dev/null  # (the synthetic battery still builds)
ls -l $B/nativelink/nativelink.prx $B/nativedraw/nativedraw.suprx
ls -l $B/nptest/EBOOT.PBP
echo "all ok"
