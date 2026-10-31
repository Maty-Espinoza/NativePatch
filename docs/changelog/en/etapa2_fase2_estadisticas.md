# Stage 2, phase 2 (2 and 3): direct reading by the capturer (discarded) and compact statistics (gepresent v13.11, 2026-10-29)

**Commit:** gepresent **v13.11** (= v13.11.2 with the final number), with GePatch **0.2-dev66** unchanged.

Follows `etapa2_fase2_compuerta.md` (point 1). Points 2 and 3 of the proposal in `etapa2_fase1_impacto.md`.
Tags: [measured], [deduced].

## In simple terms
- **Point 2 (discarded):** have the Vita's capturer read the PSP's memory itself, in chunks of 256 bytes, instead of
  taking the lines the readers leave for it in the shared cache. It turned out worse: in GT almost 3 times more waits by
  the game in the race. The capturer jumps around a lot between areas and gets little out of each read; the readers, on
  the other hand, do those waits in parallel on other cores. **The shared cache stays.**
- **Point 3 (done):** each stored sublist added the capturer's 127 statistics when applied, almost all of them 0. Now it
  stores only the ones that are not 0 and adds only those. In the heaviest stretches of GT the Vita takes ~2 ms less per
  frame (19.1 → 17.0 ms with more than 75 000 commands) and the game goes from 54.3 to 57.3 fps there. The logs stay the
  same.

## Point 2: v13.11.1, the capturer reads directly (discarded)
- Change: in `rdc`, the capturer (`R0`) did not use `clGet`/`clPut`; the readers kept using the cache among themselves.
  Switch `cache_compartida` to go back to the previous behavior (it did not stay in the code).
- Phase 1 assumption: shared cache line 1.13 µs against 0.75 µs per line reading 256 B (3.0 µs).
- [measured] In GT: the capturer's own reads ~500 → ~1 500–1 700 per frame to replace ~2 600 cache lines → it uses ~2.3
  of the 4 lines of each read (~1.3 µs per useful line, more than the cache).
- Result (`etapa2_v13.11.1/gt2`, medium log):

| Commands per frame | Vita per frame dev66.3 | v13.11.1 | fps dev66.3 | v13.11.1 |
|---|---|---|---|---|
| 30–45 thousand | 10.3 ms | 11.6 | 60.0 | 60.0 |
| 45–60 thousand | 12.4 ms | 14.5 | 60.1 | 59.9 |
| 60–75 thousand | 15.6 ms | 17.8 | 59.2 | 56.5 |
| more than 75 thousand | 19.1 ms | 19.5 | 54.3 | 52.5 |
- Game waits for the Vita in the race: ~690 → ~2 040.
- **The Vita hung** in the first run with v13.11.1 (`etapa2_v13.11.1/gt1`): the logs stop ~10 s after the start, with no
  errors, and vitacompanion stopped responding until the user restarted it. The second run with the same version
  finished fine: it is not known whether the version was the cause. Watch for it happening again.
- If the shared cache is to be made cheaper later: take the line out with NEON and fewer barriers (`clGet` does two
  `__sync_synchronize`). Uncertain gain (tenths of a ms).

## Point 3: compact statistics in the sublist memory (v13.11)
- Before: `Memo.d[SW]` (127 words, 508 B per sublist) with the difference of all the statistics; apply = add the 127 and
  read that memory (in GT ~270 sublists per frame, ~650 KB of entries: almost all outside the cache). [deduced from
  phase 1: ~7 µs per applied sublist]
- Now (`recEnd`): `Memo.cmds`, `Memo.draws` and up to `SN` = 48 pairs (index `si[]`, value `sv[]`) of the statistics that
  are not 0. If a sublist adds more than 48, it is not stored (it stays as `sin_guardar`) and the log reports it once
  ("memoria: sublista ... suma mas de 48 estadisticas"). In GT it never happened.
- Apply (`memoTry`): adds only those pairs. At the **minimum** level only `cmds` and `draws` (what appears in its line).
- Result (`etapa2_v13.11.2/gt1`, medium log, the same race; same calculation as the table above):

| Commands per frame | Vita per frame dev66.3 | v13.11.2 | ns per command | fps dev66.3 | v13.11.2 |
|---|---|---|---|---|---|
| 30–45 thousand | 10.3 ms | 10.3 | 256 → 253 | 60.0 | 60.0 |
| 45–60 thousand | 12.4 ms | 12.1 | 241 → 234 | 60.1 | 60.0 |
| 60–75 thousand | 15.6 ms | **14.9** | 228 → 218 | 59.2 | **60.0** |
| more than 75 thousand | 19.1 ms | **17.0** | 239 → 214 | 54.3 | **57.3** |
- Game waits (every 10 s, from the race start): dev66.3 63 / 229 / 51 / 5 / 9 / 9 / 8 / 9 / 2 / 5 / 300;
  v13.11.2 12 / 188 / 68 / 9 / 6 / 9 / 5 / 2 / 12 / 7 / 29 / 413. The end of the lap still waits: there the Vita needs
  more than 16.7 ms (17 ms median with more than 75 thousand commands).
- Sublist memory: 3 DISTINTAS in 51 121 checked, all sublists in the VRAM (the known issue in `TODO.md`; the change does
  not touch the GE state, only the counters).
- Image and audio: no changes according to the user.

## How to measure this (without the extreme log)
With the `ge_patch_captura.log` of each run, average per command range (the `seg:` lines with more than 30 000):
`grep "seg: frames" ge_patch_captura.log | awk '{for(i=1;i<=NF;i++){split($i,a,"=");if(a[1]=="comandos/f")c=a[2];
if(a[1]=="us/f")u=a[2]}} c>=30000{b=int(c/15000);if(b>5)b=5;N[b]++;U[b]+=u} END{for(b=2;b<=5;b++)print b*15"k",U[b]/N[b]}'`

## Next (rest of the phase 2 proposal)
4. Check sublists more cheaply or ahead of time (today ~5–6 ms per frame in the race: `comparar_us`).
5. Capturer core / priority (it loses ~17%), taking care of the audio.
6. DISTINTAS counts of the sublists in the VRAM; `sin_captura` run for the end of the lap.

## Versions
| Version | What | gepresent md5 |
|---|---|---|
| v13.11.1 | the capturer reads directly (point 2) — discarded | `16cc1ca4…` |
| v13.11.2 | compact statistics (point 3) | `847f4a36…` |
| **v13.11 (commit)** | = v13.11.2 with the final number | `2149515e…` |
Patches in `vitabridge/gepresent_v13.11.1.patch` and `gepresent_v13.11.2.patch`; logs in `vitabridge/etapa2_v13.11.1/`
(`gt1` = the hang, `gt2`) and `etapa2_v13.11.2/gt1`.
