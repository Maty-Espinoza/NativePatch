#!/bin/bash
# Compila PPSSPP con el modelo de Adrenaline (adrenaline.patch) para este Mac.
#   bash ~/Documents/gepatch-lab/compilar.sh
# La primera vez baja PPSSPP (~1 GB con sus submodulos) y compila (~15-20 min).
set -e
LAB="$(cd "$(dirname "$0")" && pwd)"
REV=1adf3c5  # el commit de PPSSPP sobre el que esta hecho el parche
if ! xcode-select -p >/dev/null 2>&1; then
  echo ">> Faltan las Command Line Tools de Xcode (compilador). Se abre una ventana: elige"
  echo "   \"Instalar\", espera a que termine y vuelve a correr este script."
  xcode-select --install || true
  exit 1
fi
command -v brew >/dev/null || { echo ">> Falta Homebrew: https://brew.sh"; exit 1; }
# Este PPSSPP usa SDL3 + SDL3_ttf (texto); fontconfig y fribidi son opcionales pero evitan avisos
for p in cmake pkgconf sdl3 sdl3_ttf fontconfig fribidi; do brew list "$p" >/dev/null 2>&1 || brew install "$p"; done
cd "$LAB"
[ -d ppsspp/.git ] || git clone https://github.com/hrydgard/ppsspp.git
cd ppsspp
git fetch -q origin || true
git checkout -q -f "$REV"
git clean -fdq -e build -e build-headless   # quita lo del parche anterior
git submodule update --init --recursive
git apply ../adrenaline.patch
rm -f build/CMakeCache.txt   # por si quedo cacheado un intento sin SDL3
./b.sh --release
echo
echo ">> Listo: $LAB/ppsspp/build/PPSSPPSDL.app"
echo "   Para jugar: bash $LAB/jugar.sh \"nombre del iso\""
