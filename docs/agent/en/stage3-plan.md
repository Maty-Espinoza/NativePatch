# Stage 3: minimal renderer (plan and results)

Stage 3 of the roadmap (`../../dev/en/architecture-and-roadmap.md` §6): **does the complete chain (capture → Vita's GPU →
screen) work without slowing the game down?** Plan agreed with the user when the stage started (2026-10-29) and what was
learned in each test version. Status of each step at the end. This document is updated while the stage lasts; when it
closes, what is worth keeping moves to `../../dev/en/findings.md` and the document is archived in `../../changelog/`.

## In simple terms
Until now NativeDraw only **reads** what the game asks of the PSP's graphics chip. In this stage it starts **drawing** it
with the Vita's GPU, at high resolution, starting with the simplest (2D: menus, sprites) and always with a fallback: if
a frame contains something it does not know how to draw yet, the game's normal image is shown. Never worse than without the plugin.

## How Adrenaline 8 draws (`user/menu.c` of isage/Adrenaline, function `AdrenalineDraw`) [code]
- **PSP games:** on every loop it copies the PSP's image (0x74000000, 512x272 8888) into its `psp_tex` texture and draws
  it with vita2d centered, with the scale from its menu (`psp_screen_scale`, 2.0 = full screen).
- **PS1 games:** the image comes from the Vita's PS1 emulator in another texture (`pops_tex`, 1024x544, already in 4:3 and
  with the PS1 scale from the menu). It does not go through the GE: **NativeLink does not see it and NativeDraw does not
  touch it.**
- **"Original" filter** (or the 8's direct scaling: "sharp" at 2x without f.lux): Adrenaline does not draw with GXM and the
  presenter cannot act. To test NativeDraw a filter with GXM (Bilinear) has to be used.
- **Aspect ratio (4:3 / 16:9):** the presenter only replaces the `psp_tex` texture with its own surface of **exactly double**
  its size (1024x544). Adrenaline keeps placing and scaling the image as usual, so its scale, its position and the
  aspect ratio the game has are respected. If a PSP game draws in 4:3 inside its 480x272 (with borders), the HD version
  stays the same. The PS1 image is not touched.

## Steps (each one is tested on the Vita before continuing)

| Step | What is seen | What it validates | Version |
|---|---|---|---|
| **3.0 Transparent presenter** | Nothing different: the game's image goes through a NativeDraw surface of its own | Taking Adrenaline's screen in a stable way and how much it costs; the black screen that was left pending from stage 1 (`../../changelog/en/etapa1_banco_gxm.md` §3.3) | NativeDraw 0.0.3 |
| **3.1 L+R+START menu drawn by NativeDraw** | Menu over the game with the options that today are files (`../../dev/en/logs-and-switches.md`) | Drawing on top. Blocking the game's buttons from the Vita (Adrenaline's input hooks) [verify]; if it cannot be done, NativeLink does it | 0.0.x |
| **3.2 Frame record** | Nothing (only the log) | The capturer, besides counting, saves each draw with its state; 2D vertices are copied (the game may reuse that memory in the next frame). Organized **by sublists** from the start (basis for reusing what was recorded in GT, `etapa2_nucleos.md`) | 0.0.x |
| **3.3 2D translator** | Menus and 2D games in HD (960x544) | Clearing the screen, sprites/triangles in "through" mode, 8888/565/5551/4444 textures, CLUT4/8 palettes, PSP swizzle, basic blending and alpha test. Only the completely understood frame is shown in HD | 0.0.x |
| **3.4 Block copies + comparison** | The same, more robust | TRANSFER on the Vita; T5: the reduced HD image against the native one → if they differ a lot, the native one and a dump | 0.0.x |

Test games: **Retro City Rampage DX** (2D), **KOF 96** (2D; [verify] whether it is the PSP version or the PS1 one: the PS1
one does not go through the GE and is only useful to check that NativeDraw does not touch it) and the menus of GT, RR, SH
and LocoRoco.

## Decisions
- **Versions:** everything is 0.0.x until the first release, which will be NativeLink 0.1.0 + NativeDraw 0.1.0
  (`../../dev/en/versions.md`).
- **Switch `ux0:data/nativedraw/dibujar`, off by default** while it stabilizes (without the file nothing of the presenter
  is hooked). New GPU work is turned on on purpose: in stage 1 an experiment shut down the whole Vita.
- **Own translator**, with PPSSPP as a reference and copying specific functions when it saves time (compatible license,
  noted in `THIRD_PARTY_NOTICES.md`).
- **Shaders:** for 3.0 and the menu, vita2d's already compiled ones (`nativedraw/sombreadores.h`, the same ones Adrenaline
  uses). For 3.3, own precompiled shaders with the FSR toolchain (Sony's compiler under qemu,
  `JagerDesu/vita-shaders`, documented in `../../changelog/en/conversaciones/resumen_conversacion_2026-10-27f.md`).
- **Scale:** 2x (960x544 visible). More (3x reduced = antialiasing) is a later option.
- **Latency:** the HD image is seen one frame after the native one (the Vita draws frame N while the game builds
  N+1). Step 3.0 already works this way.

## Step 3.0: transparent presenter (`nativedraw/presentador.c`, NativeDraw 0.0.3)
- Hooks `AdrenalineUser`'s imports `sceGxmSetFragmentTexture`, `sceGxmEndScene` and `sceDmacMemcpy` (like
  the stage 1 bench). It recognizes `psp_tex` by its size (512x272). The copy of the PSP's image that Adrenaline makes
  goes to two own alternating textures (not to `psp_tex`).
- Memory: surfaces, z and copies in contiguous physical LPDDR; nothing in CDRAM.
- Safety: if the surface comes out black 180 consecutive frames (3 s; 30 until 0.0.8.16, it triggered in KOF 96's
  transitions) and the game's image does not, it stops replacing it until Adrenaline is closed. The log marks "first
  frame drawn" and "60 frames drawn" (to see where it hangs if that happens).
- After Adrenaline's scene, in its thread and its context, it draws `psp_tex` at double size on one of two own surfaces
  (physical LPDDR, 1024x544 with 960x544 visible, z D24S8). In the next frame, when Adrenaline uses `psp_tex`, the surface
  is passed to it. If in a frame Adrenaline did not use `psp_tex` (Adrenaline menu, PS1, Original filter), nothing is
  drawn and when it comes back nothing is changed until there is a new surface.
- `nativedraw.log` every 10 s: frames passed through the surface, CPU per frame (average and maximum) and how many nearly
  black frames the game's image and the surface had (16 samples), for the pending black screen.
- Criterion: the image looks the same as without NativeDraw (with 1 frame of delay), no cuts or black, the game at its
  fps, presenter CPU ≲ 0.3 ms per frame.

## Step 3.0: result (NativeDraw 0.0.3, tested on 2026-10-29) [measured]
Adrenaline's menu, the game selector (preview videos and start-up animation), Gran Turismo (cutscene and
race), Retro City Rampage and KOF 96 look the same as without NativeDraw, at 60 fps, for 15 minutes straight. Presenter
CPU: 0.14–0.2 ms per frame (maximum ~1 ms on an occasional frame). Logs: `vitabridge/etapa3_0.0.3.x/`.

What was learned in the test versions:
| Test | What happened | Lesson |
|---|---|---|
| 0.0.3.1 | The screen showed an orange trapezoid | The `wvp` matrix of vita2d's shaders is **column-major** (translation in [12] and [13]; `mul(pos, wvp)`) |
| 0.0.3.2 | GT cutscene black, static in the selector's preview, odd start-up animation | **Do not use CDRAM**: in Adrenaline's process there are only 4 MB and the videos that the Vita decodes need it. It also explains the stage 1 bench's black screen (pending item closed) |
| 0.0.3.3 | Surfaces in normal LPDDR and no z: black surface | — |
| 0.0.3.4 | Surfaces in contiguous physical LPDDR and no z: Adrenaline frozen (no dump) | With an own render target, **always use z** (D24S8, background 1.0), like the bench and 0.0.3.2 |
| 0.0.3.5 | All fine except a ~20 px strip once in a while | The GPU reads the PSP's image for NativeDraw after it does for Adrenaline, and the next frame's copy overwrote it |
| 0.0.3.6 = 0.0.3 | All fine | Adrenaline's copy (`sceDmacMemcpy` 0x74000000 → `psp_tex`) is diverted to two own alternating textures |

Memory with the presenter (free at once): CDRAM 4 MB (untouched), physical LPDDR 13 MB (uses 2 surfaces + z + 2
copies of the image ≈ 11 MB), LPDDR 25 MB.

Minor pending item: a fixed dotted line over the date in Adrenaline's menu (only there; not in games). It remains to see
whether it also appears without NativeDraw (`dibujar` with `no`).

## Step 3.1: L+R+START menu (NativeDraw 0.0.4, tested on 2026-10-29) [measured]
- Buttons: hook on the ScePspemu import `sceCompatWriteSharedCtrl` (the same one Adrenaline uses for its menu).
  L+R+START opens/closes; while open, the game receives "nothing pressed" (also while the combination is held). It only
  opens if Adrenaline is showing the PSP image (not in PS1).
- Drawing: a 256x128 box (texture in LPDDR, point filter) at double size, centered, with blending, on top of the image in
  the presenter's surface. Text with the installer's 8x16 font.
- Without `dibujar`, the presenter is still prepared (the usual memory) and replaces the image only with the menu open.
- For now it only shows the versions (the user: it is a place for the options; which ones go is decided later).
- Lesson (0.0.4.1): the first line of `nativelink.log` (the version) is written by the PSP directly to the card, before
  NativeDraw serves the log: to know the version the file is read.
- Tested in Adrenaline's menu, Retro City Rampage and Gran Turismo: it opens, closes, blocks the buttons, no jumps.

## Step 3.2: frame record (NativeDraw 0.0.5, tested on 2026-10-29) [measured]
`nativedraw/registro.c` (included in `captura.c`), active with `dibujar`. Per draw: type, count, vertex type,
vertices (and indices) copied at that moment, the state (56 GE registers, `RS`) only when it changes and the loaded
palette (copied at every C4). Frames closed at the flip, 4 records that take turns (`rg_pub` = last complete one; there were 3 until test 0.0.8.6:
the last one the Vita drew is not rewritten until it draws another, because the GPU reads it later). Complete =
all the counted draws went through the record (sublist memory, curves and what could not be read make it
incomplete); an incomplete frame stops being copied. A `registro:` line per second in `nativedraw_captura.log`.
Test outside the Vita: `REG=1 ./prueba 0` (all complete) and `REG=1 ./prueba 1` (incomplete because of memory).

| Game | Complete | Draws per frame | What it uses | Cost |
|---|---|---|---|---|
| Retro City Rampage (NPUH-10199) | 4,142 of 4,143 | **1**: a full-screen rectangle with a T8 texture (256 palette) | the game builds the image with the CPU | 5 µs |
| KOF 96 (NeoGeo Station, NPUH-10119) | 8,032 of 8,118 (7 by cap, in a transition) | ~400 in the fight | T4 rectangles (sprites) with swizzle (~98 %), blending and alpha test (~99 %), 2 backgrounds 5650 | ~2.4 ms; 59.9 fps |
| Gran Turismo (UCUS-98632) | 16,569 of 23,567 (menus yes; the race no: sublist memory and 3D) | — | race: T4 with swizzle, alpha test 96 % | 59.8–59.9 fps in the race |

Lesson (0.0.5.1): the record in uncached memory cost 23–30 ms per frame in GT (6 µs per draw in KOF even with
cache: copying from the PSP's uncached RAM). With cache and no longer copying an already incomplete frame, GT is back at 60 fps.
Retro City does not gain resolution (its image already comes finished at 480x272): it serves as the first drawing test (1 draw).

## Step 3.3, first version: 2D translator (NativeDraw 0.0.6, tested on 2026-10-30) [measured]
`nativedraw/traductor.c` (included in `presentador.c`): in Adrenaline's thread, instead of copying the PSP's image,
it draws the last complete frame of the record at double size. This version: through mode, rectangles/triangles/strips,
5650/5551/4444/8888/T4/T8 texture without swizzle, no blending or alpha test, no color. If a frame contains anything else,
normal image.
- Textures from RAM are copied into the record at that moment (DMA, uncached LPDDR registered for the GPU): reading them
  from the PSP's RAM one frame later mixed two frames (lines in Retro City's title and HUD, 0.0.6.1).
- A frame of the record is repeated at most 2 times (game at 30 fps); if another does not arrive, normal image (0.0.6.1 repeated
  the last one forever and with the game paused the PS button dialog was not seen).
- **"Covered"** (0.0.6.3): the PS button dialog is drawn by the PSP's system on top of the game's frames, which keep
  arriving: every frame 64 points of the normal image are compared with the Vita's drawing; with more than half different 6
  frames in a row the normal image is shown until 10 equal frames. In Retro City it activated only with the dialog. The
  notifications and the volume are drawn by the Vita on top of everything (they do not affect it).
- Retro City (title screen and game): 100 % of the frames drawn by the Vita, 0.23 ms per frame, "perfect".
  KOF 96: 0 % (every frame starts with a clear: next version).
- Lesson: registering the PSP's RAM with `sceGxmMapMemory` (0x70000000, 32 MB) works; 0x72000000 gives 805b0017.

## Step 3.3 in KOF 96 (NativeLink 0.0.3 + NativeDraw 0.0.7, tests 0.0.7.1–0.0.8.12, 2026-10-30) [measured]
KOF 96 (NeoGeo Station, NPUH-10119) drawn by the Vita in the fights, without broken frames, ~40 fps from the Vita with the
game at 59.9. What was learned along the way (the test numbers serve to locate the logs in
`vitabridge\etapa3_<version>\` and the dumps):
- **Own shaders** (`nativedraw/sombreadores/*.cg` → `sombreadores_nd.h`, 0.0.7): color per vertex or from the
  material, texture function modulate/replace (TCC, color at double), alpha test, clears (rectangle of the requested
  color; without the color bit it draws nothing), blending (one fragment program per combination). The surface's alpha
  is always 255: the PSP's alpha is its stencil and Adrenaline does not use it.
- **Intermediate images** (0.0.7.2): KOF draws the fight at the Neo Geo's resolution in a hidden VRAM image and then
  stretches it to the screen. Draws to another image (width ≤ 512) go to an own surface at double size
  (up to 2, 1024x544) and a texture at that address is read from there; draws are grouped into scenes by
  target (`scenesPerFrame = 8`). They are reserved when the translator starts (0.0.7.9: the automatic dump took their memory away).
- **Underneath, the normal image stretched** (0.0.7.5): KOF draws the borders once and afterwards only the center (with black
  they disappeared). Rectangles are clipped to the scissor (KOF's clear is full-screen clipped to the center).
  If no draw goes to the image that is shown (the flip arrives with the other of the double buffer), the last draw's one counts.
- **16-color palettes in 64 bytes** (0.0.7.6) and one equal to the previous one is reused: KOF loads ~260 per frame and with
  the cap of 256 of 1 KB almost all frames went with the normal image.
- **VRAM textures** (KOF's sprites are there): from the mirror (0.0.7.3); only the groups of 8 rows that the
  draws use (0.0.7.5: copying the whole 512x512 sheets cost 7–17 ms per frame and the game dropped to ~35 fps).
  0.0.8: they are copied after the flip, from the mirror copy of **that** flip: the game uploads the sprites of a frame
  after the previous flip, so the copy with which the list was read was from one frame earlier (tiles; dump 013
  of 0.0.7.10 drawn on the PC came out right with the next copy). By DMA (~512 MB/s; the CPU reads the PSP's RAM at
  ~74 MB/s) and without sorting the swizzle: the `nd_fs` shader does it, hence no filter.
- **Full mirror for 2D** (NativeLink, switch 16, 0.0.7.6): with 64 pages in turns KOF (256 pages of
  sprites) saw each page every 4 frames → sprites with old pieces. `espejo_espera` (switch 32, 0.0.8.2): the
  PSP's flip waits for the mirror copy.
- **One-frame broken frames in the fights** (0.0.8.6–0.0.8.11): first 4 records instead of 3 (0.0.8.6). Then,
  measuring with GXM notifications (0.0.8.10, `gpu:` lines of `../../dev/en/logs-and-switches.md`): the Vita's GPU was 3 to 8
  scenes behind and ~140 frames per 10 s started with the one from 2 ago unfinished; they were rewritten with the GPU reading
  vertices and palettes (25–180 every 10 s), the normal image (~200) and textures of the record (~8–11 per second). Each one
  matched a type of error in the video: textures → background with pieces, vertices → deformed "K.O.", palettes → wrong
  colors. Fix (0.0.8.11): if the GPU has not finished the frame from 2 ago, another one is not sent and the
  surface is repeated; before rewriting vertices/palettes or the normal image copy the GPU is waited for (at most 30 ms);
  the next record is one the GPU has already finished reading. Result: 0 rewritten.
- **Performance** (0.0.8.12): the check for different frames used `sceGxmFinish` (~85 times every 10 s, ~20 ms each);
  now it is skipped if the GPU has not finished the surface. Fights: ~35 → ~40 fps from the Vita, CPU per frame ~13 →
  ~6.5 ms, GPU waits ~1.5 s → 0 every 10 s.
- **Pacing** (0.0.8.9): Neo Geo games make transparencies by drawing something one frame yes and the next no; with skips or
  repeats it flashes (HOW TO PLAY box). `traductor: ritmo` line.
- Lesson: recording the screen (Vitastream + OBS at 60 fps) found what the dumps did not capture; the detector of
  one-frame broken frames in a video is in `vitabridge\respaldo\limpieza_0.0.8.11\transitorios.py`. Use videos
  only when needed (`workflow.md`).
- What comes next: the Vita's GPU takes ~20 ms per KOF frame (~430 draws) and every 10 s ~165 frames are not sent
  because it is behind; the translator spends ~7 ms of CPU per frame. Measure GPU time per scene before changing anything.

## GPU performance and Metal Slug XX (NativeDraw tests 0.0.8.13–0.0.8.21, 2026-10-30) [measured]
GPU timer (`cronometro.c`, switch `medir_gpu`, `gpu tiempos:` lines of `../../dev/en/logs-and-switches.md`): each scene
(ours and Adrenaline's) marks its end with a GXM notification (510 for Adrenaline's, 511 ours); a thread
calculates how long each one took.
- **KOF 96, before:** GPU at 96–99 %, ~26 ms per PSP frame (intermediate image ~16 ms with ~430 draws, two surface
  scenes ~3 + ~5.5 ms, Adrenaline ~2 ms) → ~40 fps. Test modes (0.0.8.15, already removed): without textures the
  intermediate one dropped to ~6.5 ms and with the swizzle read as linear to ~7.9 ms → **~8 ms were the swizzle
  calculation in the** `nd_fs` **shader**. The surfaces' memory (CDRAM or LPDDR) hardly mattered. Lesson (0.0.8.14): reserving the
  intermediate one in CDRAM left 1 MB free and Adrenaline showed static in the previews: **never CDRAM**.
- **0.0.8.16: swizzled textures sorted by the CPU** when copying them into the record (`rgTexCopia`: DMA to a cached
  staging memory and the CPU leaves each group of 8 rows sorted; the GPU reads them with `nd_ft`, with filter if the game
  asks for it). `nd_fs` and `trPrueba` were removed. At start-up it is checked that the DMA leaves the cached memory up to date
  (`rgCoherente`: yes). Dumps version 4 (textures already sorted). KOF's intermediate: ~16 → ~7–8.7 ms.
- **0.0.8.18: a single scene for the surface.** If only the draws to the surface read intermediate images and
  none is drawn after being read, first go all the intermediate ones and then the surface in one scene (each
  scene reads and writes its whole surface: KOF goes from T → I → T to I → T, ~3 ms less). **KOF 96 at 60 fps in the
  fights** (615–633 frames drawn every 10 s, ~16 ms of GPU per frame, GPU at 87–97 %).
- **Black-screen safety removed** (0.0.8.18, at the user's request): in the fights it activated because it checked the
  surface with the GPU unfinished (0.0.8.17: all the "black" frames were read that way). In `../../../TODO.md`.
- **Metal Slug XX (ULUS-10495):** the game runs at 30 fps; the match is 2D through (the menus, 3D). The Vita draws it
  entirely at its speed: intermediate ~13.5 ms (~2 million pixels, ~3.6 times the screen), surface ~7.2 ms, GPU
  ~75 %. Textures (`texturas:` line, 0.0.8.19): all from the PSP's RAM, 630–800 KB per frame in 90–170 copies (half
  of them under 1 KB), 128-byte rows, **~98 % equal to the previous copy** (KOF: from the VRAM, 140–220 KB in
  8–12 copies, ~99 % equal). What is expensive is the DMA: ~100 µs per call (10–17 ms per frame in Metal Slug, ~1.5 ms in
  KOF); sorting ~5.5 ms. It averages 90–126 distinct textures per frame (cap 128).
- **Metal Slug XX white glitch** (0.0.8.20 found it): 16x16 background tiles arrived with positions of
  ~2^31 and uv of 65535; the GPU filled them by repeating a 32x32 texture (white screen with colors, scenes of
  55–1900 ms). Cause: the capturer was behind (copying textures cost 20–28 ms of every 33 ms frame) and the
  gate waits at most 5 ms: it copied vertices that the game was already rewriting. 0.0.8.21: a vertex outside ±4096 or NaN
  sends that frame with the normal image (reason `vertice_fuera`). What attacks the cause: the texture cache.
- **0.0.8.22 + NativeLink 0.0.3.4 (2026-10-31): cleanup**, no behavior changes: comments without history or
  TODO (they go to `TODO.md`), out goes the black-screen safety that no longer activated (`falla`), the `volcado_auto` file and
  the mailbox's 0x4B/0x4C words that nobody read; documentation reorganized into `docs/dev`, `docs/agent` and
  `docs/changelog` in English and Spanish.
- What comes next: **texture cache between frames** (sorted in the Vita's own memory; cheap per-sample check
  in every frame, full check in turns 1 of every N groups, and invalidation when the GE writes to that memory; the user
  accepted that in rare cases a texture looks old for up to N frames). Afterwards, translator caches (palettes, translated
  draws, equal scenes).

## Status
| Step | Status |
|---|---|
| 3.0 | **done** (NativeDraw 0.0.3) |
| 3.1 | **done** (NativeDraw 0.0.4) |
| 3.2 | **done** (NativeDraw 0.0.5) |
| 3.3 | Retro City (NativeDraw 0.0.6), **KOF 96** (NativeDraw 0.0.7; 60 fps in the fights since test 0.0.8.18) and **Metal Slug XX** (at its 30 fps; white glitch avoided in 0.0.8.21). Pending to test on the Vita: 0.0.8.21/0.0.8.22 in Metal Slug XX up to the end of the level. Next: texture cache |
| 3.4 | partly advanced: comparison with the normal image (detects what the system draws on top) |
