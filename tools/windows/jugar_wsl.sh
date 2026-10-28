#!/bin/bash
# Lo llama jugar.cmd. Igual que tools/laboratorio/jugar.sh del Mac, con PPSSPP compilado en WSL.
LAB="$(cd "$(dirname "$0")" && pwd)"
APP=~/gepatch-lab/ppsspp/build/PPSSPPSDL
[ -x "$APP" ] || { echo ">> Primero: instalar.cmd laboratorio (o bash $LAB/laboratorio_wsl.sh)"; exit 1; }
if [ -z "$1" ]; then echo "ISO en $LAB/iso:"; ls "$LAB/iso"; exit 0; fi
ISO="$1"; [ -f "$ISO" ] || ISO="$LAB/iso/$1"
mkdir -p "$LAB/memstick/PSP"
# --debugger: API de depuracion en localhost:45678 (tambien se ve desde Windows)
PPSSPP_ADRENALINE=${PPSSPP_ADRENALINE:-1} exec "$APP" --memstick="$LAB/memstick" --graphics=software --debugger=45678 "$ISO"
