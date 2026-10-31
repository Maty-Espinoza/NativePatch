# NativePatch

*[Versión en español: `README.es.md`.]*

Play PSP games on the PS Vita at the Vita's resolution (960x544) with
[Adrenaline 8](https://github.com/isage/Adrenaline), without slowing the game down.

> **Work in progress; not ready for playing yet.** Today the Vita draws 2D games (KOF 96 at 60 fps, Metal Slug XX,
> Retro City Rampage) at double size; 3D games are captured and measured but still look as they do without the
> plugins. See the status table below.

> **Made with AI ("vibecoded").** The code and the documents were written by Claude (Anthropic) following the tests,
> ideas and decisions of [Maty-Espinoza](https://github.com/Maty-Espinoza), who tests it on his own Vita.

## How it works
The PSP inside the Vita runs the game unchanged, at its original speed. Two plugins work together:

| Plugin | Where it runs | What it does |
|---|---|---|
| **NativeLink** (`nativelink.prx`) | PSP side (inside Adrenaline, next to the game) | Changes nothing in the game: writes into a shared memory ("mailbox") every display list the game sends to its graphics chip (the GE), copies for the Vita what the Vita cannot see (the PSP's VRAM, the "mirror") and, if the Vita falls behind, makes the game wait a little (the "gate", up to 5 ms). |
| **NativeDraw** (`nativedraw.suprx`) | Vita side (taiHEN plugin in Adrenaline's process) | Reads the mailbox, walks the same lists from the Vita, records what each frame needs and draws it again with the Vita's GPU at double size, replacing the image Adrenaline shows. If a frame contains something it cannot draw yet, the normal image of the game is shown: never worse than without the plugins. |

More detail: [`docs/dev/en/overview.md`](docs/dev/en/overview.md) and [`docs/dev/en/how-it-works.md`](docs/dev/en/how-it-works.md).

## Status
| Stage | | State |
|---|---|---|
| 0–1 | Measure whether the Vita can draw what the PSP draws (draws per frame, cost of each one in GXM) | done |
| 2 | Capture the game's lists from the Vita without slowing it down | done (Gran Turismo ≥ 58 fps in its heaviest part, Ridge Racer and LocoRoco at 60) |
| 3 | Minimal drawing on the Vita: 2D, block copies, HD presentation | **in progress** (KOF 96 at 60 fps, Metal Slug XX at its 30 fps; next: a texture cache between frames) |
| 4–6 | 3D, HD framebuffers, performance | pending |

## Requirements
- PS Vita with HENkaku/Ensō and **Adrenaline 8** (earlier versions are untested).
- VitaShell to install the VPK.

## Install
1. Install `NativePatch.vpk` with VitaShell and open **NativePatch** (the installer).
2. **Instalar / actualizar** (install / update): copies NativeLink to `ux0:pspemu/seplugins/` and enables it in
   `plugins.txt` (Adrenaline 8's plugin manager); copies NativeDraw to `ux0:data/nativedraw/` and adds it to the
   `*PSPEMUCFW` section of taiHEN's `config.txt`. A backup of each configuration file it changes is left in
   `ux0:data/nativepatch/respaldo/`.
3. Reboot the Vita if the installer asks for it (when `config.txt` changed).

To update, install the new VPK and use **Instalar / actualizar** again. The installer also uninstalls.
- Drawing on the Vita is enabled by the file `ux0:data/nativedraw/dibujar` (without it NativeDraw only measures).
- Open a game holding **L** (or SELECT): NativeLink stays off for that session.
- **L+R+START** during a game: NativeDraw's menu (shows the versions for now). **L+R+SELECT**: a dump for debugging.
- Logs in `ux0:pspemu/`: `nativelink.log`, `nativedraw.log` and `nativedraw_captura.log`.
- With Adrenaline's "Original" filter the Vita does not draw with GXM and NativeDraw cannot act: use Bilinear.

## Build
Linux x86_64 (or WSL on Windows; on Windows `tools/windows/instalar.cmd` sets everything up):
```sh
sh tools/preparar.sh     # once: PSP (pspdev) and Vita (vitasdk) compilers in ~/tc
sh tools/compilar.sh     # leaves nativelink.prx, nativedraw.suprx and NativePatch.vpk in build/
```

## Documentation
| Folder | For | Contents |
|---|---|---|
| [`docs/dev/`](docs/dev/en/README.md) (`en`, `es`) | developers | what it is, how it works, architecture and roadmap, logs and switches, dumps, Adrenaline notes, findings and limits, versions, graphics basics |
| [`docs/agent/`](docs/agent/en/README.md) (`en`, `es`) | the AI that works on the project (and anyone who wants to repeat its workflow) | workflow, build environments, the relay with the Vita, good practices, current stage plan |
| [`docs/changelog/`](docs/changelog/en/README.md) (`en`, `es`) | history | what was done and measured in each version, as it was written at the time (not updated) |
| [`TODO.md`](TODO.md), [`CHANGELOG.md`](CHANGELOG.md) | everyone | pending items; changes of each version |
| [`CLAUDE.md`](CLAUDE.md) | the AI | what to read first in every conversation |

## Versions
NativeLink and NativeDraw each have their own `0.MINOR.PATCH` version; until the first release (0.1.0) everything is
0.0.x. Versions tested on the Vita carry a fourth number (`0.0.8.22`) and go into the repo marked "(prueba)" (test).
Rules and history: [`docs/dev/en/versions.md`](docs/dev/en/versions.md).

## Origin and credits
It started as a fork of [GePatch](https://github.com/TheOfficialFloW/GePatch) by TheFlow (abandoned at v0.2), which
scaled the image by making the PSP draw bigger. That path had a performance limit (the PSP's graphics chip cannot fill
4 times the pixels) and errors that came from fooling the game, so the project changed architecture: the PSP draws
normally and the Vita reads and draws. Since NativeLink 0.0.2 no GePatch code remains; the project as it was when it was
still GePatch is in the `gepatch-dev66` tag. Thanks to TheFlow for GePatch and Adrenaline, and to isage for
Adrenaline 8.

## License
[GPLv3](LICENSE): anyone can use, study, modify and share the code and make their own version, as long as what they
distribute stays GPLv3 with its source code available. Third-party parts: [`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md).
