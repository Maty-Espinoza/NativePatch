# Summary to continue in another conversation: stage 2, VRAM mirror (2026-10-28, third conversation)

Read in this order: this summary → `etapa2_espejo_vram.md` (design, numbers, pending items) → `etapa2_memoria_sublistas.md`
→ `etapa2_avance.md` → `arquitectura_dibujo_en_vita.md` (§3.5 VRAM, §6 stages). The previous one:
`resumen_conversacion_2026-10-28_etapa2b.md`.

## Where we left off
- **Commit `054f7e1` on `dev`** (Windows repo `Documents\GitHub\NativePatch`, **no push**): GePatch **0.2-dev62**
  (`main.c`, "Espejo de la VRAM") + gepresent **v13.3** (`tools/gepresent/captura.c`, `gepresent.c`, `prueba_memoria.c`)
  + `docs/etapa2_espejo_vram.md`.
- On the Vita: GePatch dev62 (md5 `d44ba997…`, verified by downloading it), gepresent v13.3 (`282b3a74…`, verified).
  Shadow mode by default. No gepresent options files.
- Binaries: `vitabridge/respaldo/ge_patch_dev62.1.prx`, `ge_patch_dev62.prx`; `vitabridge/gepresent/gepresent_v13.{1,2,3}.suprx`.
  Patch: `vitabridge/gepatch_dev62_gepresent_v13.3.patch`. Logs: `vitabridge/etapa2_v13.{1,2,3}/gt1/`, `etapa2_v13.3/sh1/`.
- **Result:** the Vita reads GT in full (100–140 sublists per frame from the mirror, none skipped, 0 DISTINTAS).
  GT 59.4–59.9 fps over almost the whole lap, race start 56–58, bridge ~52. SH the same as before. Audio and image fine.

## In simple terms (what each piece does)
- **GePatch (PSP):** the Vita asks it for 4 KB chunks of the video memory; at every frame change the PSP's painter
  copies them to normal memory (the "mirror"): instructions on every frame, textures the first time and then little by
  little.
- **gepresent (Vita):** reads the instructions that are in the video memory from the mirror; a low-priority thread
  measures how much the mirror changes. The sublist memory grew 4 times (GT now calls ~250 per frame).

## Lessons (do not repeat)
- The mirror copy is cheap (the game waits for the GE 0.1–0.3 ms per sync): what lowered the fps in v13.1 was the full
  sublist memory (it was emptied 4–5 times per second).
- Do not let something necessary depend on a priority 0xB0 thread: sometimes it gets no time (v13.2 kept skipping
  sublists because only that thread marked pages as valid).
- Comparing the lines of the saved sublists costs ~2 µs per line: ~9 of the ~18 ms of the bridge.
- The mirror list cannot go at RENDER_LIST + 0x8000 (TX_RING): it goes at 0x0A100000.
- GT changes textures in the VRAM during the race (100–230 pages per sec); SH asks for 1 MB of pages that change on
  every copy (render targets written with TRANSFER).
- The PSP tests (`tests/`) do not build since dev61 (`asm("sync")`). In the container, `gcc-multilib` uninstalls
  `gcc-arm-linux-gnueabihf` (the qemu test); do not install both at the same time.
- If Adrenaline is not closed between games, the logs join the sessions (it happened with SH).

## Proposed next steps
1. Compare less in the sublist memory (readers that verify ahead of time, compare from the shared cache): this is what
   is missing for the GT bridge to reach 60.
2. SH: mark TRANSFER targets and stop asking for pages that always change.
3. After that: T2 (black box), T4, T6, GXM recording on several threads → stage 3 (draw).

## Working rules in force (from the user)
- Test versions `.x` (next: gepresent v13.4 / GePatch dev63.x); commit only with the confirmed version.
- Build in the container (pspdev + vita-toolchain, recipe `etapa1_banco_gxm.md` §5) and copy to Downloads,
  `vitabridge\enviar` and `gepatch-lab\memstick\seplugins`; verify with md5 by downloading it from the Vita; close
  Adrenaline with `cmd_cerrar.txt` (`destroy`) to load a new gepresent.
- The user asked to do the commit and the review from his Windows PC: git in `Documents\GitHub` (delete permission
  approved in that session), changes passed as a patch (`git apply`).
- When asking for tests: which game, what to do and what to look at; options with the menu name. Simple summary after
  every change. Logs of each run in `vitabridge/<etapa>_<versión>/<juego><n>/`.
