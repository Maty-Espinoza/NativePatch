# Summary to continue in another conversation: stage 2, check by the readers (2026-10-28, fourth conversation)

Read in this order: this summary → `etapa2_comparar_menos.md` (design, numbers, ideas) → `etapa2_espejo_vram.md` →
`etapa2_memoria_sublistas.md` → `arquitectura_dibujo_en_vita.md` (§6 stages). The previous one:
`resumen_conversacion_2026-10-28_etapa2c.md`.

## Where we left off
- **Commit `1dac8e9` on `dev`** (Windows repo `Documents\GitHub\NativePatch`, **no push**): **gepresent v13.4**
  (`tools/gepresent/captura.c`, `gepresent.c`, `prueba_memoria.c`) + `docs/etapa2_comparar_menos.md`. GePatch unchanged
  (0.2-dev62).
- On the Vita: GePatch dev62 (`d44ba997…`), gepresent v13.4 (`dc016491…`, verified by downloading it). Shadow mode by
  default.
- Binaries: `vitabridge/gepresent/gepresent_v13.4{.1,.2,}.suprx`; patches `vitabridge/gepresent_v13.4{.1,.2,}.patch`.
  Logs: `vitabridge/etapa2_v13.4.1/gt1/`, `etapa2_v13.4.1/sh1/`, `etapa2_v13.4.2/gt1/`.
- **Result:** GT on the bridge 57–60 fps (v13.3 ~52), race start 59.4, capture ~15.5 ms per frame on the bridge
  (before 18.3); 0 DISTINTAS in ~53,000 checked. SH the same as before (26–60 depending on the scene).

## In simple terms (what each piece does)
- **gepresent (Vita), capturer:** reuses the saved sublists without walking them; if a helper has already checked them
  in this list, it does not even read them.
- **Helpers (readers, cores 0 and 2):** when they reach a saved sublist they compare its lines with what was saved and
  leave a mark; they split the sublists according to their pace and skip the ones the capturer has already passed.

## Lessons (do not repeat)
- What was expensive about "comparing" was reading the PSP RAM (~6.5 µs per 256 bytes without cache), not comparing.
- The readers have little time (~140 ms/s on core 0, ~280 on core 2): a fixed split leaves the core 0 one behind. Even
  with a split by pace, only ~40 % of the sublists arrive checked in time.
- The container's build chain (`etapa1_banco_gxm.md` §5, ~5 min) reproduces the previous binaries except for the
  build date (9 bytes). To check that two binaries have the same code: build the `.o` files and compare `.text` with
  `objcopy -O binary -j .text`.
- For the commit from the PC's VM, delete permission in `Documents\GitHub` has to be requested in every session.

## Proposed next steps
1. (Decided to leave it for later) check the stable sublists at the start of each frame: faster, with the risk of using
   an old version if the game changes it in that frame.
2. SH: mark TRANSFER targets and stop asking for mirror pages that always change.
3. T2 (black box), T4, T6, GXM recording on several threads → stage 3 (draw).

## Working rules in force (from the user)
- Test versions `.x` (next: gepresent v13.5.x / GePatch dev63.x); commit only with the confirmed version.
- Build in the container and copy to `vitabridge\enviar` (and Downloads / the lab's memstick for ge_patch.prx); verify
  with md5 by downloading it from the Vita; close Adrenaline with `cmd_cerrar.txt` (`destroy`) to load a new gepresent.
  Do not upload anything to the Vita while the user has a pending test with the previous version.
- Commit and review from the Windows PC (git in `Documents\GitHub`, changes as a patch with `git apply`).
- When asking for tests: which game, what to do and what to look at; options with the menu name. Simple summary after
  every change. Logs of each run in `vitabridge/<etapa>_<versión>/<juego><n>/`.
