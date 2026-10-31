# Basic graphics manual for NativePatch

To understand just enough to follow the project, know what to look at when something looks wrong and send more
complete reports. No graphics knowledge is needed: it is explained from scratch, with examples from Ridge Racer (RR) and
Silent Hill (SH). The graphics part (CPU, GE, RAM, VRAM, lists, textures...) applies to any PSP game. How NativePatch
uses it: `how-it-works.md`.

---

## 1. The PSP inside: four pieces

```
   CPU  ──writes──►  RAM (32/64 MB)  ◄──reads── GE (graphics chip)
    │                  lists, textures,           │
    │                  vertices, data             ▼
    └──────────────────────────────────►  VRAM (2 MB) ──► screen
```

- **CPU**: the processor. It runs the game: physics, logic, sound. It also **builds the display
  lists** (the "recipe" for each image).
- **GE** (Graphics Engine): the graphics chip. It reads the recipe and draws. It is fast but dumb: it does
  exactly what the list says.
- **RAM**: general memory. The game's code, the lists, the 3D models, the textures... live there.
- **VRAM**: video memory, small (2 MB) and fast. The GE draws the image you see there.

The CPU and the GE work **in parallel**: while the GE draws frame 1, the CPU is already
building the recipe for frame 2. This matters (section 11.4).

## 2. What is an image for the machine?

- An image is a grid of **pixels**. The PSP shows **480x272** pixels; the Vita, **960x544**
  (double on each side, four times more pixels).
- Each pixel is a number that says its color. Formats you will see:
  - **565**: 16 bits per pixel (5 red, 6 green, 5 blue). It is the screen's format.
  - **8888**: 32 bits (red, green, blue and **alpha** = transparency). More quality and more memory.
  - **4444 / 5551**: other 16-bit variants with alpha.
- **Framebuffer (fb)**: the piece of memory where an image is drawn. It has an address
  (where it starts) and a **line width** (pitch). For example, 512 pixels per row even if only
  480 are visible.
- **Double buffer**: the game draws in one fb while showing the other, and when it finishes it swaps them.
  That swap is the **flip** (`sceDisplaySetFrameBuf`). In RR the two fb are `0a000000` and
  `0a044000`: that is why they alternate in the dumps.
- **Frame**: a complete image. At 60 fps there are 60 per second. The log numbers each line with the
  frame (`009297 ...`).

## 3. The recipe: display lists

The game does not tell the GE "draw a car". It passes it a **list of commands**: each command is
a 32-bit word, with 8 bits for *what* (the command number) and 24 bits for *the value*.

```
FRAMEBUFPTR   044000    ← draw in this fb
TEXADDR0      0fc7c0    ← use this texture
VERTEXTYPE    00019f    ← the vertices come like this
VADDR         72e520    ← and they are at this address
PRIM          040004    ← draw! (type 4 = triangle strip, 4 vertices)
```

- A **draw** is a command that draws (PRIM). Everything before it is "state": it configures how it
  will be drawn.
- **CALL / RET**: jump to a sublist and come back (like calling a function). RR stores each
  model (car, sky, building) in a sublist that it reuses in every frame.
- **JUMP**: jump without coming back.
- **Stall**: the game can enqueue the list half-written and announce "you can read up to here". The
  plugin patches each new section.
- **FINISH / END**: end of the list.

**GePatch gets in right here**: it reads the list before the GE and changes words. For example,
`FRAMEBUFPTR 044000 -> 000000` (draw in the big buffer) or `PRIM -> CALL` (replace a draw with
a list of the plugin's own). In the `gpd.py` report you see exactly that: what the game sent
and what the plugin turned it into.

## 4. Geometry: points, triangles and camera

- Everything 3D is made of **triangles**. Each corner is a **vertex**, with a position (x, y, z) and,
  if needed, color and texture coordinate (u, v).
- A **triangle strip** (tira_tri) reuses vertices: with 4 vertices you get 2 triangles.
- **2D or "through"**: the game gives direct screen coordinates (HUD, menus, text, sprites).
  GePatch **doubles** them by hand (x2) so they fit in 960x544.
- **3D**: the game gives "world" positions and the GE projects them to the screen with three
  **matrices** (tables of numbers):
  1. **World**: where the object is (the car on the track).
  2. **View**: where the camera is and where it looks.
  3. **Projection**: the perspective (what is far looks small).
- **Viewport**: how to go from camera space to pixels: a center and a scale. GePatch
  doubles the scale so the image comes out at double size.
- **Scissor / region**: a rectangle outside of which nothing is drawn. It is also doubled.

## 5. Textures

- A **texture** is an image that is "stuck" onto the triangles (the asphalt, the clouds in the
  sky, the car body).
- It can be in RAM or in VRAM. In RR the sky uses the texture `0a0fc7c0`, which is in the
  fake VRAM (section 11).
- **Palette (CLUT)**: some textures do not store colors but numbers (0..255) that are looked up in
  a color table. It saves memory. If the palette is wrong, the texture comes out with odd colors.
- **Swizzle**: the texture stored in scrambled blocks so the GE reads it faster. It is
  just an ordering detail.
- **Texture from an fb**: a game can draw something and then use *that image* as a texture
  (section 8). It is one of the hardest things for GePatch.

## 6. Depth: the z-buffer

- Problem: if you draw the car and then a building that is behind it, which covers which?
- Solution: besides the color, each pixel stores **how far away** what was drawn there is. That
  image of distances is the **z-buffer** (or depth buffer).
- **Depth test**: when drawing a new pixel, the GE compares its distance with the
  stored one and only paints it if it is closer.
- On the Vita the z-buffer of the big buffer is at `0x04100000`. Since the VRAM is full, GePatch
  **lends that zone** for other things (render targets at scale 1). It first saves the z in RAM and
  then gives it back (Z_SAVE). If that fails, you see things going through walls or disappearing.

## 7. Clipping: why triangles disappear at double size

- **Near plane**: the camera does not see what is behind it or stuck to the lens. A triangle
  that crosses that plane is **clipped**: the piece behind is cut off.
- **w**: a number that comes out of the projection. For what is in front of the camera it is positive;
  behind, negative. If you see "w<0" in a conversation, it means "behind the camera".
- **The GE's limit**: the GE works with screen coordinates from 0 to 4096. If **a single
  vertex** of a triangle falls outside that range, the GE **throws away the whole triangle**.
  - At 1x (480x272) the screen takes up the center of that space and there is plenty of margin.
  - **At double size, the margin shrinks to less than half.** Big, close triangles (the
    asphalt under the car in RR, the sky) have vertices far away, off-screen, and now they fall
    outside the range. The GE throws them away and gaps appear.
- **GePatch's 3D clipping** (`sub.h`, option `recorte3d`): the plugin calculates on the CPU where
  each vertex will land. If a triangle is going to be lost, it cuts it into pieces that do fit and draws
  those pieces instead of the original. That is how the road came back in RR (dev48), but from then on the
  **black sky** appeared. Understanding this section is understanding the most important bug right now.

## 8. Render targets: drawing on a hidden image

- Sometimes the game does not draw on the screen but on a hidden fb, to use it later
  as a texture:
  - RR's **rear-view mirror** (the view behind is drawn separately and stuck on the mirror);
  - **reflections** on the car body (environment map, fb `0a088000`, 256 wide);
  - **blur or glow**;
  - SH's **flashlight** (a light pass that is blended over the scene).
- That is a **render target (rt)**. Fbs with a width different from the screen's are usually
  one (64, 256...).
- GePatch:
  - It draws them at **scale 1** (original size) in the z-buffer zone (**rt1**).
  - When finished it copies them to the fake VRAM, so the game finds them where it left them.
- Typical errors: the rt comes out empty (black rear-view mirror), with garbage (squares or purple
  stripes) or from the previous frame.

## 9. Copies (TRANSFER)

- The GE can copy a rectangle of memory somewhere else without drawing. It is used for videos,
  loading screens and to save or restore things.
- GePatch scales some copies (those of videos) and uses copies of its own (Z_SAVE, rt1).

## 10. Blending and alpha

- **Alpha**: how visible a pixel is (0 = invisible, 255 = solid).
- **Blending**: how what is drawn is combined with what was already there: add (glows), multiply
  (shadows, SH's flashlight) or average (transparencies).
- If blending uses the wrong image, you get washed-out, black or fluorescent colors.

## 11. Memory and addresses

### 11.1 Addresses in hexadecimal
- Addresses are written in base 16 (0–9 and a–f): `0x0A000000`. The `0x` only warns that it is
  hexadecimal.
- Trick for reading them: the first digits tell the zone.
  - `0x04......` is **VRAM**.
  - `0x08......` and `0x09......` is the **game's RAM**.
  - `0x0A......` is the **extra RAM that the plugin uses**.
- In the dumps often only a part is seen (`044000` instead of `0x0A044000`): the
  command holds 24 bits and the "base" supplies the rest.

### 11.2 The map
```
0x04000000  real VRAM (2 MB)  ── big buffer 960x544 + z-buffer (0x04100000)
0x08000000  game RAM (code, lists, models, textures)
0x0A000000  extra RAM: GePatch lives here
   0x0A000000  fake VRAM (the game believes it is its VRAM)
   0x0A400000  final image that Adrenaline shows on the Vita
   0x0A600000  patch table (which original word there was in each command)
   0x0A800000  the plugin's own lists (ring of 512 in dev51)
   0x0AA00000  dump zone
   0x0AE00000  3D clipping (new vertices)
   0x0AF60000  Z_SAVE (saved z-buffer)
0x0B000000  Adrenaline's memory  ── DO NOT TOUCH
```

### 11.3 Why a fake VRAM?
- The game needs its VRAM for its fb, textures and 480x272 z-buffer. But the real VRAM (2 MB)
  is taken up by the big 960x544 buffer.
- Solution: GePatch tells the game "your VRAM is at 0x0A000000" (RAM). The game stores its
  textures there and believes it draws there. The plugin redirects the screen drawing to the big buffer.

### 11.4 The GE runs behind
- The plugin patches the list when the game hands it over. The GE executes it **later**, up to one
  or two frames after.
- If the plugin writes a list of its own in one place and before the GE uses it **overwrites** it with
  another, the GE executes the wrong one: something is missing or garbage comes out.
- That is why the plugin's own lists go in a big **ring** (like a circular tape: it always writes
  in the next place and goes back to the start when it runs out). In dev51 it was enlarged
  from 64 to 512 places because with 3D clipping ~42 were used per frame and the tape went around
  too fast.

### 11.5 Cache (basic level)
- The CPU has a fast intermediate memory (cache). What it writes can stay there for a while
  before reaching RAM. The GE reads RAM, not the cache.
- That is why the plugin "flushes the cache" (writeback) before the GE reads what it wrote. If not,
  the GE sees old data.

### 11.6 Why does the game crash?
- If the plugin (or the GE with a bad list) writes to memory that is not its own, it overwrites data of the
  game or of Adrenaline: a function address, a list, a variable. When the game
  uses it, it jumps anywhere. For example "Bad Execution Address", "Jump to 00000000" or a Vita
  that shuts down.
- Another cause: reading a 16-bit number from an odd address. The PSP's CPU does not allow it
  and crashes. That is what happened with dev49 in the automatic test.

## 12. A frame with NativePatch, step by step

1. The game builds the frame's list and enqueues it. NativeLink (on the PSP) writes it down in the mailbox and lets the GE
   draw it as always, in the PSP's VRAM at 480x272 (that image is not seen).
2. NativeDraw (on the Vita) reads the mailbox and walks the same list from the PSP's RAM: it keeps the GE's state and saves
   each draw with its vertices, its texture and its palette (the frame record).
3. At the flip, NativeLink writes down which image is shown and asks the GE to copy to normal RAM the pieces of VRAM
   that the Vita requested (the mirror).
4. On the Vita, the translator draws the record with the GPU on a double-size surface (960x544) and the presenter hands it
   to Adrenaline instead of the PSP's image. If something in the frame cannot be drawn, the PSP's image is shown.
5. All of that takes time: the HD image is seen one frame after the PSP's.

## 13. Log and dump: what they are and how to get good ones

- **Logs** (`ux0:pspemu/nativelink.log`, `nativedraw.log`, `nativedraw_captura.log`): text with events and
  statistics (`logs-and-switches.md`).
  - At the start: version, memory, what was hooked.
  - Every 10 s (NativeLink and `nativedraw.log`) or every 1 s (`nativedraw_captura.log`): fps, commands and draws per frame,
    Vita time, textures, GPU.
  - New events, once each.
  - The number at the start of each line is the frame (NativeLink) or the ms since start-up (NativeDraw).
- **Dump** (`ux0:data/nativedraw/volcados/NNN/`, L+R+SELECT or automatic): what happened on screen in one or more
  frames: the PSP's image, the one the Vita drew, a text with how it was done and everything the Vita received in order to
  draw it (`dumps.md`). With that `tools/volcado.py` draws the frame on the PC and says whether the error is in the data or in
  how the Vita draws.
- Tips for a good dump:
  - Capture **right when the error is on screen**. If the error lasts a short time, use the `auto` level (it fires by itself).
  - Note down what you were seeing ("background with pieces of another sprite in fight 2").
  - If you can, get the same place with NativeDraw not drawing (`dibujar` set to `no`): it is the reference of how it should look.
  - Trying the same place with another version helps to know **since when** the error is there.

## 14. The minimum about the PC tools

- `python3 tools/resumen_corrida.py <folder>`: summary of a run (fps every 10 s, waits at the sync, mirror,
  seconds where the Vita cannot keep up, DISTINTAS) without reading the whole logs.
- `python3 tools/volcado.py <dump folder> [K]`: draws frame K on the PC and leaves `fK_pc.png` with
  **reference | PC | Vita | differences**. It is the fastest way to see where the error is.

## 15. Quick glossary

| Term | In simple terms |
|---|---|
| GE | the PSP's graphics chip |
| display list | the recipe of commands that the game passes to the GE |
| draw / PRIM | a command that draws |
| fb / framebuffer | memory where an image is drawn |
| big buffer | the 960x544 screen that GePatch builds in the real VRAM |
| fake VRAM | RAM that the game believes is its VRAM (0x0A000000) |
| flip | the moment when the finished frame is shown |
| vertex | corner of a triangle |
| through / 2D | direct screen coordinates (HUD, menus) |
| matrix | table that moves, rotates or projects the vertices |
| viewport | how to go from the camera to pixels |
| scissor | rectangle outside of which nothing is drawn |
| texture | image stuck onto triangles |
| CLUT / palette | color table of an indexed texture |
| z-buffer | distance of each pixel, to know what covers what |
| near plane | limit in front of the camera; what is behind it is not drawn |
| w | projection number; negative = behind the camera |
| 3D clipping | cutting triangles so they fit in the GE's range |
| render target / rt / rt1 | hidden fb that is later used as a texture |
| TRANSFER | memory copy done by the GE |
| alpha / blending | transparency / how colors are combined |
| slot / ring | place for a list of the plugin's own / circular tape of those places |
| setOp / patch | change a word of the list while remembering the original |
| passive mode | the plugin changes nothing (reference) |
| dump / .gpd | complete recording of a frame |

## 16. Checklist before reporting

- [ ] Plugin version (it appears on the first line of the log).
- [ ] Vita or lab?
- [ ] Game, scene and what looks wrong, in your own words.
- [ ] Since which version does it happen? Does it happen in passive mode?
- [ ] Did you change menu options?
- [ ] Log and dump of the scene (and the frame number of the capture).
- [ ] If it crashed: a photo or screenshot of the message and the log of that session.
