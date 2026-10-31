# Stage 2, phase 2 (1): the wait for the Vita moves to "before enqueuing the next list" (GePatch dev66 + gepresent v13.10, 2026-10-29)

**Commit:** GePatch **0.2-dev66** + gepresent **v13.10** (= dev66.3 + v13.10.3 with the final numbers).

Follows `etapa2_fase1_impacto.md` (point 1 of the proposal for phase 2). Tags: [measured], [deduced].

## In simple terms
- Before, the game waited for the Vita at the end of each image (at the sync). Now it waits right before sending the next
  list: the Vita gains the time between the end of one image and the start of the next (~2.4 ms in GT).
- So as not to break games that rewrite their list before that, the Vita leaves some "hashes" of each list and GePatch
  checks them before sending the next one. If they changed, it goes back to waiting at the sync.
- Result in GT (medium log, the same race): **3 times fewer waits in the race** (~2 150 → 690) and in the middle of the
  lap almost none (~1 000 → ~45; 59.7–59.9 fps). The start and the end of the lap still wait: there the Vita needs more
  than a whole image (up to 20–25 ms), and that is fixed by making it faster (points 2–5).
- Silent Hill: the check saw no changes (~3 000 checks) and used the new wait the whole time, with no waits.

## How it works
- **GePatch** (`main.c`, "dev66 (fase 2)"): in `sceGeDrawSync`/`sceGeListSync` it no longer waits; it records the last
  list that went through a sync (`gateSync`). In `sceGeListEnQueue`/`EnQueueHead`, `gateBeforeEnq` waits (up to 5 ms, as
  before, with `sbGate`) for the Vita to have captured that list, and then does the self-check.
- **Self-check:** gepresent leaves the hash sample (16 lines of 64 B of the top level and the first line of up to 16
  sublists, `lifeTake`) of the largest list of each frame, now at all log levels, with its sequence number in `SB[0x53]`.
  GePatch redoes the hashes with the cache (what the game's CPU sees) only if the sample is from a list enqueued after
  the previous sync (`sync_lo < orden <= sync_ord`).
- **Modes** (`SB[0x54]`, in GePatch's log "stats compuerta" and in gepresent's "impacto en el PSP" line):
  0 = at the sync, testing (when the game opens and after a change); 1 = before enqueuing (after 60 clean checks in a
  row); 2 = at the sync until the game closes (10 changes).
- Switch `ux0:data/gepresent/compuerta_sync` (`SB[0x4A] & 8`): always at the sync, as before (the checks continue). It is
  turned off with `no` inside.
- `EV_ENQ`/`EV_HEAD` carry in their data the wait before enqueuing (us | 1); gepresent adds it up (`eg_n`, `eg_us`) and
  at extreme level records it in `ge_patch_tarde.log` with type `e`.

## Test versions [measured]
| Version | What | Result | Folder |
|---|---|---|---|
| dev66.1 + v13.10.1 | gate before enqueuing; one change leaves it at the sync until the game closes | menus fine; on the loading screen before the race list 4613 "changed" → the whole race at the sync | `etapa2_dev66.1/gt1` |
| dev66.2 + v13.10.2 | one change goes back to "testing" (60 clean), 10 changes = until closing | 10 "changes" in the race, all in frames with a wait → at the sync from s 91 | `etapa2_dev66.2/gt1` |
| **dev66.3 + v13.10.3** | gepresent leaves the sample before marking the list as captured; GePatch only accepts the sample of a list after the previous sync | **0 changes in the whole run**; Silent Hill the same | `etapa2_dev66.3/gt1`, `sh1` |

**False alarm in dev66.1/dev66.2 [deduced]:** gepresent marked the list as captured (`SB[3]`) and only afterwards left
its sample. When the gate released the game right at that moment, GePatch checked the sample of the previous list, whose
area GT is already rewriting (it alternates two areas for its lists): it looked like a change. That is why it only
appeared in frames with a wait (never in the menus). The first "false alarm" of dev66.1 (loading screen) is of the same
kind: with dev66.3 the load went through with no changes.

## Results in GT (dev66.3, medium log; comparison with dev66.1, which did the race with the wait at the sync)
| Stretch (s of GePatch's log) | fps dev66.1 | fps dev66.3 | waits per 10 s dev66.1 → dev66.3 | wait per frame (µs) |
|---|---|---|---|---|
| 42–48 (start) | 56.2 | 56.5 | 143 → 63 | 451 → 367 |
| 48–54 | 56.3 | 59.2 | 525 → 229 | 1 861 → 624 |
| 54–60 | 59.7 | 59.7 | 245 → 51 | 545 → 166 |
| 60–102 (middle) | 59.3–59.9 | **59.8–59.9** | 1 031 → 47 | 116–441 → 3–74 |
| 102–108 (end) | 56.0 | 54.7 | 281 → 300 | 1 283 → 1 495 |
- The end of the lap (54.7 fps) is the known stretch where the Vita takes 20–25 ms per frame (`TODO.md`): moving the wait
  is not enough; with a single run it is not known whether the drop against 56.0 is real or variation.
- Sublist memory: 1 DISTINTAS in 50 536 checked (the usual: sublists in the VRAM).
- Cost of the self-check: not noticeable in the hooks (3 µs per frame in GePatch's log).

## Next (rest of the phase 2 proposal)
2. Capturer: read directly in chunks of 256 bytes instead of the shared cache (~0.8 ms).
3. Apply sublists without adding all the statistics (~1.5 ms).
4. Check sublists more cheaply or ahead of time.
5. Capturer core / priority (taking care of the audio).
6. DISTINTAS counts of the sublists in the VRAM; `sin_captura` run for the end of the lap.

## Versions
| Version | GePatch / gepresent md5 |
|---|---|
| dev66.1 + v13.10.1 | `4e756743…` / `f80f245a…` |
| dev66.2 + v13.10.2 | `3ac67b21…` / `c79cdae4…` |
| dev66.3 + v13.10.3 | `d0854495…` / `d4504573…` |
| **dev66 + v13.10 (commit)** | `16755a0c…` / `5f08b9d1…` |
