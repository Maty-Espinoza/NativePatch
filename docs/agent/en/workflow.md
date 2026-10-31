# Workflow (read at the start of every conversation)

How work is done in NativePatch with Claude: where everything is, how to update and push the repo, how to ask for tests
and save results, and the user's rules. Building: `environment.md`. Sending to the Vita and bringing back logs: `relay.md`.
Cleanup rules: `good-practices.md`. How the plugins work: `../../dev/en/how-it-works.md`.

## 1. At the start of a conversation
1. Read `good-practices.md` and this document. Then the latest `claude/resumen_conversacion_*.md` of the claude.ai
   project: it says where everything stands, what is on the Vita and what comes next. Then whatever that summary points
   to and `../../../TODO.md`.
2. See which computer is linked: `get_device_info` (`platform`: `win32` = Windows PC, `darwin` = Mac). If none is
   linked, the `mcp__remote-devices__*` tools do not appear: ask the user to link the computer from the desktop app.
3. Request access to the relay folder (once per conversation, with `device_request_folder_access`): `~/Downloads/vitabridge`.
   It is the only folder on the user's computer that is needed for the tests. If the task touches the user's repo
   (see 4), also `~/Documents/GitHub`.
4. **Claude's working repo is a clone in its container**: `add_repo` (`push` access) and `git clone --branch dev
   https://github.com/Maty-Espinoza/nativepatch /home/claude/nativepatch`. That is where it is edited, built and pushed.
   The repos on the Mac (`~/Documents/GitHub/NativePatch`) and on the Windows PC (`C:\Users\spect\Documents\GitHub\NativePatch`)
   belong to the user: they may be behind and there is no need to touch them unless he asks; they are updated with
   `git pull --ff-only` (or by git bundle if his computer's VM cannot reach GitHub, see §2).

| What | Windows (`desktop-19r1h9e`, user `spect`) | Mac |
|---|---|---|
| User's repo (`dev` branch) | `C:\Users\spect\Documents\GitHub\NativePatch` | `~/Documents/GitHub/NativePatch` |
| In Claude's VM (`device_bash`), if access is requested | `$HOME/mnt/GitHub/NativePatch` | `$HOME/mnt/GitHub/NativePatch` |
| Relay (opened by the user) | `Descargas\vitabridge.cmd` (double click) | `python3 ~/Documents/GitHub/NativePatch/tools/vitabridge.py` in the Terminal |
| Relay folder | `Descargas\vitabridge\` | `~/Downloads/vitabridge/` (in the VM: `$HOME/mnt/vitabridge`) |
| Build by hand (user) | `NativePatch\tools\windows\compilar.cmd` (WSL; the first time `instalar.cmd`) | `sh tools/compilar.sh` |

Remote: `origin` = https://github.com/Maty-Espinoza/NativePatch (public). Branches: `dev` (work) and `master` (the
latest confirmed). The tag `gepatch-dev66` is the last state with the old names.

## 2. Pull and push
- **Push:** from Claude's container, with the `add_repo` clone (push access): `git push origin dev` (`--tags` if
  there are new tags). The user does not mind that the commits are under Claude's name; he cares that they are
  pushed so he can continue in another conversation or on another computer. If it cannot be done, say so: he can push
  from his own Git (Windows: Git for Windows; Mac: GitHub Desktop).
- **The user's repo is behind:** `git pull --ff-only` in his computer's VM. The VM sometimes **cannot reach GitHub**
  (the proxy answers 403): then it is done by git bundle: in the container `git bundle create dev.bundle <commit the
  user has>..dev`, take it to `~/Downloads/vitabridge/` (`device_commit_files`) and in the VM
  `git fetch $HOME/mnt/vitabridge/dev.bundle dev:refs/remotes/origin/dev && git merge --ff-only origin/dev`.
  Without delete permission on the repo folder, git leaves `.lock` files that block it (`device_request_delete_permission`).
- **User's uncommitted changes:** before touching his repo, `git status -sb`; if there are changes, ask.
- **While testing:** every test version sent to the Vita is pushed as a commit to `dev` marked "(test)"
  with its full number (user's decision, 2026-10-30). What matters is that each commit says which version it brings.

## 3. Build and send to the Vita
- Build: `environment.md` (in the container: `T=/home/claude/tc sh tools/preparar.sh` once per session and
  `T=/home/claude/tc sh tools/compilar.sh <test version>`).
- Send: `relay.md`. In short: binaries go to `vitabridge/enviar/ux0/...`, NativeDraw needs Adrenaline to be closed
  (`cmd_*.txt` with `destroy`), and the md5 of what reached the Vita is verified before asking for the test.
- **After every change it is built and sent to the Vita** (user's rule). Tests are only on the Vita: there is no
  PC lab since NativeLink 0.0.2 (NativeLink does nothing visible without the Vita). The capturer test with
  qemu (`environment.md`) is for the capture logic, not for graphics.

## 4. Asking the user for tests
- Say **which game, what to do and what to look at**, and at the end a summary of which game goes with which option. The
  options, with the name they have in the plugin's menu (when it exists) or, in the meantime, which file was uploaded to
  `ux0:data/nativedraw/`.
- Games: Gran Turismo (UCUS-98632; the race and the bridge are the heaviest; the user always does the same race
  with the same car), Silent Hill Shattered Memories (ULUS-10450; outdoors), Ridge Racer (ULJS-00001; the heaviest is
  the curve before the crowd, once per lap), LocoRoco (NPEG-00024), Retro City Rampage DX (NPUH-10199), KOF 96
  (NPUH-10119, NeoGeo Station), Metal Slug XX (ULUS-10495). L when opening the game = NativeLink off.
- The user has no fps counter: fps come from the logs. If he does not mention the audio, it was fine.
- After every change, explain to him **what was done and how it was implemented**, in simple terms (what is being
  attempted, what each component does), without unnecessary technical details: he wants to learn in order to give better
  orders and ideas.

## 5. Cause first (user's rule, 2026-10-30)
**A change is never made if the origin of the problem is not clear.** When what a test left (logs, dumps, what the user
said) is not enough to explain it:
1. **Say what information was missing** to know the cause.
2. **See whether it can be obtained.** If it can, it is added first (a log line, a counter, a dump with more data)
   and tested again, without fixing anything yet.
3. **If it cannot be done that way, research how**: documentation and reference code (Adrenaline, PPSSPP, vitasdk) and, if
   needed, small programs that run the test (on the PC with qemu, like `nativedraw/prueba_memoria.c`, or a bench on the
   Vita).
4. **Only with the origin clear is the change made**, and the next test confirms it was that.
A reasonable suspicion is not enough: it is stated as a suspicion and confirmed with data before changing the code.

## 6. Saving and reading results
- At the end of each run, **before the user opens another game** (logs get replaced): wait for the relay to download
  them (15–30 s after closing the game) and copy from `recibidos/` `nativelink.log`, `nativelink_prev.log`,
  `nativedraw_captura.log`, `nativedraw_captura_prev.log`, `nativedraw.log` (with extreme log level, also
  `nativedraw_frames.log` and `nativedraw_tarde.log`) to `vitabridge/etapa<N>_<version>/<game><n>/` (e.g.
  `etapa3_0.0.8.22/metalslug1`). The relay deletes what is in `recibidos/` after 7 days: what was not copied is lost.
- **Verify the version in the logs before analyzing:** `head -1 nativelink.log`, the first line of
  `nativedraw_captura.log` and the last "NativeDraw 0.0.x ... compilado" line of `nativedraw.log` (this file is
  appended to, not replaced: the analysis starts at that line). The "prev" of the capturer log only rotates when opening
  Adrenaline: if the user changes game without closing Adrenaline, both end up in the same log.
- Summary without reading the whole logs: `python3 tools/resumen_corrida.py <folder>`. With extreme log level:
  `python3 tools/analisis_fase1.py <folder> [<folder> ...]`. The log lines: `../../dev/en/logs-and-switches.md`.
- Visual errors: dumps (`../../dev/en/dumps.md`) and `tools/volcado.py` to draw the frame on the PC and find out whether
  the error is in the data or in the Vita's drawing. **Videos only when necessary** (Vitastream + OBS): logs and dumps
  first.
- The logs can be read from the computer's VM (`device_bash`, grep/python3) or brought to the container
  (`device_stage_files`).

## 7. When finishing the task
1. Build, send to the Vita and leave the Vita ready (close Adrenaline with `destroy` if NativeDraw changed).
2. Update the affected repo documents, **in English and in Spanish**: `docs/dev/`, `docs/agent/` (including
   `stage3-plan.md` if anything about the stage changed), `TODO.md` (remove what is done, add what is new), `CHANGELOG.md`
   (when confirming a version) and `versions.md` (history row).
3. Commit with the version and push to `dev` (§2).
4. Write the conversation summary in the claude.ai project (`claude/resumen_conversacion_<date>_<stage>.md`):
   status (commits, what is on the Vita, switches), what was done, what comes next, pending items.
5. Memory: note the user's new decisions (not technical details: those go to the documents).

## 8. User's rules
- Versions (`../../dev/en/versions.md`): a change in one plugin = bumps its PATCH; a change that needs both = bumps the
  MINOR of both. Tests with a fourth number and a "(test)" commit. Every bug fixed = one commit with its version.
  No versions should be lost: the confirmed commit carries the one that follows the last one confirmed on GitHub.
- After every change: build and send to the Vita. Do not run processes that spend many tokens or make changes to the
  test environment without asking; do not take actions he did not ask for.
- Warn before touching Windows environment variables. Do not change the original shortcuts (L when opening = off,
  L+R+START menu, L+R+SELECT dump).
- Experiments with the Vita's GPU: carefully (a stage 1 experiment shut down the whole Vita). If something fails,
  the user closes the game.
- Caches "everywhere": avoid redoing what did not change; avoid full copies and full draws whenever possible.
- Compact but understandable code; comments according to `good-practices.md`. Answer in Spanish.
