# Summary to continue in another conversation: new identity, NativeLink + NativeDraw 0.0.2 (2026-10-29, twelfth)

Read: `docs/dev/FLUJO_DE_TRABAJO.md` → this summary → `TODO.md` → `docs/dev/funcionamiento.md` →
`docs/dev/arquitectura_dibujo_en_vita.md` §6 (stage 3 is next). Details of what was done:
`docs/changelog/etapa3_preparacion.md`.

## Where we left off
- **GitHub (`dev`, pushed from the container under Claude's name):** NativeLink 0.0.1 + NativeDraw 0.0.1 (rename),
  NativeLink 0.0.2, NativeDraw 0.0.2, tools, installer and documents. `master` = `dev`. Tag
  `gepatch-dev66` = last state with the old names (architecture A, dumps, lab, test benches remain there).
- **Vita:** installed with the VPK. `ux0:pspemu/seplugins/nativelink.prx` (enabled in `plugins.txt`, the only line),
  `ux0:data/nativedraw/nativedraw.suprx` (in `*PSPEMUCFW` of `ur0:tai/config.txt`), no leftovers from GePatch or from the
  tests. Backups in `ux0:data/nativepatch/respaldo/`. Medium log level, no switches (capturer core free).
- **Windows PC:** repo up to date; new relay in `Descargas\vitabridge.py` (downloads `native*.log`). Copies of the Vita from
  before the change in `vitabridge/rebrand/`. Logs: `vitabridge/etapa3_0.0.1/gt1`, `etapa3_0.0.2.1/{gt1,gt2,instalador}`.

## What was done (in simple terms)
- GePatch → **NativeLink** (PSP side: notes what the game asks of the GE) and gepresent → **NativeDraw** (Vita side: walks
  through it; in stage 3, draws it). Each in its own folder and with its own version `0.MINOR.PATCH`.
- What was inherited from GePatch was removed (scaling on the PSP, old menu, dumps, GXM test bench). NativeLink: 3,889 → ~620 lines.
- **VPK installer** (`instalador/`, `NativePatch.vpk`): install/update, uninstall, clean leftovers (temporary) and
  restart. Icon: only the glasses, no text or background; no LiveArea.
- Repo: new README (made with AI, in development), GPLv3 license, `docs/dev` (current) and `docs/changelog` (archive +
  conversation summaries, which are removed before the first release), `CHANGELOG.md`, script-based environment
  (`tools/preparar.sh`, `tools/compilar.sh` → `build/`), automatic GitHub build of everything.
- Tests: GT with 0.0.1 and with 0.0.2 the same as with v13.13 (60.3 fps in the heaviest part, 0 DISTINTAS); the installer
  cleaned and installed correctly on the real Vita.

## The user's decisions (this conversation)
- It is no longer a fork (separating it on GitHub is something they do). GPLv3.
- Versions: a change to one plugin = PATCH; a change that needs both = MINOR of both. First release separate.
- Install and update VPK (online updater: for the future); "clean leftovers" is removed before 1.0.
- No .gpd dumps, no capture to the log, no PPSSPP lab: tests only on the Vita. Old tools out of the
  repo (they remain in the tag).
- **L+R+START menu drawn by NativeDraw**, with all the options that are files today: in `TODO.md` (depends on stage 3).
- Documents: `docs/dev` for working, `docs/changelog` for archiving; conversation summaries go there for now.

## Lessons
- Adrenaline 8 enables PSP plugins in `ux0:pspemu/seplugins/plugins.txt` (`game, ms0:/seplugins/x.prx, on`), or in
  `EPIplugins.txt` if it exists; `game.txt` is no longer used. On this Vita taiHEN uses `ur0:tai/config.txt`.
- The relay also uploads to `ur0:` (`enviar/ur0/tai/config.txt`) and lists folders (`listar_*.txt`).
- Test the installer on the PC with fake file functions and real copies of the configuration files before
  touching the Vita.
- NativeLink's md5 changes on every build (`log.c` carries the time): compare against the binary that was sent.
- The `Projects` tool is not available to subagents: copying project documents is done by the main conversation.

## Next
1. Ridge Racer with 0.0.2 (optional; the capture did not change) and test "Uninstall" on the Vita.
2. Separate the fork on GitHub (user) and set `master` as the default branch.
3. **Stage 3**: minimal renderer in NativeDraw (clears, TRANSFER, 2D with textures, HD presentation), and with that the
   L+R+START menu drawn by NativeDraw. Test versions 0.0.3.x / 0.1.0.x depending on whether one or both plugins are touched.

## Working rules in force
- Test versions with a fourth number; commit only with the one confirmed on the Vita (`docs/dev/versiones.md`).
- Tests: which game, what to do, what to look at; simple summary after each change; medium log level in development.
- Push to GitHub after confirming (push from the container; commits under Claude's name).
