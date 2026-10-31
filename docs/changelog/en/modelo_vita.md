# Vita model for testing: feasibility and design

Written on 2026-10-22, at the user's request.

**Idea:** measure on the console how the GE really behaves inside Adrenaline, with controlled tests and recordings longer than one frame. Run the same thing in headless PPSSPP, compare, and adjust PPSSPP with a "Vita profile" until they match. This way the plugin could be tested without the console.

## Feasibility: yes, in parts

**What already exists and reduces the work:**
- **pspautotests** (PPSSPP's test repository; already in the local clone, at `ppsspp/pspautotests`).
  - It brings 112 GE tests (`tests/gpu/*`: rendertarget, transfer, stencil, dither, ge/edram, ge/edramswizzle, ge/context, displaylist, timing…).
  - Each test has its expected result (`.expected`) measured on a real PSP and the `.prx` already compiled. They are generated with `gentest.py` and psplink.
  - Examples:
    - `edramswizzle.expected` describes the VRAM mirrors with permuted addresses, the same as seen on the Vita.
    - `context.expected` describes the lists with context, the same ones that froze the game on Adrenaline.
- **PSP toolchain** (pspdev) and **PPSSPPHeadless** already built in the environment. Headless runs those `.prx` directly and compares with `.expected`.
- **The relay** (vitacompanion + `vitabridge.py`): uploads files, downloads results and simulates buttons.
- **The .gpd dumps** and their conversion to `.ppdmp`.

**Three-way comparison:** real PSP (`.expected`) vs Vita/Adrenaline (new) vs PPSSPP.
- Where the Vita differs from the PSP: it is Adrenaline. That is the missing data.
- Where PPSSPP differs from the Vita: it is what the Vita profile has to correct.

**Limits (so as not to promise an exact model):**
- **Timing and concurrency:** when each list runs relative to the flip, interrupts, Kermit latencies. They cannot be reproduced cycle by cycle. They are measured and modeled as order of events, not as exact time.
- **GE arithmetic:** filters, blending, dither. PPSSPP's software renderer is almost exact in 16 bits, but not in everything. Some tests need tolerance.
- **CPU cache:** PPSSPP ignores it. The plugin's cache errors would not be seen.
- **The plugin does not run inside PPSSPP:** it uses Adrenaline's systemctrl and kernel calls. What can be done: replay in PPSSPP the lists the plugin left (including its own lists) and compare with what the console showed.

## Design

### A. Test battery on the console (geptest)
- **An Adrenaline EBOOT** (`ux0:/pspemu/PSP/GAME/GEPTEST/EBOOT.PBP`) that loads the `.prx` files from pspautotests/gpu one by one, plus own tests. The output of each test goes to `ms0:/geptest/<test>.txt` and, if applicable, to a `.bmp` screenshot.
  - **Option A** (decided at implementation): recompile the tests with a modified `common` to write to a file.
  - **Option B:** redirect standard output from the loader.
- **Safety:**
  - Each test has a watchdog: `sceGeListSync` with a time limit, and then `sceGeBreak` and reset. A hung test is noted as "CUELGA" and does not stop the others.
  - The results are written in sync, so a shutdown keeps what was done up to that moment.
  - The tests that can hang Adrenaline go at the end and are requested separately.
- **Own tests**, for the problems that have already appeared:
  1. VRAM map: write and read by CPU, GE, transfer and texture at 0x04000000–0x047FFFFF.
  2. Alpha write according to stencil, mask and format.
  3. TEXFLUSH and TEXSYNC when reading what was just drawn.
  4. RAM↔VRAM and RAM↔RAM copies with different widths and alignments.
  5. Lists with context, enqueue at the front and stall.
  6. Order between lists and flips (timestamp per event).
  7. Drawing to RAM: confirm that the GE cannot.
- **Flow:** I upload the EBOOT through the relay. You open it once from Adrenaline. It runs everything by itself (a few minutes) and the relay downloads `ms0:/geptest/*`.

### B. Long recording in the plugin
- **"Record N frames" mode** (300 by default), lightweight. For each list it saves:
  - address, stall and times;
  - a summary of each command (hash and state changes);
  - the header of each draw, the plugin's decisions, the flips with their time and what happened between them (for example, the two lists per flip that made the door flicker).
- **Full dump:** every K frames (for example 30) a full `.gpd` is added, with data and VRAM.
- **Memory and writing:** buffer in the free RAM (0x0AA00000–0x0AF00000), written by the log thread.
- **`gpd.py`:**
  - `linea` draws the timeline (lists, flips, swaps, copies) and warns about odd patterns.
  - `ppsspp` accepts several consecutive frames (a `.ppdmp` with several DISPLAY) to see a sequence and not just one frame.

### C. Vita profile in PPSSPP (the intermediary)
- **PPSSPP fork at a fixed commit.** The changes live as patches in the repo (`tools/ppsspp-vita/*.patch`) and are enabled with a headless option (`--vita`).
- **Each adjustment is tied to a failing test from A.** Candidates (to be confirmed with data):
  - 2 MB VRAM with the mirrors measured on Adrenaline;
  - error or hang when enqueuing with context;
  - order of lists and flips according to what was recorded in B;
  - Adrenaline memory: 0x0A000000–0x0B000000, flash0, scratchpad.
- **Runner `vitamodel.py`:**
  1. Runs the battery in PPSSPP with and without `--vita`.
  2. Compares it with the console results and with the real PSP.
  3. Builds an HTML report per test (PPSSPP's `frametests.py` already does something similar for frame dumps and serves as a base).
- **Goal:** that all the relevant tests match.
  - From there, `gpd.py ppsspp --plugin` can also include the plugin's own lists, which cannot be done today.
  - Each console dump is replayed in the model and compared pixel by pixel with what the Vita showed.

### Proposed order and estimated effort
1. **A with the 112 existing tests**, without adding anything. It measures how far PPSSPP is from the Vita; the distance may be small. Estimated: 1 session.
2. **Own tests of A** for the known problems. Estimated: 1 session.
3. **B, long recording.** Estimated: 1 session.
4. **C, Vita profile**: depending on what comes out of 1 and 2.
