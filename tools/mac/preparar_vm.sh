#!/bin/sh
# Como tools/preparar.sh, pero para la VM Linux ARM64 que usa Claude en el Mac (Ubuntu 22.04 sin root, se borra con
# cada sesion): instala pspdev, Adrenaline, vitasdk, taihen y stubs en $HOME/tc y escribe $HOME/tc/env.sh.
# Uso: sh tools/mac/preparar_vm.sh ; despues: V=$HOME/mnt/Downloads/vitabridge sh tools/compilar.sh [version]
# - pspdev para ARM64 pide glibc 2.38: se usa la de conda-forge (sysroot 2.39) y un envoltorio por programa que lo lanza
#   con ese cargador. No usar patchelf: rompe cc1.
# - vitasdk: paquete "core" aarch64-linux-gnu de vitasdk/autobuilds (corre con la glibc del sistema).
# - GCC 15 de vitasdk usa C23 por defecto y taihen.h no compila: el envoltorio de arm-none-eabi-gcc agrega -std=gnu17.
set -e
T=$HOME/tc; mkdir -p $T; cd $T
REL=https://github.com/vitasdk/autobuilds/releases
[ -d pspdev ] || { curl -sSL https://github.com/pspdev/pspdev/releases/latest/download/pspdev-ubuntu-24.04-arm-arm64.tar.gz | tar xz; W=1; }
if [ ! -d sr ]; then
  curl -sSL -o sr.conda https://conda.anaconda.org/conda-forge/noarch/sysroot_linux-aarch64-2.39-h934ec21_6.conda
  mkdir sr; (cd sr && unzip -q ../sr.conda && for f in pkg-*.tar.zst; do zstd -dc $f | tar x; done); rm sr.conda
fi
S=$T/sr/aarch64-conda-linux-gnu/sysroot/lib64
if [ -n "$W" ]; then  # envoltorios: cada ELF de pspdev pasa a .real
  for f in $(find pspdev -type f -perm -u+x); do
    if [ "$(head -c4 $f | tail -c3)" = ELF ] && readelf -lW $f 2>/dev/null | grep -q "program interpreter"; then
      mv $f $f.real; printf '#!/bin/sh\nexec %s --library-path %s:/lib/aarch64-linux-gnu "$0.real" "$@"\n' $S/ld-linux-aarch64.so.1 $S > $f; chmod +x $f
    fi
  done
fi
[ -d Adrenaline ] || git clone -q --depth 1 https://github.com/TheOfficialFloW/Adrenaline
if [ ! -d vcore ]; then  # la instantanea mas nueva que tenga el paquete core para aarch64-linux-gnu
  TAG=$(curl -sSL $REL | grep -o 'releases/tag/sdk-snapshot-[0-9.]*' | head -1 | cut -d/ -f3)
  PKG=$(curl -sSL $REL/expanded_assets/$TAG | grep -o 'vitasdk-core-[^"]*-aarch64-linux-gnu.pkg.tar.xz' | head -1)
  mkdir vcore; curl -sSL $REL/download/$TAG/$PKG | tar xJ -C vcore
fi
[ -d taipkg ] || { mkdir taipkg; curl -sSL https://github.com/vitasdk/packages/releases/download/master/taihen.tar.xz | tar xJ -C taipkg; }
# Un .S por funcion de la base de datos de vita-headers: nativedraw/build.sh arma solo los stubs que usa
[ -d stubs ] || { mkdir stubs; vcore/bin/vita-libs-gen-2 -yml=vcore/share/vita-headers/db/360 -output=stubs >/dev/null; }
# nb/: nombres arm-none-eabi-* que espera nativedraw/build.sh, apuntando a las herramientas de vitasdk
mkdir -p nb; for t in ld nm as; do ln -sf $T/vcore/bin/arm-vita-eabi-$t nb/arm-none-eabi-$t; done
printf '#!/bin/sh\nexec %s -std=gnu17 "$@"\n' $T/vcore/bin/arm-vita-eabi-gcc > nb/arm-none-eabi-gcc; chmod +x nb/arm-none-eabi-gcc
cat > env.sh <<E2
export PSPDEV=$T/pspdev PATH=$T/nb:$T/vcore/bin:$T/pspdev/bin:\$PATH VH=$T/vcore/arm-vita-eabi STUBS=$T/stubs TAI=$T/taipkg ADRENALINE=$T/Adrenaline/cef
E2
. ./env.sh; psp-gcc --version | head -1; arm-none-eabi-gcc --version | head -1
