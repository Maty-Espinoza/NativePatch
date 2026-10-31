# Resumen de la conversación del 2026-10-27 (noche): offload a la GPU de la Vita → dev57

Para continuar en una conversación nueva. Leer antes `contexto_ia.md`, `errores_conocidos.md` y
`analisis_offload_gpu_vita.md`.

## Estado al cerrar (2026-10-27, 01:10)
- **Commit `2c4e937` (dev57) en `origin/dev`**: regla "omitir draws solo de stencil (565)", `tools/carga_ge.py`, test
  `test_sin_efecto` (150 comprobaciones), `gpd.py` con la opción nueva. gepresent sigue en **v9** (sin cambios).
- En la Vita: `ux0:/pspemu/seplugins/ge_patch.prx` = dev57 (md5 `3ef94a99…`). En `~/Downloads/ge_patch.prx`: dev57.
  **El laboratorio (`~/Documents/gepatch-lab`) quedó en dev56.3**: la carpeta no estaba conectada; copiar
  `~/Downloads/ge_patch.prx` a `gepatch-lab/memstick/seplugins/`.
- El clon del Mac (`~/Documents/GitHub/NativePatch`) tiene `origin/dev` al día (fetch hecho) pero la copia de trabajo
  sigue en dev49 con cambios viejos: el usuario debe hacer `git stash` (o descartar) y `git checkout dev && git pull`.
- Opciones de GT en la Vita: el menú dejó guardado "Omitir draws solo de stencil (565) = no" en la corrida B.
  **Volver a "si"** (menú → esa opción → "Guardar opciones para este juego"), o borrar la línea `sin_efecto=0` de
  `ux0:/pspemu/gepatch_UCUS-98632.txt`. El perfil de GT sigue en "reloj + Allegrex" (`perfil=2`, funcionó sin cerrar
  Adrenaline): para medir fps, ponerlo en "no".

## Qué se hizo (en simple)
1. **Pregunta del usuario:** ¿se puede mandar parte de los comandos del GE a la GPU de la Vita? **No conviene.** El GE
   dibuja en una memoria que la Vita no ve; todo lo 3D depende del z-buffer que está ahí. Copiar esa memoria tampoco
   sirve (5 ms por copia, varias por frame). Solo la "cola" 2D del frame (HUD) podría pasar a la Vita, y vale 1–2 ms.
   Detalle en `analisis_offload_gpu_vita.md`.
2. **Herramienta nueva `tools/carga_ge.py`**: estima cuántos píxeles rellena cada draw de una captura y separa lo que
   usa z de lo que no.
3. **Hallazgo:** GT dibuja 4 rectángulos de pantalla completa por frame que solo escriben stencil; sobre el buffer 565
   de GePatch no hacen nada. **dev57 los omite**: GT a 2x pasa de 24,6 a 22,4 ms de GE por frame y de 34,4 a 37,8 fps.
4. Logs de la medición en `vitabridge/etapa5_dev57.1/` (A con regla, B sin regla).

## Lo que se aprendió
- Un draw con el color enmascarado le cuesta al GE menos que un borrado: 4 pantallas de 960x544 = ~2,2 ms (no 3,5).
- GT dibuja el HUD en 3D con proyección ortogonal (no en modo through): el criterio "2D" en los análisis debe ser
  "sin prueba de z", no "through".
- Relleno estimado por frame: GT 3 clears + 3–7 pantallas de 3D; SH 4 clears + **22–24 pantallas de 3D con z** (por eso
  SH no llega a 30 fps a 2x: es puro relleno); RR 2 + 5.
- `perfil=2` (contadores del Allegrex) funciona en Adrenaline 8 ("cada medición cuesta 15,44 ciclos").

## Siguientes tareas propuestas (una por conversación)
1. **1,5x + ampliación con nitidez en gepresent** (sombreador GXM precompilado): la forma real de pasar relleno a la
   GPU de la Vita. Antes, probar GT con `escala=3` y vpjuego (ya propuesto).
2. Imagen grande en el formato del juego (5551 GT / 8888 SH): linterna y tablero (V1, V2). Nota: apaga la regla dev57.
3. Plan A paso 2 (recorrido sombra en el ARM), cuando el GE deje de ser el límite.
4. Recapturar la colección con dev57 y correr `carga_ge.py` sobre SH y RR por si hay más relleno sin efecto.
