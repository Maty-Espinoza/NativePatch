# Log levels, switches and log lines

What each plugin writes in its logs, what each line means, and which files in `ux0:data/nativedraw/` change its
behavior. The version numbers in parentheses say since when each thing exists (the old ones, `devNN`/`vNN`, are
explained in `versions.md`). Pending item: change the switches from the L+R+START menu (`../../../TODO.md`);
for now they are files. After 0.0.8.24 the old Spanish switch files are no longer read: the names
are the ones here (old names: `versions.md`), and the installer's "Clean test leftovers" removes the old files.

Each game has its own logs (0.0.8.27): opening Adrenaline or another game moves `nativedraw.log` and
`nativedraw_capture.log` to `*_prev.log`, like NativeLink's. Only one game back is kept. "First time" lines and the capped
detail lines (`translator: first frame with`, `giant draw`, `vertex_out (n)`, `late frame`) are counted per Adrenaline
session, so a second game may not repeat them.

Logs help to understand what is going on, but they also cost performance. That is why there are three levels. One is
chosen on the Vita with the file `ux0:data/nativedraw/log_level` (content `minimum`, `medium` or `extreme`; **no file: medium**).
NativeDraw reads it when Adrenaline opens and passes it to NativeLink through the mailbox (`SB[0x4E]`: 0, 1 or 2), so it applies to both.
To change it: upload the file with the relay (`send/ux0/data/nativedraw/log_level`) and close the applications
(`cmd_*.txt` with `destroy`).

| Level | What for | Cost |
|---|---|---|
| **minimum** | the version that gets published | almost nothing |
| **medium** | everyday development (default) | low: counters, one line per second |
| **extreme** | analysis (phase 1 and similar; development build only) | noticeable: with everything on (run 5) the slow frames of GT during a race went up from ~17 % to 27 % |

## What each level does
| | minimum | medium | extreme |
|---|---|---|---|
| NativeLink log every 10 s (`nativelink.log`) | yes | yes | yes |
| `nativedraw_capture.log` | 2 lines every 10 s (frames, commands, Vita time, syncs) | all lines, every second | same + mirror (sampling), T4/data lifetime, list lifetime |
| Sublist memory check (1 out of 32, MISMATCHES) | no | yes | yes |
| Per-frame cost for the game (EV_FRAME: gate, wait for the GE, mirror, hooks) | no | yes ("PSP impact" line) | yes |
| Free PSP CPU (lowest-priority thread, like vpjuego) | no | yes | yes |
| Processor of the emulator threads (runClocks, every second) | no | yes | yes |
| `nativedraw_frames.log` (one line per frame, PSP + Vita) | no | no | yes |
| `nativedraw_late.log` (every sync with the Vita running late, with its times) | no | no | yes |
| List lifetime (hashes at the flip; NativeLink checks before enqueuing and at every sync: EV_VIDA) | no | no | yes |
| Read times (own 1/16, shared cache 1/8) and sublist memory times (check, apply) | no | no | yes |
| Data lifetime (vertices, textures, palettes 1 and 2 frames later) | no | no | yes |
| Mirror sampling (how many pages change) | no | no | yes |
| T4: notices from the game CPU (cache flush, DMA) to the mailbox | no (only counts) | no (only counts) | yes |

These stay separate, each with its own file: `draw` (the stage 3 presenter: with the file it always replaces the image; without it, only with the
L+R+START menu open; it also turns on the frame record and its `record:` line in `nativedraw_capture.log`; see `../../agent/en/stage3-plan.md`), T6 (`ge_timing`), `memo_check` (check all), `no_memo`,
`capture_no_readers`, `off`, `gpu_timer` (GPU timer) and `replay` (`dumps.md`).

## Development and production builds
The development measurements are inside `#ifdef NP_DEV`. A normal build has them; `PROD=1` (`tools/build.sh`,
`nativedraw/build.sh`, `nativelink/Makefile`) leaves them out. In production:
- `extreme` falls back to medium: no `nativedraw_frames.log`, no `nativedraw_late.log`, no measuring of list lifetime.
- T4 and T6 (`ge_timing`) and the GPU timer (`gpu_timer.c`, `gpu_timer`, its `gpu times:` and `gpu: slow scene:` lines)
  are not there.
- The switches `no_capture`, `no_lookahead`, `no_idle` and `gate_sync` do nothing.
- Dumps, MISMATCHES and the minimum and medium levels stay. The mailbox layout does not change (`how-it-works.md`).

## Dumps (NativeDraw 0.0.8.5)
- Development builds: a GE NOP carrying `0x4E5044` asks for a dump, like L+R+SELECT (NPTEST).
- Development builds: with no memory for a dump, L+R+SELECT makes a `peek` (`dumps.md`).
Dump levels in `ux0:data/nativedraw/dump`: `off`, `screenshot`, `frame`, `sequence` (without the file)
or `auto` (3 letters are compared; `no` also turns it off). What each one saves, how much it costs and how they are read with `tools/dump.py`: `dumps.md`.

The development measurements are listed in `nativedraw/measures.h`, one line each: file, variable, what it does. To add one, add a line there.

## Analysis phase switches (`ux0:data/nativedraw/`; turned off with `no` inside, the relay does not delete)
| File | What it does |
|---|---|
| `no_gate` | the game does not wait for the Vita at the sync (NativeLink only counts how many times it was late) |
| `no_mirror` | NativeLink does not copy the VRAM to the mirror |
| `no_idle` | no free-CPU thread on the PSP |
| `no_capture` | the Vita does not walk anything: it marks the lists as captured (almost NativeLink alone) |
| `gate_sync` | (v13.10) the wait for the Vita stays at the sync, as before dev66 (for comparison); see `etapa2_fase2_compuerta.md` |
| `no_lookahead` | (v13.12) the readers do not do the look-ahead check of the plan (only on reaching each CALL, as before); see `etapa2_fase2_adelanto.md` |
| `mirror_wait` | (NativeLink 0.0.3) the PSP flip waits for the mirror copy to finish (mailbox switch 32): in KOF 96 the frame's sprites are copied by the time the Vita asks for them. On the user's Vita it is set to `yes` |
| `thread_split` | (development) every second in the capture log: on which core the capturer, the record thread and Adrenaline's drawing thread ran, how long, and how often they were preempted, interrupted and moved |
| `vert_split` | (development) splits the vertex work of 1 HD frame in 8: `vertex split plain` and `vertex split warm` lines, summed up by `tools/bottleneck.py` |
| `cpu_verts` | (development) no vertices go to the GPU's vertex program (`nd_v3`): every 3D draw takes the CPU path, to compare. Read when Adrenaline opens. |
| `cap_split` | (development) times every part of the capturer on every frame (it costs ~0.7 us per clock read and in a heavy game the capturer falls behind: graphic errors and the normal image; warn before using it): `capture split` lines by kind of frame, summed up by `tools/bottleneck.py` |
| `crowd_log` | (development) every second in the capture log: commands by type, bounding box jumps, what a frame needs from the one before |
| `no_join` | (development) draws of the GPU path are not joined, to compare |
| `keep_hd` | (development) images that only blur stay at double size, to compare |
| `fx_log` | (development) `bottleneck effects:` pixels and draws of the 3D on the surface by effect, 1 frame in 32 |
| `hold_game` | (development) the game waits for the whole chain (gate up to 200 ms, the flip until the frame is walked, the capturer for the presenter and the GPU): every frame complete and in HD, and the game slower. Read when Adrenaline opens. For measuring |
| `rec_cost` | (development) keeps recording a frame already incomplete, without publishing it, to measure what recording costs |
| `hd_mark` | a green circle in the top right corner on frames drawn in HD (the translator draws it last on the surface). Read when Adrenaline opens. Also in production |
| `walk_log` | (development) the walk diagnostics of the capture log (`base:`, `bad target:`, `vram copy:`, `register`, `walk suspect`) log up to 4000 lines each instead of a few dozen, plus `vram sublist:` (a VRAM sublist outside every noted transfer) and `vram copy applied:` (a transfer with pieces not applied to the mirror). Floods the log: lines get dropped |
| `walk_trace` | (development) flight recorder in `ux0:pspemu/nativedraw_trace.log`: every transfer to VRAM with its registers, every CALL/JUMP/RET of a VRAM sublist (latest transfer covering it, mirror equal to its RAM source or not), lists and flips, in two 8 MB halves of the spare block. The first suspicious event (`TRIGGER`: registers differ from the real GE, BASE changed in a VRAM sublist, a transfer position changed, a transfer out of the real VRAM, a bad target, the list limit) stops it 150 frames (or 10 s) later and both halves are written, oldest first. `CONTEXT` lines: the stack and 16 words of the mirror and of the RAM source around the event. Slows the capturer |
| `photo_probe` | (development) at each sync the capturer handles, copies by DMA the RAM pages the photo at the sync would copy (the `photo footprint` prediction) into a buffer that is thrown away: compare the game's fps and `sync_GE_us` with and without it to see whether the copy slows the PSP's GE. It runs when the capturer gets to the sync, not at the game's own sync. `probe KB/us` in the `photo footprint` line |
| `gate_sync` | (development) the gate always waits at the game's sync instead of before the next enqueue. With `hold_game` (wait up to 200 ms) this is the PSP-Vita frame sync that keeps God of War's VRAM-copied sublists right (see `how-it-works.md`, gate) |
| `capture_core` | (v13.13) `0`, `1` or `2` pins the capturer to that core (before v13.13 always core 1); no file or anything else: any of the 3. The `threads: capture` line of the log says which one was used; see `etapa2_ridge_racer.md` |

- Development builds only, every second in `nativedraw_capture.log`: `vram copies (per frame): calls, KB, us`, the RAM to VRAM transfers the capturer applies to its VRAM mirror (`xferMirror`) and their time; with `cap_split`, also the `xfer` column of `capture split` (subtracted from the interpreter by `tools/bottleneck.py`).
- Development builds only, every second in `nativedraw_capture.log`: `photo footprint (per frame): frames, pages, copied, pieces, missed | frames with a miss, with no history | probe KB, us`: the 4 KB pages of PSP RAM the walk and the record read for each frame (lists, vertices, indices, palettes, RAM textures, transfer sources; textures copied by the record thread may count in the next frame), what the photo at the sync would copy (pages of the last 4 frames with the same first list, 8 more each side, pieces joined when 4 or fewer apart, as in `tools/perf/sync_lab.c`), its pieces and the pages it would have missed; frames with no history would wait for a live walk.
- Development builds only, every second in `nativedraw_capture.log`, after `photo footprint`: `photo footprint by the last frames (per frame): 8 more copied, pieces, missed, frames with a miss | 4 more ...`: the same prediction from the last 4 frames whatever their first list, with 8 and with 4 pages of margin (Ridge Racer moves its list start every frame, so a history by list rarely has the frame's list).
- Development builds only, in `nativedraw_capture.log`: `photo misses (by the last frames, 8 more; pages in the second): lists, vertices, textures, transfers | never read before, up to 8 frames before, up to 64, longer ago` every second with misses: the pages the prediction by the last frames missed, by what read them (`vertices` is everything the record copies with `rgCopy`: vertices, indices, palettes) and by when they were read before; and the first 40 one by one, `photo miss: frame N page A read by ..., last read N frames before`.
- Development builds only, once: `memory bench (DMA into cached):` DMA from PSP RAM into cached memory, reading it back with the CPU, and the cost of one 4 KB DMA call (in ns).

## Drawing on the Vita (stage 3, with `draw`; `nativedraw.log`, every 10 s)
- Development builds only: `translator: result at frame N: <reason>` at each change of a frame's result (`ok` or the
  reason for the normal image), the first 400. With NPTEST it gives the reason of each scene; scene `sync` lines it up.
- `presenter: N frames in T ms through the surface (changes, with own copy), CPU us/frame (max) | nearly black: game,
  surface | checked N, different from the normal image M`: frames that went through the NativeDraw surface, their
  cost on the Adrenaline thread, and the check of different frames (1 out of 4, only if the GPU has already finished the surface).
- `translator: HD frames N, normal image M | reasons: ... | draws drawn N`: how many frames the Vita drew and
  why the others went with the normal image (`no_new_frame`, `3d`, `primitive`, `format`, `blend`...; `held_back`: drawable,
  but not drawn because less than a second passed since a frame that was not).
- Development builds only: `clocks: GPU N MHz (crossbar N), CPU N, bus N`, the Vita clocks, every 10 s. The `clocks` switch (development) sets CPU 444 and crossbar 166 at each of those lines.
- Development builds only: `translator: speed:` (clock and memory, once), `translator: per HD frame:` (vertices, draws, same state as the one before, lights, programs).
- Development builds only: `translator: per HD frame: 3D vertices not moved by the GPU, why:` (first reason that applies) and `translator: per HD frame: scissor ...` (each partial scissor, its draws and vertices). `3D vertices (per frame)` in the capture log ends with what the record could not give the GPU (`record busy`, `no room`).
- Development builds only, on address problems in the capturer: `bad target:` a CALL or JUMP outside RAM and VRAM, how the address was formed and the commands before it; `base:` who changed the capture's BASE; `register 10:` BASE repaired from the real GE register; `vram copy:` each new RAM to VRAM transfer and, when a list jumps into one, whether the mirror equals the source; `capture stopped` (watchdog) where the capturer was when its heartbeat stopped for 2 s, and the state of its threads.
- Development builds only, every second: `bottleneck:` what happened to each Adrenaline frame (`drawn`, `repeated`, `behind`, `no_record`, `normal`), ms in the translator and waiting for the GPU, the translator's parts and the GPU per scene type (with `gpu_timer`). `bottleneck pixels`: thousands of pixels sent per program, before the z test, on 1 HD frame in 4 (`mid`: the programs `nd_fd` and `nd_fa`; `base`: the image underneath). `bottleneck why generic`: of those pixels, why they are not on the fast program. `python3 tools/bottleneck.py <run> [from_s to_s]` says what limits each second.
- Development builds only, every second when the capture heartbeat (SB[4]) or the PSP's events (SB[2]) stopped for 1 s, or always with `walk_log`: `capture health:` the state of the capturer, record, log and reader threads (status/wait/ms), where the capturer is and the PSP's progress. Written by the presenter thread, so it says which side stopped first (in the God of War closes the PSP stopped first, the capturer was idle). With `hold_game`, the capture log also has `hold: flip walk CUT` when the walk at the flip passes 150 ms.
- `translator: pace (PSP frames advanced per drawn frame): 0 (repeated) N, 1 N, 2 N...`: with many repeated frames
  or jumps, Neo Geo games flicker (they do transparencies by drawing something one frame yes and the next no).
- `gpu: at each frame start, our unfinished scenes: average, max | unfinished: previous frame N, 2 frames ago N |
  rewritten with the GPU unfinished: vertices and palettes N, normal image N`: how far behind the Vita's GPU
  is and how many times something was rewritten that it might still be reading (the textures of the frame record, in the
  `record:` line of `nativedraw_capture.log`: "with the GPU unfinished (measured)"). It must be 0: otherwise, broken frames.
- `gpu: frames not sent because the GPU was behind N, waits for the GPU N ms | CPU in 10 s: translator N ms,
  different-frame check N ms | surface checks skipped (GPU unfinished) N` (NativeDraw 0.0.7): frames that were not
  sent to the GPU because it had not finished the one from 2 frames ago (the surface is repeated), time spent waiting for it before rewriting, and
  CPU time of the translator and of the check.
- `gpu times: <type>: scenes, GPU ms (average, max) | draws per scene (swizzled), thousands of pixels per scene |
  in queue | ms histogram` and `gpu times: busy N ms of M (%)` (timer, `gpu_timer` switch, since
  0.0.8.13): how long the GPU takes for each type of scene (Adrenaline, normal image, translator on the surface, intermediate
  image). `gpu times: detail`: 16 consecutive scenes (A Adrenaline, N normal image, T translator, I intermediate:
  draws, GPU ms / queue ms).
- `gpu: slow scene: <type>, draws, thousand pixels, ms` (0.0.8.20): each of our scenes over 30 ms.
  `translator: giant draw`: the first 40 draws with an area of more than 2 screens.
- Reason `vertex_out` (0.0.8.21): a vertex outside ±4096 or NaN (data read too late): the frame goes with the normal image.
  In dev builds the first 40 also log `translator: vertex_out (n)` (frame, draw, vertex type and index) and the bytes of the vertex
  and the draw; each giant draw logs its vertex type and bytes. `late frame <n>` (capturer log, first 400): gate at its limit or a
  capture over 33 ms. `record: texture limit` (first 20): how many textures and bytes the frame had when it hit the cap.
- `screen:` (0.0.8.13, every 1 s and only if something changes): scenes and frames to the Adrenaline and ScePspemu screen,
  system dialog, surface state. `notifications:`: someone else wrote a GXM notification word.
- In `nativedraw_capture.log`, the `textures (per frame):` line (0.0.8.19): copies, KB and DMA and sorting time for
  RAM and VRAM; DMA calls; copies by size; swizzled KB by row width; KB equal, changed or new
  compared with the last copy of that group of 8 rows (hash per group); KB without swizzle; distinct textures per frame.
- In `nativedraw_capture.log`, the `texture cache (per frame):` line: KB reused and copied into the cache, groups found
  changed, RAM textures that went to the record, times with no room, and µs spent checking. `texture cache: no memory`
  at start-up: it runs without the cache.
- How we know when the GPU finishes: each of our own scenes writes its number into a word of the GXM notifications
  (the last one Adrenaline does not use, counting down from 511; the log notes it at start-up: `gpu: end of scenes at notification N`). Reading a surface the GPU has not finished gives false results, and `sceGxmFinish` to wait for it
  costs ~20 ms in KOF: that is why the check is skipped instead of waiting.

## Sublist memory (v13.11)
Each stored sublist adds up only its nonzero statistics (up to 48; with more it is not stored and the log says so). At the
minimum level, when applying a sublist only commands and draws are added up: the rest of its line does not use it. Details in
`etapa2_fase2_estadisticas.md`.
v13.12: the memory line adds `| lookahead/f: plan= checked= not_equal= late=` (the readers' look-ahead check;
`late` = times a reader ran into the capturer). See `etapa2_fase2_adelanto.md`.

## Gate (dev66 + v13.10, all levels)
- `nativelink.log`, every 10 s: `stats gate: <mode> | waits before enqueue=N (us each, max) |
  self_checks=N changed=N`, and a `gate: ...` line every time it changes mode (60 clean checks
  → before enqueuing; the list changed → at the sync).
- `nativedraw_capture.log`, "PSP impact" line (medium and extreme): at the end `gate: <mode>, waits before enqueue/sec=N
  (us each)`. The total wait per frame is still in `gate_us` (at the sync or before enqueuing).
- Extreme: `nativedraw_late.log` also notes the waits before enqueuing, with type `e`.

## Frame profile (development builds, extreme level; `nativedraw/prof.h`)
Follows one frame through every piece. Once per second `nativedraw_capture.log` gets `prof <group>:` lines, with
`name=count/average_us/max_us`. `python3 tools/prof_summary.py <run> [from_s to_s]` ranks them; a stretch of the run can be
chosen. For the whole picture also turn on `gpu_timer` (GPU) and `ge_timing` (GE). All of this lowers the fps: use it to compare
pieces, not as real speed. Compare with `draw` set to `no` to separate capturing from drawing.

| Group | What it shows |
|---|---|
| `psp` | The game's frame: `game` (its own work) + `vblank` (free time) + `gate`, `sync_ge`, `mirror_cpu`, `hooks`; `ge_busy` (needs `ge_timing`). `ge_mirror` reads 0 and is not reliable |
| `capture` | Capturer per frame; `draw`, `record` and `rec_*` are estimated from 1 draw in 8, `read_*` from sampled reads. `list_work` and `list_at_stall`: work and wait for the game, per list |
| `record` | Record thread: `wait_mirror`, `copy_vram`; `mirror_ge`: from the PSP flip until the GE finished its mirror copy (seen between lists by the capturer: an upper bound) |
| `latency` | `flip_to_closed`, `flip_to_published`, `published_to_taken`, `enqueued_to_captured` |
| `adrenaline` | Its drawing thread: `period` (16.7 ms at 60 fps), `presenter` (what we add), `image_dma`, `wait_gpu`, `checks` |
| `translator` | `check`, `textures`, `vertices`, `gxm`, `scenes`, `end_scene`, `all`. A frame refused by the check is not in `all` |
| `frames` | `complete`, `not_taken`, `no_record`, `gpu_behind`, `drawn`, `repeated`, `normal` |
| `gpu` | Time per scene type (needs `gpu_timer`) |
| `cpu` | Processor received per second by each thread; `free0-2`: what each core has left (lowest-priority threads) |
| `limits` | Times a fixed limit was reached: `stuck`, `queue_full`, `list_skipped`, `list_no_end`, `record_*` |

`drawing stuck:` lines (development builds, `nativedraw.log`, first 30): Adrenaline finished no scene for a second. Stage (0 outside `hookEnd`, 1 its `sceGxmEndScene`, 2 checks, 3 the record, 4 translating that draw, 5 `sceGxmBeginScene`, 6 `sceGxmEndScene`, 7 normal image), scenes sent and finished, what the system says about the thread, and the records (`regState`).

`stuck:` lines (first 60): a list with no progress for 50 ms and newer lists waiting. NativeLink's `stats frame` line (every 10 s) gives vblank,
all the time in hooks, lists, GE busy.

## Extreme level files
- `nativedraw_frames.log` (CSV, one line per game frame): `frame_us`, `gate_us` (wait for the Vita), `waits`,
  `no_wait`, `sync_GE_us` (wait for the GE inside the sync), `cpu_idle_x1000`, `mirror_us` / `mirror_pages`,
  `hooks_us`, and from the Vita: `commands`, `draws`, `capture_us` (clock), `capture_cpu_us` (processor received),
  `applied`, `checked_readers`, `own_reads`, `from_readers`, `pending_lists`, `late_syncs`,
  `list_addr`, `list_kb`, `memory_us` (all memoTry), `apply_us`.
- `nativedraw_late.log` (CSV, every sync in which the Vita finished the list after the game reached the sync): `wait_us`,
  `delay_us`, `commands`, `lists_before`, `enqueued_ago_us` and `last_stall_ago_us` (relative to the sync),
  `vita_started_after_us` (since the enqueue), `vita_walked_us`, `work_us`, `own_reads`, `from_readers`,
  `applied`, `processor_us`. The Vita's times are converted to PSP times using the minimum clock difference.
- Summary: `python3 tools/phase1_analysis.py <folder> [<folder> ...] [--table]` (one column per run).
