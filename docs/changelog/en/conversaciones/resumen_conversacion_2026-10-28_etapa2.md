# Summary to continue in another conversation: stage 2 of architecture B (2026-10-28)

Read in this order: this summary → `etapa2_avance.md` (all the technical detail and the numbers) → `etapa2_inicio.md`
(stage plan) → `arquitectura_dibujo_en_vita.md` (§3 design, §5 budget, §6 stages).

## Where we left off
- **Commit `81d835b` on the `dev` branch** (Windows repo `Documents\GitHub\NativePatch`, **no push**): GePatch
  **0.2-dev61** + gepresent **v12.9** + `docs/etapa2_avance.md`.
- On the Vita: GePatch dev61 (md5 `48e6167d…`), gepresent v12.9 (md5 `ee1373e1…`), ge_patch **on** in `plugins.txt`.
- **Shadow mode is enabled by default in all games** (user request): the game runs as if there were no plugin and the
  Vita follows its lists. To go back to the old GePatch in a game: L+R+START during that game (takes effect the next
  time it is opened).
- Result of the last test (v12.9, GT, one full lap): audio fine, no graphical errors, almost the whole track smooth;
  **the bridge at the end drops to ~36–40 fps** because the capture takes ~30 ms per frame there (~67,000 commands,
  ~13,800 draws). SH runs at its original speed.

## In simple terms (what each piece does)
- **GePatch (PSP):** no longer enlarges the image; it writes down in a notebook what the game asks its painter (GE) to
  do and, when the game waits for the painter, it waits up to 5 ms for the Vita to finish reading.
- **gepresent (Vita):** a main thread (core 1) reads and walks through those instructions; two helpers (cores 0 and 2,
  minimum priority) read ahead and leave the data ready for it in fast memory. It does not draw anything yet: it
  measures how much work there would be and writes `ux0:pspemu/ge_patch_captura.log` every second.

## Lessons from this conversation (do not repeat)
- The PSP RAM is in 3 blocks of the Vita (0x70000000, 0x72000000, 0x73000000).
- SH asks for the VRAM before the disc is mounted: ID from `sceKernelGetGameInfo` (+0x44); the options file cannot be
  opened at that moment either.
- Reading the PSP RAM from the ARM: DMA in small windows is very expensive (~200 µs per copy); word by word ~1.5 µs per
  command; with NEON in 64/256-byte chunks ~350 ns. The cost is the wait on each read.
- **Emulator threads:** core 1 = audio (0x40–0x43), core 2 = Media Engine (0x60, decodes the game's audio), core 0 =
  ScePspemuMain (0x61). A thread that polls without pause above the Media Engine **cuts the audio**; with low priority
  it gets little core time. Chosen capturer: core 1, priority 0x61. Helpers: 0xB0 (below AdrenalineDraw 0xA0, which
  has no fixed affinity).
- In GT, what prevents merging draws is almost only **0xC8 TEXLEVEL** (~8,200 changes per frame).
- GT has almost all of its textures, palettes and ~100 sublists per frame in the VRAM: the mirror is mandatory.

## Proposed next steps (the user approved continuing in this order)
1. **Measure how much core time the capturer really gets** (`sceKernelGetThreadInfo` → runClocks per second): ~440 ns
   per command with 93 % of the lines already in the shared cache is a lot; separate "shared core" from "work".
2. **Sublist memory:** GT repeats almost the same sublists every frame; fast hash and reuse what has already been
   walked. This is what should pay off most on the bridge.
3. Count draw batches without breaking on TEXLEVEL.
4. After that: VRAM mirror (GT, RR), black box (T2), GE timings (T6), GXM recording on several threads.

## Working rules in force (from the user)
- Test versions `.x` (next: GePatch dev62.x if the PSP side changes; gepresent v13.x or v12.10 depending on the
  change); commit only with the confirmed version; build and copy to Downloads, to the lab and to the Vita (via
  `vitabridge`, verify with md5 by downloading it again) after every change; close Adrenaline with `cmd_cerrar.txt`
  (`destroy`) so that it loads a new gepresent.
- When asking for tests: which game, what to do and what to look at; name options as in the menu.
- Save the logs of each run in `vitabridge/<etapa>_<versión>/` (they are replaced when the game is opened again).
- Simple summary after every change, for someone without technical knowledge.
- Tools: build in the assistant's container (pspdev: GitHub tarball + Adrenaline clone; Vita: recipe from
  `etapa1_banco_gxm.md` §5); the repo is edited on the PC (device_bash) and passed to the container with stage/commit;
  git on the PC needs delete permission in `Documents\GitHub`.
