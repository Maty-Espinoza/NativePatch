# Conversation summary of 2026-10-26/27 (stage 4 and start of plan A)

To continue in a new conversation. Read `contexto_ia.md`, `errores_conocidos.md` and
`plan_a_recorrido_vita.md` first.

## State at close (2026-10-27, 00:10)
- **Commit `e1c3ccf` (dev56) on `origin/dev`** of `Maty-Espinoza/NativePatch`: everything from this conversation.
  GePatch **0.2-dev56** + **gepresent v9**.
- On the Vita: `ux0:/pspemu/seplugins/ge_patch.prx` = dev56 (md5 `0fefaee5…`); `ux0:data/gepresent/gepresent.suprx` = v9
  (md5 `20f3c01d…`). In the lab and in `~/Downloads/ge_patch.prx`: dev56.
- Options files on the Vita: GT (`gepatch_UCUS-98632.txt`) and SH (`gepatch_ULUS-10450.txt`) with `perfil=1` and
  `espera=3`; RR and LocoRoco with `escala=3`. GT also has as known options `comprobar=1` and `tex_fb=0`.
- Vita build chain set up in the container (lost when it closes; recipe in `plan_a_recorrido_vita.md`).

## What was done (in simple terms)
1. **GePatch uses less processor** when reading the game's instructions:
   - GT: 15–27 ms → 8–10 ms per frame; SH: 10.3 → 6.4 ms.
   - How: the memory that remembers the parts of the list that do not change had 1 slot per part and they overwrote each
     other (90 of 434 in GT); now there are 4 slots and what is needed fits in a single memory line; simple
     instructions take a short path; the change table is only consulted when needed.
2. **Optional timer** (`perfil=1`) that says where GePatch's time goes; `perfil=2` (Allegrex counters)
   not tested yet.
3. **GT, black areas after the first curve: fixed** (finding by the user): they were draws that GePatch canceled with
   "Textures from framebuffer = skip"; GT stays with "read memory".
4. **Captures (dumps): from 4–9 s with the game stopped to 0.4–0.6 s.** The Vita (gepresent) receives the log
   and the captures through a "mailbox", copies them by DMA to its own RAM and writes them to the card in the background
   (idea by the user).
5. **`espera` option**: the capture is taken N seconds after releasing L+R+SELECT (in GT, L changed the camera).
6. **Collection of test captures** (`tools/coleccion.py`, `tools/coleccion.txt`; result in
   `vitabridge/coleccion/`).
7. **Document `errores_conocidos.md`**: all the errors with status, cause or theory and possible solution.

## What was learned
- **At 2x the limit is the graphics chip (GE)**, not the processor: GT with the GE busy 85–88% (21–30 ms per frame,
  ~26–31 fps); SH 43–57 ms. Lowering GePatch's CPU cost barely raises the fps at 2x.
- SH's flashlight and GT's dashboard fail because **GePatch stores the image in 16 bits with no mask channel
  (stencil)**: SH draws in 8888 with stencil; GT in 5551 with stencil.
- Drawing with the Vita's GPU instead of the GE is possible in theory, but it means writing a graphics emulator (months).
- To update gepresent you have to **close Adrenaline from the LiveArea** and open it again.
- The card writes at ~4–6 MB/s.
- The shadow in GT's intro video comes from the game (same without GePatch).

## Proposed next tasks (one per conversation)
1. **Relieve the GE without losing on-screen resolution:** 1.5x + upscale with a filter that recovers sharpness in
   gepresent (GXM shader). Before that: test GT with `escala=3` and vpjuego to see how much the GE drops.
2. **Large image in the game's format** (5551 for GT at no cost; 8888 for SH with scale ≤ 1.5x): SH's flashlight and
   GT's dashboard (`errores_conocidos.md` V1, V2).
3. **Plan A, step 2:** "shadow" walk on the Vita (same C code compiled for the ARM, writing nothing) to measure
   its real time; it makes sense once the GE stops being the limit (scale < 2).
4. Render targets at full scale in the free VRAM (reflections in GT; RR's rearview mirror only if the user asks for it).
5. Recapture the collection with the current version (each scene with its passive).

## Where everything is
| What | Where |
|---|---|
| Code | `origin/dev` commit `e1c3ccf` (the Mac's clone is behind: pull) |
| Logs of each test | `~/Downloads/vitabridge/etapa4_dev56.3/`, `etapa4_dev56.4/`, `etapa4_dev56.5/`, `etapa4_dev56.6/`, `etapa4_dev56.7/`, `etapa4_dev56.8/`, `etapa4_dev56.8_v8/`, `etapa4_dev56.8_v9/` |
| gepresent v5–v9 | `~/Downloads/vitabridge/gepresent/` |
| Intermediate patches (already in the commit) | `~/Downloads/vitabridge/gepatch_dev56.*.patch` |
| Reviewed images | `~/Downloads/vitabridge/revision_dev56.3/`, `revision_dev56.4/`, `revision_dev56.8/` |
| Collection | `~/Downloads/vitabridge/coleccion/` (`salida_vieja/` can be deleted) |
