# Build environments and tools

How NativeLink (PSP, MIPS) and NativeDraw (Vita, ARM) are built in each place, what each script in `tools/` does and
how the capturer is tested outside the Vita. Everything that needs PATH stays inside one folder (`~/tc` or
`/home/claude/tc`): system environment variables are not touched (the user's rule for Windows).

## Compilers needed
| For | What | From where |
|---|---|---|
| NativeLink | pspdev (gcc for MIPS, pspsdk) + `systemctrl.h` and `libpspsystemctrl_kernel.a` from Adrenaline | `tools/setup.sh` |
| NativeDraw | vitasdk (gcc for ARM, vita-toolchain, vita-headers) + `taihen` package + stubs generated with `vita-libs-gen-2` | `tools/setup.sh` |
| Installer (VPK) | vitasdk (`vita-make-fself`, `vita-mksfoex`, `vita-pack-vpk`) | same |
| Translator shaders (`nativedraw/shaders/*.cg`) | Sony's compiler under qemu (JagerDesu/vita-shaders): `apt-get install qemu-user-static gcc-arm-linux-gnueabihf libc6-dev-armhf-cross`, clone, in `resolve.c` change `memmem(start, -1,` to `memmem(start, 0x300000,`, `make CC=arm-linux-gnueabihf-gcc`, then `SHACC=<folder> sh nativedraw/shaders/build.sh` | only when a `.cg` changes; the compiled `.h` is in the repo |

vitasdk's GCC 15 uses C23 by default and `taihen.h` does not compile: the `arm-none-eabi-gcc` wrapper that `setup.sh`
leaves adds `-std=gnu17`.

## Where it is built
| Place | Prepare (once) | Build | Notes |
|---|---|---|---|
| **Claude's container** (Linux x86_64; the normal place) | `T=/home/claude/tc sh tools/setup.sh` (~15 s; in the container `$HOME` is `/root`, hence `T`) | `T=/home/claude/tc sh tools/build.sh <test version>` | The container is wiped between sessions: prepare in every conversation. |
| Mac's Linux ARM64 VM (`device_bash`, Ubuntu 22.04 without root) | `sh tools/mac/setup_vm.sh` | `V=$HOME/mnt/Downloads/vitabridge sh tools/build.sh <version>` | pspdev for ARM64 requires glibc 2.38: the script uses conda-forge's with per-program wrappers (not patchelf: it breaks cc1). It is wiped with every session |
| WSL on the Windows PC (user) | `tools/windows/install.cmd` (leaves the repo, compilers in Ubuntu's `~/tc` and the relay in Descargas) | `tools/windows/build.cmd [version]` | Uses `V` = `Descargas\vitabridge` |
| User's Linux / native Mac | `sh tools/setup.sh` | `sh tools/build.sh` | |
| GitHub Actions (`.github/workflows/ci.yml`) | `tools/setup.sh` | `tools/build.sh` | On every push; the binaries stay as an artifact. Also runs `tools/test_ci.sh` |

## Emulator package
`sh tools/emu.sh` (needs `tools/setup.sh`) downloads PPSSPP headless from the release named in `tools/emu-version`, built by
`.github/workflows/emulator.yml` for Linux x86_64 (container, WSL), Linux ARM64 (the Mac's VM) and macOS ARM64, and runs
NPTEST (`docs/dev/en/nptest.md`). To change it: bump `tools/emu-version` and push. It checks the scenes, not the plugins.

## Toolchain package
`tools/setup.sh` downloads and unpacks one pinned package, named by `tools/toolchain-version` (`toolchain-1`), from a
GitHub release built by `.github/workflows/toolchain.yml` on push.
- If the package gives a 404, or with `FROM_SOURCE=1`, it fetches each part from source, with vitasdk pinned to
  `sdk-snapshot-20261015.850.1`.
- To change it: bump `tools/toolchain-version` and push. To use a new pin, delete `$T` first.
- The package is x86_64 only: the Mac's ARM64 VM keeps `tools/mac/setup_vm.sh`.

## What `tools/build.sh` leaves
- `build/nativelink.prx`, `build/nativedraw.suprx`, `build/NativePatch.vpk` (the installer with both plugins and a
  `version.txt`), plus the md5s on screen.
- With a version: copies `build/<plugin>_<version>.*`, `NativePatch_<version>.vpk` and `changes_<version>.patch` (the
  `git diff`).
- With `V=<relay folder>`: the plugins to `V/send/ux0/...` (the relay uploads them to the Vita), the plugins and the VPK
  to the folder above `V` (Descargas) and the versioned copies to `V/backup/`.
- It stops on the first failure and shows NativeDraw's errors. NativeLink is always a clean build.
- `PROD=1` in the environment gives a production build of both plugins, without `NP_DEV` (also in `nativedraw/build.sh`
  and `nativelink/Makefile`); the default is dev (`../../dev/en/logs-and-switches.md`).
- NativeDraw's md5s change on every build (it carries date and time) and between compilers: they are compared only
  against the binary that was sent to the Vita.
- `nativedraw/build.sh` links twice, the second with `-Tdata` one page beyond the code: `vita-elf-create`
  adds ~1.1 KB at the end of the code segment and failed when the data one started too close.
- Versions: `NATIVELINK_VERSION` in `nativelink/log.h`; `NATIVEDRAW_VERSION` in `nativedraw/version.h`.
  `build.sh` reads them for the installer's `version.txt`.

## Tools in `tools/`
| Script | What it does |
|---|---|
| `setup.sh`, `mac/setup_vm.sh` | Install the compilers (above) and write `$T/env.sh` |
| `build.sh`, `windows/build.cmd` | Build everything (above) |
| `windows/install.cmd`, `windows/install.sh` | Complete environment on Windows + WSL; `install.cmd` works on its own (downloads `install.sh` from the `dev` branch) |
| `vitabridge.py`, `windows/vitabridge.cmd` | Relay with the Vita (`relay.md`) |
| `run_summary.py <folder>` | Summary of a run: fps every 10 s, sync waits, mirror, seconds where the Vita falls short, MISMATCHES. `--check [--ref <folder>] [--target 30\|60]` judges it (fps target order: `good-practices.md` §7). It finds the lines with regular expressions: if a log's text changes, change it there too |
| `run_compare.py <ref> <run> [--check]`, `run_compare.py <run> --save <json>` | Two runs zone by zone (race, hard, crowd, by draws per frame): fps, HD frames, capturer, translator, GXM, GPU. Medium log is enough; same switches on both sides. Reference lap: `tests/perf/rr_hold_0.0.21.3.json` (Ridge Racer, `hold_game`, extreme log: compare only with care; the fair reference is a run of the same version with `no_join`-style switches) |
| `test_ci.sh`, `test_local.sh`, `test_link.py` | The three tests (below) |
| `phase1_analysis.py <folder>...` | Summarizes `nativedraw_frames.log` and `nativedraw_late.log` (extreme log level): fps, slow frames and the cost of each part per frame |
| `prof_summary.py <run> [from_s to_s]` | Ranks the `prof` lines of the extreme log level: where each frame goes, per piece (`../../dev/en/logs-and-switches.md`) |
| `bottleneck.py <run> [from_s to_s]` | What limits the HD frames, second by second: translator, GPU, no new frame; translator parts, pixels per program, capturer (`../../dev/en/logs-and-switches.md`) |
| `perf/bench.sh [--save] [scene]` | Performance bench of the capturer and the record on ARM under qemu (`perf/capture_bench.c`): per scene, a deterministic estimate of Vita microseconds from the memory counters and the CPU time under qemu, compared with `perf/baseline.json`. `perf/iterate.sh [--keep]` adds the capturer test and prints KEEP/REVERT; `perf/ai_loop.sh [n]` runs Claude Code unattended on it (`perf-loop.md`) |
| `perf/bench.sh`, `perf/iterate.sh`, `perf/instr.sh`, `perf/ab.sh`, `perf/tr_check.sh`, `perf/arm_count.sh` | Performance bench (`perf-loop.md`): counters, instructions per frame, verdict, comparison with a commit, same translator output, real ARM count |
| `perf/sync_lab.sh [check \| <scene> [<mode>]]` | Lab for how the capturer reads PSP memory (`perf/sync_lab.c`, the real capturer and record under qemu, a scripted game): today's gate, `hold_game` and the photo at the sync, frame by frame against the held capture (`DETAIL=1`: per frame); then the timing model `perf/sync_model.py` (game fps and HD per design, from the Vita logs). `../../dev/en/performance-plan.md` §D |
| `dump.py <dump folder> [K]` | Draws a dumped frame on the PC imitating the GE and compares it with the normal image and the Vita's (`../../dev/en/dumps.md`). Needs numpy and Pillow |

## Tests
Rules: `good-practices.md` §7. When to run them: `workflow.md` §3.

| Test | Command | Where |
|---|---|---|
| Automatic: no functional regression | `sh tools/test_ci.sh` | `.github/workflows/ci.yml`, or anywhere after `setup.sh` with the apt packages `qemu-user` and `gcc-arm-linux-gnueabihf` |
| Local: performance did not regress | `sh tools/test_local.sh <run> [ref folder or .json]` (`run_summary.py --check [--ref]`, `run_compare.py --check`) | PC, on a saved run |
| Execution: components communicate | `python3 tools/test_link.py <run> [version.txt]` | PC, on the logs after a Vita run |
| Performance on the PC (not one of the three: a filter before the Vita) | `sh tools/perf/bench.sh`, `sh tools/perf/iterate.sh` | PC, same packages as the capturer test; `perf-loop.md` |

`test_ci.sh` runs the capturer test, the memory-reading lab (`sync_lab check`), the parsers against the `tests/run/` fixture (and a damaged copy, which must
fail) and the production builds.

### Capturer test outside the Vita (qemu)
`nativedraw/capture_test.c` includes `capture.c` and runs it on ARM under qemu on the PC with a fake PSP RAM:
synthetic lists, 8 frames, simulated double mirror, readers. It is for the capture logic (sublist memory,
readers, look-ahead check, frame record); not for graphics. With no arguments it runs 14 cases with 10 pass/fail checks
and returns an exit code. How to build it and what modes it has is in the file's header. The headers are taken from a
folder with only vitasdk's `psp2*` and `vitasdk*` (with the whole `include` folder, vitasdk's libc clashes with the PC's).

## claude.ai project and memory
- The claude.ai "Ge_patch" project stores the conversation summaries (`claude/resumen_conversacion_*.md`), a
  copy of `CLAUDE.md` and some repo documents. The newest summary is the starting point of every conversation.
- Claude's memory stores the user's decisions (rules, preferences, which games he has); technical details
  go to the repo documents, not to memory.
