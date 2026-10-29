#!/bin/sh
# Compila NativeLink (lado PSP, pspdev) y NativeDraw (lado Vita, vitasdk), arma el instalador (NativePatch.vpk, que los
# lleva adentro) y deja todo en build/ del repo.
# Antes, una vez: sh tools/preparar.sh (Linux x86_64: contenedor de Claude o WSL) o sh tools/mac/preparar_vm.sh (VM del Mac).
# Uso: [V=<carpeta del relevo>] sh tools/compilar.sh [version de prueba, p. ej. 0.0.2.1]
#   con version: ademas build/<plugin>_<version>.* y build/cambios_<version>.patch (git diff), de respaldo
#   con V (p. ej. ~/Downloads/vitabridge): copia a V/enviar/ (el relevo lo sube a la Vita), V/respaldo/ y a la carpeta
#   de V (Descargas)
set -e
T=${T:-$HOME/tc}; . $T/env.sh
R=$(cd "$(dirname "$0")/.." && pwd); B=$R/build; mkdir -p $B
make -s -C $R/nativelink ADRENALINE=$ADRENALINE && cp $R/nativelink/nativelink.prx $B/
(cd $R/nativedraw && O=$B/nativedraw sh build.sh >/dev/null) && cp $B/nativedraw/nativedraw.suprx $B/
# Instalador: version de cada plugin (de sus fuentes), app sin firma "segura" (necesita escribir en ur0:tai)
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
