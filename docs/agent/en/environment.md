# Build environments and tools

How NativeLink (PSP, MIPS) and NativeDraw (Vita, ARM) are built in each place, what each script in `tools/` does and
how the capturer is tested outside the Vita. Everything that needs PATH stays inside one folder (`~/tc` or
`/home/claude/tc`): system environment variables are not touched (the user's rule for Windows).

## Compilers needed
| For | What | From where |
|---|---|---|
| NativeLink | pspdev (gcc for MIPS, pspsdk) + `systemctrl.h` and `libpspsystemctrl_kernel.a` from Adrenaline | `tools/preparar.sh` downloads them (Adrenaline is cloned only for those two files) |
| NativeDraw | vitasdk (gcc for ARM, vita-toolchain, vita-headers) + `taihen` package + stubs generated with `vita-libs-gen-2` | `tools/preparar.sh` |
| Installer (VPK) | vitasdk (`vita-make-fself`, `vita-mksfoex`, `vita-pack-vpk`) | same |
| Translator shaders (`nativedraw/sombreadores/*.cg`) | Sony's compiler under qemu (JagerDesu/vita-shaders) | only when a `.cg` changes; the compiled `.h` is in the repo (`sombreadores/compilar.sh`) |

vitasdk's GCC 15 uses C23 by default and `taihen.h` does not compile: the `arm-none-eabi-gcc` wrapper that `preparar.sh`
leaves adds `-std=gnu17`.

## Where it is built
| Place | Prepare (once) | Build | Notes |
|---|---|---|---|
| **Claude's container** (Linux x86_64; the normal place) | `T=/home/claude/tc sh tools/preparar.sh` (~3 min; in the container `$HOME` is `/root`, hence `T`) | `T=/home/claude/tc sh tools/compilar.sh <test version>` | The container is wiped between sessions: prepare in every conversation. The container's proxy only lets GitHub releases be downloaded, not its pages be viewed: `preparar.sh` picks the vitasdk version with `git ls-remote` |
| Mac's Linux ARM64 VM (`device_bash`, Ubuntu 22.04 without root) | `sh tools/mac/preparar_vm.sh` | `V=$HOME/mnt/Downloads/vitabridge sh tools/compilar.sh <version>` | pspdev for ARM64 requires glibc 2.38: the script uses conda-forge's with per-program wrappers (not patchelf: it breaks cc1). It is wiped with every session |
| WSL on the Windows PC (user) | `tools/windows/instalar.cmd` (leaves the repo, compilers in Ubuntu's `~/tc` and the relay in Descargas) | `tools/windows/compilar.cmd [version]` | Uses `V` = `Descargas\vitabridge` |
| User's Linux / native Mac | `sh tools/preparar.sh` | `sh tools/compilar.sh` | |
| GitHub Actions (`.github/workflows/ci.yml`) | `tools/preparar.sh` | `tools/compilar.sh` | On every push; the binaries stay as an artifact. If it fails because of vitasdk downloads, pin the version in `preparar.sh` |

## What `tools/compilar.sh` leaves
- `build/nativelink.prx`, `build/nativedraw.suprx`, `build/NativePatch.vpk` (the installer with both plugins and a
  `version.txt`), plus the md5s on screen.
- With a version: copies `build/<plugin>_<version>.*`, `NativePatch_<version>.vpk` and `cambios_<version>.patch` (the
  `git diff`).
- With `V=<relay folder>`: the plugins to `V/enviar/ux0/...` (the relay uploads them to the Vita), the plugins and the VPK
  to the folder above `V` (Descargas) and the versioned copies to `V/respaldo/`.
- **Careful:** `compilar.sh` does not stop if NativeDraw fails to build (it shows the md5 of the previous `.suprx`). To see
  NativeDraw errors: `cd nativedraw && O=/tmp/nd sh build.sh` with `. $T/env.sh` loaded. Always check that the
  md5 changed.
- NativeDraw's md5s change on every build (it carries date and time) and between compilers: they are compared only
  against the binary that was sent to the Vita.
- `nativedraw/build.sh` links twice, the second with `-Tdata` one page beyond the code: `vita-elf-create`
  adds ~1.1 KB at the end of the code segment and failed when the data one started too close.
- Versions: `NATIVELINK_VERSION` in `nativelink/log.h`; `NATIVEDRAW_VERSION` in `nativedraw/version.h`.
  `compilar.sh` reads them for the installer's `version.txt`.

## Tools in `tools/`
| Script | What it does |
|---|---|
| `preparar.sh`, `mac/preparar_vm.sh` | Install the compilers (above) and write `$T/env.sh` |
| `compilar.sh`, `windows/compilar.cmd` | Build everything (above) |
| `windows/instalar.cmd`, `windows/instalar.sh` | Complete environment on Windows + WSL; `instalar.cmd` works on its own (downloads `instalar.sh` from the `dev` branch) |
| `vitabridge.py`, `windows/vitabridge.cmd` | Relay with the Vita (`relay.md`) |
| `resumen_corrida.py <folder>` | Summary of a run: fps every 10 s, sync waits, mirror, seconds where the Vita falls short, DISTINTAS. It finds the lines with regular expressions: if a log's text changes, change it there too |
| `analisis_fase1.py <folder>...` | Summarizes `nativedraw_frames.log` and `nativedraw_tarde.log` (extreme log level): fps, slow frames and the cost of each part per frame |
| `volcado.py <dump folder> [K]` | Draws a dumped frame on the PC imitating the GE and compares it with the normal image and the Vita's (`../../dev/en/dumps.md`). Needs numpy and Pillow |

## Capturer test outside the Vita (qemu)
`nativedraw/prueba_memoria.c` includes `captura.c` and runs it on ARM under qemu on the PC with a fake PSP RAM:
synthetic lists, 8 frames, simulated double mirror, readers. It is for the capture logic (sublist memory,
readers, look-ahead check, frame record); not for graphics. How to build it and what modes it has is
in the file's header. The headers are taken from a folder with only vitasdk's `psp2*` and `vitasdk*` (with the whole
`include` folder, vitasdk's libc clashes with the PC's).

## claude.ai project and memory
- The claude.ai "Ge_patch" project stores the conversation summaries (`claude/resumen_conversacion_*.md`), a
  copy of `CLAUDE.md` and some repo documents. The newest summary is the starting point of every conversation.
- Claude's memory stores the user's decisions (rules, preferences, which games he has); technical details
  go to the repo documents, not to memory.
