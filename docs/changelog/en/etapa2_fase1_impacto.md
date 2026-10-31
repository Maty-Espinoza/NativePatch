# Stage 2, phase 1: why shadow mode still costs GT (GePatch dev65 + gepresent v13.9, 2026-10-29)

**Commit:** GePatch **0.2-dev65** + gepresent **v13.9** (= dev65.3 + v13.9.5 with the final numbers).

Follows `etapa2_nucleos.md`. Plan agreed with the user: **phase 1** = measure everything needed regardless of cost;
**phase 2** = improve by iterations with that data. Tags: [measured], [deduced]. Log levels: `registros.md`.
Before this, gepresent v13.8.1/v13.8.2 was tried (check sublists without re-reading lines already checked, read in
chunks of 64–256 bytes): it read ~45% less of the PSP's RAM but the time did not go down and the DISTINTAS count went up
(2 → 13, all from sublists in the VRAM) → **discarded, no commit** (patch in `vitabridge/gepresent_v13.8.2.patch`).

## In simple terms
- **The whole cost to the game is the wait for the Vita** ("gate" at the sync). Without that wait GT runs at 59.9 fps the
  whole race; with it, ~58 fps and 16–20% slow frames. The rest (GePatch's notices, the mirror copy, the Vita reading
  the PSP's memory) is not noticeable.
- GT enqueues **one big list per frame** (50 000–80 000 instructions), complete, and asks for the sync ~14.3 ms later.
  The Vita starts almost immediately but needs ~13–16 ms (up to 20 in the heavy parts): ~1/3 of the frames arrive late,
  by a median of 1.5 ms. The PSP has ~20% free CPU (~3 ms) and absorbs small waits; the ones of 2–5 ms are the fps drops.
- **A frame's list is not touched until the game enqueues the next one** (0 changes in 11 436 checks; at the next sync
  90% had already changed). GT alternates two areas for its list (`08d004c0` / `08d204c0`).
- That is why the wait can be moved to "before enqueuing the next list": the Vita would have a whole frame (~16.7 ms).
  With the measured times, the Vita would be behind in 15% of the frames instead of 31%. It also has to be made
  faster: half of its time goes to checking and applying stored sublists, and it loses ~17% of its core.

## Runs (GT UCUS-98632, the same race and car; logs in `vitabridge/fase1/`)
| Run | Version | What | Folder |
|---|---|---|---|
| 1 | dev65.1 + v13.9.1 | everything normal | `gt1` |
| 2 | dev65.1 + v13.9.1 | `sin_compuerta` | `gt2_sin_compuerta` |
| 3 | dev65.1 + v13.9.2 | list lifetime (hashes at the flip) | `gt3_vida_lista` |
| 4 | dev65.2 + v13.9.3 | list lifetime according to GePatch (before enqueuing, at each sync) | `gt4_vida_gepatch` |
| 5 | dev65.2 + v13.9.4 | breakdown of the Vita's time | `gt5_desglose` |
Summary: `python3 tools/analisis_fase1.py gt1 gt2_sin_compuerta gt3_vida_lista gt4_vida_gepatch gt5_desglose`.

## Results [measured] (race frames: more than 30 000 commands)
| | 1 normal | 2 no gate | 3 | 4 | 5 |
|---|---|---|---|---|---|
| fps | 58.2 | **59.9** | 57.6 | 58.0 | 57.7 |
| frames > 17.5 ms | 15.8% | **0.2%** | 20.0% | 18.6% | 27.3% (full extreme log) |
| frame p99 | 24.5 ms | 16.9 ms | 24.4 | 24.0 | 23.8 |
| wait for the Vita (per frame) | 0.76 ms | 0 | 0.99 | 0.89 | 1.31 |
| wait for the GE inside the sync | 1.29 ms | 1.25 | 1.23 | 1.17 | 1.14 |
| PSP free CPU | 20% | 19% | 20% | 19% | 19% |
| mirror (flip CPU / pages) | 0.07 ms / 136 | 0.07 / 128 | | | |
| GePatch hooks | 6 µs | 6 µs | | | 8 µs |
| Vita: walking the list (median, late lists) | 15.4 ms | 15.0 | 15.9 | 15.7 | 15.4 |
| game: enqueue → sync (median) | 14.3 ms | 14.3 | 14.5 | 14.4 | 14.1 |

### The wait (`ge_patch_tarde.log`, run 1)
- Late lists: `listas_antes` = 0 (one list per frame), `ultimo_stall_hace` = `encolada_hace` (GT enqueues it complete,
  with no stall), the Vita starts 0.3 ms after it is enqueued (p90 1.6 ms), `trabajo` ≈ `recorrio` (it waits for nothing:
  it is all work). Lateness: median 1.5 ms, p90 5.2 ms; 12% of the waits reach the 5 ms cap.
- Without the gate (run 2) the Vita falls behind without limit in the heavy stretch (up to ~30 pending lists, lateness
  p90 190 ms): the wait is necessary while the Vita does not keep up. The few slow frames of that run are from the end of
  the lap, with the Vita far behind (a `sin_captura` run is missing to see whether it is GT alone).

### List lifetime (runs 3 and 4)
| Moment (after capturing list N) | Checked | Top level changed | Sublists changed |
|---|---|---|---|
| drawsync of frame N | 11 414 | 0 | 0 |
| **before enqueuing list N+1** | **11 436** | **0** | **0** |
| drawsync of frame N+1 | 2 902 | 2 603 (90%) | 178 (6%) |
| flip of frame N+1 (race) | 6 831 | 6 808 (99.7%) | 745 (11%) |
- From the drawsync to the next enqueue ~1.0–1.3 ms pass. With the Vita finishing X ms after the enqueue: more than
  14.3 ms (today) in 31% of the race frames; more than 16.7 ms in 15%; more than 20 ms in 4% [run 4].

### Where the Vita's time goes (run 5, median per frame)
| Commands per frame | Wall time | CPU time received | Check + apply sublists | of which, apply | Own reads | From the shared cache |
|---|---|---|---|---|---|---|
| 30–45 thousand | 12.4 ms | 10.9 | 5.6 | 1.4 (214 sublists) | 321 | 1613 |
| 45–60 thousand | 14.6 ms | 12.2 | 7.2 | 1.7 (235) | 406 | 1984 |
| 60–75 thousand | 17.1 ms | 13.9 | 8.1 | 1.9 (263) | 446 | 2314 |
| more than 75 thousand | 17.6 ms | 14.3 | 8.8 | 2.0 (270) | 467 | 2471 |
- Own read of 256 bytes: **3.0 µs** (0.75 µs per line). One line of the shared cache (filled by the readers):
  **1.13 µs** → reading directly is cheaper per line than the shared cache [measured; the cache is paid for in syncing
  between cores].
- Applying a stored sublist costs ~7 µs (mostly adding ~200 statistics counters and copying registers).
- The capturer gets ~83% of its core (core 1, shared with the audio threads): ~2.4 ms per frame lost.
- Reading the time costs 390 ns: the extreme log (thousands of reads per frame) is noticeable.

## Proposal for phase 2 (in order: gain / cost)
1. **Move the gate to "before enqueuing the next list"** (GePatch): +2.4 ms for the Vita, safe according to the list
   lifetime. For other games: check it on its own (check a few lines of the previous list before enqueuing, ~10 µs on
   the PSP) and go back to the sync if they change. **Done in dev66 + v13.10 (`etapa2_fase2_compuerta.md`): 3 times
   fewer waits in the GT race, almost none in the middle of the lap.**
2. Capturer: read directly in chunks of 256 bytes instead of taking lines from the shared cache (~0.8 ms); the readers
   only do look-ahead checks of sublists. **Tried in v13.11.1 and discarded: almost 3 times more waits in GT (the
   capturer uses ~2.3 of the 4 lines of each read and loses the parallelism of the readers).
   `etapa2_fase2_estadisticas.md`.**
3. Apply sublists without adding all the statistics (only what is needed; the rest according to the log level): ~1.5 ms.
   **Done in v13.11 (compact statistics): ~2 ms less Vita time per frame in the heaviest part of GT (19.1 → 17.0 ms),
   54.3 → 57.3 fps in that stretch. `etapa2_fase2_estadisticas.md`.**
4. Check sublists more cheaply or ahead of time (today ~5.5–6.8 ms): have the readers get to more sublists sooner.
   **Done in v13.12 (look-ahead, from the end): 57.3 → 58.2 fps in the heaviest part of GT; the capturer still
   compares for ~5.5 ms because reading the PSP's memory costs ~2 µs per line. Cheaper reading is left for the future.
   `etapa2_fase2_adelanto.md`.**
5. Capturer core / priority (it loses ~17%), taking care of the audio.
6. Pending item from phase 1: `sin_captura` run for the end of the lap; DISTINTAS counts of the sublists in the VRAM
   (the memory compares against the mirror from when it was stored, not the frame's) **→ fixed in v13.12**.

**Stage 2 closed on 2026-10-29** with GePatch 0.2-dev66 + gepresent v13.12 (the user's decision: 58 fps minimum in the
heaviest part of GT is enough). Points 5 and 6 stay in `TODO.md`.

## Versions
| Version | What | md5 |
|---|---|---|
| dev65.1 + v13.9.1 | EV_FRAME, free CPU, switches, frames/late | `bf2ea3d6…` / `c7af2246…` |
| v13.9.2 | list lifetime (hashes at the flip) | `e0b4cf7e…` |
| dev65.2 + v13.9.3 | EV_VIDA (GePatch checks the sample before enqueuing and at each sync) | `cfe999e2…` / `01461f8c…` |
| v13.9.4 | breakdown: memory, apply, reads, CPU time per list | `012268f3…` |
| dev65.3 + v13.9.5 | log levels (minimum / medium / extreme) | `990f6afb…` / `8af10774…` |
| **dev65 + v13.9 (commit)** | = dev65.3 + v13.9.5 with the final numbers; confirmed in GT (`gt6_confirmacion`, medium log) | `744ab734…` / `9d3439d3…` |
