# Changelog archive (docs/changelog)

What was done and measured in each version, as it was at the time. **It is not updated**: it uses the names and numbers
of its time (GePatch `0.2-devNN` = PSP side, today NativeLink; gepresent `vNN` = Vita side, today NativeDraw; see
`../../dev/en/versions.md`). What is current for working is in `../../dev/en/` and `../../agent/en/`. This archive also exists in Spanish (`../es/`), with the same file names; the Spanish versions are the originals. The code of those versions is in the tag
`gepatch-dev66` and in the git history.

The short summary of all this is `CHANGELOG.md` (root). **Before the first release `conversaciones/` is removed** (user's
decision): those summaries are for resuming work between conversations with Claude.

| File | What |
|---|---|
| `conversaciones/` | Summaries of each conversation with Claude (2026-10-26 to 2026-10-29), in order |
| `CONTEXTO_IA.md`, `CONTEXTO_IA_LOCAL.md` | GePatch context up to dev49 (architecture A); the second one is for a local AI |
| `diagnostico.md`, `estrategia_diagnostico.md`, `regresiones_versiones.md` | Diagnosis of architecture A bugs |
| `geptest_resultados.md`, `laboratorio_mac.md`, `modelo_adrenaline.md`, `modelo_vita.md` | PPSSPP lab and GE tests (tools removed in 0.0.2) |
| `gepatch_compatibilidad.csv` | Original GePatch compatibility list |
| `etapa0_resultados.md` | Stage 0: how much each game draws |
| `etapa1_banco_gxm.md` | Stage 1: GXM test bench (gepresent v11) |
| `etapa2_inicio.md`, `etapa2_avance.md` | Stage 2: plan, shadow mode and capturer (dev61, v12) |
| `etapa2_memoria_sublistas.md`, `etapa2_comparar_menos.md` | Sublist memory and check by the readers (v12.12–v13.4) |
| `etapa2_espejo_vram.md`, `etapa2_espejo_doble.md` | VRAM mirror, single and double (dev62, dev64) |
| `etapa2_t4_t6_vida_datos.md` | T4/T6 telemetry and data lifetime (dev63, v13.5) |
| `etapa2_nucleos.md` | GXM recording on several cores (v13.7): GT does not enter draw by draw |
| `etapa2_fase1_impacto.md` | Phase 1: why shadow mode still cost the game performance (dev65, v13.9) |
| `etapa2_fase2_compuerta.md`, `etapa2_fase2_estadisticas.md`, `etapa2_fase2_adelanto.md` | Phase 2 (dev66, v13.10–v13.12): closing of stage 2 |
| `etapa2_ridge_racer.md` | Ridge Racer at 60 fps (v13.13) |
| `etapa3_preparacion.md` | Preparation for stage 3: NativeLink + NativeDraw 0.0.1/0.0.2, installer, cleanup, tests |
