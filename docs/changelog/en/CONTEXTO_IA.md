# Context to continue GePatch (handoff to another AI)

**For how to work (where the repo is, pull, build, relay, tests): `FLUJO_DE_TRABAJO.md`.**

Updated on 2026-10-24 (GePatch 0.2-dev49, branch `dev`). Read it
in full before touching code; the other documents in `docs/` expand on each topic.

## 1. What it is and what we are after

- **GePatch** (`ge_patch.prx`) is a kernel plugin for **Adrenaline** (the official PSP
  emulator inside the PS Vita). It makes PSP games render at **960x544** (double
  480x272) by patching the GE display lists (the PSP's GPU) before they run.
- The Vita's GE is real PSP hardware: the plugin emulates nothing, it rewrites commands.
- This repo (`Maty-Espinoza/NativePatch`, a fork of TheFlow's original GePatch, abandoned
  at v0.2) aims to **revive it and fix its bugs**. Test games: **Silent Hill
  Shattered Memories (ULUS-10450)**, **Ridge Racer (ULJS-00001)** and Gran Turismo.
- The user's clone on his Mac: `~/Documents/GitHub/NativePatch`.

### User rules (follow them)
- **Always** answer in **Spanish**.
- **Compact but understandable** code: simplicity and readability are preferred over the constants and
  verbosity of "best practices".
- **Commits:** only when a bug is fixed (ideally confirmed on the Vita), not on every
  iteration. If the user explicitly asks for a commit, make it.
- The user tests on his Vita and sends logs and dumps; he describes what he sees (flickering,
  colors, missing textures).

## 2. Build and test

| What | How |
|---|---|
| Plugin | pspdev toolchain in the PATH; `make ADRENALINE=<path to Adrenaline/cef>` (needs `include/systemctrl.h` and `lib/libpspsystemctrl_kernel.a` from TheOfficialFloW/Adrenaline). Produces `ge_patch.prx`. |
| PC tests | `make -C tests` (gcc with `-m32`, Ubuntu: `gcc-multilib`). Today: **115 checks, 0 failures** (dev49 removed the tests for what was deleted). They simulate GE lists with `tests/stubs.c` and mapped PSP memory. CI in `.github/workflows/ci.yml`. |
| Benchmark | `make -C tests bench` (Gran Turismo-style list). |
| Version | `GEPATCH_VERSION` in `log.h` (bump it on every delivery to the Vita: dev45 → dev46…). |
| Install on the Vita | `ux0:/pspemu/seplugins/ge_patch.prx` and in `game.txt`: `ms0:/seplugins/ge_patch.prx 1`. Adrenaline's "Force high memory layout" **off**. |

On the console:
- **L when starting the game:** passive mode (no patches; serves as a reference).
- **L+R+START:** diagnostic menu with the options (`menu.c`). They are saved per game in `ms0:/gepatch_<ID>.txt`.
- **L+R+SELECT:** text capture in the log and binary dump `ms0:/gepatch_<ID>_<frame>.gpd` of **3 consecutive frames** (since dev49: what one frame draws into a render target, such as the rear-view mirror, is usually used in the next one).
- **Log:** `ms0:/ge_patch.log` (the previous one is kept as `ge_patch_prev.log`).

## 3. Platform facts that cost dearly (do not forget)

- **The PSP's VRAM on the Vita is 2 MB.** `sceGeEdramSetSize(4 MB)` fails (0x80000104).
  - 0x04200000–0x043FFFFF is a **mirror**: what the GE draws there lands at `addr − 2 MB`; what is read there comes out permuted (`^0x8040` in that area).
  - The 2 MB are full: big 960x544 565 buffer at 0x04000000 and its z-buffer at 0x04100000 (0xFF000 bytes). **There is no free VRAM** for render targets.
- **The destination alpha** is only written via stencil in the GE (the downscale uses stencil REPLACE 0xff).
- A **list with context** enqueued from the plugin freezes Adrenaline; LoadExec from a game or writing beyond the VRAM (0x04800000) closes Adrenaline.
- The GE **cannot draw into RAM**. What does not fit in VRAM is copied to RAM with TRANSFER.
- Adrenaline reads the native image from **0x0A400000** (960x544 565) when `*(u32*)0xABCDEF00 != 0` (DRAW_NATIVE). Not synchronized with the flip.
- Adrenaline runs at 333 MHz. The plugin's cost is almost entirely the **list walk** (~300 ns per command on the Allegrex).

## 4. Plugin memory map (RAM 0x0A000000–0x0B000000, free in Adrenaline)

| Address | Use |
|---|---|
| 0x0A000000 | **Fake VRAM** (4 MB): `sceGeEdramGetAddr/GetSize` patched; the game believes it is its VRAM |
| 0x0A400000 | Native 960x544 image shown by Adrenaline (DISPLAY_BUFFER) |
| 0x0A500000 | LAYER_SAVE: scene saved during the swap (960x544 565) |
| 0x0A600000 | MEMO_ADDR: patch table (original word of each patched command) |
| 0x0A700000 | VMEMO: duplicated 2D vertices; 0x0A780000 WC_ADDR (sublist cache) |
| 0x0A800000 | RENDER_LIST: the plugin's own GE lists; +0x8000 ring of 64 slots of 1 KB (`txSlot`) |
| 0x0A900000 | Menu image |
| 0x0A980000 | .gpd dump buffer; 0x0AA00000–0x0AE00000 dump snapshots |
| 0x0AE00000 | 3D clipping (`sub.h`): clipping coordinates (64 KB), flags (4 KB) and ring of new vertices up to 0x0AF00000 |
| 0x0AF40000 | OVL_BUF: CPU drawings (spinner) at double size |
| 0x0AF60000 | Z_SAVE (up to 0x0B000000, 640 KB): z-buffer saved while its VRAM is used for an rt |
| 0x0B000000 | Adrenaline flash0 package: **do not touch** |

## 5. Plugin architecture (`main.c` ~3300 lines, `sub.h`, `dump.c`, `menu.c`, `log.c`, `gu.c`)

- **Hooks:** `sctrlHENPatchSyscall` on `sceGeListEnQueue` and `sceGeListUpdateStallAddr` → `patchGeList(list, stall)` walks the new commands before the GE runs them. It also patches `sceGeEdramGetAddr/GetSize` and the flip (`copyFrameBuffer` / `dumpOnFlip`).
- **Reversible patches:** `setOp(p, orig, op)` writes the word and remembers the original (`origOp`) in the MEMO table. Lists are walked several times (games with static lists), so each decision is recomputed on every walk and whatever no longer applies is restored.
- **Slots:** `txSlot()` gives 1 KB from the ring; `txCall(list, reg, sl)` changes the word before the draw into BASE and the draw into a CALL to the plugin's list. This is how commands are inserted "in place" without moving the game's list. The slot's list repeats the replaced word.
- **Framebuffers (fb):** each FRAMEBUFPTR/WIDTH is classified in `state.framebuf_addr[16]` and ends up in one of these destinations:
  - **big buffer** (screen at double size);
  - **ignored** (draws skipped with NOP);
  - **scale-1 rt in the z VRAM** (`rt1`, dev41+, see below).
  - (dev49 removed what was never used on the Vita: double-size render targets in the high VRAM, "intermediate" fbs as render target, the scale-1 copy fb (csrc), the downscaled copy to the shown fb, the old list walk and the 4 MB VRAM test. It is in the git history if needed.)
- **Scaling:** viewport, scissor and region ×2; 2D (through) vertices duplicated in VMEMO.
- **Textures** (`texCheck`/`texCheckCalc`, with `tcache` cache). `texmode` values:
  - **0** normal;
  - **1** skip the draw;
  - **3** texture from the real VRAM moved to the fake one;
  - **4** the current fb;
  - **6** blending of the swap pass (`swapComposite`);
  - **7** rt1 that reads the screen (`rt1FromBig`, by quarters of the big buffer);
  - **8** rt1 that reads itself (`rt1SelfDraw`: the texture changes only for that draw);
  - **9** the screen (big buffer) reading itself (`rt1FromBig` with the vertices already at double size; fixed the purple squares in SH).
- **Copies (TRANSFER):**
  - The game's copies to VRAM are scaled with the GE (`txUpscale`, in strips).
  - Copies from the screen are taken from the downscaled big buffer (`emitDownscale` to 0x04100000 and then to RAM).
  - Small copies (≤ 8 per frame) are drawn at double size.
  - `fb_txbox` remembers copied areas.
- **rt1, scale-1 render targets (dev41–dev45):** an fb that is not the screen, with pitch 16–479, is drawn at scale 1 in the z VRAM (0x04100000).
  - **On entry** (`rt1Words`): the FBPTR/FBWIDTH words become a CALL that saves the z into Z_SAVE if the big buffer has already used depth, loads the previous content from the fake VRAM and sets its own depth.
  - **On exit** (`rt1Emit`): copies the rt to its place in the fake VRAM, restores the z and returns to the big buffer.
  - The textures that read it go to the fake VRAM.
  - Menu option `rt_esc1` (on by default).
- **Swap** (SH light passes, fb 0a170000):
  1. `swapLearn` detects a never-shown fb that is pasted over the screen.
  2. From the next frame on, `swapEnter` saves the scene in LAYER_SAVE and the pass is drawn in the big buffer.
  3. `swapComposite` blends the scene on top with the inverted blending (`blendSwap`).
  4. If that is not possible, `swapRestore` returns the scene and `swapBackoff` waits 2, 4, 8… frames (maximum 600) before retrying.
- **3D triangle clipping** (`sub.h`, dev47–dev48, option `recorte3d`): the GE discards an entire triangle if a vertex falls outside 0..4096 on screen; at double size, large triangles near the camera (the road under the car in RR) disappeared. GePatch follows the game's matrices (`mtxCmd`), computes on the CPU where the vertices of each 3D draw land (first the 8 corners of its box) and, if a triangle that the game draws at 1x goes out of range at double size, clips it against the near plane (with the game's depth clipping) and a safe range 128..3968 (the GE computes with 24-bit floats: at the exact edge it also discarded). The draw becomes a CALL to its list with the new triangles. Formats: no weights or morph; position s8/s16/float; color 16-bit or 8888.
- **CPU drawings** (`cpuOverlay`): SH's loading spinner is drawn by the CPU into the shown fb; the box is detected and pasted at double size.
- **.gpd dump** (`dump.c`): 3 consecutive frames (`DUMP_FRAMES`): commands, draws, log events, real and fake VRAM at the start and at the end, copy destinations and snapshots. It weighs ~3 times more (40–50 MB).
- **Log** (`log.c`): own thread with double buffer. `LOG_ONCE(key, ...)` writes only once; `LOG_ONCE_CAP` also triggers an automatic capture (maximum 5).

## 6. Tools

- **`tools/gpd.py`** (standard library only): `resumen`, `informe` (all commands game → plugin, with draws and events), `imagenes` (PNGs of fbs, big buffer, rts, copies, textures), `ppsspp` (to .ppdmp) and `comparar` (passive versus active). Since dev49, to diagnose with few tokens (see `estrategia_diagnostico.md`):
  - `linaje VOLCADO`: who writes each fb (destination and draw ranges, per frame `f0..f2`) and who reads it as a texture, with the last draw that wrote it (also from the previous frame); warns about 3D viewports whose scale is not that of their destination (x2 on the screen, x1 in rt1).
  - `diff ACTIVO [PASIVO]`: critical registers (viewport, offset, scissor, region, fb, z, texture, vertex type) that the plugin changed, by draw ranges and only what changes from one range to the next (~50 lines); marks the draws replaced by CALL (3D clipping, rt1, swap). With PASIVO, the draws where the game itself sent other values.
  - `vram VOLCADO`: map of the fake VRAM (fbs and textures, with collisions) and when the z VRAM was saved or restored (rt1/Z_SAVE) relative to the draws with depth.
  - `original VOLCADO CARPETA [--ppsspp=PPSSPPHeadless]`: the same frame **without GePatch**, reassembled from an active dump (original commands + fake VRAM) and drawn by PPSSPPHeadless. It leaves `comparacion.png` (without GePatch | with GePatch downscaled | differences). It is faithful for what is drawn within the frame (Ridge Racer comes out clean). It warns about the fbs that GePatch does not leave in the fake VRAM (ignored or swap passes): their previous content is noise. It affects SH's flashlight.
  - Possible improvement: repeat the replay using the result of the first one as previous content.
- **`tools/vitabridge.py`** (relay; the user runs it in the Mac's Terminal, folder `~/Downloads/vitabridge`, Vita at 192.168.0.15 with vitacompanion: FTP 1337, commands 1338):
  - `enviar/ux0/...` → uploaded to the Vita.
  - `cmd_*.txt` → commands (`launch`, `press`, `wait`…).
  - `bajar_*.txt` → downloads paths to `recibidos/`, or "path -> destination".
  - `listar_*.txt` → folder listings.
  - It downloads only the new `ge_patch*.log` and `*.gpd` files.
  - `estado.txt` is the heartbeat.
  - When copying files to the Mac, use unique names at the source: the copy tool caches by name.
- **`tools/geptest`:** the 112 pspautotests GE tests run on the Vita in a single process. Result: the Vita's GE behaves like a PSP's, except for what is Adrenaline as a system (see `geptest_resultados.md`).
- **`tools/ppsspp-vita` (PPSSPP-Adrenaline model):** `adrenaline.patch` on hrydgard/ppsspp **1adf3c5**; enabled with `PPSSPP_ADRENALINE=1` (or `=estricto`).
  - Loads GePatch **from the .prx** via `seplugins/game.txt`.
  - 64 MB with a 32 MB user partition.
  - systemctrl stubs.
  - 2 MB VRAM with permuted mirrors on all accesses (that is why it uses the IR interpreter, not the JIT).
  - 333 MHz.
  - `memset`/`memmove` that return the destination.
  - Headless captures also leave `X_nativo.bmp`.
  - Usage: `modelo.sh PPSSPPHeadless MEMSTICK JUEGO [opciones]`.
- **`tools/geptest/escena` (GEPVM):** deterministic scene (render to texture, alpha, palette, copies, 3D cube, CPU box, 64x64 RT2 that reads the screen and accumulates). `kcap.prx` saves captures at frames 5/30/60/90/120 and `comparar_vm.py VITA MODELO [REF] [html]` compares them.
  - **Result:** Vita and model identical byte for byte (dev40–dev43). With the model, the scene's bugs are debugged without the console.
- **Lab on the Mac** (`~/Documents/gepatch-lab`, see `laboratorio_mac.md`): PPSSPP with a UI + a patch to run SH and RR watching the native image. **Built and working**: it gives the same as the Vita, except for some performance. `jugar.sh` opens with `--debugger=45678` (PPSSPP's WebSocket API, for future control from Claude). Dumps are left in `memstick/`. It deliberately uses the software renderer (the one that imitates the GE byte for byte).

## 7. Summarized history

- **Original v0.2:** big buffer, fake VRAM, DFS walk.
- **dev1–dev20:** walk fixed and optimized; menu; passive mode; SH videos scaled with the GE; L+R+SELECT capture.
- **dev21–dev30:** unchanged sublists skipped; copies from the screen; SH form; .gpd dump.
- **dev31–dev33:** discovery of the 2 MB VRAM; alpha in the downscale (SH form fixed, commit e4530d5).
- **dev34–dev40:** SH door and flashlight by swap; CPU spinner; GEPTEST.
- **dev41:** rt1 (scale-1 rt in the z VRAM), small copies, cpuOverlay 565/8888, PPSSPP-Adrenaline model and GEPVM.
- **dev42:** z save (Z_SAVE) → the car appears in RR. **dev43:** SH without flickering; RR with purple artifacts.
- **dev44:** loading of the previous content of rt1; `swapRestore`; fixed 10 s wait after the swap fails.
- **dev45:** RR without purple stripes (`rt1SelfDraw`: the rt that reads itself no longer leaves the game's texture words patched). SH: growing wait when the swap fails (the car without textures for 10 s). Confirmed on the Vita.
- **dev46:** SH without purple squares: the light pass in the big buffer was reading itself at the game's address (the z-buffer). Confirmed in the lab. `gpd.py original` (the same frame without GePatch).
- **dev47:** 3D clipping with subdivision into 4 (not enough: the corner that went out never came back into range).
- **dev48:** exact clipping against the near plane and the safe range (verified by replaying an RR dump in the model: the road comes back). **Open side effect:** RR's rear-view mirror (third-person camera) no longer shows anything.
- **dev49:** 3-frame dump, `gpd.py linaje/diff/vram`, cleanup of unused code (see section 5), documents up to date.

## 8. Open / next steps

1. **RR, empty rear-view mirror with dev48** (the user asked not to solve it yet). To investigate it: 3-frame dump with the mirror visible and `gpd.py linaje` + `diff` (does a draw with a clipping CALL draw it? mirror viewport/scissor?).
2. **RR:** confirm on the Vita that the road is right with dev48/dev49 (in the lab it improved a lot).
3. **SH:**
   - **Flashlight:** blending 0x032 of the light pass; the plan is to make the pass in 4444 and the blending in the z area.
   - **Ignored 1024-wide passes** (~250 draws per frame, e.g. fb 0a04c000 with texture 08bb3600).
   - Read in the log the reason for the swap failures ("intercambio: la mezcla no se puede hacer…").
4. **Performance:** in SH, 18–22 fps in heavy scenes (plugin 4–9 ms per frame); GT, 20–30 fps. Big idea: walk the lists on the Vita's ARM through Kermit (`KERMIT_MODE_EXTRA_1` free; see `adrenaline_notas.md`).
5. **Lab on the Mac:**
   - Phase 2: monitor over WebSocket (`--debugger=45678`) and Claude commands: pause, save/load state, capture, read/write the plugin's memory.
   - Phase 3: repeat the same frame with other parameters (saved state + plugin settings table) and regression between versions.

## 9. Workflow that worked

1. The user runs GEPVM, then the games; he captures with L+R+SELECT where he sees the error.
2. The `ge_patch.log` and the `.gpd` files are read: first `gpd.py original` (does the game without GePatch look right?), `linaje`, `diff` and `vram` (short summaries); only after that `informe` and `imagenes` for the specific draw (GE state, patched words — "parche posterior" — and what it reads or writes).
   - To test an idea without building the plugin, it can be simulated in Python on the dump (replay with viewport x2 in PPSSPPHeadless): that is how the 3D clipping was found.
3. The fix is made with a new test in `tests/test_main.c`, GEPVM is run on the model (comparing with the previous version's model: it must stay the same or improve) and the `.prx` is uploaded with the relay.
4. A log event is only a bug if it coincides with a difference in the image.

## 10. Documents

| File | Contents |
|---|---|
| `diagnostico.md` | .gpd dump, relay, 2 MB VRAM, SH findings, dev45–dev48 analysis |
| `estrategia_diagnostico.md` | Low-token diagnostic proposal (3-frame burst, lineage, diff, VRAM) and its status |
| `adrenaline_notas.md` | How Adrenaline works, memory map, performance, Kermit |
| `modelo_vita.md` | Test model design (GEPTEST, recording, Vita profile) |
| `geptest_resultados.md` | Results of the 112 GE tests on the Vita |
| `modelo_adrenaline.md` | PPSSPP-Adrenaline model, GEPVM and byte-for-byte verification |
| `laboratorio_mac.md` | Lab on the Mac (PPSSPP with UI, monitor, inputs) |
| `../tools/ppsspp-vita/README.md`, `../tools/geptest/README.md` | Usage of the tools |
| `gepatch_compatibilidad.csv` | GePatch's original compatibility list |
