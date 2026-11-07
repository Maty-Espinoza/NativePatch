#!/bin/sh
# Builds NativeLink (PSP side, pspdev, with its Makefile) and NativeDraw (Vita side, nativedraw/build.sh: the user plugin and
# its kernel half), packs the NativePatch.vpk installer (carries the plugins and a version.txt) and leaves everything in the repo's build/; at the
# end it shows the md5s. Called by hand, tools/windows/build.cmd, tools/windows/install.sh and .github/workflows/ci.yml.
# Once before: sh tools/setup.sh (Linux x86_64) or sh tools/mac/setup_vm.sh (the Mac's ARM64 VM); both leave $T/env.sh
# with the variables this script uses. Environments and workflow: docs/agent/en/environment.md.
# Usage: [V=<relay folder>] sh tools/build.sh [test version]
#   with a version: also copies build/<plugin>_<version>.*, NativePatch_<version>.vpk and changes_<version>.patch (git diff)
#   with V (e.g. ~/Downloads/vitabridge): the plugins to V/send/ (the relay uploads them to the Vita), the plugins and
#   the VPK to the folder above V (Downloads) and the versioned copies to V/backup/
set -e
T=${T:-$HOME/tc}; . $T/env.sh
R=$(cd "$(dirname "$0")/.." && pwd); B=$R/build; mkdir -p $B
make -s -C $R/nativelink clean && make -s -C $R/nativelink ADRENALINE=$ADRENALINE || { echo "== NativeLink failed to build"; exit 1; }
cp $R/nativelink/nativelink.prx $B/
(cd $R/nativedraw && O=$B/nativedraw sh build.sh >$B/nativedraw.log) || { cat $B/nativedraw.log; echo "== NativeDraw failed to build"; exit 1; }
cp $B/nativedraw/nativedraw.suprx $B/nativedraw/nativedraw.skprx $B/
# Installer: version.txt with each plugin's version (taken from nativelink/log.h and nativedraw/version.h).
# vita-make-fself without -s: non-"safe" app, needed so it can write to ur0:tai.
VL=$(sed -n 's/.*NATIVELINK_VERSION "\([^"]*\)".*/\1/p' $R/nativelink/log.h)
VD=$(sed -n 's/.*NATIVEDRAW_VERSION "\([^"]*\)".*/\1/p' $R/nativedraw/version.h)
I=$B/installer; rm -rf $I; mkdir -p $I
printf 'NativeLink %s\nNativeDraw %s\n' $VL $VD > $I/version.txt
arm-vita-eabi-gcc -O2 -Wall -Wl,-q -o $I/eboot.elf $R/installer/main.c -lSceDisplay_stub -lSceCtrl_stub -lScePower_stub
vita-elf-create $I/eboot.elf $I/eboot.velf >/dev/null && vita-make-fself -c $I/eboot.velf $I/eboot.bin >/dev/null
vita-mksfoex -s TITLE_ID=NATV00001 -s APP_VER=01.00 "NativePatch" $I/param.sfo
S=$R/installer/sce_sys
vita-pack-vpk -s $I/param.sfo -b $I/eboot.bin -a $B/nativelink.prx=nativelink.prx -a $B/nativedraw.suprx=nativedraw.suprx \
  -a $B/nativedraw.skprx=nativedraw.skprx -a $I/version.txt=version.txt -a $S/icon0.png=sce_sys/icon0.png $B/NativePatch.vpk >/dev/null  # (icon only, no LiveArea)
if [ -n "$1" ]; then
  cp $B/nativelink.prx $B/nativelink_$1.prx; cp $B/nativedraw.suprx $B/nativedraw_$1.suprx; cp $B/nativedraw.skprx $B/nativedraw_$1.skprx; cp $B/NativePatch.vpk $B/NativePatch_$1.vpk
  git -C $R diff > $B/changes_$1.patch
fi
if [ -n "$V" ]; then
  mkdir -p $V/send/ux0/pspemu/seplugins $V/send/ux0/data/nativedraw $V/backup
  cp $B/nativelink.prx $V/send/ux0/pspemu/seplugins/; cp $B/nativedraw.suprx $B/nativedraw.skprx $V/send/ux0/data/nativedraw/
  cp $B/nativelink.prx $B/nativedraw.suprx $B/nativedraw.skprx $B/NativePatch.vpk $V/..
  [ -n "$1" ] && cp $B/*_$1.* $V/backup/
fi
cd $B && md5sum nativelink.prx nativedraw.suprx nativedraw.skprx NativePatch.vpk && echo "($(cat installer/version.txt | tr '\n' ' '))"
