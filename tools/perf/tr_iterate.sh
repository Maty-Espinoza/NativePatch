#!/bin/sh
# One step of the translator's performance loop (docs/agent/en/perf-loop.md, "Translator loop"), on the real frames of
# tools/perf/data. Correctness first: on every frame the GPU path must send the same triangles as the CPU path (tr_gv), and with
# --check <commit> the CPU path must give the same draws as that commit (tr_check). Then the cost of the God of War combat frames
# (gow_combat, gow_enemies: est_ms is their sum) with the GPU path on, as on the Vita: real ARM instructions per frame under qemu, the per-draw GXM calls and the uniform floats
# written, and from them an estimate of Vita ms per HD frame. Compares with tools/perf/tr_baseline.json and prints KEEP (cheaper,
# all correct), NO CHANGE or REVERT. --keep after a KEEP saves the new reference; --fast skips the ARM count (~3 min).
# Usage: sh tools/perf/tr_iterate.sh [--keep] [--fast] [--check <commit>]
set -e
KEEP=0; FAST=0; CHK=
while [ $# -gt 0 ]; do case $1 in --keep) KEEP=1;; --fast) FAST=1;; --check) CHK=$2; shift;; esac; shift; done
T=${T:-$HOME/tc}; . $T/env.sh
R=$(cd "$(dirname "$0")/../.." && pwd); B=$R/build/perf; P=$R/tools/perf; mkdir -p $B/inc
REF=${REF:-$P/tr_baseline.json}
[ -e $B/inc/taihen.h ] || ln -s $T/taipkg/include/taihen.h $B/inc/
for d in $VH/include/psp2* $VH/include/vitasdk*; do [ -e $B/inc/$(basename $d) ] || ln -s $d $B/inc/; done
Q=$(command -v qemu-arm-static || command -v qemu-arm)
arm-linux-gnueabihf-gcc -std=gnu17 -static -O2 -mthumb -mfpu=neon -mfloat-abi=hard -w -I$B/inc -I$R/nativedraw -o $B/tr_it $P/translate_bench.c $P/host/vita_stubs.c -lm
bad=
echo "== same triangles on the GPU path and the CPU path (tr_gv)"
for g in $P/data/*.bin.gz; do
  s=$(basename $g .bin.gz); gzip -dc $g > $B/$s.bin
  TR_TRI=$B/$s.cpu $Q $B/tr_it $B/$s.bin 1 >/dev/null; TR_GV=1 TR_TRI=$B/$s.gpu $Q $B/tr_it $B/$s.bin 1 > $B/$s.line
  r=$(python3 $P/tr_diff.py $B/$s.cpu $B/$s.gpu | head -1); echo "$s: $r | $(cat $B/$s.line)"
  case $r in ok*) ;; *) bad="$bad tr_gv:$s";; esac
done
grep -q 'result=ok' $B/gow_combat.line || bad="$bad gow_combat_not_whole"
[ "$(sed 's/.*gxm_draws=\([0-9]*\).*/\1/' $B/rr_crowd.line)" -lt 400 ] || bad="$bad rr_crowd_gxm_draws"
if [ -n "$CHK" ]; then
  echo "== CPU path against $CHK (tr_check)"
  sh $P/tr_check.sh $CHK > $B/trc.out 2>&1 || true; cat $B/trc.out
  grep -v '^ok\|:$' $B/trc.out | grep -q . && bad="$bad tr_check"
fi
G="gow_combat gow_enemies"; args=
for g in $G; do
  L=$B/$g.line; arm=0
  if [ $FAST = 0 ]; then
    echo "== ARM instructions per frame ($g, GPU path on)"
    cnt() { TR_GV=1 $Q -one-insn-per-tb -d nochain,exec -D /dev/stdout $B/tr_it $B/$g.bin $1 2>/dev/null | grep -c '^Trace'; }
    a=$(cnt 1); b=$(cnt 2); arm=$(((b - a) / 1000))
  fi
  args="$args $g $arm $(for k in gxm_ops uniform_floats gxm_draws cpu_vertices; do sed "s/.*$k=\([0-9]*\).*/\1/" $L; done)"
done
python3 - "$REF" $B/tr_last.json $FAST $args <<'EOF'
import json, sys
ref, out, fast, a = sys.argv[1], sys.argv[2], sys.argv[3] == "1", sys.argv[4:]
# Unit costs from the Vita (perf-loop.md, "Translator loop"): ns per ARM instruction, us per GXM call
C_INS, C_OP = 7.5, 5.4
try: base = json.load(open(ref))
except Exception: base = {}
cur, tot = {}, 0
for i in range(0, len(a), 6):
    g, arm, ops, uf, dr, cv = a[i], *map(int, a[i + 1:i + 6])
    if fast: arm = base.get(g + ".arm_k", 0)
    est = round(arm * C_INS / 1000 + ops * C_OP / 1000, 1); tot += est
    cur.update({g + ".arm_k": arm, g + ".gxm_ops": ops, g + ".uniform_floats": uf, g + ".gxm_draws": dr, g + ".cpu_vertices": cv, g + ".est_ms": est})
cur["est_ms"] = round(tot, 1)
json.dump(cur, open(out, "w"))
print("%-28s %10s %10s" % ("", "reference", "now"))
for k in cur: print("%-28s %10s %10s" % (k, base.get(k, "-"), cur[k]))
if fast: print("(--fast: arm_k is the reference's, est_ms only sees GXM)")
EOF
echo "== verdict"
if [ -n "$bad" ]; then echo "REVERT:$bad"; exit 1; fi
r=0; python3 - "$REF" $B/tr_last.json $KEEP <<'EOF' || r=$?
import json, os, shutil, sys
ref, last, keep = sys.argv[1], sys.argv[2], sys.argv[3] == "1"
a = json.load(open(last))
if not os.path.exists(ref):
    print("NO CHANGE: no reference yet" + (", created " + ref if keep else ""))
    if keep: shutil.copy(last, ref)
    sys.exit(2)
d = (a["est_ms"] - json.load(open(ref))["est_ms"]) * 100 / json.load(open(ref))["est_ms"]
if d > 1: print("REVERT: est_ms %+.1f %%" % d); sys.exit(1)
if d > -1: print("NO CHANGE: est_ms %+.1f %%" % d); sys.exit(2)
print("KEEP: est_ms %+.1f %%" % d)
if keep: shutil.copy(last, ref); print("reference updated: " + ref)
EOF
exit $r
