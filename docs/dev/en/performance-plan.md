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
3. **Vertex and RAM-texture copies on another core: tried for vertices, discarded (0.0.19.1-0.0.19.2).** Copying after the list is released deforms the geometry: the game rewrites the vertices. Copying during the walk and waiting at the end of the list gave no gain (complete frames 30.3 -> 29, the capturer waits 0.7-2 ms, runs too small for DMA) and left some deformed triangles, cause not found. The list cannot be released before its vertices are copied, so this wait does not go away. Original idea (the `record` thread already exists): the copy of frame N runs while
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

**Translator (bench, 0.0.17.2):** real frames are uniform: ~15,000 vertices in ~2,700 draws, 5.5 per draw, 77-99 % in format `0x19f`, 87-99 %
lit with no light on, 87 % with fog. A NEON loop for that vertex and a shorter path for draws that share the state: **ARM instructions
per frame 8.14 M -> 5.78 M (-29 %)**, same draws and vertices in `tr_check.sh`. On the Vita (race seconds against 0.0.17.1): vertices only 22.4 -> 19.8 ms
(-12 %), translator work 41.5 -> 38.1 ms, 18.7 -> 19.6 HD frames per second. **Fewer instructions gave much less time: the vertices bucket is not mostly
instructions.** Suspected, not measured: reads of the record and writes to `tv`. Log of the Vita: cached read 24 ns per word, uncached 199.

**What the vertex time is made of (`vert_split`, 0.0.17.3, ~1140 draws and 6900 vertices, clock cost removed):** `trVerts` ~9.7 ms, `trTris` ~5.3, the rest
of the part ~4.4 (development counters included), state ~1.3, copy to `tv` ~0.6. Repeating `trVerts` at once, with data and code in the cache, costs 1.26 us per
vertex against 1.39 the first time, and reading the data first changes nothing: **it is not memory, a prefetch would not help.** 1.26 us is ~420 cycles, which the
instructions do not explain; the cause is open. A fit on 10 s windows gives ~27 us per draw rewritten by `trTris`.
**`nd_fa` (0.0.18):** GPU 38.1 -> 32.5 ms per HD frame, idle 24 -> 36 %; the HD rate does not move: the translator limits alone.
**Threads (`thread_split`, 0.0.17.6, race):** Adrenaline's drawing thread (priority 0xA0, not pinned) runs 749 ms per second on cores 1 and 2, is preempted 8 times per second, interrupted
~2500 and moved between cores ~1400 times. The capturer moves ~650 times per second. In a heavy frame it ran 42 of 54 ms: since it is hardly preempted, those 12 ms are not the capturer taking its core
(`capture_core` will not recover them); they are probably waits inside GXM calls, not measured. Moves were not tested.

**Scissor and join checks before C.1 (0.0.18.1-0.0.18.5, race, `vert_split` on):** real race: 20-50 % of the 3D draws have a scissor under the surface (the two bench frames understate it). `trTris` spends ~4.8 ms on ~150 draws.
| Run | HD/s | GPU surface ms per HD frame | GPU wait ms/s | Translator ms per HD frame | `tris` ms |
|---|---|---|---|---|---|
| Base | 19.4 | 24.6 | 15 | 39.8 | 4.8 |
| `clip_gpu`, clip planes `CLP0` (with or without region clip) | 14.6 | 42.2 | 99 | 45.2 | 1.4 |
| `prog_swap`, vertex program change alone | 20.6 | 24.5 | 17 | 37.9 | 4.8 |
| `clip_discard`, `discard` in `nd_ftc`/`nd_fcc` + region clip | 20.5 | 26.4 | 37 | 37.1 | 1.7 |
Clip planes are exact but saturate the GPU, not because of the area (region clip does not help) or the program change; the cause is unknown. `discard` works with the generic program. Visual: one moment of artifacts in the sky in the crowd with `clip_discard`, cause open; the failure `color_test_or_logic` of that zone also appeared with `prog_swap`. `no_join`: +1 us of CPU per draw (GXM 5.43 -> 6.45 us), ~0.8 ms per HD frame, GPU unchanged: C.1 can draw without joining.
**Lit vertices (`light_split`, 0.0.18.4):** car selection screen: ~5200 vertices, ~25 ms: types `0x142`/`0x143` with one directional light and specular ~5.5 us per vertex, generated UV ~3.3 us, against ~1.3 us of the fast path; the light is ~2 us of it. In the race, rival cars add only ~1-3.5 ms; the peaks are more `0x19f` vertices.

**C.1, first stage (0.0.18.6-0.0.18.8, confirmed as 0.0.19):** `nd_v3` reads the record's copy at the end of `tx`. Eligible: type `0x19F`, no indices, triangles, strip or fan, smooth, no lights, no generated UV, scissor that takes in the whole image.
| Race, per HD frame | 0.0.18.5 | 0.0.19 |
|---|---|---|
| HD frames per second | 20.5 | 21.7 |
| vertices, ms | ~19.5 | 13.1 |
| translator, ms | ~37 | 32.1 |
| GPU, ms | ~35 | 33.2 |
| vertices moved by the GPU | 0 % | 54 % |
Left on the CPU per HD frame: rear-view mirror ~1500 vertices (scissor 150,10 to 330,48) and other vertex types ~500. Marked vertices refused by the translator read uncached memory (177-207 ns per word against ~15), so a draw must not be marked and then refused. The capturer's copy to `tx` shows no clear cost in the race.

**Lit vertices on the GPU (0.0.19.3-0.0.19.4, confirmed as 0.0.20):** car selection was limited by the CPU, not the GPU (translator ~33 ms, vertices ~21 ms, GPU ~20 ms and idle 54 %). `nd_v4` takes types `0x142`/`0x143`: one directional light with specular, or UV generated from the normal.
| Car selection, per HD frame | 0.0.19 | 0.0.20 |
|---|---|---|
| translator, ms | ~33 | 16.5 |
| vertices, ms | ~21 | 2.3 |
| GPU, ms | ~20 | ~21 |
| Adrenaline's frames per second | ~27 | ~44 |
| HD frames per second | ~21 | ~27 |
Coplanar passes of a model must take the same path and program: with only the lit pass on the GPU, the GEQUAL depth test of the reflection pass failed by rounding and the reflections flickered in saw teeth.
**What limits car selection now:** `regFlip` skips recording a frame (`R_SKIP`) when the one before was not taken yet. The GPU takes ~21 ms, longer than a game frame, so every second frame is skipped: ~30 recorded and ~30 only walked per second, a cap of ~30 HD frames although the chain could give ~45. The capturer has time to spare there.

## Crowd measured clean (`hold_game`, 0.0.21.3, `stage3_0.0.21.3/rr1`, 2026-11-04)
With `hold_game` the game waits for the whole chain: every frame complete and in HD, no list read late. CPU 333, GPU 222, extreme log; ~3600 draws, ~19,000 commands, ~19,000 vertices, 446 KB of vertices per frame. The game itself takes ~7 ms; here it runs at 15 fps (66 ms).

| Stage, ms per frame | Crowd | Hard stretches (1000-2500 draws) | For 30 fps | For 60 fps |
|---|---|---|---|---|
| Capturer (processor) | ~32 | 17-21 | 33 | 16.7 |
| Drawing thread (translator ~45 of it) | ~54 | ~40 | 33 | 16.7 |
| GPU | ~34 | ~35 | 33 | 16.7 |

- **The slowest stage is the drawing thread, not the capturer.** The capturer limits today only because it falls behind the game and that wrecks frames; once it fits, the HD rate is set by the drawing thread and the GPU.
- Translator, ms: GXM 15, vertices 8, textures 4.6, check 3.8, scenes 1.4, ~12 not split. Capturer, ms: record 12.4 (vertices 7.3, state 2.6, textures 2.5), reads 3, the rest the interpreter. Record thread: 13 more for VRAM textures, on its own.
- GPU, ms: 2 surface scenes ~25; 15 intermediate scenes of one draw, 0.6 each, ~9.
- 92 % of the draws do not change state; the vertices come in ~220 contiguous runs. 11,000 of the 19,000 commands are BASE, VADDR and PRIM.
- Discarded: bounding box jumps. 0 in the crowd with clean reads; the 5400 per second seen before were lists read late. The real crowd is ~16,000-19,000 commands, not 25,000.

**Next, by gain:** join draws with the same state into one `sceGxmDraw` (GXM, GPU); group the intermediate scenes (GPU); a tight path for BASE, VADDR and PRIM in the interpreter (capturer); then vertices of the rear-view mirror.

## Draws joined (0.0.21.5, `stage3_0.0.21.5/rr1`, `hold_game`, medium log)
Joining works: 3405 -> 267 GXM draws in the crowd frame, same triangles. Vertices for the GPU must keep 16-byte alignment: packed one after another (0.0.21.4) the GPU crashed on entering car selection, with draws joined or not; in multiples of 48 it does not. The exact reason inside the GPU is unknown.
- The frame rate moves in steps, 60 / n: 30, 20, 15. Translator under 33 ms gives 30 fps; 33-50 ms gives 20.
- The "GXM" time did not fall with the number of draws (crowd ~13 ms): that bucket is mostly work per draw before the GXM call. On the PC, the crowd frame takes ~6 M instructions, ~1600 per draw, with no single hot spot: `trFrame` 36 %, `trClipTri` 11 % (rear-view mirror), software division 7 % (`__udivsi3`, in the join itself), `trCheckFrame` 6 %.
- Where the GPU limits: stretches of ~550 draws at ~30 fps with `gpu_wait` 270-450 ms per second. Not measured with `gpu_timer` off.
- Bridge straight (draws ~1850, before the green tunnel): 20 fps, translator ~40 ms, capturer ~21 ms. Crowd: 15-20 fps, translator 45-54 ms, capturer ~30 ms. Car selection: 41 fps, translator 15 ms.

**Next:** a short path in the translator for a draw with the state of the one before; no division in the join; rear-view mirror on the GPU; the control run with `no_join` to know what joining gave; `gpu_timer` where the GPU limits.

**Short path (0.0.21.6, `stage3_0.0.21.6/rr1`, against 0.0.21.5, same switches):** crowd 18.8 -> 25.5 fps, translator 45 -> 30 ms; hard stretches 21.9 -> 24.5 fps, 35 -> 32 ms. Normal race (without car selection, which skews the zone) 27.2 -> 28.6 fps. No normal image during the race. Bridge straight stays at ~20 fps, translator ~36 ms: vertices are 14 ms of it, and 2973 of its ~4300 CPU vertices are the rear-view mirror, kept off the GPU path only by its scissor. Fair reference for the next changes: `tests/perf/rr_hold_0.0.21.6.json`.

**Rear-view mirror on the GPU (0.0.22.1, confirmed as 0.0.23, `stage3_0.0.22.1/rr1`, against 0.0.21.6, same switches):** the mirror is one stretch: a clear of color and depth that covers its scissor, then its 3D draws, with nothing read from the surface. So it goes to an image of its own (`rtb`), moved to the corner of the box, in the intermediate pass; the edge of the image clips it exactly, with no `CLP0`, `discard` or `sceGxmSetRegionClip`, and the surface scene is not cut. A paste on the surface puts it back. `scissor` in `3D vertices not moved by the GPU`: ~3000 -> 0. Hard stretches 24.5 -> 28.8 fps, translator 31.7 -> 25.8 ms; crowd 25.5 -> 28.4 fps; normal race unchanged, 29.3 fps. Vertices 4.4-5 ms. On the PC, real ARM instructions: `rr_2722` -29 %, `rr_crowd` -20 %, `rr_2227` (no mirror) +3.6 %, mostly `trFrame`, not the search. Limits: one stretch per frame; a clear with position only.

**Now the GPU limits:** of the translator's 26 ms, ~9 ms are waiting for the GPU, 11-16 ms on the bridge straight (seconds at 24-26 fps) and 19-32 ms in a short stretch of 19-25 fps near the start of the race (900 draws). CPU work is ~16 ms. Reference: `tests/perf/rr_hold_0.0.22.1.json`.

**GPU, scene by scene (0.0.23.1, `tests/perf/runs/0.0.23.1_rr_gpu_scenes`, complete frames of the race):** first surface scene 16-21 ms, rear-view mirror ~1 ms, bloom chain 8-9 ms, second surface scene 5-6 ms with ~90 draws; 30-35 ms in all, against 33 ms for 30 fps. The bloom chain is 12 draws on image `088000` in 15 scenes: four draws read the surface at half size, then each pass reads the image it draws on, so each needs its own scene and a copy to `rtc`. Passes depend on the one before: they cannot share a scene. Reading the image directly instead of a copy is not safe: written and read regions overlap, and the GPU stores tiles while it reads. Reading the previous frame's surface before the first scene would remove the second surface scene, but the bloom would feed on itself. Next: no image underneath, then the bloom image at PSP size: it is only blurred, and every scene of the chain gets 4 times smaller. `no_join` (0.0.23): GXM +66-100 %, translator +14-35 %, GPU +2-8 %, fps -6 to -25 %.

**No image underneath (0.0.23.3, `keep_base` against none, `tests/perf/runs/0.0.23.3_*`):** 522 thousand fewer pixels per frame and the same GPU time (32.5 -> 32.2 ms, 34.4 -> 34.6, 35.3 -> 34.3), same battery (34 mAh in 210 s, 37 in 200 s). A likely reason, not proven: the clear covers it and the GPU drops hidden pixels before shading them. The GPU is busy 95-98 % of the race.
**Where the frame goes, per 10 s:** first surface scene 16-18 ms, up to 23-26 in the slow stretches, with 50 % more pixels; bloom the same on every frame: images 5.8 ms, copies 3.1 ms, second surface scene 5.4 ms, 14 ms in all. So the bloom raises the floor and the first scene makes the drops. Seconds with the 64 x 64 image `1fe000` (a reflection map, 0.8 ms itself) are among the slow ones: the draws that use it are not measured yet.
**Do not call undocumented power functions:** `scePowerGetBatteryElec`, not in the SDK headers, restarted the Vita a second after the first frame (0.0.23.2).

**Bloom at PSP size (0.0.23.5, `tests/perf/runs/0.0.23.5_rr_bloom_psp`, against 0.0.23.3 with the image underneath):** images 5.8 -> 3.0 ms, copies 3.1 -> 1.7 ms; GPU 32.5 -> 28.5 ms, 34.4 -> 30.0, 35.3 -> 30.6; fps 29.7 -> 32.4, 27.8 -> 29.8, 26.9 -> 29.1. No difference seen by the user. The second surface scene stays at 5.6 ms. `fx_log` raises GXM: it turns the short path off on its sampled frames.
**Cars close by (same run, few samples):** beside three cars at the start of the race the first surface scene takes 32-34 ms, against 24 ms a few seconds later alone. `fx_log` there: what the GPU lights 779 thousand pixels, blended with fog 1403 thousand instead of 581, reflections 295 thousand, all near zero alone, in 3826 thousand pixels against 3240. So cars lay several blended layers, and having them close adds about 8-10 ms. Next: a cheaper program for those layers (C.3).

## Order from here (2026-11-04)
0. **Done in 0.0.21:** a frame is skipped only when the capturer was behind at a sync and the last frame was not taken, or when the record to write is still read by the GPU: car selection ~28 -> ~35 HD frames per second, race and crowd the same. Below ~45 because records are busy with the GPU. Never write a record the GPU reads: it froze the drawing thread and crashed the GPU. Original item: expected ~45 HD frames per second in car selection; the race and the crowd must stay the same. Verify: Vita (`frames only walked`, `gate_us`, game fps).
1. **GPU (C.3), started first by the user:** the capturer in the crowd waits; B.3 failed and B.9 is the remaining idea there. It falls behind there: 7-10 complete frames per second, and lists read late give frames that fail in the translator (1 to 8 per run), each shown as the normal image for several game frames.
2. **GPU (C.3):** fewer pixels (9.4 screens per frame). In the normal race translator and GPU take about the same, ~33 ms.
3. **Done in 0.0.23:** rear-view mirror on the GPU, in an image of its own. **In 0.0.24:** bloom at PSP size.
4. **C.2 only if the capturer limits after B.3.**

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
