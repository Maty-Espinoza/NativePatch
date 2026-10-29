#!/bin/sh
# Compiladores de GePatch (pspdev) y gepresent (vitasdk) en el contenedor Linux x86_64 de Claude (2026-10-29).
# El contenedor se borra con la sesion: esto lo rearma en ~3 min en $T (/home/claude/tc).
# - El proxy del contenedor no deja ver las paginas de github.com (listados de releases ni la API), pero si bajar archivos
#   de releases: se usa el nombre fijo "vitasdk-bootstrap-<host>.tar.bz2" de la ultima instantanea (git ls-remote).
# - GCC 15 de vitasdk usa C23 por defecto: el envoltorio de arm-none-eabi-gcc agrega -std=gnu17 (por taihen.h).
# Uso: sh preparar.sh ; despues: . /home/claude/tc/env.sh y sh compilar.sh
set -e
T=${T:-/home/claude/tc}; mkdir -p $T; cd $T
[ -d pspdev ] || curl -sSL https://github.com/pspdev/pspdev/releases/latest/download/pspdev-ubuntu-latest-x86_64.tar.gz | tar xz
[ -d Adrenaline ] || git clone -q --depth 1 https://github.com/TheOfficialFloW/Adrenaline
if [ ! -d vcore ]; then
  TAG=$(git ls-remote --tags https://github.com/vitasdk/autobuilds | grep -o 'sdk-snapshot-[0-9.]*$' | sort -V | tail -1)
  curl -sSL https://github.com/vitasdk/autobuilds/releases/download/$TAG/vitasdk-bootstrap-x86_64-linux-gnu.tar.bz2 | tar xj
  mv vitasdk vcore; echo "vitasdk $TAG"
fi
[ -d taipkg ] || { mkdir taipkg; curl -sSL https://github.com/vitasdk/packages/releases/download/master/taihen.tar.xz | tar xJ -C taipkg; }
[ -d stubs ] || { mkdir stubs; vcore/bin/vita-libs-gen-2 -yml=vcore/share/vita-headers/db/360 -output=stubs >/dev/null; }
mkdir -p nb; for t in ld nm as; do ln -sf $T/vcore/bin/arm-vita-eabi-$t nb/arm-none-eabi-$t; done
printf '#!/bin/sh\nexec %s -std=gnu17 "$@"\n' $T/vcore/bin/arm-vita-eabi-gcc > nb/arm-none-eabi-gcc; chmod +x nb/arm-none-eabi-gcc
echo "export PSPDEV=$T/pspdev PATH=$T/nb:$T/vcore/bin:$T/pspdev/bin:\$PATH VH=$T/vcore/arm-vita-eabi STUBS=$T/stubs TAI=$T/taipkg ADRENALINE=$T/Adrenaline/cef" > env.sh
. ./env.sh; psp-gcc --version | head -1; arm-none-eabi-gcc --version | head -1
