# NativePatch context for a local AI

This document explains the project and the way of working to a local AI (Ollama or another),
to help Matías when there is no access to Claude. It is written to be read in full in one go.
At the end there are concrete instructions on what to do and what not to do.

Status: 2026-10-24, GePatch **0.2-dev52**, branch `dev` of `Maty-Espinoza/NativePatch`.

---

## 1. The project in a few words

- **Adrenaline** is the official PSP emulator that runs inside the **PS Vita**. The Vita has
  the PSP's graphics chip (the **GE**) for real: PSP games draw with real hardware.
- **GePatch** (`ge_patch.prx`) is a plugin for Adrenaline. It makes PSP games render
  at **960x544** instead of 480x272 (double the resolution, the Vita's native screen).
- It emulates nothing. It **rewrites the command lists** that the game sends to the GE (display
  lists) right before the GE runs them: it doubles the size of the drawing area, moves
  memory addresses, duplicates 2D coordinates, etc.
- The original author (TheOfficialFloW) abandoned it at version 0.2 with many bugs.
  NativePatch is Matías's fork to revive it.
- Test games:
  - **Ridge Racer** (ID ULJS-00001, Japanese version): 3D racing.
  - **Silent Hill Shattered Memories** (ULUS-10450): flashlight, light passes, videos.
  - **Gran Turismo** (UCUS-98632): performance.

## 2. Working rules (always follow them)

1. **Always** answer in **Spanish**.
2. **Compact but understandable** code: prefer simplicity and readability over the constants and
   verbosity of "best practices".
3. **Commits only** when a bug is fixed and confirmed (on the Vita or in the lab), or
   when Matías explicitly asks for one. Do not commit for every attempt.
4. **Do not take actions that were not requested.** If something seems useful but was not asked for, propose it
   and wait for an answer.
5. **Do not fix Ridge Racer's rear-view mirror** until Matías asks.
6. A log event is only a bug if it coincides with something that looks wrong in the image.
7. When something is not known, say so. Do not invent addresses, values or results.

## 3. Where everything is

| What | Where |
|---|---|
| Plugin code | NativePatch repo: `main.c` (~3000 lines), `sub.h` (3D clipping), `dump.c` (dumps), `menu.c`, `log.c`, `gu.c` |
| PC tests | `tests/test_main.c` (`make -C tests`; today 115 checks, 0 failures) |
| Dump reader | `tools/gpd.py` (Python 3 standard library only) |
| Documents | `docs/` (this one, `CONTEXTO_IA.md`, `diagnostico.md`, `regresiones_versiones.md`, `MANUAL_BASICO.md`...) |
| Lab on the Mac | `~/Documents/gepatch-lab` (modified PPSSPP that imitates Adrenaline) |
| Relay with the Vita | `~/Downloads/vitabridge` (see section 6) |
| Plugin on the Vita | `ux0:/pspemu/seplugins/ge_patch.prx`, enabled in `game.txt` with `ms0:/seplugins/ge_patch.prx 1` |
| Plugin in the lab | `~/Documents/gepatch-lab/memstick/seplugins/ge_patch.prx` |
| Old versions to compare | `~/Documents/gepatch-lab/memstick/seplugins/versiones/` (original v0.2, dev46, dev49, dev51) |

## 4. How the plugin works (what is needed to reason about it)

### 4.1 The central idea
- The game believes it draws on a 480x272 screen in VRAM (video memory, 2 MB).
- The plugin:
  - Gives the game a **fake VRAM** in RAM (0x0A000000).
  - Makes the GE draw the screen into a **big buffer** of 960x544 in the real VRAM
    (0x04000000).
  - Copies that buffer to 0x0A400000, from where Adrenaline displays it.
- To do this it scales viewport, scissor and region ×2, and duplicates the coordinates of 2D draws
  ("through" mode: HUD, menus, sprites).

### 4.2 List walk
- Hooks:
  - `sceGeListEnQueue` and `sceGeListUpdateStallAddr` call `patchGeList(list, stall)`, which
    reads each new command.
  - It also patches `sceGeEdramGetAddr/GetSize` (fake VRAM) and `sceDisplaySetFrameBuf` (the
    "flip", when a frame ends).
- `setOp(p, orig, op)` replaces a word of the list and saves the original in the MEMO table
  (`origOp`). Lists are walked many times (some are static), so each
  decision is recomputed and whatever no longer applies is undone.
- `txSlot()` reserves 1 KB in a ring of the plugin's own lists. `txCall(list, reg, sl)` changes the
  word before the draw into BASE and the draw into a CALL to that list. This is how commands are inserted without
  moving the game's list.

### 4.3 Framebuffer (fb) classification
Every time the game chooses where to draw (FRAMEBUFPTR + FRAMEBUFWIDTH):
- **Width 512/480/960 → big buffer** (it is the screen).
- **Width 16–479 → "rt1"**, a scale-1 render target drawn in the z-buffer area
  (0x04100000). When finished it is copied to the fake VRAM. If depth has already been used, the z is saved
  beforehand in Z_SAVE and restored afterwards.
- **Everything else → ignored** (its draws are canceled).

### 4.4 Textures (`texmode`)
| value | what it does |
|---|---|
| 0 | normal |
| 1 | skip the draw |
| 3 | the texture was in the real VRAM: it is read from the fake one |
| 4 | the texture is the current fb |
| 6 | blending of the swap pass (Silent Hill) |
| 7 | rt1 that reads the screen |
| 8 | rt1 that reads itself |
| 9 | the screen reading itself (fixed the purple squares in SH) |

### 4.5 3D clipping (`sub.h`, menu option `recorte3d`)
- The GE discards an entire triangle if **one vertex** falls outside 0..4096 on screen. At
  double size, large triangles near the camera (the road under the car in RR) disappeared.
- The plugin follows the game's matrices, computes on the CPU where each vertex lands and clips the
  triangles that go out. It clips them against the near plane and against a safe range
  (128..3968). Then it replaces the draw with a CALL to a list with the new triangles.
- Since dev50: if the clipping leaves nothing, the original triangle is drawn.

### 4.6 Plugin memory map (RAM 0x0A000000–0x0B000000)
| Address | Use |
|---|---|
| 0x0A000000 | fake VRAM (4 MB for the game) |
| 0x0A400000 | 960x544 image shown by Adrenaline |
| 0x0A500000 | scene saved for the swap |
| 0x0A600000 | MEMO: original words of the patched commands |
| 0x0A700000 | duplicated 2D vertices; 0x0A780000 sublist cache |
| 0x0A800000 | own lists; from +0x8000, ring of **512** lists of 1 KB (dev51; before 64) |
| 0x0A900000 | menu image |
| 0x0A980000 | dump buffer; 0x0AA00000–0x0AE00000 snapshots |
| 0x0AE00000 | 3D clipping: coordinates, flags, ring of vertices up to 0x0AF00000 |
| 0x0AF40000 | CPU drawings (spinner) |
| 0x0AF60000 | Z_SAVE (up to 0x0B000000) |
| 0x0B000000 | Adrenaline package: **do not touch** |

### 4.7 Platform facts (they cost dearly, do not forget)
- The PSP's VRAM on the Vita is **2 MB** and it is full: big buffer (0x04000000) and its
  z-buffer (0x04100000). There is no free VRAM.
- The GE **cannot draw into RAM**. What does not fit is copied with TRANSFER.
- The GE runs **behind** the walk: the plugin patches lists that the GE runs later (up to
  one or two frames). All memory that the plugin writes and the GE reads (lists, vertices) has to
  last several frames.
- Enqueuing a list with context from the plugin freezes Adrenaline. Writing beyond the VRAM
  (0x04800000) closes it.

## 5. Controls on the console and in the lab

- **Hold L when starting the game:** passive mode (no patches; serves as a reference).
- **L+R+START:** plugin menu. Options are saved per game in `ms0:/gepatch_<ID>.txt`
  (for example `recorte3d=0`).
- **L+R+SELECT:** text capture in the log and **binary dump of 1 frame**
  (`ms0:/gepatch_<ID>_<frame>.gpd`). dev49 recorded 3 frames; dev52 went back to 1.
- **Log:** `ms0:/ge_patch.log` (the previous session is kept as `ge_patch_prev.log`).
- There are also up to 5 automatic text **captures** when a new event appears.

## 6. Workflow

### 6.1 Normal cycle
1. Matías tests on the Vita or in the lab and describes what he sees (missing textures,
   colors, flickering, crashes).
2. Where he sees the error, he presses **L+R+SELECT**: a log and a `.gpd` are left.
3. They are analyzed (section 7).
4. A change is proposed, a test is added in `tests/test_main.c`, it is built, the
   version is bumped (`GEPATCH_VERSION` in `log.h`: dev52 → dev53) and the `.prx` is copied to the lab and to
   the Vita.
5. If Matías confirms it is fixed, a commit is made.

### 6.2 Build
- Plugin: pspdev toolchain in the PATH and then
  `make ADRENALINE=<path to Adrenaline/cef>` (uses `systemctrl.h` and
  `libpspsystemctrl_kernel.a` from TheOfficialFloW/Adrenaline). Produces `ge_patch.prx`.
- Tests: `make -C tests` and then `tests/test_main` (needs gcc with `-m32`).

### 6.3 Relay with the Vita (`~/Downloads/vitabridge`)
`tools/vitabridge.py` is run by Matías in the Mac's Terminal. The Vita is at 192.168.0.15 with
vitacompanion.
- What is put in `enviar/ux0/...` is uploaded to the Vita.
- New logs and dumps are downloaded to `recibidos/`.
- `estado.txt` shows whether the relay is alive.

### 6.4 Lab on the Mac (`~/Documents/gepatch-lab`)
- `bash jugar.sh rr.iso` opens Ridge Racer in PPSSPP as if it were Adrenaline, with the plugin from
  `memstick/seplugins`. The window shows the native image.
- What is on the Vita looks the same in the lab (except for performance), and the black sky
  is also reproduced there.
- The log and the dumps are left in `memstick/`.
- To test another version: copy a `.prx` from `versiones/` as `seplugins/ge_patch.prx`.

## 7. How to read a dump (`tools/gpd.py`)

Recommended order (from short to long):
1. `python3 tools/gpd.py resumen X.gpd`: version, options, fbs used, how many draws per fb
   and per decision, log events.
2. `gpd.py original X.gpd CARPETA --ppsspp=PPSSPPHeadless`: the same frame **without GePatch**.
   It leaves `comparacion.png` with three panels: without GePatch | with GePatch | differences.
3. `gpd.py linaje X.gpd`: who writes each fb and who reads it as a texture.
4. `gpd.py diff X.gpd`: registers that the plugin changed, by draw ranges.
5. `gpd.py vram X.gpd`: map of the fake VRAM and saving/restoring of the z.
6. Only after that, `gpd.py informe X.gpd > informe.txt` (all commands, it can have 30,000
   lines: search with grep `>>> DRAW #N`) and `imagenes X.gpd CARPETA --texturas`.

How to read the report:
- `>>> DRAW #92 tira_tri n=4 vt=00019f 3D fb=0a044000 -> buffer grande tex=0a0fc7c0 [normal]`:
  - draw number 92, triangle strip of 4 vertices;
  - vertex type 0x19f;
  - 3D, draws into fb 0a044000, which goes to the big buffer;
  - texture at 0x0A0FC7C0;
  - decision "normal".
- `PRIM 040010 -> CALL 80dc00`: the plugin replaced that draw with a CALL to its list (3D clipping,
  rt1 or swap).
- `(parche posterior)`: an earlier word that the plugin changed later.

## 8. Current status and open problems

1. **Ridge Racer, black sky** (since dev48; also in the lab).
   - The GE registers in the first draw of the sky are the same as dev46's, which looks
     fine, and the texture is intact.
   - Replaying the commands with the 3D clipping in PPSSPPHeadless does draw the sky.
   - Hypothesis: the ring of 64 own lists was reused before the GE ran them
     (dev50 used ~42 per frame). dev51 raised it to 512. **Unconfirmed.**
2. **Lab crash with dev51** ("El juego crasheo" screen, Bad Execution Address, thread
   SasCore). In an automated test of an RR race:
   - dev46 holds up for 240 s without problems.
   - dev49 crashes inside the plugin, in `GetIndexBounds`: it reads 16-bit indices from
     an odd address.
   - dev51 starts reading garbage commands at ~155 s.
   - Suspicion: new code from dev47–dev49 (the 3D clipping is the biggest). **Not investigated
     in depth.** A useful test: run with `recorte3d=0` in `ms0:/gepatch_ULJS-00001.txt` and see
     whether the crash disappears.
3. **RR's empty rear-view mirror** since dev48: **do not touch** until Matías asks.
4. **Silent Hill:** flashlight (blending of the light pass), ignored 1024-wide passes.
5. **Performance:** SH 18–22 fps in heavy scenes, GT 20–30 fps.

Recent history:
| version | change |
|---|---|
| dev45 | RR without purple stripes (confirmed on the Vita) |
| dev46 | SH without purple squares (texmode 9) |
| dev47 | 3D clipping by subdivision (not enough) |
| dev48 | exact clipping against the near plane; the black sky and the empty rear-view mirror appear |
| dev49 | 3-frame dump, `gpd.py linaje/diff/vram`, code cleanup |
| dev50 | if the clipping leaves no triangles, the original is drawn |
| dev51 | list ring 64 → 512; SIGNAL SYNC only skips if FINISH+END follow |
| dev52 | dump back to 1 frame |

Lessons from the original GePatch (detail in `regresiones_versiones.md`):
- Canceling draws instead of redirecting them broke videos and effects (v0.17).
- Reusing memory that the GE had not yet read caused flickering (v0.2).
- Heuristics based on fixed values (480, 512, ±2048) work in one game and fail in another.

## 9. What the local AI can do (and what it cannot)

It can:
- Explain concepts, the code and the documents to Matías.
- Read a log or `gpd.py` output that Matías pastes, and say what stands out,
  following section 7.
- Help prepare a good report for Claude (section 10).
- Propose hypotheses, always marked as hypotheses.
- Review a small code change and explain it.

It must not:
- Make commits, upload files or change options on its own.
- Treat as confirmed something that was not tested.
- Change the memory map or write outside 0x0A000000–0x0B000000.
- Touch RR's rear-view mirror.

## 10. Report template for Claude

```
Plugin version: dev__
Where: Vita / lab
Game and scene: (e.g. RR, first race, bridge; SH, corridor with flashlight)
What looks wrong: (what is missing, color, flickering, crash; since when)
How to reproduce it: (steps)
Files: ge_patch.log, gepatch_<ID>_<frame>.gpd (capture frame)
Options changed in the menu: (or "none")
Comparison: does it look fine in passive mode (L at start)? and with another version?
What I already tried / what the local AI told me:
```
