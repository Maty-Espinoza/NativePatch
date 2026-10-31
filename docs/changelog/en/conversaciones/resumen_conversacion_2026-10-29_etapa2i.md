# Summary to continue in another conversation: stage 2, phase 2, item 1 (2026-10-29, ninth conversation)

Read: `flujo_de_trabajo.md` → this summary → `etapa2_fase2_compuerta.md` (what was done) → `etapa2_fase1_impacto.md`
(phase 2 proposal) → `registros.md` → `TODO.md`.

## Where we left off
- **Commit `768af4b` on `dev`, pushed to GitHub: GePatch 0.2-dev66 + gepresent v13.10** (gate before enqueuing the next
  list + `docs/etapa2_fase2_compuerta.md`, logs, workflow and `TODO.md` updated). Push from the
  container (`add_repo` with push, from the clone `/home/claude/maty-espinoza/nativepatch`), under Claude's name.
- The Windows repo ended up identical to GitHub (bundle `vitabridge/dev66.bundle`). The Mac repo is behind (3 commits):
  pull before working there.
- Vita: tested with dev66.3 + v13.10.3 (same code). The final dev66 version (`16755a0c…`) + v13.10 (`5f08b9d1…`)
  is waiting in `vitabridge/enviar/`: the relay lost the connection to the Vita ("timed out") at the end. When the relay is
  restarted it uploads on its own; verify it with a `bajar_verif.txt` to `etapa2_dev66/verif/`.
- Switches on the Vita: `compuerta_sync`, `sin_compuerta`, `banco_nucleos` = "no". Medium log level (no file).
- Logs: `vitabridge/etapa2_dev66.1/gt1`, `etapa2_dev66.2/gt1`, `etapa2_dev66.3/gt1` and `etapa2_dev66.3/sh1`.

## What was done
- The game's wait for the Vita moved from the sync to "before enqueuing the next list", with a self-check: gepresent
  leaves hashes of the list (at all levels, with its sequence number in `SB[0x53]`) and GePatch checks them before
  enqueuing; if they changed it goes back to the sync (60 clean checks to return; 10 changes = until the game is closed).
- dev66.1 and dev66.2 gave false alarms: gepresent marked the list as captured before leaving its sample, and GePatch
  checked the previous list's sample (a zone that GT already reuses). Fixed in dev66.3 (sample before `SB[3]`, and only
  the sample of a list later than the previous sync is accepted): 0 changes in GT and in SH.
- GT: ~2,150 → 690 waits in the race; middle of the lap ~1,000 → ~45, 59.8–59.9 fps. Start-up and finish still
  wait (the Vita needs more than 16.7 ms there). End of the lap 54.7 fps (versus 56.0): a single run, keep an eye on it.

## Next (phase 2, items 2–5 of `etapa2_fase1_impacto.md`)
2. Capturer: read directly in 256 B chunks instead of the shared cache (~0.8 ms).
3. Apply sublists without adding up all the statistics (~1.5 ms).
4. Check sublists more cheaply or by look-ahead.
5. Capturer core / priority (careful with the audio).
Also: try the new gate in Ridge Racer and LocoRoco; `resumen_corrida.py` does not yet show the gate lines
(use `grep "stats compuerta" ge_patch.log`).

## Lessons
- A safety check can fail because of the order of the notices between the PSP and the Vita: if the alarms appear only in
  the frames with a wait, suspect the check before the game.
- To compare stretches of the race, `grep "stats impacto" ge_patch.log` helps (wait per frame and waits every 10 s).
- The container's stop hook asks for a commit with unpushed changes: no commit is made for test versions (the user's
  rule); this is explained and the confirmation is awaited.

## Working rules in force
- Test versions `.x` (next: GePatch dev67.x / gepresent v13.11.x); commit only with the confirmed one.
- Tests: which game, what to do, what to look at; options with the menu name; simple summary after each change.
- Log level: medium during development, extreme for analysis, minimum for publishing.
