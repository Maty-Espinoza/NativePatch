#!/bin/sh
# Compila NativeLink (lado PSP, pspdev) y NativeDraw (lado Vita, vitasdk) y deja los binarios en build/ del repo.
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
if [ -n "$1" ]; then
  cp $B/nativelink.prx $B/nativelink_$1.prx; cp $B/nativedraw.suprx $B/nativedraw_$1.suprx
  git -C $R diff > $B/cambios_$1.patch
fi
if [ -n "$V" ]; then
  mkdir -p $V/enviar/ux0/pspemu/seplugins $V/enviar/ux0/data/nativedraw $V/respaldo
  cp $B/nativelink.prx $V/enviar/ux0/pspemu/seplugins/; cp $B/nativedraw.suprx $V/enviar/ux0/data/nativedraw/
  cp $B/nativelink.prx $B/nativedraw.suprx $V/..
  [ -n "$1" ] && cp $B/*_$1.* $V/respaldo/
fi
cd $B && md5sum nativelink.prx nativedraw.suprx
