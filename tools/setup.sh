#!/bin/sh
# Instala en $T ($HOME/tc) los compiladores de NativeLink (pspdev + encabezados de Adrenaline) y NativeDraw (vitasdk,
# paquete taihen y stubs de vita-libs-gen-2) en Linux x86_64: contenedor de Claude, Ubuntu en WSL o GitHub Actions.
# Escribe $T/env.sh, que carga tools/build.sh. Se puede correr de nuevo: salta lo que ya esta.
# Uso: sh tools/setup.sh ; despues: sh tools/build.sh (la VM ARM64 del Mac usa tools/mac/setup_vm.sh)
# By default it unpacks the ready-made package of the release named in tools/toolchain-version (built by
# .github/workflows/toolchain.yml; a copy already in $T is used as is). If that package is missing, or with
# FROM_SOURCE=1, it fetches each part. The package has no absolute paths: stubs, nb/ and env.sh are always made here.
# - GCC 15 de vitasdk usa C23 por defecto y taihen.h no compila: el envoltorio de arm-none-eabi-gcc agrega -std=gnu17.
set -e
V=$(cat "$(dirname "$0")/toolchain-version"); P=$V-linux-x86_64.tar.xz
T=${T:-$HOME/tc}; mkdir -p $T; cd $T
if [ -z "$FROM_SOURCE" ] && [ ! -d vcore ]; then
  [ -f $P ] || curl -fsSL --retry 3 -o $P https://github.com/Maty-Espinoza/NativePatch/releases/download/$V/$P || rm -f $P
  if [ -f $P ]; then tar xJf $P; rm $P; else echo "no package $V: fetching each part"; fi
fi
[ -d pspdev ] || curl -sSL https://github.com/pspdev/pspdev/releases/latest/download/pspdev-ubuntu-latest-x86_64.tar.gz | tar xz
[ -d Adrenaline ] || git clone -q --depth 1 https://github.com/TheOfficialFloW/Adrenaline
if [ ! -d vcore ]; then
  TAG=sdk-snapshot-20261015.850.1
  curl -sSL https://github.com/vitasdk/autobuilds/releases/download/$TAG/vitasdk-bootstrap-x86_64-linux-gnu.tar.bz2 | tar xj
  mv vitasdk vcore; echo "vitasdk $TAG"
fi
[ -d taipkg ] || { mkdir taipkg; curl -sSL https://github.com/vitasdk/packages/releases/download/master/taihen.tar.xz | tar xJ -C taipkg; }
# Un .S por funcion de la base de datos de vita-headers: nativedraw/build.sh arma solo los stubs que usa
[ -d stubs ] || { mkdir stubs; vcore/bin/vita-libs-gen-2 -yml=vcore/share/vita-headers/db/360 -output=stubs >/dev/null; }
# nb/: nombres arm-none-eabi-* que espera nativedraw/build.sh, apuntando a las herramientas de vitasdk
mkdir -p nb; for t in ld nm as; do ln -sf $T/vcore/bin/arm-vita-eabi-$t nb/arm-none-eabi-$t; done
printf '#!/bin/sh\nexec %s -std=gnu17 "$@"\n' $T/vcore/bin/arm-vita-eabi-gcc > nb/arm-none-eabi-gcc; chmod +x nb/arm-none-eabi-gcc
echo "export PSPDEV=$T/pspdev PATH=$T/nb:$T/vcore/bin:$T/pspdev/bin:\$PATH VH=$T/vcore/arm-vita-eabi STUBS=$T/stubs TAI=$T/taipkg ADRENALINE=$T/Adrenaline/cef" > env.sh
. ./env.sh; psp-gcc --version | head -1; arm-none-eabi-gcc --version | head -1
