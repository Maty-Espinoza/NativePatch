# Ridge Racer at 60 fps with the capture: interpret line by line and capturer on any core (gepresent v13.13, 2026-10-29)

**Commit:** gepresent **v13.13** (= v13.13.2 with the final number and the free core by default), GePatch **0.2-dev66**
unchanged. After the close of stage 2 (`etapa2_fase2_adelanto.md`), testing other games. Tags: [measured], [deduced].

## In simple terms
- **Ridge Racer dropped to ~30 fps** at the same place on every lap: a curve before an area full of crowd. There each
  frame has ~20 000 instructions and ~3 700 draws (double the normal; the crowd is many small pieces). The PSP had
  ~55–60% free processor: it was not the game, it was the wait for the Vita.
- Ridge Racer **does not repeat sublists**, so the sublist memory does not help (it skips ~1% of the instructions; in GT
  ~85%): the Vita interprets everything, ~1 µs per instruction. In addition, the capturer shared its core with the game's
  sound and received only ~65% of it.
- **Two changes:** 1) interpret line by line of 64 bytes (16 instructions): the checks that used to be done at each
  instruction are now done once per line (~1 070 → ~800 ns per instruction); 2) the capturer can run on any of the 3
  cores, not pinned to the sound's (it receives ~100%).
- **Result:** Ridge Racer at 60 fps the whole race (1 second at 55); GT also improved in the heaviest part (58.2 → 60.2).
  No audio cuts or image problems (user's tests), 0 DISTINTAS.

## Why it dropped (`vitabridge/etapa2_v13.12/rr1`) [measured]
| Commands per frame | Seconds | fps | Draws | Vita per frame |
|---|---|---|---|---|
| 3–9 thousand | 101 | 59.9 | ~850 | 8.1 ms |
| 9–12 thousand | 111 | 59.5 | ~1 250 | 10.9 ms |
| 12–15 thousand | 56 | 57.7 | ~1 700 | 13.7 ms |
| 15–17 thousand | 18 | 39.8 | ~2 300 | 16.8 ms |
| more than 17 thousand | 23 | 30.8 | ~3 300 | 19.9 ms |
- Sublist memory: ~3 applied per frame, 0–1% of the commands not walked.
- RR rewrites 16 lines of its list before enqueuing the next one: the dev66 gate goes back to the sync (correct: if not,
  the Vita could read the list halfway through changing) and the Vita has less margin. The wait reaches the 5 ms cap.
- RR syncs each image with the screen: if the frame does not arrive in time it drops straight from 60 to 30.
- Capturer: ~65% of core 1 (RR's audio uses ~30% of that core; in GT ~19%).
- In the generated code, `walkList` was embedded inside `walk`: the compiler ran out of registers and at each command
  recalculated the list address (`&q[...]`) and stored and read values from the stack.

## How it works (tools/gepresent/captura.c)
### v13.13.1: line by line
- `walkList` is a separate function (`noinline`).
- The address translation (`vtr`) is done once per 64-byte line (`lpc`/`lra`).
- Per line: the stall (limits how many commands remain, `k`), the read (`rdc`), the `LIST_MAX` cap, `recLine` and `lo`.
  Within the line it continues with `op = R0.buf[++i]` while: commands remain (`k`), the walk is sequential
  (`l->pc == pc + 4`), the level does not change (`l->sp`) and the `R0` buffer is the same (`memoTry` replaces it when
  comparing).
- `draw`: the vertex size is remembered while the format does not change (`vs_vt`), and the data lifetime sampling
  (`smpAdd`) is only called at extreme log level.
- qemu test (`prueba_memoria.c`): results identical to v13.12 in modes 0/1/2 × vram 0/1/2/3 × readers 0/1/2/3 and with
  NIVEL=0/1 and VIDA=1.

### v13.13.2 / v13.13: the capturer on any core
- The capturer's `sceKernelCreateThread` with affinity 0x70000 (cores 0, 1 and 2) instead of 0x20000 (only core 1), same
  priority (0x61). Switch `ux0:data/gepresent/nucleo_captura`: "0", "1" or "2" pins it to that core; with no file or
  "libre", any. In v13.13.2 the default was still 1; v13.13 makes free the default (tested in RR and GT).
- The readers stay pinned (cores 0 and 2, priority 0xB0): they receive less time (GT: ~265–430 → ~110–130 ms per second
  between the two; ~110–140 → ~50–70 sublists per frame arrive checked), but the capturer with a whole core more than
  makes up for it.

## Versions
| Version | What | md5 |
|---|---|---|
| v13.13.1 | line by line + `noinline` + remembered vertex size | `186d4414…` |
| v13.13.2 | + `nucleo_captura` switch (tested with "libre") | `3be2a248…` |
| v13.13 (commit) | = v13.13.2 with the free core by default | `8c6adb31…` |

## Results [measured]
### Ridge Racer (ULJS-00001), the same race (`etapa2_v13.12/rr1`, `etapa2_v13.13.1/rr1`, `etapa2_v13.13.2/rr1`)
| Commands per frame | | v13.12 | v13.13.1 | v13.13.2 (free) |
|---|---|---|---|---|
| 3–9 thousand | fps / Vita per frame | 59.9 / 8.1 ms | 60.0 / 6.7 | 59.9 / 4.6 |
| 12–15 thousand | fps / Vita per frame | 57.7 / 13.7 ms | 59.6 / 10.7 | 60.0 / 7.6 |
| 15–17 thousand | fps / Vita per frame | 39.8 / 16.8 ms | 58.3 / 12.8 | 60.0 / 9.2 |
| more than 17 thousand | fps / Vita per frame | 30.8 / 19.9 ms | 49.9 / 15.1 | **60.0 / 10.4** |
| all | ns per command (wall time) | ~1 070 | ~800 | ~580 |
| all | capturer processor | ~65% | ~65% | ~100% |
| race | seconds under 58 fps | 56 | 17 | **1** (55 fps) |
- In the crowd stretch with v13.13.1: the PSP with ~55–60% free CPU, ~120 textures (as in the rest of the track); only the
  draws grow. There is no load spike from the game: it is an even load while the crowd is on screen.
- 0 DISTINTAS in the three runs.

### Gran Turismo (UCUS-98632), the same race (`etapa2_v13.12.2/gt1` against `etapa2_v13.13.2/gt1`, with "libre")
| Commands per frame | fps / Vita per frame v13.12 | v13.13.2 |
|---|---|---|
| 30–45 thousand | 60.0 / 10.1 ms | 60.0 / 7.8 |
| 45–60 thousand | 59.9 / 12.8 ms | 60.0 / 10.0 |
| 60–75 thousand | 59.9 / 14.9 ms | 59.9 / 12.0 |
| more than 75 thousand | 58.2 / 16.7 ms | **60.2 / 13.4** |
- Race seconds under 58 fps: 6 → 0. Wait for the Vita in the heaviest part: 0.91 → 0.08 ms per frame. 0 DISTINTAS.
- Comparing sublists goes up a little (5.6 → 6.8 ms per frame in the heaviest part) because the readers get to fewer, but
  the total goes down.

### LocoRoco (NPEG-00024) with v13.12 (`etapa2_v13.12/locoroco1`)
- 59.9 fps almost always; ~4–8 ms of Vita per frame; 0 DISTINTAS in 65 330. A sublist changes before the next one is
  enqueued: the gate stays at the sync (correct).

## What remains (in `TODO.md`)
- Why RR rewrites 16 lines of its list before enqueuing the next one: if it is something the Vita has already read, the
  gate before enqueuing could be used and give it more margin.
- When the Vita draws (stage 3), RR is going to need the margin it gained here: ~10 ms of capture in the heaviest part.
- Test LocoRoco and Silent Hill with v13.13 (the free core was not tested on them).
