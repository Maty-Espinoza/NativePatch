# Summary to continue in another conversation: stage 2, recording on several cores (2026-10-29, seventh conversation)

Read: **`docs/FLUJO_DE_TRABAJO.md`** (new: how we work) → this summary → `etapa2_nucleos.md` → `TODO.md` →
`arquitectura_dibujo_en_vita.md` (§5, §6).

## Where we left off
- **Commit `2b105cb` on `dev`, pushed to GitHub:** gepresent **v13.7** (core test bench) + `docs/etapa2_nucleos.md`
  + `docs/FLUJO_DE_TRABAJO.md` + `tools/contenedor/` (build in Claude's container) + `tools/resumen_corrida.py`
  + dev64 confirmation in `etapa2_espejo_doble.md` + architecture §5. GePatch is still at **0.2-dev64**. Before: `5c4d435`.
- Work was done **from Windows**. The Windows repo is up to date (`dev...origin/dev` with no differences). **The Mac
  repo is behind**: pull there before working (`FLUJO_DE_TRABAJO.md` §2).
- On the Vita: GePatch dev64 (`32742693…`) and gepresent v13.7 (`4a73f210…`), verified in
  `vitabridge/etapa2_v13.7/verif`. `ux0:data/gepresent/banco_nucleos` = "no" (test bench off). Shadow mode by default.
  v13.7 = v13.7.1 with a different number (it was not run separately).
- Logs (Windows, `Descargas\vitabridge`): `etapa2_dev64/gt1`, `etapa2_dev64/sh1` (dev64 + v13.6 confirmation),
  `etapa2_v13.7.1/gt1` (core test bench with GT, includes `ge_patch_banco.log`).

## Result
- **dev64 + v13.6 confirmation:** GT 59.7–60 fps except at the start of a race (55–58) and a heavy stretch (48–56: ~80,000
  commands, 16,500 draws, capture 17–19 ms per frame). SH: the game never waits for the Vita (fps = those of the original).
- **Core test bench (9000 T draws with GT in a race):** 1 free thread ~32 ms; core 0 ~28 ms; core 1 ~170 ms
  (capture); core 2 ~59 ms (sound); 2 or 3 threads with no fixed core **~26 ms**. Recording is spread out without blocking, but
  ~1.5 cores stay free. GPU ~16 ms per 9000 draws. **GT does not fit when recording draw by draw** → reuse recorded lists of
  stable sublists (85% of GT's commands) and merge draws, earlier than planned (stage 6).
- The test bench did not stall the game (60 frames in each round); the hitches every ~15 s were Adrenaline's image waiting
  for the GPU during each round.
- New: the last ~7 s of the GT lap at 40–48 fps (RECTANGLES + sublists with a different entry; `TODO.md`).
- DISTINTAS: 2 / 175,172 (dev64), 13 / 134,312 (v13.7.1).

## Lessons (do not repeat)
- Claude's VM on Windows **cannot reach GitHub** (proxy 403): pull and push through git bundles (`FLUJO_DE_TRABAJO.md`).
  The push is done from the container (`add_repo` with `access: "push"`).
- The container's proxy does not allow viewing github.com pages (release listings): vitasdk is downloaded by fixed name
  (`vitasdk-bootstrap-x86_64-linux-gnu.tar.bz2` from the latest `sdk-snapshot-*`, with `git ls-remote`).
- A relay that gets cut off during a download leaves a `.parcial` file (it happened with the first verification): repeat the request.
- Logs are replaced when another game is opened: save them at the end of each run.

## What remains in stage 2 (agreed order)
1. ~~Stable mirror~~, ~~recording on 2–3 cores~~ (done: not enough for GT draw by draw).
2. T2 (black box) at the end. Ridge Racer was not tested in stage 2.
3. Before drawing (stage 3): the DISTINTAS, register MIRROR2 for the GPU, repeat the TRANSFERs to VRAM on the Vita.
4. **Decide with the user** whether to bring forward the "recorded sublist cache" / merging draws (needed for GT) or
   continue with stage 3 on light games (Retro City Rampage, menus, SH) and leave GT for later.

## Working rules in force (from the user)
- Test versions `.x` (next: GePatch dev65.x / gepresent v13.8.x); commit only with the confirmed version.
- When asking for tests: which game, what to do and what to look at; options with the menu name. Simple summary after
  each change. Logs of each run in `vitabridge/<stage>_<version>/<game><n>/`.
