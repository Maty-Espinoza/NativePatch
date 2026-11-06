# TODO: pending items

The only list of pending work. Ordered from what comes next to what can wait. Each item says where it came from
(a document in `docs/dev/en/`, `docs/agent/en/`, `docs/changelog/en/` or the claude.ai project). Done items are removed;
what was done is in `CHANGELOG.md`. Updated 2026-11-06 (NativeLink 0.0.8 + NativeDraw 0.0.25 confirmed; tests up to NativeDraw 0.0.25.20; photo at the sync proposed). How the work is done: `docs/agent/en/workflow.md`; stage 3 plan and results:
`docs/agent/en/stage3-plan.md`.

## Next
- [ ] **God of War play in HD, the translator is the limit** (0.0.25.23): HD frames come (the atlas and glow of 0.0.25.20 work), but only 3 per second: the translator spends ~1000 ms of CPU per second, ~750 of them on vertices (~250 ms per HD frame). 28,000 vertices per frame, 26,600 not moved by the GPU because of their type: `0x1BE` (16-bit UV, 8888 color, 8-bit normal, float position, lit; 27,000 vertices in `gow_combat.bin.gz`) and skinned ones (`0x7BE`, `0x47B9`, `0x87B9`, `0x7B9`...). The GPU path reads only `0x142`/`0x143` (`GVL`, record.h). With `hold_game` the game waits for it: 5-6 fps. 0.0.25.24: `0x1BE` with no lights goes to the GPU (`nd_v3` with 16-bit UV; `tr_gv.sh` gives the same triangles, 62,571 vertices of `gow_combat`), but on the Vita it did nothing: GoW's 3D vertices are in VRAM and only those in RAM were offered to the GPU (log: `not marked 26355`, `draws_vert_vram=5953`). 0.0.25.25: VRAM vertices with a mirror copy also go to the GPU (`rgVNow`); to measure on the Vita. Next: skinning (~31,000 vertices per frame), then more than one light. The whole list of what is not done: `docs/dev/en/unsupported.md`.
- [ ] **God of War after the fight, garbage VRAM sublist** (0.0.25.23, from frame 7805, second ~334, to the end of the run): the walk goes into the VRAM sublist `04190400`, whose words look like floats (`40a567de bfb585f6 c1f35ba1 3f2aaaab`), walks 400,000 commands (the limit) and drops the frame (`list_too_long`): 116 ms per frame, the game at 9 fps with `hold_game`, no HD. Same family as the garbage chain of 0.0.25.17 (mirror or BASE). Look at the transfers that fill that sublist and the mirror pages around it.
- [ ] **God of War menu, image `162000` left out for `mask`:** an alpha test the translator cannot convert (`alphaFn`); what reads that image gives `other_target`. Log the alpha test register (0xDB) of that draw first.
- [ ] **Photo at the sync** (`docs/dev/en/performance-plan.md` §D, proposed 2026-11-06 from the user's frame-sync idea of 2026-11-05, instead of waiting at the sync only when needed): at each sync the RAM the frame used is copied to the Vita and the capturer walks that copy, so nothing the game writes after the sync reaches the frame and the reads have cache. Phase 0 measured in 0.0.25.21 (§D, "Phase 0 results"): `xferMirror` 30 ms per menu frame and 70 ms per play frame in God of War; DMA into cached memory 429 MB/s with ~170 us per call; the photo would copy 3.3-5.7 MB per God of War frame (the 8-page margin almost triples it) with 21-22 % of frames missing a page; in Ridge Racer 70-90 % of frames have no history because the list start moves every frame. 0.0.25.22: by the last frames no frame lacks history (Ridge Racer by list: 63 %), God of War still misses in 15 % of frames, and in the Ridge Racer race the photo would cost more than it gives. 0.0.25.23: every missed page was never read before in the run (new memory: vertices, transfer sources, textures), so no history predicts it; the photo needs a way for frames that read new memory (read them live and keep the frame only if the page was not written after the sync, or wait like hold). Next: `photo_probe` with God of War without `hold_game`; then decide phase 1 (transfers by copy: DMA from ~20 KB, CPU copy below, model 9.7 -> 12.5 fps in the menu with hold) and the photo as a switch. Lab and model: `tools/perf/sync_lab.sh`.
- [ ] **`sync_lab` does not model yet:** several syncs per frame, games that never call a sync (photo before the next enqueue or at the flip), CPU writes to VRAM, the GE slowed by the copy (bus), and the cost of the copy itself (the model `sync_model.py` does).
- [ ] **God of War closes (C1-2858-3) with `hold_game`:** twice, the PSP stopped first (`capture health:` the capturer was idle); not with `hold_game` off; not seen with `gate_sync`. Suspected: frames of 200+ ms the game does not tolerate. Not proven.
- [ ] **God of War, still to check once the menu and play are in HD:** the gray blocks of the menu (dumps 287-289: 9 draws with garbage state, from the mixed-frame captures; check again with the sync) and whether an image kept at PSP size was something sharp (`keep_hd`).
- [ ] **Diagnostics added for God of War, remove when closed:** `walk_log`, `walk_trace` (flight recorder), `capture health:`, the `hold: flip walk` lines.
- [ ] **PSP jingle when Adrenaline opens sounds slow** (user, 2026-11-05, with NativeDraw 0.0.25.14): not looked at. Possibly related to the memory work of 0.0.25.1-0.0.25.10 (spare block, `no_memo`); check with NativeDraw off.
- [ ] **`dump: no memory` also in `auto` level:** God of War only got peeks, no record.
- [ ] **Crowd work left (`performance-plan.md`):** tight path for BASE/VADDR/PRIM in the capturer.
- [ ] **Ridge Racer crowd, late lists as complete frames:** garbage state reaches the translator (`color_test_or_logic`, `vertex_out`, `mask`), 1 to 8 frames per run. Look at why they pass as complete. The user also saw something odd in the change to the normal image there: not explained.
- [ ] **C.1, what is left:** other vertex types, more lights, indices. `0x143` has no saved frame in `tools/perf/data`: not compared by `tr_gv.sh`; a dump of car selection is missing (`dump: no memory`). Remove the `trWhy` logs when closed.
- [ ] **Logs per game (agreed with the user, 2026-11-05):** on the Vita `ux0:pspemu/logs/<game ID>/{nativedraw,capture,nativelink}.log.0` to `.4`, newest first, rotated together when the game is known, with a boot log for what comes before. In the relay `logs/<game>/` and `dumps/<game>/`, replaced on each test, read by run stamp in the first line, with the file names the tools use today. Then clean the relay's old folders on Windows and the Mac, with the user's OK.
- [ ] **Second surface scene (paste of the bloom and HUD):** 5.6 ms on every frame, no clear solution yet (`performance-plan.md`).
- [ ] **Boxed stretch, limits:** one per frame, and only with a position-only clear (`trBoxed`); the others stay on the CPU with vertices in slow memory, because the capturer marks them all. In the crowd, a full-screen rectangle with depth test comes after the mirror and no longer sees its depth: no error seen.
- [ ] **High clocks as an option of the L+R+START menu** (user, 2026-11-03): a plan B, the goal is not to need them. Today a development switch: production builds run at Adrenaline's clocks.
- [ ] **Other games with 0.0.15** (KOF 96, Metal Slug XX, Retro City, Street Fighter, Gran Turismo): only Ridge Racer was tested with CPU and GPU in parallel. A GPU slower than the CPU now waits inside the hook instead of skipping the frame.
- [ ] **GPU, the limit away from the crowd** (95 % busy, main scene ~30 ms): almost every 3D draw uses `nd_ft`, generic, with `discard`, because of the fog. A program with fog, no alpha test and no `discard`; `shacc` must be built in the container. Skip the image underneath when the frame covers it.
- [ ] **Ridge Racer, capturer in the crowd:** ~16 ms per frame at 444 MHz (~3500 draws), `behind` 6-12 per second. Without the high clocks it does not fit.
- [ ] **Memory:** under 1 MB free in one go after start, dumps fail (`peek` replaces them for now). Look at what can go (4 x 1.5 MB of textures, `fails`).
- [ ] **Differential test of the translator:** this conversation ran old and new `trFrame` on the PC with GXM stubs over a real `f0_record.bin`, comparing all triangles and states. Worth keeping as a test tool in `tools/`.
- [ ] **Translator failures left:** color test that is not black under an add (`fails/003_color_test_or_logic`), the first frame of the flare before its texture is in the mirror, T8 with no copy (`fails/006_bad_texture`, not understood), points on images that are read, memory free after start: 0 MB in one go with the dev reservations.
- [ ] **Ridge Racer, opening movie no longer plays in HD versions** (user, 0.0.14.11): check it after the race. Also dump 255 and the 2 videos in the claude.ai conversation of 2026-11-03.
- [ ] **VRAM mirror limit:** 64 data pages per copy (`MIR_DATA`) while Ridge Racer asks for 250-500; reached on every flip and each
      page refreshes every ~7 frames (HD frames could carry textures up to 7 frames old). `prof record: mirror_ge` reads ~20 ms but is an
      upper bound (the capturer looks between lists): refine it (look inside `walk`) and then size the copy by measured GE time
      instead of a fixed count.
- [ ] **Capturer budget in the heaviest stretch:** with the record it does not fit in the frame (16 ms with record, 8 without);
      the gate waits up to 5 ms each frame and those frames go incomplete. Split in `prof capture` (`rec_state`, `rec_vertices`,
      `rec_textures`) before cutting. Copying the list at the sync on the PSP would remove the gate, but the vertices are not in the
      list: the `data life` line (vertices changed 1-2 frames later) says how much slack that gives.
- [ ] **Other fixed limits to review:** list queue 64, gate 5 ms, per-frame record sizes (only 1 hit in the runs).
- [ ] **Logs on another partition** (idea, 2026-11-03): with sd2vita the game's ISO and the logs share the microSD; if log writes ever
      show up as PSP stalls (`frame_max` in `stats performance`), move them to `ur0:` or `uma0:`. Not needed so far: the race shows
      `frame_max` 17 ms with the extreme log.
- [ ] **Test for lag:** a capturer case that closes a frame with lists queued (`capture_test.c`); today only `test_link.py` (`resets`) covers it.
- [ ] **Ridge Racer, opening movie in HD (user, 0.0.14.4):** an error to look at later; dump 255 (`stage3_0.0.14.4/rr1/d255`) is a candidate, its Vita image differs from the normal one (a different moment, or a real error).
- [ ] **Ridge Racer selector, small flash at one angle of the last car** (user, 0.0.12.12): minor; a dump if it stays.
- [ ] **Ridge Racer selector, the last car showed the previous car's color for a moment** (user, 0.0.12.11): likely the texture cache
      checking 1 group in 4 per frame (accepted risk). Look again only if it causes bigger errors.
- [ ] **Ridge Racer selector, GPU ~31 ms per frame** (~4.2 M pixels, 1000 draws): the fast shader did not change it. Skip the image
      underneath when the frame covers it; look at the GPU clock Adrenaline sets.
- [ ] **Ridge Racer, odd drawings in the opening movie** (user, 0.0.12.10): looks minor. Dump 245 caught a clean frame: the movie is one
      1 MB texture in RAM. Suspected, not proven: the texture cache samples 1 group in 4 per frame since 0.0.12.8 and shows old rows of
      a texture rewritten every frame. Next: a dump on a bad frame, or try with `no_tex_cache`.
- [ ] **Ridge Racer race, VRAM texture cache** by tracking written pages (the record thread spends ~19 ms per frame).
- [ ] **Texture cache, what is left:** VRAM textures (KOF 96 fights: DMA ~1 ms + sort ~2 ms per frame; only after understanding the
      tiles: if they are stale data, a cache can make them worse); the check costs ~3.2 ms per frame (Metal Slug XX), see what
      can go; translator caches (palettes, translated draws, identical scenes; the translator spends ~8–10 ms of CPU per
      frame).
- [ ] **Street Fighter Alpha 3 MAX fights:** ~32 HD frames per second with the game at 60: intermediate image ~16 ms of GPU, surface
      ~4 ms, translator ~57 % of a core, ~700 draws per frame. Joining draws and translator caches would help. Windows of up to 316
      frames in 10 s fall to the normal image (`other_target`), and 7 with `bad_texture` (a user's "suspicious flash"?).
      One capturer `resets` in the 0.0.11.3 run, unexplained. Background seen moving wrong on 0.0.11.1: not seen since.
- [ ] KOF 96 fights: seconds at 42–57 fps with the GPU at 90–98 % and the capturer at ~11 ms; suspected GPU limit, not proven.
- [ ] **NPTEST on 0.0.8.32, real differences left:** `firstState` (alpha test set only in the first lists is lost), `vertFmts`
      (8-bit positions: the PSP does not draw the sprite where the translator does), `paletteRt`, `depthTex`. Animated
      scenes give false differences: the normal image of a dump is caught half drawn (suspected: NPTEST's flip), and
      `layers`, `rtOne` and `lateList` fail the comparator because of that.
- [ ] **KOF 96, tiles in the player select:** the user also sees them on 0.0.8.29, so they are not new (his report; dump 218 of
      0.0.8.29 caught a clean frame). `tools/dump.py` on the bad dump 217 (`stage3_0.0.8.32/kof_d217`) draws the same error on the
      PC: the Vita received wrong data. Clue: in 217 two textures come from mirror copy 419 while the list is from 632;
      in the clean 218 all come from 804. Suspected: a VRAM page used again after a while is read before the mirror
      refreshes it. Not proven. Next: a dev log of textures whose copy is far older than the frame's.
- [ ] **NPTEST, still open:** `stencilAlpha` and `blendDst` (stencil to alpha at the end of a scene), `blend2x`, `fixGray` and bit
      masks (shaders; no shacc in the container), `readBack` (read the surface mid-scene), `cpuPixels` and `noClear`
      (CPU writes and partial redraws), offsets into intermediate images (`draw()` takes the height from 0xD6, MINZ, not
      0xD5: fixing it needs them; `subRt`, `xferIntra`).
- [ ] **Synthetic battery NPTEST** (`docs/dev/en/nptest.md`): pending: false differences in animated scenes, image tolerance
      with real scenes, 3D scenes, useful GPU tests of pspautotests.
- [ ] **Simplify the structure** (after the tests): split `capture.c` (1621 lines) into walk, sublist memory, readers and
      logs; fewer switches; tidy the mailbox.
- [ ] **Giant draws in Metal Slug XX** (y -4127, target intermediate): 2 in the 0.0.8.24 run, none in 0.0.8.25 or 0.0.8.26.
      Dev builds log their bytes (`translator: giant draw bytes`): read them when they show up, before changing anything.
- [ ] Metal Slug XX: almost every checked frame comes out "different from the normal image" (KOF ~0) although it looks
      fine. Understand why (filter? one-frame delay at 30 fps?) before trusting `dump auto` with this game.

## Stage 3: drawing on the Vita
- [ ] **Put the black-screen safety back** (removed in 0.0.8.18 at the user's request: if something goes wrong, he
      closes the game). Until 0.0.8.17 it kept the normal image until Adrenaline closed when the surface came out black
      and the game's image did not; in KOF 96 it fired in fights because it read the surface while the GPU was still
      drawing it. When putting it back: only check surfaces the GPU has finished (like "covered") and do not turn
      drawing off forever.
- [ ] **PS button in KOF 96** (0.0.8.17): the dialog appeared behind the fight and over the background, and 1–2 s later
      on top of everything, **with NativeDraw not replacing the image**: it is not the surface drawing but something that
      keeps running (GPU timer with its notification 510 in Adrenaline's scenes, hooks, capturer). Next test: the same
      with `gpu_timer` set to `no`. `sceCommonDialogIsRunning` always says "no": useless for this. Postponed by the user.
      Earlier case (0.0.8.12): pressing PS to quit removed the background at the sides and the dialog did not appear; PS
      twice to the Vita menu and reopening Adrenaline fixed it. Low priority: solve it if it comes back.
- [ ] Translator, not drawn yet (frame goes with the normal image): lines and points, weights and morph, color test and logical
      operations; destination alpha blending (`blend`); joining draws with the same state.
- [ ] Frame record: copying from the PSP RAM costs ~6 µs per draw (KOF 96, ~2.4 ms
      per frame): make it cheaper if it matters; some KOF 96 frames hit the cap (`R_LIMIT`) in transitions.
- [ ] Possible bugs noted while cleaning comments (not touched; confirm before changing): `replay.c` `repLoad`
      goes on with garbage data when a texture is bigger than the buffer and does not free memory on failure;
      `capture.c` `markRt` does not guard `n == 0` (`want` does) and could mark all 512 VRAM pages; installer: `hasLine()`
      counts a commented-out line as enabled, "Uninstall" deletes the dumps without warning and "Clean test leftovers" reports
      files as deleted without checking. `dumpFrame`: f1 and f2 of each dump come out without `.bin` (confirmed in 0.0.8.20).
- [ ] A fixed dotted line over the date of Adrenaline's menu with the presenter (menu only). Check whether it appears
      with `draw` set to `no`.
- [ ] Run **Ridge Racer** and the other installed games with NativeDraw 0.0.2 or later (only GT was retested after the
      rename; the capture did not change).
- [ ] **Options in the L+R+START menu** (the menu exists since NativeDraw 0.0.4): the ones that are files in
      `ux0:data/nativedraw/` today (`docs/dev/en/logs-and-switches.md`), including the dump level, so a user can turn on
      automatic dumps, play and hand us the `dumps/` folder. Decide which are worth it; the ones that only apply when
      Adrenaline opens (`capture_core`, `off`) must say so. Save in `ux0:data/nativedraw/options.txt`.
- [ ] `nativedraw/capture_test.c` does not simulate the Vita's notice to the PSP (the mirror copy the Vita announces
      in `SB[0x48]`).
- [ ] **Updater** in the installer (future): look in GitHub releases for a new VPK and download it.
- [ ] `tools/windows/install.sh`: under `curl | bash` (`install.cmd` without a clone) `read` may consume the script from stdin
      (untested).
- [ ] **Before 1.0:** remove "Clean test leftovers" from the installer.
- [ ] **Before the first release:** remove `docs/changelog/*/conversaciones/` (`CHANGELOG.md` stays as the summary) and
      make the GitHub release (VPK + loose plugins) with tags `nativelink-X.Y.Z` / `nativedraw-X.Y.Z`.
- [ ] `tools/windows/install.cmd` / `install.sh` use the `dev` branch while the default branch is `master`; move
      `dev` to `master` when the first release is made.

## Capture performance (stage 2 closed with ≥ 58 fps in the heaviest part of GT)
- [ ] **Read the PSP RAM more cheaply** (now planned as the photo at the sync, above). It limits the sublist
      check: ~2 µs per line for the capturer and the readers; the capturer still compares ~5.5 ms per frame in the
      heaviest part of GT. Ideas: a cached view or DMA (KBENCH: 1.7–4 times faster on big blocks). Measure first.
      (`docs/changelog/en/etapa2_fase2_adelanto.md`, `etapa2_fase1_impacto.md`)
- [ ] Applying a sublist still copies its registers (up to 64) and matrices: measure what is left with the extreme log
      (`apply_us`) before touching it.
- [ ] A `no_capture` run in GT to know whether the slow frames at the end of the lap belong to the game alone.
- [ ] Interpretation loop: `l->n`, `acc.cmds`, `f_cmds`, `l->pc`, `l->prev` and `l->hi` are still counted per command;
      they could be added once per line (watch the `return`s inside the `switch`). The `draw()` statistics (batches,
      state changes) could move to the extreme log now that stage 2 is over.
- [ ] Compare straight from the shared cache without copying the line to `R0.buf` (small gain). (`etapa2_espejo_vram.md`)
- [ ] Small idea if it matters again: fetch the shared-cache line with NEON and fewer barriers in `clGet`.
      (`etapa2_fase2_estadisticas.md`)
- [ ] `memoVerify` compares every version (up to 8) of a sublist even when they have the same lines: compare once per
      range. In GT there is almost always a single version, so it is not noticeable. (`etapa2_comparar_menos.md`)
- [ ] Many sublists entering with another state (`other_entry`) are walked again: find a way to reuse them.
- [ ] Since v13.12 a line of a VRAM sublist with no valid copy in the frame's mirror is re-recorded instead of compared
      with the old copy: if other games show many "changed" in VRAM sublists, look at this.
- [ ] `run_summary.py` still does not show the gate lines ("stats gate" in `nativelink.log`) nor the look-ahead
      line (`lookahead/f` in `nativedraw_capture.log`); today they are read with grep.

## Other games (after stage 2)
- [ ] Test NativeDraw on the user's other installed games and on Silent Hill (and LocoRoco with the free core): the gate
      before enqueue, the look-ahead check, VRAM sublists and the capturer on any core were only seen in GT, RR and
      LocoRoco.
- [ ] Ridge Racer rewrites 16 lines of its list before enqueuing the next one, so the gate stays at the sync (less margin
      for the Vita). See what changes: if it is something the Vita already read, the gate before enqueue could be used.
      It will matter when the Vita also draws RR.
- [ ] **Vita hang** in the first GT run with v13.11.1 (~10 s after the start). It did not happen again. If it does, note
      the version and the moment.

## Performance for drawing GT (from `docs/changelog/en/etapa2_nucleos.md`)
- [ ] **Record each stable sublist once and reuse the recorded GXM list.** Recording draw by draw does not fit for GT
      even on 3 cores (~24 ms of CPU and ~16 ms of GPU for 9000 draws; ~1.5 core free). In GT ~85 % of the commands come
      from sublists the capture already recognizes as identical. To solve: the input state the sublist inherits
      (`other_entry`). Probably moves from stage 6 to stage 3/4 for GT.
- [ ] **Join draws** with the same state (stage 1: 2000 squares in 1 draw = 0.02 µs per square, half the GPU).
- [ ] The translator/recorder must run on core 0 (and what is left of core 2): core 1 belongs to the capture (~10–15 %
      free).
- [ ] **End of the GT lap at 40–48 fps** (last ~7 s): ~300 RECTANGLES per frame appear and ~50 sublists per frame enter
      with another state; the sublist memory skips 70 % instead of 85 % and the capture rises to 20–25 ms per frame. See
      which screen it is (the finish?) and whether the memory can keep several entries per sublist.

## VRAM mirror
- [ ] Register the mirrors (MIRROR 0x0A200000 and MIRROR2 0x0AC00000, 2 MB each) with `sceGxmMapMemory` so the Vita's
      GPU can read them while drawing. (`etapa2_espejo_doble.md`)
- [ ] The Vita will have to repeat the block copies (TRANSFER) to VRAM and keep the result: since v13.6 those
      destinations are not requested from the mirror (in GT, almost all the textures that change; in SH, the copy of its
      image). (`etapa2_espejo_doble.md`)
- [ ] Find the physical address of the VRAM from the Vita's kernel, to read it without a mirror. Risky and low priority.
      (`docs/dev/en/architecture-and-roadmap.md` §7)

## Telemetry (T4/T6)
- [ ] **T4: the DMA functions were not found** (`sceDmacMemcpy`/`sceDmacTryMemcpy`, library `sceDmac`) under any of the
      module names tried: the right name in Adrenaline 8 is missing. GT and SH could be measured without it.
      (`etapa2_t4_t6_vida_datos.md`)
- [ ] T6: "GE busy when called" at the sync is always 0, although the game waits ~0.7 ms (GT) or ~3.7 ms (SH) inside
      `sceGeDrawSync`. Understand what it waits for there (the end of the list with the GE already stopped, or the
      callbacks?).
- [ ] T6 is off by default (it took ~2–3 fps from GT): enable with `ux0:data/nativedraw/ge_timing`. To leave it always
      on, sample every ~1 ms or from somewhere else.

## Unreliable measurements
- [ ] Core 0 of the `processor` log line comes out almost empty: the CreateThread hook does not see the threads
      Adrenaline created before loading NativeDraw. (`docs/changelog/en/etapa2_memoria_sublistas.md`)
- [ ] Approximate sublist-memory statistics: "changes" and batches of the first draw of an applied sublist depend on the
      input state; `chg[]` does not add the applied sublists. (`etapa2_memoria_sublistas.md`)
- [ ] vpjuego: "GE in stall" always reads 0 %. For GE timings use the enqueue and FINISH times (T6).
      (`docs/dev/en/architecture-and-roadmap.md` §7)
- [ ] Reading the clock on the Vita costs 390 ns: the full extreme log alters what it measures.

## Low priority
- [ ] **PSP menu (XMB) in HD through NativeDraw** (user, 2026-11-04): it looks possible, not tried. The menu draws with the GE like a game, but NativeLink only loads in games (`game,` line in
      `plugins.txt`): it needs a `vsh,` line and checking that the mailbox, the hooks and the capturer work there. First see whether the menu's waves and icons go through the paths the translator supports.
- [ ] **Wi-Fi drops around PSP games:** the relay lost the Vita at both Metal Slug XX runs on 2026-11-02 (2 cases only). The
      plugins have no network code; Adrenaline has `kermit_wlan` and a per-game "wlan switch" setting. Test: Metal Slug with L held
      when opening (NativeLink off), and KOF 96.
- [ ] **Battery, heat and Vita memory**: measure them later (a 30-minute session, same game, with and without the plugin,
      or log the battery percent). The optimizations overlap with the caches, not redrawing identical frames and waking
      threads only when there is work.
- [ ] **PS1 games in high resolution** (user's request, 2026-10-30). Today NativeDraw does not touch them: in
      Adrenaline 8 the PS1 image comes from Sony's PS1 emulator built into the Vita (`pops_tex`, `user/menu.c`), not from
      the PSP's graphics chip, so NativeLink has nothing to capture; Adrenaline only stretches and filters it. Possible in
      theory, but almost another project: find where that closed-source emulator draws the PS1 GPU commands, hook it and
      write a new translator for that GPU. VitaGrafix does not help: it raises the resolution of native Vita games, one by
      one. [verify] with Castlevania SOTN (PS1): with NativeDraw active the capture log must show 0 lists and 0 draws and
      the game must look the same as without the plugin (a control test, not a scaling test).
- [ ] The cause of the Vita switching off with the stage 1 bench v11.3 and GePatch active at 2x is still unconfirmed
      (GePatch's scaling no longer exists; be careful with new GPU work anyway). (`docs/changelog/en/etapa1_banco_gxm.md`)
- [ ] Medium term: propose to Adrenaline an official hook point for an "external presenter", so as not to depend on
      hooking its drawing thread. (`docs/dev/en/architecture-and-roadmap.md` §8)
- [ ] Vita recovery (the user noticed it "slightly affected" on 2026-10-29; it hangs now and then since the tests that
      moved memory): steps already given (full power off → safe mode → rebuild database → reinstall Adrenaline; never
      update the firmware). When the user wants.
- [ ] `tools/build.sh` and the documents mention the Windows PC paths (`C:\Users\spect`); if the user changes
      machines, update `docs/agent/en/workflow.md`.

## Cleanup
- [ ] Delete the remote branch `prueba-0.0.8.13` by hand (the session proxy refused it).
- [ ] Delete by hand on the Windows PC: the empty folders `C:\Users\spect\GePatch` and `Documents\ambientepsp`, and
      `Documents\GitHub\gepatch-lab` (scripts and memstick of the old lab). In WSL, `~/gepatch-lab` (PPSSPP build) and
      `~/pspdev` (replaced by `~/tc` from `tools/setup.sh`). On the Mac, `~/Documents/gepatch-lab` and the backup
      `~/Documents/GitHub/_respaldo_mac_nativedraw_0.0.7.2/` once it is clear nothing is needed from it.
- [ ] The Windows relay folder (`Descargas\vitabridge`) still has the GePatch-era folders, patches and git bundles:
      delete them as was done on the Mac on 2026-10-31.
- [ ] `ambientes_de_prueba.md` and `errores_conocidos.md` (claude.ai project) describe GePatch with architecture A.
      When the Vita draws 3D (stage 4), check which image errors disappear with B (table in
      `docs/dev/en/architecture-and-roadmap.md` §3.7) and archive them.
