# Stage 2: sublist check by the readers (gepresent v13.4.1–v13.4.2, 2026-10-28)

Follows `etapa2_espejo_vram.md` (GePatch dev62 + gepresent v13.3). Marks: [measured], [deduced].
**Commit:** GePatch 0.2-dev62 (no changes) + **gepresent v13.4** (= v13.4.2 with the final version number; same code).

## In simple terms
- The Vita saves the sublists that GT repeats on every frame and, before reusing them, checks that they did not change. On the
  bridge that check was ~9 of the ~18 ms per frame, and what was expensive was not comparing: it was **reading** the PSP's memory
  (slow for the Vita: ~6.5 µs every 256 bytes).
- The two helpers (readers, cores 0 and 2, with the leftover time) were walking instruction by instruction exactly those
  sublists, which the capturer was already skipping.
- **Now:** on reaching a saved sublist, the helper compares its lines itself and leaves a "checked in this
  list" mark. The capturer, if it finds the mark, uses it without reading anything; if not, it compares as before (never worse than v13.3).
  Each helper takes the next free sublist (not a fixed half) and skips the ones the capturer already passed.
- **Result:** GT's bridge at 57–60 fps (v13.3: ~52), the start of the race at 59.4 (56–58). SH the same as before.

## How it works (tools/gepresent/captura.c)
- `Memo` has at the end `ver` (goes up in `recBegin` before touching anything; `recBegin` does not clear those two fields) and `vok`
  (`vokOf(ord, ver)` = list number << 16 | version). `recEnd` puts a barrier before `state = 1`.
- `memoVerify` (reader): on a CALL of the upper level it looks for the saved versions (up to 8 with the same key,
  `state = 1`, current `pgen`) and compares all their lines with the pool (`rdc` + `lineEq`; the lines also end up in the
  shared cache). Near the stall it does not check (the capturer sees it). If at the end `ver` or `pool_gen` changed (the
  capturer re-recorded it or emptied the pool), it is discarded. Equal → `vok`; different → `vok = 0`.
- `memoTry` (capturer): after `depsOk` and `nearStall`, if `vok == vokOf(l->ord, ver)` it applies without comparing
  (`memo_pre`). The 1-out-of-32 check stays the same (walks and compares the state).
- Split (`claim[2048]`, v13.4.2): the n-th CALL of the list's upper level is taken by the first reader that arrives
  (`claimTake`, CAS with list number << 2 | who); the capturer marks the ones it reached (`claimSet`) and a reader that
  arrives later counts it as `tarde` and skips it. After CALL 2048, one yes and one no as before.
- The reader was turned into functions (`Lec`, `lecStart`, `lecWalk`) for the test outside the Vita.
- Log, memory line: `aplicadas= (comprobadas_por_lectores=)` and `| lectores/f: lineas= comprobadas= no_iguales=
  tarde=`.
- Test: `prueba_memoria.c <modo> <vram> <lectores>`; readers 1 = go before the capturer (same columns as without
  memory, the capturer compares 0 lines, the reader detects the sublist that changes in frame 3); 2 = go after
  (they skip everything, 0 lines read). Tested with vram 0/1/2 and modes 1/2: equal, 0 different.

## Versions
| Version | What | md5 |
|---|---|---|
| v13.4.1 | check by the readers (split one yes, one no) | `ccc116f0…` |
| v13.4.2 | split according to pace + skip what the capturer already passed | `0ebc04d7…` |
| v13.4 (commit) | = v13.4.2 with the final version number (identical code) | `dc016491…` |

## Results [measured]
Logs: `vitabridge/etapa2_v13.4.1/gt1/`, `etapa2_v13.4.1/sh1/`, `etapa2_v13.4.2/gt1/`.

### GT (UCUS-98632), one lap with the bridge (GePatch fps every 10 s)
| | v13.3 | v13.4.1 | v13.4.2 |
|---|---|---|---|
| start of the race | 57.2 | 59.0 | 59.4 |
| last stretch (bridge) | 53.6 | 57.3 | 58.4 |
| frames per sec in the heaviest seconds (~82,000 commands) | 51–52 | 55–59 | 57–60 |
| capture, ms per frame on the bridge | ~18.3 | ~16.6 | ~15.5 |
| comparar_us per frame / lines the capturer compares | 8,400–9,200 / ~4,050 | 6,500–7,200 / ~3,000 | 5,700–6,500 / ~2,750 |
| sublists applied / checked by the readers (per frame) | ~280 / – | ~277 / ~85 | ~277 / ~110 |
| checked / DISTINTAS | – / 0 | 51,102 / 0 | 52,990 / 0 |
- The readers receive ~140 (core 0) and ~280 (core 2) ms of processor per sec; with v13.4.1 core 0's reader arrived
  late at its fixed half. With v13.4.2 ~110 of ~277 sublists arrive checked; in ~107 per frame both readers arrive
  after the capturer (`tarde`, ~215 per frame counting both). **They do not have enough time** to get ahead.
- The capturer's own reads: ~860 → ~570 per frame.

### SH (ULUS-10450), with v13.4.1
- 26–60 fps depending on the scene (as before); it barely uses the sublist memory (applied ~0); 0 different; ~5.6 ms of
  capture per frame on average.

## What is missing / ideas
1. **Check at the start of each frame** (without waiting for the game to reach the CALL): the readers would check as soon as
   the list starts the sublists used in the previous one. It would give more time, but if the game changes a sublist after
   it is checked, that frame would use the old one (the 1/32 check would see it). Only for sublists stable for many frames.
   Decided to leave it for later (user, 2026-10-28).
2. Read less from the PSP RAM: what is expensive is the uncached read (~6.5 µs every 256 bytes); any way of not reading
   (hashes computed elsewhere, a cached view) yields more than comparing faster.
3. SH: mark TRANSFER targets (still pending from `etapa2_espejo_vram.md`).
4. T2 (black box), T4, T6, GXM recording on several threads → stage 3 (drawing).
