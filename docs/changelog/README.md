# Archivo de cambios (docs/changelog)

Lo que se hizo y se midió en cada versión, tal como quedó en su momento. **No se actualiza**: usa los nombres y números
de su época (GePatch `0.2-devNN` = lado PSP, hoy NativeLink; gepresent `vNN` = lado Vita, hoy NativeDraw; ver
`docs/dev/versiones.md`). Lo vigente para trabajar está en `docs/dev/`. El código de esas versiones está en la etiqueta
`gepatch-dev66` y en el historial de git.

El resumen corto de todo esto es `CHANGELOG.md` (raíz). **Antes del primer release se quita `conversaciones/`** (decisión
del usuario): esos resúmenes son para retomar el trabajo entre conversaciones con Claude.

| Archivo | Qué |
|---|---|
| `conversaciones/` | Resúmenes de cada conversación con Claude (2026-10-26 a 2026-10-29), en orden |
| `CONTEXTO_IA.md`, `CONTEXTO_IA_LOCAL.md` | Contexto de GePatch hasta dev49 (arquitectura A); el segundo, para una IA local |
| `diagnostico.md`, `estrategia_diagnostico.md`, `regresiones_versiones.md` | Diagnóstico de errores de la arquitectura A |
| `geptest_resultados.md`, `laboratorio_mac.md`, `modelo_adrenaline.md`, `modelo_vita.md` | Laboratorio PPSSPP y pruebas del GE (herramientas quitadas en 0.0.2) |
| `gepatch_compatibilidad.csv` | Lista de compatibilidad del GePatch original |
| `etapa0_resultados.md` | Etapa 0: cuánto dibuja cada juego |
| `etapa1_banco_gxm.md` | Etapa 1: banco de pruebas GXM (gepresent v11) |
| `etapa2_inicio.md`, `etapa2_avance.md` | Etapa 2: plan, modo sombra y capturador (dev61, v12) |
| `etapa2_memoria_sublistas.md`, `etapa2_comparar_menos.md` | Memoria de sublistas y comprobación por los lectores (v12.12–v13.4) |
| `etapa2_espejo_vram.md`, `etapa2_espejo_doble.md` | Espejo de la VRAM, simple y doble (dev62, dev64) |
| `etapa2_t4_t6_vida_datos.md` | Telemetría T4/T6 y vida de los datos (dev63, v13.5) |
| `etapa2_nucleos.md` | Grabación GXM en varios núcleos (v13.7): GT no entra draw por draw |
| `etapa2_fase1_impacto.md` | Fase 1: por qué el modo sombra todavía le costaba al juego (dev65, v13.9) |
| `etapa2_fase2_compuerta.md`, `etapa2_fase2_estadisticas.md`, `etapa2_fase2_adelanto.md` | Fase 2 (dev66, v13.10–v13.12): cierre de la etapa 2 |
| `etapa2_ridge_racer.md` | Ridge Racer a 60 fps (v13.13) |
| `etapa3_preparacion.md` | Preparación para la etapa 3: NativeLink + NativeDraw 0.0.1/0.0.2, instalador, limpieza, pruebas |
