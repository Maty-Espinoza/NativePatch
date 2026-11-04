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

   **Resultado de 2 (`stage3_0.0.16.8/rr2`, 53.785 muestras de vértices en carrera):** 0,5 % cambia un frame después (~13 ms) y 21,5 %
   dos frames después (~30 ms); texturas y paletas 0 %. La GPU los leería 30-60 ms después del flip: **C.1 no puede leer la RAM del PSP
   directamente**. B.2 sigue siendo posible (la copia ocurre antes de 16 ms); C.1 debe leer la copia del registro. Las texturas podrían durar más que hoy.

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

**Resultados del banco, 0.0.16.6 contra 0.0.16.5 (en el mismo momento; comparar solo con otras corridas del banco):** `rr_crowd`
qemu -25 %, mem -13 %; `rr_race` qemu -26 %, mem -12 %; `sprites2d` qemu -13 %, mem -5 %; `sublists` sin cambio. Hecho: B.5 (1 draw de
64); B.9 en parte (comandos simples y de matriz en un bucle corto, contadores una vez por línea); las mismas filas de VRAM se saltan
`rgTex`; `draw()` se salta los chequeos de textura si no cambió. Probado y descartado: `VADDR`/`PRIM` dentro del bucle corto (sin
ganancia); recordar el tamaño de vértice para `regDraw` (`sprites2d` +10 %, causa no encontrada).
**B.1 no ayuda como está descrito:** los logs de la Vita dan a `neon256` ~2,8 us por 256 B, los mismos 91 MB/s de `memcpy`. Solo el
DMA es más rápido, y sobre ~11 KB por llamada.
**Fuga del espejo (arreglada en 0.0.16.6, por confirmar):** una textura en una página de imagen se pedía como página de lista y nunca
se soltaba: `lists` creció de ~150 a 368 páginas en una sesión, la PSP gastaba 3,2 ms por frame en el espejo en vez de 1,3 y el juego
bajó a 40-46 fps. Ahora esas páginas expiran cuando ningún frame las lee. Verificar: Vita (`mirror: pages requested lists`, `mirror_us`).

**Banco mejorado (0.0.16.7):** instrucciones exactas por frame (`ir_k/f`) con perfil por función y línea, el traductor sobre frames
reales (`tr_*`), las páginas pedidas al espejo (`want`) y los frames que se recorren sin grabar (`*_late`). Con eso: `regDraw` se salta
`vsize`, `rgRows` y `rgTex` en draws 3D con la misma textura de VRAM: `rr_crowd` -16 %, `rr_race` -15 % de instrucciones.

**0.0.16.7 en la Vita (`stage3_0.0.16.7/rr1`, sin relojes altos):** juego a 60 fps en todo, carrera completa en HD con 0 frames de
imagen normal, 21-25 frames HD por segundo, 7-12 en el segundo del público; `lists` bajo 150 páginas, espejo 1,3 ms. El traductor limita
126 de 139 s. Probado en el banco: recordar el último color de `t3Flat` (+1,9 %: los colores cambian vértice a vértice).

**`nd_fd` en la Vita (0.0.17.1, segundos de carrera contra 0.0.16.8, por frame HD):** 27 % de los píxeles pasó a él; superficie 36,6 ->
30,0 ms, GPU 45,0 -> 38,2 ms, inactiva 17 -> 27 %. El ritmo HD no cambia (18,7 por segundo): el traductor limita 128 de 140 s, 42,8 ms por
frame HD en carrera, 22,4 de ellos vértices. 1,36 M de píxeles siguen en `nd_ft` por el alpha test.

**Traductor (banco, 0.0.17.2):** los frames reales son uniformes: ~15.000 vértices en ~2.700 draws, 5,5 por draw, 77-99 % en formato `0x19f`,
87-99 % con iluminación y ninguna luz, 87 % con niebla. Un bucle NEON para ese vértice y un camino más corto para draws que comparten estado:
**instrucciones ARM por frame 8,14 M -> 5,78 M (-29 %)**, mismos draws y vértices en `tr_check.sh`. En la Vita (segundos de carrera contra 0.0.17.1): vértices solo 22,4 -> 19,8 ms
(-12 %), trabajo del traductor 41,5 -> 38,1 ms, 18,7 -> 19,6 frames HD por segundo. **Menos instrucciones dieron mucho menos tiempo: la parte de vértices no son
sobre todo instrucciones.** Sospecha, sin medir: lecturas del registro y escrituras a `tv`. Log de la Vita: lectura con caché 24 ns por palabra, sin caché 199.

**De qué está hecho el tiempo de vértices (`vert_split`, 0.0.17.3, ~1140 draws y 6900 vértices, sin el costo del reloj):** `trVerts` ~9,7 ms, `trTris` ~5,3, el resto de la
parte ~4,4 (incluye contadores de desarrollo), estado ~1,3, copia a `tv` ~0,6. Repetir `trVerts` de inmediato, con datos y código en caché, cuesta 1,26 us por vértice contra 1,39 la primera vez, y leer
los datos antes no cambia nada: **no es la memoria, un prefetch no ayudaría.** 1,26 us son ~420 ciclos, que las instrucciones no explican; la causa sigue abierta. Un ajuste sobre ventanas de 10 s da ~27 us
por draw reescrito por `trTris`.
**`nd_fa` (0.0.18):** GPU 38,1 -> 32,5 ms por frame HD, libre 24 -> 36 %; los frames HD no se mueven: limita solo el traductor.
**Hilos (`thread_split`, 0.0.17.6, carrera):** el hilo de dibujo de Adrenaline (prioridad 0xA0, sin núcleo fijo) corre 749 ms por segundo en los núcleos 1 y 2, lo desplazan 8 veces por segundo, lo interrumpen
~2500 y lo mueven de núcleo ~1400. El capturador se mueve ~650 veces por segundo. En un frame pesado corrió 42 de 54 ms: como casi no lo desplazan, esos 12 ms no son el capturador quitándole el núcleo
(`capture_core` no los recupera); probablemente son esperas dentro de llamadas a GXM, sin medir. Los movimientos no se probaron.

**Verificaciones de recorte y unión antes de C.1 (0.0.18.1-0.0.18.5, carrera, `vert_split` encendido):** en la carrera real, 20-50 % de los dibujos 3D tienen scissor menor que la superficie (los dos frames del bench lo subestiman). `trTris` gasta ~4,8 ms en ~150 dibujos.
| Corrida | HD/s | GPU superficie ms por frame HD | Espera GPU ms/s | Traductor ms por frame HD | `tris` ms |
|---|---|---|---|---|---|
| Base | 19,4 | 24,6 | 15 | 39,8 | 4,8 |
| `clip_gpu`, planos `CLP0` (con o sin region clip) | 14,6 | 42,2 | 99 | 45,2 | 1,4 |
| `prog_swap`, solo cambio de vertex program | 20,6 | 24,5 | 17 | 37,9 | 4,8 |
| `clip_discard`, `discard` en `nd_ftc`/`nd_fcc` + region clip | 20,5 | 26,4 | 37 | 37,1 | 1,7 |
Los planos son exactos pero saturan la GPU, y no por el área (region clip no ayuda) ni por el cambio de programa; causa desconocida. `discard` funciona con el programa general. Visual: un momento de artefactos en el cielo en el público con `clip_discard`, causa abierta; el fallo `color_test_or_logic` de esa zona también salió con `prog_swap`. `no_join`: +1 us de CPU por dibujo (GXM 5,43 -> 6,45 us), ~0,8 ms por frame HD, GPU igual: C.1 puede dibujar sin unir.
**Vértices con luz (`light_split`, 0.0.18.4):** pantalla de elegir auto: ~5200 vértices, ~25 ms: tipos `0x142`/`0x143` con una luz direccional y especular ~5,5 us por vértice, UV generado ~3,3 us, contra ~1,3 us del camino rápido; la luz es ~2 us de eso. En la carrera, los autos rivales suman solo ~1-3,5 ms; los picos son más vértices `0x19f`.

## Orden desde aquí (2026-11-04)
1. **C.1 sin tocar NativeLink:** los vértices de los draws elegibles van a una zona del registro sin caché y mapeada para la GPU, como las texturas (`tx`), con la misma espera antes de reusarla, y un vertex
   shader los transforma. Sin copia de la GE. Antes de empezar: scissor en el shader (si no, `trTris` sigue en la CPU) y el costo de no unir draws (etapa 1: ~1 us de CPU y ~1,3 us de GPU por draw). Esperado:
   ~10-13 ms menos de traductor, hasta ~28 frames HD por segundo, donde vuelve a limitar la GPU.
2. **GPU (C.3):** menos píxeles (9,4 pantallas por frame).
3. **C.2 solo si el capturador pasa a ser el límite:** no limita la carrera normal, y para el público B.3 es más barato.

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
