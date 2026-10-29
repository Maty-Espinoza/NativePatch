#!/bin/bash
# Ambiente de NativePatch en Windows + WSL (lo llama instalar.cmd). Se puede correr de nuevo: salta lo que ya esta.
# Todo lo que necesita PATH queda en Ubuntu (~/tc); en Windows solo se crean carpetas:
#   Documents\GitHub\{NativePatch,Adrenaline}   Downloads\{vitabridge.py,vitabridge.cmd,vitabridge\}
set -e
REPO_URL=https://github.com/Maty-Espinoza/NativePatch.git
WIN=${WINHOME:-$(wslpath "$(cmd.exe /c 'echo %USERPROFILE%' 2>/dev/null | tr -d '\r')")}  # C:\Users\<usuario>
GH=$WIN/Documents/GitHub; DL=$WIN/Downloads
[ -d "$WIN" ] || { echo "No encuentro la carpeta de usuario de Windows ($WIN)"; exit 1; }

echo "== 1/5 paquetes de Ubuntu (pide la clave de Ubuntu)"
sudo apt-get update -qq && sudo apt-get install -y -qq git make python3 curl xz-utils bzip2 ca-certificates >/dev/null

echo "== 2/5 git"
if [ -z "$(git config --global user.name)" ]; then
  read -rp "   nombre para los commits [Maty-Espinoza]: " n; git config --global user.name "${n:-Maty-Espinoza}"
  read -rp "   correo para los commits [matias.espinozaq@33sur.cl]: " m; git config --global user.email "${m:-matias.espinozaq@33sur.cl}"
fi
GCM="/mnt/c/Program Files/Git/mingw64/bin/git-credential-manager.exe"   # el inicio de sesion de GitHub de Windows
[ -f "$GCM" ] && git config --global credential.helper "$GCM" || echo "   (sin Git for Windows: el primer push pedira iniciar sesion)"

echo "== 3/5 repo en $GH"
mkdir -p "$GH" && cd "$GH"
[ -d NativePatch ] || git clone -q -b dev $REPO_URL NativePatch
git -C NativePatch config core.filemode false
git -C NativePatch config user.name  "$(git config --global user.name)"    # vale tambien para el git de Windows
git -C NativePatch config user.email "$(git config --global user.email)"

echo "== 4/5 compiladores del PSP y de la Vita en ~/tc (~3 min la primera vez)"
sh NativePatch/tools/preparar.sh

echo "== 5/5 relevo con la Vita y primera compilacion"
mkdir -p "$DL/vitabridge"
cp NativePatch/tools/vitabridge.py NativePatch/tools/windows/vitabridge.cmd "$DL/"
V="$DL/vitabridge" sh NativePatch/tools/compilar.sh
echo
echo "Listo. Compilar: Documents\\GitHub\\NativePatch\\tools\\windows\\compilar.cmd   Relevo: Descargas\\vitabridge.cmd"
