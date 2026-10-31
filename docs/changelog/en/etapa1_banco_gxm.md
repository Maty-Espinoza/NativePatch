# Stage 1 of architecture B: GXM test bench on the Vita (2026-10-28) — CLOSED

Second stage of `arquitectura_dibujo_en_vita.md` (§6). Question: **how much does a GXM draw cost the Vita (ARM + GPU)
inside the Adrenaline process**, depending on what changes between draws. The plan's criteria: (b) texture change ≲ 1 µs and (c)
program change ≤ 2 µs per draw, and ≥ 16 MB for surfaces → B also works for GT.

Marks: **[measured]** on the Vita; **[estimated]** calculated from measurements; **[warning]** conclusion drawn from a synthetic
test that **is taken as neither true nor false until it is tried with the real game** (the user's decision).

- No changes to GePatch (still 0.2-dev57.1 on the Vita, no new GePatch commit). Change only in **gepresent**
  (v11.1 → v11.2 → v11.3). Commit: gepresent v11.3 + `tools/carga_vita.py` (stage 0) + documents in `docs/`.
- **Final state on the Vita:** gepresent **v9** (md5 `20f3c01d4344df5dba3bc9dc2b6b3172`), no adrfsr in `config.txt`.
  The file `ux0:data/gepresent/banco` is still on the Vita (v9 ignores it; delete it with VitaShell; the relay only goes up).
- Pending item postponed by the user: the black screen of the presentation (see below).

## 1. Before starting: adrfsr (FSR) turned off
- Earlier decision by the user: do not continue with FSR 1. adrfsr was still listed in `*PSPEMUCFW`.
- `ur0:tai/config.txt`: `ux0:data/adrfsr/adrfsr.suprx` was removed (new md5 `0fe51b1eff5a0b98ce2071b0e93bce31`). Backup
  of the previous one: `vitabridge/respaldo/config_con_adrfsr.txt` and `vitabridge/gepresent/config_con_adrfsr.txt`. Also
  `ux0:data/adrfsr/off`. Vita restarted with `cmd_reinicio.txt` (`reboot`).
- `*PSPEMUCFW` is left with only `ux0:data/gepresent/gepresent.suprx`.

## 2. gepresent v11 = v9 + `banco.c`
- Source: `tools/gepresent/banco.c`, `banco_v.cg`, `banco_f1.cg`, `banco_f2.cg`, `banco_shaders.h` (already compiled
  shaders), `build.sh` (resolves the stubs by itself from the missing symbols; `sh build.sh shaders` regenerates the
  `.h`), and in `gepresent.c`: a call to `bancoStart`/`bancoStop` and, if the bench file exists, it does not reserve the
  24 MB dump ring (the dumps are written directly).
- **Off by default:** it only runs if `ux0:data/gepresent/banco` exists (contents = seconds to wait, in Adrenaline
  frames / 60, before starting). Log: `ux0:pspemu/ge_patch_banco.log` (appended to the end).
- It hooks the `AdrenalineUser` imports `sceGxmEndScene` (0xFE300E2F) and `sceGxmSetFragmentTexture`
  (0x29C34DF5). After Adrenaline's scene, in its thread and its context, it runs one case per frame: 2000 draws of
  16x16 quads (strips of 4 vertices, 2 vertex streams, 3 repetitions), on its own 960x544 8888 + D24S8 surface with
  a render target of 8 scenes per frame. Timings with `sceKernelGetProcessTimeLow` (µs).
- Phases: clocks and memory → immediate context (cases A–M) → deferred context recorded in another thread (A, B, P, C, U, T,
  S and T on 2 threads) → GPU reading vertices (own LPDDR, own CDRAM, PSP RAM) → 10 s of presentation (Adrenaline
  shows the bench surface: the game image redrawn + a moving colored square at the top).
- Binaries in `vitabridge/gepresent/`: v9 (`20f3c01d…`, **the one that stays**), v11.1 (`1bae76a4…`), v11.2 (`c1fde749…`,
  crashed the GPU), v11.3 (`ed98588b42b9ed59d07c8a944057bfe5`, the one in the commit). Sources also in
  `vitabridge/respaldo/gepresent_v11_*.tgz`.
- Logs: `vitabridge/etapa1_v11.1/`, `etapa1_v11.2/`, `etapa1_v11.3/`, `etapa1_v11.3_apagado/`.

## 3. Results (GT open with L = without GePatch, Bilinear filter; v11.1 11:49, v11.2 12:00, v11.3 12:08)

### 3.1 Clocks and memory [measured]
- ARM **444 MHz**, bus 222, **GPU 222**, xbar 111.
- Memory the Adrenaline process can request (largest block / total in 1 MB blocks):
  - with gepresent's dump ring reserved (24 MB): CDRAM 7, LPDDR 6, contiguous physical LPDDR 23;
  - **without the ring: CDRAM 4/4, LPDDR 33/33, contiguous physical LPDDR 23/23** (the physical one shares with LPDDR [deduced]).
  - With everything in the bench (2 surfaces + z ≈ 6 MB, 16 MB of deferred-context pools, 6.4 MB of vertices) LPDDR is left with
    7–8 MB and CDRAM with 1 MB. One of the surfaces and the z fell into physical LPDDR (CDRAM was not enough).

### 3.2 Cost per draw [measured; 2000 draws of 16x16; range of 3 runs on 3 versions]

| Case | Immediate: CPU µs/draw (Adrenaline thread) | GPU after submit (ms) | Deferred: record µs/draw (other thread) | GPU (ms) |
|---|---|---|---|---|
| A no changes | **1.0–1.2** | 3.2–3.9 | 1.0–1.1 | 2.5 |
| B changes 8888 texture | **1.9–2.2** | 4.4–4.6 | 1.8–2.3 | 3.3 |
| P changes P4 texture + palette | 1.9–2.3 | 4.2–4.4 | 2.0–2.4 | 3.3 |
| C changes fragment program | **2.1–2.4** | 4.3–4.5 | 2.5–3.2 | 3.3 |
| U changes 64 B of uniforms | 1.9–2.2 | 4.2 | 2.0–2.6 | 3.4 |
| T texture + uniforms | **3.0–3.5** | 4.0–4.3 | 3.3 | 4.1 |
| D = A in 8 scenes | 1.5–1.7 | 6.1–6.9 (≈ +0.4 ms per extra scene) | — | — |
| O = A off-screen (no pixels) | 1.1 | **2.5–3.1** | — | — |
| S = A with 1 stream per draw | 1.0–1.1 | 3.2–3.8 | 0.85–0.9 | 2.5 |
| M = all 2000 quads in 1 single draw | **0.02** (40 µs in total) | **1.9–2.1** | — | — |
| T recorded on 2 threads (1000 + 1000) | — | — | 5.2 / 3.3 / 2.7 wall clock (each thread 4.2–10 ms) | 4.1 |

- "GPU after submit" = from when the CPU finished submitting until the GPU finished (`sceGxmFinish`). With draws of
  4 vertices GXM sends almost everything when the scene is closed, so it is almost the total GPU time of the scene.
- EndScene: ~100–250 µs. **Running a recorded list inside Adrenaline's scene: ~30 µs** per 2000 draws.
- Memory of a deferred context: ~160–320 KB per 2000 draws (T ~640 KB).
- **Vertices:** 100 draws × 4096 vertices (6.4 MB with stride 16): **17 M vertices/s both from own LPDDR and from the
  PSP RAM** (0x08800000 registered with `sceGxmMapMemory` read-only, no error). Reading directly from the PSP RAM
  does not cost more. CDRAM was not tested (no memory was left).

### 3.3 Presentation [measured]
- It works: 600 frames, Adrenaline's 512x272 texture was swapped 599 times for the bench surface; 158–195 µs
  of CPU per frame for the own scene. The game kept running.
- **Black screen (postponed by the user):** during the 10 s the user saw black with the colored square on top
  (v11.1: "black until I pressed X"; v11.3: GT seemed to be in its cutscene). At 5 s, the game pixel (240,136)
  and (20,20) in Adrenaline's texture was `ff020202` (almost black) and the bench surface had the same: the bench's
  copy is faithful; what was arriving at that moment was black.
- **Verified later (12:32, user, gepresent v9, GT with L):** GT's cutscene **looks fine** without the bench. That is, the
  cutscene does go through the image that Adrenaline draws. It remains unexplained why the texture
  was black at those moments: it may be that GT was in a black transition right then, or something in the bench (for example, the moment at
  which the texture is read relative to the copy Adrenaline makes) [verify later: read several pixels every
  second during the presentation and compare them with those from before the bench].

### 3.4 Two bench failures (and their lessons)
1. **v11.2 crashed the GPU** ("GPU Driver detects GPU Crash", coredump) in the second deferred case. Cause: before
   each recording the deferred context's memory pool was emptied (`pos = 0`), but the context keeps using the pieces
   it already received; the pool handed that same memory back for another ring and the GPU read overwritten commands.
   v11.3: the pool is never reused. **Lesson for B:** recycle a deferred context's memory in whole blocks and
   only once the GPU has finished with all the lists that use it.
2. **v11.3 shut down the whole Vita** (12:16–12:18) when GT was opened **without** L (GePatch dev57.1 active at 2x, Adrenaline in
   `DRAW_NATIVE` mode). The bench was prepared fine (same memory figures) and the connection dropped ~60 s after opening
   Adrenaline, right when the bench starts drawing. When it was turned on, the Vita asked for date and time (which it does after
   an abrupt shutdown); the applications and data were fine and it had battery. Cause not confirmed: the combination
   active GePatch (GE at 2x, gepresent registering 0x0A000000/0x0AE00000 for the GPU) + bench with ~8 MB of free LPDDR +
   GPU at maximum. After the restart Adrenaline opened again with v11.3: the applications were closed with
   `cmd_cerrar.txt` (`destroy`) and v9 was restored. **Lesson:** an experiment with the Vita's GPU must not run with
   GePatch active (check that the 512x272 texture is visible before starting) nor stay installed between tests.

## 4. Reading

### 4.1 What is proven [measured]
- **Deferred context:** recording costs the same per draw as immediate, but in another thread; Adrenaline's thread only
  runs the list (~30 µs). It is the shape of B: the translator records in its thread and the presenter only runs.
- **The GPU reads the PSP RAM directly**, at the same speed as its own memory.
- **The presenter works**: Adrenaline's image can be replaced by an own surface every frame.
- **Merging draws is the biggest lever**: 1 draw with 2000 quads = 40 µs of CPU and ~2 ms of GPU, versus 2–7 ms of
  CPU and 3.5–4.5 ms of GPU in 2000 separate draws.
- **Memory:** ~33 MB of LPDDR + 4 MB of CDRAM in the Adrenaline process (without the dump ring). One
  HD 960x544 8888 surface + z ≈ 4 MB. gepresent's 24 MB dump ring cannot coexist with B as it is.
- A second SetVertexStream per draw barely costs anything (A ≈ S).

### 4.2 Estimates for the games [estimated with the synthetic draws]
- With ~2 µs per draw with a new texture and ~1.2–1.5 µs of GPU per draw (case O):
  - **RR** (~1100 draws, 170–270 batches per frame): ~2–3 ms of CPU, ~2 ms of GPU → comfortable.
  - **SH** (~320 draws, up to 1400): < 1–3 ms → comfortable.
- **[warning] GT** (~9000 draws per frame, ~5300 with a new texture, matrices): by this calculation it would be ~17–30 ms of CPU
  and ~11–14 ms of GPU in draws alone, that is, **it might not reach 60 fps without merging draws**. It is an extrapolation from
  synthetic 16x16 quads with a simple shader; **it is taken as neither true nor false** until measured with GT's
  real lists (stage 2 onward). What could change it: merging consecutive draws with the same state,
  recording on 2–3 threads pinned to different cores, the real cost of the GE's shaders, how many of GT's draws
  really share a texture.
- 2 threads recording at once did not scale in the bench (best case 2.7 versus 3.3 µs/draw; each half took longer than alone).
  The threads were created without affinity: they probably shared a core [verify in stage 2 by pinning cores].

### 4.3 Criteria of §6
- (b) texture change ≈ 2 µs (criterion ≲ 1 µs: **not met on one thread**); (c) program ≈ 2.2 µs (criterion ≤ 2 µs:
  **at the limit**); memory ≥ 16 MB for surfaces: **yes, without the dump ring**.
- → Continue with B. RR, SH and the light catalog fit with margin [estimated]; for GT the **warning** of §4.2 remains,
  to be confirmed with real data.

## 5. Build chain (Claude's container, ~5 min)
`apt-get install gcc-arm-none-eabi cmake libzip-dev libyaml-dev qemu-user-static gcc-arm-linux-gnueabihf
libc6-dev-armhf-cross`; libelf 0.8.13 (`http://archive.ubuntu.com/ubuntu/pool/main/libe/libelf/libelf_0.8.13.orig.tar.gz`,
unpacked into `libelf-0.8.13.orig/`, `--prefix=<vita-toolchain>/deps --disable-shared`, `ln -s . deps/include/libelf/libelf`);
vita-toolchain (`git clone --recursive`, `PKG_CONFIG_PATH=deps/lib/pkgconfig cmake .. -DUSE_BUNDLED_ENDIAN_H=ON`);
stubs `vita-libs-gen-2 -yml=vita-headers/db/360 -output=stubs`; taiHEN from the official package
(`https://github.com/vitasdk/packages/releases/download/master/taihen.tar.xz`); `JagerDesu/vita-shaders` with
`memmem(start, 0x300000,` in `resolve.c` and `make CC=arm-linux-gnueabihf-gcc`. In `tools/gepresent`:
`VH=<vita-headers> STUBS=<stubs> TAI=<taipkg> sh build.sh` (and `SHACC=<vita-shaders> sh build.sh shaders` for the
shaders). Verify `.vitalink.fstubs.taihen` = `281a3d20 5d6e6353`.
Files container → PC: `device_commit_files` to `Downloads` with a new name per version; in the Windows repo
`tar` cannot overwrite (there is no delete permission): extract into a temporary folder and `cp`.
