# Changes

Versions: `docs/dev/en/versions.md`. The detail of each step is in `docs/changelog/` (the conversation summaries are
removed from there before the first release; this file stays as the summary). Versions marked "(test)" were sent to
the Vita but not yet confirmed as a numbered release.

## Tests NativeDraw 0.0.8.13–0.0.8.22 + NativeLink 0.0.3.4 (2026-10-30/31, not confirmed yet)
Will be confirmed as **NativeDraw 0.0.9**. Detail in `docs/agent/en/stage3-plan.md`.
- **GPU timer** (`cronometro.c`, switch `medir_gpu`, 0.0.8.13): how long the Vita's GPU takes in each scene.
- **Swizzled textures unswizzled by the CPU** when copied into the frame record (0.0.8.16): the intermediate image of
  KOF 96 went from ~16 to ~7–8.7 ms of GPU; the shader `nd_fs` and the test modes were removed.
- **One scene for the surface** (0.0.8.18): all intermediate images first, then the surface in a single scene.
  **KOF 96 at 60 fps in fights** (~16 ms of GPU per frame).
- Black-screen safety removed (0.0.8.18, user's request; it fired while the GPU was still drawing). In `TODO.md`.
- **Metal Slug XX** drawn by the Vita at its 30 fps. Texture measurements (`texturas:` line, 0.0.8.19): ~98–99 % of
  the bytes copied each frame equal the previous copy; the DMA is the expensive part. Draw counter fixed.
- White glitch of Metal Slug XX diagnosed (0.0.8.20: slow scenes → log + automatic dump): garbage vertices read late
  because the capturer was running behind. A vertex outside ±4096 sends the frame with the normal image (0.0.8.21,
  `vertice_fuera`).
- **Cleanup** (0.0.8.22 + NativeLink 0.0.3.4, 2026-10-31): comments without history or TODO lists (they live in
  `TODO.md`); unused code removed (the `falla` flag of the removed safety, the `volcado_auto` file, mailbox words
  0x4B/0x4C nobody read, legacy `ge_patch*` log names in the tools); the relay deletes files older than 7 days in
  `enviados/`, `recibidos/` and `respuestas/`; documentation reorganized into `docs/dev` (for people), `docs/agent`
  (for the AI) and `docs/changelog` (history), in English and Spanish; `CLAUDE.md` and good practices added.

## NativeLink 0.0.3 + NativeDraw 0.0.7 (2026-10-30)
Tested on the Vita as NativeDraw 0.0.7.1–0.0.8.12 and NativeLink 0.0.3.1–0.0.3.3 (logs in
`Descargas\vitabridge\etapa3_<test version>\`); what was confirmed is 0.0.8.12 + 0.0.3.3. Detail in
`docs/agent/en/stage3-plan.md` (step 3.3 in KOF 96).
- **Stage 3, step 3.3 in KOF 96:** the Vita draws the fights at double size. Own shaders (`nativedraw/sombreadores/`):
  clears, vertex color, blending, alpha test and swizzle; intermediate images (KOF draws the fight into a hidden VRAM
  image); 16-color palettes; scissor; the normal image underneath.
- **Textures by DMA:** those in RAM right away and those in VRAM from the mirror copy of their flip; only the groups of
  8 rows the draws use; the swizzle was unswizzled by the GPU.
- **NativeLink:** full mirror for 2D frames (switch 16) and `espejo_espera` (32: the PSP's flip waits for the mirror
  copy). Log line buffer from 160 to 256 bytes (long lines were cut).
- **Dumps** by levels, replay of a dump on the Vita and `tools/volcado.py` (`docs/dev/en/dumps.md`).
- **One-frame broken frames in fights (fixed):** the Vita rewrote textures, vertices, palettes and the copy of the normal
  image while its GPU was still reading them (it ran 3 to 8 scenes behind). Measured with GXM notifications (`gpu:`
  lines). Now no new frame is sent if the GPU has not finished the one from 2 frames ago (the surface is repeated), the
  GPU is waited for before rewriting (30 ms at most) and the next record is one the GPU has finished reading. Rewrites
  while the GPU was reading: 0.
- **Performance:** the different-frames check and "covered" read the surface only if the GPU has finished it (no
  `sceGxmFinish`, which cost ~20 ms in KOF); 4 copies of the normal image.
- **Cleanup:** comments say what each function does and how (history moved to the docs); unused code and counters
  removed (identical binary).
- Measured in KOF 96 fights (every 10 s), 0.0.8.11 → 0.0.8.12: Vita frames ~350 → ~385–410 (~35 → ~40 fps), CPU per
  frame ~13 → ~6.5 ms, GPU waits ~1.5 s → 0, different-frames check ~2.3 s → ~0.03 s, repeated frames 40–80 → 1–12. The
  game stays at 59.9 fps. The limit is now the Vita's GPU (~165 frames every 10 s not sent because it runs behind).
- Known minor bug: the PS button in KOF 96 (`TODO.md`).

## NativeDraw 0.0.6 (2026-10-30)
- **Stage 3, step 3.3 (first version):** with `dibujar`, the Vita draws the frame with its GPU from the record, at
  double size (simple 2D: rectangles and triangles with texture, no transparency). Retro City Rampage is drawn entirely
  this way. What it cannot draw yet is shown with the normal image.
- If something not in the record covers the image (the PSP's PS-button dialog), the normal image is shown.

## NativeDraw 0.0.5 (2026-10-29)
- **Stage 3, step 3.2: frame record.** With `dibujar`, the capturer stores every draw of the frame (copied vertices, GE
  state, palettes) so the Vita can draw it in step 3.3. Retro City and KOF 96: ~99 % complete frames; GT stays at 60
  fps. The `registro:` log line says what each game uses.

## NativeDraw 0.0.4 (2026-10-29)
- **Stage 3, step 3.1: L+R+START menu** drawn by NativeDraw over the game; while it is open the game receives no
  buttons. For now it shows the NativeLink and NativeDraw versions (the options that are files today will be added
  later). Works even with `dibujar` off.

## NativeDraw 0.0.3 (2026-10-29)
- **Stage 3, step 3.0 (transparent presenter):** NativeDraw takes Adrenaline's screen: the PSP game image goes through
  an own surface of double size drawn with the Vita's GPU (a copy for now). It respects the scale, position and aspect
  ratio Adrenaline chooses; PS1 games are not touched. Off by default: enabled with `ux0:data/nativedraw/dibujar`.
  Cost ~0.15 ms per frame. Detail and lessons (CDRAM memory and videos, mandatory z) in `docs/agent/en/stage3-plan.md`.
- Relay (`tools/vitabridge.py`): sleeps when there are no requests (lets the Vita sleep: `nosleep off`) and opens the
  FTP only for what is needed.

## NativeLink 0.0.2 + NativeDraw 0.0.2 (2026-10-29)
- **NativeLink:** everything inherited from GePatch that was no longer used is removed: scaling on the PSP
  (architecture A), fake VRAM, descriptor presentation, old L+R+START menu, per-game options, capture to the log and
  .gpd dumps (L+R+SELECT), PC tests. What remains is stage 2, unchanged: mailbox for NativeDraw, VRAM mirror, gate,
  telemetry and log. 3,889 → ~620 lines.
- **NativeDraw:** the presentation of GePatch's scaled image, the stage 1 GXM bench and dump writing (24 MB ring) are
  removed. The capturer and NativeLink's log remain.
- **Installer** (`NativePatch.vpk`, new): installs/updates both plugins (Adrenaline 8: `plugins.txt`; taiHEN:
  `*PSPEMUCFW`), uninstalls and cleans the leftovers of the tests (temporary, until before 1.0).
- **Repo:** old test tools out (PPSSPP lab, geptest, kbench, vprobe, .gpd dumps); environment by scripts
  (`tools/preparar.sh`, `tools/compilar.sh`, `tools/windows/`). Documents split into `docs/dev` (current) and
  `docs/changelog` (archive). New README, GPLv3 license.
- Tested on the Vita: Gran Turismo same as with GePatch dev66 + gepresent v13.13 (≥ 60 fps in the heaviest part, 0
  DISTINTAS); the installer installed and cleaned the leftovers (`docs/changelog/en/etapa3_preparacion.md`).

## NativeLink 0.0.1 + NativeDraw 0.0.1 (2026-10-29)
- GePatch is renamed **NativeLink** and gepresent **NativeDraw**; each in its own folder. New names for files, logs and
  paths on the Vita. Same behavior as GePatch 0.2-dev66 + gepresent v13.13.

## Before: GePatch and gepresent (2026-10-21 to 2026-10-29)
- **GePatch 0.2-dev1…dev60 (architecture A):** revive TheFlow's GePatch on Adrenaline 7/8: the PSP's GE draws at
  960x544 with a fake VRAM. Many errors were fixed, but the GE cannot handle 4 times the pixels (GT 24–37 fps, SH 15–21)
  and the VRAM trick caused image errors. Tools: menu, .gpd dumps, lab in PPSSPP, relay with the Vita (vitacompanion).
- **gepresent v1–v10:** Vita plugin that presented GePatch's image with the Vita's GPU (per-game scale) and wrote the
  log and the dumps for the PSP.
- **Analysis (2026-10-26/28):** hardware tests (the PSP's VRAM is not visible from the Vita; it cannot be enlarged) and
  the new architecture **B**: the PSP runs the game unchanged and the Vita reads its lists and draws them in HD.
- **Stages 0–1 (2026-10-28):** how much each game draws (GT ~9000 draws per frame) and what it costs in GXM (bench v11).
- **Stage 2 (dev61–dev66 + v12–v13.13, 2026-10-28/29):** shadow mode + capturer on the Vita, sublist memory, double VRAM
  mirror, T4/T6, impact analysis, gate before enqueue, look-ahead check. Result: GT ≥ 58 fps in the heaviest part (60.2
  with v13.13), Ridge Racer and LocoRoco at 60, Silent Hill at its original speed, 0 DISTINTAS.
- Discarded along the way: FSR 1 (adrfsr, did not improve the image), "by copy" 2D vertices, reading the PSP RAM
  directly without the shared cache, not re-reading checked lines.
