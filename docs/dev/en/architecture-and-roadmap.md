# Architecture: the PSP draws normally and the Vita draws in HD (analysis of 2026-10-28)

> **How to read this document:** it is the analysis that decided the current architecture and the stage-by-stage
> roadmap (§6), and it is still valid. It is kept as it was written, with the names of its time: **GePatch** = NativeLink
> (PSP side), **gepresent** = NativeDraw (Vita side); "shadow mode" = what NativeLink does today all the time; "current
> GePatch (A)" = the scaling on the PSP, which was removed in NativeLink 0.0.2 (it does not remain as a fallback mode).
> The documents it cites (`contexto_ia.md`, `etapaN_*.md`, `errores_conocidos.md`...) are in `../../changelog/en/` or in
> the claude.ai project. **Stages 0, 1 and 2 done**; stage 3 is in progress (`../../agent/en/stage3-plan.md`). How it
> works today: `how-it-works.md`; what was measured: `findings.md`.

Analysis conversation (no code changes, without touching the Vita or the lab). The user's question: review all the
documentation and other projects (Adrenaline 8, PPSSPP, original GePatch, NativePatch) and propose **the best way to
implement the scaling** with two criteria: **performance equal or similar to the original** and **games that draw
well**. Complexity is not a criterion: it can be advanced little by little.

Markers: **[measured]** verified on the Vita; **[estimated]** calculated from measurements; **[deduced]** conclusion
without direct proof; **[verify]** must be confirmed before building on top of it (with the test indicated);
**[warning]** extrapolation from a synthetic test that is taken as neither true nor wrong until it is tried with the
real game.

Read after `contexto_ia.md`. This document **reopens route G of `filosofia.md`** ("emulate the GE on top of GXM",
discarded only for complexity) and makes it the target architecture. Current GePatch remains as a fallback mode.

**Status:**
- Stage 0 **done** on 2026-10-28 → **`etapa0_resultados.md`** (tool `tools/carga_vita.py`). It corrected §3.5 (mirror
  mandatory for GT and RR), §5 (GT has ~9000 draws, not ~4000) and the stage 1 criterion (§6).
- Stage 1 **done** on 2026-10-28 → **`etapa1_banco_gxm.md`** (test bench in gepresent v11.3). It measured the cost of a
  draw in GXM inside Adrenaline (~1 µs with no changes, ~2 µs with a new texture or program, ~3 µs texture + uniforms),
  the available memory (~33 MB LPDDR + 4 MB CDRAM), the deferred context (works; the Adrenaline thread only executes,
  ~30 µs), the GPU reading the PSP's RAM (works, same speed) and the presenter (works). It updated §5, §6 and §8.
- Stage 2 **in progress** (2026-10-28) → **`etapa2_avance.md`**: shadow mode (GePatch dev61, default) + capturer on the
  Vita (gepresent v12.9). SH at original speed; GT at 55–60 fps except for the bridge (~30 ms of capture per frame).
  After that: sublist memory, VRAM mirror (double since v13.6), T4/T6 (`etapa2_*.md`). 2026-10-29 (v13.7): recording
  on several cores measured with GT in a race (`etapa2_nucleos.md`): **GT does not fit when recording draw by draw**
  (see §5). T2 (black box) is missing.

---

## 0. In simple terms (for someone without technical knowledge)

- Today GePatch asks the PSP's "painter" (the GE) to paint the image four times bigger. The painter is the same as
  always and cannot keep up: that is why heavy games run slowly. Also, for the big image to fit, the game has to be
  tricked with a fake memory, and that trick is the origin of almost all the image errors.
- The proposal is **not to touch the PSP**: the game runs exactly as without the plugin, at its original speed, and its
  painter draws the usual small image (which nobody sees).
- Meanwhile, **the Vita reads the same drawing instructions** that the game sends to its painter and draws them again
  with its own graphics card, which is much more powerful, in high resolution. That is the image that is shown.
- Advantages: the game does not become slow (the PSP does the same as without the plugin), the Vita has plenty of
  memory (goodbye to the 2 MB limit), and since the game is not tricked, the errors that came from the trick disappear.
- Cost: it is like writing the graphics part of an emulator (what PPSSPP does) inside the Vita. It is a lot of work,
  but it can be done in stages and each stage leaves something useful (the first one already gives a diagnostic "black
  box").
- If the Vita does not know how to draw something, the normal image of the game is shown: **never worse than without
  the plugin**.

---

## 1. Main conclusion: the current architecture cannot meet the performance goal

With current GePatch the GE draws `s²` times the pixels. What was measured with vpjuego:

| Game | GE without GePatch | GE with GePatch 2x | How much fits in 16.7 ms (60 fps) |
|---|---|---|---|
| Ridge Racer | 4–5.7 ms | 22–27 ms (1.5x: 13–15) | 2x no; 1.5x barely |
| Gran Turismo | 8–13 ms | 22–38 ms | not even at 1.5x (estimated 16–22 ms) |
| Silent Hill SM | 18–37 ms per flip | 40–60 ms | does not reach 60 even at 1x; 2x lowers it to 15–21 fps |
| LocoRoco MC | 1–3 ms | 3–9 ms | yes |

- It is a **physical limit of the GE** (pixel fill), not a logic error: the with/without ratio is 3–4, equal to the
  number of pixels. [measured, B1]
- That is why no CPU optimization (path B, plan A of the ARM walk) raises the fps to 2x in GT, RR or SH. [measured]
- The only lever of the GE is lowering the scale (1.25–1.5x), which is exactly the opposite of what is wanted.
- In addition, the current architecture carries by design: a full 2 MB VRAM (rt at scale 1, Z_SAVE, swap), a 565 buffer
  without stencil (SH's flashlight, GT's dashboard), a reduced guard band (RR's gaps), a fake VRAM in RAM (GE +19–75 %
  with textures over the bus), patches inside the game's memory (the drops of dev58).

**→ To have the original performance and a good image, the high-resolution drawing has to be taken out of the GE.** The
only other graphics chip available is the Vita's GPU (SGX543MP4+), and the only free processor is the Vita's ARM.

---

## 2. The architecture families

| | A. Current (GE at scale) | A'. Improved current | **B. PSP untouched + Vita draws in HD** | C. Mixed (GE without draws + Vita) |
|---|---|---|---|---|
| What the GE draws | everything, at `s²` pixels | everything, at ≤1.5x | **everything, at 1x, unchanged** | only what the game's CPU needs |
| What the Vita draws | nothing (it only enlarges) | nothing | **everything, at 2x or more** | everything |
| Game performance (PSP) | GE ×3–4, CPU +5–10 ms | GE ×2, CPU +5–10 ms | **= original** (+hooks, µs) | better than original in GE |
| Image | errors by design | fewer errors | errors only from the translator; **fallback = native image** | same as B, less data for the CPU |
| VRAM | 2 MB (hard limit) | 2 MB, ~0.9 MB free at 1.5x | **the Vita's** (~33 MB free in Adrenaline, stage 1) | the Vita's |
| Guard band | shrinks when scaling | shrinks less | **same as the original** | same as B |
| Complexity | already exists | medium | **very high** (a PPSSPP-style renderer) | very high + risk |
| Does it meet the goal? | no | only light games | **yes, in principle** (depends on the ARM, see §5) | not needed |

**Recommendation: B**, with A as a per-game fallback while B matures. C only if some game needed to lower the native
GE (not the goal).

---

## 3. Design B in detail

### 3.1 Principle
**The PSP is not touched.** The game runs its list as is on the real GE, at 480x272. GePatch (PSP side) becomes a thin
plugin that only **notifies** (enqueues, stall, sync, flip) and never rewrites the list. The Vita reads the same list
from the PSP's RAM, translates it to GXM and draws it on its GPU at whatever scale is wanted.

Direct consequences:
- Everything the game observes is **exact**: VRAM, z, stencil, GE interrupts (FINISH/SIGNAL), CPU reads of what was
  drawn, `sceGeEdramGetSize` (real 2 MB). There is no fake VRAM, no patch memory, no `BASE_PLUG`, no scaled vertices.
- The GE cost is the original. The PSP CPU cost is that of the hooks (writing a few words into a mailbox).
- The copy per flip to 0x0A400000 disappears (2.3 ms of GE at 2x) and the 0x0A000000 zone is no longer needed (P1–P3).

### 3.2 The user's questions

**How do we redirect?** Nothing is redirected. The Vita **reads** the same thing the GE does: the list (and its
sublists, vertices, indices, textures, CLUT) is in the PSP's RAM, which the ARM sees at 0x70000000 (uncached) and
0x61000000 (cached) and which the Vita's GPU can read if it is registered with `sceGxmMapMemory` (gepresent already does
it; **stage 1: the GPU read vertices from 0x08800000 at the same speed as from its own memory** [measured]).
GePatch's hooks (`sceGeListEnQueue/Head`, `sceGeListUpdateStallAddr`, flip) already exist: they only change from "walk
and patch" to "write down in the mailbox". **Exception (stage 0):** what the game stores in VRAM (GT: sublists,
vertices, textures; RR: textures) is not in RAM: see §3.5.

**What is the latency cost?**
- PSP→ARM channel through shared memory: p50 11 µs in a burst, 200–500 µs if the ARM thread sleeps. [measured, KBENCH]
  The ARM thread has to poll without sleeping while there is a game (sleeping 100 µs is ~400 µs in reality).
- Image: the Vita finishes drawing frame N after the game sent it; it is shown **one frame later** than today
  (~16.7 ms at 60 fps, 33 ms at 30 fps). It is the typical latency of an emulator with triple buffering. [estimated]
- The game does not wait for that latency (see "synchronize", below): only the eye sees it.

**How do we synchronize the two CPUs?** Two stages with different rules:
1. **Capture** (ARM): walks the list following the stall that the PSP publishes and copies into the Vita's RAM what it
   will need later (already interpreted commands, vertex/index ranges, loaded CLUTs, hash of each texture). It has to
   go **at the GE's pace**, because the game can reuse its buffers once the GE has finished.
2. **Drawing** (ARM + GPU): translates what was captured to GXM and draws. It runs **decoupled**, with 1 frame of lag.
   If it falls further behind, the Vita repeats the previous frame on screen, but the game keeps its own speed.
   **Stage 1:** the drawing is recorded in a deferred GXM context on the translator thread, and the Adrenaline thread
   only executes the list (~30 µs per 2000 draws) [measured].

Where the PSP waits (and only if the capture is behind):
- **Normal mode (default): gate at the sync.** The hooks of `sceGeDrawSync`, `sceGeListSync` and the flip wait until
  the capture has reached the end of the list. It is the point where almost all games reuse their buffers. No extra
  threads on the PSP.
- **Strict mode (per game): gate at the stall.** The stall hook gives the GE at most what the Vita has already captured.
  It guarantees that the Vita reads everything before the GE does. It needs someone on the PSP to move the stall when
  the Vita advances (a polling thread): it costs PSP CPU. Only for games that reuse buffers in the middle of the frame.

**How do we tell the GE that the commands were already applied and it should stop waiting?** It is not needed: in B
**the real GE executes the list itself** (at 1x) and generates its own signals; the game sees exactly the original
behavior. The Vita is never in the GE's path (except strict mode, which only delays the stall). If one day variant C
(GE without drawing) were wanted, the PRIMs are changed to NOPs (a reversible mechanism that already exists) and
SIGNAL/FINISH/END/JUMP/CALL are left: the GE walks the list almost for free and keeps giving the signals on time (an
idea already noted in `plan_a_recorrido_vita.md`).

**How do we synchronize the frames with GePatch?** The flip hook (`sceDisplaySetFrameBuf`) writes "frame N: fb X is
shown" into the mailbox. The Vita presents its HD version of that fb when it finishes it. To present, what already
works in gepresent/adrfsr is used: hook the GXM imports of `AdrenalineUser` and change the texture that Adrenaline
draws to the HD surface. [measured: adrfsr changes the fragment program in Adrenaline's draw without GePatch;
**stage 1: the bench swapped the 512x272 texture for its own surface in 599 of 600 frames, by hooking
`sceGxmSetFragmentTexture` and drawing its scene after Adrenaline's `sceGxmEndScene`**]
Note: **SH draws almost all of its frame after the flip, in the same list** [measured, stage 0]: SH's frame N is what
is drawn between one flip and the next, not "the list enqueued before the flip".

### 3.3 Components

| Component | Where | What it does | Existing base |
|---|---|---|---|
| **Thin GePatch** | PSP (kernel) | Hooks for enqueue, stall, sync, flip; `sceGuCopyImage`/TRANSFER is not needed (it is seen in the list). Mailbox. Menu. "VRAM mirror" requests (§3.5). Mode A as a per-game fallback | Current GePatch (hooks, mailboxes of dev56.5) |
| **Capturer** | Vita, ARM thread 1 | Shadow walk of the list; copies by DMA what is referenced; sublist cache by hash | Plan A step 2 (`plan_a_recorrido_vita.md`), KBENCH |
| **Translator** | Vita, ARM thread 2 (deferred GXM context) | GE state → GXM state; texture cache; programs; framebuffer manager; **merges consecutive draws with the same state** | Parts of PPSSPP (§3.10); stage 1 bench (`tools/gepresent/banco.c`) |
| **Renderer** | Vita GPU | GXM scenes on its own HD surfaces (8888 + z/stencil) | adrfsr (shader patcher, compilation on the PC), stage 1 bench |
| **Presenter** | Vita, Adrenaline's drawing thread | Executes the recorded lists and shows the HD surface of the fb the game displayed; if there is none, the native image | gepresent / adrfsr / stage 1 bench |
| **Black box + telemetry** | Vita | Last captured frames always in RAM; statistics; `.ppdmp` dumps | gepresent v9 (ring and background writer; **the 24 MB ring has to shrink**: §5) |

### 3.4 One frame, step by step
1. The game enqueues its list → hook: writes (address, stall) into the mailbox. The real GE starts at the same time.
2. The game moves the stall → hook: updates the stall word (no queue: the Vita reads the latest value).
3. Capturer (ARM): advances up to the stall; for each draw it records state + vertex/index range + hash of the texture
   and its CLUT. Sublists equal to those of the previous frame (same hash) are reused already translated.
4. The game calls `sceGeDrawSync` → hook: waits only if the capture did not reach the end (normally it does not wait).
5. Flip → hook: "frame N shows fb X".
6. Translator (frame N, while the game is already building N+1): records GXM scenes for each render target, in the
   game's order, in a deferred context.
7. Presenter: on Adrenaline's thread it executes what was recorded and on the next vblank shows the HD surface of X; if
   frame N had something unsupported (or there were no GE draws in X: video, CPU drawing), it shows the native image.

### 3.5 What the Vita cannot see: the real VRAM
The PSP's VRAM is not mapped in Adrenaline's process [measured, B4]. In B the Vita needs its contents when a texture, a
CLUT, **a sublist or a vertex buffer** lives in VRAM. Where that content can come from:

| Origin | How the Vita gets it | Cost |
|---|---|---|
| A render target that the GE drew | **Its own HD version** (framebuffer manager, like PPSSPP) | no extra cost |
| Block copy (TRANSFER) from RAM | The Vita sees it in the list and repeats it from RAM | no extra cost |
| CLUT loaded from VRAM | Same as a texture | — |
| **Fixed data that the game uploaded to VRAM before the frame** (stage 0: GT 370–700 KB per frame between textures, sublists and vertices; RR 70–460 KB of textures; SH nothing) | **Mandatory mirror**: copy with the GE to RAM at load time and whenever it changes; between dumps of the same game 77–100 % is equal | GE ~1.6 ms per 700 KB copy; see strategies in `etapa0_resultados.md` |
| The game's CPU wrote the VRAM (textures copied by hand, software drawing, SH's spinner) | **Requested mirror**: the Vita detects a texture in VRAM with no known origin and asks the PSP to copy it with the GE to RAM at the end of the frame (used from the next frame) | GE: ~2.3 ms per MB; normally KB [estimated]. Stage 0: no texture changed within a frame |
| Screen drawn by the CPU or a video (no GE draws in that fb) | The presenter shows the native image of that frame | none |
| CPU drawing **on top of** a GE scene | Per game: compare the native image with the reduced HD one and paste the difference | ARM, per game |

- **GT walks sublists that are in VRAM** (~20 % of its commands) [measured in dumps, stage 0]: without a mirror the
  capturer cannot follow its lists. It is the most demanding case; SH is the simplest (it only reads its own rt).
- **Double mirror (stage 2, dev64 + v13.6, `etapa2_espejo_doble.md`):** two alternating copies (0x0A200000 and
  0x0AC00000); each list is read with the last finished copy and the Vita says which one it uses, so the GE never
  overwrites it. Each copy holds all the requested sublists and up to 256 KB of textures. TRANSFER destinations in VRAM
  are treated as rt: in GT they are almost all the textures that change during a race, and in SH the copy of its image
  for effects [measured].
- **The native image already reaches the Vita for free**: in Adrenaline's normal mode (without `DRAW_NATIVE`), the
  compatibility hardware leaves each frame in the 0x74000000 buffer (512x272, 8888), Adrenaline copies it to its
  512x272 texture and draws it [code]. **Stage 1:** the bench drew it inside its own surface while replacing the
  texture [measured]. GT's cutscenes are seen through that path (user, gepresent v9). [verify] why during the bench's
  presentation the texture arrived black (`ff020202` pixels; the user saw black + the bench's square): pending,
  postponed by the user (`etapa1_banco_gxm.md` §3.3).

### 3.6 Data coherence (what is most likely to fail)
- Textures: the Vita cannot copy all the textures every frame. Cache by (address, size, format, CLUT) + hash. To avoid
  recomputing hashes of everything every frame: a quick hash (samples) every frame + a full one every N frames or when
  a known write is seen (TRANSFER, `sceKernelDcache*` over that range). It is PPSSPP's same strategy and its known risk
  is textures that change without notice (text written letter by letter, animated palettes): a per-game option "always
  full hash". Stage 0: GT uses 400–700 different textures per frame (almost all clut4 with swizzle), RR 20–124, SH
  17–109.
- Vertices: GT 0.5–1 MB and SH up to 3 MB per frame [measured, stage 0]: copying them by DMA (7.2 ms/MB) does not fit.
  **Read them directly from the PSP's RAM with the GPU** (already registered with `sceGxmMapMemory`; **stage 1: same
  speed as its own memory, 17 M vertices/s** [measured]) and copy only what the game rewrites before the GPU reads it
  or what is not in
  GXM format. The vertices that are in VRAM (GT, ~35 % of the draws) come from the mirror.
- Lists that the game reuses between frames by changing words (GT): the capture rereads them every frame; the sublist
  cache by content hash (what was already learned with `WalkCache`).
- `sceKernelDcacheWritebackInvalidate*`: the ARM reads uncached or after DMA, and the lists written with cache by the
  game are already in RAM when the game enqueues them (the real GE requires it). [deduced]
- **Memory of the deferred contexts** (stage 1): GXM asks for memory through callbacks while recording and keeps using
  it between lists. Recycling it too early made the GPU crash (bench v11.2). Recycle by whole blocks and only when the
  GPU has finished with the lists that use it. ~160–640 KB per 2000 draws [measured].

### 3.7 How each known error looks (`errores_conocidos.md`)

| # | Error | In B |
|---|---|---|
| V1 | SH's flashlight (8888 + stencil) | **Disappears**: 8888 surfaces with stencil (D24S8) |
| V2 | GT's dashboard (5551 + stencil) | **Disappears** (same reason) |
| V3 | Black areas in GT (omitted draws) | **Disappears**: nothing is omitted |
| V7 | Texture that reads a part of an fb | Solved in the framebuffer manager (offset inside the fb) |
| V8 | RR's rear-view mirror, GT's reflections, SH's passes | **Disappears**: all rt in HD (plenty of memory) |
| V9 | Gaps in RR's road (guard band) | **Disappears**: the discard is decided with the 1x coordinates, like the real GE (§3.8) |
| V10 | 1024-wide passes | No GE width limit: they are drawn in HD |
| V11 | 1 px borders in 2D | New-old risk of emulators: seams between sprites at a high scale (see §3.9) |
| V12 | Textures with garbage (z as a texture) | If the game reads its z as a texture: the Vita has it in HD (surface z) or asks for a mirror |
| V13 | CPU drawing | Mirror / native image (§3.5) |
| V14 | Menu cut off at \< 2x | The menu is drawn by the Vita on its own surface |
| R1 | GE as the limit | **Disappears**: the GE works at the original level |
| R2 | CPU of the walk on the PSP | **Disappears**: the walk is on the ARM |
| P1–P3 | Zone 0x0A000000, high memory | **Disappear**: that zone is not used (except for a small mailbox) |

### 3.8 GE restrictions: which are emulated and which are overcome
- **Guard band:** the GE only clips against the near plane and discards the whole triangle if it leaves a virtual area
  of 4096x4096. PPSSPP implements the rule ("vertex range culling": discards if any vertex falls outside, except when
  the depth clamp would act) and has already had cases where its approximation fails (TDU). In B the rule is applied
  **with the 1x coordinates**, so what is discarded is the same as in the original: no new gaps appear.
  Optional per game: do not discard (may show geometry that the original hid; to be tested).
  Stage 0, triangles discarded per frame at 1x / 2x: GT 140–800 / 300–2100, RR 20–80 / 100–270, SH 0–490 / 26–1770.
- **565/5551/4444 formats with dither:** the Vita draws in 8888 (no banding). When a game reads an fb as a texture
  expecting the small format (PPSSPP's "depal" effects), it is converted in the shader. SH reads an rt as clut32 from
  VRAM [measured, stage 0].
- **16-bit Z, range and clamp:** depth problems are the most frequent ones in PPSSPP; use its rules.
- **1024/2048 width limit, 2 MB VRAM, fill:** they do not exist on the Vita.

### 3.9 GXM versus the GE: what maps directly and what does not

| GE function | In GXM | Note |
|---|---|---|
| CLUT4/CLUT8 textures | **Direct**: P4/P8 formats with palette (64-byte palette alignment in `gxm.h`) | The PSP's 16-bit CLUT is converted to 8888 (≤ 256 entries, cheap). CLUT shift/mask: in the shader if it is not the simple case. **Stage 1: a draw that changes a P4 texture + palette costs the same as one that changes an 8888 (~2 µs)** [measured] |
| PSP swizzle (16-byte blocks × 8 rows) | Does not match GXM's swizzle | Un-swizzle on the ARM with NEON, only when entering the cache |
| DXT1/3/5 | GXM has UBC1/2/3 | The PSP stores the DXT blocks in another order: reorder when loading [verify with PPSSPP `TextureDecoder`]. GT uses dxt1 and dxt5 |
| Blending with constants (FIXA/FIXB), logic operations, partial color mask | Not in fixed blending | **GXM has programmable blending** (the shader reads the fb color): it allows copying the exact GE. Vita3K documents that few games use it and that it is a challenge for emulators on PC, not on the Vita |
| Alpha test, color test, fog, texture functions, color doubling | Shader | |
| Stencil | D24S8 | |
| Lights (4), materials, bones (8), morph | Vertex shader (or the ARM for morph) | |
| RECTANGLES (sprites) | Does not exist | Expand to 2 triangles on the ARM (few per frame: RR ~20, SH ~21, GT ~60) |
| 8-bit indices | GXM uses 16/32 | Convert at capture (only SH uses indices: 5–21 KB) |
| Bézier/spline | No | Tessellate on the ARM (PPSSPP already does it). None of the 3 games uses them |
| PSP vertex attributes | Most are described as is (U8/S8/S16/F32 with stride) | Packed 565/5551/4444 colors: unpack in the shader |

**Shaders:** two paths, from less to more:
1. **Precompiled ubershaders** on the PC with Sony's compiler under qemu (a chain that already works for adrfsr and the
   stage 1 bench). No requirements for the end user. More GPU cost because of branches.
2. **Compilation on the console** with `libshacccg.suprx` (installed with ShaRKBR33D, as for vitaGL/flycast) and a cache
   on the card. Faster on the GPU, but each new shader takes ~0.5 s to compile (stutters the first time: the big ports
   report it) → persistent cache per game.
Start with 1; move to 1+2 if the GPU is not enough. Stage 1: switching fragment program between draws costs
~2.1–2.4 µs (against ~1 µs with no changes) [measured]: it is better to have few programs and to sort/merge by program.

**Known problems of emulators when raising the resolution** (B inherits them): seams between 2D sprites, textures that
"bleed" at the edges, 1 px lines. PPSSPP mitigates them with per-game options (UV rounding, drawing 2D at integer
scale). See the screenshot collection.

### 3.10 Reusing PPSSPP
- PPSSPP is GPL 2.0 or later; Adrenaline is GPLv3. **[verify]** the GePatch/NativePatch license before copying code.
- Useful parts (backend independent): vertex decoder, texture decoder (swizzle, DXT, CLUT), texture cache, framebuffer
  manager (rt, copies, depal), depth and discard rules, tessellation. What is not useful: its CPU/HLE and its backend
  (Vulkan/GL/D3D).
- Big work: a GXM backend (scenes, surfaces, programs) and adapting its memory reading to 0x70000000.
- Simpler alternative to start: write our own translator using PPSSPP as a reference, with the fixed shaders of §3.9.
  Decide after stage 3 (§6).

### 3.11 Graceful degradation (guaranteed compatibility)
- **Per frame:** if the translator finds something it does not know how to draw, it marks the frame and the presenter
  shows the native image. Worst case = the game without the plugin.
- **Per game (profile):** `mode = native | GE (current GePatch) | Vita HD`, plus B options (normal/strict gate, full
  hash, 2x/3x scale with downscaling = antialiasing, GXM's 4x MSAA).
- **Automatic detection:** every N frames compare the reduced HD image against the native one (§4.5); if the difference
  passes a threshold, automatic black box dump and a warning in the log.

---

## 4. Telemetry: what is still unknown and how to get it

Today there is a log + dump by key combination (3 frames forward), vpjuego (GE busy, CPU free, fps), walk profile and
Allegrex counters. What is missing: seeing what the game's CPU did, seeing frames **before** the problem, replaying a
frame step by step and knowing the exact rules of the GE.

| # | Tool | What it answers | Cost for the PSP | Base |
|---|---|---|---|---|
| T1 | **Capture statistics** (every second in the log): draws, prims by type, through/3D, real state changes, rt changes, textures used (count, KB, format, swizzle, CLUT, in RAM/VRAM), KB of vertices, TRANSFER, CLUT loads, discarded by guard band, **batches that could be merged** | The real budget of the ARM and the GPU per game; which GE functions each game uses | none (the ARM does it) | capturer; **the same figures already come out of the dumps with `tools/carga_vita.py`** |
| T2 | **Black box**: the last 3–5 captured frames always in the Vita's RAM; L+R+SELECT saves the ones **before** the problem | Errors that are seen before reacting; what the 3-frames-forward burst does not see | none | gepresent v9 ring (shrink it: §5) |
| T3 | **Dump in PPSSPP's `.ppdmp` format** | Open the frame in PPSSPP's GE debugger: step draw by draw, see state, textures and fb; replay it in PPSSPPHeadless (with the software renderer, which models the GE) and compare PNGs | same as today | `gpd.py ppsspp`: **generates valid `.ppdmp` (v6)**; headless drew one from SH the same as the Vita and the user opened one with PPSSPPSDL in WSL (stage 0) |
| T4 | **Trace of the game's calls** (event ring with time): `sceGe*` (enqueue, stall, sync, callbacks), `sceDisplay*`, `sceKernelDcache*`, `sceDmac*` toward VRAM, `sceGeEdram*`, `sceMpeg*` | "What the CPU did and when": where the game waits, whether it uses double buffering, whether it writes VRAM with the CPU or with DMA, how many flips there are per frame (open question about SH) | µs per call | GePatch hooks |
| T5 | **Native vs HD comparison** (native image from 0x74000000 against the reduced HD one) | Automatic detection of missing or extra draws; % difference per frame; automatic dump | none | presenter |
| T6 | **GE times per list** (time of enqueue and of FINISH) + integrated vpjuego sampling | The real margin of the native GE; whether the sync gate makes it wait | µs | vpjuego |
| T7 | **GE tests on the Vita** (extended geptest): exact edge of the guard band (vertices at 4095/4096/4097, with and without z clamp), near-plane clipping, dither, texture LOD | The exact rules that the translator has to imitate; checked against PPSSPP's software renderer | separate homebrew | `tools/geptest` |
| T8 | Allegrex counters (`perfil=2`) in mode B | Confirm that the PSP spends the same as without the plugin | already exists | |

---

## 5. Performance budget on the Vita side (updated with stage 1)

| Work | Known data | Estimate |
|---|---|---|
| Walking the list on the ARM | 53,000 commands in 10.8 ms with prior DMA (203 ns/cmd) [measured, KBENCH, synthetic] | Stage 0: GT 44,000 commands per frame (up to 76,000) → 9–15 ms without sublist cache; RR and SH ~9000 → ~2 ms |
| Vertices | 7.2 ms per MB by DMA [measured]; **the GPU reads the PSP's RAM at 17 M vertices/s, same as its memory** [measured, stage 1] | GT 0.5–1 MB, SH up to 3 MB per frame → do not copy: the GPU reads them from the PSP's RAM |
| Draws in GXM (CPU) | **Stage 1 [measured]**: ~1.0 µs with no changes; ~1.9–2.2 with a new texture (8888 or P4 + palette); ~2.1–2.4 with a new program; ~1.9–2.2 with new uniforms; ~3.0–3.5 with texture + uniforms; 1 draw of 2000 quads = 40 µs. Same in a deferred context (another thread); executing what was recorded ~30 µs | RR (~1100 draws) ~2–3 ms; SH (~320, up to 1400) < 1–3 ms. **[warning] GT** (~9000 draws, ~5300 with a new texture): by the synthetic count 17–30 ms → might not fit in one thread at 60 fps; **not confirmed with real lists** |
| Draws in GXM (GPU) | **Stage 1 [measured]**: ~1.2–1.5 µs per draw even if it paints nothing (2000 off-screen draws = 2.5–3.1 ms); +0.4 ms per extra scene; 1 draw of 2000 quads ≈ 2 ms in total | RR, SH comfortable. **[warning] GT** ~11–14 ms in draws alone (same caveat) |
| Textures | un-swizzle and hashes with NEON, not measured | Stage 0: GT 400–700 different textures (0.5–1.1 MB), RR 20–124, SH 17–109 (the big thing in SH is its rt, which are already on the Vita) |
| VRAM mirror | GE copy ~2.3 ms per MB [measured] | GT 370–700 KB, RR 70–460 KB, almost all fixed; SH 0 (§3.5) |
| Vita memory | **Stage 1 [measured]**: ~33 MB of LPDDR + 4 MB of CDRAM in Adrenaline's process, without gepresent's 24 MB dump ring (with it: 6 + 7 MB) | HD surface 960x544 8888 + z ≈ 4 MB; texture cache and deferred contexts with the rest. **The dump ring has to shrink or be requested only when dumping** |
| GPU | SGX543MP4+ at **222 MHz** (ARM 444, bus 222) [measured, stage 1]; tile-based rendering with hidden surface removal | SH has 22–24 fill screens with z per frame: tile-based removal should absorb most of them (opaque) [deduced]; blending does not. Triangles per frame: GT 30,000–60,000, SH up to 79,000 |

- The work is split across 2 ARM threads (capture and translation) + the GPU, in a pipeline with 1 frame of lag.
- **Merging draws** (consecutive draws with the same state/texture into a single draw; reordering opaque ones with z by
  texture) is the biggest lever that stage 1 measured: it lowers CPU and GPU at the same time. **Recording on several
  threads** did not scale in the bench (threads without fixed affinity). **Stage 2 (`etapa2_nucleos.md`, with GT in a
  race):** recording does spread across cores (without blocking), but ~1.5 cores remain free (core 1 belongs to the
  capture, core 2 to the sound) and 9000 draws need ~24 ms of processor (best case ~26 ms of wall clock with 2–3
  threads) and ~16 ms of GPU: **GT does not fit when recording draw by draw**; it needs to reuse recorded lists of stable
  sublists and/or merge draws from the start.
- Light games (RR, SH, most of the catalog) should run with room to spare. **GT is the worst case** because of the
  number of draws and because it depends on the VRAM mirror; the warning above is confirmed or discarded with its real
  lists.
- Battery and temperature: the ARM polling and the GPU working use more than today.
- If the drawing falls behind: the screen repeats frames but **the game is not slowed down**. If the capture falls
  behind: the game waits at the sync (normal mode) → this would lower the game's fps. That is why the capture has to be
  as light as possible (copy and note down; translate later).

---

## 6. Roadmap by stages (each with its question and its criterion)

| Stage | What | Question / criterion to continue | Vita / authorization |
|---|---|---|---|
| **0 (done)** | `tools/carga_vita.py` over the 90 `.gpd` (GT, SH, RR; there are none of LocoRoco) + generated `.ppdmp`. **Results in `etapa0_resultados.md`** | GT ~9000 draws (5300 with a new texture), RR ~1100, SH ~320; no VRAM texture with an unknown origin, but GT and RR have fixed data in VRAM (mandatory mirror; GT includes sublists and vertices); SH does not need a mirror | No (PC) |
| **1 (done)** | GXM test bench inside Adrenaline (gepresent v11.3, off by default). **Results in `etapa1_banco_gxm.md`** | Draw with no changes ~1 µs, (b) texture ~2 µs, (c) program ~2.2 µs, texture + uniforms ~3 µs; deferred context works (the Adrenaline thread only executes); GPU ~1.3 µs per draw; GPU reads the PSP's RAM; presenter works; ~33 MB + 4 MB of memory. → **Continue with B**: RR/SH/light catalog with margin [estimated]; **GT with a warning** (might not fit without merging draws; confirm in stage 2) | Yes (done) |
| **2 (done, 2026-10-29)** | GePatch "shadow mode" (passive + hooks to the mailbox) + capturer on the ARM + T1 (including **batches that could be merged**), T2, T4, T6 + **VRAM mirror** (on load + verification; GT's sublists per frame). Repeat the recording measurement on 2–3 threads with fixed affinity | Does the capture reach the end before the sync in GT and SH? Does the game run at its original fps with shadow mode? Does GT's mirror stay up to date? How many real GT draws remain after merging? It already leaves the black box useful even if B did not continue. **Result** (dev66 + v13.12; `etapa2_fase1_impacto.md`, `etapa2_fase2_*.md`): with shadow mode GT runs at 59.9 fps almost the whole race and ≥ 58 in the heaviest parts (75–85 thousand commands, ~15,000 draws, ~8,700 batches per frame); SH does not wait for the Vita; the double mirror stays up to date (0 DISTINTAS since v13.12). Recording draw by draw does not fit for GT even on 3 cores (`etapa2_nucleos.md`): the recordings of stable sublists have to be reused and draws merged | Yes |
| **3** | Minimal renderer: clears, TRANSFER, 2D draws (through) with 565/5551/4444/8888 textures and CLUT, basic blending; HD presentation; per-frame fallback to native; T5 | Does the full chain (capture → GXM → screen) work without slowing the game down? Test on a 2D game (Retro City Rampage DX) and on the menus | Yes |
| **4** | 3D: matrices, viewport, z, guard band discard at 1x, near plane, swizzle/DXT textures, alpha/stencil, fog, lights, bones | LocoRoco → **SH (does not need a mirror)** → RR → GT, against the native image (T5) and the collection | Yes |
| **5** | Framebuffer manager: rt in HD, texture from an fb (with offset), depal, requested VRAM mirrors | GT's reflections, RR's rear-view mirror (**only when the user asks for it**), SH's flashlight | Yes |
| **6** | Performance: cache of translated sublists, merging draws, hashes on a schedule, ubershaders → compilation on the console if needed; quality options (reduced 3x, MSAA) | Screen fps = game fps in the 4 games | Yes |

Meanwhile, **current GePatch (A)**: close dev58/dev59 with a commit and use it as the "GE" mode per game. Do not invest
in the big fixes of A that B solves by design (game format in the big buffer, rt in the free VRAM, hot textures): only
cheap fixes and what is reused in B (for example the `struct Fb` table, which is the seed of the framebuffer manager).

---

## 7. Earlier conclusions that touch this plan (and how to check them)

| Earlier conclusion | Does it block B? | What to review |
|---|---|---|
| "Passing commands to the Vita's GPU is not worthwhile" (`analisis_offload_gpu_vita.md`) | **No.** It was about splitting a frame between the GE and the Vita (they needed to share the z in VRAM). In B the Vita draws the **whole** frame with its own z | — |
| "Emulating the GE on top of GXM: discarded" (`filosofia.md`, route G) | No: it was discarded for complexity, which is no longer a criterion | — |
| The ARM walks as fast as the PSP (KBENCH) | Affects the budget | It was with a synthetic list, 1 thread. **ARM at 444 MHz confirmed** in Adrenaline's process (stage 1). Repeat the walk in stage 2 with real lists |
| VRAM is not mapped in Adrenaline (B4) | Forces the mirrors (§3.5) | Stage 0: **no texture without an origin**, but GT (textures, sublists, vertices) and RR (textures) keep fixed data in VRAM → mandatory mirror for them. Investigate the physical address of VRAM from the Vita's kernel (risky, low priority) |
| The native image arrives at 0x74000000 in normal mode | It is B's fallback | **Stage 1: the bench drew it inside its surface [measured]**; the black screen during presentation remains open (§3.5) |
| vpjuego: "GE in stall" always 0 % | Affects T6 | Use enqueue/FINISH times instead of sampling |
| Does SH do 2 flips per frame? | Affects the presenter | The dumps show 1 flip and almost the whole frame drawn **after** the flip in the same list; T4 confirms the pace |
| A list with context enqueued from the plugin freezes Adrenaline | B hardly enqueues lists of its own | The mirrors use the same path as the current copy per flip |
| A Vita plugin can change the shader and the texture of Adrenaline's drawing | It is the basis of the presenter | [measured with adrfsr 3b and with the stage 1 bench] |
| The GPU only reads memory registered with `sceGxmMapMemory` | For the GPU to read the PSP's RAM directly | Solved in gepresent; **stage 1: vertex reading from 0x08800000 at no extra cost** |
| **GPU experiments with GePatch active** (stage 1) | Stability risk | Bench v11.3 running with GePatch active (2x, `DRAW_NATIVE`) shut down the whole Vita. Cause not confirmed. Until that is understood: B's new work is tested with GePatch in passive/off (L) |

---

## 8. Main risks
1. **Cost of the draws in GXM and of the ARM** (stage 1 measured it): ~2 µs per draw with a new texture or program,
   ~1.3 µs of GPU per draw. **[warning]** GT has ~9000 draws per frame: by the synthetic count it might not fit at 60
   fps without merging draws or spreading the recording across several cores. It is taken as neither true nor wrong
   until measured with GT's real lists (stage 2). If confirmed: GT would need to merge draws (atlas, reorder opaque
   ones by texture) or stay in A at ≤ 1.5x.
2. **VRAM mirror** (GT, RR): fixed data that the game uploads to VRAM; if it changes without notice, the Vita draws with
   old data. GT also needs the mirror in order to walk its lists.
3. **Integration with Adrenaline:** the drawing thread of another program is hooked. Each Adrenaline version can break
   it. In the medium term, propose to isage/Adrenaline an official hook point ("external presenter"). **Stage 1:** with
   GePatch active + a GPU experiment the whole Vita shut down (cause not confirmed).
4. **Memory of Adrenaline's process:** ~33 MB of LPDDR + 4 MB of CDRAM (stage 1). Enough for B, but not together with
   gepresent's 24 MB dump ring; the texture cache has to have a cap.
5. **Translator compatibility:** PPSSPP took years to cover the catalog; the fallback to the native image prevents an
   error from leaving a game unusable.
6. **Data that changes without notice** (dynamic textures, buffers reused in the middle of a frame): full hash and
   strict gate per game.
7. **Stutters from shader compilation** if `libshacccg` is used: cache per game.
8. **Memory of the deferred contexts:** recycling it too early makes the GPU crash (bench v11.2) [measured].

---

## External sources consulted
- PPSSPP, description of the GE (clipping only at the near plane, 4096 guard band): https://www.ppsspp.org/docs/psp-hardware/gpu/ge-overview
- PPSSPP, vertex range discard rule: https://github.com/hrydgard/ppsspp/issues/11393 and its failed approximation in TDU: https://github.com/hrydgard/ppsspp/issues/13355
- PPSSPP, `.ppdmp` dumps and the GE debugger: https://github.com/hrydgard/ppsspp/issues/11216
- Vita3K, GXM programmable blending: https://vita3k.org/blog/2021-07-31-3-years-Progress-Report
- vitasdk `gxm.h` (palettes, contexts): https://docs.vitasdk.org/gxm_8h_source.html
- vitaGL / vitaShaRK (`libshacccg.suprx`): https://github.com/TheOfficialFloW/vitaGL, https://github.com/Rinnegatamante/vitaShaRK
- Shader compilation time on the Vita (Half-Life 2 port): https://github.com/LargeVegetable/source-engine/releases
