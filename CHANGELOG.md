# Changes

Versions: `docs/dev/en/versions.md`. The detail of each step is in `docs/changelog/` (the conversation summaries are
removed from there before the first release; this file stays as the summary). Versions marked "(test)" were sent to
the Vita but not yet confirmed as a numbered release.

## Tools (2026-11-03, no plugin change)
- `tools/perf/`: performance bench of the capturer and the record on the PC (ARM under qemu, synthetic scenes shaped like Ridge Racer, 2D
  sprites and sublists), with a deterministic estimate of Vita time from memory counters, a committed reference (`baseline.json`), the
  KEEP/REVERT step (`iterate.sh`) and the unattended loop with Claude Code (`ai_loop.sh`, `docs/agent/en/perf-loop.md`).
- `docs/dev/*/performance-plan.md`: the ordered plan towards 60 fps, from the analysis of the capturer on `stage3_0.0.16.4/rr1`.

## NativeDraw 0.0.25 + NativeLink 0.0.8 (2026-11-05)
Tests 0.0.24.1-0.0.24.8, checked on the Vita with God of War Ghost of Sparta (`hold_game`): no crash in about 3 minutes of play, cutscene in HD. The menu (~4,500 draws per frame) and play (6,000-13,000) go past the record's limit, so they still show the normal image.
- **GE state of the previous game:** when a frame failed, the capturer went back to the state of the last clean frame, which could belong to the previous game, so a game opened after another one never reached HD. NativeLink now publishes the real BASE and the capturer repairs its copy; going back to the last clean frame keeps it.
- **Sublists copied to VRAM during the frame:** God of War copies its lists from RAM to VRAM with GE transfers and calls them right after, while the mirror only held the last flip. The capturer now applies each RAM to VRAM transfer to the mirror, with aligned accesses (a general memcpy on that memory crashed Adrenaline with C1-2858-3) and only inside the real 2 MB of VRAM.
- **Measurements (development):** `bad target:`, `base:`, `vram copy:` and the `capture stopped` watchdog.
- **Test:** `capture_test` checks that a transfer reaches the mirror and that one above the real VRAM writes nothing.

## NativeDraw 0.0.24 (2026-11-05)
Tests 0.0.23.1-0.0.23.5, checked on the Vita with Ridge Racer and `hold_game`. Against 0.0.23: normal race 29.7 -> 32.4 fps, hard stretches 27.8 -> 29.8, crowd 26.9 -> 29.1; GPU ~4 ms less per frame.
- **Images that only blur at PSP size:** an intermediate image of half the screen or less, made only of 2D rectangles that read no record texture, is kept at PSP size. The bloom chain falls from 8.9 to 4.7 ms.
- **Measurements (development):** `gpu scenes:` (scenes per image and kind, one whole frame), `resources:` (GPU, battery, temperature, clocks), `fx_log` (pixels per effect on the surface), `keep_hd` (to compare).
- **Tried and dropped:** not drawing the image underneath: 522 thousand fewer pixels, same GPU time.
- **Found:** undocumented power functions restart the Vita; close cars add 8-10 ms in blended layers. `tests/perf/runs/` keeps the useful runs, with an inventory.

## NativeDraw 0.0.23 (2026-11-05)
Test 0.0.22.1, checked on the Vita with Ridge Racer and `hold_game`. Against 0.0.21.6: hard stretches 24.5 -> 28.8 fps, crowd 25.5 -> 28.4 fps, vertices 37-60 % lower. The rear-view mirror looks right.
- **Boxed stretch (rear-view mirror) on the GPU:** a stretch behind a partial scissor that starts with a clear of color and depth covering it goes to an image of its own, moved to its corner, before the surface; the edge of that image clips it on the GPU and the result is pasted on the surface. The capturer marks those draws for the GPU. One stretch per frame.
- **`tr_gv.sh`** now compares the triangles of that stretch too.

## NativeDraw 0.0.22 + NativeLink 0.0.7 (2026-11-04)
Tests 0.0.21.1–0.0.21.6 and NativeLink 0.0.6.1, checked on the Vita with Ridge Racer and `hold_game`. Against 0.0.21.5: crowd 18.8 -> 25.5 fps, translator 45 -> 30 ms.
- **`hold_game`** (development): the game waits for the whole chain, so every frame is complete and in HD. It needs NativeLink: the gate waits up to 200 ms. For measuring, and playable.
- **Draws joined by indices:** draws of the GPU path with the same state go in one GXM call (crowd frame 3405 -> 267). Vertices for the GPU in multiples of 48: packed without 16-byte alignment the GPU crashed.
- **Translator short path:** a draw with the state of the one before only adds its indices; repeated checks skipped; quick scissor accept and reject.
- **Tools:** `nativedraw/measures.h` (the measurements, one line each), `tools/run_compare.py` (runs zone by zone), `tests/perf/*.json` (reference laps), a real crowd frame in `tools/perf/data`.
- **Found:** bounding box jumps are not in the crowd; the slowest stage there was the drawing thread. Detail in `performance-plan.md`.

## NativeDraw 0.0.21 (2026-11-04)
Tests 0.0.20.1–0.0.20.3, checked on the Vita with Ridge Racer; NativeLink stays at 0.0.6. Car selection: ~28 -> ~35 HD frames per second; race unchanged; game at 60 fps.
- **Frame skipping:** `regFlip` skips a frame only if the presenter did not take the last one and the capturer was behind at a sync, or if the record to write is still read by the GPU.
- **Bug found on the way (0.0.20.1, 0.0.20.2):** with no skipping, no record was free and `regFlip` wrote one the GPU was reading: the drawing thread froze once and the GPU crashed once. `with the GPU unfinished (measured)` was 0 in the runs before and after, 1137 and 13 in those two.
- **`drawing stuck:`** (development builds): logs where Adrenaline's drawing thread is if it finishes no scene for a second.

## NativeDraw 0.0.20 (2026-11-04)
Tests 0.0.19.1–0.0.19.4, checked on the Vita with Ridge Racer; NativeLink stays at 0.0.6. Car selection against 0.0.19: vertices ~21 -> 2.3 ms per HD frame,
translator ~33 -> 16.5 ms, GPU ~20 -> ~21 ms, Adrenaline's frames ~27 -> ~44 per second, HD frames ~21 -> ~27 per second. Race: no clear change; game at 60 fps.
- **`nd_v4`:** a vertex program for types `0x142` and `0x143` (16-bit position and normal, no color): one directional light with specular, or UV generated from the normal. Both passes of a car use it, so they get the same depth.
- **Discarded:** vertex copies on the record thread (0.0.19.1-0.0.19.2): no gain and deformed triangles; reverted.

Tests 0.0.18.1–0.0.18.8, checked on the Vita with Ridge Racer; NativeLink stays at 0.0.6. Race against 0.0.18: vertices ~19.5 -> 13.1 ms per HD frame,
translator ~37 -> 32.1 ms, GPU ~35 -> 33.2 ms; 54 % of the vertices moved by the GPU; the HD rate has no clear gain (20.5 -> 21.7, noise 19.4-21.0); game at 60 fps.
- **`nd_v3` (first stage of C.1):** a vertex program reads the 3D vertices as the game wrote them (type `0x19F`, no lights, no generated UV, smooth) when the scissor takes in the whole image. They stay at the end of `tx`, so no new memory. Narrow textures are always copied without their gap.
- **Removed:** `clip_gpu`, `prog_swap`, `no_join`, `light_split`, `clip_discard`, `nd_vc`, `nd_vs`, `nd_ftc`, `nd_fcc`. New switch: `cpu_verts`.
- **Tools:** `tools/perf/tr_gv.sh` (in `test_ci.sh`): both paths send the same triangles to the surface on real frames.

## NativeDraw 0.0.18 (2026-11-04)
Tests 0.0.17.3–0.0.17.6, checked on the Vita with Ridge Racer; NativeLink stays at 0.0.6. Race seconds against 0.0.17: GPU 38.1 -> 32.5 ms per
HD frame, idle 24 -> 36 %, pixels on `nd_ft` 1392 -> 90 thousand; the HD rate does not change (~19.5 per second): the translator now limits alone.
- **`nd_fa`:** a branchless fragment program for modulate with the alpha tests greater and greater or equal, as one minimum on the alpha.
- **Measurements:** `vert_split` (what the vertex time is made of) and `thread_split` (cores, preemptions and moves of the main threads).

## NativeDraw 0.0.17 (2026-11-04)
Tests 0.0.17.1–0.0.17.2, checked on the Vita with Ridge Racer; NativeLink stays at 0.0.6. Race seconds against 0.0.16: GPU 45.0 -> 38.3 ms per
HD frame, translator 42.0 -> 38.1 ms, 18.1 -> 19.6 HD frames per second; game at 60 fps.
- **`nd_fd`:** a branchless fragment program for modulate with doubling or fog and no alpha test; 27 % of the pixels.
- **Translator:** a NEON loop for the common 3D vertex (float position and texture coordinates, 8888 color, no lights on), and draws that share
  the state keep scissor, material and layout.
- **Tools:** `tr_check.sh` (same draws, state and vertices as a commit), `arm_count.sh` (real ARM instructions); shaders are built in the container.

## NativeDraw 0.0.16 (2026-11-04)
Tests 0.0.16.1–0.0.16.8, checked on the Vita with Ridge Racer; NativeLink stays at 0.0.6. **Without the high clocks:** game at 60 fps in
every part, the whole race in HD with no frames of normal image, 21-25 HD frames per second; in the crowd complete frames went from
4-5 to ~18 per second.
- **Mirror leak:** a texture in a page of an image was requested for good; in a session the PSP copied 370 pages per frame (3.2 ms) and
  the game fell to 40-46 fps. Those pages now expire when no frame reads them.
- **Capturer lighter:** plain and matrix commands in a tight loop with counters once per line; a repeated texture skips its checks and,
  in 3D, the record's lookup; the record reads the clock on 1 draw in 64.
- **Tried and removed:** cutting a late list at the flip (broke the state) and not walking unrecorded frames (lists read late).
- **Development:** `bottleneck:` lines and `tools/bottleneck.py` (what limits each second, pixels per program), `cap_split` (capturer by
  parts; its clock reads slow it down, off for speed runs), capture time no longer counts the flip walk twice.
- **Tools:** `tools/perf`: exact instructions per frame with Callgrind, profile by function and line, the translator on real frames,
  pages asked of the mirror, A/B against a commit.

## NativeDraw 0.0.15 (2026-11-03)
Tests 0.0.15.1–0.0.15.10, checked on the Vita with Ridge Racer; NativeLink stays at 0.0.6. The whole lap goes in HD at 20-25 HD frames per
second (about 18-22 in the crowd) with the game at 60, with the high clocks on; nitro, crowd, reflections and bloom look right.
- **Intermediate images were drawn shrunken:** a scene with a valid region gets a viewport of that region, so the matrix now scales to it.
  Reflections (image 1FE000) and bloom read the right place; the white blocks on the cars came from this.
- **CPU and GPU in parallel:** the next frame is prepared while the GPU draws the last one, and its surface scene waits for it. From ~12 to ~22 HD frames per second.
- **Draws with the same state joined** (triangle strips with repeated vertices in between) and their state set once.
- **Vertices built in cached memory** and copied once: reading them back from uncached memory cost 153 ns per word against 21.
- **Lit vertices with no lights on** (almost all of them): no normals, integer color. Checks and blends once per state; palettes compared in cached memory.
- **Nitro:** the game reads the image of the frame before as a texture; it no longer stops the record and the Vita uses its HD image.
- **Mirror:** a texture page can come from either mirror; a page used this frame stays requested under a target whose height is an estimate.
- **Capturer:** each image counts once among the frame's targets: with the list full, textures in target pages were taken for images (`bad_texture` in the crowd).
- **Development:** `clocks` switch (CPU 444, crossbar 166), `peek` when a dump has no memory, detail lines of the translator.

## NativeDraw 0.0.14 (2026-11-03)
Tests 0.0.14.1–0.0.14.15, checked on the Vita with Ridge Racer; NativeLink stays at 0.0.6. A whole lap of the race goes in HD (0 frames
with the normal image in the race), about 10 HD frames per second with the game at 60. The crowd section still shows artifacts.
- **Scenes:** a frame can read the surface mid-way (the scene is cut), an image being drawn (through a copy, `rtc`) and the image of the
  frame before. Reordering only reaches the first draw that needs the game's order, and depth is stored and loaded when used after a cut.
  Intermediate-image scenes touch only their region and use their own render targets (8 scenes each).
- **What the PSP computes:** draws on images nobody reads are not done, and images started with points, palettes built by the GE and
  data sharing pages with images come from the mirror (lens flare, lamp halos). Render target height from the scissor and region,
  not MINZ.
- **Blends and primitives:** gray fixed blend, color test that only rejects black under an add, lines as thin triangles, big fans in through mode.
- **Capturer:** a frame is not recorded until the presenter took the one before; the state goes back to the last clean frame after a late one;
  real GE values also for lights and materials; VRAM vertices go with the lists.
- **Presenter:** a surface the GPU did not finish is not shown or overwritten; a single frame that cannot be drawn keeps the last surface.
- **Development:** frames with a failed draw saved at once to `fails/`, census of the frame (extreme), cause of `not_read`, dumps wait for
  the GPU; dumps for slow scenes removed.

## NativeDraw 0.0.13 + NativeLink 0.0.6 (2026-11-03)
Tests 0.0.13.1–0.0.13.7 + NativeLink 0.0.6.1–0.0.6.2, checked on the Vita with Ridge Racer. The race runs at 59.9 fps on the normal
image, without stutters or jumps; menus and the selector stay in HD.
- **Profile of a frame's whole journey** (`prof` lines at the extreme level, `tools/prof_summary.py`): PSP, capturer, record thread,
  latencies, Adrenaline's thread, translator, GPU, CPU per thread and fixed limits reached.
- **Gate:** the "10 changes = at the sync until closing" rule is gone; the gate can always return to before enqueuing.
- **Capturer:** a list the game has left behind (past its sync, at its stall with no END) is dropped instead of sticking to it; a frame
  closed with lists still queued is not drawn, nor the next one.
- **Stutters:** `nativedraw.log` goes through a ring and the writer thread; writing it inside Adrenaline's drawing thread took
  50-558 ms per line.
- **HD only when it holds:** after a frame that cannot be drawn, HD resumes once the frames have been drawable for a second
  (`held_back`); an HD frame in the race cost ~100 ms and showed as a jump.
- Development log: `clocks:` (GPU 222 MHz) and `prof record: mirror_ge`.

## NativeDraw 0.0.12 + NativeLink 0.0.5 (2026-11-02)
Tests 0.0.12.1–0.0.12.14 + NativeLink 0.0.4.1, checked on the Vita with Ridge Racer. Menus and the car selector are drawn in HD; the
race still goes with the normal image.
- **3D in the translator:** matrices in the record, transform and perspective, depth, cull, lighting, fog, generated UVs, texture
  functions decal, blend and add, doubled alpha blend factors. Material alpha comes from 0x58.
- **Sublist memory** only runs when a frame does not fit the record (over 4096 draws).
- **Lost GE state:** NativeLink reads the real registers every 32 frames; a stable value that differs from the capture replaces it.
- **Speed:** fast fragment program, palettes translated once per frame, state lookup by table, VRAM textures in their own thread,
  matrix and light blocks only when they change, close 3D vertices by DMA, draw timing sampled.
- Slow scenes ask for dumps only at the `auto` level.
- **Ridge Racer selector:** game ~57 fps, translator 193 to ~30-38 ms per frame, GPU ~31 ms. **Race (normal image):** 30-47 fps, capturer ~21 ms.

## NativeDraw 0.0.11 (2026-11-02)
Tests 0.0.11.1–0.0.11.3, checked on the Vita with Street Fighter Alpha 3 MAX and Metal Slug XX. NativeLink stays at 0.0.4.
- **Triangle fans (prim 5)** in the translator and `tools/dump.py`: Street Fighter fights are drawn in HD.
- **Texture cache fixed:** the sample read only the first column of each group, usually transparent, so characters showed
  old pieces. It now reads a different column in each row, and textures under 8 KB stay out of the cache.
- **Dumps:** without memory for 3 frames they save 1 instead of failing (the cache takes ~4.25 MB).
- **Metal Slug XX (per frame, median):** capturer 9.7 ms, textures 5.3 ms, reused 736 KB.

## NativeDraw 0.0.10 (2026-11-02)
Test 0.0.10.1, checked on the Vita with Metal Slug XX. NativeLink stays at 0.0.4.
- **Texture cache between frames, RAM textures only:** a texture stays sorted in a 4 MB store the GPU reads, and draws use
  it without copying. Each frame 8 words per group of 8 rows are sampled, and 1 group of every 16 is hashed whole. A
  change, or a block copy over it, sends the texture to the record as a normal copy until no record reads the entry.
- **Metal Slug XX (per frame, median):** capturer 19.4 → 8.3 ms, record 16.2 → 5.5 ms, textures 14.5 → 4.1 ms, copied from RAM
  746 → 3 KB, DMA calls 44 → 0. Same 30 fps, no visual errors reported.
- **Not touched:** VRAM textures, so KOF 96 fights and their tiles stay the same.

## NativeLink 0.0.4 + NativeDraw 0.0.9 (2026-11-02)
Closes tests NativeDraw 0.0.8.13–0.0.8.32 and NativeLink 0.0.3.1–0.0.3.11. Tested on the Vita with NPTEST (49 scenes identical
to the PSP, 41 falling to the normal image with their reason), KOF 96 and Metal Slug XX.
- **NPTEST** (`tests/nptest`, `docs/dev/en/nptest.md`): 108 2D scenes; `tools/nptest_report.py`, `tools/nptest_compare.py`;
  PPSSPP headless package for the PC (`tools/emu.sh`).
- **Translator:** 8-bit UVs, UV rotation of rectangles, scissor and region on all primitives, flat shading, texture width,
  MIN/MAX blending, alpha test mask, channel masks, depth and stencil in 2D.
- **Capture:** truncated frames marked incomplete, images held in HD only for the frame that draws them, palette and rows
  per list, TRANSFER onto the shown image, cases with no answer rejected with a reason.
- **Known:** tiles in the KOF 96 player select, also on 0.0.8.29 (`TODO.md`).

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
