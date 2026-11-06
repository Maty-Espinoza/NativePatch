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
3. **Copias de vértices y texturas de RAM en otro core: probado con vértices y descartado (0.0.19.1-0.0.19.2).** Copiar después de soltar la lista deforma la geometría: el juego reescribe los vértices. Copiar durante el recorrido y esperar al final de la lista no dio ganancia (frames completos 30,3 -> 29, el capturador espera 0,7-2 ms, tramos muy chicos para DMA) y dejó algunos triángulos deformados, sin causa encontrada. La lista no se puede soltar antes de copiar sus vértices, así que esa espera no desaparece. Idea original (el hilo `record` ya existe): la copia del frame N corre mientras el
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

**C.1, primera etapa (0.0.18.6-0.0.18.8, confirmada como 0.0.19):** `nd_v3` lee la copia del registro al final de `tx`. Elegibles: tipo `0x19F`, sin índices, triángulos, tira o abanico, sombreado suave, sin luces, sin UV generada, scissor que abarca toda la imagen.
| Carrera, por frame HD | 0.0.18.5 | 0.0.19 |
|---|---|---|
| Frames HD por segundo | 20,5 | 21,7 |
| vértices, ms | ~19,5 | 13,1 |
| traductor, ms | ~37 | 32,1 |
| GPU, ms | ~35 | 33,2 |
| vértices movidos por la GPU | 0 % | 54 % |
Quedan en CPU por frame HD: retrovisor ~1500 vértices (scissor 150,10 a 330,48) y otros tipos ~500. Los vértices marcados que el traductor rechaza leen memoria sin caché (177-207 ns por palabra contra ~15), así que un dibujo no debe marcarse y luego rechazarse. La copia del capturador a `tx` no muestra un costo claro en carrera.

**Vértices con luz en la GPU (0.0.19.3-0.0.19.4, confirmada como 0.0.20):** la selección de autos estaba limitada por la CPU, no por la GPU (traductor ~33 ms, vértices ~21 ms, GPU ~20 ms y 54 % sin trabajo). `nd_v4` toma los tipos `0x142`/`0x143`: una luz direccional con especular, o UV generada desde la normal.
| Selección de autos, por frame HD | 0.0.19 | 0.0.20 |
|---|---|---|
| traductor, ms | ~33 | 16,5 |
| vértices, ms | ~21 | 2,3 |
| GPU, ms | ~20 | ~21 |
| frames de Adrenaline por segundo | ~27 | ~44 |
| frames HD por segundo | ~21 | ~27 |
Las pasadas coplanares de un modelo deben ir por el mismo camino y programa: con solo la pasada de luz en la GPU, la prueba de profundidad GEQUAL del reflejo fallaba por redondeo y los reflejos parpadeaban en dientes de sierra.
**Qué limita ahora la selección de autos:** `regFlip` se salta grabar un frame (`R_SKIP`) si el anterior aún no fue tomado. La GPU tarda ~21 ms, más que un frame del juego, así que se salta uno de cada dos: ~30 grabados y ~30 solo recorridos por segundo, un tope de ~30 frames HD aunque la cadena daría ~45. Ahí el capturador tiene tiempo de sobra.

## Público medido limpio (`hold_game`, 0.0.21.3, `stage3_0.0.21.3/rr1`, 2026-11-04)
Con `hold_game` el juego espera a toda la cadena: todos los frames completos y en HD, sin listas leídas tarde. CPU 333, GPU 222, log extremo; ~3600 dibujos, ~19.000 comandos, ~19.000 vértices y 446 KB de vértices por frame. El juego mismo toma ~7 ms; aquí corre a 15 fps (66 ms).

| Etapa, ms por frame | Público | Tramos difíciles (1000-2500 dibujos) | Para 30 fps | Para 60 fps |
|---|---|---|---|---|
| Capturador (procesador) | ~32 | 17-21 | 33 | 16,7 |
| Hilo de dibujo (~45 son del traductor) | ~54 | ~40 | 33 | 16,7 |
| GPU | ~34 | ~35 | 33 | 16,7 |

- **La etapa más lenta es el hilo de dibujo, no el capturador.** El capturador limita hoy solo porque se atrasa frente al juego y eso arruina frames; cuando alcance, el ritmo HD lo fijan el hilo de dibujo y la GPU.
- Traductor, ms: GXM 15, vértices 8, texturas 4,6, revisión 3,8, escenas 1,4, ~12 sin desglosar. Capturador, ms: registro 12,4 (vértices 7,3, estado 2,6, texturas 2,5), lecturas 3, el resto el intérprete. Hilo del registro: 13 más por texturas de VRAM, aparte.
- GPU, ms: 2 escenas de superficie ~25; 15 escenas intermedias de un dibujo, 0,6 cada una, ~9.
- El 92 % de los dibujos no cambia el estado; los vértices vienen en ~220 tramos contiguos. 11.000 de los 19.000 comandos son BASE, VADDR y PRIM.
- Descartado: saltos por caja envolvente. 0 en el público con lectura limpia; los 5400 por segundo de antes eran listas leídas tarde. El público real son ~16.000-19.000 comandos, no 25.000.

**Lo que sigue, por ganancia:** juntar dibujos con el mismo estado en un `sceGxmDraw` (GXM, GPU); agrupar las escenas intermedias (GPU); un camino corto para BASE, VADDR y PRIM en el intérprete (capturador); luego los vértices del retrovisor.

## Dibujos juntos (0.0.21.5, `stage3_0.0.21.5/rr1`, `hold_game`, log medio)
Juntar funciona: 3405 -> 267 llamadas GXM en el frame del público, mismos triángulos. Los vértices para la GPU deben mantener alineación de 16 bytes: pegados uno tras otro (0.0.21.4) la GPU cayó al entrar a la selección de autos, juntos o no; en múltiplos de 48 no cae. La razón exacta dentro de la GPU no se conoce.
- Los fps se mueven en escalones, 60 / n: 30, 20, 15. Traductor bajo 33 ms da 30 fps; entre 33 y 50 ms da 20.
- El tiempo "GXM" no bajó con la cantidad de dibujos (público ~13 ms): esa cuenta es sobre todo trabajo por dibujo antes de la llamada a GXM. En el PC, el frame del público toma ~6 M instrucciones, ~1600 por dibujo, sin un punto único: `trFrame` 36 %, `trClipTri` 11 % (retrovisor), división por software 7 % (`__udivsi3`, en el propio juntar), `trCheckFrame` 6 %.
- Donde limita la GPU: tramos de ~550 dibujos a ~30 fps con `gpu_wait` 270-450 ms por segundo. Sin medir, con `gpu_timer` apagado.
- Recta del puente (~1850 dibujos, antes del túnel verde): 20 fps, traductor ~40 ms, capturador ~21 ms. Público: 15-20 fps, traductor 45-54 ms, capturador ~30 ms. Selección de autos: 41 fps, traductor 15 ms.

**Lo que sigue:** un camino corto en el traductor para un dibujo con el estado del anterior; sin división al juntar; retrovisor en la GPU; la vuelta de control con `no_join` para saber qué aportó juntar; `gpu_timer` donde limita la GPU.

**Camino corto (0.0.21.6, `stage3_0.0.21.6/rr1`, contra 0.0.21.5, mismos interruptores):** público 18,8 -> 25,5 fps, traductor 45 -> 30 ms; tramos difíciles 21,9 -> 24,5 fps, 35 -> 32 ms. Carrera normal (sin la selección de autos, que distorsiona la zona) 27,2 -> 28,6 fps. Sin imagen normal en la carrera. La recta del puente sigue en ~20 fps, traductor ~36 ms: los vértices son 14 ms, y 2973 de sus ~4300 vértices en CPU son del retrovisor, fuera del camino de GPU solo por su scissor. Referencia justa para los próximos cambios: `tests/perf/rr_hold_0.0.21.6.json`.

**Retrovisor en la GPU (0.0.22.1, confirmada como 0.0.23, `stage3_0.0.22.1/rr1`, contra 0.0.21.6, mismos interruptores):** el retrovisor es un solo tramo: una limpieza de color y profundidad que cubre su scissor y luego sus dibujos 3D, sin leer nada de la superficie. Por eso va a una imagen propia (`rtb`), desplazada a la esquina del recuadro, en la pasada de intermedias; el borde de la imagen lo recorta exacto, sin `CLP0`, `discard` ni `sceGxmSetRegionClip`, y la escena de la superficie no se corta. Un pegado en la superficie lo devuelve a su lugar. `scissor` en `3D vertices not moved by the GPU`: ~3000 -> 0. Tramos difíciles 24,5 -> 28,8 fps, traductor 31,7 -> 25,8 ms; público 25,5 -> 28,4 fps; carrera normal sin cambios, 29,3 fps. Vértices 4,4-5 ms. En el PC, instrucciones ARM reales: `rr_2722` -29 %, `rr_crowd` -20 %, `rr_2227` (sin retrovisor) +3,6 %, sobre todo en `trFrame`, no en la búsqueda. Límites: un tramo por frame; limpieza solo con posición.

**Ahora limita la GPU:** de los 26 ms del traductor, ~9 ms son espera a la GPU, 11-16 ms en la recta del puente (segundos a 24-26 fps) y 19-32 ms en un tramo corto de 19-25 fps cerca del inicio de la carrera (900 dibujos). El trabajo de CPU es ~16 ms. Referencia: `tests/perf/rr_hold_0.0.22.1.json`.

**GPU, escena por escena (0.0.23.1, `tests/perf/runs/0.0.23.1_rr_gpu_scenes`, frames completos de la carrera):** primera escena de superficie 16-21 ms, retrovisor ~1 ms, cadena de brillo 8-9 ms, segunda escena de superficie 5-6 ms con ~90 dibujos; 30-35 ms en total, contra 33 ms para 30 fps. La cadena de brillo son 12 dibujos sobre la imagen `088000` en 15 escenas: cuatro dibujos leen la superficie a la mitad, y luego cada pasada lee la imagen sobre la que dibuja, así que necesita su escena y una copia en `rtc`. Cada pasada depende de la anterior: no pueden compartir escena. Leer la imagen directamente en vez de la copia no es seguro: las zonas escritas y leídas se solapan, y la GPU guarda tiles mientras lee. Leer la superficie del frame anterior antes de la primera escena quitaría la segunda escena, pero el brillo se alimentaría de sí mismo. Sigue: no dibujar la imagen de abajo, y luego la imagen del brillo a tamaño PSP: solo se difumina, y cada escena de la cadena queda 4 veces más chica. `no_join` (0.0.23): GXM +66-100 %, traductor +14-35 %, GPU +2-8 %, fps -6 a -25 %.

**Sin imagen de abajo (0.0.23.3, `keep_base` contra sin ella, `tests/perf/runs/0.0.23.3_*`):** 522 mil píxeles menos por frame y el mismo tiempo de GPU (32,5 -> 32,2 ms, 34,4 -> 34,6, 35,3 -> 34,3), misma batería (34 mAh en 210 s, 37 en 200 s). Razón probable, sin probar: la limpieza la tapa y la GPU descarta píxeles ocultos antes de sombrearlos. La GPU está ocupada 95-98 % de la carrera.
**Dónde se va el frame, cada 10 s:** primera escena de superficie 16-18 ms, hasta 23-26 en los tramos lentos, con 50 % más píxeles; el brillo es igual en todos los frames: imágenes 5,8 ms, copias 3,1 ms, segunda escena de superficie 5,4 ms, 14 ms en total. El brillo sube el piso y la primera escena hace las caídas. Los segundos con la imagen de 64 x 64 `1fe000` (un mapa de reflejos, 0,8 ms en sí) están entre los lentos: falta medir los dibujos que la usan.
**No llamar funciones de energía no documentadas:** `scePowerGetBatteryElec`, que no está en los encabezados del SDK, reinició la Vita un segundo después del primer frame (0.0.23.2).

**Brillo a tamaño PSP (0.0.23.5, `tests/perf/runs/0.0.23.5_rr_bloom_psp`, contra 0.0.23.3 con la imagen de abajo):** imágenes 5,8 -> 3,0 ms, copias 3,1 -> 1,7 ms; GPU 32,5 -> 28,5 ms, 34,4 -> 30,0, 35,3 -> 30,6; fps 29,7 -> 32,4, 27,8 -> 29,8, 26,9 -> 29,1. El usuario no vio diferencia. La segunda escena de superficie sigue en 5,6 ms. `fx_log` sube GXM: apaga el camino corto en sus frames de muestra.
**Autos cerca (misma corrida, pocas muestras):** junto a tres autos al inicio de la carrera la primera escena de superficie tarda 32-34 ms, contra 24 ms unos segundos después, solo. `fx_log` ahí: lo que ilumina la GPU 779 mil píxeles, transparente con niebla 1403 mil en vez de 581, reflejos 295 mil, todo casi cero estando solo, en 3826 mil píxeles contra 3240. Los autos ponen varias capas transparentes, y tenerlos cerca suma unos 8-10 ms. Sigue: un programa más barato para esas capas (C.3).

## Orden desde aquí (2026-11-04)
0. **Hecho en 0.0.21:** un frame se salta solo si el capturador iba atrasado en una sincronización y el último frame no fue tomado, o si la GPU aún lee el registro a escribir: selección de autos ~28 -> ~35 frames HD por segundo, carrera y público iguales. Queda bajo ~45 porque los registros están ocupados con la GPU. Nunca escribir un registro que la GPU lee: congeló el hilo de dibujo y colgó la GPU.
1. **GPU (C.3), primero por decisión del usuario:** el capturador en el público queda en espera; B.3 falló y queda B.9 como idea. Ahí se atrasa: 7-10 frames completos por segundo, y las listas leídas tarde dan frames que fallan en el traductor (1 a 8 por corrida), cada uno visible como imagen normal durante varios frames del juego.
2. **GPU (C.3):** menos píxeles (9,4 pantallas por frame). En carrera normal traductor y GPU tardan casi lo mismo, ~33 ms.
3. **Hecho en 0.0.23:** retrovisor en la GPU, en una imagen propia. **En 0.0.24:** brillo a tamaño PSP.
4. **C.2 solo si el capturador limita después de B.3.**

## God of War Ghost of Sparta como nueva vara de medir (0.0.25, `hold_game`, 2026-11-05)
Menú ~4.500 dibujos y 66.000 órdenes por frame, juego 6.000-13.000 dibujos: el capturador tarda 65 ms en el menú y 100-150 ms jugando, 3 a 4 veces el presupuesto de 33 ms, antes de dibujar nada. Orden: quitar el tope de dibujos del registro para ver ambos en HD, aunque vayan lentos, y después optimizar el capturador con este juego.

Actualización (0.0.25.18, `hold_game` + `gate_sync`): el registro da frames completos (menú 9 de 9 por segundo, juego 7 de 7)
pero el juego va a la velocidad del capturador: menú ~9 fps (4.500 dibujos), juego 5-7 fps (6.500-9.000 dibujos). Aún sin HD:
el traductor deja fuera un atlas de sombras de 1024 de ancho (`161800`, `181800`, cuadros dibujados moviendo el destino 64
píxeles) y una imagen en `162000` con una máscara de prueba alfa que no sabe convertir.

## D. Leer la memoria del PSP: foto en el sync (propuesta, 2026-11-06)
**Por qué.** Tres hechos, todos de los logs:
- Leer la RAM del PSP desde la Vita es la parte lenta: ~2 us por línea de 64 bytes sin caché. KBENCH, 1 MB: 34,6 ms sin
  caché, 15,9 ms con memcpy, 7,2 ms por DMA; las copias de texturas por DMA llegan a ~512 MB/s con ~100 us por llamada
  (`findings.md`).
- God of War copia ~580 KB por frame del menú de la RAM a la VRAM con TRANSFER (59 copias, `tests/perf/runs/0.0.24.8_gow_menu_play`,
  frame 1450). El capturador repite cada copia palabra por palabra en memoria sin caché (`xferMirror`): unos 34 de sus 68 ms
  por frame del menú **[deducido: ~148.000 palabras x 0,2 us, sin medir el tiempo]**. Leer listas y sublistas suma ~4 ms.
- El juego reescribe esa RAM justo después del sync. Hoy una copia de RAM a VRAM encontrada después del sync deja fuera el
  frame (sin basura, pero tampoco HD); `hold_game` + `gate_sync` lo deja bien, pero el juego va a la velocidad del capturador.

**La idea.** En cada sync se copia a la Vita la memoria que usó el frame (la **foto**) y recién ahí el juego sigue; el
capturador recorre la foto después, como si hubiera llegado a tiempo. Nada de lo que el juego escriba después del sync puede
llegar al frame, y la foto se lee de la memoria propia de la Vita, con caché.
1. NativeLink anota el evento del sync **antes** de llamar al sync original (hoy lo anota después de la compuerta), deja que el
   juego espere a su GE como siempre y después espera a que la Vita marque la foto como lista (una palabra nueva del buzón;
   pasados ~20 ms el juego sigue y ese frame no se dibuja). En este modo no se usa la compuerta antes de encolar.
2. Un hilo de NativeDraw copia a su memoria las páginas de RAM previstas: DMA para los pedazos grandes, NEON para los de
   menos de ~11 KB. La copia corre mientras el juego espera a su GE, así que casi toda le sale gratis al juego.
3. Predicción: por dirección de inicio de lista, las páginas que tocaron sus últimos recorridos (en el laboratorio: los
   últimos 4 del mismo búfer, 8 páginas de margen a cada lado, pedazos a 4 páginas o menos unidos en una sola copia). Una
   lista nunca vista: el juego espera en el sync hasta que se recorre en vivo, una vez (como `hold_game`).
4. La copia del espejo de VRAM se encola en el sync y no en el flip, y el capturador también copia a su memoria las páginas
   que usa; las copias de RAM a VRAM pasan entonces de la foto a esa copia con un memcpy normal.
5. Una página fuera de la foto es un **fallo**: se lee en vivo, el frame no se publica, el estado vuelve al último frame
   limpio (`st_good`) y la página se suma a la predicción. En el laboratorio, el frame con fallo tenía los pedazos del frame
   siguiente: un frame con fallo nunca debe dibujarse.
6. Dos fotos (se recorre una mientras se toma la siguiente). Si las dos están ocupadas en un sync, ese frame queda sin foto y
   no se dibuja: el juego nunca espera al capturador.

Lo que deja de hacer falta cuando funcione (se quita entonces, `good-practices.md` §2): la compuerta antes de encolar y su
autocomprobación, las muestras de vida de lista, los lectores con su caché compartida, los reclamos y la comprobación por
adelantado, los descartes `late` y "copia después del sync", las muestras de vida de datos. La memoria de sublistas puede
quedar (ahorra interpretar), con comprobaciones que ahora son baratas.
Memoria: una foto son ~0,5-2 MB de páginas de RAM más ~0,6 MB de páginas de VRAM; dos ocupan ~3-5 MB. Se recuperan la caché
de los lectores (0,5 MB) y el pozo de sublistas. Hoy falta memoria (TODO): quizás haya que empezar con una sola foto.

**Modelo de tiempos** (`tools/perf/sync_model.py`: los logs de arriba y las mediciones de la fase 0 de NativeDraw 0.0.25.21;
la espera al GE sigue siendo estimada). fps del juego / frames HD por segundo que permite el capturador (traductor y GPU
aparte), DMA a memoria con caché a 400 MB/s y 75 us por llamada (`--recent`: predicción por los últimos frames):

| Juego | Hoy (compuerta antes de encolar) | `hold_game` + `gate_sync` | Retenido, copias por copia (fase 1) | Foto, DMA de la Vita |
|---|---|---|---|---|
| God of War, menú | 26 / 0 (copias después del sync) | 9,7 / 9,7 (medido) | 12,6 / 12,6 | 30 / 9,7 (captura 81 ms, 21 % de frames con fallo) |
| God of War, combate | 26 / 0 | 5,9 / 5,9 (medido) | 9,0 / 9,0 | 21 / 6,7 (captura 116 ms, copia 5,7 MB) |
| Ridge Racer, público | 46 / 31 (listas leídas tarde) | 31 / 31 | - | 34 / 34 por lista (80 % sin historia); 51 / 39 por los últimos frames |
| Ridge Racer, carrera | 60 / 60 | 60 / 60 | - | 60 / 60 |

En God of War la foto libera al juego pero el HD sigue a la velocidad del capturador: el límite es interpretar (fase 4).

La línea `memory bench` que cada corrida escribe al arrancar mide el DMA de la RAM del PSP a la memoria **sin caché** de la Vita en 480-570 MB/s, pero la CPU lee esa memoria a solo ~100 MB/s: la foto tiene que quedar en memoria con caché, y el DMA a memoria con caché (invalidando la caché) todavía no está medido (fase 0). Con el DMA a 139 MB/s (KBENCH, 1 MB) el combate de God of War baja a 22 fps y el público a 55: la copia ya no cabe en la
espera al GE. Una foto hecha por el GE del PSP en la zona compartida (VPROBE: 4,8 ms por MB) le cuesta al juego la copia
entera, porque corre después de las listas del frame: 25 / 23 en el menú, 52 / 37 en el público. Queda como alternativa.

**Laboratorio** (`tools/perf/sync_lab.c`, el capturador y el registro reales bajo qemu, `sh tools/perf/sync_lab.sh`): un juego
guionado en tres escenas; cada frame se compara con el mismo frame capturado con el juego retenido.

| Escena | Hoy, capturador al 0 % / 50 % / 90 % de la lista en el sync | Foto |
|---|---|---|
| `pieces` (God of War: pedazos armados en RAM, copiados a 8 lugares de VRAM reutilizados, reescritos después del sync) | todos fuera | 13 bien, 1 fuera (fallo al arrancar), 0 mal; 182 KB y 2,5 copias por frame |
| `pieces2` (lo mismo, pedazos en dos búferes) | todos fuera | 13 bien, 1 fuera, 0 mal |
| `ring` (vértices en un anillo que avanza cada frame) | todos bien | 34 bien, 4 fuera (fallos en cada vuelta del anillo), 0 mal |
| `pieces`, 8 búferes de lista por turno y 4 listas recordadas (Ridge Racer) | - | por lista: los 14 esperan un recorrido en vivo; por los últimos frames: 13 bien, 1 fuera, 0 mal |

**Resultados de la fase 0** (NativeDraw 0.0.25.21, God of War menú y combate con `hold_game` + `gate_sync`, carrera de Ridge
Racer; logs en
`tests/perf/runs/0.0.25.21_phase0`):
- `xferMirror`: 19,5 MB/s en todas las escenas. Menú 59 copias, 593 KB, **30 ms** por frame (se cumplen los 34 ms deducidos);
  combate 99 copias, 1.388 KB, **70 ms** por frame (cerca del 40 % del frame de 170 ms). Ridge Racer: una copia cada dos
  frames, con sus páginas casi todas sin copia (0,15 ms).
- `memory bench (DMA into cached)`: DMA de RAM del PSP a memoria con caché 429 MB/s; leerla de vuelta 213 MB/s (el doble que la
  sin caché, ~100); **una llamada de 4 KB 181 us**: un costo fijo de ~170 us por llamada, así que el DMA le gana a la copia de la
  CPU (91 MB/s) recién desde ~20 KB, y a la copia palabra por palabra de `xferMirror` desde ~3,5 KB. No hay función de usuario
  para invalidar la caché: la llamada de DMA la deja bien.
- Huella (páginas de 4 KB leídas por frame) y lo que copiaría la foto con la predicción del laboratorio: menú de God of War 322
  páginas (1,3 MB), copia 834 páginas (3,3 MB) en 17 pedazos; combate 511 páginas (2 MB), copia 1.458 páginas (5,7 MB) en 37
  pedazos. El margen de 8 páginas casi triplica la copia. **El 21-22 % de los frames tuvo un fallo** (5-13 páginas): esos no
  se dibujarían.
- Carrera de Ridge Racer: ~150-170 páginas por frame, pero **el 70-90 % de los frames no tenía historia**: el inicio de su lista
  cambia cada frame entre más de las 4 listas que se recuerdan, así que una historia por inicio de lista nunca se llena. El
  laboratorio (`PHOTO_LISTS=8`, `PHOTO_SLOTS=4`) hace lo mismo; con la historia de los últimos frames, sea cual sea su lista
  (`PHOTO_BYLIST=0`), ningún frame espera y ninguno sale mal, si el margen cubre el salto de la propia lista (`sync_lab check`
  lo mantiene). La 0.0.25.22 mide esa variante en la Vita (`photo footprint by the last frames`, con 8 y 4 páginas de margen).
- `photo_probe` (¿la copia frena al GE?): sin medir todavía (hace falta God of War sin `hold_game`).
- 0.0.25.22 (logs en el relevo de Windows, `stage3_0.0.25.22/gow1`, `rr1`): por los últimos frames sea cual sea su lista, con 8
  páginas de margen: menú de God of War 16 % y combate 15 % de frames con fallo (por lista 19-20 %), copiando un poco más (3,1 y
  6 MB); carrera de Ridge Racer: ningún frame sin historia (por lista 63 %), pero la copia es de 740 páginas (2,9 MB) para 171
  leídas, y 5 % de frames con fallo; con 4 páginas de margen 2,1 MB y 7 %. En la carrera la foto le costaría al juego ~4 ms
  (modelo 48 fps, hoy 60): ahí no aporta. Una llamada DMA de 4 KB tardó esta vez 72-76 us (181 us en la 0.0.25.21): el costo
  fijo varía. Por qué God of War sigue fallando en el 15 % de los frames todavía no se sabe: la 0.0.25.23 dice qué lecturas
  fallan y qué tan vieja es la página.
  fallan y qué tan vieja es la página.
  0.0.25.23: todas las páginas que fallaron no se habían leído nunca antes en la corrida (memoria nueva: copias de vértices del
  registro, orígenes de copias, texturas, algunas páginas de listas). Ninguna historia puede predecirlas: un frame que lee
  memoria nueva necesita otro camino (esperar como hold, o leer en vivo y comprobar que no se escribió después del sync).

**Fases** (cada una se comprueba en la Vita antes de la siguiente; la foto es una opción hasta que pase a ser la normal):
0. **Medir, sin cambiar nada** (versión de desarrollo): (a) tiempo de `xferMirror` por frame en `capture split`; (b) DMA de la
   RAM del PSP a la memoria con caché de la Vita, por llamada y MB/s de 4 KB a 1 MB, una vez al arrancar; (c) por frame, las
   páginas de RAM que toca el recorrido, las copias que forman y cuántas habría fallado la predicción del laboratorio (una
   línea de log); (d) una foto tomada y descartada en cada sync: ¿frena al GE (`sync_GE_us`, fps del juego)? (a) y (c) necesitan
   la misma corrida de God of War.
1. **Mejora rápida en el diseño de hoy:** `xferMirror` por DMA para los pedazos grandes, si (a) confirma los 34 ms. Sirve con
   `hold_game` y como la copia de las transferencias dentro de la foto.
2. **Foto con DMA de la Vita** (opción `photo`): NativeLink, el gancho del sync (evento antes del sync original, espera a la
   palabra nueva después); NativeDraw, el hilo de la foto, `vtr()` hacia la foto, copias desde la foto, fallos. Prueba:
   `sync_lab check` con el código real y después el menú y el combate de God of War y la carrera de Ridge Racer en la Vita.
3. **Foto por defecto** y quitar lo que reemplaza (`capture.c` más simple).
4. **El capturador mismo** (interpretar son 83 ms en el combate de God of War): dos fotos recorridas por dos hilos a la vez (hace
   falta el estado del GE en cada sync: NativeLink guarda los registros en cada sync, no cada 32 frames), y la GPU leyendo los
   vértices desde la foto (C.1).

Descartado: una vista con caché de la RAM del PSP en vivo (KBENCH: solo 1,7 veces más rápida, y líneas viejas mientras el
juego escribe); una foto de los 24 MB (más de 50 ms); copiar solo la lista en el sync (los vértices y los orígenes de las
copias están en otro lado).

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
