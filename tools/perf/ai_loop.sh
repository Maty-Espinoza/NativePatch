#!/bin/sh
# Unattended performance loop: runs Claude Code N times, each time with the procedure of docs/agent/en/perf-loop.md,
# on a branch of its own. Every iteration tries one change to the capturer or the record, judges it with iterate.sh and
# commits it only when it is a KEEP. Costs tokens on every iteration: run it only when the user asked for it.
# Usage: sh tools/perf/ai_loop.sh [iterations, default 5] [branch, default perf-loop]
# Needs the claude CLI (Claude Code) logged in, tools/setup.sh done and the apt packages of bench.sh.
set -e
R=$(cd "$(dirname "$0")/../.." && pwd); N=${1:-5}; BR=${2:-perf-loop}
cd $R
git rev-parse --verify $BR >/dev/null 2>&1 || git branch $BR
git checkout -q $BR
i=1
while [ $i -le $N ]; do
  echo "== iteration $i of $N ($(date))"
  claude -p "$(cat docs/agent/en/perf-loop.md)

This is iteration $i of $N of the unattended loop (tools/perf/ai_loop.sh). Do one iteration as described in section 3 and stop. The repo is at $R on branch $BR; the toolchain is at \$HOME/tc (T=\$HOME/tc). Do not push." \
    --allowedTools "Bash,Read,Edit,Write,Grep,Glob" --max-turns 60 || true
  git status --short | grep -q . && { echo "working tree left dirty: reverting"; git checkout -q -- .; git clean -fdq nativedraw tools; }
  i=$((i + 1))
done
echo "== done: $(git log --oneline -n $N)"
