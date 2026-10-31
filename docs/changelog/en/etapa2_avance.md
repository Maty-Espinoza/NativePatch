# Stage 2 of architecture B: shadow mode and capturer (2026-10-28)

Read first `etapa2_inicio.md` (plan of the stage) and `arquitectura_dibujo_en_vita.md`. Marks: [measured], [estimated],
[deduced], [verify]. Commit: **GePatch 0.2-dev61 + gepresent v12.9** (tests dev61.1–dev61.3, v12.1–v12.9).

## In simple terms
- **Shadow mode (GePatch, PSP side):** the game runs as if without the plugin (real VRAM, Adrenaline's native image, its lists are not
  walked or touched). The enqueue, stall, sync and flip hooks only write to a mailbox. **It is the default mode
  since dev61.3** (the user's decision: it makes no sense to test the old GePatch now). L+R+START during
  shadow mode turns it off for the next time that game is opened (saves `sombra=0` in `ms0:/gepatch_<ID>.txt`).
- **Capturer (gepresent, Vita side):** a thread reads the mailbox and walks the same lists from the PSP RAM, the way the
  GE would, without drawing. Every second it writes statistics to `ux0:pspemu/ge_patch_captura.log` (the previous session in
  `ge_patch_captura_prev.log`; the relay downloads them by itself).
- **Gate at the sync:** when the game waits for the GE (`sceGeDrawSync(0)` / `sceGeListSync(qid, 0)`), GePatch waits
  up to 5 ms for the Vita to have captured the lists the game is waiting for. If the capture does not keep up, the game drops fps
  (that is what is seen in GT in the heavy parts).
- **Result:** SH runs at its original speed with the capture up to date (the flashlight is visible). GT: menus and most of the
  track at 55–60 fps; the bridge at the end drops to ~36–40 fps because the capture takes ~30 ms per frame. Audio fine, no
  graphical errors (the image is the native one).

## Components
### GePatch dev61 (main.c, "Shadow mode")
- Mailbox at 0x0A000000 (the fake VRAM, unused in shadow mode), uncached: `+0 "SOMB"`, `+4` version, `+8` events written
  (PSP), `+0xC` last captured list (Vita, order number), `+0x10` Vita heartbeat, `+0x14` events read (Vita),
  `+0x18` lists enqueued (PSP). Ring of 4096 events of 32 bytes at `+0x1000`: `tipo | dato<<8, a, b, c, hora PSP,
  orden`. Types: 1 enqueue (list, stall, qid, order), 2 enqueue at the front, 3 stall (qid, stall), 4 drawsync (wait,
  captured, order), 5 listsync, 6 flip (fb, width|format<<16, frame).
- `modeDecide()`: first hook the game calls (asking for the VRAM, enqueueing or flipping). It reads the game's ID and profile; if
  `opt.sombra`, it switches to `sombra = passive = 1`, `DRAW_NATIVE = 0` (Adrenaline shows the native image) and sets up the mailbox.
  `sceGeEdramGetAddr/Size` return the real VRAM in shadow mode. On every flip the "SOMB" and "GPV1" marks are restored (the log and the
  dumps are still written by gepresent).
- **Game ID:** SH asks for the VRAM before mounting the disc (`disc0:/UMD_DATA.BIN` does not exist yet): it is taken from
  `sceKernelGetGameInfo` (SysMemForKernel 0xEF29061C, +0x44 "ULUS10450", the same one Adrenaline uses). At that moment
  `ms0:/gepatch_<ID>.txt` cannot be opened either (that is why shadow mode became the default). [measured]
- New hooks: `sceGeDrawSync` (0xB287BD61) and `sceGeListSync` (0x03444EB4) in sceGe_driver; outside shadow mode they just pass through.
- Log every 600 frames: `stats sombra` (events, lists, whether the Vita responds, captured), `stats sombra sync` (syncs that
  waited, how many reached 5 ms, average/max wait) and `stats sombra GE` (what the game waits on the GE in drawsync).

### gepresent v12.9 (tools/gepresent/captura.c)
- It starts when the PSP RAM is mapped; it activates on seeing "SOMB". The PSP RAM is in **3 blocks of the Vita**:
  0x70000000 (PSP 0x08000000–0x0A000000), 0x72000000 and 0x73000000 [measured]; v12.1 looked at only one and did not start.
- It follows the events and walks like the GE (one list at a time, up to its stall; JUMP/CALL/RET, BASE, OFFSET, ORIGIN,
  END after SIGNAL), without drawing. Per draw it counts: type, 3D/2D/clear, real state changes (texture, matrix, target,
  "other"), mergeable batches, distinct textures (RAM/VRAM), palettes, copies, vertices. What is in the PSP's VRAM cannot be
  read: a CALL into the VRAM is counted and skipped; a JUMP into the VRAM loses the list. Cap of 400,000 commands per list.
- **Reading** (almost all the cost is waiting for the PSP RAM from the ARM):
  | Form | Cost in GT | Version |
  |---|---|---|
  | DMA in 2 KB windows | ~200 µs per copy, ~24 commands per window → ~10 µs/command | v12.2 |
  | word by word (uncached view) | ~1.5 µs/command | v12.3 |
  | 64 bytes with NEON (vld1) | ~350 ns/command with the core to itself | v12.4 |
  | 256 bytes (8 NEON loads before storing) + 2 look-ahead readers | ~440 ns/command on core 1 | v12.9 |
- **Readers (v12.9):** two threads (cores 0 and 2, priority 0xB0: only the leftover time, below AdrenalineDraw
  0xA0) walk the list the capturer publishes (without interpreting draws) and split the sublists of the upper
  level among them. They leave 64-byte lines in a shared cache (8192 lines, 512 KB, tag = line + order number of the
  list; written with CAS and re-reading the tag). What lies between the stall and 256 KB after it is not shared
  (it may not be written yet). In GT the capturer takes **~93 % of the readers' lines** [measured].
  `ux0:data/gepresent/captura_sin_lectores` turns them off.
- **Emulator threads** (ScePspemu, measured with gepresent v12.6):
  - core 0 (0x10000): ScePspemuMain 0x61 and remote services 0x5d–0x63;
  - core 1 (0x20000): audio: RemoteAudioOutOp 0x40, AudioCh0/1Buffer 0x43, AudioOutCh0/1Intr 0x41, Mixer 0x41;
  - core 2 (0x40000): **ScePspemuRemoteMe 0x60 (Media Engine: decodes the game's audio)**, TitleInfo 0x60,
    AvcAuInput 0x60, ScePopsDisplay 0x7d;
  - Adrenaline: AdrenalineDraw 0xA0 and AdrenalineCompat 0x60 without affinity.
- **Where the capturer runs** (core / priority): 2/0x40 → cuts the audio (covers the Media Engine); 2/0xA0 → audio
  fine but ~1000 ns/command; 2/0x40 with pauses → worst of all; 2/0x61 → ~900 ns in a race (the Media Engine uses more than
  half the core); **1/0x61 → ~510 ns, audio fine (chosen)**. Rule: a thread that polls without pausing cannot be
  above the Media Engine on core 2 nor above the audio threads on core 1.
- `banco.c` (stage 1) is still inside, but it is activated with `ux0:data/gepresent/banco_v12` (the old `banco` file is still on
  the Vita and v12 ignores it).

## Numbers [measured]
- **SH (ULUS-10450):** 1 list per frame; the capture up to date almost always (few waits, < 4 ms); fps = the original ones.
- **GT (UCUS-98632), 1 list + 2 drawsyncs + 1 listsync per frame:**
  - track: 23,000–42,000 commands and 5,000–10,000 draws per frame → 10–18 ms of capture (v12.9);
  - bridge at the end: ~67,000 commands and ~13,800 draws per frame → ~30 ms (36–40 fps);
  - almost all 3D triangle strips; vertices in RAM (0.5–0.9 MB per frame, none in the VRAM);
  - ~95 % of the draws with a texture in the VRAM; 120–230 distinct textures per frame; ~1000 palette loads per frame
    (~80 % from the VRAM); 80–140 sublists in the VRAM per frame (skipped) → **the VRAM mirror is mandatory for GT**;
  - **merging draws:** with all the registers, ~6,450 batches out of 11,800 draws (~45 % fewer). What breaks the batches is almost
    only **0xC8 TEXLEVEL** (~8,200 changes per frame); the rest of "other" is small (0x22 alpha blend ~130, 0x5C ~70,
    0xC7 ~70, 0xC6 ~65, 0xCF ~55, 0xDB ~50, 0xE7 ~50). If TEXLEVEL goes per draw (attribute or constant) without breaking the
    batch, merging would yield much more [deduced; measure in the next version].
- The ARM at low priority on a busy core receives only part of the time; the ns/command include that.

## Next steps
1. Measure the CPU time the capturer really receives (`sceKernelGetThreadInfo`: runClocks) to separate "shared
   core" from "work per command" (~440 ns per command with 93 % of the lines already in the cache is a lot).
2. **Sublist memory:** GT repeats almost the same sublists every frame; fast hash + reuse what was already
   walked (and later what was already translated). It is what should yield the most on the bridge.
3. Count batches without breaking at TEXLEVEL.
4. VRAM mirror (GT and RR), T2 (black box), T4, T6, GXM recording on several threads with fixed affinity.

## Files and state of the Vita
- Vita: GePatch dev61 (`ux0:pspemu/seplugins/ge_patch.prx`, `plugins.txt` with ge_patch **on**), gepresent v12.9
  (`ux0:data/gepresent/gepresent.suprx`), GT and SH options with `sombra=1` (and shadow by default in the code).
- Backup of the previous options: `Downloads/vitabridge/respaldo/opciones_antes_etapa2/`. Binaries of each version:
  `vitabridge/gepresent/gepresent_v12.*.suprx`, `vitabridge/respaldo/ge_patch_dev61*.prx`.
- Logs of each test: `vitabridge/etapa2_dev61.1/`, `etapa2_dev61.2/prueba2/`, `etapa2_dev61.3/prueba3…prueba9_v12.9/`.
- Build: in the assistant's container (pspdev + vita-toolchain, recipe in `etapa1_banco_gxm.md` §5, ~5 min),
  files to the PC with stage/commit; in the Windows repo git needs delete permission in `Documents\GitHub`.
