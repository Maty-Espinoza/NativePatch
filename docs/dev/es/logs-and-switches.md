# Niveles de registro, interruptores y líneas del log

Qué escribe cada plugin en sus logs, qué significa cada línea y qué opciones cambian su comportamiento. Los números de
versión entre paréntesis dicen desde cuándo existe cada cosa (los viejos, `devNN`/`vNN`, se explican en `versions.md`).

## El archivo de opciones
Cada opción (nivel de log, nivel de volcado, interruptores, mediciones) es una línea `nombre=valor` de un único archivo,
`ux0:data/nativedraw/options.txt`, que se lee una vez al abrir Adrenaline (`nativedraw/options.h`). Una opción de
encendido/apagado está encendida con cualquier valor que no empiece con `no` (`hold_game=yes`); sin la línea está apagada,
o toma el valor por defecto que dice cada opción abajo. Las líneas que no son `nombre=valor` (comentarios, vacías) se
ignoran. Para cambiar una opción: subir el archivo completo con el relevo (`send/ux0/data/nativedraw/options.txt`) y
cerrar las aplicaciones (`cmd_*.txt` con `destroy`). Ejemplo:
```
log_level=medium
dump=auto
draw=yes
hd_mark=yes
hold_game=yes
gate_sync=yes
mirror_wait=yes
gpu_timer=yes
fx_log=no
```
Antes de NativeDraw 0.0.28.10 cada opción era un archivo propio en `ux0:data/nativedraw/` con el mismo nombre (y antes de
0.0.8.24, con nombre en español: `versions.md`); esos archivos ya no se leen y "Clean test leftovers" del instalador los
borra. Pendiente: cambiar las opciones desde el menú L+R+START (`../../../TODO.md`).

Cada juego tiene sus propios logs (0.0.8.27): abrir Adrenaline u otro juego pasa `nativedraw.log` y
`nativedraw_capture.log` a `*_prev.log`, igual que los de NativeLink. Solo se guarda un juego hacia atrás. Las líneas de "primera
vez" y las de detalle con tope (`translator: first frame with`, `giant draw`, `vertex_out (n)`, `late frame`) se cuentan por sesión
de Adrenaline, así que un segundo juego puede no repetirlas.

Los registros sirven para entender qué pasa, pero también cuestan rendimiento. Por eso hay tres niveles. Se elige uno
con la opción `log_level` (`minimum`, `medium` o `extreme`; **sin la línea: mediano**).
NativeDraw lo lee al abrir Adrenaline y se lo pasa a NativeLink por el buzón (`SB[0x4E]`: 0, 1 o 2), así que vale para los dos.

| Nivel | Para qué | Costo |
|---|---|---|
| **mínimo** | la versión que se publique | casi nada |
| **mediano** | el desarrollo de todos los días (por defecto) | bajo: contadores, una línea por segundo |
| **extremo** | análisis (fase 1 y similares; solo en compilación de desarrollo) | se nota: con todo encendido (corrida 5) los frames lentos de GT en carrera subieron de ~17 % a 27 % |

## Qué hace cada nivel
| | mínimo | mediano | extremo |
|---|---|---|---|
| Log de NativeLink cada 10 s (`nativelink.log`) | sí | sí | sí |
| `nativedraw_capture.log` | 2 líneas cada 10 s (frames, comandos, tiempo de la Vita, syncs) | todas las líneas, cada segundo | ídem + espejo (muestreo), T4/vida de los datos, vida de la lista |
| Revisión de la memoria de sublistas (1 de cada 32, MISMATCHES) | no | sí | sí |
| Costo por frame para el juego (EV_FRAME: compuerta, espera al GE, espejo, ganchos) | no | sí (línea "PSP impact") | sí |
| CPU libre del PSP (hilo de prioridad mínima, como vpjuego) | no | sí | sí |
| Procesador de los hilos del emulador (runClocks, cada segundo) | no | sí | sí |
| `nativedraw_frames.log` (una línea por frame, PSP + Vita) | no | no | sí |
| `nativedraw_late.log` (cada sync con la Vita atrasada, con sus tiempos) | no | no | sí |
| Vida de la lista (huellas al flip; NativeLink revisa antes de encolar y en cada sync: EV_VIDA) | no | no | sí |
| Tiempos de lectura (propia 1/16, caché compartida 1/8) y de la memoria de sublistas (revisar, aplicar) | no | no | sí |
| Vida de los datos (vértices, texturas, paletas 1 y 2 frames después) | no | no | sí |
| Muestreo del espejo (cuántas páginas cambian) | no | no | sí |
| T4: avisos de la CPU del juego (vaciar caché, DMA) al buzón | no (solo cuenta) | no (solo cuenta) | sí |

Siguen aparte, cada uno su propia opción: `draw` (presentador de la etapa 3: encendido, reemplaza la imagen siempre; apagado, solo con el menú
L+R+START abierto; además activa el registro del frame y su línea `record:` en `nativedraw_capture.log`; ver `../../agent/en/stage3-plan.md`), T6 (`ge_timing`), `memo_check` (revisar todas), `no_memo`,
`capture_no_readers`, `off`, `gpu_timer` (cronómetro de la GPU) y `replay` (`dumps.md`).

## Compilación de desarrollo y de producción
Las mediciones de desarrollo están dentro de `#ifdef NP_DEV`. Una compilación normal las tiene; `PROD=1`
(`tools/build.sh`, `nativedraw/build.sh`, `nativelink/Makefile`) las deja fuera. En producción:
- `extreme` cae a mediano: sin `nativedraw_frames.log`, sin `nativedraw_late.log`, sin medir la vida de la lista.
- T4 y T6 (`ge_timing`) y el cronómetro de la GPU (`gpu_timer.c`, `gpu_timer`, sus líneas `gpu times:` y `gpu: slow scene:`) no existen.
- Los interruptores `no_capture`, `no_lookahead`, `no_idle` y `gate_sync` no hacen nada.
- Los volcados, MISMATCHES y los niveles mínimo y mediano se quedan. La disposición del buzón no cambia (`how-it-works.md`).

## Volcados (NativeDraw 0.0.8.5)
- Compilación de desarrollo: un NOP del GE con `0x4E5044` pide un volcado, como L+R+SELECT (NPTEST).
- Compilación de desarrollo: sin memoria para un volcado, L+R+SELECT hace un `peek` (`dumps.md`).
Niveles de volcado en la opción `dump`: `off`, `screenshot`, `frame`, `sequence` (sin la línea)
o `auto` (se comparan 3 letras; `no` también lo apaga). Qué guarda cada uno, cuánto cuesta y cómo se leen con `tools/dump.py`: `dumps.md`.

Las mediciones de desarrollo están en `nativedraw/measures.h`, una línea cada una: opción, variable y qué hace. Para agregar una, se agrega una línea ahí.

## Interruptores de la fase de análisis (opciones de `options.txt`; se apagan con `no` o sin la línea)
| Opción | Qué hace |
|---|---|
| `no_gate` | el juego no espera a la Vita en el sync (NativeLink solo cuenta cuántas veces iba atrasada) |
| `no_mirror` | NativeLink no copia la VRAM al espejo |
| `no_idle` | sin el hilo de CPU libre del PSP |
| `no_capture` | la Vita no recorre nada: marca las listas como capturadas (casi NativeLink solo) |
| `gate_sync` | (v13.10) la espera a la Vita se queda en el sync, como antes de dev66 (para comparar); ver `etapa2_fase2_compuerta.md` |
| `no_lookahead` | (v13.12) los lectores no comprueban el plan por adelantado (solo al llegar a cada CALL, como antes); ver `etapa2_fase2_adelanto.md` |
| `mirror_wait` | (NativeLink 0.0.3) el flip del PSP espera a que termine la copia del espejo (interruptor 32 del buzón): en KOF 96 los sprites del frame están copiados cuando la Vita los pide. En la Vita del usuario está en `yes` |
| `thread_split` | (desarrollo) cada segundo en el log de captura: en qué núcleo corrieron el capturador, el hilo del registro y el hilo de dibujo de Adrenaline, cuánto, y cuántas veces los desplazaron, interrumpieron y movieron |
| `vert_split` | (desarrollo) separa el trabajo de vértices de 1 frame HD de cada 8: líneas `vertex split plain` y `vertex split warm`, resumidas por `tools/bottleneck.py` |
| `cpu_verts` | (desarrollo) ningún vértice va al programa de vértices de la GPU (`nd_v3`): todo dibujo 3D sigue por la CPU, para comparar. Se lee al abrir Adrenaline. |
| `cap_split` | (desarrollo) mide cada parte del capturador en todos los frames (cuesta ~0,7 us por lectura del reloj, ~64 ms por frame de combate de God of War, y en un juego pesado el capturador se atrasa: errores gráficos y la imagen normal; avisar antes de usarla): líneas `capture split` por tipo de frame, resumidas por `tools/bottleneck.py` Cuesta ~0,7 us por lectura de reloj y en un juego pesado atrasa al capturador: errores gráficos e imagen normal; avisar antes de usarlo. |
| `crowd_log` | (desarrollo) cada segundo en el log del capturador: comandos por tipo, saltos por caja envolvente, qué necesita un frame del anterior |
| `no_join` | (desarrollo) los dibujos del camino de GPU no se juntan, para comparar |
| `keep_hd` | (desarrollo) las imágenes que solo difuminan quedan al doble, para comparar |
| `fx_log` | (desarrollo) `bottleneck effects:` píxeles y dibujos del 3D sobre la superficie por efecto, 1 frame de cada 32; al final sus píxeles por programa de fragmentos (`fm` por `nd_fm`, `f2`, `fd`, `fa`, `fr`, `ft`, `fc`) y los del camino de GPU (a diferencia de `bottleneck pixels`, los cuenta: sus vértices se hacen otra vez en la CPU) |
| `hd_mark` | un círculo verde en la esquina superior derecha en los frames dibujados en HD (el traductor lo dibuja al final sobre la superficie). Se lee al abrir Adrenaline. También en producción |
| `walk_log` | (desarrollo) los diagnósticos del recorrido en el log del capturador (`base:`, `bad target:`, `vram copy:`, `register`, `walk suspect`) anotan hasta 4000 líneas cada uno en vez de unas decenas, más `vram sublist:` (una sublista de VRAM fuera de toda copia anotada) y `vram copy applied:` (una copia con partes no aplicadas al espejo). Satura el log: se pierden líneas |
| `walk_trace` | (desarrollo) caja negra en `ux0:pspemu/nativedraw_trace.log`: cada copia a VRAM con sus registros, cada CALL/JUMP/RET de una sublista de VRAM (última copia que la cubre, espejo igual o distinto de su origen en RAM), listas y flips, en dos mitades de 8 MB del bloque libre. El primer evento sospechoso (`TRIGGER`: registros distintos del GE real, BASE cambiado en una sublista de VRAM, una posición de copia cambiada, una copia fuera de la VRAM real, un salto imposible, el tope de la lista) la detiene 150 frames (o 10 s) después y se escriben las dos mitades, la más vieja primero. Líneas `CONTEXT`: la pila y 16 palabras del espejo y del origen en RAM alrededor del evento. Frena al capturador |
| `photo_probe` | (desarrollo) en cada sync que procesa el capturador, copia por DMA las páginas de RAM que copiaría la foto en el sync (la predicción de `photo footprint`) a un búfer que se descarta: comparar los fps del juego y `sync_GE_us` con y sin la opción para ver si la copia frena al GE del PSP. Corre cuando el capturador llega al sync, no en el sync mismo del juego. `probe KB/us` en la línea `photo footprint`. Las marcas de páginas y las líneas `photo footprint` solo corren con esta opción: cuestan una parte fija del trabajo del capturador en cada frame (en el banco, 17-40 % de sus instrucciones) |
| `gate_sync` | (desarrollo) la compuerta espera siempre en el sync del juego en vez de antes de encolar la siguiente lista. Con `hold_game` (espera hasta 200 ms) es la sincronización de frames PSP-Vita que deja bien las sublistas copiadas a VRAM de God of War (ver `how-it-works.md`, compuerta) |
| `hold_game` | (desarrollo) el juego espera a toda la cadena (compuerta hasta 200 ms, el flip hasta recorrer el frame, el capturador al presentador y a la GPU): todos los frames completos y en HD, y el juego más lento. Se lee al abrir Adrenaline. Para medir |
| `rec_cost` | (desarrollo) sigue grabando un frame ya incompleto, sin publicarlo, para medir cuánto cuesta grabar |
| `capture_core` | (v13.13) `0`, `1` o `2` fija el capturador a ese núcleo (antes de v13.13 siempre el 1); sin la línea o cualquier otra cosa: cualquiera de los 3. La línea `threads: capture` del log dice cuál se usó; ver `etapa2_ridge_racer.md` |

- Solo en desarrollo, cada segundo en `nativedraw_capture.log`: `vram copies (per frame): calls, KB, us`, las copias de RAM a VRAM que el capturador aplica a su espejo de la VRAM (`xferMirror`) y su tiempo; con `cap_split`, también la columna `xfer` de `capture split` (`tools/bottleneck.py` la resta del intérprete).
- Solo en desarrollo, cada segundo en `nativedraw_capture.log`: `photo footprint (per frame): frames, pages, copied, pieces, missed | frames with a miss, with no history | probe KB, us`: las páginas de 4 KB de RAM del PSP que leen el recorrido y el registro en cada frame (listas, vértices, índices, paletas, texturas en RAM, orígenes de copias; las texturas que copia el hilo del registro pueden contar en el frame siguiente), lo que copiaría la foto en el sync (páginas de los últimos 4 frames con la misma primera lista, 8 más a cada lado, pedazos unidos si están a 4 páginas o menos, como en `tools/perf/sync_lab.c`), sus pedazos y las páginas que le habrían faltado; los frames sin historia esperarían un recorrido en vivo.
- Solo en desarrollo, cada segundo en `nativedraw_capture.log`, después de `photo footprint`: `photo footprint by the last frames (per frame): 8 more copied, pieces, missed, frames with a miss | 4 more ...`: la misma predicción con los últimos 4 frames sea cual sea su primera lista, con 8 y con 4 páginas de margen (Ridge Racer cambia el inicio de su lista cada frame, así que una historia por lista casi nunca tiene la lista del frame).
- Solo en desarrollo, en `nativedraw_capture.log`: `photo misses (by the last frames, 8 more; pages in the second): lists, vertices, textures, transfers | never read before, up to 8 frames before, up to 64, longer ago` cada segundo con fallos: las páginas que se le escaparon a la predicción por los últimos frames, por quién las leyó (`vertices` es todo lo que el registro copia con `rgCopy`: vértices, índices, paletas) y por cuándo se leyeron antes; y las primeras 40 una por una, `photo miss: frame N page A read by ..., last read N frames before`.
- Solo en desarrollo, una vez: `memory bench (DMA into cached):` DMA de RAM del PSP a memoria con caché, leerla de vuelta con la CPU y el costo de una llamada DMA de 4 KB (en ns).
- Solo en desarrollo, una vez: `memory bench (PSP RAM->mirror, <tamaño> B, ns per call): CPU, DMA (código de error), words, cached->mirror` copia desde el código del juego al segundo espejo (en ese momento no hay páginas pedidas) de 512 B a 256 KB, como lo hace `xferMirror`; `bad` después de un valor: los bytes no llegaron. Después `memory bench (PSP RAM->mirror): memory types` de los dos bloques de memoria (0x80 normal sin caché, 0xD0 con caché) y si el origen cambió durante la prueba (`CHANGED`: las comprobaciones no valen).
- Solo en desarrollo, una vez, después de esas: `memory bench (256 B pieces into uncached memory, ns per KB): from cached filled by DMA, filled by DMA and read once, written by the CPU, from the mirror | in cached memory: 256 B copies, compares of 432 B, position check of 24 B vertices | clock read N ns`, los costos unitarios del trabajo de vértices del registro, cada uno sobre 128 KB (ns por KB / 1024 = ns por byte). Los cuatro primeros copian trozos de 256 bytes a memoria sin caché, como `rgVNow` copia los vértices a `tx`: desde memoria con caché recién llenada por DMA y no leída desde entonces, la misma leída una vez por la CPU antes (como queda la copia de las transferencias después de ir al espejo), memoria con caché que la CPU acaba de escribir, y el espejo (sin caché); incluyen dos lecturas del reloj cada 16 KB. Los tres siguientes corren en memoria con caché ya leída: copias de 256 bytes, `sceClibMemcmp` de 432 bytes iguales (el bloque de huesos de `rgBlock`) y `rgVBad` (la revisión de posiciones float) sobre vértices de 24 bytes. Al final, lo que cuesta una lectura del reloj.
- Solo en desarrollo, cada segundo en `nativedraw_capture.log` después de `3D vertices`, no con `cap_split` (sus lecturas del reloj caerían dentro de las partes): `record draw split (1 draw in 64, ns per draw without the clock / % of draws): timed=N clock=C ns | parte=ns/% ... | bytes per draw: tx_copy, tx_mirror, ram, copy | us per frame (x64): states, index to blocks`. En 1 dibujo de cada 64 (no los que mide la línea `record:`) `regDraw` lee el reloj entre sus partes. Cada parte da sus ns por dibujo medido (los dibujos en que no corrió cuentan 0) sin el reloj, y en cuántos de cada 100 dibujos medidos corrió; `clock` es lo que una lectura suma a cada parte (medido justo después de la última parte de cada dibujo medido, y restado). Las partes, en orden: `state` (copia y comparación del estado), `index` (la entrada del dibujo y la copia de sus índices), `gpu_check` (las revisiones de registros de `rgVGpu`; con vértices en RAM también si esperan), `source` (`rgSrc`: la página del espejo de cada página de VRAM), `from` (`rgFrom`: la búsqueda en la copia de las transferencias), `mirror` (posiciones float que no están en la copia de las transferencias: su copia del espejo a `rg_ord`), `check` (`rgVBad`), `tx_copy` y `tx_mirror` (la copia a `tx`, la memoria sin caché que lee la GPU, desde la copia de las transferencias o de bytes del espejo), `ram` (vértices en RAM, `rgVLater`), `copy` (vértices para el camino de la CPU al área del registro, `rgCopy`), `matrix`, `bones` y `lights` (`rgBlock`, solo cuando cambiaron o hay luz), `rest` (hasta la textura). Las partes por dibujo por los dibujos por cuadro son el tiempo de cada una; `index to blocks` (x64 como la línea `record:`, sobre los cuadros del segundo) se compara con su `vertices`, que lleva una lectura del reloj más por dibujo medido. Cómo leerla: cada parte contra sus instrucciones ARM en el PC (`tools/perf`) da el costo por instrucción; `tx_copy` por byte contra el memory bench de arriba dice si la copia de las transferencias sigue en la caché cuando el dibujo la lee; `check` contra la revisión de posiciones del bench, si paga la primera lectura de esos bytes. Costo: ~12 lecturas del reloj por dibujo medido, ~0.1 us por dibujo (~0.8 ms por cuadro de combate de God of War).

## Dibujo en la Vita (etapa 3, con `draw`; `nativedraw.log`, cada 10 s)
- Solo en compilación de desarrollo: `translator: result at frame N: <motivo>` en cada cambio del resultado de un frame
  (`ok` o el motivo de la imagen normal), los primeros 400. Con NPTEST da el motivo de cada escena; la escena `sync` lo alinea.
- `presenter: N frames in T ms through the surface (changes, with own copy), CPU us/frame (max) | nearly black: game,
  surface | checked N, different from the normal image M`: frames que pasaron por la superficie de NativeDraw, su
  costo en el hilo de Adrenaline y la revisión de frames distintos (1 de cada 4, solo si la GPU ya terminó la superficie).
- `translator: HD frames N, normal image M | reasons: ... | draws drawn N`: cuántos frames dibujó la Vita y por
  qué los otros fueron con la imagen normal (`no_new_frame`, `3d`, `primitive`, `format`, `blend`...; `held_back`: dibujable,
  pero no dibujado porque pasó menos de un segundo desde un frame que no lo era).
- Solo en compilación de desarrollo: `clocks: GPU N MHz (crossbar N), CPU N, bus N`, los relojes de la Vita que puso Adrenaline, cada 10 s.
- Solo en compilación de desarrollo: el interruptor `clocks` pone CPU 444 y crossbar 166 en cada línea `clocks:`.
- Solo en compilación de desarrollo: `translator: speed:` (reloj y memoria, una vez), `translator: per HD frame:` (vértices, draws, mismo estado que el anterior, luces, programas).
- Solo en compilaciones de desarrollo: `translator: per HD frame: 3D vertices not moved by the GPU, why:` (el primer motivo que aplica) y `translator: per HD frame: scissor ...` (cada scissor parcial, sus dibujos y vértices). `3D vertices (per frame)` del log de captura termina con lo que el registro no pudo dar a la GPU (`no room`) y, en toda compilación, los KB por frame que el registro leyó de la copia con caché de las transferencias (`KB read from the transfer copy`) y del espejo sin caché (`from the mirror`).
- Solo en compilaciones de desarrollo, ante problemas de direcciones en el capturador: `bad target:` un CALL o JUMP fuera de la RAM y la VRAM, cómo se formó la dirección y las órdenes anteriores; `base:` quién cambió el BASE de la captura; `register 10:` BASE reparado desde el registro real del GE; `vram copy:` cada copia nueva de RAM a VRAM y, cuando una lista salta a ella, si el espejo es igual al origen; `capture stopped` (vigilante) dónde estaba el capturador cuando su latido se detuvo por 2 s, y el estado de sus hilos.
- Solo en compilaciones de desarrollo, cada segundo: `bottleneck:` qué pasó con cada frame de Adrenaline (`drawn`, `repeated`, `behind`, `no_record`, `normal`), ms en el traductor y esperando a la GPU, las partes del traductor y la GPU por tipo de escena (con `gpu_timer`). `bottleneck pixels`: miles de píxeles enviados por programa, antes del test de z, en 1 de cada 4 frames HD (`mid`: los programas `nd_fd`, `nd_f2`, `nd_fa` y `nd_fr`; `base`: la imagen de abajo). Los dibujos del camino de GPU (`nd_v3`, `nd_v4`, `nd_v5`) no se cuentan: la CPU no ve sus vértices (en el combate de God of War son la mayoría de los píxeles de la superficie: los frames del banco en el PC le mandan ~8,4 millones, esta línea ve ~2,6). `bottleneck why generic`: de esos píxeles, por qué no van por el programa rápido. `python3 tools/bottleneck.py <corrida> [desde_s hasta_s]` dice qué limita cada segundo.
- Solo en compilaciones de desarrollo, cada segundo después de las líneas `bottleneck`: `gxm split (1 draw in 64, ns per call without the clock/calls per HD frame/longest us): frames=F timed=T clock=C ns | vres=ns/n/max fres=... u4=... u16=... ubig=... vprog=... fprog=... tex=... stream=... draw=... zs=... | t3set=... t3uni=... | us per HD frame: gxm=G t3set=S t3uni=U`: cuánto cuesta en la Vita cada tipo de llamada GXM del traductor. Se cuentan todas las llamadas; en 1 de cada 64 dibujos de los que no se unen por el camino corto (`timed`, en `frames` frames HD) cada llamada además se cronometra con una lectura del reloj antes y otra después. Tipos: `vres` y `fres` reservan el búfer de uniforms por defecto de vértices y de fragmentos, `u4`, `u16` y `ubig` ponen datos de uniforms de 4, 16 y más floats (`ubig`: los 252 floats de `nd_v5`), `vprog` y `fprog` ponen el programa de vértices y de fragmentos, `tex` la textura, `stream` el stream de vértices, `draw` es `sceGxmDraw`, `zs` profundidad, stencil y cull. `t3set` y `t3uni` son trabajo de CPU alrededor de ellas: leer las matrices, huesos y luces del dibujo (`t3Set`) y armar los uniforms de un programa de vértices de la GPU (`t3Uni`, sin sus propias llamadas GXM). Cada entrada da ns por llamada sin el reloj (`clock` es lo que cuesta una lectura, medida en los mismos dibujos; cada intervalo cronometrado lleva una lectura), llamadas por frame HD y la llamada más larga del segundo en us (una larga es una espera dentro de GXM, por ejemplo por espacio en sus búferes). `us per HD frame`: ns x llamadas, sumado sobre los tipos GXM, y de cada pieza. Cómo leerla: contra `gxm` de la línea `bottleneck:`, que además lleva `t3Set` de los dibujos del camino de la GPU, `t3Uni`, el resto del trabajo por dibujo y sus propias lecturas del reloj (~0.6 us cada una, más o menos una por dibujo); y los ns de cada tipo por sus llamadas en el PC (`gxm_ops` de `tools/perf/tr_iterate.sh`) dicen qué llamadas conviene quitar. Una llamada también puede dejar trabajo para el siguiente `sceGxmDraw`: comparar `draw` antes y después de un cambio. Costo: ~0.2 ms por frame de combate de God of War. `tools/bottleneck.py <corrida> [desde_s hasta_s]` la suma en esos segundos (ponderada por llamadas).
- Solo en desarrollo, cada segundo cuando el latido del capturador (SB[4]) o los eventos del PSP (SB[2]) se detienen 1 s, o siempre con `walk_log`: `capture health:` el estado de los hilos del capturador, del registro, del log y de los lectores (estado/espera/ms), dónde está el capturador y el avance del PSP. Lo escribe el hilo del presentador, así que dice qué lado se detuvo primero (en los cierres de God of War se detuvo primero el PSP, el capturador estaba libre). Con `hold_game`, el log del capturador también anota `hold: flip walk CUT` cuando el recorrido en el flip pasa de 150 ms.
- `translator: pace (PSP frames advanced per drawn frame): 0 (repeated) N, 1 N, 2 N...`: con muchos repetidos
  o saltos los juegos de Neo Geo destellan (hacen transparencias dibujando algo un frame sí y otro no).
- `gpu: at each frame start, our unfinished scenes: average, max | unfinished: previous frame N, 2 frames ago N |
  rewritten with the GPU unfinished: vertices and palettes N, normal image N`: cuánto va atrasada la GPU de la
  Vita y cuántas veces se reescribió algo que todavía podía estar leyendo (las texturas del registro, en la línea
  `record:` de `nativedraw_capture.log`: "with the GPU unfinished (measured)"). Debe ser 0: si no, cuadros rotos.
- `gpu: frames not sent because the GPU was behind N, waits for the GPU N ms | CPU in 10 s: translator N ms,
  different-frame check N ms | surface checks skipped (GPU unfinished) N` (NativeDraw 0.0.7): frames que no se le
  mandaron a la GPU porque no terminó el de hace 2 (se repite la superficie), tiempo esperándola antes de reescribir y
  tiempo de CPU del traductor y de la revisión.
- `gpu times: <type>: scenes, GPU ms (average, max) | draws per scene (swizzled), thousands of pixels per scene |
  in queue | ms histogram` y `gpu times: busy N ms of M (%)` (cronómetro, interruptor `gpu_timer`, desde
  0.0.8.13): cuánto tarda la GPU en cada tipo de escena (Adrenaline, imagen normal, traductor en la superficie, imagen
  intermedia). `gpu times: detail`: 16 escenas seguidas (A Adrenaline, N imagen normal, T traductor, I intermedia:
  draws, ms de GPU / ms en cola).
- `gpu: slow scene: <type>, draws, thousand pixels, ms` (0.0.8.20): cada escena nuestra de más de 30 ms.
  `translator: giant draw`: los primeros 40 draws de más de 2 pantallas de área.
- Motivo `vertex_out` (0.0.8.21): un vértice fuera de ±4096 o NaN (datos leídos tarde): el frame va con la imagen normal.
  En compilaciones de desarrollo, los primeros 40 también anotan `translator: vertex_out (n)` (frame, draw, tipo e índice de vértice) y los bytes del vértice
  y del draw; cada giant draw anota su tipo de vértice y sus bytes. `late frame <n>` (log del capturador, primeros 400): compuerta en su tope o
  captura de más de 33 ms. `record: texture limit` (primeros 20): cuántas texturas y bytes tenía el frame al llegar al tope.
- Motivo `bad_texture`: un dibujo lee una textura que el traductor no le puede dar. En compilaciones de desarrollo, los primeros 40
  que salen al dibujar (no de la revisión previa) anotan `translator: bad_texture (n): frame, draw of N, check K | dirección de la
  textura, formato, tamaño, ancho de fila, copia en el registro | destino | leída: imagen, sola, sh, lugar, superficie | dibujada:
  imagen, sola, lugar, superficie, región`. Comprobaciones: 1 la imagen leída es la que se dibuja y comparte su superficie con
  otras, 2 y 3 GXM rechazó la textura de una imagen, 4 otra vista de la imagen que se dibuja se superpone con lo que lee el
  dibujo, 5 una textura sin copia en el registro, 6 GXM rechazó la textura de una textura del registro.
- `translator: thread X` (al arrancar, `nativedraw.log`): el hilo propio del traductor (negativo: no pudo arrancar y no hay HD).
  Desde NativeDraw 0.0.28.11 el traductor graba en ese hilo y el de Adrenaline solo ejecuta lo grabado: en la línea
  `bottleneck:`, `translator` y la espera a la GPU son tiempo del hilo del traductor, no del de dibujo, y `gxm split` mide la
  grabación de cada llamada (su costo del lado de la GPU aparece como `draw` en el hilo de Adrenaline solo al ejecutar la
  escena). `gpu: ... rewritten with the GPU unfinished: vertices and palettes N` ahora cuenta las veces que el traductor esperó
  una superficie que la GPU aún leía (`vertices, palettes and commands waited for` en las primeras líneas): nunca las reescribe
  sin terminar.
- `screen:` (0.0.8.13, cada 1 s y solo si algo cambia): escenas y frames a la pantalla de Adrenaline y de ScePspemu,
  diálogo del sistema, estado de la superficie. `notifications:`: otro escribió una palabra de notificación de GXM.
- En `nativedraw_capture.log`, línea `textures (per frame):` (0.0.8.19): copias, KB y tiempo de DMA y de ordenar de la
  RAM y de la VRAM; llamadas al DMA; copias por tamaño; KB con swizzle por ancho de fila; KB iguales, cambiados o nuevos
  respecto de la última copia de ese grupo de 8 filas (huella por grupo); KB sin swizzle; texturas distintas por frame.
- En `nativedraw_capture.log`, línea `texture cache (per frame):`: KB reutilizados y copiados al caché, grupos que
  cambiaron, texturas de la RAM que fueron al registro, veces sin espacio y µs de revisión. `texture cache: no memory` al
  arrancar: funciona sin el caché.
- Cómo se sabe cuándo termina la GPU: cada escena propia escribe su número en una palabra de las notificaciones de GXM
  (la última que Adrenaline no usa, de 511 hacia abajo; el log la anota al empezar: `gpu: end of scenes at notification N`). Leer una superficie que la GPU no terminó da resultados falsos y `sceGxmFinish` para esperarla
  cuesta ~20 ms en KOF: por eso la revisión se salta en vez de esperar.

## Memoria de sublistas (v13.11)
Cada sublista guardada suma solo sus estadísticas distintas de 0 (hasta 48; con más no se guarda y el log lo avisa). En el
nivel mínimo, al aplicar una sublista solo se suman comandos y draws: lo demás de su línea no lo usa. Detalle en
`etapa2_fase2_estadisticas.md`.
v13.12: la línea de la memoria agrega `| lookahead/f: plan= checked= not_equal= late=` (comprobación por adelantado
de los lectores; `late` = veces que un lector se encontró con el capturador). Ver `etapa2_fase2_adelanto.md`.

## Compuerta (dev66 + v13.10, todos los niveles)
- `nativelink.log`, cada 10 s: `stats gate: <mode> | waits before enqueue=N (us each, max) |
  self_checks=N changed=N`, y una línea `gate: ...` cada vez que cambia de modo (60 comprobaciones limpias
  → antes de encolar; la lista cambió → en el sync).
- `nativedraw_capture.log`, línea "PSP impact" (mediano y extremo): al final `gate: <mode>, waits before enqueue/sec=N
  (us each)`. La espera total por frame sigue en `gate_us` (en el sync o antes de encolar).
- Extremo: `nativedraw_late.log` anota también las esperas antes de encolar, con tipo `e`.

## Perfil del frame (compilaciones de desarrollo, nivel extreme; `nativedraw/prof.h`)
Sigue un frame por todas las piezas. Cada segundo `nativedraw_capture.log` recibe líneas `prof <grupo>:`, con
`nombre=veces/promedio_us/maximo_us`. `python3 tools/prof_summary.py <corrida> [desde_s hasta_s]` las ordena; se puede elegir un
tramo. Para el cuadro completo activa también `gpu_timer` (GPU) y `ge_timing` (GE). Todo esto baja los fps: sirve para comparar
piezas, no como velocidad real. Compara con `draw` en `no` para separar capturar de dibujar.

| Grupo | Qué muestra |
|---|---|
| `psp` | El frame del juego: `game` (su trabajo) + `vblank` (tiempo libre) + `gate`, `sync_ge`, `mirror_cpu`, `hooks`; `ge_busy` (con `ge_timing`). `ge_mirror` sale 0 y no es confiable |
| `capture` | Capturador por frame; `draw`, `record` y `rec_*` se estiman de 1 de cada 8 dibujos, `read_*` de lecturas muestreadas. `list_work` y `list_at_stall`: trabajo y espera al juego, por lista |
| `record` | Hilo de registro: `wait_mirror`, `copy_vram`; `mirror_ge`: desde el flip del PSP hasta que el GE terminó su copia del espejo (visto entre listas por el capturador: es un tope) |
| `latency` | `flip_to_closed`, `flip_to_published`, `published_to_taken`, `enqueued_to_captured` |
| `adrenaline` | Su hilo de dibujo: `period` (16,7 ms a 60 fps), `presenter` (lo que agregamos), `image_dma`, `wait_gpu`, `checks` |
| `translator` | `check`, `textures`, `vertices`, `gxm`, `scenes`, `end_scene`, `all`. Un frame rechazado por la revisión no entra en `all` |
| `frames` | `complete`, `not_taken`, `no_record`, `gpu_behind`, `drawn`, `repeated`, `normal` |
| `gpu` | Tiempo por tipo de escena (con `gpu_timer`) |
| `cpu` | Procesador recibido por segundo por cada hilo; `free0-2`: lo que le queda a cada núcleo (hilos de prioridad mínima) |
| `limits` | Veces que se alcanzó un límite fijo: `stuck`, `queue_full`, `list_skipped`, `list_no_end`, `record_*` |

Líneas Líneas `drawing stuck:` (builds de desarrollo, `nativedraw.log`, primeras 30): Adrenaline no terminó ninguna escena en un segundo. Etapa (0 fuera de `hookEnd`, 1 su `sceGxmEndScene`, 2 revisiones, 3 el registro, 4 traduciendo ese dibujo, 5 `sceGxmBeginScene`, 6 `sceGxmEndScene`, 7 imagen normal), escenas enviadas y terminadas, lo que dice el sistema del hilo y los registros (`regState`).

`stuck:` (las primeras 60): una lista sin avanzar por 50 ms con listas nuevas esperando. La línea `stats frame` de NativeLink (cada 10 s) da vblank,
todo el tiempo en ganchos, listas y GE ocupado.

## Archivos del nivel extremo
- `nativedraw_frames.log` (CSV, una línea por frame del juego): `frame_us`, `gate_us` (espera a la Vita), `waits`,
  `no_wait`, `sync_GE_us` (espera al GE dentro del sync), `cpu_idle_x1000`, `mirror_us` / `mirror_pages`,
  `hooks_us`, y de la Vita: `commands`, `draws`, `capture_us` (reloj), `capture_cpu_us` (procesador recibido),
  `applied`, `checked_readers`, `own_reads`, `from_readers`, `pending_lists`, `late_syncs`,
  `list_addr`, `list_kb`, `memory_us` (todo memoTry), `apply_us`.
- `nativedraw_late.log` (CSV, cada sync en que la Vita terminó la lista después de que el juego llegó al sync): `wait_us`,
  `delay_us`, `commands`, `lists_before`, `enqueued_ago_us` y `last_stall_ago_us` (respecto del sync),
  `vita_started_after_us` (desde el encolado), `vita_walked_us`, `work_us`, `own_reads`, `from_readers`,
  `applied`, `processor_us`. Las horas de la Vita se pasan a horas del PSP con la diferencia mínima de relojes.
- Resumen: `python3 tools/phase1_analysis.py <carpeta> [<carpeta> ...] [--table]` (una columna por corrida).
