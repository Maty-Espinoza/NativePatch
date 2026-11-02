#!/bin/sh
# Automatic test (GitHub, .github/workflows/ci.yml): no functional regression. Runs the capturer test on ARM under qemu
# (nativedraw/prueba_memoria.c decides pass/fail), the log parsers against tests/corrida/, and builds both plugins in
# production (PROD=1, on a copy of nativelink so a dev build in the repo does not mix with it).
# Needs: tools/preparar.sh done, and apt packages qemu-user and gcc-arm-linux-gnueabihf (the capturer test uses
# the Linux libc, which vitasdk does not have).
# Usage: sh tools/test_ci.sh
set -e
T=${T:-$HOME/tc}; . $T/env.sh
R=$(cd "$(dirname "$0")/.." && pwd); B=$R/build/test; rm -rf $B; mkdir -p $B/inc
for d in $VH/include/psp2* $VH/include/vitasdk*; do ln -s $d $B/inc/; done  # (only these: the whole include clashes with libc)

echo "== capturer"
(cd $R/nativedraw && arm-linux-gnueabihf-gcc -static -O2 -mthumb -mfpu=neon -mfloat-abi=hard -w -I$B/inc -o $B/prueba prueba_memoria.c)
$(command -v qemu-arm-static || command -v qemu-arm) $B/prueba

echo "== log parsers"
C=$R/tests/corrida; X=$B/corrida
python3 $R/tools/test_link.py $C $C/version.txt
sh $R/tools/test_local.sh $C $C
# A damaged copy of the run: each script must fail on it. A run with no game must pass.
cp -r $C $X
sed -i 's/fps=[0-9.]*/fps=50.0/; s/vita=si/vita=NO/' $X/nativelink.log
sed -i 's/DISTINTAS=0/DISTINTAS=3/; s/reinicios=0/reinicios=2/; s/seg: frames=[0-9]*/seg: frames=20/' $X/nativedraw_captura.log
sed -i 's/sin_frame_nuevo=10/vertice_fuera=10/' $X/nativedraw.log
echo 'NativeDraw 9.9.9' > $X/version.txt
for t in "test_link.py $X $X/version.txt" "resumen_corrida.py $X --check"; do
  n=$(python3 $R/tools/$t | grep -c FAIL || true); python3 $R/tools/$t >/dev/null && { echo "FAIL: $t did not fail"; exit 1; }
  echo "ok   $t: $n FAIL lines"
done
rm $X/nativelink.log; python3 $R/tools/test_link.py $X | grep 'no game'
python3 $R/tools/test_link.py $X >/dev/null || { echo "FAIL: a run with no game must pass"; exit 1; }

echo "== production build"
cp -r $R/nativelink $B/nativelink
make -s -C $B/nativelink clean ADRENALINE=$ADRENALINE
make -s -C $B/nativelink PROD=1 ADRENALINE=$ADRENALINE
(cd $R/nativedraw && O=$B/nativedraw PROD=1 sh build.sh >$B/nativedraw.log) || { cat $B/nativedraw.log; exit 1; }
ls -l $B/nativelink/nativelink.prx $B/nativedraw/nativedraw.suprx
echo "all ok"
