# Plan de rendimiento: 60 fps con la Vita dibujando

La única lista de trabajo de rendimiento, en orden. Sale del análisis del capturador y de la corrida de Ridge Racer
`stage3_0.0.16.4/rr1` (2026-11-03; detalle en el proyecto de claude.ai, `claude/analisis_capturador_2026-11-03.md`). Cada
punto dice cómo se verifica: **bench** (`tools/perf/`, en el PC, `../../agent/en/perf-loop.md`) o **Vita** (una prueba en la
consola). Lo hecho pasa a `CHANGELOG.md`; los resultados de los intentos quedan aquí, bajo cada punto.

## Dónde se va el tiempo hoy (Ridge Racer, relojes altos)
Tres etapas trabajan en paralelo en tres cores: capturador, traductor (hilo de dibujo de Adrenaline) y GPU. Para 60 frames
HD por segundo cada una debe quedar bajo 16,7 ms por frame.

| Etapa | Hoy | Hace falta |
|---|---|---|
| Capturador | 7 ms promedio, **24 ms en el público** (3500 draws, 545 KB de vértices por frame) | ~2x solo en el público |
| Traductor | 30-36 ms por frame HD (16-20 ms son vértices 3D transformados en la CPU) | ~2,5x |
| GPU | 35-39 ms por frame HD (superficie 32 ms: 4,8 M de píxeles, 98 % con blend) | ~2,5x |

El capturador es la única etapa que le cuesta fps al juego (la compuerta hace esperar al juego: 55 fps en el público); las
otras solo bajan la tasa HD. En el segundo del público, 44 de 55 frames no se dibujaron igual (capturador atrasado), así que
esa espera se pagó por nada. Los 24 ms del capturador son, aproximadamente: copia de vértices ~5-6 ms (`rgVFlush`, memcpy
desde la RAM sin caché del PSP a 91 MB/s), registro 4 ms, lecturas de la lista ~3 ms, recorrido y `draw()` ~5-8 ms, cierre
del frame.

## A. Confirmar primero (Vita)
1. **`cap_split` con 0.0.16.5 en el público** (`ux0:data/nativedraw/cap_split`): la línea `capture split` separa events,
   flip, walk, hit, own, draw, record, state, vertices, textures y `vflush`. Esperado: `vflush` ~5 ms, `draw` varios ms.
   Hasta medirlo, B.1-B.3 se apoyan en una estimación.
   **Resultado (`stage3_0.0.16.5/rr1`, sin relojes altos, ms por frame; cada lectura de reloj suma 0,4 us, hasta ~3,2 us por draw):**
   un frame completo de ~1450 draws y 11.000 comandos cuesta ~18-19 ms reales (24 medidos): registro ~10 (vértices ~4,7, texturas
   ~3,8, estado ~1), intérprete 5, lectura de listas ~2, `draw()` menos de 0,5, fin de lista y eventos 1-2. Un frame del público de
   ~2800 draws y 17.800 comandos, ya incompleto y sin copiar nada, cuesta ~13 ms reales (17 medidos): intérprete 7, lecturas ~3. O
   sea, un frame del público no cabe ni sin grabar; grabarlo tomaría ~33 ms (nunca medido: ninguno quedó completo).
   Contra la estimación: `vflush` y el volcado de fin de lista son 1-2 ms como mucho en los frames que se completan, y `draw()` es barato.
   Los 21 ms leídos antes de 0.0.16.5 contaban dos veces el recorrido del flip. Error: la columna `events` se desborda (ignorarla).
2. **`data life` en nivel extremo en la carrera**: cuántas veces los vértices cambian 1 y 2 frames después de su draw.
   Decide B.2 y C.1 (si la GPU o una copia posterior pueden leer vértices que el juego podría reescribir).

## B. Capturador, cambios chicos
1. **Copia de vértices con cargas NEON de 256 bytes** en vez de `sceClibMemcpy` en `rgVFlush` y `rgCopy` (las cargas solapan
   las esperas de la memoria sin caché, como hace `neon256` con las listas). Esperado 2-3x sobre ~5 ms. Verificar: bench
   (`ucp_kb` no cambia, el modelo de costos no lo ve: medir la rutina de copia dentro del bench con una línea de
   microbenchmark) y luego Vita (`vflush`).
2. **Publicar `SB[3]` antes de `rgVFlush`** (A.1: solo 1-2 ms en los frames que se completan): hoy el juego espera en la compuerta también la copia de vértices. Quita hasta
   ~6 ms de compuerta por frame sin costo. Condición: A.2 muestra que los vértices viven al menos un frame. Verificar: Vita
   (`gate_us`, `vertex_out`).
3. **Copias de vértices y texturas de RAM en otro core** (el hilo `record` ya existe): la copia del frame N corre mientras el
   capturador recorre el frame N + 1. Misma latencia, el doble de capacidad. Verificar: Vita (`capture us/f`, `complete`).
4. **Compuerta adaptativa**: cuando el frame ya está incompleto (R_SKIP, behind, registro apagado) NativeDraw pone un bit en
   `SB[0x4A]` y NativeLink no espera ese frame: el juego sigue a 60 fps mientras la Vita descarta el frame. Riesgo: listas
   leídas mientras se reescriben (mitigado por `st_good` y el snapshot de registros). Verificar: Vita (fps del juego, `late`).
5. **Menos lecturas de reloj**: el registro muestrea 1 draw de 8 con 4 lecturas de reloj cada uno: ~1700 lecturas por frame
   del público, ~1 ms en la Vita (0,6 µs cada una). Muestrear 1 de 64, o solo en nivel medio. Verificar: bench (`clock/f`,
   `mem_us`) y luego Vita.
6. **`regDraw()` por draw** (A.1: `draw()` en sí es menos de 0,5 ms, no vale tocar las estadísticas; el tiempo está en
   `vertices` y `textures`, por separar más): búsqueda lineal de textura en `rgTex` (hasta
   ~90 entradas por frame, 3500 draws), `rtRead`/`markRt`/`want` en cada draw con textura VRAM, contadores de estadísticas
   que solo alimentan el log (bajo `level`), comparación de estado por draw. Verificar: bench (`qemu_min_us`) y luego Vita
   (`draw`).
7. **Prioridad de los lectores**: en el público cada lector recibe 49 ms/s (0xB0 en cores ocupados); lecturas propias 525
   por frame contra 1415 de los lectores. Probar 0x80-0xA0 o el core libre. Verificar: Vita (`own` contra `from_readers`,
   audio).
8. **Tamaño del espejo por tiempo medido del GE** (TODO: 257 páginas de datos pedidas, 64 copiadas por flip; primero las
   páginas usadas en este frame). Verificar: Vita (`no_fit`, antigüedad de las texturas).

9. **Costo del intérprete por comando** (A.1: ~400 ns, ~130 ciclos, 5-7 ms por frame, más de la mitad de un frame del público que
   no se graba): lo que se anota por comando en `walkList` (`l->n`, `acc.cmds`, `f_cmds`, `l->prev`, `TOUCH`, `cls`). Verificar: banco
   (`qemu_min_us`) y luego Vita (`capture split`).

## C. Cambios grandes (preguntar primero al usuario)
1. **Vértices 3D en un vertex shader de GXM leyendo la RAM del PSP mapeada para la GPU** (`sceGxmMapMemory` en 0x70000000
   funciona, etapa 1). Quita la copia de vértices del capturador (~5 ms) y la transformación en CPU del traductor (16-20 ms
   por frame HD): la mayor ganancia de toda la cadena. Necesita A.2, `shacc` en el contenedor y matrices, luces, texgen y
   niebla como uniforms. Verificar: Vita.
2. **Rangos de vértices juntados por el GE del PSP (TRANSFER, como el espejo) o su DMAC** en un buffer contiguo de la zona
   compartida, enlistado al flip por NativeLink: sin CPU de ningún lado; con C.1 la GPU lee ese buffer directo. ~55 corridas
   por frame del público. Verificar: Vita.
3. **Relleno de la GPU**: un programa de niebla sin alpha test ni `discard`, no dibujar la imagen de abajo cuando el frame
   la cubre (11 % de los píxeles), menos sobredibujado. La etapa más dura: 4,8 M de píxeles por frame en el SGX543 a 222 MHz.
   Verificar: Vita (línea `bottleneck`, ms de `gpu`).
4. **Cachés del traductor** (paletas, draws traducidos, escenas idénticas) y unir más draws. Verificar: Vita.
5. **Dividir `capture.c`** (1900 líneas) en recorrido, memoria de sublistas, lectores y logs: no da fps, pero hace seguros
   B.6 y C.1. Verificar: bench y test_ci sin cambios.

## Metas realistas
- Juego a 60 fps con el plugin encendido en toda Ridge Racer: B.2 + B.4 (+ B.1/B.3) deberían lograrlo.
- Frames HD: 20-25 hoy en Ridge Racer con relojes altos. 40-45 con C.1 y C.3. 60 en el público probablemente necesite menos
  píxeles (no dibujar la imagen de abajo, sin 2x en las capas más pesadas). Medido, no prometido: cada punto se comprueba en
  la Vita antes del siguiente.
