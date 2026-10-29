# Resumen de la conversación del 2026-10-27 (d): RR con "por copia" en el laboratorio

Continúa `resumen_conversacion_2026-10-27c.md`, que sigue valiendo entero, incluida su "Metodología de prueba".
**Este archivo reemplaza el punto 0 de "Abierto / próximos pasos" de `contexto_ia.md`.** Leer primero
`contexto_ia.md`, luego `resumen_conversacion_2026-10-27c.md` y después este.

## Estado
- Código: **dev58.7 sin commit**, igual que al cerrar la conversación c. No se cambió código en esta conversación.
  Respaldo en el Mac: `~/Downloads/vitabridge/gepatch_dev58.7.patch` (incluye `tests/replay.c`).
- **Prueba hecha: RR en el laboratorio del Mac (PPSSPP) con "Vertices 2D (HUD, sprites)" = "por copia (dev58)"
  → también falla.** No se ven la pista ni los autos, igual que en la Vita.
  - El usuario sacó un par de capturas (L+R+SELECT) en la carrera. Dice que **el retrovisor se dibujaba bien**; si no
    aparece en las capturas es porque la combinación de teclas cambió la vista de la cámara.
  - **Capturas y `ge_patch.log` de esa corrida: todavía sin revisar.** Siguen en el Mac, en
    `~/Documents/gepatch-lab/memstick/` (log y `.gpd`). Copiarlas a `~/Downloads/vitabridge/etapa6_dev58.7_lab_RR/`
    antes de la próxima corrida, porque el log se pisa.

## Conclusión
- **Se descarta la hipótesis principal** (que el GE real no ignora los 16 bits bajos de `BASE_PLUG`): PPSSPP sí los
  ignora y aun así falla. El problema está en el mecanismo de dev58 con RR, no en una diferencia entre la Vita y el
  emulador.
- El laboratorio reproduce la falla, así que **se puede bisecar ahí** sin arriesgar la Vita.
- Sospecha: la interacción de las listas CALL de los draws 2D con las de rt1 (retrovisor), que también van por
  `BASE_PLUG + CALL` (`txCall`). Puntos a revisar: el orden rt1SelfDraw/rt1FromBig → v2dDraw, los slots compartidos
  del anillo de 512 (RR gasta ~190 por frame), el VADDR/BASE restaurados al volver de la lista y si un draw 2D
  encadenado cruza un cambio de fb (buffer grande ↔ rt1).
- Otra cosa por verificar con el volcado: en la Vita el z-buffer quedaba limpio y el retrovisor mostraba la pista de
  frente (el mundo se dibujaba en el rt). En el laboratorio el usuario dice que el retrovisor se veía bien: comparar.

## Próximo paso (pendiente; instrucciones ya entregadas al usuario)
Prueba de bisección: **solo Ridge Racer, en el laboratorio del Mac, con "Vertices 2D (HUD, sprites)" =
"prueba: lista mínima"** (`v2d=4`: el desvío por CALL sin copia de vértices).
1. Guardar la corrida anterior en `~/Downloads/vitabridge/etapa6_dev58.7_lab_RR/`.
2. `PPSSPP_ADRENALINE=estricto bash ~/Documents/gepatch-lab/jugar.sh rr.iso > ~/Documents/gepatch-lab/ppsspp_salida.txt 2>&1`
3. Menú L+R+START: "Vertices 2D (HUD, sprites)" → "prueba: lista mínima". No tocar nada más (la escala 1,5x viene
   del perfil de RR). Guardar.
4. Ver en carrera si aparecen la pista y los autos (el HUD sin agrandar es normal en este modo).
5. Guardar el log en `~/Downloads/vitabridge/etapa6_dev58.7_lab_RR_v2d4/`.

Cómo leerlo:
- **Se ve la pista con "lista mínima"** → el desvío funciona y falla la copia ("por copia" / `v2d=2`). Siguiente:
  "prueba: copia sin aplicar" (`v2d=3`) y revisar la lista que arma `v2dDraw` con `tests/replay.c` (`W=`) sobre el
  volcado del laboratorio.
- **No se ve** → falla el desvío mismo: revisar la convivencia con rt1 (lista arriba).

## Notas de esta conversación
- **El usuario cambia las opciones desde el menú del plugin (L+R+START), también en el laboratorio.** Indicarle
  siempre el nombre de la opción y el valor como aparecen en el menú, no el archivo `gepatch_<ID>.txt` ni el nombre en
  el código.
- **Esta conversación quedó enlazada al PC con Windows** (`desktop-19r1h9e`), donde el laboratorio no tiene ISO ni
  dev58.7 (repo en dev57). Las carpetas del Mac no se pueden pedir desde una conversación enlazada a otro computador.
  Si la conversación nueva es para el laboratorio del Mac, empezarla desde la app del Mac con el Mac enlazado y
  conectar `~/Documents/gepatch-lab` y `~/Downloads`.
- En modo automático, la primera solicitud de acceso a carpetas fue rechazada sin preguntarle al usuario. Si pasa,
  volver a pedirla.
