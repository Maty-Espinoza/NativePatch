# Resumen de la conversación del 2026-10-26/27 (etapa 4 y comienzo del plan A)

Para continuar en una conversación nueva. Leer antes `contexto_ia.md`, `errores_conocidos.md` y
`plan_a_recorrido_vita.md`.

## Estado al cerrar (2026-10-27, 00:10)
- **Commit `e1c3ccf` (dev56) en `origin/dev`** de `Maty-Espinoza/NativePatch`: todo lo de esta conversación.
  GePatch **0.2-dev56** + **gepresent v9**.
- En la Vita: `ux0:/pspemu/seplugins/ge_patch.prx` = dev56 (md5 `0fefaee5…`); `ux0:data/gepresent/gepresent.suprx` = v9
  (md5 `20f3c01d…`). En el laboratorio y en `~/Downloads/ge_patch.prx`: dev56.
- Archivos de opciones en la Vita: GT (`gepatch_UCUS-98632.txt`) y SH (`gepatch_ULUS-10450.txt`) con `perfil=1` y
  `espera=3`; RR y LocoRoco con `escala=3`. GT además tiene como opciones conocidas `comprobar=1` y `tex_fb=0`.
- Cadena de compilación de la Vita armada en el contenedor (se pierde al cerrar; receta en `plan_a_recorrido_vita.md`).

## Qué se hizo (en simple)
1. **GePatch gasta menos procesador** al leer las instrucciones del juego:
   - GT: 15–27 ms → 8–10 ms por frame; SH: 10,3 → 6,4 ms.
   - Cómo: la memoria que recuerda las partes de la lista que no cambian tenía 1 casillero por parte y se pisaban (90 de
     434 en GT); ahora 4 casilleros y lo necesario en una sola línea de memoria; las instrucciones simples van por un
     camino corto; la tabla de cambios solo se consulta cuando hace falta.
2. **Cronómetro opcional** (`perfil=1`) que dice en qué se va el tiempo de GePatch; `perfil=2` (contadores del
   Allegrex) sin probar todavía.
3. **GT, zonas negras después de la primera curva: arreglado** (hallazgo del usuario): eran dibujos que GePatch anulaba
   con "Texturas desde framebuffer = omitir"; GT queda con "leer memoria".
4. **Capturas (volcados): de 4–9 s con el juego detenido a 0,4–0,6 s.** La Vita (gepresent) recibe por un "buzón" el log
   y las capturas, los copia por DMA a su propia RAM y los escribe a la tarjeta en segundo plano (idea del usuario).
5. **Opción `espera`**: la captura se hace N segundos después de soltar L+R+SELECT (en GT, L cambiaba la cámara).
6. **Colección de capturas de prueba** (`tools/coleccion.py`, `tools/coleccion.txt`; resultado en
   `vitabridge/coleccion/`).
7. **Documento `errores_conocidos.md`**: todos los errores con estado, causa o teoría y solución posible.

## Lo que se aprendió
- **A 2x el límite es el chip gráfico (GE)**, no el procesador: GT con el GE ocupado 85–88 % (21–30 ms por frame,
  ~26–31 fps); SH 43–57 ms. Bajar el costo de CPU de GePatch casi no sube los fps a 2x.
- La linterna de SH y el tablero de GT fallan porque **GePatch guarda la imagen en 16 bits sin canal de máscara
  (stencil)**: SH dibuja en 8888 con stencil; GT en 5551 con stencil.
- Dibujar con la GPU de la Vita en lugar del GE es posible en teoría, pero es escribir un emulador gráfico (meses).
- Para actualizar gepresent hay que **cerrar Adrenaline desde la LiveArea** y abrirlo de nuevo.
- La tarjeta escribe a ~4–6 MB/s.
- La sombra del video del inicio de GT es del juego (igual sin GePatch).

## Siguientes tareas propuestas (una por conversación)
1. **Aliviar el GE sin perder resolución en pantalla:** 1,5x + ampliación con un filtro que recupere nitidez en
   gepresent (sombreador GXM). Antes: probar GT con `escala=3` y vpjuego para ver cuánto baja el GE.
2. **Imagen grande en el formato del juego** (5551 para GT sin costo; 8888 para SH con escala ≤ 1,5x): linterna de SH y
   tablero de GT (`errores_conocidos.md` V1, V2).
3. **Plan A, paso 2:** recorrido "sombra" en la Vita (mismo código C compilado para el ARM, sin escribir nada) para medir
   su tiempo real; tiene sentido cuando el GE deje de ser el límite (escala < 2).
4. Render targets a escala completa en la VRAM libre (reflejos de GT; retrovisor de RR solo si el usuario lo pide).
5. Recapturar la colección con la versión actual (cada escena con su pasivo).

## Dónde está cada cosa
| Qué | Dónde |
|---|---|
| Código | `origin/dev` commit `e1c3ccf` (el clon del Mac está atrasado: hacer pull) |
| Logs de cada prueba | `~/Downloads/vitabridge/etapa4_dev56.3/`, `etapa4_dev56.4/`, `etapa4_dev56.5/`, `etapa4_dev56.6/`, `etapa4_dev56.7/`, `etapa4_dev56.8/`, `etapa4_dev56.8_v8/`, `etapa4_dev56.8_v9/` |
| gepresent v5–v9 | `~/Downloads/vitabridge/gepresent/` |
| Parches intermedios (ya en el commit) | `~/Downloads/vitabridge/gepatch_dev56.*.patch` |
| Imágenes revisadas | `~/Downloads/vitabridge/revision_dev56.3/`, `revision_dev56.4/`, `revision_dev56.8/` |
| Colección | `~/Downloads/vitabridge/coleccion/` (`salida_vieja/` se puede borrar) |
