# What NativeDraw does not draw yet

The rendering techniques of PSP games that NativeDraw does not support, or supports only slowly. For each one: what happens
today, the reason the log gives (`translator: result at frame N: <reason>` and the `translator: HD frames` line), the test
that already covers it and where it was seen. It is the list to design support from, one item at a time, with the tests
that already exist:
- **NPTEST** (`nptest.md`, `tests/nptest/`): a PSP program that draws one technique per scene. A scene with
  `expected_reason` `?` or a reason is a technique not done yet; once supported, its expected reason becomes `ok`.
  NPTEST is 2D: items marked "needs a 3D scene" have no scene yet.
- **Saved frames and the translator bench** (`tools/perf/data/*.bin.gz`, `tools/perf/tr_gv.sh`, `tools/test_ci.sh`): real
  frames run through the real translator on the PC. `tr_gv.sh` compares the triangles of the CPU path and of the GPU path:
  the way to add vertex work to the GPU without changing the image. A new technique needs a frame of a game that uses it
  (a L+R+SELECT dump, `dumps.md`).

Vertex counts per frame come from `tools/perf/data` (God of War Ghost of Sparta combat, Ridge Racer race and crowd).

## 1. The frame falls back to the normal image
The whole frame is shown at PSP resolution (or a draw to an intermediate image is left out, if nothing needs it).

| Technique | Reason | NPTEST scene | Seen in |
|---|---|---|---|
| Points | `primitive` | `points` | - |
| Bezier and spline surfaces | record incomplete (`curves`) | `bezier`, `spline` | Puzzle Bobble, LocoRoco (2D rectangles) |
| Primitive "keep previous" | `primitive` | `primKeep` | - |
| Morphing (several vertex sets blended) | `vertex` | `morph` | - |
| Weights in through mode | `vertex` | `weights` | - |
| Vertex positions over 4096 | `vertex_out` | `offscreen` | Ridge Racer crowd (late lists) |
| DXT1, DXT3, DXT5 textures | `format` | `dxt1`, `dxt3`, `dxt5` | - |
| 16 and 32-bit palette indices (T16, T32) | `format` | `t16`, `t32p` | - |
| Texture functions with no equivalent | `texture_function` | `decal`, `tfxBlend`, `tfxAdd` | - |
| Color test that changes the image | `color_test_or_logic` | `colorTest` | Ridge Racer (`fails/003`) |
| Logic operations | `color_test_or_logic` | `logicOp` | - |
| Fixed blend colors other than 0, white or a gray pair | `blend` | `fixGray` | God of War (glow chain, `162000`, fixed color `b2b265`) |
| ABSDIFF blending | `blend` | `blendAbs` | Dragon Ball Z: Tenkaichi Tag Team |
| Destination alpha in the blend | `blend` | `blendDst` | Wipeout, God Eater |
| Doubled alpha mixed with plain alpha factors | `blend` | `blend2x` | - |
| Color mask by bits, not whole channels | `mask` | `pixelMask`, `blueAlpha` | Split/Second, DiRT 2, OutRun |
| Alpha test whose mask the shaders cannot compare | `mask` | `alphaMask` | God of War menu (`162000`) |
| An intermediate image read in another format | `bad_texture` | `cast565`, `castT8`, `castT16` | Burnout Dominator, SOCOM, Syphon Filter, Resistance |
| Depth read as a texture | `bad_texture` | `depthTex` | Me & My Katamari, Suikoden, Kurohyo 2 |
| A palette drawn by the GE | `bad_texture` | `paletteRt` | Burnout Dominator (lens flare) |
| An image read while drawing on it, when it shares its surface | `bad_texture` | `selfBlur`, `rtReread` | - |
| Intermediate images that do not fit in the two surfaces, or share memory at the same time | `other_target` | `overlapRt`, `strideGap` | God of War (a full-screen `091000` with the glow chain) |
| Over 96K vertices per surface, 64K in one GXM draw | `limit` | `manyDraws` | God of War combat needed ~75K (raised in 0.0.25.20) |
| The CPU reads or writes VRAM | normal image or wrong | `cpuRead`, `cpuPixels` | Wipeout Pure, Dangan Ronpa |

Capture side (the frame is dropped before the translator): a conditional jump by bounding box is assumed not taken
(`bboxJump`, Tekken); a VRAM sublist with garbage after a God of War fight (`list_too_long`, `TODO.md`).

## 2. Drawn, but the CPU does the vertex work
The GE's vertex work (transform, lights, UV) is done by the CPU when the GPU path (`nd_v3`, `nd_v4`, `rgVGpu` in
`record.c`) does not take the draw. It is right but slow: in God of War combat the CPU vertices cost ~250 ms per HD frame
(0.0.25.23). The GPU path today: types `0x19F` and `0x1BE` with no lights (`nd_v3`), `0x142` and `0x143` with one
directional light or UV from the normal (`nd_v4`); vertices in RAM (`rgVLater`) or in VRAM with a mirror copy (`rgVNow`);
no indices, triangles only, a scissor that takes the whole image, smooth shading.

| Technique | Vertices per frame | Test |
|---|---|---|
| Skinning (weights 1 to 4, bone matrices): `0x7BE`, `0xC7BE`, `0x87BE`, `0x87B9`, `0x47B9`, `0x7B9` | God of War combat ~31,000 (half of the frame) | saved frame `gow_combat`; needs a 3D scene |
| More than one light, point and spot lights | God of War skinned vertices with 3 directional lights ~5,000 | needs a 3D scene (`cube3d` has one light) |
| Vertex color with lights (`nd_v4` has no color) | - | needs a 3D scene |
| Other vertex types: `0x102`, `0x100`, `0x11E`, `0x19D`, `0x183` | Ridge Racer ~700 | saved frames `rr_*` |
| Indexed draws | - | `prims` (2D); needs a 3D scene |
| UV generated by projection from the position | God of War shadows on the floor (`0xC0` = 1) | saved frame `gow_combat` |
| A scissor that cuts the image, flat shading | Ridge Racer: shadows and the rear-view mirror area | saved frames `rr_*` |

## 3. State with no code (ignored: the image may differ)
From NPTEST, scenes with no code behind them: forced mip level (`mipLevel`, Tactics Ogre keeps a font there), UV offset and
scale (`uvOffset`), `dither`, fog in 2D (`fog`), flat shading in 2D (`flat`), the order of a rectangle's corners
(`rectCorners`), odd rectangles (`rectOdd`), a buffer wider than the texture (`texStride`), odd widths (`texNpot`), linear
filter at the edge of an atlas tile (`atlasBleed`), drawing into a 4444 image (`fb4444`).

## 4. Approximations
- Destination alpha is not computed (not written) in blending.
- Fixed blend colors of at most 1/255 count as 0.
- An intermediate image alone in its surface is read as the whole surface: wrap and clamp stop at the surface's edge, not
  at the texture's (images that share a surface are read through just their part).
- Images that only shrink and blur stay at PSP size (`keep_hd` to compare).
