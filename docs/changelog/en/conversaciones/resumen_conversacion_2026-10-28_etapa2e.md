# Summary to continue in another conversation: stage 2, T4/T6 and data lifetime (2026-10-28, fifth conversation)

This conversation took two steps: "compare less" (gepresent v13.4, see `resumen_conversacion_2026-10-28_etapa2d.md`
and `etapa2_comparar_menos.md`) and T4/T6 + data lifetime (this summary). Read: this summary →
`etapa2_t4_t6_vida_datos.md` → `TODO.md` → `arquitectura_dibujo_en_vita.md` (§3.6 data coherence, §6 stages).

## Where we left off
- **Commit `a619d80` on `dev`** (Windows repo `Documents\GitHub\NativePatch`, **no push**): GePatch **0.2-dev63** +
  gepresent **v13.5** + `TODO.md` (new, at the repo root) + `docs/etapa2_t4_t6_vida_datos.md`. Before: `1dac8e9`
  (gepresent v13.4).
- On the Vita: GePatch dev63 (`23589485…`), gepresent v13.5 (`5967a226…`), both verified by downloading them. Shadow
  mode by default. T6 (thread that watches the GE) off: it is turned on by creating `ux0:data/gepresent/medir_ge`.
- Binaries: `vitabridge/respaldo/ge_patch_dev63{.1,.2,.3,}.prx`, `vitabridge/gepresent/gepresent_v13.5{.1,.2,.3,}.suprx`;
  patches `vitabridge/gepatch_dev63*_gepresent_v13.5*.patch`. Logs: `vitabridge/etapa2_dev63.1/gt1`, `etapa2_dev63.2/gt1`,
  `etapa2_dev63.2/sh1`, `etapa2_dev63.3/gt1`.

## Result (the data question)
- GT and SH: vertices, indices and palettes **do not change during the following frame**; they are rewritten 2 frames
  later (1–2 % in game, 13 % in GT menus; 9.5 % of SH's indices outdoors). Textures almost never, except ~1.5 % of SH
  outdoors that change within the same frame (flashlight? user's hint).
- GT empties the whole cache ~3 times per frame: the CPU notifications do not say which zone changed.
- T6: GT ~11.4 ms of GE per frame (margin ~5 ms); SH with the GE always busy (~20–35 ms per frame).
- **Rule for stage 3:** draw frame N during frame N+1 (finish before flip N+2); if it falls behind, repeat the image or
  make the game wait; re-read SH's dynamic textures. There is no need to copy vertices.
- GT fps with dev63.3/v13.5.3 the same as v13.4 (start 59.8, bridge 55–60). 0 DISTINTAS.

## User's guide for reading the SH logs
SH starts with a couple of cutscenes, then an indoor scene in a small room, an in-game cutscene and the outdoor scene.
The fps drop in the cutscenes; **the last ~30 seconds are the most representative** of the game.

## Lessons (do not repeat)
- A high-priority thread on the PSP that wakes every 250 µs takes 2–3 fps away from GT: telemetry that always runs has
  to be almost free; everything else, on request.
- Do not put new work on the readers' cores/priority (0xB0, cores 0 and 2): it takes time from them and lowers the fps.
- `UtilsForUser` is in `sceSystemMemoryManager`; `sceDmac` was not found (unknown module in Adrenaline 8).
- `sceGeGetReg` from a hook: with k1 = 0 (even so "GE busy when called" gives 0: pending item).
- The repo's `TODO.md` is the reference one (the project has a copy, `claude/TODO.md`).

## What is left in stage 2 (agreed order)
1. ~~Point 4 + T6~~ (done) and ~~decide how to secure the data~~ (answered above).
2. **Point 6 + stable mirror:** two alternating copies of the mirror (the Vita's GPU cannot read it while the GE
   overwrites it) and GT textures up to date (100–230 pages per sec change during a race; the sweep of 8 per frame is
   not enough).
3. GXM recording on 2–3 fixed cores (on the Vita side), before reaching GT in stage 4 (~16,500 draws per frame).
4. T2 (black box) last. Ridge Racer was not tested in stage 2.

## Working rules in force (from the user)
- Test versions `.x` (next: GePatch dev64.x / gepresent v13.6.x); commit only with the confirmed version.
- Build in the container: vita-toolchain (`etapa1_banco_gxm.md` §5) and pspdev (Linux tarball + clone of
  TheOfficialFloW/Adrenaline for `ADRENALINE=.../cef`). Copy `ge_patch.prx` to Downloads, `vitabridge\enviar` and
  `gepatch-lab\memstick\seplugins`; gepresent to `vitabridge\enviar\ux0\data\gepresent`. Verify with md5 by downloading
  them; close Adrenaline with `cmd_cerrar.txt` (`destroy`). Do not upload anything while a test is pending.
- Commit from the Windows PC (git in `Documents\GitHub`, delete permission in that folder in every session, changes as a
  patch with `git apply`).
- When asking for tests: which game, what to do and what to look at; options with the menu name. Simple summary after
  every change. Logs of each run in `vitabridge/<etapa>_<versión>/<juego><n>/`.
