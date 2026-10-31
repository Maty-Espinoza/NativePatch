# Preparation for stage 3: NativeLink + NativeDraw (2026-10-29)

**Commits:** NativeLink 0.0.1 + NativeDraw 0.0.1 (rename), NativeLink 0.0.2, NativeDraw 0.0.2, tools,
installer and documents. Before: GePatch 0.2-dev66 + gepresent v13.13 (tag `gepatch-dev66`).

## In simple terms
- The project was separated from the original GePatch: the PSP-side plugin is called **NativeLink** and the Vita-side one
  **NativeDraw**, each in its own folder and with its own version (`docs/dev/versiones.md`).
- Everything that came from GePatch and was no longer used was removed (the scaling on the PSP, the old menu, the dumps, the test
  bench). What does the work (the stage 2 capture) stayed the same, and the tests confirm it.
- An **installer** (`NativePatch.vpk`) leaves both plugins installed and enabled, and for now also deletes the
  leftovers from the tests.
- The repo ended up with a new README, GPLv3 license, documents separated into current (`docs/dev`) and archive
  (`docs/changelog`), and an environment that is built with scripts.

## User decisions
- It is no longer a fork: it has to be separated on GitHub (the user does it). GPLv3 license (it allows forks and own versions,
  always as GPLv3 with their code).
- Versions `0.MINOR.PATCH` per plugin; a change to one plugin raises its PATCH, one that needs both raises the MINOR.
  The first release is marked separately.
- Installation with a VPK; updater in the future; the option to clean leftovers is removed before 1.0.
- Remove the .gpd dump, the capture to the log and the PPSSPP lab (tests become Vita-only); take the
  tools from past tests out of the repo (they stay in the tag).
- The L+R+START menu has to be drawn by NativeDraw on the Vita and from there it must be possible to change all the options that
  are files today: it stays in `TODO.md` (depends on stage 3).
- The installer icon: just the glasses (two screens joined), with no text or background, and no LiveArea images.

## Findings
- **Adrenaline 8 loads the PSP plugins from `ms0:/seplugins/plugins.txt`** (or `EPIplugins.txt` if it exists), with lines
  `game, ms0:/seplugins/x.prx, on`. `game.txt` is only converted once by recovery mode (code from isage/Adrenaline,
  `cef/core/systemctrl/src/plugin.c`). Details in `docs/dev/adrenaline_notas.md`.
- On the user's Vita, taiHEN uses `ur0:tai/config.txt` (there is no `ux0:tai/config.txt`).
- exFAT is not case-sensitive: the old plugin was stored as `GE_PATCH.PRX` and `ge_patch.prx` deletes it all the same.
- `log.c` carries the compile date and time: NativeLink's md5 changes on every build even if the code does not change.
- Leftovers from the tests that were on the Vita: `ge_patch.prx`, `ge_patch_anterior.prx`, `geptest.prx`, `kbench.prx`,
  `vpjuego.prx`, Finder `._*` files, `ux0:data/{gepresent,adrfsr,kbench,vprobe}`, `ux0:pspemu/{kbench,vprobe,
  gepvm,geptest}`, `PSP/GAME/{GEPTEST,GEPVM,KBENCH,VPROBE}`, ~100 `.gpd` dumps, `ge_patch*` logs and
  `gepatch_*.txt` options (`PSP/GAME/UPDATE` is not part of the project and is not touched).

## Tests on the Vita [measured]
| Test | Folder | Result |
|---|---|---|
| 0.0.1 (uploaded by hand: `plugins.txt` and `config.txt` edited with the relay, reboot) | `vitabridge/etapa3_0.0.1/gt1` | GT one lap: same as v13.13 (table below), 0 DISTINTAS in 51,405 |
| Installer (with the original configuration restored) | `vitabridge/etapa3_0.0.2.1/instalador` | Clean leftovers + install: `plugins.txt` with only NativeLink, `config.txt` = original with the NativeDraw line instead of the gepresent one, backups in `ux0:data/nativepatch/respaldo/`, no leftovers |
| 0.0.2 (installed with the VPK) | `vitabridge/etapa3_0.0.2.1/gt1` (menus), `gt2` (lap) | GT one lap: same as v13.13, 0 DISTINTAS in 50,638; the gate changes once during loading (as in dev66.2) |

GT (UCUS-98632), fps / Vita ms per frame according to commands per frame:

| Commands per frame | v13.13.2 | 0.0.1 | 0.0.2 |
|---|---|---|---|
| 30–45 thousand | 60.0 / 7.8 | 60.0 / 7.9 | 60.0 / 8.2 |
| 45–60 thousand | 60.0 / 10.0 | 60.0 / 10.0 | 60.0 / 10.3 |
| 60–75 thousand | 59.9 / 12.0 | 59.9 / 12.4 | 60.0 / 12.6 |
| more than 75 thousand | 60.2 / 13.4 | 60.1 / 13.5 | 60.3 / 13.6 |

Ridge Racer was not run again with 0.0.2 (the capture did not change; it stays in `TODO.md`).

## Installer tested earlier on the PC
`instalador/main.c` was compiled for the PC with fake file functions over a "card" in a folder, with real copies
of `config.txt` and `plugins.txt`: install, reinstall (does not duplicate lines), turned-off line (`off`) replaced,
`config.txt` without the `*PSPEMUCFW` section (it adds it), without `plugins.txt` (it creates it), clean and uninstall.
