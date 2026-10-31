# Stage 2, phase 2 (point 4): DISTINTAS counts from the VRAM and look-ahead check (gepresent v13.12, 2026-10-29)

**Commit:** gepresent **v13.12** (= v13.12.2 with the final number; same code), with GePatch **0.2-dev66** unchanged.
**This closes stage 2** (the user's decision: 58 fps minimum in the heaviest part of GT is enough for now).

Follows `etapa2_fase2_estadisticas.md` (points 2 and 3). Point 4 of the proposal in `etapa2_fase1_impacto.md` ("check
sublists more cheaply or ahead of time") and the pending item about the DISTINTAS counts (`etapa2_espejo_doble.md`).
Tags: [measured], [deduced].

## In simple terms
- **DISTINTAS counts fixed.** The Vita reads the PSP's video memory through two copies (mirrors) that alternate every
  frame. Each stored sublist remembered its lines with the address of the mirror it was recorded in, and in the frames of
  the other mirror it compared against a copy one frame old: if the sublist had changed right there, it considered it
  equal and used the old one. Now it stores the video memory address and when comparing it translates it to the current
  frame's mirror. In GT: **0 DISTINTAS in ~100 000 checks** (before, 1–7 per run, all from the VRAM).
- **Look-ahead check.** The two helpers (readers) checked each sublist when they reached its call and arrived late for
  almost half of them. Now, as soon as a list starts, they receive the plan (the sublists that were applied in the
  previous list, in order) and check it **from the end**, until they meet the capturer.
- Result: somewhat better in the heaviest part of GT (57.3 → 58.2 fps, 17.4 → 16.8 ms of Vita per frame), but the
  capturer still compares for ~5.5 ms per frame. The limit is **reading the PSP's memory (~2 µs per line)**, not the
  order: the helpers manage to check ~130–150 sublists per frame out of ~270. Cheaper reading (cached view or DMA) is
  left in `TODO.md` for the future.

## How it works (tools/gepresent/captura.c)
### VRAM addresses (fix for the DISTINTAS counts)
- `recLine(b, rb)`: stores in `Memo.ra` the logical address (`pc`, 0x04xxxxxx for the VRAM) and copies the data from
  where it was read (`rb`, the mirror).
- `mline(b)`: to compare, `vtr(b)` if it is from the VRAM (mirror of `esp_u`, the frame's), `b` if it is RAM. If the page
  has no valid copy in that mirror, it cannot be compared: the capturer re-records it (`memo_fail`), the reader leaves it.
- `memoLines` (readers) and `memoTry` (capturer) use `mline`. `nearStall` and "already stored" still use the logical
  address.

### Look-ahead plan
- `memoTry`, when applying a sublist: `racha++` (times in a row applied with no changes; `recBegin` resets it to 0) and,
  if `racha >= pv_min` (8), records it in `pv[pv_c]` (up to 1024). `pv_pos` = how many it has recorded in this list
  (where the capturer is).
- `pubList` (new list for the readers): what was recorded in the previous list becomes the plan (`pv_plan`, `pv_pn`).
- `pvRun` (each reader, before walking): takes the next free one (`pv_i`) **from the end**; if it is one the capturer has
  already passed (`k < pv_pos`), it stops. `memoMark` compares and leaves the `vok` mark as before; if it was already
  marked for this list it does not read again. Afterwards the reader continues with its usual walk (`lecWalk`).
- Accepted risk: if the game changes a sublist after it was checked and before the capturer reaches it, the old one is
  used. GT enqueues the complete list and does not touch it until the next one (`etapa2_fase1_impacto.md`, list
  lifetime); the streak of 8 leaves only stable sublists; the 1/32 review would see it as DISTINTAS.
- Switch `ux0:data/gepresent/sin_adelanto` (no plan; turned off with "no" inside).
- Log, memory line: `| adelanto/f: plan= comprobadas= no_iguales= tarde=` (tarde: times a reader met the capturer;
  since v13.12.2 ~1 per frame).

### Test outside the Vita (`prueba_memoria.c`, qemu)
- `./prueba <modo> 3`: each frame writes its VRAM sublists only in that frame's mirror and sublist 10 changes in frame 4.
  With the v13.11 code, `./prueba 2 3` gives **BAD=1** in frame 4 (the GT error reproduced); with v13.12, BAD=0 and the
  change is detected in the same frame.
- `./prueba <modo> <vram> 3`: the readers only make the plan before the capturer (`PVMIN=n`, default 1): the capturer
  compares 0–3 lines per frame instead of ~45; ~20 of 21 sublists arrive checked.
- Modes 1/2 × vram 0/1/2/3 × readers 0/1/2/3: columns equal to no memory and 0 BAD.

## Versions
| Version | What | md5 |
|---|---|---|
| v13.12.1 | VRAM addresses + plan walked in order | `f877801e…` |
| v13.12.2 | plan walked from the end | `88888798…` |
| v13.12 (commit) | = v13.12.2 with the final version number | `04ccd6e6…` |

## Results [measured] (GT UCUS-98632, the same race and car, medium log)
Logs: `vitabridge/etapa2_v13.12.1/gt1`, `etapa2_v13.12.2/gt1` (against `etapa2_v13.11.2/gt1`). Medians per second.

| Commands per frame | | v13.11 | v13.12.1 | v13.12.2 |
|---|---|---|---|---|
| 45–60 thousand | fps / Vita per frame | 60.0 / 11.8 ms | 59.9 / 12.0 | 59.9 / 12.7 |
| 60–75 thousand | fps / Vita per frame | 60.0 / 15.1 ms | 59.2 / 15.7 | 59.9 / 14.9 |
| more than 75 thousand | fps / Vita per frame | 57.3 / 17.4 ms | 55.6 / 18.1 | **58.2 / 16.8** |
| more than 75 thousand | sublists applied / arrive checked | 272 / 130 | 273 / 131 | 272 / 135 |
| more than 75 thousand | lines the capturer compares / µs | 2 423 / 5 359 | 3 273 / 5 676 | 2 775 / 5 660 |
| more than 75 thousand | plan / checked ahead | – | 278 / 158 | 276 / 131 |
| whole run | DISTINTAS / checked | 3 / 51 121 | **0 / 50 324** | **0 / 49 371** |

- v13.12.1 (plan in order): the readers checked ~160 ahead of time, but they kept pace with the capturer (they are no
  faster than it) and compared the same sublists it was about to use: the capturer compared ~30% more lines and the
  heaviest part ended up at 55.6 fps.
- v13.12.2 (from the end): they meet ~1 time per frame; ~135–150 sublists arrive checked. The ones the capturer
  compares are at the start of the list and are longer (~20 lines each against ~17).
- Cost of reading: capturer ~1.9–2.0 µs per compared line; readers ~2.4 µs per line of their CPU time (~2 900 lines per
  frame in ~6.8 ms). [deduced] This is the limit: with ~270 sublists of ~10–20 lines per frame it is not enough.
- No hangs; the user did not report image or audio problems.

## What remains (in `TODO.md`)
- Read the PSP's memory more cheaply (cached view or DMA; KBENCH measured 1.7–4 times faster in large blocks): the only
  thing that would really lower the ~5.5 ms of comparing. Postponed by the user: 58 fps minimum is enough.
- The VRAM lines with no valid copy in the frame's mirror are now re-recorded instead of compared against the old copy:
  if in other games many sublists in the VRAM show up as "changed", look at this first.
