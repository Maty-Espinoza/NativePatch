# Resumen para seguir en otra conversación: etapa 2, fase 2, punto 1 (2026-10-29, novena conversación)

Leer: `flujo_de_trabajo.md` → este resumen → `etapa2_fase2_compuerta.md` (lo hecho) → `etapa2_fase1_impacto.md`
(propuesta de la fase 2) → `registros.md` → `TODO.md`.

## Dónde quedamos
- **Commit `768af4b` en `dev`, con push a GitHub: GePatch 0.2-dev66 + gepresent v13.10** (compuerta antes de encolar la
  lista siguiente + `docs/etapa2_fase2_compuerta.md`, registros, flujo de trabajo y `TODO.md` actualizados). Push desde el
  contenedor (`add_repo` con push, desde el clon `/home/claude/maty-espinoza/nativepatch`), a nombre de Claude.
- El repo de Windows quedó igual a GitHub (paquete `vitabridge/dev66.bundle`). El repo del Mac está atrasado (3 commits):
  pull antes de trabajar ahí.
- Vita: probada con dev66.3 + v13.10.3 (mismo código). La versión final dev66 (`16755a0c…`) + v13.10 (`5f08b9d1…`)
  quedó en `vitabridge/enviar/` esperando: el relevo perdió la conexión con la Vita ("timed out") al final. Al reiniciar
  el relevo se sube sola; verificarla con un `bajar_verif.txt` a `etapa2_dev66/verif/`.
- Interruptores en la Vita: `compuerta_sync`, `sin_compuerta`, `banco_nucleos` = "no". Registro mediano (sin archivo).
- Logs: `vitabridge/etapa2_dev66.1/gt1`, `etapa2_dev66.2/gt1`, `etapa2_dev66.3/gt1` y `etapa2_dev66.3/sh1`.

## Qué se hizo
- La espera del juego a la Vita pasó del sync a "antes de encolar la lista siguiente", con autocomprobación: gepresent
  deja huellas de la lista (en todos los niveles, con su número de orden en `SB[0x53]`) y GePatch las revisa antes de
  encolar; si cambiaron vuelve al sync (60 comprobaciones limpias para volver; 10 cambios = hasta cerrar el juego).
- dev66.1 y dev66.2 daban falsas alarmas: gepresent marcaba la lista como capturada antes de dejar su muestra, y GePatch
  revisaba la muestra de la lista anterior (zona que GT ya reusa). Arreglado en dev66.3 (muestra antes de `SB[3]`, y solo
  se acepta la muestra de una lista posterior al sync anterior): 0 cambios en GT y en SH.
- GT: ~2 150 → 690 esperas en la carrera; mitad de la vuelta ~1 000 → ~45, 59,8–59,9 fps. Arranque y final siguen
  esperando (la Vita necesita más de 16,7 ms ahí). Final de la vuelta 54,7 fps (contra 56,0): una sola corrida, vigilar.

## Siguiente (fase 2, puntos 2–5 de `etapa2_fase1_impacto.md`)
2. Capturador: leer directo de a 256 B en vez de la caché compartida (~0,8 ms).
3. Aplicar sublistas sin sumar todas las estadísticas (~1,5 ms).
4. Revisar sublistas más barato o por adelantado.
5. Núcleo / prioridad del capturador (cuidado con el audio).
Además: probar la compuerta nueva en Ridge Racer y LocoRoco; `resumen_corrida.py` no muestra todavía las líneas de la
compuerta (usar `grep "stats compuerta" ge_patch.log`).

## Lecciones
- Una revisión de seguridad puede fallar por el orden de los avisos entre el PSP y la Vita: si las alarmas aparecen solo
  en los frames con espera, sospechar de la revisión antes que del juego.
- Para comparar tramos de la carrera sirve `grep "stats impacto" ge_patch.log` (espera por frame y esperas cada 10 s).
- El stop hook del contenedor pide commit con cambios sin subir: no se hace commit de versiones de prueba (regla del
  usuario); se explica y se espera la confirmación.

## Reglas de trabajo vigentes
- Versiones de prueba `.x` (siguiente: GePatch dev67.x / gepresent v13.11.x); commit solo con la confirmada.
- Pruebas: qué juego, qué hacer, en qué fijarse; opciones con el nombre del menú; resumen simple después de cada cambio.
- Registro: mediano durante el desarrollo, extremo para análisis, mínimo para publicar.
