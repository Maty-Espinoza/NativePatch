# Stage 2: VRAM mirror (GePatch dev62 + gepresent v13.1–v13.3, 2026-10-28)

Follows `etapa2_memoria_sublistas.md` (sublist memory, v12.13). Marks: [measured], [deduced].
**Commit:** GePatch **0.2-dev62** (tests dev62.1) + **gepresent v13.3**.

## In simple terms
- The Vita cannot read the PSP's video memory (the VRAM), and Gran Turismo keeps part of its drawing
  instructions, textures and palettes there. Up to v12.13 the Vita skipped those instructions: its reading of GT was incomplete.
- **Mirror:** the Vita asks the PSP for the 4 KB chunks of the VRAM that it needs. On every frame change the PSP's painter
  (the GE) copies those chunks to a normal memory area (0x0A200000) that the Vita can read. The instructions are copied
  on every frame; the textures and palettes the first time and afterwards little by little, in turns.
- **Result:** the Vita now reads GT completely (100–140 sublists per frame from the mirror, none skipped). GT runs at
  59.4–59.9 fps almost the whole lap; the start of the race at 56–58 and the bridge at ~52 (before ~56, but reading less).
  SH runs the same as before. Audio and image fine (user). The image is still the native one: the Vita does not draw yet.

## How it works
### GePatch dev62 (main.c, "VRAM mirror")
- Shadow-mode mailbox version 2 (`SB[1] = 2`). New words: `+0x80` page map of **lists** and `+0xC0` of
  **data** (16 words = 512 pages of 4 KB each, written by the Vita); `+0x100` number of the last finished
  copy (written by the GE); `+0x110` copies enqueued, `+0x114` pages copied, `+0x118` times the previous copy
  was still in the GE (totals, PSP); `+0x800` number of the last copy of each page (512 words, PSP).
- `espejoFlip()` in the flip hook (with k1 = 0): list pages all of them; data pages the new ones (up to 64 per frame) and
  8 already copied per frame, rotating. Contiguous spans with `sceGuCopyImage` 8888 of 1024 px width (1 row = 1 page),
  up to 100 spans; at the end a 16-byte copy puts the copy number in `+0x100`. Two alternating lists at
  0x0A100000 (not at RENDER_LIST + 0x8000: TX_RING is there); if the one from two frames ago is still in the GE
  (`sceGeListSync(qid, 1) > 0`) that frame does not copy. With no requests it enqueues nothing.
- Log every 600 frames: `stats espejo: paginas pedidas listas= datos= | copias= paginas_copiadas= (KB/frame) tramos_max=
  ocupado= terminada=`.

### gepresent v13.x (tools/gepresent/captura.c, "VRAM mirror")
- `vtr(a)`: readable address for `a`; the VRAM (0x04xxxxxx, `& 0x1FFFFF`) goes to the mirror if its page is valid. The
  capturer and the readers read everything through `vtr`. A page is valid when its copy finished (`+0x800[p]` ≤
  `+0x100`); since v13.3 the capturer itself checks it (`espCheck`).
- Requests (`want`): CALL/JUMP to a page with no copy → destination page and the next one (list); the sublist is skipped
  that frame. Texture in the VRAM (bytes of level 0 according to format, buffer width and height) and palette (`0xC4`: 32 B blocks)
  → data. The pages of a render target (the draw's fb and its z with z test, height = bottom edge of the scissor) are
  marked `rt` (`markRt`) and are not requested as data (the Vita will have them in HD).
- A sublist that continues into a page with no copy: it goes back to the upper level (`UNWIND`, counter `cortadas`) instead of
  losing the list.
- Mirror thread (`gepresent_espejo`, core 0, priority 0xB0, every 5 ms): statistics only. It computes a hash of
  each page after each copy and counts the ones that changed (lists/data). It rewrites the request maps in the mailbox
  every 100 ms in case GePatch reset it.
- **v13.2:** sublist memory of 2048 slots and a 2 MB pool (before 512 and 512 KB; 5976 KB in total). If there is no
  memory, it tries with half.
- **v13.3:** `espCheck` (before, only the mirror thread marked pages valid and it sometimes got no time: ~6 sublists
  per frame were still being skipped); line comparison with NEON (`lineEq`); `comparar_us` in the log (time spent comparing
  the lines of the saved sublists, sampled 1 out of every 8 × 8).
- New log line: `espejo: paginas pedidas listas= datos= validas= en_destinos= | por seg: copias= paginas_copiadas=
  (KB/f) ocupado= revisadas= nuevas= cambiaron listas= datos= hilo_us= invalidadas= | sublistas_vram/f recorridas= cortadas=
  texturas_en_destino/f=`.
- Test outside the Vita: `prueba_memoria.c <modo> 1` puts the sublists in the VRAM (contents in the mirror); from
  frame 1 it gives the same counts as in RAM with and without memory and checking all of them. `<modo> 2`: a page with no copy.

## Versions
| Version | What | md5 |
|---|---|---|
| dev62.1 + v13.1 | mirror | `2faf13fa…` / `515e8ef3…` |
| v13.2 | sublist memory ×4 | `81605a67…` |
| v13.3 | capturer validates pages, NEON, `comparar_us` | `282b3a74…` |
| dev62 (commit) | = dev62.1 with the final version number | `d44ba997…` |

## Results [measured]
Logs: `vitabridge/etapa2_v13.1/gt1/`, `etapa2_v13.2/gt1/`, `etapa2_v13.3/gt1/`, `etapa2_v13.3/sh1/` (the latter also
includes the GT session: Adrenaline was not closed between games).

### GT (UCUS-98632)
| | v12.13 (no mirror) | v13.1 | v13.2 | v13.3 |
|---|---|---|---|---|
| GePatch fps, almost the whole lap | 59–60 | 33–60 | 59.5–59.9 | 59.4–59.9 |
| start of the race / bridge | 59 / ~56 | 38 / 33 | 52–53 / 52 | 56–58 / ~52 |
| commands per frame on the bridge | ~67,000 | ~82,000 | 77,000–82,000 | 75,000–83,000 |
| memory pool emptied | ~40 in the lap | 4–5 per sec | ~0 | ~0 |
- Requested: 43–81 list pages (up to 324 KB per frame) and ~145 data pages. The GE copies 200–350 KB per frame; the game
  waits for the GE in drawsync 0.1–0.3 ms per sync (as before): **the copy is not what lowers the fps**.
- v13.1 dropped because of the sublist memory: with the VRAM ones GT calls ~250 sublists per frame and the table (512) and the
  pool (512 KB) were emptied 4–5 times per second (everything was recorded again).
- v13.3: 100–140 sublists per frame read from the mirror, 0 cut, 0 skipped; memory check 0 DISTINTAS.
- **What is still expensive:** comparing the lines of the saved sublists costs ~2 µs per 64-byte line: ~9 ms of
  the ~18 ms per frame on the bridge (4000 lines). The rest is walking what is not saved.
- **Changes in the mirror:** list pages that change between copies: 0–78 per sec; data pages (textures):
  100–230 per sec during a race in v13.3 (in v13.1 0 were measured: the thread barely got any time). For stage 3 the sweep
  of 8 data pages per frame (~0.3 s per full lap) is not enough [deduced].

### SH (ULUS-10450)
- fps as with v12.10 (26–56 depending on the scene; SH does not reach 60 even without the plugin); the capture almost never makes anything wait.
- **Detail:** it requests 253 data pages (1 MB) and all of them change on every copy (they are targets that are not detected: SH copies
  to the VRAM with TRANSFER, ~160 copies per frame). It costs 32 KB of copy per frame (~0.07 ms of GE): it does not affect, but
  it is surplus.

## What is missing / ideas
1. **Compare less:** the readers (cores 0 and 2) could do a look-ahead check of the lines of the saved
   sublists, or compare directly from the shared cache without copying; it is ~half of the bridge's time.
2. SH: mark as a target what TRANSFER writes into the VRAM and stop requesting data pages that change on every
   copy.
3. GT textures that change during a race: faster sweep or request per texture when it is used (stage 3).
4. PSP tests (`tests/`): they have not compiled since dev61 (`asm("sync")` in `sbPost` with the PC's compiler).
5. What is pending from stage 2: T2 (black box), T4, T6, GXM recording on several threads → stage 3 (drawing).
