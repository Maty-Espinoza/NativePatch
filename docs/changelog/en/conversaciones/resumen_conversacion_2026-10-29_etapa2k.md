# Summary to continue in another conversation: close of stage 2 (2026-10-29, eleventh conversation)

Read: `flujo_de_trabajo.md` → this summary → `TODO.md` (section "Preparación para la etapa 3" first) →
`etapa2_ridge_racer.md` (the last thing done) → `etapa2_fase2_adelanto.md`.

## Where we left off
- **Stage 2 of architecture B closed** (the user's decision: ≥ 58 fps in the heaviest part of GT is enough for now).
- Commits on `dev`, pushed to GitHub: `e463aff` **gepresent v13.12** (close of stage 2), `0afb581` **relay**
  (logs only when the game is closed) and `795d461` **gepresent v13.13** (Ridge Racer at 60 fps). GePatch is still at
  **0.2-dev66**. Push from the container clone `/home/claude/maty-espinoza/nativepatch` (`add_repo` with push).
- Windows repo identical to GitHub (by bundle: today the Windows VM did **not** reach GitHub, 403). The Mac repo is
  behind (7 commits): pull before working there.
- Vita: GePatch dev66 + gepresent v13.13 (`8c6adb31…`, verified), with `ux0:data/gepresent/nucleo_captura` = "libre"
  (same as no file). Medium log level. New relay in `Descargas\vitabridge.py`.
- Logs: `vitabridge/etapa2_v13.12.1/gt1`, `etapa2_v13.12.2/gt1`, `etapa2_v13.12/locoroco1`, `etapa2_v13.12/rr1`,
  `etapa2_v13.13.1/rr1`, `etapa2_v13.13.2/rr1`, `etapa2_v13.13.2/gt1`.

## What was done
- **Stage 3 list in the TODO** (user's request): GePatch → NativeLink (PSP side: capture), gepresent →
  NativeDraw (Vita side: draws with the GPU); new README (project made with AI, in progress, no longer a fork); decide the
  installation (two plugins / installer / updater); versions 0.0.x until the first release NativeLink 0.1 +
  NativeDraw 0.1; remove what was inherited from GePatch and is not used; replace compiled environments with scripts. **None of
  that was started.** The user added the summary to the project instructions.
- **DISTINTAS fixed (v13.12):** the VRAM sublists were compared against the mirror they were recorded in (in the
  frames of the other mirror, a copy one frame behind). Now the VRAM address is saved and translated to the frame's
  mirror. 0 DISTINTAS in ~100,000 checks (before 1–7 per run). The qemu test reproduces the error (vram 3).
- **Look-ahead check (v13.12):** the readers check, when the list starts, the stable sublists
  applied in the previous one. v13.12.1 (in order) came out worse: they kept pace with the capturer and compared the same things.
  v13.12.2 (from the end) was kept: GT in the heaviest part 57.3 → 58.2 fps, 17.4 → 16.8 ms of Vita per frame.
- **What limits it** is reading the PSP's memory (~2 µs per line): the capturer still compares ~5.5 ms per frame. Reading
  more cheaply (cached view or DMA) was left in `TODO.md` for the future, by the user's decision.

## After the close: other games
- **Relay (commit `0afb581`):** twice the Vita stopped opening programs after "550 Could not allocate memory"
  from vitacompanion's FTP, always after ~600 downloads. The relay listed every 1 s and downloaded the whole logs each time
  they grew. Now it looks every 15 s and downloads a log only when it has stopped growing (when the game or Adrenaline is closed): one
  RR session made 7 transfers and no errors. After closing a game you have to wait 15–30 s before the
  next one.
- **LocoRoco** (v13.12): 59.9 fps, 0 DISTINTAS; a sublist changes before enqueuing → gate at the sync (correct).
- **Ridge Racer** dropped to ~30 fps in the curve before the crowd zone (once per lap: ~20 thousand commands and ~3,700
  draws per frame; the PSP with ~55% free). RR does not repeat sublists (everything is interpreted, ~1 µs per command) and the capturer
  had ~65% of its core (the audio uses the rest). v13.13.1: interpret one 64-byte line at a time (~800 ns per command,
  that stretch at ~50 fps). v13.13.2: capturer on any core (~100%): **RR at 60 fps the whole race**; GT also tested
  (58.2 → 60.2 in the heaviest part), with no audio cuts. Committed as **v13.13** with the core free by default.

## Next
- The user can keep **testing their other installed games** with v13.13 (Silent Hill and LocoRoco with the
  free core are missing).
- After that: the preparation for stage 3 (`TODO.md`), and then stage 3 (minimal renderer).

## Lessons
- Look at the code the compiler generates before optimizing blindly: `walkList` inlined into `walk` ran
  out of registers (it recalculated `&q[...]` and used the stack on every command).
- The capturer pinned to the audio core lost ~35% in RR; with affinity to the 3 cores the system moves it on its own and
  the audio did not suffer (RR and GT).
- Splitting work ahead of time between threads does not help if they go in the same order and are no faster than the one waiting:
  make them start from the other end.
- For the qemu test in the container: `-I` to a folder with only the `psp2*` and `vitasdk*` headers from
  vitasdk (with the whole `include` the libc clashes).
- If the Vita answers "550 Could not allocate memory" and then "timed out", it is hung or low on memory: the user
  restarted it and the relay uploaded on its own.
- The delete permission on `Documents\GitHub` can be lost in the middle of a conversation: ask for it again if git fails with
  "Operation not permitted".

## Working rules in force
- Test versions `.x` (next: gepresent v13.14.x; GePatch dev67.x if it changes); commit only with the confirmed one.
  With stage 3 it changes to NativeLink/NativeDraw 0.0.x (to be defined in the preparation).
- Tests: which game, what to do, what to look at; options with the menu name; simple summary after each change.
- Log level: medium during development, extreme for analysis, minimum for publishing.
