# NPTEST: synthetic 2D battery

A PSP homebrew (`tests/nptest/`) that draws one technique per scene, for 2 s each (108 scenes, about 3.5 min). It does not
touch the plugins. Each scene shows its number top right and as a bar code on the bottom line. `ms0:/native_test.log`
(on the Vita `ux0:pspemu/native_test.log`, the relay downloads it) gets one line per scene:
`number name expected_reason first_frame hash_frame30 hash_frame119`. The hashes are of the PSP image: they must not change
between plugin versions, or with the plugins off.

Buttons: RIGHT/LEFT next/previous, CROSS stay on a scene (to take a L+R+SELECT dump), START exit.

## Scene groups
| Group | `expected_reason` | Scenes | What it is for |
|---|---|---|---|
| Works today | `ok` | 0, 2–22 | Regression: must be drawn in HD and match the normal image |
| Rejected with a known reason | the reason | 1, 23–50 (bezier, spline, rtReread, manyDraws, manyTex: `?`) | Regression: the frame falls to the normal image with that reason. Scene 1 (`sync`) also lines up the logs |
| No code in the translator | `?` | 51–107 | Discovery: the image and the reason say what happens |

Scene 0 must stay first: it tests state set only in the first lists of the program.

## All the scenes
- **Primitives and vertices:** clear with scissor (`clearS`); triangles, strips, rectangles, 8, 16 and 32-bit indices (`prims`); fans (`fan`); position, color and UV formats, material color (`vertFmts`).
- **3D, drawn:** orthographic sprite (`ortho`); lit, fogged, textured cube with depth and cull (`cube3d`).
- **Textures:** 5650, 5551, 4444, 8888, T4, T8 (`texFmts`); swizzle (`texSwz`); point and linear filter, repeat and clamp (`filterWrap`); palette shift, mask and start, 4 palette formats (`palettes`).
- **Pixel operations:** modulate, replace, TCC, doubling (`texFuncs`); add, subtract, reverse subtract, min, max (`blendOps`); blend factors and fixed 0 or white (`blendFactors`); the 8 alpha test functions (`alphaTest`); scissor (`scissorRect`); 200 layers (`layers`).
- **Targets and copies:** one and two intermediate images (`rtOne`, `rtTwo`); drawing in 565 and 5551 (`rtFmts`, `fb565`, `fb5551`); TRANSFER from RAM and VRAM (`xferRam`, `xferVram`); list sent after the flip (`lateList`); state set only in the first lists (`firstState`).
- **Rejected primitives and vertices:** lines (`sync`), `points`, `lineStrip`, `bezier`, `spline`, `primKeep`; `weights`, `morph`.
- **Rejected texture and pixel work:** `decal`, `tfxBlend`, `tfxAdd`, `dxt1`, `dxt3`, `dxt5`, `t16`, `t32p`; `colorTest`, `logicOp`; gray fixed color (`fixGray`), `blendAbs`, destination alpha (`blendDst`), 2x alpha (`blend2x`).
- **Limits:** target over 512 wide (`wideTarget`), 3 intermediates (`threeRt`), draw on an image already read (`rtReread`), position over 4096 (`offscreen`), 5000 draws (`manyDraws`), 300 textures (`manyTex`), 512 rows (`tallTex`).
- **Depth, stencil and masks:** `depth2d`, `stencilMask`, `stencilAlpha`, `alphaMask`, `pixelMask`, `regionClip`, empty scissor (`scissorEmpty`), scissor on triangles (`scissorTri`).
- **State with no code:** forced mip level (`mipLevel`), UV offset and scale (`uvOffset`), `dither`, `fog`, flat shading (`flat`), rectangle corners (`rectCorners`), odd rectangles (`rectOdd`), buffer wider than the texture (`texStride`), odd width (`texNpot`), linear filter on an atlas (`atlasBleed`), 4444 image (`fb4444`).
- **Images read in unusual ways:** part of an image by address (`subRt`), taller than the screen (`rtTall`), from the previous frame (`rtPrev`), the shown image (`readFront`), the image being drawn (`readBack`), itself (`selfBlur`), `strideGap`, `overlapRt`, format casts (`cast565`, `castT8`, `castT16`, `blueAlpha`), VRAM mirror (`texMirror`), depth (`depthTex`).
- **Palettes:** drawn by the GE (`paletteRt`), in VRAM (`clutVram`), changed with no load (`clutStale`), swapped between draws (`clutTwice`).
- **Data that changes:** texture, copy and vertices rewritten within a frame (`texTwice`, `xferTwice`, `vtxTwice`), sublist patched (`sublist`), texture with 1 texel or all of it changed (`cache1`, `cache2`).
- **List commands:** no vertex address (`vaddrKeep`), conditional jump (`bboxJump`).
- **Copies on the image:** `xferFb`, `xferIntra`, `xferSelf`.
- **CPU and display:** VRAM read (`cpuRead`), pixels written (`cpuPixels`), partial redraw (`noClear`), 30 fps (`fps30`), one image only (`singleBuf`), moved address (`shake`), shown image in RAM (`ramShown`).

## Rare techniques and where they come from
| Scene | Technique | Seen in |
|---|---|---|
| `castT16`, `castT8` | An image read as palette indices, by channel or byte by byte | Burnout Dominator, SOCOM, Syphon Filter, Resistance |
| `paletteRt` | A palette drawn by the GE | Burnout Dominator (lens flare) |
| `depthTex` | Depth as a texture through a VRAM mirror | Me & My Katamari, Suikoden, Kurohyo 2 |
| `cpuRead` | The program reads VRAM with the CPU | Wipeout Pure, Dangan Ronpa |
| `blueAlpha` | The alpha of a 4444 image written in 565 with a bit mask | Split/Second, DiRT 2, OutRun |
| `stencilAlpha`, `blendDst` | Stencil as alpha | Wipeout, God Eater |
| `strideGap`, `overlapRt` | Images inside or over another | Tantalus games, Juiced 2, God of War |
| `xferFb`, `xferIntra`, `xferSelf` | Block copies on the framebuffer | Trails in the Sky, Grand Knights History, Gradius Collection, Colin McRae |
| `mipLevel` | A font kept in a mip level | Tactics Ogre |
| `bezier` | Curves used for 2D rectangles | Puzzle Bobble, LocoRoco |
| `blendAbs` | ABSDIFF blending | Dragon Ball Z: Tenkaichi Tag Team |
| `bboxJump` | Conditional jump by bounding box (the capturer assumes it is not taken) | Tekken |
| `rectCorners` | A rectangle's UVs rotate with the order of its corners | GE rule (PPSSPP docs) |
| `depth2d` | Sprite priority by depth | Heroes Phantasia, Phantasy Star |

Sources: the PPSSPP docs (`psp-hardware/gpu`), `assets/compat.ini` and its issues #13256, #15923, #22482, #15898, #15500, #15888,
#17810. The other scenes come from reading `translator.c` and `capture.c` (states saved and not used, cases with no code).

## On a PC
`sh tools/emu.sh` builds the battery and runs it on PPSSPP headless (software renderer, about 6 s). It leaves in
`build/nptest/` the log, the raw images and sheets with all scenes. It tells whether a scene hangs and what it should look
like; it says nothing about the plugins, which only run on the Vita. PPSSPP is not the reference: the normal image on the
Vita is. The first Vita run confirmed it is right on all but `morph`, `readFront`, `selfBlur` and `clutStale`.

## Reading a Vita run
`python3 tools/nptest_report.py <run folder> [PPSSPP native_test.log]` prints, for each scene, the expected and the obtained
result (needs a development build). `ok` only says the frame was drawn in HD, not that the image is right. A frame with no
new record (`no_new_frame`, for example `limit`) is not in those lines: look at the `record:` lines of `nativedraw_capture.log`.

## Comparing the images
With `ms0:/nptest_dumps` starting with `y` (`send/ux0/pspemu/nptest_dumps`), each scene lasts 5 s (about 9 min) and at its
frame 60 sends a GE NOP carrying `0x4E5044`, which makes a development NativeDraw take a dump. Use the `screenshot` level
(`ux0:data/nativedraw/dump`, read when Adrenaline opens) and leave it as it was afterwards.
`python3 tools/nptest_compare.py <dumps> <run> <out>` matches each dump to its scene by the bar code, reduces the Vita image
and compares it in 4x4 blocks: `same`, `DIFF`, `normal (reason)` or `no dump`. It leaves `compare.txt` and pages with PSP,
Vita and differences. Animated scenes can differ by a frame. Scenes that show no GE list (`ramShown`) make no dump.
