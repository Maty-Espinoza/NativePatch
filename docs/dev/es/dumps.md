# Volcados

Un volcado guarda lo que pasó en pantalla en uno o más frames: la imagen del PSP, la que dibujó la Vita y lo que la Vita
recibió para dibujarla. Sirve para encontrar el origen de un error visual sin adivinar (regla "primero el origen" de
`../../agent/en/workflow.md`). Los niveles funcionan como los del registro (`logs-and-switches.md`): se elige uno según cuánta
información hace falta y cuánto puede costar.

## Niveles (`ux0:data/nativedraw/dump`, una palabra; sin el archivo: `sequence`)

| Nivel | L+R+SELECT guarda | Costo |
|---|---|---|
| `off` | nada | ninguno (no toma memoria) |
| `screenshot` | 1 frame: imagen del PSP + imagen de la Vita + una línea | ~3 MB solo mientras guarda |
| `frame` | 1 frame con todo: imágenes, texto, registro (`.bin`), imagen intermedia y la imagen exacta del PSP | ~8 MB solo mientras guarda; el PSP copia su imagen ~1,5 s (más lento ese rato) |
| `sequence` | 3 frames seguidos con todo (con la imagen exacta, como `frame`) | ~15 MB solo mientras guarda; lo mismo |
| `auto` | como `sequence`, y además se dispara solo cuando un bloque de la imagen de la Vita no se parece a ninguna de las últimas 4 imágenes del PSP | ~17 MB tomados todo el juego; unos ms por frame del hilo de Adrenaline; nada para el PSP |

- Cada volcado queda en `ux0:data/nativedraw/dumps/NNN/`. Guardar tarda unos 7 s (la tarjeta): mientras, no se
  puede hacer otro. El log de NativeDraw anota `dump: done in ... (automatic)`.
- `auto`: compara (1 de cada 2 frames) bloques de
  16x16 de la imagen de la Vita con las **últimas 4 imágenes del PSP** que copió Adrenaline y vale la más parecida (la
  Vita va 1–2 frames atrás: así el atraso no cuenta). Se dispara con 6 bloques que no se parecen a ninguna (y menos de
  la mitad: con más, es un diálogo encima). `fK_normal.bmp` es la imagen del PSP más parecida. (0.0.8.4–0.0.8.7
  comparaba con la imagen exacta copiada por el PSP en cada frame: le costaba ~1,2 ms por frame y con eso los mosaicos
  de KOF 96 no aparecían.) Como mucho 10 por juego,
  con 1 s entre uno y otro. El log del presentador dice cada 10 s cuántos frames revisó (1 de cada 4) y cuántos eran
  distintos. Solo funciona con `draw`.
- Para probar algo puntual conviene `frame` o `sequence`; `auto` es para cazar errores que duran poco.
- Desde 0.0.8.7, con L+R+SELECT en `frame` o `sequence` el PSP copia su imagen por 1,5 s y el volcado espera (hasta
  0,5 s) al primer frame que la trae: así se compara con el frame correcto sin el costo de `auto` todo el juego (en KOF
  96, `auto` bajaba a 32–36 fps y cambiaba los tiempos: los mosaicos que quedaban no aparecían con `auto`).

## Frames fallidos (versión de desarrollo)
Cuando el traductor no puede hacer un dibujo, ese mismo frame se guarda al instante, sin apretar nada, en
`ux0:data/nativedraw/fails/NNN_<motivo>/`: `f0_record.bin`, `f0_normal.bmp` (la imagen del PSP de ese momento) y `f0.txt` (qué
dibujo falló, su destino y textura y todos sus registros del GE). Un frame por tipo de fallo (motivo, destino, formato, primitiva),
como máximo 4 por motivo y 64 por sesión. Usa 2 MB reservados al arrancar, así que también atrapa fallos de un solo frame, a
diferencia de L+R+SELECT, que parte en el frame siguiente. `tools/dump.py` lee la carpeta. No ve frames que nunca se completaron
(la causa está en `record not_read:`) ni frames mal dibujados sin un fallo.

- Líneas `census:` de `nativedraw.log` (nivel extreme): todos los motivos por frame, qué son las texturas sin copia y la cadena de imágenes de cada tipo de frame. `record not_read:` de `nativedraw_capture.log`: la causa de cada frame incompleto y los frames solo recorridos.

## Archivos de cada volcado

| Archivo | Qué es |
|---|---|
| `fK_normal.bmp` | La imagen del PSP que tenía Adrenaline en ese momento (480x272). **Puede ser de otro frame** que el de la Vita (la Vita va 1–2 frames atrás) |
| `fK_vita.bmp` | Lo que mostró Adrenaline desde la superficie de NativeDraw (960x544) |
| `fK.txt` | Cómo se hizo esa superficie (dibujo de la Vita o imagen normal y por qué) y, con registro: texturas (filas copiadas, de qué copia del espejo), estados (solo lo que cambió) y la lista de draws |
| `fK_record.bin` | Todo lo que recibió la Vita para dibujar ese frame: draws, estados, vértices, paletas, texturas copiadas y (en `auto`) la imagen exacta del PSP de ese frame |
| `f0_intermediate_XXXXXX.bmp` | La imagen intermedia (la que KOF 96 dibuja escondida y después estira), dibujada por la Vita al doble |

## Cómo se leen (`tools/dump.py`)

1. Bajar la carpeta: un `list_*.txt` con `ux0:/data/nativedraw/dumps` y un `get_*.txt` con los archivos
   (`../../agent/en/relay.md`). **Escribir el pedido con otro nombre y renombrarlo al final**: el relevo puede tomar un
   archivo a medio escribir.
2. `python3 tools/dump.py <carpeta del dump> [K] [--textures-from <other .bin>] [--out <carpeta>]` (numpy y
   Pillow; corre también en la VM de Windows). Dibuja el frame en el PC imitando al GE del PSP, con los mismos datos que
   recibió la Vita, y deja `fK_pc.png` (referencia | PC / Vita | diferencias del PC en magenta) en la carpeta de salida
   (por defecto la del volcado; nunca en el repo).
3. Cómo interpretar:

| Resultado | Significa |
|---|---|
| PC ≈ referencia y Vita ≠ referencia | Los datos están bien: el error es de cómo dibuja la Vita (traductor, sombreadores) |
| PC ≈ Vita ≠ referencia | La Vita recibió datos malos (texturas de otro momento, copia incompleta) |
| Con `--textures-from` el PC sale bien | Las texturas de ese frame estaban atrasadas (las del frame siguiente eran las buenas) |
| Solo cambia la pose o algo que parpadea | Es atraso (otro frame), no un error: compararlo con la imagen exacta (`auto`) |

La referencia es la imagen exacta del frame si el `.bin` la trae (`auto` desde 0.0.8.4; `frame` y `sequence` desde
0.0.8.7); si no, `fK_normal.bmp`.

## Reproducción
`ux0:data/nativedraw/replay` con la ruta de un `fK_record.bin`: al abrir el juego (con `draw`) la Vita lo
vuelve a dibujar con el mismo traductor y deja `fK_replay.bmp` y `fK_replay_intermediate.bmp` junto al `.bin`
(`_record.bin` se cambia por el sufijo `_replay`)
(después escribe `no` en el archivo). Si la reproducción sale bien y el volcado en vivo no, el error depende del momento
(así se encontró en 0.0.8.6 que la Vita sobrescribía texturas que la GPU todavía leía).

## Historia
- 0.0.7.4: L+R+SELECT, 4 frames, imágenes y texto. 0.0.7.5: escritura rápida y texto compacto. 0.0.7.7: registro
  `.bin` y `tools/dump.py`. 0.0.7.8–0.0.7.10: automático (puntos → bloques; se confirma con la GPU terminada).
  0.0.7.9: 3 frames, texto armado desde el registro al escribir. 0.0.8.3: imagen intermedia. 0.0.8.4: imagen exacta del
  frame. 0.0.8.5: niveles y reproducción (`replay.c`: `ux0:data/nativedraw/replay` con la ruta de un `.bin` lo
  vuelve a dibujar en la Vita). 0.0.8.7: imagen exacta también con L+R+SELECT.
- En 0.0.8–0.0.8.15 una textura con swizzle va tal cual en el registro (versiones 2 y 3 del registro): `tools/dump.py`
  la ordena en el PC. Desde 0.0.8.16 (versión 4) el registro ya la trae ordenada; la reproducción en la Vita no carga
  volcados de antes de la 4 que tengan texturas con swizzle. Si ningún draw va a la imagen que se muestra, se toma la del último draw (como el traductor).
