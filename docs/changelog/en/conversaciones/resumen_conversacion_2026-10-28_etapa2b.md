# Summary to continue in another conversation: stage 2, sublist memory (2026-10-28, second conversation)

Read in this order: this summary → `etapa2_memoria_sublistas.md` (design, numbers, pending items) →
`etapa2_diagnostico_v12.11.md` (why the memory) → `etapa2_avance.md` (shadow mode and capturer) →
`arquitectura_dibujo_en_vita.md` (§3 design, §5 budget, §6 stages). The previous one: `resumen_conversacion_2026-10-28_etapa2.md`.

## Where we left off
- **Commit `77908df` on `dev`** (Windows repo `Documents\GitHub\NativePatch`, **no push**): gepresent **v12.13**
  (`tools/gepresent/captura.c`, `gepresent.c`, `prueba_memoria.c`) + `docs/etapa2_memoria_sublistas.md`. GePatch is still
  **0.2-dev61** (commit `81d835b`).
- On the Vita: GePatch dev61 (shadow mode by default in all games), gepresent v12.13 (md5 `d4141c1f…`, verified by
  downloading it). No gepresent options files (`memo_revisar`, `sin_memoria`, `captura_sin_lectores`).
- **Result:** GT at 56–60 fps over the whole lap, including the bridge (before 36–40), audio and image fine (user). The
  memory skips ~78 % of the commands in a race; 0 errors in ~28,700 sublists checked. The image is still the native
  one: the Vita does not draw yet.

## In simple terms (what each piece does)
- **GePatch (PSP), shadow mode:** the game runs as if there were no plugin; it writes into a mailbox what it asks its
  painter (GE) to do and, when the game waits for the painter, it waits up to 5 ms for the Vita to finish reading.
- **gepresent (Vita):** one thread (core 1) reads and walks through those instructions; two helpers (cores 0 and 2)
  bring the memory in ahead of time. **New:** it remembers the sublists it has already walked and, if they did not
  change, uses what was saved instead of walking them. 1 out of every 32 is walked anyway to check that what was saved
  is exact.

## Lessons from this conversation (do not repeat)
- The capturer's cost was **interpreting** (~360 ns of processor per command, ~60 Thumb instructions), not waiting for
  the PSP RAM (a second pass with everything in cache cost almost the same). Sharing core 1 with the audio explains only
  ~20 %.
- GT repeats ~88 % of its commands in identical sublists (fixed geometry; matrices and addresses go in the list above).
- A sublist depends on state set by the list above it (vertex type, texture, render target, input vertices): this has to
  be checked in addition to its memory; the same sublist can be called with different states (several versions).
- `now()` (sceKernelGetProcessTimeLow) costs ~390 ns: do not time per command.
- When adding up saved counters, do not add the ones the thread counts (frames, times, events) and correct the base if
  the recording crosses the change of second (bug in v12.12, fixed in v12.13).
- **Test in the container before the Vita:** `prueba_memoria.c` runs `captura.c` on ARM under qemu with a fake PSP RAM
  (`apt-get install gcc-arm-linux-gnueabihf libc6-dev-armhf-cross qemu-user-static`, build with `-static`). It found a
  latent hang (`texAdd` with `gen = 0`).
- If Adrenaline is not closed between games, `ge_patch_captura.log` joins the two sessions (look for "buzon ... encontrado").

## Proposed next steps
1. **VRAM mirror** (GT, RR): GT has 80–140 sublists per frame in the VRAM (skipped today), ~95 % of textures and ~80 %
   of palettes there. Without the mirror the GT capture is incomplete (there will be more commands; the memory should
   absorb part of them). This is the next step of §6 of the architecture.
2. Test SH (and RR) with v12.13 to confirm that nothing got worse.
3. Optional: shorter interpretation loop; readers should not walk what the memory skips.
4. After that: T2 (black box), T6, GXM recording on several threads with fixed affinity → stage 3 (draw).

## Working rules in force (from the user)
- Test versions `.x` (next: gepresent v12.14 / v13.x depending on the change; GePatch dev62.x if the PSP side changes);
  commit only with the confirmed version and not for every iteration; build and copy to Downloads/`vitabridge`, verify
  with md5 by downloading it from the Vita; close Adrenaline with `cmd_cerrar.txt` (`destroy`) so that it loads a new
  gepresent.
- When asking for tests: which game, what to do and what to look at; options with the menu name.
- Save the logs of each run in `vitabridge/<etapa>_<versión>/<juego><n>/` (with `bajar_*.txt` to the relay).
- Simple summary after every change, for someone without technical knowledge.
- Tools: Vita in the container (recipe `etapa1_banco_gxm.md` §5, ~5 min); the repo is edited on the PC (device_bash)
  and passed with stage/commit; git on the PC needs delete permission in `Documents\GitHub`.
