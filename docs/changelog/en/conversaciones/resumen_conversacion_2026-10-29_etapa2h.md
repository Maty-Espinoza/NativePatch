# Summary to continue in another conversation: stage 2, analysis phase 1 (2026-10-29, eighth conversation)

Read: `flujo_de_trabajo.md` → this summary → `etapa2_fase1_impacto.md` (results and phase 2 proposal) →
`registros.md` → `TODO.md`.

## Where we left off
- **Commit `6193d07` on `dev`, pushed to GitHub: GePatch 0.2-dev65 + gepresent v13.9** (analysis phase 1 + log
  levels + `docs/etapa2_fase1_impacto.md`, `docs/registros.md`, `tools/analisis_fase1.py`, updated
  workflow). The push was done from the container (`add_repo` with push, with the user's permission): the commit is under
  Claude's name; the user does not care about authorship, only that the changes end up upstream.
- The Windows repo ended up identical to GitHub (`dev...origin/dev` with no differences). The Mac repo is behind
  (2 commits): pull before working there.
- Vita: GePatch dev65 (`744ab734…`) + gepresent v13.9 (`9d3439d3…`), verified (`vitabridge/etapa2_dev65/verif`).
  Medium log level (no `registro` file). `ux0:data/gepresent/sin_compuerta` = "no", `banco_nucleos` = "no".
- Phase 1 logs: `Descargas\vitabridge\fase1\gt1 … gt6_confirmacion`.

## Plan agreed with the user
1. **Phase 1 (done): measure** why shadow mode still costs the PSP, regardless of the cost of measuring.
2. **Phase 2 (next): improve by iterations** with that data and try different implementations.

## Phase 1 results (details in `etapa2_fase1_impacto.md`)
- All of the cost to the game is **the wait for the Vita at the sync**: without it (`sin_compuerta`) GT runs at 59.9 fps and 0.2%
  slow frames; with it ~58 fps and 16–20%. GePatch notices (6 µs), mirror (0.07 ms of CPU), Vita reads:
  not noticeable. Free PSP CPU ~20%.
- GT enqueues **one list per frame** (50–80 thousand instructions), complete, ~14.3 ms before the sync. The Vita starts at
  ~0.3 ms and takes 12–18 ms (pure work). ~1/3 of the frames arrive late (median 1.5 ms; 12% at the 5 ms cap).
- **The list does not change until the game enqueues the next one** (0 of 11,436); GT alternates between two list zones. Moving
  the wait there: the Vita behind in 15% of frames instead of 31%.
- Vita time (45–60 thousand commands): 14.6 ms of wall clock, 12.2 of processor (loses ~17% of core 1); checking and
  applying sublists 7.2 ms (applying ~7 µs each because it adds up ~200 counters); own 256 B read 3.0 µs, shared
  cache line 1.13 µs (more expensive per line than reading directly).
- Before phase 1, v13.8.1/v13.8.2 were tested (do not re-read lines already checked): it read ~45% less but did not lower the time
  and the DISTINTAS went up → discarded (no commit; `vitabridge/gepresent_v13.8.2.patch`).

## Proposal for phase 2 (order: gain / cost)
1. Move the gate to "before enqueuing the next list" (with a self-check for other games).
2. Capturer: read directly in 256 B chunks instead of the shared cache; the readers only check sublists.
3. Apply sublists without adding up all the statistics.
4. Check sublists more cheaply / look-ahead.
5. Capturer core / priority (careful with the audio).
6. DISTINTAS of the sublists in VRAM: the memory compares against the mirror from when the sublist was saved, not against the
   current frame's (hypothesis; all the DISTINTAS are 0x041xxxxx). Fix before drawing.

## Lessons
- Measuring changes what is measured: reading the time costs 390 ns on the Vita; the full extreme log raised the slow frames
  from ~17% to 27%. Hence the log levels and the A/B comparisons with switches.
- Reading fewer bytes does not speed things up if the cost is not in the reading: measure before optimizing (v13.8).
- The user always does the same race and car; has no fps counter (the log gives them). If they do not mention the audio it is
  because it was fine.
- The relay can lose the connection to the Vita ("timed out"): the user restarts it; whatever is pending in `enviar/` uploads on its own.
- In the container, the classifier may block `preparar.sh` (downloads) and the push the first time: with the user's
  explicit permission they worked. Push: commit with `user.name=Claude`, `user.email=noreply@anthropic.com` and
  `add_repo` with push; then bring the commit to the PC repo with a bundle (`git reset --hard origin/dev` if the
  content is the same).

## Working rules in force
- Test versions `.x` (next: GePatch dev66.x / gepresent v13.10.x or v14.0.x); commit only with the confirmed one.
- Tests: which game, what to do, what to look at; options with the menu name; simple summary after each change.
- Log level: medium during development, extreme for analysis, minimum for publishing.
