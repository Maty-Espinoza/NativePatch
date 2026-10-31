# Dumps

A dump saves what happened on screen in one or more frames: the PSP image, the one the Vita drew, and what the Vita
received to draw it. It is used to find the origin of a visual error without guessing (the "cause first" rule of
`../../agent/en/workflow.md`). The levels work like the log levels (`logs-and-switches.md`): one is chosen depending on how much
information is needed and how much it can cost.

## Levels (`ux0:data/nativedraw/volcado`, one word; without the file: `secuencia`)

| Level | L+R+SELECT saves | Cost |
|---|---|---|
| `desactivado` | nothing | none (takes no memory) |
| `pantallazo` | 1 frame: PSP image + Vita image + one line | ~3 MB only while saving |
| `frame` | 1 frame with everything: images, text, frame record (`.bin`), intermediate image and the exact PSP image | ~8 MB only while saving; the PSP copies its image for ~1.5 s (slower during that time) |
| `secuencia` | 3 consecutive frames with everything (with the exact image, like `frame`) | ~15 MB only while saving; same |
| `auto` | like `secuencia`, and it also triggers by itself when a block of the Vita image does not look like any of the last 4 PSP images | ~17 MB taken for the whole game; a few ms per frame of the Adrenaline thread; nothing for the PSP |

- Each dump ends up in `ux0:data/nativedraw/volcados/NNN/`. Saving takes about 7 s (the card): meanwhile, another one
  cannot be made. The NativeDraw log notes `volcado: listo en ... (automatico)`.
- `auto`: compares (1 out of every 2 frames) 16x16 blocks of the Vita image with the **last 4 PSP images** that Adrenaline
  copied and takes the most similar one (the Vita is 1–2 frames behind: this way the lag does not count). It triggers with
  6 blocks that do not look like any of them (and fewer than half: with more, it is a dialog on top). `fK_normal.bmp` is
  the most similar PSP image. (0.0.8.4–0.0.8.7 compared against the exact image copied by the PSP on every frame: it cost
  ~1.2 ms per frame and because of that the tiles of KOF 96 did not show up.) At most 10 per game,
  with 1 s between one and the next. The presenter log says every 10 s how many frames it checked (1 out of 4) and how many were
  different. It only works with `dibujar`.
- To test something specific, `frame` or `secuencia` is better; `auto` is for catching errors that last a short time.
- Since 0.0.8.7, with L+R+SELECT in `frame` or `secuencia` the PSP copies its image for 1.5 s and the dump waits (up to
  0.5 s) for the first frame that carries it: this way it is compared with the correct frame without the cost of `auto` for the whole game (in KOF
  96, `auto` dropped to 32–36 fps and changed the timings: the tiles that remained did not show up with `auto`).

## Files of each dump

| File | What it is |
|---|---|
| `fK_normal.bmp` | The PSP image that Adrenaline had at that moment (480x272). **It can be from a different frame** than the Vita's (the Vita is 1–2 frames behind) |
| `fK_vita.bmp` | What Adrenaline showed from the NativeDraw surface (960x544) |
| `fK.txt` | How that surface was made (Vita drawing or normal image, and why) and, with the frame record: textures (rows copied, from which mirror copy), states (only what changed) and the list of draws |
| `fK_registro.bin` | Everything the Vita received to draw that frame: draws, states, vertices, palettes, copied textures and (in `auto`) the exact PSP image of that frame |
| `f0_intermedia_XXXXXX.bmp` | The intermediate image (the one KOF 96 draws hidden and then stretches), drawn by the Vita at double size |

## How they are read (`tools/volcado.py`)

1. Download the folder: a `listar_*.txt` with `ux0:/data/nativedraw/volcados` and a `bajar_*.txt` with the files
   (`../../agent/en/relay.md`). **Write the request under another name and rename it at the end**: the relay may pick up a
   half-written file.
2. `python3 tools/volcado.py <dump folder> [K] [--texturas-de <other .bin>] [--salida <folder>]` (numpy and
   Pillow; it also runs in the Windows VM). It draws the frame on the PC imitating the PSP's GE, with the same data the Vita
   received, and leaves `fK_pc.png` (reference | PC / Vita | PC differences in magenta) in the output folder
   (by default the dump's own; never in the repo).
3. How to interpret:

| Result | Means |
|---|---|
| PC ≈ reference and Vita ≠ reference | The data is fine: the error is in how the Vita draws (translator, shaders) |
| PC ≈ Vita ≠ reference | The Vita received bad data (textures from another moment, incomplete copy) |
| With `--texturas-de` the PC comes out right | The textures of that frame were late (the ones from the next frame were the good ones) |
| Only the pose or something that blinks changes | It is lag (another frame), not an error: compare it with the exact image (`auto`) |

The reference is the exact image of the frame if the `.bin` carries it (`auto` since 0.0.8.4; `frame` and `secuencia` since
0.0.8.7); otherwise, `fK_normal.bmp`.

## Replay
`ux0:data/nativedraw/reproducir` with the path of an `fK_registro.bin`: when the game is opened (with `dibujar`) the Vita
draws it again with the same translator and leaves `fK_reproduccion.bmp` and `fK_reproduccion_intermedia.bmp` next to the `.bin`
(then it writes `no` in the file). If the replay comes out right and the live dump does not, the error depends on the timing
(this is how it was found in 0.0.8.6 that the Vita overwrote textures the GPU was still reading).

## History
- 0.0.7.4: L+R+SELECT, 4 frames, images and text. 0.0.7.5: fast writing and compact text. 0.0.7.7: `.bin` record
  and `tools/volcado.py`. 0.0.7.8–0.0.7.10: automatic (points → blocks; confirmed with the GPU finished).
  0.0.7.9: 3 frames, text built from the record when writing. 0.0.8.3: intermediate image. 0.0.8.4: exact image of the
  frame. 0.0.8.5: levels and replay (`reproduccion.c`: `ux0:data/nativedraw/reproducir` with the path of a `.bin` draws it
  again on the Vita). 0.0.8.7: exact image also with L+R+SELECT.
- In 0.0.8–0.0.8.15 a swizzled texture goes as is in the record (record versions 2 and 3): `tools/volcado.py`
  sorts it on the PC. Since 0.0.8.16 (version 4) the record already carries it sorted; the replay on the Vita does not load
  dumps from before version 4 that have swizzled textures. If no draw goes to the image that is shown, the one from the last draw is taken (like the translator).
