# PPSSPP-Adrenaline model: GePatch running inside PPSSPP

Complements `modelo_vita.md` (2026-10-22): corrects its limit "the plugin does not run inside PPSSPP". It now runs from its `.prx`, unchanged.

**PPSSPP-Adrenaline model** (`tools/ppsspp-vita/adrenaline.patch`, branch `vita-model` on hrydgard/ppsspp 1adf3c5; enabled with `PPSSPP_ADRENALINE=1`):
- Loads the plugins from `ms0:/seplugins/game.txt`.
- 64 MB of RAM with a 32 MB user partition.
- `sctrlHENFindFunction` also finds the system functions (HLE stub) and `sctrlHENPatchSyscall` redirects the imports of the user modules (the plugin's follow the original, as in the CFW).
- `sceGeEdramSetSize(4 MB)` = 0x80000104, `sceGeGetList` (NID 6.60), `sceKernelInitFileName`.
- Headless screenshots also leave `X_nativo.bmp` (what the Vita shows: 960x544 from 0x0A400000 if DRAW_NATIVE ≠ 0).
- Warning (or stop with `PPSSPP_ADRENALINE=estricto`) on a list with context enqueued from a plugin.
- First run: GePatch dev40 starts, finds and patches all its functions, writes `ms0:/ge_patch.log` and draws the native image.

**Verification against the Vita: GEPVM scene** (`tools/geptest/escena`):
- Deterministic homebrew: render to texture, alpha blending, palette, block copy, 3D cube with depth and a box drawn by the CPU.
- At frames 5, 30, 60, 90 and 120, `kcap.prx` (kernel) saves to `ms0:/gepvm/` the native image, the PSP's and DRAW_NATIVE.
- It is run the same on the Vita and in the model (same `ge_patch.prx`) and `comparar_vm.py` measures the difference per frame, with an HTML report.
- GePatch dev40 in the scene: the cube with render to texture does not appear (render targets disabled → draws omitted), the render target copy is missing and the CPU box (fb 8888) comes out as dotted lines. They are real bugs: the Vita shows exactly the same.

## Result of the first comparison (2026-10-22)
- **The 5 native images from the Vita and from the model are byte-for-byte identical** (frames 5, 30, 60, 90 and 120), and so is `ge_patch.log`, except for the measured times.
- To get there, two things in the model were adjusted after the first run on the console:
  - Adrenaline starts at 333 MHz (bus 166); PPSSPP at 222.
  - VRAM mirrors: on the PSP and the Vita the CPU sees 0x04200000 and 0x04600000 permuted on all accesses; PPSSPP only on 16-bit ones. It now applies to all of them (`edramswizzle` goes from 229 differing lines to 0) and GePatch's `probeVram` gives the same messages as on the Vita. For that the model uses the IR interpreter, because the JIT reads memory directly.

**Consequence:** the GePatch bugs seen in the model with the scene can be debugged on the Mac, without the console.

**Next:**
- Build the same patch in the version with an interface for the Mac and run games watching the native image in real time.
- Add cases to the scene for each known bug (SH's 1024-wide passes, Ridge Racer's environment map) and use it as a regression test.

## dev41: GePatch fixes made with the model (2026-10-22, not yet tested on the Vita)
- **Scale 1 render targets in the z-buffer VRAM** (option "Render targets a escala 1", on by default): an fb with width under 480 that is not the screen is drawn at scale 1, in the game's format, at 0x04100000, only if since the last flip nothing used the depth of the big buffer. When leaving it, the FRAMEBUFPTR/WIDTH words of the next fb become a CALL that copies it to its place in the fake VRAM; the textures and copies that read it go to the fake VRAM. In GEPVM the cube appears again. Candidate for Ridge Racer's environment map (256 fb).
- **Small copies to the screen** (up to 8 per frame): they are drawn at double size in the big buffer at their place in the list, reading from RAM. The plugin's original copy (`txEmitOrig`) now uses the fake VRAM: before, a copy with real VRAM addresses wrote over the big buffer.
- **CPU drawings** also in displayed fbs of 480/512 (before only SH's 1024 ones), in 565 and 8888, with the exact box (before it went with a black margin) and skipping the areas where game copies landed.
- GEPVM v2 draws the CPU box with the address from `sceGeEdramGetAddr`, like the games. v1 wrote to a fixed 0x44000000, that is, in the real VRAM over the big buffer, which no plugin can avoid.
- Result in the model: the native image differs from the scene without GePatch in ~2.5% of the pixels (edges due to the different resolution and bands due to the 565 of the big buffer).
- **Model bug found along the way:** sysclib's `memset`/`memmove` in PPSSPP returned 0 instead of the destination; GCC uses that value and the clearing of the CPU drawing failed. Fixed in `adrenaline.patch`.
