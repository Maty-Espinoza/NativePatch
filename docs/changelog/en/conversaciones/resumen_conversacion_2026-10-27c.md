# Conversation summary of 2026-10-27: architecture analysis → dev58.1…dev58.7 (no commit)

To continue in a new conversation. Read `contexto_ia.md`, `errores_conocidos.md`,
`analisis_arquitectura_2026-10-27.md` (origin of this version) and **the "Test methodology" section below** first.

## State at close (2026-10-27, 12:00)
- **dev58.7, no commit**, on top of dev57 (`2c4e937`). PC tests: **189 checks, 0 failures**. md5 `1121c3be…`.
  On the Vita, in `~/Downloads/ge_patch.prx`, in the lab (`seplugins/` and `versiones/ge_patch_dev58.x.prx`,
  x = 1…7). Patches: `vitabridge/gepatch_dev58.7.patch` (`git apply` on `origin/dev`; also 58.1…58.6).
- **What is confirmed:**
  - dev58.7 with everything at defaults = dev57 (GT half a lap with no bugs, Vita).
  - **GT with "2D vertices (HUD, sprites)" = "by copy (dev58)"**: one lap on the Vita with no garbage, 34–36 fps,
    GE ~25 ms (same as dev57); in the lab a clean lap. Logs in `vitabridge/etapa6_dev58.7_vita/` and
    `etapa6_dev58.7_lab_GT_v2d2.log`.
  - **"Texture inside a fb = the fb"**: no benefit in GT or SH (see below). Leave it at **no**.
- **Open: RR with "by copy" on the Vita** looks wrong (the track and the cars are not visible). Dumps and log in
  `vitabridge/etapa6_dev58.7_vita_RR2/` (18400 = race, 6477 = menu). Main hypothesis below.
- Options saved on the Vita at close: GT `v2d=2` (+ `comprobar=1 tex_fb=0`, profile); RR `v2d=2 escala=3`
  (+ possibly `fb_parte`/`sin_efecto` touched: check `gepatch_ULJS-00001.txt` and leave only `escala=3`).
  Lab: `memstick/gepatch_UCUS-98632.txt` = `comprobar=1 tex_fb=0 v2d=2`.

## What dev58 changes compared to dev57 (everything off by default except the menu and the diagnostics)
1. **Framebuffer table with real size** (`struct Fb`; before, 10 arrays). `findFramebuf` recognizes a texture
   inside a fb (`fb_off`). Option "Texture inside a fb = the fb (dev58)" (`fb_parte`): **no benefit**.
   The size is computed as "width × 272 × bpp" and overestimates (GT stores textures there: 220–480 per frame "inside the fb").
2. **2D vertices by copy** (`v2d=2`): the game's vertices are not touched; they are copied, scaled, to a ring of its own
   (0x0A740000, 256 KB; VMEMO went down to 16,384 entries) and the PRIM becomes `BASE 0A + CALL` to a list in the ring
   (`BASE game; previous word; OFFSETADDR 0; BASE; VADDR copy; PRIM; BASE; VADDR as the GE leaves it; BASE game;
   RET`). Consecutive 2D draws are chained. Test modes in the same option: `v2d=3` copy without applying (CPU only),
   `v2d=4` minimal list without a copy (only the CALL mechanism).
3. **Reduced menu** (7 options); the rest only through the file (`copias_ge original sublistas comprobar fb1024
   vram_real dx dy autocap cpu intercambio rt_esc1`). Known profiles: GT, RR and LocoRoco.
4. **Diagnostics**: `stats dev58: choques_memo texturas_dentro_de_fb`; `fb … nunca mostrado en 120 frames`;
   `lista con mas de N comandos … se corta en <dir>`; **automatic dump** at the first `CALL a direccion invalida`.
5. **`BASE_PLUG`**: the BASE that the plugin writes into the game's list (2D copies and `txCall` of copies/rt1) carries
   a mark in the low 16 bits (`0x100AD1E7`); `mayBePatch` only recognizes that value.

## Errors found and fixed along the way (each one cost a run)
- dev58.1: the word before the PRIM could be one the plugin patches (viewport…): it was emitted unscaled and the
  next walk scaled it on top of the BASE → CALL without a base → the GE executing garbage. Fixed: only words
  the plugin never patches, within the same stretch (`walk_from`) and with stack < 6.
- dev58.2: using `txCall` set `tx_patched_any`, which **turns off the whole sublist cache** (GT: 15–27 ms of
  walk, hang). Fixed: own mark `v2d_patched_any`.
- dev58.3–58.4: the "texture inside the fb" diagnostic wrote one line per texture and filled the log (the
  last lines before each crash were lost). Now one per fb.
- dev58.6 → 58.7 (**the real cause of GT's crashes**, seen in the automatic dump from the lab): GT reuses its
  list between frames and wrote `BASE 0A` (a value identical to the plugin's patch) where there used to be a patched
  VERTEXTYPE; the patch memory took it for the patch, returned the VERTEXTYPE, the walk went on with another BASE
  and the game's CALL sent it to floating-point data (`091f3800`), which it scaled as if they were viewports → corruption
  of the game. Fixed with `BASE_PLUG`. Test `test_base_del_juego_no_es_parche`.

## Hypothesis for RR (first thing in the next conversation)
- In RR, with `v2d=2`, the big buffer at the end of the frame has sky, sea and HUD but **neither track nor cars**, the
  z-buffer is clean (the world was not drawn on screen) and the rearview mirror shows a track seen from the front.
- The plugin's walk is **identical** in both modes (replay on the PC, 1562 draws): the difference is in the GE.
- RR uses rt1 (mirror), whose entry/exit lists also go through `BASE + CALL` (`txCall`), now with
  `BASE_PLUG`. **If the real GE does not ignore the low 16 bits of BASE** (PPSSPP does ignore them), the CALL to the rt's
  exit list is lost, the framebuffer stays on the rt and the world is drawn there. GT does not use rt1.
- **How to decide:** (1) RR with `v2d=2` in the **lab** (PPSSPP ignores the low bits): if RR looks right there,
  it is the real GE → remove the mark and recognize the plugin's BASE by what follows it: **BASE 0A followed by a
  CALL into the plugin's ring** (the game never calls there); (2) if it also fails in the lab, it is something else in the
  CALL mechanism with rt1 (order rt1SelfDraw/rt1FromBig → v2dDraw, slots, restored VADDR) and it has to be bisected with `v2d=4`.
- Meanwhile RR runs with `v2d=1`.

## Test methodology (what worked; use it from the start)
1. **Environment:** in the container, pspdev (GitHub tarball) + Adrenaline clone in `/home/claude/tc`;
   `make ADRENALINE=/home/claude/tc/Adrenaline/cef`; tests `make -C tests` (gcc-multilib). Connect the Mac's folders
   `~/Downloads` and `~/Documents/gepatch-lab`; deliver with `device_commit_files` to `~/Downloads/ge_patch.prx`,
   `~/Downloads/vitabridge/enviar/ux0/pspemu/seplugins/ge_patch.prx`, `gepatch-lab/memstick/seplugins/ge_patch.prx`
   and `versiones/ge_patch_<v>.prx`, plus `vitabridge/gepatch_<v>.patch`; verify md5 in `vitabridge/enviados/`.
2. **Every behavior change is an option that is off by default**: the base version must behave like the
   previous one; one option is turned on per game and per run. Tell the user **which game, which menu option and
   which value**, one test per message.
3. **Lab before Vita** for whatever hangs: it is deterministic (same garbage on the same frames as the
   Vita), it does not hang and it allows bisecting (`v2d=3`/`v2d=4`). The user runs
   `PPSSPP_ADRENALINE=estricto bash ~/Documents/gepatch-lab/jugar.sh gt.iso > ~/Documents/gepatch-lab/ppsspp_salida.txt 2>&1`
   (PPSSPP's output with the software renderer does not show GE errors: it is of little use). The log ends up in
   `gepatch-lab/memstick/ge_patch.log`; the `.gpd` files in `memstick/`. I edit the game's options in
   `memstick/gepatch_<ID>.txt` from the Mac's shell.
4. **Replay on the PC** (`tests/replay.c`, new): `gcc -m32 … -o replay_bin replay.c stubs.c ../log.c ../menu.c
   ../dump.c`; `W=<dir> SC=<scale> ./replay_bin DUMP.gpd FRAMES V2D` loads the lists (original values) and the
   vertices of the dump into the simulated memory, walks N frames and reports list words altered without memo and
   garbage in the log; with `W=` it prints the game's words, the plugin's list and the vertex copy; compiled
   with `-DREPLAY_TRACE=N` it prints the first N commands and one line per PRIM (walk state) to compare
   modes (`diff`). What the dump does not have (bone sublists) is filled with RET. It is useful for the walk;
   it does **not** model the GE. Useful dumps: GT dev57 frame 8047 (`recibidos/`), RR 6477/18400 (`etapa6_dev58.7_vita_RR2/`).
5. **Read the dumps without gpd.py** when the scale is not 2x: `gpd.py imagenes` assumes 960x544; with a short
   script (MEM records kind 0/2 = real VRAM start/end, 1/3 = fake VRAM) the big buffer is drawn with its real pitch
   (720 at 1.5x) and the z-buffer (at 0x04090000 at 1.5x) as PNG. A clean z-buffer = the world was not drawn.
6. **Save each log as soon as it arrives** in `vitabridge/etapa6_<version>_<test>/` (vpjuego's log and `ge_patch.log`
   are overwritten on every run). If the Vita hangs, the relay is left with a `.parcial` until it comes back.
7. **Codes:** `C1-2858-3` is the Vita's generic one ("the application closed"), it does not distinguish PSP/ARM.
   The lines `CALL a direccion invalida` / `fb … pitch=<raro>` / `offset juego=(…,592)` in the log mean that the
   walk is reading data as commands: the game is already corrupted; the automatic dump captures it.
8. Platform findings: GT keeps its **sublists in the fake VRAM** (0x0A1BC000+) and **reuses the list between
   frames** changing only some words; RR enqueues with uncached addresses (0x48…) and updates the stall every
   two words (`BASE; CALL sub`); `vpjuego` always reports "GE stalled 0%" (it does not distinguish a slow CPU from a slow GE).
   The patch memory in SH has ~120 collisions per frame (harmless because SH rebuilds its lists; pending).

## In simple terms
- The idea of copying the flat drawings instead of modifying the game's data works in Gran Turismo (a full lap,
  same performance). The day's three crashes were three different errors in the mechanism, the last one
  found thanks to an automatic dump in the Mac's emulator: the game wrote on its own a value equal
  to the plugin's "note", and the plugin read numbers as instructions.
- In Ridge Racer it still fails (the track is not visible): the suspicion is that the new signature of that note is not liked by
  the real graphics chip, and it will be decided with a run in the emulator.
- A way of working remains: each improvement is a switch, it is tested in the emulator before the console, and
  there is a tool on the PC that replays the plugin's walk on real captures without touching the Vita.
