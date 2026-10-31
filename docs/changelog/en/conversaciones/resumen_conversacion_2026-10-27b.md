# Conversation summary of 2026-10-27 (night): offload to the Vita's GPU → dev57

To continue in a new conversation. Read `contexto_ia.md`, `errores_conocidos.md` and
`analisis_offload_gpu_vita.md` first.

## State at close (2026-10-27, 01:10)
- **Commit `2c4e937` (dev57) on `origin/dev`**: rule "skip stencil-only draws (565)", `tools/carga_ge.py`, test
  `test_sin_efecto` (150 checks), `gpd.py` with the new option. gepresent stays at **v9** (unchanged).
- On the Vita: `ux0:/pspemu/seplugins/ge_patch.prx` = dev57 (md5 `3ef94a99…`). In `~/Downloads/ge_patch.prx`: dev57.
  **The lab (`~/Documents/gepatch-lab`) was left at dev56.3**: the folder was not connected; copy
  `~/Downloads/ge_patch.prx` to `gepatch-lab/memstick/seplugins/`.
- The Mac's clone (`~/Documents/GitHub/NativePatch`) has `origin/dev` up to date (fetch done) but the working copy
  is still at dev49 with old changes: the user must run `git stash` (or discard) and `git checkout dev && git pull`.
- GT options on the Vita: the menu left "Skip stencil-only draws (565) = no" saved in run B.
  **Set it back to "yes"** (menu → that option → "Save options for this game"), or delete the line `sin_efecto=0` from
  `ux0:/pspemu/gepatch_UCUS-98632.txt`. GT's profile is still on "clock + Allegrex" (`perfil=2`, it worked without closing
  Adrenaline): to measure fps, set it to "no".

## What was done (in simple terms)
1. **User's question:** can part of the GE commands be sent to the Vita's GPU? **It is not worthwhile.** The GE
   draws into a memory the Vita cannot see; everything 3D depends on the z-buffer that lives there. Copying that memory
   does not work either (5 ms per copy, several per frame). Only the 2D "tail" of the frame (HUD) could move to the Vita,
   and it is worth 1–2 ms. Details in `analisis_offload_gpu_vita.md`.
2. **New tool `tools/carga_ge.py`**: estimates how many pixels each draw of a capture fills and separates what
   uses z from what does not.
3. **Finding:** GT draws 4 full-screen rectangles per frame that only write stencil; on GePatch's 565 buffer
   they do nothing. **dev57 skips them**: GT at 2x goes from 24.6 to 22.4 ms of GE per frame and from 34.4 to 37.8 fps.
4. Measurement logs in `vitabridge/etapa5_dev57.1/` (A with the rule, B without the rule).

## What was learned
- A draw with masked color costs the GE less than a clear: 4 screens of 960x544 = ~2.2 ms (not 3.5).
- GT draws the HUD in 3D with orthographic projection (not in through mode): the "2D" criterion in the analyses must be
  "without z test", not "through".
- Estimated fill per frame: GT 3 clears + 3–7 screens of 3D; SH 4 clears + **22–24 screens of 3D with z** (which is why
  SH does not reach 30 fps at 2x: it is pure fill); RR 2 + 5.
- `perfil=2` (Allegrex counters) works on Adrenaline 8 ("each measurement costs 15.44 cycles").

## Proposed next tasks (one per conversation)
1. **1.5x + sharp upscale in gepresent** (precompiled GXM shader): the real way to move fill to the
   Vita's GPU. Before that, test GT with `escala=3` and vpjuego (already proposed).
2. Large image in the game's format (5551 GT / 8888 SH): flashlight and dashboard (V1, V2). Note: it turns off the dev57 rule.
3. Plan A step 2 (shadow walk on the ARM), once the GE stops being the limit.
4. Recapture the collection with dev57 and run `carga_ge.py` on SH and RR in case there is more fill with no effect.
