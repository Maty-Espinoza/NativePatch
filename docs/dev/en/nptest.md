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
Vita is. Known differences to confirm: 8-bit UVs in through mode (`vertFmts`, cell 0), `bboxJump` (red shows), `mipLevel` and
`clutStale`.
