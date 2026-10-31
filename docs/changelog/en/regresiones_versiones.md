# Regressions by version: original GePatch (v0.1x-v0.2) and NativePatch (dev39-dev51)

Sources: git history of TheOfficialFloW/GePatch (`git log 67a2f19`), the compatibility spreadsheet
(`docs/gepatch_compatibilidad.csv`, 816 games) and the RR dumps from the Vita and from the lab.

## 1. The spreadsheet: where games broke

271 of 816 games have a "best version" earlier than the last one tested (regression). Games whose
best version was each one (that is, that the next one made worse):

| best | games | what is reported afterwards |
|---|---|---|
| v0.15 | 29 | black FMVs, hangs, 480x272 |
| v0.16 | 42 | black FMVs, missing effects/backgrounds, slowness |
| v0.17.1 | 35 | a quarter of the screen (Surf's Up), floor that disappears at the corners |
| v0.18.1 | 70 | flickering, black FMVs, Adrenaline hangs |
| v0.19.1 | 31 | flickering in save menus, tearing (Persona, Gurumin, Miku) |

## 2. Change in each version, the risk it brought and how it is today

| version | change | risk / regression | today (dev51) |
|---|---|---|---|
| a67d616 | fb with width != 512: draw at 1 KB of VRAM | what is drawn in render targets is lost | real render targets (rt1, scale 1) |
| 4749115 | viewport sign, fixed center 2048 | games with a different center | `vpPatch` from the game's values |
| a043e58 | through vertices x2 only in (-2048, 2048) | large coordinates not scaled | same (not seen as a problem) |
| v0.15 | DFS walk, CALL/RET with stack; BJUMP always jumps | draws of the path not taken are left unpatched | BJUMP walks both paths |
| v0.15 | indexed draws: `lower..upper` range exclusive | last vertex not doubled (fixed in v0.17.1 with `upper+1`) | `upper += 1` |
| v0.16 | `AdvanceVerts` only in through | implicit VADDR wrong after a 3D draw: later 2D doubles 3D vertices | always advances |
| v0.16 | NOP of draws to ignored fbs | invisible characters when reusing lists (DBZ) | the PRIM is restored on each pass |
| v0.17 | `ignore_texture`: draws that read an fb are canceled; textures in 0x04000000-0x04200000 too | **black FMVs, missing effects and text backgrounds** (the most cited regression) | texmodes 3/4/6/7/8/9 instead of canceling; `tex_fb` in the menu |
| v0.17 | FAKE_VRAM 0x0B000000 | hangs (Patapon); reverted | 0x0A000000 |
| v0.18 | "already doubled" mark written in the vertex padding | corrupts data if the padding is used; false positives | patch memory by address/hash |
| v0.18 | `sceGeEdramGetSize` = 4 MB | the game thinks it has 4 MB: places things up to 0x0A400000 | same; the fake VRAM takes 4 MB |
| v0.18.1 | fb of width 480/960 as screen | "weird triangles in Sonic" (480 removed in v0.19.1) | fb by width/format |
| v0.19 | matrix skip (`list += 12`) | commands are lost if the block is not exact; removed in v0.19.1 | the matrices are read (3D clipping) |
| v0.19 | SIGNAL CALL saves BASE, normal CALL does not (garbage) | SIGNAL's RET restores BASE at random | CALL saves BASE |
| v0.19.1 | forced dithering | color change; removed in v0.2 | the game's |
| v0.2 | copy to the screen when enqueuing each list | **flickering and tearing** (half-drawn frames) | only if there was no flip in 100 ms |
| v0.2 | SIGNAL SYNC: `list += 2` always | if FINISH+END do not follow, 2 game commands are skipped | **dev51**: only if FINISH and END follow |
| v0.2 | CALL to bone matrix blocks is skipped | none (there are no draws) | same |

Common pattern in the regressions of the original:
1. Canceling draws (v0.17) instead of redirecting them: each new heuristic erased something in another game.
2. Plugin memory reused while the GE is still using it: single copy list in v0.2, which was
   rewritten with the previous copy still pending (flickering).
3. Heuristics by value (480/272, width 512/480/960, range +-2048) that hit in one game and
   fail in another.

## 3. Applied to NativePatch: the black sky of Ridge Racer (dev48-dev50)

RR dumps by version (big buffer at the end of the frame):

| version | where | sky |
|---|---|---|
| dev39, dev42-dev44, dev46 | Vita | fine |
| dev47 | lab | fine |
| dev48 | lab | **black** |
| dev49, dev50 | Vita | **black** |

The lab reproduces the error, so it is not a difference in the real GE. The other tests:
- The registers at the first sky draw are the same in dev46 and dev50.
- The texture and the palette are intact in the fake VRAM.
- Replaying the game's commands with viewport x2 and the sub.h clipping (Python or C) in
  PPSSPPHeadless draws the sky.

Probable cause (to be confirmed with dev51). What did change is the number of own lists per frame: 15 in dev46 and 42 in dev50. dev48 made
the vertices past the near plane require clipping, and the sky and the road started using the 3D
clipping. The plugin's list ring (`TX_RING`) had 64. The GE runs up to a couple of frames
behind the walk, so the ring wrapped around in 1.5 frames. The first lists of the
frame (reflection render target and sky) were rewritten with those of the next frame before the GE
executed them. It is pattern 2 of the original.

The dumps do not show it: they save each list when it is written, not what the GE executes afterwards. That is
why everything looked correct during analysis.

**dev51 (unconfirmed):** the ring goes to 512 lists (512 KB, 0x0A808000-0x0A888000, ~12 frames with RR's usage).

Rule for the future: all memory that the plugin writes per draw or per frame and that the GE reads
(lists, vertices, copies) has to last several frames. Today these comply:
- `SUB_RING`: ~1 MB, about 16 frames.
- `TX_RING`: 512 lists.
- Screen copy list: double buffer, with the same content every frame.
- `Z_SAVE`: used in order within the frame.

## 4. Regression tests

In the lab, `memstick/seplugins/versiones/` has:
- `ge_patch_v0.2_original.prx`: v0.2 by TheOfficialFloW, with the build fix and the logs of the first commit.
- `ge_patch_dev46.prx`: last one with a fine sky.
- `ge_patch_dev49.prx`: black sky.
- `ge_patch_dev51.prx`.

To test one, copy it as `seplugins/ge_patch.prx`. Versions earlier than v0.2 do not
compile: the current code is different, and the spreadsheet already says what broke in each one.
