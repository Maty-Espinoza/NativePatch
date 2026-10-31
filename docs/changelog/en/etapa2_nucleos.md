# Stage 2: recording in GXM from several cores (gepresent v13.7, 2026-10-29)

Follows `etapa2_espejo_doble.md`. Stage 2 item (`arquitectura_dibujo_en_vita.md` §6): "repeat the recording measurement
with 2–3 threads with fixed affinity" (in stage 1, 2 threads without a fixed core did not scale). Tags: [measured],
[deduced]. **Commit:** gepresent **v13.7** (= v13.7.1 with the final number); GePatch stays at **0.2-dev64**.

## In simple terms
- To draw in HD, the Vita will have to "record" each draw of the game in the format of its graphics card (GXM).
  GT has ~9000 draws per frame (up to 16 500). The test recorded 9000 practice draws while GT was running, with 1, 2
  or 3 threads on different processor cores.
- **Recording can indeed be spread** across cores (there is no internal lock), **but there is no free processor left**:
  in a race, core 1 is almost entirely used by reading the frame (capture) and half of core 2 by the PSP's sound. The
  best result was ~26 ms for 9000 draws (60 fps is 16.7 ms per frame).
- The graphics card also takes time: ~16 ms for 9000 small draws.
- **Conclusion:** GT cannot run at 60 fps recording each draw separately. We have to record less: batch draws and/or
  record once the pieces of the list that repeat (in GT 85% of the instructions come from sublists that do not change)
  and reuse those recordings. SH, RR and the lightweight games (~300–1400 draws) do have headroom.

## How it works (tools/gepresent/banco.c, "banco de nucleos")
- It is enabled with `ux0:data/gepresent/banco_nucleos` (contents: seconds to wait, 30 if empty; **"no" turns it off**,
  because the relay cannot delete files). With the file enabled gepresent does not reserve the dump ring.
- One recording = 9000 draws of case T of the stage 1 test bench (texture + uniforms per draw, the closest to GT),
  split among 1, 2 or 3 recorder threads (priority 0xA0: below the capturer 0x61 and above the readers 0xB0), each with
  its deferred context. The lists are run in the scene of the Adrenaline thread (it checks that they work).
- 10 configurations: 1 thread without a fixed core / on 0 / on 1 / on 2; 2 threads without a fixed core / 0+2 / 1+2 /
  0+1; 3 threads without a fixed core / 0+1+2. A round = each one 3 times (one frame each); 20 rounds every 15 s.
- The deferred context is created anew before each recording over its empty pool (the GPU has already finished with the
  previous one): this way the pool is reused without the v11.2 problem. Pools of 5 + 3 + 3 MB (9000 T draws = 3.2 MB).
- Log `ux0:pspemu/ge_patch_banco.log`: per recording, total wall time, us/draw, per thread wall time/CPU time
  (`runClocks`), wake-up delay, run and GPU. `tools/resumen_corrida.py` summarizes it per configuration.
- Visible effect: during each round (~1 s) Adrenaline waits for the GPU and the image stutters; **the game does not
  slow down** (in the seconds of each round the capture sees 60 frames, same as before).

## Results [measured] (GT, shadow mode, GePatch dev64; log `vitabridge/etapa2_v13.7.1/gt1`)
Rounds 1–3 in menus, 4–20 in a race (17 × 3 = 51 recordings per configuration). No errors or GPU crashes.

| Configuration (race) | Wall time (median) | µs/draw | Total CPU time | Busiest thread |
|---|---|---|---|---|
| 1 thread without a fixed core | 31.8 ms | 3.54 | 23.8 ms | 23.8 ms |
| 1 thread on core 0 | **27.6 ms** | 3.07 | 23.9 ms | 23.9 ms |
| 1 thread on core 1 | 169 ms | 18.8 | 25.9 ms | 25.9 ms |
| 1 thread on core 2 | 58.7 ms | 6.52 | 23.9 ms | 23.9 ms |
| 2 threads without a fixed core | **25.9 ms** | 2.88 | 28.0 ms | 14.4 ms |
| 2 threads cores 0+2 | 38.9 ms | 4.32 | 24.3 ms | 13.1 ms |
| 2 threads cores 1+2 | 86 ms | 9.6 | 24.9 ms | 13.7 ms |
| 2 threads cores 0+1 | 89 ms | 9.9 | 25.5 ms | 13.6 ms |
| 3 threads without a fixed core | **25.8 ms** | 2.86 | 27.6 ms | 10.3 ms |
| 3 threads cores 0+1+2 | 66 ms | 7.3 | 26.9 ms | 9.8 ms |

- **Cost of recording:** ~2.65 µs of CPU time per T draw (same as stage 1: 3.3 µs of wall time on a free thread). Spread
  out it does not get much more expensive (2.7–3.1 µs/draw adding up the threads).
- **There is no lock between threads:** with 0+2, the thread on core 0 finishes its half in ~13 ms (≈ its CPU time)
  while the one on core 2 keeps going; if GXM forced recording one at a time, the first one would wait too.
- **Free CPU in a race** (for priority 0xA0) [measured with the `procesador` line of the capture + table]:
  core 0 ~85–95% (emulator ~40 ms/s; reader 0 has lower priority), core 1 ~10–15% (capture ~650–770 ms/s
  + audio and emulator ~200 ms/s), core 2 ~40–50% (Media Engine ~500–550 ms/s). In total ~1.5 cores.
- In menus core 0 is busier (emulator) and core 1 more free (the capture barely works).
- **GPU:** 15.5–16 ms for the 9000 draws (≈1.75 µs per 16x16 draw with a texture and new uniforms), the same in all the
  configurations. GT's heavy stretch (16 500 draws) would be ~29 ms of GPU alone.
- Delay in waking a thread: ~50–100 µs (sometimes 2–6 ms on core 0 or another busy one).

### The same run, on the capture side (unrelated to the test bench)
- GT fps: 59.5–60 for almost the whole race; they drop to 55–58 at the start of the race, **53–57 in the heavy stretch**
  (~80 000 commands and 16 500 draws per frame, capture ARM ~16–17 ms per frame) and **40–48 in the last ~7 s** (ARM
  ~20–25 ms per frame with only ~12 000 draws: ~300 RECTANGLES appear and the sublist memory skips 70% of the commands
  instead of 85%, with ~50 sublists per frame coming in with another state, `otra_entrada`). The same, without the test
  bench, in the dev64 confirmation run (`etapa2_espejo_doble.md`).
- DISTINTAS: 13 of 134 312 checked (≈0.01%).

## Reading
1. **Recording draw by draw does not fit for GT**, not even spread out: it needs ~24 ms of CPU time per 9000 draws
   (≈44 ms in the heavy stretch) and ~1.5 cores are left free, which the capture also shares. This confirms with the
   real game the warning from stage 1 (§5 of the architecture).
2. **The GPU does not fit either** with one draw per GE draw (≈1.75 µs each).
3. Spreading helps, but only among cores with free CPU: the translator should go on core 0 (and whatever is left of
   core 2), not on core 1 together with the capture. 2 threads without a fixed core perform the same as 3: the
   scheduler spreads them well.
4. **What could lower the cost** [deduced, untested]:
   - *Record each stable sublist once and reuse the recorded list* (GXM runs the same recorded list many times;
     running 2000 recorded draws costs ~30 µs on the Adrenaline thread, stage 1). In GT ~85% of the commands come from
     sublists that the capture already recognizes as equal to those of the previous frame. Problem to solve: the input
     state (matrices, textures) that the sublist inherits from its caller.
   - *Batch draws* with the same state (stage 1: 2000 quads in 1 draw = 0.02 µs per quad and the GPU at half).
   - Lower the cost of the capture itself in the heavy stretches (`TODO.md`, capture performance).
   This brings forward part of stage 6 (cache of translated sublists, batching draws): for GT it has to exist from the
   start. SH/RR can follow the planned order.

## Versions
| Version | What | gepresent md5 |
|---|---|---|
| v13.7.1 | cores test bench (built in Claude's container) | `1ef945bc…` |
| v13.7 (commit) | = v13.7.1 with the final number; stays on the Vita with `banco_nucleos` = "no" | `4a73f210…` |
GePatch on the Vita: dev64 (`32742693…`, from the Mac). From this stage on it is also built in Claude's container
(`tools/contenedor/`, see `FLUJO_DE_TRABAJO.md`).
