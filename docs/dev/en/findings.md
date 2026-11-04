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
- Permanent check: 1 in 32 applied sublists is walked anyway and compared (MISMATCHES in the log). It must be 0 and it
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
  per frame in 90–170 copies). Hence the texture cache between frames.
- **Texture cache (RAM textures):** textures stay sorted in a store the GPU reads, checked by sampling. In Metal Slug XX the
  capturer went from 19.4 to 8.3 ms per frame, textures from 14.5 to 4.1 ms, copies from 746 to 3 KB and DMA calls from 44 to
  0, with nothing out of the store and no visual errors reported [measured, 0.0.10.1]. The check itself costs ~2.5 ms per
  frame. VRAM textures (KOF 96 fights, DMA ~1 ms + sort ~2 ms per frame) are not cached.
- **Cache sample and small textures:** words spaced exactly one row apart all land in the first column, usually transparent, so
  a whole new sprite goes unseen. Street Fighter Alpha 3 MAX rebuilds its characters from 64x64 T4 textures of 2 KB in RAM, and they
  came out with foreign pieces. Now each word is in another column, and textures under 8 KB stay out of the cache: they cost
  little to copy and change often. Metal Slug XX stays at capturer 9.7 ms, textures 5.3 ms, check ~3.2 ms [measured, 0.0.11.3].
- **Street Fighter Alpha 3 MAX in HD** (fans, 0.0.11.3): ~700 draws per frame, the intermediate image takes ~16 ms of GPU and the
  surface ~4 ms, and the translator ~57 % of a core: the fight shows ~32 HD frames per second while the game runs at 60.
- **Capturer behind = garbage vertices:** if copying textures takes longer than a frame, the gate (5 ms at most) lets
  the game through and vertices the game was already rewriting get copied (positions of ~2^31, uv 65535; the GPU filled
  the whole screen with one texture: Metal Slug XX's "white glitch"). A vertex outside ±4096 sends the frame with the
  normal image [measured, 0.0.8.20–0.0.8.21]. The texture cache attacks the cause.
- **Metal Slug XX on 0.0.8.23** (run `stage3_0.0.8.23/ms`): 30 fps held through gameplay (121 s, none below 30). The
  capturer takes ~25 ms of each 33 ms frame (p90 30 ms); texture copies: 358 KB per frame, DMA ~5 ms + unswizzle ~3 ms.
  2 slow spots: ~2 s at 38–40 ms per frame with peaks of 70 ms. One burst of 174,353 commands in a frame: 182 ms, 25,054
  draws, 2 frames hit the record cap; 44 `vertex_out` frames in that window [measured].
- **Cost of copying textures** (Metal Slug XX, 27 seconds of 0.0.8.24): DMA ≈ 98 µs per call + 2 µs per KB (R² 0.998). Most
  copies are under 1 KB, so the calls are the cost, not the bytes. Under 8 KB the CPU copies instead [measured,
  0.0.8.26]: in seconds with ~390 draws the capturer went from 34 ms to 22 ms per frame. Sorting got slower (~5.9 → 8.4 ms),
  because it reads the PSP's memory directly, but the total gain is ~11 ms.
- **`vertex_out` in Metal Slug XX comes from the capturer running late** (one sample, 0.0.8.25): the vertex memory held GE
  commands of a rewritten list, with the real vertices 20 bytes later, and the gate was at its limit around that frame.
  With the capturer on time (0.0.8.26) the same game had 0 `vertex_out`, 0 frames at the limit and held 30 fps [measured].
- **Street Fighter Alpha 3 MAX did not open** (C1-2858-3 or 80020148, only with NativeLink): the game changes the image
  before anything else, and NativeLink read the game ID from the disc in that first call. The crash was inside that
  function; the ID now comes from kernel data [measured, 0.0.3.10].
- **Black image with good data missing:** the capturer started at the events present when it saw the mailbox and skipped
  the game's first lists. This game sets the alpha test only there, so every draw had "never pass". The capturer now
  walks the ring from its start [measured, 0.0.8.29]. Fights in this game use triangle fans, not drawn yet: normal image.
- **3D on the Vita (Ridge Racer):** the translator does the GE's vertex work on the CPU; x, y and z keep their clip w, so the GPU clips
  and corrects perspective. Selector [measured]: translator ~30-38 ms per frame, GPU ~31 ms for 4.2 M pixels and 1000 draws.
- **Sublist memory and the record do not mix:** a skipped sublist leaves the frame incomplete. It now runs only for frames over 4096
  draws. Ridge Racer skipped 6 % of its commands and lost nearly every frame to it [measured].
- **The capture can miss GE state** set before it reads the first lists; which registers changes between runs, so it fails only some
  times (Ridge Racer: `c9`, black backgrounds). `sceGeGetCmd` gives the real values, and works in Adrenaline [measured].
- **What decides the game's fps in Ridge Racer:** ~10 ms of its own work plus two waits at the gate of up to 5 ms each. Over 16.6 ms in
  total it drops straight to 30 fps [measured]. Its list changes before the next enqueue, so the gate stays at the sync.
- **Reading is the limit, not sorting:** hashing textures to skip the sort costs about the same as sorting. Ridge Racer's 3D vertices
  are scattered and outside the list; DMA over whole windows made the capturer slower (26 ms) [measured].
- **Stutters in the Ridge Racer race came from writing `nativedraw.log` inside Adrenaline's drawing thread** (each change of the
  translator's result wrote a line; a write to the card takes tens to hundreds of ms): presenter peaks of 50-558 ms with the
  translator at ~0. With the log through a ring and the writer thread, the peaks are 5-8 ms [measured, 0.0.13.6].
- **An HD frame in the race costs ~100 ms of CPU** (1900 draws; 76 ms are the 3D vertices), and in the race the game alternates every
  2 frames between drawable and drawing to another target (`other_target`): an isolated HD frame is a stutter plus a one-frame
  jump back. HD now waits for a second of drawable frames; the race stays on the normal image at 59.9 fps [measured, 0.0.13.7].
- **Adrenaline runs the GPU at 222 MHz** (crossbar 111, CPU 333, bus 222): the selector's ~31 ms of GPU are not a low clock [measured].
- **The mirror copy is seen finished ~20 ms after the flip** (`mirror_ge`, upper bound: the capturer looks between lists), and NativeLink
  never found the copy from two frames ago still in the GE (`busy=0`) [measured, 0.0.13.6].
- **Palettes rebuilt per draw cost 116 ms per frame** in the selector: 1000 draws of 256 entries each [measured].
- **Ridge Racer's bloom:** shrinks the screen into 088000 (the z memory), subtracts, blurs by adding itself 4 times with a gray fixed blend (src and dst * 4b4b4b) and adds it to the screen with a color test. Almost every frame [measured, 0.0.14].
- **Depth does not survive a scene cut:** with the cut between the clear and the world, the world vanished (background depth 1.0 is "near" for the GE's GEQUAL). Reordering avoids it; otherwise force store and load [measured, 0.0.14.5].
- **A late capturer reads rewritten lists and the garbage stays in the state:** emissive values like 0x400011 tinted the whole world for seconds, until the game set the register again. Recording only frames the presenter takes, and restoring the last clean state, solved it [measured, 0.0.14.8-0.0.14.10].
- **Lens flare and lamp halos are computed by the game with the GE:** it samples 64 pixels with points, draws a palette with the result and draws T8 sprites with it. The Vita reads the result from the mirror, one frame late [measured, 0.0.14.13].
- **Dumps from a surface the GPU has not finished, and scenes on different render targets:** Adrenaline could show a surface half drawn. Showing the older one fixed the one-frame tiles [video, 0.0.14.11].
- **sceGxmCreateRenderTarget:** at most 8 scenes per frame (24 gives 805b0003, and the presenter stays off) [measured, 0.0.14.2].
- **A valid region is also the viewport:** a scene with region 128x128 on a 1024x544 target drew everything shrunken by 128/1024 and 128/544 (16x30 pixels), and bloom came out 228 wide instead of 484 [measured with `peek`, 0.0.15.9]. The matrix has to scale to the region. Reflections read the stale rest of the image: the white blocks.
- **Uncached memory:** reading a word costs 153 ns against 21 with cache; writing, 9 against 6 [measured]. What hurts is reading back, not writing. The clock (`sceKernelGetProcessTimeLow`) costs ~0.6 us: per-draw timers add milliseconds in dev builds.
- **A vertex at 333 MHz:** 0.92 us 3D, 1.73 lit with no lights, 1.83 with fog, 2.86 with one light [measured on synthetic draws, 0.0.15.5]. In Ridge Racer almost no lit vertex has a light on. 3.3 us in the game, with cold data.
- **CPU and GPU one after the other were the limit:** 40 + 40 ms gave ~12 HD frames per second; overlapping them, ~22 [measured]. Now the GPU sets the pace away from the crowd (95 % busy) and the CPU in the crowd.
- **CPU 444 and crossbar 166** (`clocks`): translator 50-65 ms -> 32-45 ms, and the capturer fits in the crowd [measured]. Plan B, the goal is not to need them.
- **Nitro reads the image of the frame before** (04000000 or 04044000) as a texture; 92 % of Ridge Racer's draws share the state of the one before.
- **Image 195140 is marked 69 pages high** by estimate, over real textures: requests were dropped and, once the list of images was full, those textures passed for images.
- **128 textures per frame was too few:** Metal Slug XX reached it with 0.8 of 1.5 MB in use and lost frames. It is 256 now
  [measured, 0.0.8.25–0.0.8.26].
- Neo Geo games make transparencies by drawing something one frame on and one frame off: if the Vita repeats or skips
  frames, it flickers. It is measured with the `translator: pace` line of the log.
- **Recording the screen** (Vitastream + OBS at 60 fps) found the one-frame broken frames that no dump captured. It is
  used only when dumps and logs are not enough.

- **The GPU can run the GE's vertex work:** on Ridge Racer `nd_v3` draws the same triangles as the CPU path (`tools/perf/tr_gv.sh`), with no image error in the race. Limits so far: rear-view mirror and other vertex types stay on the CPU.
- **Frames saved from a frame or two are not the race:** they gave 6-12 % of the vertices under a scissor and 71-87 % eligible; the race gave up to ~50 % scissored and 54 % on the GPU.
- **Crowd in Ridge Racer:** when the capturer falls behind, a late list can pass as a complete frame with garbage state (0x27=ade1f9). The translator refuses it and the normal image shows until the next HD frame, 4-16 game frames there. 1 to 8 per run, no proven link with C.1.

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
| A texture changed only in bytes the check does not see may look stale for a few frames | accepted risk of the texture cache (up to 16 frames) | full check in turns |
| Blends with no direct equivalent in GXM (destination alpha, doubled factors), color test, logic operations | fixed-function GXM does not have them (it does have programmable blending, not used yet) | normal image, reason `blend` / `color_test_or_logic` |
| Color mask per bit (0xE8) and alpha test masks that are not high bits | GXM masks whole channels and the shaders compare the whole alpha | normal image, reason `mask` |
| Depth and stencil across scenes | they live in one GXM scene (no force load/store), so they do not survive a change of target or frame | what relies on them is drawn wrong |
| Dependency on Adrenaline | imports of AdrenalineUser and ScePspemu are hooked | each Adrenaline version may break it; in the future, propose an official hook point |

## Things that were measured and were not what they seemed
- vpjuego: "GE in stall" always 0 %: for the GE's timings use the enqueue and FINISH times (T6).
- Core 0 of the `processor` log line comes out almost empty: the CreateThread hook does not see the threads Adrenaline
  created before loading NativeDraw.
- Reading the clock on the Vita costs ~390 ns: the full extreme log alters what it measures.
- `sceCommonDialogIsRunning` always says "no" even with the PS button dialog open: useless for detecting it.
- The PSP's DMA functions (`sceDmacMemcpy`) were not found under any module name in Adrenaline 8 (T4 incomplete; it
  was not needed).
