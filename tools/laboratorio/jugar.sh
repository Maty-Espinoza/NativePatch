#!/bin/bash
# Abre un juego en el laboratorio: PPSSPP como Adrenaline, con GePatch desde memstick/seplugins.
#   bash ~/Documents/gepatch-lab/jugar.sh "Silent Hill.iso"   (sin nombre: lista los ISO)
# La ventana muestra lo mismo que la Vita (la imagen nativa de GePatch). El log de GePatch
# queda en memstick/ge_patch.log. Variables: PPSSPP_ADRENALINE=estricto para parar en lo que
# en la Vita cuelga Adrenaline.
LAB="$(cd "$(dirname "$0")" && pwd)"
APP="$LAB/ppsspp/build/PPSSPPSDL.app/Contents/MacOS/PPSSPPSDL"
[ -x "$APP" ] || { echo ">> Primero: bash $LAB/compilar.sh"; exit 1; }
if [ -z "$1" ]; then echo "ISO en $LAB/iso:"; ls "$LAB/iso"; exit 0; fi
ISO="$1"; [ -f "$ISO" ] || ISO="$LAB/iso/$1"
mkdir -p "$LAB/memstick/PSP"
# --debugger: API de depuracion por WebSocket en localhost:45678 (monitor y control de Claude: lab.py)
PPSSPP_ADRENALINE=${PPSSPP_ADRENALINE:-1} exec "$APP" --memstick="$LAB/memstick" --graphics=software --debugger=45678 "$ISO"
