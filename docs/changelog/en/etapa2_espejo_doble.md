# Stage 2: VRAM double mirror (GePatch dev64.1–dev64.5 + gepresent v13.6.1–v13.6.5, 2026-10-28)

Follows `etapa2_t4_t6_vida_datos.md` (item 6 of stage 2: "stable mirror + GT textures up to date"). Marks:
[measured], [deduced]. **Commit:** GePatch **0.2-dev64** (= dev64.5 with the final number) + **gepresent v13.6** (= v13.6.5).

## In simple terms
- The Vita cannot see the PSP's video memory (VRAM): the PSP leaves it a copy ("mirror") in normal memory on every
  frame change. There was **a single copy**, which the PSP overwrote while the Vita might be reading it, and GT's
  textures were updated 8 chunks per frame (they ended up to ~0.3 s behind).
- Now there are **two copies that take turns**. Each display list is read with the latest finished copy and the Vita tells
  the PSP which one it is using: the PSP never writes over that one.
- Every copy carries **all the instructions** (sublists) requested and **up to 256 KB of textures** (if there are more, in
  turns).
- What the game copies into the VRAM with the painter itself (block copies, TRANSFER) is no longer requested from the mirror: the Vita
  will repeat it on its own (and in high resolution). Almost all of GT's textures that change during a race and the image that SH
  copies for its effects are of that type.
- **Result:** GT at the same fps as before (bridge 58.5 versus 57.8); SH no longer copies anything (before 1 MB that changed on
  every copy) and the outdoor scene reached 43.4 fps (before 40.8). The image is still the native one: the Vita does not draw yet.

## How it works
### GePatch dev64 (main.c, "VRAM mirror")
- Mailbox version 4 (`SB[1] = 4`). Mirrors: even copies in `MIRROR` (0x0A200000), odd ones in `MIRROR2` (0x0AC00000, unused
  in every GePatch mode). Number of the last copy of each page: `+0x800` (MIRROR) and `+0x21000` (MIRROR2,
  right after the event ring), 512 words each.
- `espejoFlip()`: all the list pages + up to `ESP_DATOS` (64) data pages, starting where it left off the last time
  they did not all fit; contiguous spans with `sceGuCopyImage` (up to 200, 8 KB lists alternating at 0x0A100000).
- It does not copy if the copy from two frames ago is still in the GE (`SB[0x46]`) **or if the Vita is using that mirror**: `SB[0x48]`
  (copy in use, written by the Vita) with the same parity, only if it is a real copy (≤ the last one enqueued) and the Vita
  responds (`sbAlive`). Counter `SB[0x49]` ("vita_usando").
- Log: `stats espejo: pedidas listas= datos= (KB) | copias= paginas= (KB/frame)` and `stats espejo: tramos_max= ocupado=
  vita_usando= no_entraron= terminada=` (two lines: the LOG cuts at 160 characters).

### gepresent v13.6 (tools/gepresent/captura.c, "VRAM mirror")
- At the start of each list (`walk`, before publishing it to the readers): `esp_u = SB[0x40]` (last finished copy) and
  `SB[0x48] = esp_u`. `vtr()` reads the VRAM from the mirror of `esp_u`; a page is usable if its last copy in that mirror is ≤
  `esp_u` (`espCheck`, bits per mirror `esp_ok[2][16]`). With no copy yet (0), nothing is valid.
- TRANSFER with a destination in the VRAM: the destination is marked as a render target (`markRt`: it is not requested as data) and the
  source in the VRAM is requested from the mirror.
- Mirror thread: priority 0xBE (below the readers), hash of 4 requested pages every 5 ms in turns (before:
  each page after each copy; with v13.5 it took ~37 % of core 0 [measured]). `hilo_us` is wall-clock time:
  at low priority it includes the waits.
- Log: `espejo: paginas pedidas listas= datos= validas=<espejo 0>/<espejo 1> en_destinos= | por seg: copias=
  paginas_copiadas= (KB/f) ocupado= vita_usando= | sublistas_vram/f recorridas= cortadas= texturas_en_destino/f=` and
  `espejo (por seg): listas= con_la_copia_anterior= | muestreo: revisadas= nuevas= cambiaron listas= datos= hilo_us=`.
- Test outside the Vita: `prueba_memoria.c` simulates the alternating copies (`<modo> 1`: sublists in the VRAM; from
  frame 1 equal to RAM; `<modo> 2`: a page never copied).

## Versions
| Version | What | md5 (GePatch / gepresent) |
|---|---|---|
| dev64.1 + v13.6.1 | double mirror; the Vita used the flip's copy | `662ce863…` / `3a314ded…` |
| dev64.2 + v13.6.2 | fixes the start-up (it announced copy -1: GePatch never copied) | `29c6f543…` / `40041583…` |
| dev64.3 + v13.6.3 | each list uses the latest finished copy, without waiting; small TRANSFERs as a destination | `2f704796…` / `900eccfe…` |
| dev64.4 + v13.6.4 | cap of 64 data pages per copy | `64944cde…` / `03a2a159…` |
| dev64.5 + v13.6.5 | every TRANSFER destination in the VRAM is a destination (no size cap) | `4aa7eb10…` / `e1c9068d…` |
| dev64 + v13.6 (commit) | = dev64.5 + v13.6.5 with the final numbers | `32742693…` / `5a6e524e…` |
From this stage on, builds are done in the Mac's VM (`tools/mac/`): pspdev for ARM64 and vitasdk (GCC 15). The md5s are not
comparable with those compiled in the container (other compilers).

## Results [measured]
Logs: `vitabridge/etapa2_dev64.1/gt1`, `etapa2_dev64.2/gt1`, `etapa2_dev64.3/gt1`, `etapa2_dev64.4/gt1`,
`etapa2_dev64.4/sh1`, `etapa2_dev64.5/sh1` (on the Mac).

### What went wrong along the way
- **dev64.1:** at start-up, the Vita announced "I use copy -1" (fallback to copy 0) and GePatch skipped all the
  copies (`vita_usando=600` per 10 s). The test outside the Vita does not simulate that notice.
- **dev64.2:** the Vita waited (up to ~4.5 ms) for the copy enqueued at the last flip. Because of the sync gate, GT
  waited for the Vita: bridge 55.7 fps (before 57.8). Also, GT enqueues part of the next frame before the flip; for those
  lists the correct copy is the previous one. With dev64.3 (latest finished, without waiting): `con_la_copia_anterior=0` in all
  runs (the flip's copy has already finished when the next list starts).
- **dev64.2/.3:** after ~2 min of GT, `en_destinos` went from 4 to 440–512 pages: many small block copies (when
  loading the race) marked almost the whole VRAM as a destination, and the requested textures dropped from 139 to 12. It was not an
  error: those textures are written by the GE with TRANSFER (the Vita will repeat them). In dev64.4's lap GT did not make those
  copies and the 145 pages were still requested (the heaviest case).
- **Copying all the textures on every frame** (dev64.2/.3, ~730–850 KB per frame) lowered the start of GT's race to
  55–57 fps (the game waits ~0.8 ms per sync on the GE versus ~0.4). With the cap of 64 pages (dev64.4): ~540 KB per frame,
  each texture page up to date every ~2 frames (before ~18).
- **SH (dev64.4):** it requested 253 pages (1 MB) that changed on every copy: one block copy per frame of its 480x272
  image (0x0404C000, stride 512, 32 bits = 136 pages) to a texture. It fell outside dev64.3's 64-page cap;
  dev64.5 marks every TRANSFER destination and SH copies nothing.

### fps (GePatch, every 10 s)
| | dev63.3 + v13.5.3 | dev64.3 | dev64.4 | dev64.5 |
|---|---|---|---|---|
| GT start of the race | 59.8 | 55.5 / 57.2 | 55.9 (loading) / 58.2 | — |
| GT almost the whole lap | ~59.9 | 59.6–59.9 | 59.7–59.9 | — |
| GT bridge | 57.8 | 57.5 | **58.5** | — |
| SH outdoor (last ~30 s) | 40.8 | — | 39.4–40.7 | **43.4** |
- GT with dev64.4: 43–76 list pages and 145 data pages requested; ~540 KB copied per frame; 60–132 sublists per
  frame read from the mirror, 0 cut; GE: the game waits ~0.65 ms per drawsync.
- SH with dev64.5: 0 pages requested, 0 copies; `en_destinos=512`. Capturer ARM ~4 ms per frame.
- **DISTINTAS** (sublist memory check, 1 out of every 32): 0 with dev64.1 (no mirror), 1 / 4 / 7 with
  dev64.2 / .3 / .4 in ~50,000 checked per run (≈0.01 %); with v13.5 always 0. **Cause found and fixed in
  v13.12** (`etapa2_fase2_adelanto.md`): the memory compared against the mirror in which the sublist was recorded, which in the
  frames of the other mirror held a copy from one frame back. 0 DISTINTAS in ~100,000 checks.

### Confirmation with dev64 + v13.6 (2026-10-29, Windows) [measured]
Logs `vitabridge/etapa2_dev64/gt1` and `sh1` (on the PC with Windows). Same code as dev64.5 + v13.6.5.
- **GT** (one lap, with a ~10 min pause in the middle): 59.7–60 fps almost the whole lap; 55–58 fps for ~3 s at the start
  (~75,000 commands, 15,000 draws per frame) and **48–56 fps in the last ~12 s** (~80,000 commands, 16,500 draws per
  frame): the capture takes ~17–19 ms per frame (ARM) and the game waits for it at the sync. During the pause the mirror keeps
  copying ~548 KB per frame without lowering the fps. DISTINTAS: 2 out of 175,172 checked.
- **SH:** the game **never waited for the Vita** (0 µs per sync); capture 4–15 ms per frame, 0 copies. The fps (26–59
  depending on the area) are those of the original game. DISTINTAS: 0.

## What is missing / ideas
1. ~~Understand the few DISTINTAS with the double mirror before drawing~~: fixed in v13.12 (`etapa2_fase2_adelanto.md`).
2. The Vita will have to repeat the block copies (TRANSFER) into the VRAM and keep the result (its own VRAM): what is
   marked as a destination is no longer in the mirror. If the game made the copy before the capture started, the Vita does not
   have it (in practice the capturer starts with the game).
3. To draw (stage 3), MIRROR2 (0x0AC00000–0x0AE00000) must be registered for the GPU (`mapZonas` in gepresent.c).
4. GXM recording on 2–3 fixed cores (next item of stage 2) and T2 (black box).
