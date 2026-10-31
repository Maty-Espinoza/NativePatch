# Stage 0 of architecture B: results (2026-10-28) — CLOSED

First stage of `arquitectura_dibujo_en_vita.md` (§6): measure, **without the Vita**, how much work the Vita would have if
it drew every frame of the game itself. Done on the Windows PC (Claude's VM over `Documents\GitHub` and `Downloads`).
**No changes to the plugin, the Vita or the lab.** No new GePatch version. `tools/carga_vita.py` went into
the stage 1 commit.

## Tool: `tools/carga_vita.py`

```
python3 tools/carga_vita.py VOLCADO [VOLCADO...] [--detalle]
```
- Uses `gpd.py` and `carga_ge.py` (3D positions like the GE). Standard library only. ~1 s per dump.
- Counts **only what the game sent** (original value of each command; the plugin's patches do not count).
- **Unit = the whole dump, divided by the flips.** It is not cut at the flip because **SH draws almost all of its
  frame after the flip, in the same list** (`L L F` and then ~1000 draws). GT: 1 list and 1 flip; RR: 18 lists.
- In an active dump the game sees its VRAM in the fake VRAM (0x0A000000–0x0A3FFFFF): there it is counted as VRAM. In B
  that data would be in the real VRAM, which the ARM cannot see.
- Groups of changes (as GXM would charge for them): **render target** (new scene), **program/fixed state** (vertex type,
  blending, z, stencil, alpha, lights on…), **texture** (address, size, format, CLUT, filter), **constants**
  (viewport, colors, lights, fog, scissor) and **matrices**. VADDR/IADDR/BASE/CALL do not count as a change.
- "Batches" = draws that cannot be merged with the previous one (something changed, even a constant).
- Origin of each texture in the VRAM: rt or z drawn earlier in the frame, TRANSFER of the frame, fb that the game draws at
  another time (the fb manager would have it), **fixed in the dump** (the same at the start and at the end: uploaded
  earlier) or **unknown** (changed without the GE writing it).
- "Mirror" = KB of VRAM that the Vita would need (lists, vertices, indices, CLUT and textures that the GE did not draw).
  With several dumps, "same" = % of that mirror identical in the previous dump of the same game (same address).
- Guard band: 3D triangles with some vertex outside 0..4096 (at 1x and with the scale at double size and the same offset);
  those that cross the near plane are counted separately (they are clipped, not discarded). Without bones or morph.

Full results: `Downloads\etapa0\carga_vita_{GT,RR,SH}.txt`. Dumps used: the 90 in
`vitabridge\recibidos` (GT dev50–dev60.1, RR dev39–dev59.12, SH dev28–dev58.2, 5 passive SH ones). **There are no dumps
of LocoRoco.**

## Results (per frame; median and range of the dumps with more than 100 draws)

| | GT (21 dumps) | RR (25) | SH (28) |
|---|---|---|---|
| Draws | **9100** (314–15,157) | 1100 (546–3058) | 316 (233–1396) |
| Of them 2D | 3 | 111 | 58 |
| Commands | 44,000 (up to 76,000) | 9,000 (6,500–15,900) | 9,000 (6,400–27,300) |
| Render targets (scenes) | **1** | 5 | 9 (8–14) |
| Draws with program/fixed state change | 133 (31–261) | 57 | 93 |
| Draws with texture change | **5300** (up to 8600) | 117 | 136 |
| Batches (not merged with the previous one) | = draws with expensive change (4800–8700) | 170–270 | 150–480 |
| Distinct textures | 400–700 (460–1100 KB) | 20–124 (0.6–1.4 MB) | 17–109 (2.7–4.6 MB: their own 8888 rts) |
| Textures in the VRAM | 180–490, **all fixed** | 13–100 fixed + 2–3 rt | **2–6, all frame rts** |
| Unknown textures | 0 | 0 | 0 |
| Vertices (KB) | 550–1080 (without repeats 500–970) | 80–350 | 285–3100 (without repeats 240–780) |
| Indices | not used | not used | 5–21 KB |
| CLUT loads | 576 (33–1245) | 111 | 167 (154–437) |
| TRANSFER | 220 (3 KB) | 0 | 0–1 (up to 256 KB) |
| **Commands in the VRAM** | **3200–10,500** (~20% of the list: its sublists) | 0 | 0 |
| **Draws with vertices in the VRAM** | **1400–4900** (~35%) | 0 | 0 |
| **VRAM mirror needed** | **370–700 KB** | 70–460 KB | **0** |
| Mirror same as the previous dump | 77–100% (median ~83%) | 74–100% | — |
| 3D triangles | 30,000–60,000 | 2,200–8,600 | 700–79,000 |
| Discarded by guard band 1x / 2x | 140–800 / 300–2100 | 20–80 / 100–270 | 0–490 / **26–1770** |
| Formats | clut4 swizzle (almost all), dxt1, dxt5, clut8 | clut4/clut8 swizzle, 5551, 8888 | clut4/clut8 swizzle, 8888 (rt), 5650, 4444, **clut32 from the VRAM** (depal-like effect) |

Primitives: almost all **triangle strips**; GT ~60 rectangles, RR ~20 and ~33 fans, SH 2D sprites. No bézier
or spline in these games.

## Answers to the stage 0 questions

**1. How much work per frame would the Vita have?**
- **SH and RR: little.** 150–480 batches per frame, < 100 program changes: they fit comfortably if a draw costs ≤ 2 µs
  (stage 1: it costs ~2 µs with a new texture).
- **GT: much more than estimated.** The document assumed ~4000 draws; there are **~9000 (up to 15,000)**, almost all small
  strips, and in ~5300 the texture changes. Program changes are few (~130). What decides it is **how much a draw that only changes the
  texture costs in GXM** (stage 1: ~2 µs; for GT it stays as a warning, see `etapa1_banco_gxm.md`).
- Vertices: GT 0.5–1 MB per frame and SH up to 3 MB (0.8 MB without repeats). Copying them by DMA (7.2 ms/MB) would cost
  4–7 ms in GT: they have to be **read directly from the PSP RAM with the GPU** (stage 1: it works at the same speed).

**2. How many textures in the VRAM have no known origin?**
- **None "unknown"** in 90 dumps (1-frame window: none changed without the GE drawing it).
- But **GT and RR keep their fixed textures in the VRAM** (GT 180–490, RR 13–100 per frame), uploaded before the frame.
  In B they cannot be read: they need a mirror. SH, on the other hand, only reads its own render targets from the VRAM: **for SH
  no mirror is needed**.

**3. New finding: GT keeps lists and vertices in the VRAM.**
- ~20% of GT's commands (its sublists, at 0x0A1BC000+ of the fake VRAM) and the vertices of ~35% of its draws
  are in the VRAM. In B they would be in the real VRAM: **the capturer could not walk GT's lists without a mirror**.
  [deduced: in B the game receives the real VRAM from `sceGeEdramGetAddr`]
- The mirror GT needs is 370–700 KB per frame; between different dumps of the same game 77–100% is the same
  (data loaded with the track). Within a frame nothing changes.
- Copying it whole with the GE every frame: ~1.6 ms of GE (2.3 ms/MB). Options to evaluate in stage 2: full mirror
  at load + periodic verification; per-frame mirror of the sublists only (~40 KB, ~0.1 ms); or, only for games
  like this, a "fake VRAM" for data (without fb) that would require touching lists (which is what mode A does today).

**4. Guard band.**
- At 1x (what B would do): GT discards 140–800 triangles, RR 20–80, SH 0–490. B has to discard exactly those.
- At 2x (current GePatch): GT 300–2100, RR 100–270 (the known gaps in the road), **SH 26–1770**. The SH figure at 2x
  was not recorded as an error: candidate for gaps in SH with mode A (it was not added to `errores_conocidos.md`).

**5. T3 (PPSSPP `.ppdmp`): DONE.**
- `gpd.py ppsspp` generates valid files (header `PPSSPPGE` version 6, zstd). PPSSPPHeadless draws a passive SH
  `.ppdmp` the same as the Vita (`diagnostico.md`).
- Generated: `Downloads\etapa0\{UCUS-98632_010777, ULJS-00001_003603, ULUS-10450_005501, ULUS-10450_002052}.ppdmp`
  (for active ones, the game's commands are replayed with its fake VRAM as VRAM). GT weighs 75 MB (70,000 commands).
- **Confirmed by the user (2026-10-28):** `~/gepatch-lab/ppsspp/build/PPSSPPSDL
  /mnt/c/Users/spect/Downloads/etapa0/ULJS-00001_003603.ppdmp` in WSL opens the dump and shows the image in the emulator.

## Consequences for the plan (applied in `arquitectura_dibujo_en_vita.md`)
- §5: "GT ~4000 3D draws/frame" → **~9000 (up to 15,000)**, ~5300 with a new texture, ~130 with a new program.
- §6, stage 1 criterion: measure separately (a) draw without changes, (b) draw that **only changes the texture** (and the
  palette), (c) draw with a new program, (d) new scene.
- §3.5: add as a data origin in the VRAM the **fixed textures, sublists and vertices** that the game uploads to the
  VRAM before the frame (GT, RR). The mirror is mandatory for GT and RR.
- §3.6 vertices: read them from the PSP RAM with the GPU instead of copying them (except those in the VRAM, which go to the mirror).
- SH is the best candidate for the first 3D test of B: no mirror, few draws, its textures in the VRAM are its rts.

## Limits of the measurement
- One dump = ~1 frame: "fixed in the dump" does not prove that the texture does not change between frames. The "same" between
  dumps compares different moments (sometimes another track or scene).
- In the active dumps, the fake VRAM at the end may include copies from the plugin; no game texture
  was seen to be affected.
- GT and RR have menu/loading dumps (1–2 draws, or RR 006477 with 1 render target): they are excluded from the table for having
  ≤ 100 draws.
- The guard band at 2x assumes the same offset; GePatch may move it.
