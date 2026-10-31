# Stage 2: T4, T6 and data lifetime (GePatch dev63.1–dev63.3 + gepresent v13.5.1–v13.5.3, 2026-10-28)

Follows `etapa2_comparar_menos.md` (gepresent v13.4). Tags: [measured], [deduced].
**Commit:** GePatch **0.2-dev63** (= dev63.3 with the final number) + **gepresent v13.5** (= v13.5.3, same code).

## Question
The Vita is going to draw a frame after reading the list, and its GPU will read vertices, indices, textures and palettes
straight from the PSP's RAM. **Is the data still there when the Vita draws it?** Also: how much the PSP's GE works (T6)
and what the game's CPU signals to it (T4).

## In simple terms
- **GePatch** records in the mailbox, without touching the game: when the PSP's painter starts and stops working (with a
  thread that checks it every 0.25 ms, **off by default**: it cost GT 2–3 fps), how long the game waits for the painter
  at each sync, and every time the game's CPU flushes the cache (with address and size).
- **gepresent** takes a sample of the data the draws use and looks at it again 1 and 2 frames later.
- **Answer:** in GT and SH the vertices, indices and palettes **do not change during the following frame**; they are
  rewritten 2 frames later (the games use two sets of buffers). Textures almost never change, except a few in SH in the
  outdoor scene that change within the same frame.
- **Rule for stage 3:** the Vita has to finish drawing frame N before flip N+2 (that is, during the following frame). If
  it falls further behind: repeat the previous image or make the game wait. There is no need to copy vertices.

## How it works
### GePatch dev63 (main.c, "T6 and T4")
- Mailbox version 3 (`SB[1] = 3`). New events: `EV_GEFIN` (7: a = start of the busy span, b = samples in the stall,
  c = samples; the event time is the end, ±250 µs), `EV_SYNCEND` (8: after `sceGeDrawSync`/`ListSync`: a = µs
  waited, b = GE busy at the time of the call, c = 0 drawsync / 1 listsync), `EV_CPU` (9: data = type 0 WritebackRange, 1
  WritebackInvalidateRange, 2 InvalidateRange, 3 WritebackAll, 4 WritebackInvalidateAll, 5/6 DMA; a = address or
  destination, b = size or source, c = DMA size; up to 96 per frame) and `EV_CPUSUM` (10, at the flip: data = events not
  recorded, a = cache calls, b = KB, c = DMA calls | KB << 16).
- `geThread` (priority 0x11): with `SB[0x47] = 1` (set by gepresent if `ux0:data/gepresent/medir_ge` exists) it polls
  `sceGeGetReg(3/5/6)` every 250 µs while the GE has work, every 2 ms if not; without the request it sleeps 100 ms. The
  start of the span is the enqueue time if the GE was idle.
- `UtilsForUser` hooks (`sceSystemMemoryManager`): the 5 cache functions, found. `sceDmac`: not found under any module
  name tried (pending item in `TODO.md`).
- GePatch log every 600 frames: `stats T6 (por frame): GE ocupado ... | T4 (por frame): cache ... dma ...`.

### gepresent v13.5 (tools/gepresent/captura.c, "Data lifetime")
- `smpAdd`: 1 in every 64 draws (their vertices or indices), 1 in every 8 new textures of the frame, 1 in every 16
  palette loads; up to 8 vertices and 4 of everything else per frame; RAM only. Hash of the first 256 bytes when it is
  recorded. Sublists applied from the memory count with the vertices of their first draw (`Memo.sv_a`, relative to the
  input vertices if it does not set them inside).
- `ltFlip` (capturer, at each flip): hash again at the next 2 flips; counts the ones that changed and whether a known CPU
  write covered them (range or "flush the whole cache", `cwCovers`).
- **v13.5.1** did this in the mirror thread at 17/33 ms: almost all the samples arrived late and it took time away from
  the core 0 reader (GT bridge at 49–56 fps). v13.5.2 moved it to the capturer at the flip.
- New log lines (with GePatch dev63): `T6 GE del PSP (por frame): ocupado= ... | sync/f= esperas_al_GE= ... | T4
  CPU del juego (por frame): cache= dma= a_la_vram/seg= vaciar_todo/seg= sin_anotar/seg=` and `vida de los datos (por seg;
  cambiaron 1 / 2 frames después; aviso: rango / vaciar_todo): vertices= ... | revisadas a los X / Y ms`.
- Test outside the Vita: `VIDA=1 ./prueba 1 0 0` (samples that change before flip 1 or flip 2, with range and
  flush-all warnings; `vida:` lines with the expected values).

## Versions
| Version | What | md5 |
|---|---|---|
| dev63.1 + v13.5.1 | T4/T6 + data lifetime in the mirror thread | `4bec29af…` / `38d60292…` |
| dev63.2 + v13.5.2 | check at the capturer's flip; T6 fixes | `7251a065…` / `35fcb9bc…` |
| dev63.3 + v13.5.3 | T6 thread off by default (`medir_ge`) | `382d8216…` / `aac61a52…` |
| dev63 + v13.5 (commit) | = dev63.3 + v13.5.3 with the final numbers | `23589485…` / `5967a226…` |

## Results [measured]
Logs: `vitabridge/etapa2_dev63.1/gt1/`, `etapa2_dev63.2/gt1/`, `etapa2_dev63.2/sh1/`, `etapa2_dev63.3/gt1/`.

### Data lifetime
| | GT race (dev63.2) | GT race (dev63.3) | GT menus/loads | SH all | SH last 30 s (outdoors) |
|---|---|---|---|---|---|
| vertices: samples / change 1 frame / 2 frames | 39 737 / 1 / 508 (1.3%) | 47 497 / 2 / 884 (1.9%) | 6 798 / 0 / 909 (13%) | 28 729 / 1 / 260 (0.9%) | 4 860 / 1 / 61 (1.3%) |
| indices | — (GT does not use them) | — | — | 5 933 / 0 / 212 (3.6%) | 1 445 / 0 / 138 (9.5%) |
| textures | 23 889 / 1 / 1 | 28 751 / 0 / 0 | 5 648 / 3 / 4 | 24 976 / 97 (0.4%) / 114 | 4 890 / **72 (1.5%)** / 85 |
| palettes | 11 386 / 0 / 0 | 15 763 / 0 / 0 | 1 725 / 0 / 0 | 25 905 / 0 / 0 | 4 887 / 0 / 0 |
- The "1 frame later" check happens at the flip following the capture: GT at ~6–11 ms, SH ~7 ms; "2 frames" at ~24–28 ms
  (GT) and ~30–60 ms (SH).
- All the changes were covered by a "flush the whole cache" (GT does it ~3 times per frame, ~200 per second): **it is
  not useful for knowing which area changed**. Range warnings: almost none (GT ~3 per frame of a few KB; SH 3–7).
- **SH, outdoors:** ~1.5% of the textures sampled in RAM change within the same frame (the flashlight or some other
  effect that SH builds with copies? the user's hint: what SH "paints" may be the flashlight). For stage 3: re-read or
  copy those textures at every use (detect them as "dynamic").

### T6 (PSP GE)
- **GT** in a race: busy ~11.4 ms per frame, in one go (1 span per frame, 0% in the stall); the game calls 3 syncs per
  frame and waits for the GE ~0.9 times per frame, ~735 µs. GE margin: ~5 ms.
- **SH**: the GE never stops (spans of 100–240 ms that cross frames; ~20–35 ms of GE per frame, 0% in the stall in the
  game, 16–58% in the cutscenes). SH is limited by the GE (as without the plugin). Last 30 s: 1 sync per frame, waits
  for the GE ~0.4 times per frame, ~3.7 ms; 40.8 fps.
- "GE busy at the time of the call" is always 0 (pending item in `TODO.md`).

### GT fps (GePatch, every 10 s)
| | v13.4.2 (dev62) | dev63.1 + v13.5.1 | dev63.2 + v13.5.2 | dev63.3 + v13.5.3 |
|---|---|---|---|---|
| start of the race | 59.4 | 59.1 | 56.7 | 59.8 |
| last stretch (bridge) | 58.4 | 57.7 | 55.3 | 57.8 |
| frames per sec in the 6 heaviest seconds | 57–60 | 49–56 | 54–57 | 55–60 |
- dev63.2 (T6 thread on) cost GT 2–3 fps; dev63.3 (off) goes back to the v13.4 level. 0 DISTINTAS in all of them.

## What is missing / ideas
1. Data strategy for stage 3 (see In simple terms): draw frame N during frame N+1; if it falls behind, repeat or make the
   game wait at flip N+2; SH dynamic textures: re-read.
2. Point 6 of stage 2: stable mirror (two alternating copies) + up-to-date GT textures.
3. GXM recording on 2–3 fixed cores (before GT in stage 4): GT asks for ~16 500 draws per frame.
4. T2 (black box): at the end of the stage.
5. Small pending items in `TODO.md` (DMA, "GE busy at the time of the call").
