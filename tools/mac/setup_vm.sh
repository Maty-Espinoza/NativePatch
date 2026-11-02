#!/bin/sh
# Like tools/setup.sh, but for the Linux ARM64 VM Claude uses on the Mac (Ubuntu 22.04 without root, wiped with each
# session): installs pspdev, Adrenaline, vitasdk, taihen and stubs in $HOME/tc and writes $HOME/tc/env.sh.
# Usage: sh tools/mac/setup_vm.sh ; then: V=$HOME/mnt/Downloads/vitabridge sh tools/build.sh [version]
# - pspdev for ARM64 needs glibc 2.38: it uses conda-forge's (sysroot 2.39) and a wrapper per program that launches it
#   with that loader. Do not use patchelf: it breaks cc1.
# - vitasdk: "core" aarch64-linux-gnu package from vitasdk/autobuilds (runs with the system glibc).
# - vitasdk GCC 15 defaults to C23 and taihen.h does not compile: the arm-none-eabi-gcc wrapper adds -std=gnu17.
set -e
T=$HOME/tc; mkdir -p $T; cd $T
REL=https://github.com/vitasdk/autobuilds/releases
[ -d pspdev ] || { curl -sSL https://github.com/pspdev/pspdev/releases/latest/download/pspdev-ubuntu-24.04-arm-arm64.tar.gz | tar xz; W=1; }
if [ ! -d sr ]; then
  curl -sSL -o sr.conda https://conda.anaconda.org/conda-forge/noarch/sysroot_linux-aarch64-2.39-h934ec21_6.conda
  mkdir sr; (cd sr && unzip -q ../sr.conda && for f in pkg-*.tar.zst; do zstd -dc $f | tar x; done); rm sr.conda
fi
S=$T/sr/aarch64-conda-linux-gnu/sysroot/lib64
if [ -n "$W" ]; then  # wrappers: each pspdev ELF moves to .real
  for f in $(find pspdev -type f -perm -u+x); do
    if [ "$(head -c4 $f | tail -c3)" = ELF ] && readelf -lW $f 2>/dev/null | grep -q "program interpreter"; then
      mv $f $f.real; printf '#!/bin/sh\nexec %s --library-path %s:/lib/aarch64-linux-gnu "$0.real" "$@"\n' $S/ld-linux-aarch64.so.1 $S > $f; chmod +x $f
    fi
  done
fi
[ -d Adrenaline ] || git clone -q --depth 1 https://github.com/TheOfficialFloW/Adrenaline
if [ ! -d vcore ]; then  # the newest snapshot with the core package for aarch64-linux-gnu
  TAG=$(curl -sSL $REL | grep -o 'releases/tag/sdk-snapshot-[0-9.]*' | head -1 | cut -d/ -f3)
  PKG=$(curl -sSL $REL/expanded_assets/$TAG | grep -o 'vitasdk-core-[^"]*-aarch64-linux-gnu.pkg.tar.xz' | head -1)
  mkdir vcore; curl -sSL $REL/download/$TAG/$PKG | tar xJ -C vcore
fi
[ -d taipkg ] || { mkdir taipkg; curl -sSL https://github.com/vitasdk/packages/releases/download/master/taihen.tar.xz | tar xJ -C taipkg; }
# One .S per function of the vita-headers database: nativedraw/build.sh assembles only the stubs it uses
[ -d stubs ] || { mkdir stubs; vcore/bin/vita-libs-gen-2 -yml=vcore/share/vita-headers/db/360 -output=stubs >/dev/null; }
# nb/: arm-none-eabi-* names expected by nativedraw/build.sh, pointing to the vitasdk tools
mkdir -p nb; for t in ld nm as; do ln -sf $T/vcore/bin/arm-vita-eabi-$t nb/arm-none-eabi-$t; done
printf '#!/bin/sh\nexec %s -std=gnu17 "$@"\n' $T/vcore/bin/arm-vita-eabi-gcc > nb/arm-none-eabi-gcc; chmod +x nb/arm-none-eabi-gcc
cat > env.sh <<E2
export PSPDEV=$T/pspdev PATH=$T/nb:$T/vcore/bin:$T/pspdev/bin:\$PATH VH=$T/vcore/arm-vita-eabi STUBS=$T/stubs TAI=$T/taipkg ADRENALINE=$T/Adrenaline/cef
E2
. ./env.sh; psp-gcc --version | head -1; arm-none-eabi-gcc --version | head -1
