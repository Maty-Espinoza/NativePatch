# Conversation summary of 2026-10-26: Vita hardware tests

To start the next conversation (analysis and design of the new GePatch implementation).
Read together with `sugerencias_nueva_implementacion.md`, which has the conclusions and the proposal.

## Starting point
- Before this conversation, KBENCH had measured that the Vita's ARM can walk lists in parallel
  (shared memory; Kermit unusable).
- The user proposed to **move the VRAM into the Vita's memory and leave in the PSP's VRAM only what is being drawn**:
  paging the VRAM.
- A test plan was drawn up in two groups (`pruebas_hardware_vita.md`):
  - **A**, low cost, no authorization needed;
  - **B**, with authorization.
- In addition, **route E** (enlarging the VRAM) was treated as a proof of concept.

## What was done
- **VPROBE** (homebrew, `tools/vprobe`, versions 1–8): VRAM map, copy costs, paging, GE fill,
  2x 3D, bus contention, exploration of route E and the PSP side of B4.
  - Test errors fixed along the way:
    - the pspgu scissor drew 1 px;
    - the checksum included the padding;
    - the line width of the rt was wrong;
    - one color gave 0 in 565;
    - the log went to a folder the relay does not watch;
    - the low RAM search closed Adrenaline;
    - the XOR of the mirrors was wrongly assumed.
- **censo.py** (A2): rt per frame in 59 dumps of RR, SH and GT.
- **Source reading** (A3): SceCompat, `sceCompatAllocCdramWithHole` and the Adrenaline hooks.
- **vpjuego** (B1, game plugin): busy GE, free CPU and fps in RR, LocoRoco, GT and SH, with and without GePatch.
  - v1 miscalculated the GE per frame (100 times too large) and the CPU calibration.
- **vpcdram** (B4, Vita plugin, built without vitasdk): search for the VRAM in the Adrenaline process.
  - v1 only looked at blocks of 2 MB or more.
- B2 was not needed (KBENCH already measured it). B3 and B5 are left for the new implementation.

## Key results
1. **VRAM:**
   - 2 MB of its own memory, faster than RAM for the PSP's CPU.
   - It is not in the PSP's RAM (0x0BE00000 is not the VRAM) nor mapped in the Adrenaline process.
   - The hardware declares 2 MB and forcing 4 MB has no effect.
   - Fixed mirrors: 0x042 = X^0x2040, 0x044 = X, 0x046 = X^0x2040 with bits 5–9 rotated.
   - **Route E closed on both sides.** The copy by flip to 0x0A400000 is still mandatory.
2. **Costs:**
   - Copying 1 MB with the GE or with DMA: ~2.3–2.7 ms.
   - Clearing 960x544: 0.87 ms.
   - Texture stretched in strips: 1.45 ms; with a wide sprite and linear texture: 32 ms.
   - Shrinking the screen: 1.9 ms; enlarging 480x272: 3.3 ms.
   - GE copies to RAM slow the CPU by 10–20%; drawing from the VRAM slows nobody down.
3. **Paging:**
   - Whole screen: 5 ms, discarded.
   - Double rt in the z space: 0.5 ms (128x128), 4 ms (512 KB), 8 ms (1 MB). Only worthwhile for small rt.
4. **Real games (B1):** with GePatch the GE works ~3–4 times more (4 times the pixels). It is the fps limit.

| Game | GE without → with (ms per frame) | fps with GePatch | Free CPU without GePatch |
|---|---|---|---|
| RR | 5 → 22–27 | 20–30 | 29–42% |
| LocoRoco | 1–3 → 3–9 | 60 (drops to 30–55) | 25–47% |
| GT | 8–13 → 25–38 | 24–30 | 3–8% |
| SH | 18–37 per flip → 40–60 | 15–21 | ~0% |

5. **In the games measured, GePatch's cost appears to be real, not wasted logic** (the ratio does not
   exceed 4). SH still needs to be broken down, where the own passes could add up.

## Decisions and proposals
- **Main candidate:** **per-game scale** (1.5x = 2.25 times the pixels) with a final upscale in strips.
  It leaves ~830 KB of VRAM free for rt and widens the margin of the 4096 limit.
- **All copies scaled in strips.** Combine the copy by flip with the upscale.
- **Asynchronous ARM:** moves to phase 4, only for CPU-limited games (GT, SH).
- **vpjuego telemetry inside GePatch.**
- **Adrenaline 8:**
  - L when opening a game disables all plugins, so GePatch's passive mode with L no longer works;
  - enabling and disabling plugins in `plugins.txt` is easy and fast;
  - in SH the GePatch menu does not open, so the options file has to be used.

## State at close
- **Vita:**
  - `plugins.txt` = `vpjuego on`, `ge_patch on`, `kbench off`;
  - original `ur0:tai/config.txt` restored (vpcdram stops loading on reboot);
  - VPROBE 8 in `ux0:/pspemu/PSP/GAME/VPROBE`.
- **Repo:**
  - new `tools/vprobe/` (main.c, juego/, vita/, README), `tools/censo.py`;
  - documents updated in the project and in `docs/`;
  - **no commits** (neither the tools nor the documents).
- **Plugin:** unchanged (dev53).
- **Toolchains:**
  - pspdev works in the assistant's container;
  - for the Vita, vita-toolchain built from source + vita-headers stubs
    (`tools/vprobe/vita/build.sh`), because vitasdk.org is blocked.

## For the next conversation (analysis)
1. Read `sugerencias_nueva_implementacion.md`, `filosofia.md` (revised roadmap) and this summary.
2. Decide the architecture of the new implementation (per-game scale, copies in strips, profiles,
   telemetry, phase 0 on Adrenaline 8).
3. Tests that follow:
   - prototype at 1.5x measured with vpjuego (LocoRoco demo + RR);
   - GE breakdown in SH through the options file;
   - does SH do 2 flips per frame?;
   - GT with versus without after fixing its regressions.
