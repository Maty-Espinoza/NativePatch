#!/bin/bash
# Ambiente de NativePatch en Windows + WSL (lo llama instalar.cmd). Se puede correr de nuevo: salta lo que ya esta.
#   bash instalar.sh [laboratorio]
# Todo lo que necesita PATH queda en Ubuntu; en Windows solo se crean carpetas:
#   Documents\GitHub\{NativePatch,Adrenaline,gepatch-lab}   Downloads\{vitabridge.py,vitabridge.cmd,vitabridge\}
set -e
REPO_URL=https://github.com/Maty-Espinoza/NativePatch.git
WIN=${WINHOME:-$(wslpath "$(cmd.exe /c 'echo %USERPROFILE%' 2>/dev/null | tr -d '\r')")}  # C:\Users\<usuario>
GH=$WIN/Documents/GitHub; LAB=$GH/gepatch-lab; DL=$WIN/Downloads
[ -d "$WIN" ] || { echo "No encuentro la carpeta de usuario de Windows ($WIN)"; exit 1; }

echo "== 1/6 paquetes de Ubuntu (pide la clave de Ubuntu)"
sudo apt-get update -qq && sudo apt-get install -y -qq git make python3 curl xz-utils ca-certificates >/dev/null

echo "== 2/6 git"
if [ -z "$(git config --global user.name)" ]; then
  read -rp "   nombre para los commits [Maty-Espinoza]: " n; git config --global user.name "${n:-Maty-Espinoza}"
  read -rp "   correo para los commits [matias.espinozaq@33sur.cl]: " m; git config --global user.email "${m:-matias.espinozaq@33sur.cl}"
fi
GCM="/mnt/c/Program Files/Git/mingw64/bin/git-credential-manager.exe"   # el inicio de sesion de GitHub de Windows
[ -f "$GCM" ] && git config --global credential.helper "$GCM" || echo "   (sin Git for Windows: el primer push pedira iniciar sesion)"

echo "== 3/6 compilador del PSP (pspdev) en ~/pspdev"
[ -x ~/pspdev/bin/psp-gcc ] || curl -L# https://github.com/pspdev/pspdev/releases/latest/download/pspdev-ubuntu-latest-x86_64.tar.gz | tar xz -C ~
grep -q 'pspdev/bin' ~/.bashrc || echo 'export PSPDEV=~/pspdev; export PATH="$PATH:$PSPDEV/bin"   # NativePatch' >> ~/.bashrc
export PATH="$PATH:$HOME/pspdev/bin"

echo "== 4/6 repos en $GH"
mkdir -p "$GH" && cd "$GH"
[ -d NativePatch ] || git clone -q -b dev $REPO_URL NativePatch
[ -d Adrenaline ]  || git clone -q --depth 1 https://github.com/TheOfficialFloW/Adrenaline.git
git -C NativePatch config core.filemode false
git -C NativePatch config user.name  "$(git config --global user.name)"    # vale tambien para el git de Windows
git -C NativePatch config user.email "$(git config --global user.email)"
git -C NativePatch remote get-url upstream >/dev/null 2>&1 || git -C NativePatch remote add upstream https://github.com/TheOfficialFloW/GePatch.git
T=NativePatch/tools

echo "== 5/6 scripts, laboratorio y relevo"
mkdir -p "$LAB/memstick/seplugins" "$LAB/iso" "$DL/vitabridge"
cp $T/windows/{compilar.cmd,jugar.cmd,jugar_wsl.sh,laboratorio_wsl.sh} "$LAB/"
[ -f "$LAB/memstick/seplugins/game.txt" ] || printf 'ms0:/seplugins/ge_patch.prx 1\n' > "$LAB/memstick/seplugins/game.txt"
cp $T/vitabridge.py $T/windows/vitabridge.cmd "$DL/"

echo "== 6/6 compilar GePatch"
make -s -C NativePatch && for d in "$DL" "$LAB/memstick/seplugins"; do cp NativePatch/ge_patch.prx "$d/"; done
echo "   ge_patch.prx: $(md5sum < NativePatch/ge_patch.prx | cut -c1-32)  (en Descargas y en el laboratorio)"

if [ "$1" = laboratorio ]; then bash "$LAB/laboratorio_wsl.sh"; fi
echo
echo "Listo. Compilar: gepatch-lab\\compilar.cmd   Relevo: Descargas\\vitabridge.cmd"
[ "$1" = laboratorio ] || echo "Laboratorio (opcional, 20-40 min): instalar.cmd laboratorio"
