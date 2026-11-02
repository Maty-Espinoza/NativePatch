#!/bin/sh
# Runs tests/nptest on PPSSPP (headless, software renderer) to check the scenes on a PC: that none hangs, that their
# hashes do not change, and what each one should look like. It is not a test of the plugins: those only run on the Vita.
# Unpacks in $T/emu (default $HOME/tc) the package of the release named in tools/emu-version, for this system: Linux
# x86_64 (Claude's container, WSL), Linux ARM64 (Claude's VM on the Mac) or macOS ARM64. Needs: $T/env.sh (tools/setup.sh).
# Usage: sh tools/emu.sh        leaves build/nptest/: native_test.log, shots/*.raw and sheet*.png
#        sh tools/emu.sh pack   (.github/workflows/emulator.yml) builds PPSSPP from source and leaves the package in $PWD
set -e
R=$(cd "$(dirname "$0")/.." && pwd); V=$(cat $R/tools/emu-version); T=${T:-$HOME/tc}
PPSSPP=5e39edd444ad66a711cf296658de0b7e8823a920; SDL=release-3.2.24; TTF=release-3.2.2
A=$(uname -s | tr A-Z a-z)-$(uname -m | sed s/arm64/aarch64/); P=$V-$A.tar.xz
if [ "$1" = pack ]; then
  W=$PWD/emu-src; mkdir -p $W; I=$W/inst
  git clone -q --depth 1 -b $SDL https://github.com/libsdl-org/SDL $W/sdl
  git clone -q --depth 1 -b $TTF --recurse-submodules --shallow-submodules https://github.com/libsdl-org/SDL_ttf $W/ttf
  cmake -S $W/sdl -B $W/sdl/b -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=$I -DSDL_TESTS=OFF -DSDL_UNIX_CONSOLE_BUILD=ON
  ninja -C $W/sdl/b install
  cmake -S $W/ttf -B $W/ttf/b -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=$I -DCMAKE_PREFIX_PATH=$I -DSDLTTF_VENDORED=ON
  ninja -C $W/ttf/b install
  git init -q $W/ppsspp; cd $W/ppsspp
  git fetch -q --depth 1 https://github.com/hrydgard/ppsspp $PPSSPP; git checkout -q FETCH_HEAD
  git submodule update -q --init --depth 1 --recursive
  cmake -B b -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=$I -DCMAKE_BUILD_RPATH_USE_ORIGIN=ON -DHEADLESS=ON -DUSING_QT_UI=OFF -DUNITTEST=OFF -DSIMULATOR=OFF
  ninja -C b PPSSPPHeadless
  O=$W/emu; mkdir -p $O/lib; cp b/PPSSPPHeadless $O/; cp LICENSE.TXT $O/
  # The libraries it needs travel with it (the machines that use it have no root to install them)
  if [ $(uname -s) = Darwin ]; then
    dylibbundler -od -b -x $O/PPSSPPHeadless -d $O/lib -p @executable_path/lib/
  else
    LD_LIBRARY_PATH=$I/lib ldd b/PPSSPPHeadless | awk '$3 ~ /^\// {print $3}' | grep -vE '/lib(c|m|dl|rt|pthread)\.so|ld-linux' | xargs -I{} cp -L {} $O/lib/
  fi
  echo "$V PPSSPP $PPSSPP SDL $SDL SDL_ttf $TTF" > $O/version.txt
  cd $W/..; XZ_OPT='-9 -T0' tar cJf $P -C $W --owner=0 --group=0 emu 2>/dev/null || XZ_OPT='-9 -T0' tar cJf $P -C $W emu
  exit
fi
. $T/env.sh
if [ "$(cat $T/emu/version.txt 2>/dev/null | cut -d' ' -f1)" != $V ]; then
  rm -rf $T/emu
  curl -fsSL --retry 3 https://github.com/Maty-Espinoza/NativePatch/releases/download/$V/$P | tar xJ -C $T
fi
B=$R/build/nptest; M=$B/ms; rm -rf $B; mkdir -p $M/PSP/GAME/NPTEST
cp -r $R/tests/nptest $B/src; make -s -C $B/src SHOTS=1 >/dev/null; cp $B/src/EBOOT.PBP $M/PSP/GAME/NPTEST/
LD_LIBRARY_PATH=$T/emu/lib $T/emu/PPSSPPHeadless --graphics=software --timeout=120 --memstick=$M $M/PSP/GAME/NPTEST/EBOOT.PBP >$B/emu.out 2>&1 || true
mv $M/native_test.log $B/; mv $M/nptest_shots $B/shots; rm -rf $B/src $M
python3 $R/tools/nptest_sheet.py $B
N=$(grep -oE '\bS\(|\{ "[a-zA-Z0-9]+", "' $R/tests/nptest/scenes.c | wc -l)
echo "$(wc -l < $B/native_test.log) scenes run, $N in scenes.c"
[ $(wc -l < $B/native_test.log) = $N ]
