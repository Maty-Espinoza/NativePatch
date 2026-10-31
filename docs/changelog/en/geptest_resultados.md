# GEPTEST: results on the Vita

Complements `modelo_vita.md` (part A of the design).

## Battery results (2026-10-22)

**How it was run:** `tools/geptest`, with the 112 pspautotests/gpu tests recompiled and run in a single process by the runner (about 0.6–0.9 s per test on the Vita).
- There are 7 tests whose result changes because of the harness itself (the recompilation or the execution order): `dither`, `clipping/homogeneous`, `simple` and `signals/*`. In them PPSSPP, run the same way, strays from the PSP just as the Vita does.
- That is why the fair comparison is Vita against PPSSPP with the same binaries and the same order.

**Text (what each test prints):**
- Vita = real PSP in 101 of 112.
- PPSSPP (same harness) = PSP in 101.
- Vita = PPSSPP in 103.

**Real differences:**
- **`ge/edramswizzle` (VRAM mirrors):** the Vita is identical to the PSP in all 389 lines. PPSSPP differs in 229: here the emulator is the one that is wrong. It is the first thing the Vita profile has to fix.
- **`exact/pixel`:** a checksum of a dither or texture cache case changes. A different case changes on each run, so it seems to depend on timing, not on a fixed behavior.
- **`signals/*`:** the Vita is closer to the PSP than PPSSPP is. The rest of the differences in this group come from the execution order.

**Tests that bring down Adrenaline in a single process** (with one process per test they finished fine):
- `transfer/invalid`: writes beyond the VRAM (0x04800000), where on the Vita there is Adrenaline memory. It goes at the end of the list.
- `ge/context`: clashes with the GE context restoration done by the runner.

**Images:** the Vita and PPSSPP (same harness) are equal in all the drawing tests: `texmtx`, `complex`, `rendertarget` and `primitives`. The differences with the PSP captures come from the recompilation, not from the Vita.

**Conclusion:**
- In what these tests cover, the Vita's GE under Adrenaline behaves like a PSP's: blending, stencil, textures, filters, clipping, block copies and VRAM mirrors.
- The differences that cost effort are not the GE's, but Adrenaline's as a system:
  - 2 MB VRAM;
  - lists with context freeze the game;
  - LoadExec from a game closes Adrenaline;
  - writing beyond the VRAM closes Adrenaline.
- For the model:
  - PPSSPP's Vita profile needs, at a minimum, the real PSP's VRAM mirrors;
  - the system risks are documented, not emulated;
  - the next step is the own tests of point A (TEXSYNC, RAM↔RAM copies, alpha writing, order between lists and flips).
