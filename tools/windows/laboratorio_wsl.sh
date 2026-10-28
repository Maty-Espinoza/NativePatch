#!/bin/bash
# Laboratorio PPSSPP-Adrenaline en WSL (equivalente a tools/laboratorio/compilar.sh del Mac).
#   instalar.cmd laboratorio, o: bash laboratorio_wsl.sh (desde Documents\GitHub\gepatch-lab)
# La compilación queda en WSL (~/gepatch-lab, rápido); memstick, iso y scripts en Documents\GitHub\gepatch-lab.
# La primera vez baja ~1 GB y compila 20-40 min. Volver a correrlo recompila solo lo que cambió.
set -e
SRC=~/gepatch-lab                                        # PPSSPP y SDL3 (disco de WSL)
REPO="$(cd "$(dirname "$0")/.." && pwd)/NativePatch"      # de aqui sale adrenaline.patch
REV=1adf3c5                                              # commit de PPSSPP del parche

echo "== 1/4 paquetes (compilador, cmake y lo que SDL3 necesita para ventana y sonido)"
sudo apt-get update -qq
sudo apt-get install -y -qq build-essential cmake git pkg-config python3 \
  libgl1-mesa-dev libglu1-mesa-dev libegl-dev libgles-dev libx11-dev libxext-dev libxrandr-dev libxcursor-dev \
  libxi-dev libxss-dev libxtst-dev libxkbcommon-dev libwayland-dev wayland-protocols libdecor-0-dev \
  libpulse-dev libasound2-dev libudev-dev libdbus-1-dev \
  libfreetype-dev libharfbuzz-dev libfontconfig-dev libfribidi-dev >/dev/null
mkdir -p $SRC && cd $SRC

echo "== 2/4 SDL3 y SDL3_ttf (Ubuntu 24.04 no los trae: se compilan una vez en /usr/local)"
build_lib() {  # build_lib <carpeta> <repo> <tag>
  [ -d $1 ] || git clone -q --depth 1 -b $3 https://github.com/libsdl-org/$2.git $1
  cmake -S $1 -B $1/build -DCMAKE_BUILD_TYPE=Release -Wno-dev >/dev/null
  cmake --build $1/build -j$(nproc) >/dev/null && sudo cmake --install $1/build >/dev/null
}
pkg-config --exists sdl3     || build_lib sdl3     SDL      release-3.2.24
pkg-config --exists sdl3-ttf || build_lib sdl3_ttf SDL_ttf  release-3.2.2
sudo ldconfig

echo "== 3/4 PPSSPP en $REV + adrenaline.patch"
[ -d ppsspp/.git ] || git clone -q --filter=blob:none https://github.com/hrydgard/ppsspp.git
cd ppsspp
git fetch -q origin || true
git checkout -q -f $REV && git clean -fdq -e build
git submodule update --init --recursive -q
git apply "$REPO/tools/ppsspp-vita/adrenaline.patch"

echo "== 4/4 compilar PPSSPPSDL (con ventana) y PPSSPPHeadless"
cmake -B build -DCMAKE_BUILD_TYPE=Release -DUSE_FFMPEG=ON -DHEADLESS=ON -Wno-dev >/dev/null
cmake --build build -j$(nproc) --target PPSSPPSDL PPSSPPHeadless
echo "listo: $SRC/ppsspp/build/PPSSPPSDL y PPSSPPHeadless. Para jugar: jugar.cmd en Documents\\GitHub\\gepatch-lab"
