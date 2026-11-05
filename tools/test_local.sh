#!/bin/sh
# Local test of performance: judges a run saved from the Vita (fps, MISMATCHES, HD frames) with tools/run_summary.py and, with a
# reference, compares it zone by zone with tools/run_compare.py.
# Usage: sh tools/test_local.sh <run folder> [reference: the same game with ux0:data/nativedraw/off or another version, a folder or a saved .json]
# Reference lap: tests/perf/rr_hold_0.0.21.3.json (Ridge Racer with hold_game).
[ -n "$1" ] || { echo "usage: sh tools/test_local.sh <run folder> [reference]"; exit 2; }
D=$(dirname "$0")
case "$2" in
  *.json) ;;
  *) python3 "$D/run_summary.py" "$1" --check ${2:+--ref "$2"} || r=1 ;;
esac
[ -z "$2" ] || python3 "$D/run_compare.py" "$2" "$1" --check || r=1
exit ${r:-0}
