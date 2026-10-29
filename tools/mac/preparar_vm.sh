#!/bin/sh
# Compiladores de NativeLink (pspdev) y NativeDraw (vitasdk) en la VM Linux ARM64 que usa Claude en el Mac (2026-10-28).
# La VM es Ubuntu 22.04 sin root y se borra con cada sesion: este script lo rearma en ~2 minutos en $HOME/tc.
# - pspdev para ARM64 pide glibc 2.38: se usa la de conda-forge (sysroot 2.39) con un envoltorio por programa
#   (patchelf rompia cc1; el envoltorio lanza cada binario con el cargador nuevo).
# - vitasdk: paquete "core" aarch64-linux-gnu de vitasdk/autobuilds (corre con la glibc del sistema). GCC 15 usa C23 por
#   defecto: el envoltorio de arm-none-eabi-gcc agrega -std=gnu17 (nativedraw/build.sh espera arm-none-eabi-*).
# Uso: sh tools/mac/preparar_vm.sh ; despues: V=$HOME/mnt/Downloads/vitabridge sh tools/compilar.sh [version]
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
[ -d stubs ] || { mkdir stubs; vcore/bin/vita-libs-gen-2 -yml=vcore/share/vita-headers/db/360 -output=stubs >/dev/null; }
mkdir -p nb; for t in ld nm as; do ln -sf $T/vcore/bin/arm-vita-eabi-$t nb/arm-none-eabi-$t; done
printf '#!/bin/sh\nexec %s -std=gnu17 "$@"\n' $T/vcore/bin/arm-vita-eabi-gcc > nb/arm-none-eabi-gcc; chmod +x nb/arm-none-eabi-gcc
cat > env.sh <<E2
export PSPDEV=$T/pspdev PATH=$T/nb:$T/vcore/bin:$T/pspdev/bin:\$PATH VH=$T/vcore/arm-vita-eabi STUBS=$T/stubs TAI=$T/taipkg ADRENALINE=$T/Adrenaline/cef
E2
. ./env.sh; psp-gcc --version | head -1; arm-none-eabi-gcc --version | head -1
