# NativePatch: what it is and where it stands

Developer documentation. If you know nothing about the project, start here; then `how-it-works.md`.
If you also know nothing about graphics (display lists, textures, framebuffers), read `graphics-basics.md` first.

## What it is
NativePatch shows PSP games on the PS Vita at the Vita's resolution (960x544, double on each side) with
[Adrenaline 8](https://github.com/isage/Adrenaline), without the game losing speed. The PSP inside the Vita runs the game
as is, at its original speed; the Vita reads the drawing instructions the game sends to its graphics chip and draws them
again with its own GPU, bigger.

It is a project **in development** and **made with AI ("vibecoded")**: the code and the documents were written by Claude
(Anthropic) following the tests, ideas and decisions of [Maty-Espinoza](https://github.com/Maty-Espinoza), who tests it
on his own Vita. It started as a fork of [GePatch](https://github.com/TheOfficialFloW/GePatch) by TheFlow, but it no longer
shares code with it (details in "Origin", below). GPLv3 license.

## The two parts
| Plugin | Where it runs | What it does |
|---|---|---|
| **NativeLink** (`nativelink.prx`) | PSP side, inside Adrenaline, next to the game | It changes nothing in the game: it notes in a shared memory (the "mailbox") every display list the game sends to its graphics chip (the GE), copies for the Vita what the Vita cannot see (the PSP's video memory, with the "mirror") and, if the Vita falls behind, makes the game wait a little (the "gate"). |
| **NativeDraw** (`nativedraw.suprx`) | Vita side, taiHEN plugin in the Adrenaline process | It reads the mailbox, walks the same lists from the Vita (the "capturer"), saves what each frame needs (the "frame record") and draws it with the Vita's GPU at double size (the "translator"); it replaces the image Adrenaline shows with its own (the "presenter"). If a frame has something it does not know how to draw, it shows the game's normal image. |

Also: an **installer** (`NativePatch.vpk`, `instalador/`) that copies the two plugins to their place and enables them, and
**PC tools** (`tools/`): the relay with the Vita, build scripts and readers for logs and dumps.

How each part works: `how-it-works.md`.

## Status (October 2026)
| Stage | What | Status |
|---|---|---|
| 0–1 | Measure whether the Vita can draw what the PSP draws (number of draws per frame, cost of each one in GXM) | done |
| 2 | Capture the game's lists from the Vita without slowing it down | done: Gran Turismo ≥ 58 fps in the heaviest scenes (60 almost always), Ridge Racer and LocoRoco at 60, Silent Hill at its own speed |
| 3 | Minimal drawing on the Vita: 2D (menus, sprites), copies, presentation in HD | **in progress**: Retro City Rampage and KOF 96 drawn by the Vita at double size (KOF at 60 fps), Metal Slug XX at its 30 fps. Next: texture cache between frames |
| 4–6 | 3D, framebuffers in HD, performance (reuse what was recorded, merge draws) | pending |

Complete roadmap, with the questions of each stage: `architecture-and-roadmap.md` §6. Plan and results of
stage 3: `../../agent/en/stage3-plan.md`.

Games used for testing: Gran Turismo (UCUS-98632), Ridge Racer (ULJS-00001), Silent Hill Shattered Memories
(ULUS-10450), LocoRoco (NPEG-00024), Retro City Rampage DX (NPUH-10199), KOF 96 (NeoGeo Station, NPUH-10119) and
Metal Slug XX (ULUS-10495). 3D games are not drawn on the Vita yet: they look as they do without the plugin.

## Install and use
Requirements: PS Vita with HENkaku/Ensō and **Adrenaline 8** (earlier versions are not tested) and VitaShell.
1. Install `NativePatch.vpk` with VitaShell and open **NativePatch** (the installer).
2. **Install / update**: copies NativeLink to `ux0:pspemu/seplugins/` and enables it in `plugins.txt` (Adrenaline 8's
   plugin manager); copies NativeDraw to `ux0:data/nativedraw/` and adds it in the `*PSPEMUCFW` section of taiHEN's
   `config.txt`. Before changing a configuration file it leaves a backup in `ux0:data/nativepatch/respaldo/`.
3. Restart the Vita if the installer asks for it (when `config.txt` changed).

To update: install the new VPK and use **Install / update** again. The installer also uninstalls.
- Drawing on the Vita is enabled with the file `ux0:data/nativedraw/dibujar` (without it, NativeDraw only measures).
- Open a game while holding **L** (or SELECT): NativeLink stays off for that session.
- **L+R+START** during the game: NativeDraw menu (today it shows the versions). **L+R+SELECT**: dump for debugging.
- Logs in `ux0:pspemu/`: `nativelink.log`, `nativedraw.log` and `nativedraw_captura.log` (`logs-and-switches.md`).
- With Adrenaline's "Original" filter the Vita does not draw with GXM and NativeDraw cannot act: use Bilinear.

## Build
Linux x86_64 (or WSL on Windows; on Windows, `tools/windows/instalar.cmd` sets everything up):
```sh
sh tools/preparar.sh     # once: PSP (pspdev) and Vita (vitasdk) compilers in ~/tc
sh tools/compilar.sh     # leaves nativelink.prx, nativedraw.suprx and NativePatch.vpk in build/
```
Details of the environments (container, Mac VM, WSL, GitHub Actions): `../../agent/en/environment.md`.

## Versions
NativeLink and NativeDraw each have their own `0.MINOR.PATCH` version; until the first release (0.1.0) everything is 0.0.x.
The versions tested on the Vita carry a fourth number (`0.0.8.21`) and go to the repo marked "(test)". Rules
and history: `versions.md`. Changes of each version: `../../../CHANGELOG.md`. Pending items: `../../../TODO.md`.

## What it does not do (yet) and known limits
- **It does not draw 3D:** 3D games (GT, RR, SH) are captured and measured but look as they do without the plugin. That is stage 4.
- **One frame of delay:** the Vita's image appears one frame after the PSP's (16.7 ms at 60 fps).
- **PS1 games:** they are not touched. In Adrenaline 8 the PS1 image comes from the Vita's PS1 emulator, not from the
  PSP's graphics chip: there is nothing to capture.
- **What the system draws on top** (the PS button dialog): it is detected by comparing with the normal image and the
  normal image is shown while it lasts.
- More detail, with the measured numbers and what was ruled out: `findings.md`.

## Origin and credits
It started as a fork of [GePatch](https://github.com/TheOfficialFloW/GePatch) by TheFlow (abandoned at v0.2), which scaled
the image by making the PSP draw bigger. That path had a performance limit (the PSP's graphics chip cannot keep up
with 4 times the pixels) and bugs that came from tricking the game, so the project changed architecture:
the PSP draws normally and the Vita reads and draws (`architecture-and-roadmap.md`). Since NativeLink 0.0.2 no GePatch
code is left; the state of the project when it was still GePatch is in the tag `gepatch-dev66`. Thanks to TheFlow for
GePatch and for Adrenaline, and to isage for Adrenaline 8. Third-party parts: `../../../THIRD_PARTY_NOTICES.md`.

## Documentation map
| Folder | For whom | What is in it |
|---|---|---|
| `docs/dev/` (en, es) | developers | this document, `how-it-works.md`, `architecture-and-roadmap.md`, `logs-and-switches.md`, `dumps.md`, `adrenaline.md`, `findings.md`, `versions.md`, `graphics-basics.md` |
| `docs/agent/` (en, es) | the AI working on the project (and anyone who wants to repeat its flow) | how the work is done, environments, relay with the Vita, good practices, plan of the current stage |
| `docs/changelog/` (en, es) | history | what was done and measured in each version, as it was at the time (not updated) |
| root | everyone | `README.md`, `TODO.md`, `CHANGELOG.md`, `THIRD_PARTY_NOTICES.md`, `CLAUDE.md` (instructions for the AI) |
