#!/bin/sh
# Local test of performance: judges a run saved from the Vita (fps, DISTINTAS, HD frames) with tools/resumen_corrida.py.
# Usage: sh tools/test_local.sh <run folder> [reference folder: the same game with ux0:data/nativedraw/off or another version]
[ -n "$1" ] || { echo "usage: sh tools/test_local.sh <run folder> [reference folder]"; exit 2; }
exec python3 "$(dirname "$0")/resumen_corrida.py" "$1" --check ${2:+--ref "$2"}
