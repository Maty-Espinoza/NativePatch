#!/bin/sh
# Local test of performance: judges a run saved from the Vita (fps, MISMATCHES, HD frames) with tools/run_summary.py.
# Usage: sh tools/test_local.sh <run folder> [reference folder: the same game with ux0:data/nativedraw/off or another version]
[ -n "$1" ] || { echo "usage: sh tools/test_local.sh <run folder> [reference folder]"; exit 2; }
exec python3 "$(dirname "$0")/run_summary.py" "$1" --check ${2:+--ref "$2"}
