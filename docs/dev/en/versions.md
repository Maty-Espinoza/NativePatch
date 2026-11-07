# Versions

## Rule (the user's decision, 2026-10-29; corrected the same day when stage 3 started)
NativeLink (PSP side) and NativeDraw (Vita side) are sibling projects: each one carries its own version
`0.MINOR.PATCH` and can change without the other.

- **Until the first release everything is 0.0.x**, whether one or both plugins are touched: each change raises the PATCH of the plugin that
  changed. **The first release is NativeLink 0.1.0 + NativeDraw 0.1.0.**
- After the first release:

| Change | What goes up | Example |
|---|---|---|
| Touches **only one** plugin (what they pass to each other does not change) | the **PATCH** of that plugin | NativeDraw 0.1.3 → 0.1.4; NativeLink stays at 0.1.2 |
| Needs **both** (shared mailbox, mirror, gate, log...) | the **MINOR** of both, and the PATCH goes back to 0 | NativeLink 0.1.2 + NativeDraw 0.1.4 → 0.2.0 + 0.2.0 |

- **Two plugins with the same MINOR work together.** The installer always carries a compatible pair.
- **Test versions:** a fourth number on top of the version being prepared (`0.0.3.1`, `0.0.3.2`...). The commit
  that confirms carries the version confirmed on the Vita, without the fourth number. Each bug fixed = one commit with its version.
- **Test commits are pushed too** (the user's decision, 2026-10-30): to continue on another computer or in another
  conversation, each test version sent to the Vita can go as a commit to `dev`, marked "(test)" with its full
  number (`NativeDraw 0.0.8.15 (test)`). What matters are the versions: that each commit says which version it
  carries and that the confirmed ones follow the numbering without gaps.
- **No lost versions in the repo:** the commit's version is always the one that follows the last one confirmed on GitHub
  (`git log --oneline -- nativedraw/version.h` or `nativelink/log.h`), even if the tests used a different numbering.
  E.g.: GitHub had NativeDraw 0.0.6 and the tests reached 0.0.8.12 → the commit is NativeDraw 0.0.7. Commits
  marked "(test)" do not count as a confirmed version.
- **NativeDraw 0.0.8 tests** (confirmed by the user): they continue from **0.0.8.13**, so as not to
  clash with the names in `vitabridge\backup\` and `vitabridge\stage3_0.0.8.x\`, which already used 0.0.8.1–0.0.8.12
  as tests of 0.0.7.
- The first release (0.1.0) is marked with a tag and release on GitHub when the user decides.
  Before 1.0 the "Clean test leftovers" option is removed from the installer.
- Where the number lives: NativeLink in `nativelink/log.h` (`NATIVELINK_VERSION`); NativeDraw in `nativedraw/version.h`
  (`NATIVEDRAW_VERSION`). `tools/build.sh` reads them for the installer (`version.txt` inside the VPK).
- Git tags: `nativelink-X.Y.Z` / `nativedraw-X.Y.Z` when something is published. The `v0.1`…`v0.2` and `0.1` tags
  in the repo belong to the original GePatch by TheFlow; `gepatch-dev66` is the last state with the old names.

## History
| NativeLink | NativeDraw | Commit | What |
|---|---|---|---|
| 0.0.1 | 0.0.1 | `4faaa2b` | Rename and folders. Same behavior as GePatch 0.2-dev66 + gepresent v13.13 |
| 0.0.2 | | `b5d4a56` | NativeLink: what was inherited from GePatch is removed (architecture A, menu, dumps) |
| | 0.0.2 | `6992890` | NativeDraw: the old presentation, the GXM test bench and the dumps are removed |
| | 0.0.3 | `34dee77` | Stage 3, step 3.0: transparent presenter, off by default (`../../agent/en/stage3-plan.md`) |
| | 0.0.4 | `a0d85da` | Stage 3, step 3.1: L+R+START menu drawn by NativeDraw (for now, the versions) |
| | 0.0.5 | `1ae7191` | Stage 3, step 3.2: frame record (with `draw`) |
| | 0.0.6 | `da1daa0` | Stage 3, step 3.3 (first version): the Vita draws the frame record (Retro City); "covered" |
| 0.0.3 | 0.0.7 | `f6384f7` | Stage 3, step 3.3 in KOF 96 (own shaders, textures through DMA, dumps) and broken frames fixed. Tests: NativeDraw 0.0.7.1–0.0.8.12 and NativeLink 0.0.3.1–0.0.3.3 (`8573486` was a test commit, 0.0.8.10 + 0.0.3.2) |
| | (tests 0.0.8.15–0.0.8.22) | `a045974`…`3ab3b91` | Stage 3 test commits: GPU timer, textures sorted by the CPU, one scene for the surface (KOF 96 at 60 fps), Metal Slug XX, diagnosis of the white glitch; 0.0.8.22 + NativeLink 0.0.3.4: code cleanup. The commit that confirms this work will be **NativeDraw 0.0.9** |
| (test 0.0.3.5) | (test 0.0.8.23) | `8cd1ff4`…`5f70fe0` | Dev builds, test commits: code in English, ready-made toolchain, measurements behind `NP_DEV`, the three tests. Must look and run the same |
| (test 0.0.3.6) | (test 0.0.8.24) | `8cd1ff4`…`b76c030` | Whole project in English: code, source and tool names, log texts, on-disk names, relay and installer (old names below); fps target per game in `run_summary.py --check`; the relay starts awake. Must look and run the same |
| (test 0.0.3.6) | (test 0.0.8.25–0.0.8.26) | `3d33be9`…`845ff84` | Small texture copies by CPU and 256 textures per frame; logs for `vertex_out`, giant draws, late frames and the texture limit |
| (test 0.0.3.6) | (test 0.0.8.27) | `3522a99` | Each game has its own NativeDraw logs |
| (test 0.0.3.7–0.0.3.11) | (test 0.0.8.28–0.0.8.29) | `f9669cc`…`924332b` | Game ID without reading the disc (Street Fighter Alpha 3 MAX opens); the capturer walks the first lists |

Tested on the Vita with Gran Turismo: `../../changelog/en/etapa3_preparacion.md`.

## Equivalence with the old names
Before 2026-10-29 the PSP side was called **GePatch** (`ge_patch.prx`, versions `0.2-devNN`, with tests `devNN.x`) and
the Vita side **gepresent** (`gepresent.suprx`, versions `vNN.M`). The documents in `../../changelog/en/` use those numbers.

| Old | New |
|---|---|
| GePatch 0.2-dev66 | = NativeLink 0.0.1 |
| gepresent v13.13 | = NativeDraw 0.0.1 |
| `ux0:/pspemu/seplugins/ge_patch.prx` | `ux0:/pspemu/seplugins/nativelink.prx` |
| `ux0:data/gepresent/gepresent.suprx` and its switches | `ux0:data/nativedraw/nativedraw.suprx` (same switches) |
| `ge_patch.log`, `ge_patch_prev.log` | `nativelink.log`, `nativelink_prev.log` |
| `ge_patch_gepresent.log` | `nativedraw.log` |
| `ge_patch_captura.log` (and `_prev`), `ge_patch_frames.log`, `ge_patch_tarde.log` | `nativedraw_capture.log` (and `_prev`), `nativedraw_frames.log`, `nativedraw_late.log` |

Since NativeDraw 0.0.8.24 the old switch files are no longer read; the installer's "Clean test leftovers" removes them:

| Old | New |
|---|---|
| `dibujar`, `espejo_espera`, `medir_ge`, `medir_gpu`, `memo_revisar` | `draw`, `mirror_wait`, `ge_timing`, `gpu_timer`, `memo_check` |
| `nucleo_captura`, `captura_sin_lectores`, `compuerta_sync` | `capture_core`, `capture_no_readers`, `gate_sync` |
| `sin_adelanto`, `sin_captura`, `sin_compuerta`, `sin_espejo`, `sin_libre`, `sin_memoria` | `no_lookahead`, `no_capture`, `no_gate`, `no_mirror`, `no_idle`, `no_memo` |
| `registro` (`minimo`, `mediano`, `extremo`) | `log_level` (`minimum`, `medium`, `extreme`) |
| `volcado` (`desactivado`, `pantallazo`, `frame`, `secuencia`), `volcados/`, `reproducir` | `dump` (`off`, `screenshot`, `frame`, `sequence`), `dumps/`, `replay` |
| `fK_registro.bin`, `fK_reproduccion*.bmp` | `fK_record.bin`, `fK_replay*.bmp` |
| `nativedraw_captura*.log`, `nativedraw_tarde*.log` | `nativedraw_capture*.log`, `nativedraw_late*.log` |
| Relay: `enviar/`, `enviados/`, `recibidos/`, `respuestas/`, `respaldo/`, `bajar_*.txt`, `listar_*.txt`, `vigilar.txt`, `estado.txt`, `etapa<N>_<v>/` | `send/`, `sent/`, `received/`, `responses/`, `backup/`, `get_*.txt`, `list_*.txt`, `watch.txt`, `status.txt`, `stage<N>_<v>/` |

Milestones of the old numbering (details in `../../changelog/`):

| Old version | What |
|---|---|
| GePatch 0.2 (TheFlow) | Original GePatch, abandoned |
| dev1–dev60 | Architecture A: the PSP draws larger (menu, per-game options, .gpd dumps, PPSSPP lab) |
| gepresent v1–v10 | Presentation of the GePatch image with the Vita's GPU; log and dumps written by the Vita |
| gepresent v11 | Stage 1: GXM test bench |
| dev61 + v12 | Stage 2: shadow mode + capturer on the Vita |
| dev62–dev64 + v13.1–v13.6 | Sublist memory, VRAM mirror (double), T4/T6 |
| v13.7 | Core bench (GXM recording on several cores) |
| dev65 + v13.9 | Analysis phase 1, log levels |
| dev66 + v13.10–v13.12 | Phase 2: gate before enqueuing, compact statistics, look-ahead check, MISMATCHES fixed |
| v13.13 | Ridge Racer at 60 fps: interpret line by line, capturer on any core |
| 0.0.4 | 0.0.9 | `c9c50f3` | Stage 3: KOF 96 at 60 fps, Metal Slug XX at 30 fps, NPTEST synthetic battery and comparator, emulator package, fixes from the NPTEST dumps. Tests: NativeDraw 0.0.8.13–0.0.8.32 and NativeLink 0.0.3.1–0.0.3.11 |
| 0.0.4 | 0.0.10 | `b8f6d11` | Texture cache between frames for RAM textures. Metal Slug XX: capturer 19.4 → 8.3 ms per frame. Test: NativeDraw 0.0.10.1 |
| (test 0.0.4.1) | (test 0.0.12.1–0.0.12.14) | `9fa9f60`…`35e8e59` | 3D on the Vita with Ridge Racer: menus and selector in HD |
| 0.0.5 | 0.0.12 | (commit "NativeDraw 0.0.12 + NativeLink 0.0.5") | 3D in the translator, real GE registers, capturer and translator faster |
| 0.0.6 | 0.0.13 | (commit "NativeDraw 0.0.13 + NativeLink 0.0.6") | Frame profile, gate always able to return before enqueuing, lists left behind dropped, own log off the drawing thread, HD only when it holds. Tests: NativeDraw 0.0.13.1–0.0.13.7, NativeLink 0.0.6.1–0.0.6.2 |
| 0.0.4 | 0.0.11 | (commit "NativeDraw 0.0.11") | Triangle fans, cache sample fixed, small textures out of the cache, dumps fall back to one frame. Tests: NativeDraw 0.0.11.1–0.0.11.3 |
| 0.0.6 | 0.0.14 | (commit "NativeDraw 0.0.14") | Ridge Racer race in HD: scenes cut, reads of the surface and of an image being drawn, what the PSP computes comes from the mirror, capturer skips frames nobody takes, failed frames saved. Tests: NativeDraw 0.0.14.1–0.0.14.15 |
| 0.0.6 | 0.0.15 | (commit "NativeDraw 0.0.15") | Ridge Racer lap at 20-25 HD frames per second: CPU and GPU in parallel, draws joined, cached vertices, nitro, mirror fixes, intermediate images no longer shrunken. Tests 0.0.15.1–0.0.15.10 |
| 0.0.6 | 0.0.16 | (commit "NativeDraw 0.0.16") | Ridge Racer without high clocks: game at 60 fps, the whole race in HD at 21-25 HD frames per second; mirror leak fixed, capturer lighter. Tests 0.0.16.1–0.0.16.8 |
| 0.0.6 | 0.0.17 | (commit "NativeDraw 0.0.17") | `nd_fd` (GPU 45 -> 38 ms per HD frame in the race), NEON loop for the common 3D vertex (translator 42 -> 38 ms). Tests 0.0.17.1–0.0.17.2 |
| 0.0.6 | 0.0.18 | (commit "NativeDraw 0.0.18") | `nd_fa` (GPU 38 -> 32.5 ms per HD frame in the race), `vert_split` and `thread_split`. Tests 0.0.17.3–0.0.17.6 |
| 0.0.6 | 0.0.19 | (commit "NativeDraw 0.0.19") | `nd_v3`: first stage of C.1, the GPU moves 54 % of the vertices of the race (vertices ~19.5 -> 13.1 ms per HD frame). Tests 0.0.18.1–0.0.18.8 |
| 0.0.6 | 0.0.20 | (commit "NativeDraw 0.0.20") | `nd_v4`: the GPU lights the cars (car selection: translator ~33 -> 16.5 ms per HD frame). Tests 0.0.19.1–0.0.19.4 |
| 0.0.6 | 0.0.21 | (commit "NativeDraw 0.0.21") | A frame is skipped only when the capturer is short of time or the GPU still reads the record (car selection: ~28 -> ~35 HD frames per second). Tests 0.0.20.1–0.0.20.3 |
| 0.0.7 | 0.0.22 | (commit "NativeDraw 0.0.22 + NativeLink 0.0.7") | With `hold_game`, Ridge Racer stays in HD for the whole race. Draws joined by indices and a short path in the translator: crowd 18.8 -> 25.5 fps, translator 45 -> 30 ms. `measures.h`, `run_compare.py`. Tests 0.0.21.1–0.0.21.6 and NativeLink 0.0.6.1 |
| 0.0.7 | 0.0.23 | (commit "NativeDraw 0.0.23") | Rear-view mirror on the GPU, in an image of its own that is pasted on the surface: hard stretches 24.5 -> 28.8 fps, crowd 25.5 -> 28.4 fps. Test 0.0.22.1 |
| 0.0.7 | 0.0.24 | (commit "NativeDraw 0.0.24") | Images that only blur at PSP size: normal race 29.7 -> 32.4 fps, GPU ~4 ms less. Scene and resource logs. Tests 0.0.23.1-0.0.23.5 |
| 0.0.8 | 0.0.25 | (commit "NativeDraw 0.0.25") | God of War Ghost of Sparta: the capturer reads the sublists copied to VRAM during the frame, and a game no longer inherits the previous game's GE state. No HD yet in the menu or in play: over the record's limit of draws |
| 0.0.8 | 0.0.26 | (commit "NativeDraw 0.0.26") | God of War menu and half of the combat in HD: record in the spare block, bones, shared intermediate images, VRAM vertices on the GPU, transfers by DMA (combat 5.5 -> 6.8 fps with `hold_game`). Tests 0.0.25.1-0.0.25.26 |
| 0.0.8 | 0.0.27 | (commit "NativeDraw 0.0.27") | God of War combat in HD with the glow: skinning and lights on the GPU (`nd_v5`), `tx` 3 MB, fixed blend colors (translator ~230 -> ~50 ms per HD frame; game ~8 fps, the capturer is the limit). Tests 0.0.26.1-0.0.26.6 |
| 0.0.8 | 0.0.28 | (commit "NativeDraw 0.0.28") | Capturer: repeated RAM textures skip the lookup, small swizzled textures sorted from cached memory, transfers through a cached copy the record reads vertices from (God of War combat ~8 -> 11.2 fps, capturer ~125 -> ~94 ms). Test 0.0.27.1 |
