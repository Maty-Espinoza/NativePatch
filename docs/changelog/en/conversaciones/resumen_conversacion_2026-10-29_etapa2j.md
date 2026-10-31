# Summary to continue in another conversation: stage 2, phase 2, items 2 and 3 (2026-10-29, tenth conversation)

Read: `flujo_de_trabajo.md` → this summary → `etapa2_fase2_estadisticas.md` (what was done) → `etapa2_fase1_impacto.md`
(phase 2 proposal) → `registros.md` → `TODO.md`.

## Where we left off
- **Commit `a27fb32` on `dev`, pushed to GitHub: gepresent v13.11** (compact statistics in the sublist memory
  + `docs/etapa2_fase2_estadisticas.md`, `TODO.md`, workflow, logs and phase 1 updated). GePatch is still at
  **0.2-dev66** (no changes). Push from the container clone `/home/claude/maty-espinoza/nativepatch` (`add_repo`
  with push).
- The Windows repo ended up identical to GitHub (**the Windows VM can now reach GitHub: a direct `git pull --ff-only`
  worked**, no bundles). The Mac repo is behind (4 commits): pull before working there.
- Vita: GePatch dev66 (`16755a0c…`) + gepresent v13.11 (`2149515e…`), both verified. Medium log level.
- Logs: `vitabridge/etapa2_v13.11.1/gt1` (hang), `etapa2_v13.11.1/gt2`, `etapa2_v13.11.2/gt1`.

## What was done
- **Item 2 (v13.11.1, discarded):** the capturer read directly in 256 B chunks without the shared cache. In GT almost 3 times
  more waits (~690 → ~2,040 in the race): it makes use of ~2.3 of the 4 lines of each read and loses the readers'
  parallelism. The shared cache stays.
- **Item 3 (v13.11, done):** each saved sublist stores only the non-zero statistics (up to 48) and adds up only
  those; at minimum only commands and draws. GT, Vita per frame: 60–75 thousand commands 15.6 → 14.9 ms (59.2 → 60.0 fps); more than
  75 thousand 19.1 → 17.0 ms (54.3 → 57.3 fps). Logs the same; 3 DISTINTAS out of 51,121 (VRAM, the known ones).
- The Vita hung once (first run with v13.11.1, ~10 s after starting). The second run still finished fine.
  The user noticed the Vita "slightly affected" and was given recovery steps without losing Ensō (full shutdown
  → safe mode → rebuild database → reinstall Adrenaline → restore as a last resort; never update the
  firmware). **They left it for later.**

## Next (phase 2, from `etapa2_fase1_impacto.md`)
4. Check sublists more cheaply or by look-ahead (`comparar_us` ~5–6 ms per frame in the race).
5. Capturer core / priority (loses ~17%), being careful with the audio.
Also: what remains when applying a sublist (registers and matrices; measure with extreme `aplicar_us`); try Silent Hill,
Ridge Racer and LocoRoco with v13.11; the Vita recovery whenever the user wants.

## Lessons
- A per-line cost measured on average does not say how many lines are used from each read: measure the real pattern before
  changing where the capturer reads from.
- `device_commit_files` delivered an old build when reusing the file name: when rebuilding, send with another
  name and compare md5 on the PC before asking for the test.
- The relay can get stuck without errors (the heartbeat does not advance): restart it. Repeated "timed out" = the Vita does not
  respond (hung or asleep).
- Comparing runs by stretch of commands per frame (`seg:` lines of the capture log) works without the extreme log level
  (command in `etapa2_fase2_estadisticas.md`).

## Working rules in force
- Test versions `.x` (next: gepresent v13.12.x; GePatch dev67.x if it changes); commit only with the confirmed one.
- Tests: which game, what to do, what to look at; options with the menu name; simple summary after each change.
- Log level: medium during development, extreme for analysis, minimum for publishing.
