# Performance loop (capturer and frame record)

How an AI conversation improves the performance of NativeDraw's capturer (`nativedraw/capture.c`, with `record.c`)
iteratively on the PC, before anything reaches the Vita. The bench is `tools/perf/` (`../../dev/en/performance-plan.md`
says what to attack and in which order). `tools/perf/ai_loop.sh` runs this document unattended with Claude Code; a normal
conversation follows it by hand. The rules of `good-practices.md` and `CLAUDE.md` apply unchanged.

## 1. What the bench measures and what it does not
`sh tools/perf/bench.sh` builds `tools/perf/capture_bench.c` (the real `capture.c` with the Vita functions replaced, on ARM
under qemu) and runs synthetic scenes shaped like the games measured on the Vita: `rr_crowd` (3400 3D strip draws,
23.8k vertices in RAM, VRAM textures), `rr_race` (900 of the same), `sprites2d` (300 rects with swizzled RAM textures and
palettes, texture cache) and `sublists` (sublist memory and the readers, no record). Each scene gives, per frame:
- **`mem_us`**: an estimate of Vita microseconds from counters that do not depend on the PC (256-byte reads of PSP RAM,
  shared-cache hits, bytes the CPU copies or compares from PSP RAM, DMA calls and bytes, clock reads) and the unit costs
  measured on the Vita (`capture_bench.c`, `C_*`). Deterministic: two runs of the same code give the same number. **This
  is the judge for anything that moves memory.**
- **`ir_k/f`**: thousands of instructions per frame counted by Callgrind on an x86 build (`tools/perf/instr.sh`; apt packages
  `valgrind`, `libc6-dev-i386` and `lib32gcc-13-dev`, not `gcc-multilib`, which removes the ARM compiler). Exact: the same code
  gives the same number on any PC, so 1 % counts. **This is the judge for CPU work**, and `instr.sh --top` shows where it goes,
  function by function; `callgrind_annotate --auto=yes` on `build/perf/instr/cg.*`, line by line. The `tr_*` scenes are the
  translator on real frames (`tools/perf/translate_bench.c`, `tools/perf/data/`: dumps of the Vita).
- **`tools/perf/tr_check.sh <commit>`**: same draws, state and vertices as a commit, on the real frames, in the x86 and the ARM
  builds. Run it on every change to the translator: a faster translator must draw the same. `sh tools/perf/arm_count.sh [--top]`:
  real ARM instructions per frame of the translator under qemu (slow, ~1 min). **`tr_*` in `ir_k/f` does not see NEON** (on x86
  each NEON operation is C): for NEON work the judge is `arm_count.sh`.
- **`tools/perf/tr_gv.sh`**: on the real frames, the GPU path (`nd_v3`, `TR_GV=1` marks the draws as the record does) must send the same triangles to the surface as the CPU path. Intermediate images are left out: the CPU clips there and the GPU does it itself. `TR_GV=1 sh tools/perf/arm_count.sh` counts that path; it does not see the cost of reading uncached memory.
- **`want`**: pages asked of the mirror after the last frame. The `images` scene moves its images every frame: a leak shows here.
- **`qemu_min_us`**: CPU time of the fastest frame under qemu (interpretation, record bookkeeping, hashes, sorting). It is not
  Vita time and varies ~5 % between runs: it only counts for changes over 10 % (`RUNS=10` tightens it).
- `complete`: frames the record published complete. It must not drop.
What it cannot see: the real cost of uncached reads beyond the model, the readers running on other cores, the GE, the
mirror timing, the translator and the GPU. A KEEP here is a candidate for the Vita, not a result.

## 2. Reference and verdict
- `tools/perf/baseline.json` (in the repo) is the reference: the numbers of the last confirmed state. `bench.sh` compares
  against it and `compare.py` prints the table; `sh tools/perf/iterate.sh` adds the capturer test (correctness) and prints
  **KEEP** (faster somewhere, nowhere slower, test ok), **NO CHANGE** or **REVERT**.
- `qemu_min_us` depends on the machine and its load (the same code moved 26 % between sessions): at the start of a
  conversation save a local reference (`RUNS=10 sh tools/perf/bench.sh`, copy `build/perf/last.json`, use it with `REF=`), and
  to compare two versions run them alternately. `mem_us` does not change.
- `sh tools/perf/ab.sh <commit>` compares the working tree with a commit, instructions only, with today's scenes.
- `sh tools/perf/iterate.sh --keep` (or `bench.sh --save`) rewrites the reference. Only after a KEEP that is committed.
- A regression is any rise of `mem_us` or `want`, a rise of `ir_k/f` over 1 %, or fewer complete frames. `qemu_min_us` only
  counts where there is no `ir_k/f`. The numbers mean nothing against the Vita: compare them only with earlier runs of the bench.

## 3. One iteration
1. Start: `T=$HOME/tc sh tools/setup.sh` if `$HOME/tc` is missing; apt packages `qemu-user-static` and
   `gcc-arm-linux-gnueabihf`. `git status` clean. Read the first open item of `../../dev/en/performance-plan.md` whose
   verification is "bench" (items that need the Vita are not for this loop).
2. Run `sh tools/perf/bench.sh` once to see where that scene spends (`own256`, `ucp_kb`, `dma`, `clock`).
3. Write down the hypothesis in one sentence (which counter or which CPU work goes down and why). No hypothesis, no change.
4. Make **one** change, as small as the hypothesis allows, in `capture.c` or `record.c`. Comments follow
   `good-practices.md` §1; no measurements, versions or history in the code. Do not touch `capture_bench.c` to make a
   number look better: a scene changes only when the Vita logs say the shape is wrong, in its own commit.
5. `sh tools/perf/iterate.sh`.
   - **KEEP**: `git diff` (every line comes from the hypothesis), then commit on the current branch with the message
     `perf: <what changed> (bench: <scene> mem -N%, qemu -M%)` and `sh tools/perf/iterate.sh --keep` to move the reference;
     commit `tools/perf/baseline.json` with it. Add the result to the item in `performance-plan.md` ("bench: ...").
   - **NO CHANGE** or **REVERT**: `git checkout -- nativedraw/` and note in `performance-plan.md`, under the item, one
     line with what was tried and why it did not help (so the next iteration does not repeat it).
6. Stop after one change. The unattended loop starts the next iteration fresh; a conversation decides with the user.

## 4. What is off limits
- Anything the bench cannot judge (gate, mirror, readers' priorities, translator, GPU): those items say "Vita" in the plan
  and wait for a test on the console.
- Changing a test's expectation, a scene, or the unit costs `C_*` to pass.
- Pushing, merging into `dev` or sending to the Vita: the user does that after reading the commits of the branch
  (`perf-loop`), and then the batch gets a test version as usual (`../../dev/en/versions.md`).
- Spending the loop on cleanups: only changes with a measured effect.

## Crowd frame
`tools/perf/data/rr_crowd.bin.gz` is a real crowd frame of Ridge Racer (3688 draws, it ends in `vertex_out` near its last draws). `tr_gv.sh` prints `gxm_draws`, the `sceGxmDraw` calls of that frame: the CPU cost of GXM and part of the GPU cost scale with it, and `test_ci.sh` fails over 400.
