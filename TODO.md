# TODO: pending items

The only list of pending work. Ordered from what comes next to what can wait. Each item says where it came from
(a document in `docs/dev/en/`, `docs/agent/en/`, `docs/changelog/en/` or the claude.ai project). Done items are removed;
what was done is in `CHANGELOG.md`. Updated 2026-11-02 (NativeLink 0.0.3 + NativeDraw 0.0.7 confirmed; tests up to NativeDraw 0.0.8.29 + NativeLink 0.0.3.11). How the work is done: `docs/agent/en/workflow.md`; stage 3 plan and results:
`docs/agent/en/stage3-plan.md`.

## Next
- [ ] **Triangle fans (prim 5) in the translator:** Street Fighter Alpha 3 MAX fights fall to the normal image with reason
      `primitive`. With them it is the 2D benchmark at 60 fps (~600 draws per frame).
- [ ] KOF 96 fights: seconds at 42–57 fps with the GPU at 90–98 % and the capturer at ~11 ms; suspected GPU limit, not proven.
- [ ] **NPTEST, still open:** `stencilAlpha` and `blendDst` (stencil to alpha at the end of a scene), `blend2x`, `fixGray` and bit
      masks (shaders; no shacc in the container), `readBack` (read the surface mid-scene), `cpuPixels` and `noClear`
      (CPU writes and partial redraws), offsets into intermediate images (`draw()` takes the height from 0xD6, MINZ, not
      0xD5: fixing it needs them; `subRt`, `xferIntra`).
- [ ] **Synthetic battery NPTEST** (`docs/dev/en/nptest.md`, standardization goes before the texture cache): 108 2D scenes
      in `tests/nptest/`, checked on PPSSPP. Pending: first run on the Vita (does NativeDraw capture a homebrew?); the reason
      of each scene is logged (`translator: result at frame`), the report script is missing; comparing images per scene (image tolerance, open); a cache scene
      check; 3D scenes; useful GPU tests of pspautotests.
- [ ] **Simplify the structure** (after the tests): split `capture.c` (1621 lines) into walk, sublist memory, readers and
      logs; fewer switches; tidy the mailbox.
- [ ] **Texture cache between frames** (user's request: caches everywhere, avoid redoing what did not change). Measured
      in 0.0.8.19: ~98–99 % of the bytes copied each frame equal the previous copy; the DMA is the expensive part
      (~98 µs per call; since 0.0.8.26 only copies of 8 KB or more use it). Agreed design: textures kept unswizzled in the Vita's own
      memory across frames; a cheap sampled check every frame, a full check in turns (1 of every N groups of 8 rows per
      frame), immediate invalidation when the GE writes there (block copies, intermediate images); no 128-textures-per-frame
      cap. Accepted risk: a texture changed only in bytes the sample misses may look stale for up to N frames. Take care
      that the GPU never reads an entry while it is rewritten. Afterwards: translator caches (palettes, translated draws,
      identical scenes; the translator spends ~8–10 ms of CPU per frame).
- [ ] **Giant draws in Metal Slug XX** (y -4127, target intermediate): 2 in the 0.0.8.24 run, none in 0.0.8.25 or 0.0.8.26.
      Dev builds log their bytes (`translator: giant draw bytes`): read them when they show up, before changing anything.
- [ ] Metal Slug XX: almost every checked frame comes out "different from the normal image" (KOF ~0) although it looks
      fine. Understand why (filter? one-frame delay at 30 fps?) before trusting `dump auto` with this game.
- [ ] When the performance work is confirmed, the commit is **NativeDraw 0.0.9** (user's decision): update
      `docs/dev/*/versions.md` (history row), `CHANGELOG.md` and `docs/agent/en/stage3-plan.md`.

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
- [ ] Translator, not drawn yet (frame goes with the normal image): 3D (non-through mode), depth and stencil, color test
      and logical operations; blending with destination alpha or doubled factors (`blend`); scissor clipping of triangles
      and strips (only rectangles today); an intermediate image drawn in one frame and used as a texture in another;
      repeating the GE's block copies (TRANSFER) to VRAM; joining draws with the same state (fewer GPU calls).
- [ ] Frame record: store the matrices (needed for 3D); copying from the PSP RAM costs ~6 µs per draw (KOF 96, ~2.4 ms
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
- [ ] **Read the PSP RAM more cheaply** (postponed 2026-10-29: 58 fps minimum is enough for now). It limits the sublist
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
