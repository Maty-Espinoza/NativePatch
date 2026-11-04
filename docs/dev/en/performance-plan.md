# Performance plan: 60 fps with the Vita drawing

The single list of performance work, in order. It comes from the analysis of the capturer and the Ridge Racer run
`stage3_0.0.16.4/rr1` (2026-11-03; detail in the claude.ai project, `claude/analisis_capturador_2026-11-03.md`). Each item
says how it is verified: **bench** (`tools/perf/`, on the PC, `../../agent/en/perf-loop.md`) or **Vita** (a test on the
console). Done items move to `CHANGELOG.md`; results of tries stay here under the item.

## Where the time goes today (Ridge Racer, high clocks)
Three stages work in parallel on three cores: capturer, translator (Adrenaline's drawing thread) and GPU. For 60 HD frames
per second each must stay under 16.7 ms per frame.

| Stage | Today | Needed |
|---|---|---|
| Capturer | 7 ms average, **24 ms in the crowd** (3500 draws, 545 KB of vertices per frame) | ~2x in the crowd only |
| Translator | 30-36 ms per HD frame (16-20 ms are 3D vertices transformed on the CPU) | ~2.5x |
| GPU | 35-39 ms per HD frame (surface 32 ms: 4.8 M pixels, 98 % blended) | ~2.5x |

The capturer is the only stage that costs the game fps (the gate makes the game wait for it: 55 fps in the crowd); the
others only lower the HD rate. In the crowd second, 44 of 55 frames were not drawn anyway (capturer behind), so that wait
was paid for nothing. The capturer's 24 ms are, roughly: vertex copy ~5-6 ms (`rgVFlush`, memcpy from uncached PSP RAM at
91 MB/s), record bookkeeping 4 ms, list reads ~3 ms, walk and `draw()` ~5-8 ms, closing the frame.

## A. Confirm first (Vita)
1. **`cap_split` on 0.0.16.5 in the crowd** (`ux0:data/nativedraw/cap_split`): the `capture split` line separates events,
   flip, walk, hit, own, draw, record, state, vertices, textures and `vflush`. Expected: `vflush` ~5 ms, `draw` several ms.
   Until this is measured, B.1-B.3 rest on an estimate.
   **Result (`stage3_0.0.16.5/rr1`, without high clocks, ms per frame; each clock read adds 0.4 us, up to ~3.2 us per draw):**
   a complete frame of ~1450 draws and 11,000 commands costs ~18-19 real ms (24 measured): record ~10 (vertices ~4.7, textures
   ~3.8, state ~1), interpreter 5, list reads ~2, `draw()` under 0.5, end of list and events 1-2. A crowd frame of ~2800 draws
   and 17,800 commands, already incomplete and copying nothing, costs ~13 real ms (17 measured): interpreter 7, reads ~3. So
   a crowd frame does not fit even without recording; recording it would take ~33 ms (never measured: none was complete).
   Against the estimate: `vflush` and the list-end flush are at most 1-2 ms in the frames that complete, and `draw()` is cheap.
   The 21 ms read before 0.0.16.5 counted the walk at the flip twice. Bug: the `events` column overflows (ignore it).
2. **`data life` at the extreme level in the race**: how often vertices change 1 and 2 frames after their draw. Decides
   B.2 and C.1 (whether the GPU or a later copy can read vertices the game may rewrite).

   **Result of 2 (`stage3_0.0.16.8/rr2`, 53,785 vertex samples in the race):** 0.5 % change one frame later (~13 ms) and 21.5 % two
   frames later (~30 ms); textures and palettes 0 %. The GPU would read them 30-60 ms after the flip: **C.1 cannot read the PSP RAM
   directly**. B.2 stays possible (the copy happens under 16 ms); C.1 must read the record's copy. Textures could live longer than now.

## B. Capturer, small changes
1. **Vertex copy with 256-byte NEON loads** instead of `sceClibMemcpy` in `rgVFlush` and `rgCopy` (the loads overlap the
   uncached waits, as `neon256` does for lists). Expected 2-3x on ~5 ms. Verify: bench (`ucp_kb` stays, the unit cost
   model does not see it: measure the copy routine inside the bench with a microbenchmark line) and then Vita (`vflush`).
2. **Publish `SB[3]` before `rgVFlush`** (A.1: only 1-2 ms in frames that complete): today the game waits at the gate for the vertex copy too. Removes up to ~6 ms of
   gate per frame at no cost. Condition: A.2 shows vertices live at least one frame. Verify: Vita (`gate_us`, `vertex_out`).
3. **Vertex and RAM-texture copies on another core** (the `record` thread already exists): the copy of frame N runs while
   the capturer walks frame N + 1. Same latency, double throughput. Verify: Vita (`capture us/f`, `complete`).
4. **Adaptive gate**: when the frame is already incomplete (R_SKIP, behind, record off) NativeDraw sets a bit in
   `SB[0x4A]` and NativeLink skips the wait for that frame: the game keeps 60 fps while the Vita drops the frame. Risk:
   lists read while being rewritten (mitigated by `st_good` and the register snapshot). Verify: Vita (game fps, `late`).
5. **Fewer clock reads**: the record samples 1 draw in 8 with 4 clock reads each: ~1700 reads per crowd frame, ~1 ms on the
   Vita (0.6 us each). Sample 1 in 64, or only at the medium level. Verify: bench (`clock/f`, `mem_us`) then Vita.
6. **`regDraw()` per draw** (A.1: `draw()` itself is under 0.5 ms, the statistics are not worth touching; the time is in
   `vertices` and `textures`, to be split further): linear texture search in `rgTex` (up to ~90
   entries per frame, 3500 draws), `rtRead`/`markRt`/`want` on every draw with a VRAM texture, statistics counters that
   only feed the log (under `level`), state comparison per draw. Verify: bench (`qemu_min_us`) then Vita (`draw`).
7. **Readers' priority**: in the crowd each reader gets 49 ms/s (0xB0 on busy cores); own reads 525 per frame against
   1415 from the readers. Try 0x80-0xA0 or the free core. Verify: Vita (`own` vs `from_readers`, audio).
8. **Mirror size by measured GE time** (TODO: 257 data pages asked, 64 copied per flip; pages used this frame first).
   Verify: Vita (`no_fit`, texture age).

9. **The interpreter's cost per command** (A.1: ~400 ns, ~130 cycles, 5-7 ms per frame, more than half of a crowd frame that
   is not recorded): per-command bookkeeping in `walkList` (`l->n`, `acc.cmds`, `f_cmds`, `l->prev`, `TOUCH`, `cls`). Verify: bench
   (`qemu_min_us`) then Vita (`capture split`).

**Bench results, 0.0.16.6 against 0.0.16.5 (same moment, compare only with other bench runs):** `rr_crowd` qemu -25 %, mem -13 %;
`rr_race` qemu -26 %, mem -12 %; `sprites2d` qemu -13 %, mem -5 %; `sublists` no change. Done: B.5 (1 draw in 64); B.9 in part
(plain and matrix commands in a tight loop, counters once per line); same VRAM rows skip `rgTex`; `draw()` skips the texture
checks when the texture did not change. Tried and dropped: `VADDR`/`PRIM` inside the tight loop (no gain); remembering the
vertex size for `regDraw` (`sprites2d` +10 %, reason not found).
**B.1 will not help as described:** the Vita logs give `neon256` ~2.8 us per 256 B, the same 91 MB/s as `memcpy`. Only DMA is
faster, and above ~11 KB per call.
**Mirror leak (fixed in 0.0.16.6, to confirm):** a texture in a page of an image was requested as a list page and never
released: `lists` grew from ~150 to 368 pages in a session, the PSP spent 3.2 ms per frame on the mirror instead of 1.3 and the
game fell to 40-46 fps. Now those pages expire when no frame reads them. Verify: Vita (`mirror: pages requested lists`, `mirror_us`).

**Bench improved (0.0.16.7):** exact instructions per frame (`ir_k/f`) with a profile by function and line, the translator on real
frames (`tr_*`), the pages asked of the mirror (`want`) and frames that are walked without recording (`*_late`). With them:
`regDraw` skips `vsize`, `rgRows` and `rgTex` for 3D draws with the same VRAM texture: `rr_crowd` -16 %, `rr_race` -15 % instructions.

**0.0.16.7 on the Vita (`stage3_0.0.16.7/rr1`, without high clocks):** game 60 fps in every part, whole race in HD with 0 frames
of normal image, 21-25 HD frames per second, 7-12 in the crowd second; `lists` stays under 150 pages, mirror 1.3 ms. The translator
limits 126 of 139 s. Tried on the bench: remembering the last `t3Flat` color (+1.9 %: colors change vertex by vertex).

**`nd_fd` on the Vita (0.0.17.1, race seconds against 0.0.16.8, per HD frame):** 27 % of the pixels moved to it; surface 36.6 -> 30.0 ms,
GPU 45.0 -> 38.2 ms, idle 17 -> 27 %. The HD rate does not change (18.7 per second): the translator limits 128 of 140 s, 42.8 ms per HD
frame in the race, 22.4 of them vertices. 1.36 M pixels stay on `nd_ft` because of the alpha test.

## C. Large changes (ask the user first)
1. **3D vertices in a GXM vertex shader reading the PSP RAM mapped for the GPU** (`sceGxmMapMemory` at 0x70000000 works,
   stage 1). Removes the vertex copy from the capturer (~5 ms) and the CPU transform from the translator (16-20 ms per HD
   frame): the biggest single gain of the whole chain. Needs A.2, `shacc` in the container and matrices, lights, texgen
   and fog as uniforms. Verify: Vita.
2. **Vertex ranges gathered by the PSP's GE (TRANSFER, like the mirror) or its DMAC** into a contiguous buffer of the
   shared zone, enqueued at the flip by NativeLink: no CPU on either side; with C.1 the GPU reads that buffer directly.
   ~55 runs per crowd frame. Verify: Vita.
3. **GPU fill**: a fog program without alpha test or `discard`, skip the image underneath when the frame covers it (11 % of
   the pixels), less overdraw. The hardest stage: 4.8 M pixels per frame on the SGX543 at 222 MHz. Verify: Vita
   (`bottleneck` line, `gpu` ms).
4. **Translator caches** (palettes, translated draws, identical scenes) and joining more draws. Verify: Vita.
5. **Split `capture.c`** (1900 lines) into walk, sublist memory, readers and logs: no fps, but makes B.6 and C.1 safe to
   do. Verify: bench and test_ci unchanged.

## Realistic targets
- Game at 60 fps with the plugin on, in every part of Ridge Racer: B.2 + B.4 (+ B.1/B.3) should do it.
- HD frames: 20-25 today in Ridge Racer with high clocks. 40-45 with C.1 and C.3. 60 in the crowd probably needs fewer
  pixels (not drawing the image underneath, no 2x on the heaviest layers). Measured, not promised: each item is checked
  on the Vita before the next.
