#!/bin/sh
# Compila NativeLink (lado PSP, pspdev, con su Makefile) y NativeDraw (lado Vita, nativedraw/build.sh), arma el
# instalador NativePatch.vpk (lleva los dos plugins y un version.txt) y deja todo en build/ del repo; al final muestra
# los md5. Lo llaman a mano, tools/windows/compilar.cmd, tools/windows/instalar.sh y .github/workflows/ci.yml.
# Antes, una vez: sh tools/preparar.sh (Linux x86_64) o sh tools/mac/preparar_vm.sh (VM ARM64 del Mac); los dos dejan
# $T/env.sh con las variables que usa este script. Ambientes y flujo de trabajo: docs/agent/<idioma>/environment.md.
# Uso: [V=<carpeta del relevo>] sh tools/compilar.sh [version de prueba]
#   con version: ademas copias build/<plugin>_<version>.*, NativePatch_<version>.vpk y cambios_<version>.patch (git diff)
#   con V (p. ej. ~/Downloads/vitabridge): los plugins a V/enviar/ (el relevo los sube a la Vita), los plugins y el VPK
#   a la carpeta de arriba de V (Descargas) y las copias con version a V/respaldo/
set -e
T=${T:-$HOME/tc}; . $T/env.sh
R=$(cd "$(dirname "$0")/.." && pwd); B=$R/build; mkdir -p $B
make -s -C $R/nativelink ADRENALINE=$ADRENALINE && cp $R/nativelink/nativelink.prx $B/
(cd $R/nativedraw && O=$B/nativedraw sh build.sh >/dev/null) && cp $B/nativedraw/nativedraw.suprx $B/
# Instalador: version.txt con la version de cada plugin (sacada de nativelink/log.h y nativedraw/version.h).
# vita-make-fself sin -s: app no "segura", necesaria para que pueda escribir en ur0:tai.
VL=$(sed -n 's/.*NATIVELINK_VERSION "\([^"]*\)".*/\1/p' $R/nativelink/log.h)
VD=$(sed -n 's/.*NATIVEDRAW_VERSION "\([^"]*\)".*/\1/p' $R/nativedraw/version.h)
I=$B/instalador; rm -rf $I; mkdir -p $I
printf 'NativeLink %s\nNativeDraw %s\n' $VL $VD > $I/version.txt
arm-vita-eabi-gcc -O2 -Wall -Wl,-q -o $I/eboot.elf $R/instalador/main.c -lSceDisplay_stub -lSceCtrl_stub -lScePower_stub
vita-elf-create $I/eboot.elf $I/eboot.velf >/dev/null && vita-make-fself -c $I/eboot.velf $I/eboot.bin >/dev/null
vita-mksfoex -s TITLE_ID=NATV00001 -s APP_VER=01.00 "NativePatch" $I/param.sfo
S=$R/instalador/sce_sys
vita-pack-vpk -s $I/param.sfo -b $I/eboot.bin -a $B/nativelink.prx=nativelink.prx -a $B/nativedraw.suprx=nativedraw.suprx \
  -a $I/version.txt=version.txt -a $S/icon0.png=sce_sys/icon0.png $B/NativePatch.vpk >/dev/null  # (solo el icono, sin LiveArea)
if [ -n "$1" ]; then
  cp $B/nativelink.prx $B/nativelink_$1.prx; cp $B/nativedraw.suprx $B/nativedraw_$1.suprx; cp $B/NativePatch.vpk $B/NativePatch_$1.vpk
  git -C $R diff > $B/cambios_$1.patch
fi
if [ -n "$V" ]; then
  mkdir -p $V/enviar/ux0/pspemu/seplugins $V/enviar/ux0/data/nativedraw $V/respaldo
  cp $B/nativelink.prx $V/enviar/ux0/pspemu/seplugins/; cp $B/nativedraw.suprx $V/enviar/ux0/data/nativedraw/
  cp $B/nativelink.prx $B/nativedraw.suprx $B/NativePatch.vpk $V/..
  [ -n "$1" ] && cp $B/*_$1.* $V/respaldo/
fi
cd $B && md5sum nativelink.prx nativedraw.suprx NativePatch.vpk && echo "($(cat instalador/version.txt | tr '\n' ' '))"
