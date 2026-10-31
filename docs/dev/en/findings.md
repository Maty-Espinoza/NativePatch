# Findings, measurements and limits

What was learned by testing on the Vita, summarized and ordered by topic. Each point says in which version or stage it
was seen; the detail (full numbers, logs, test versions) is in `../../changelog/` and, for stage 3, in
`../../agent/en/stage3-plan.md`. Marks: **[measured]** verified on the Vita; **[deduced]** conclusion without direct
proof.

## The hardware
- The Vita carries the PSP's hardware inside its chip: a real MIPS CPU and graphics chip (GE). The game is not emulated
  in software. The MIPS has no access to the screen, the card, the audio or the controls: all of that is handled by the
  Vita's ARM through remote calls (Kermit). Details: `adrenaline.md`.
- **The PSP's VRAM (2 MB) is not mapped in the Adrenaline process** [measured]: the Vita cannot read it. That is why
  the mirror exists: NativeLink asks the GE to copy to normal RAM the pages the Vita needs.
- The PSP's RAM is seen from the ARM at `0x68000000 + address`, uncached. **Reading it is the expensive part** (~2 µs
  per 64-byte line) [measured]: almost all of the capturer's cost is that wait, not interpreting. Reading 256 bytes at a
  time with NEON overlaps the waits; DMA (`sceDmacMemcpy`) copies at ~512 MB/s versus ~74 MB/s for the CPU, but each
  call costs ~100 µs.
- **The Vita's GPU can read the PSP's RAM directly** if it is registered with `sceGxmMapMemory` (0x70000000, 32 MB),
  at the same speed as its own memory [measured, stage 1]. With 0x72000000 it fails.
- ARM at 444 MHz, SGX543MP4+ GPU at 222 MHz, bus at 222 MHz [measured, stage 1]. In the Adrenaline process there are
  ~33 MB of free LPDDR and 4 MB of CDRAM.
- Reserving **CDRAM** inside Adrenaline breaks the Vita's videos (previews, cutscenes: static or black). There were only
  4 MB and the videos use them. **Never CDRAM** [measured, 0.0.3.2 and 0.0.8.14].
- With a render target of its own in GXM you must **always use a z surface** (D24S8): without z the GPU does not draw
  or Adrenaline freezes [measured, 0.0.3.3–0.0.3.4].

## Why the architecture was changed (GePatch → NativePatch)
- GePatch made the PSP's GE draw 4 times more pixels. This is a **physical limit of the GE** (pixel fill), not a bug:
  the time ratio with/without GePatch was 3–4, the same as the number of pixels. Gran Turismo dropped to 24–37 fps,
  Silent Hill to 15–21 [measured]. No CPU optimization could fix it.
- It also tricked the game with a fake VRAM in RAM, and almost all the image errors came from that.
- The only other GPU available is the Vita's, and the only free processor is the ARM: the PSP draws normally (nobody
  sees that image) and the Vita draws in HD. If it does not know how to draw something, it shows the PSP's image. Full
  analysis: `architecture-and-roadmap.md`.

## Capturing without slowing the game down (stage 2)
- With NativeLink and NativeDraw's capturer active, Gran Turismo runs at 60.2 fps in its heaviest part (~80 thousand
  commands per frame, 13.4 ms of Vita per frame), Ridge Racer at 60 for the whole race, LocoRoco at 59.9 and Silent
  Hill at its original speed [measured, v13.13 = NativeDraw 0.0.1].
- What made it possible: **sublist memory** (a sublist identical to the previous frame's is applied without walking it;
  in GT ~85 % of the commands come from sublists recognized as equal), **read-ahead readers** on cores 0 and 2 that
  fetch the lines into a shared cache, a **look-ahead check** of the sublists that are about to be applied,
  **interpreting line by line** (64 bytes), and the **gate before enqueuing** the next list instead of at the sync.
- A capturer pinned to one core loses ~17 % of its time (it shares the core with the audio or the Media Engine): leave it
  free on all 3 cores [measured, v13.13].
- The gate waits at most 5 ms and never if the Vita does not respond: the game does not hang even if NativeDraw fails.
- **The game does rewrite its lists**: the memory of frame N's list is reused when the next one is enqueued. That is
  why the gate goes before enqueuing, and NativeLink verifies it by itself with hashes (if a list changes earlier, the
  gate goes back to the sync).
- Permanent check: 1 in 32 applied sublists is walked anyway and compared (DISTINTAS in the log). It must be 0 and it
  has been since v13.12 (before, 1–7 per run because of comparing against the wrong mirror).
- What failed: reading the PSP's RAM directly 256 bytes at a time instead of the shared cache (v13.11.1), not rereading
  lines already checked (v13.8), FSR 1 on the Vita side as a shortcut (it did not improve the image and gave stutter
  and tearing).

## Drawing on the Vita (stage 3)
- **Presenter:** replacing the 512x272 texture that Adrenaline draws with a surface of its own at exactly double size
  works and costs ~0.15 ms per frame [measured, 0.0.3]. Adrenaline places and scales the surface the same as its
  texture, so the scale, position and aspect ratio of its menu are respected. The image appears one frame later.
- The `wvp` matrix of the vita2d shaders is **column-major** (translation in [12] and [13]; `mul(pos, wvp)`)
  [measured, 0.0.3.1: an orange trapezoid until it was understood].
- Adrenaline copies the PSP's image to its texture with `sceDmacMemcpy` on every loop. The GPU reads it for NativeDraw
  later than for Adrenaline, and the next frame's copy overwrote it (stripes): it is diverted to 4 alternating copies
  of our own [measured, 0.0.3.5 and 0.0.8.x].
- **One-frame broken frames** (textures with foreign pieces, deformed letters, wrong colors): the Vita's GPU was 3 to 8
  scenes behind and the CPU rewrote vertices, palettes, textures and the image copy while the GPU was still reading
  them [measured with the GXM notifications, 0.0.8.10]. Solution: each scene of ours writes its number into a free word
  of the GXM notifications when it finishes; another frame is not sent if the one from 2 frames ago has not finished;
  before rewriting we wait (at most 30 ms); the next record is one the GPU has already finished reading. Rewrites
  while the GPU was reading: 0 [measured, 0.0.8.11].
- Reading a surface the GPU has not finished gives false results, and waiting for it with `sceGxmFinish` costs ~20 ms
  in KOF 96: the checks (different frames, "covered", black-screen safety) only look at finished surfaces, or are
  skipped.
- **Textures:** the game's textures in the PSP's RAM must be copied right away (the game changes them before the GPU
  reads them); those in VRAM are copied from the mirror copy of **that** frame's flip (KOF uploads the sprites one frame
  after the previous flip: with the previous copy, mosaics appeared). Only the groups of 8 rows that the vertices use
  are copied: copying whole 512x512 sheets cost 7–17 ms per frame [measured, 0.0.7.5].
- **Swizzle:** the GE stores textures in blocks of 16 bytes x 8 rows. Unswizzling in the shader cost ~8 ms of GPU per
  frame in KOF 96 (intermediate image at 16 ms); unswizzling with the CPU when copying (DMA to a cached staging memory,
  which DMA does keep up to date) brought it down to 7–8.7 ms and allows a bilinear filter [measured,
  0.0.8.15–0.0.8.16].
- **One scene per render target, and the surface last:** each GXM scene reads and writes its entire surface; if only
  the draws to the surface read intermediate images, all the intermediates go first and then the surface in a single
  scene (~3 ms less). With that, **KOF 96 at 60 fps in the fights** (~16 ms of GPU per frame, GPU at 87–97 %)
  [measured, 0.0.8.18].
- **What repeats between frames:** ~98–99 % of the texture bytes copied each frame are equal to the previous copy
  (KOF 96 and Metal Slug XX) [measured, 0.0.8.19]. The expensive part is the DMA (~100 µs per call; Metal Slug 10–17 ms
  per frame in 90–170 copies). Hence the next step: a texture cache between frames.
- **Capturer behind = garbage vertices:** if copying textures takes longer than a frame, the gate (5 ms at most) lets
  the game through and vertices the game was already rewriting get copied (positions of ~2^31, uv 65535; the GPU filled
  the whole screen with one texture: Metal Slug XX's "white glitch"). A vertex outside ±4096 sends the frame with the
  normal image [measured, 0.0.8.20–0.0.8.21]. The texture cache attacks the cause.
- Neo Geo games make transparencies by drawing something one frame on and one frame off: if the Vita repeats or skips
  frames, it flickers. It is measured with the `traductor: ritmo` line of the log.
- **Recording the screen** (Vitastream + OBS at 60 fps) found the one-frame broken frames that no dump captured. It is
  used only when dumps and logs are not enough.

## Gran Turismo and 3D (what comes next)
- GT has ~9000–15,000 draws per frame, ~5300 with a new texture. In GXM a draw costs ~1 µs without changes, ~2 µs with
  a new texture or program, ~1.3 µs of GPU even if it paints nothing [measured, stage 1]. **Recording GT draw by draw
  does not fit** even on 3 cores (~24 ms of processor and ~16 ms of GPU per frame; ~1.5 cores left free) [measured,
  stage 2 v13.7].
- Planned path: record each stable sublist once and reuse the recorded GXM list (in GT ~85 % of the commands come from
  sublists the capture already recognizes as equal) and join draws with the same state (2000 quads in one draw =
  0.02 µs per quad).
- GT and RR keep fixed data in VRAM (textures, sublists, vertices): they need the mirror. SH does not.

## Known limits
| Limit | Why | What is done |
|---|---|---|
| 3D is not drawn | the translator only understands "through" mode (2D in pixels) | normal image; stage 4 |
| One frame of delay in the HD image | the Vita draws frame N while the game builds N+1 | the typical latency of an emulator with triple buffering |
| PS1 games | Adrenaline 8 shows them from the Vita's PS1 emulator (`pops_tex`), not from the GE | not touched; that emulator would have to be hooked (another project) |
| Adrenaline's "Original" filter | Adrenaline does not draw with GXM and the presenter cannot act | use Bilinear |
| What the PSP's system draws on top (PS button dialog) | it does not go through the game's lists | "covered": compared with the normal image, and the normal one is shown while it lasts |
| Frames with sublists applied from memory | their draws do not go through the record | normal image (2D games hardly use sublists) |
| A texture changed only in bytes the check does not see may look stale for a few frames | accepted risk of the texture cache (design in progress) | full check in turns |
| Blends with no direct equivalent in GXM (destination alpha, doubled factors), color test, logic operations | fixed-function GXM does not have them (it does have programmable blending, not used yet) | normal image, reason `mezcla` / `test_color_o_logica` |
| Dependency on Adrenaline | imports of AdrenalineUser and ScePspemu are hooked | each Adrenaline version may break it; in the future, propose an official hook point |

## Things that were measured and were not what they seemed
- vpjuego: "GE in stall" always 0 %: for the GE's timings use the enqueue and FINISH times (T6).
- Core 0 of the `procesador` log line comes out almost empty: the CreateThread hook does not see the threads Adrenaline
  created before loading NativeDraw.
- Reading the clock on the Vita costs ~390 ns: the full extreme log alters what it measures.
- `sceCommonDialogIsRunning` always says "no" even with the PS button dialog open: useless for detecting it.
- The PSP's DMA functions (`sceDmacMemcpy`) were not found under any module name in Adrenaline 8 (T4 incomplete; it
  was not needed).
