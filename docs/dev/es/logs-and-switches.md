# Niveles de registro, interruptores y líneas del log

Qué escribe cada plugin en sus logs, qué significa cada línea y qué archivos de `ux0:data/nativedraw/` cambian su
comportamiento. Los números de versión entre paréntesis dicen desde cuándo existe cada cosa (los viejos, `devNN`/`vNN`,
se explican en `versions.md`). Pendiente: cambiar los interruptores desde el menú L+R+START (`../../../TODO.md`);
mientras tanto son archivos.

Los registros sirven para entender qué pasa, pero también cuestan rendimiento. Por eso hay tres niveles. Se elige uno en
la Vita con el archivo `ux0:data/nativedraw/registro` (contenido `minimo`, `mediano` o `extremo`; **sin archivo: mediano**).
NativeDraw lo lee al abrir Adrenaline y se lo pasa a NativeLink por el buzón (`SB[0x4E]`: 0, 1 o 2), así que vale para los dos.
Para cambiarlo: subir el archivo con el relevo (`enviar/ux0/data/nativedraw/registro`) y cerrar las aplicaciones
(`cmd_*.txt` con `destroy`).

| Nivel | Para qué | Costo |
|---|---|---|
| **mínimo** | la versión que se publique | casi nada |
| **mediano** | el desarrollo de todos los días (por defecto) | bajo: contadores, una línea por segundo |
| **extremo** | análisis (fase 1 y similares) | se nota: con todo encendido (corrida 5) los frames lentos de GT en carrera subieron de ~17 % a 27 % |

## Qué hace cada nivel
| | mínimo | mediano | extremo |
|---|---|---|---|
| Log de NativeLink cada 10 s (`nativelink.log`) | sí | sí | sí |
| `nativedraw_captura.log` | 2 líneas cada 10 s (frames, comandos, tiempo de la Vita, syncs) | todas las líneas, cada segundo | ídem + espejo (muestreo), T4/vida de los datos, vida de la lista |
| Revisión de la memoria de sublistas (1 de cada 32, DISTINTAS) | no | sí | sí |
| Costo por frame para el juego (EV_FRAME: compuerta, espera al GE, espejo, ganchos) | no | sí (línea "impacto en el PSP") | sí |
| CPU libre del PSP (hilo de prioridad mínima, como vpjuego) | no | sí | sí |
| Procesador de los hilos del emulador (runClocks, cada segundo) | no | sí | sí |
| `nativedraw_frames.log` (una línea por frame, PSP + Vita) | no | no | sí |
| `nativedraw_tarde.log` (cada sync con la Vita atrasada, con sus tiempos) | no | no | sí |
| Vida de la lista (huellas al flip; NativeLink revisa antes de encolar y en cada sync: EV_VIDA) | no | no | sí |
| Tiempos de lectura (propia 1/16, caché compartida 1/8) y de la memoria de sublistas (revisar, aplicar) | no | no | sí |
| Vida de los datos (vértices, texturas, paletas 1 y 2 frames después) | no | no | sí |
| Muestreo del espejo (cuántas páginas cambian) | no | no | sí |
| T4: avisos de la CPU del juego (vaciar caché, DMA) al buzón | no (solo cuenta) | no (solo cuenta) | sí |

Siguen aparte, con su propio archivo: `dibujar` (presentador de la etapa 3: con el archivo reemplaza la imagen siempre; sin él, solo con el menú
L+R+START abierto; además activa el registro del frame y su línea `registro:` en `nativedraw_captura.log`; ver `../../agent/es/stage3-plan.md`), T6 (`medir_ge`), `memo_revisar` (revisar todas), `sin_memoria`,
`captura_sin_lectores`, `off`, `medir_gpu` (cronómetro de la GPU) y `reproducir` (`dumps.md`).

## Volcados (NativeDraw 0.0.8.5)
Niveles de volcado en `ux0:data/nativedraw/volcado`: `desactivado`, `pantallazo`, `frame`, `secuencia` (sin el archivo)
o `auto`. Qué guarda cada uno, cuánto cuesta y cómo se leen con `tools/volcado.py`: `dumps.md`.

## Interruptores de la fase de análisis (`ux0:data/nativedraw/`; se apagan con `no` adentro, el relevo no borra)
| Archivo | Qué hace |
|---|---|
| `sin_compuerta` | el juego no espera a la Vita en el sync (NativeLink solo cuenta cuántas veces iba atrasada) |
| `sin_espejo` | NativeLink no copia la VRAM al espejo |
| `sin_libre` | sin el hilo de CPU libre del PSP |
| `sin_captura` | la Vita no recorre nada: marca las listas como capturadas (casi NativeLink solo) |
| `compuerta_sync` | (v13.10) la espera a la Vita se queda en el sync, como antes de dev66 (para comparar); ver `etapa2_fase2_compuerta.md` |
| `sin_adelanto` | (v13.12) los lectores no comprueban el plan por adelantado (solo al llegar a cada CALL, como antes); ver `etapa2_fase2_adelanto.md` |
| `espejo_espera` | (NativeLink 0.0.3) el flip del PSP espera a que termine la copia del espejo (interruptor 32 del buzón): en KOF 96 los sprites del frame están copiados cuando la Vita los pide. En la Vita del usuario está en `si` |
| `nucleo_captura` | (v13.13) `0`, `1` o `2` fija el capturador a ese núcleo (antes de v13.13 siempre el 1); sin archivo o `libre`: cualquiera de los 3. La línea `hilos: captura` del log dice cuál se usó; ver `etapa2_ridge_racer.md` |

## Dibujo en la Vita (etapa 3, con `dibujar`; `nativedraw.log`, cada 10 s)
- `presentador: N frames en T ms por la superficie (cambios, con copia propia), CPU us/frame (max) | casi negros: juego,
  superficie | revisados N, distintos de la imagen normal M`: frames que pasaron por la superficie de NativeDraw, su
  costo en el hilo de Adrenaline y la revisión de frames distintos (1 de cada 4, solo si la GPU ya terminó la superficie).
- `traductor: frames HD N, con la imagen normal M | motivos: ... | draws dibujados`: cuántos frames dibujó la Vita y por
  qué los otros fueron con la imagen normal (`sin_frame_nuevo`, `3d`, `primitiva`, `formato`, `mezcla`...).
- `traductor: ritmo (frames del PSP que avanzó cada frame dibujado): 0 (repetido) N, 1 N, 2 N...`: con muchos repetidos
  o saltos los juegos de Neo Geo destellan (hacen transparencias dibujando algo un frame sí y otro no).
- `gpu: al empezar cada frame, escenas nuestras sin terminar: promedio, max | sin terminar: el frame anterior N, el de
  hace 2 N | reescritos con la GPU sin terminar: vértices y paletas N, imagen normal N`: cuánto va atrasada la GPU de la
  Vita y cuántas veces se reescribió algo que todavía podía estar leyendo (las texturas del registro, en la línea
  `registro:` de `nativedraw_captura.log`: "con la GPU sin terminar (medido)"). Debe ser 0: si no, cuadros rotos.
- `gpu: frames no mandados por la GPU atrasada N, esperas a la GPU N ms | CPU en 10 s: traductor N ms, revisión de frames
  distintos N ms | revisiones de la superficie saltadas (GPU sin terminar) N` (NativeDraw 0.0.7): frames que no se le
  mandaron a la GPU porque no terminó el de hace 2 (se repite la superficie), tiempo esperándola antes de reescribir y
  tiempo de CPU del traductor y de la revisión.
- `gpu tiempos: <tipo>: escenas, GPU ms (promedio, max) | draws por escena (con swizzle), miles de píxeles por escena |
  en cola | histograma de ms` y `gpu tiempos: ocupada N ms de M (%)` (cronómetro, interruptor `medir_gpu`, desde
  0.0.8.13): cuánto tarda la GPU en cada tipo de escena (Adrenaline, imagen normal, traductor en la superficie, imagen
  intermedia). `gpu tiempos: detalle`: 16 escenas seguidas (A Adrenaline, N imagen normal, T traductor, I intermedia:
  draws, ms de GPU / ms en cola).
- `gpu: escena lenta: <tipo>, draws, miles de píxeles, ms` (0.0.8.20): cada escena nuestra de más de 30 ms; las 3
  primeras piden un volcado. `traductor: draw gigante`: los primeros 40 draws de más de 2 pantallas de área.
- Motivo `vertice_fuera` (0.0.8.21): un vértice fuera de ±4096 o NaN (datos leídos tarde): el frame va con la imagen normal.
- `pantalla:` (0.0.8.13, cada 1 s y solo si algo cambia): escenas y frames a la pantalla de Adrenaline y de ScePspemu,
  diálogo del sistema, estado de la superficie. `notificaciones:`: otro escribió una palabra de notificación de GXM.
- En `nativedraw_captura.log`, línea `texturas (por frame):` (0.0.8.19): copias, KB y tiempo de DMA y de ordenar de la
  RAM y de la VRAM; llamadas al DMA; copias por tamaño; KB con swizzle por ancho de fila; KB iguales, cambiados o nuevos
  respecto de la última copia de ese grupo de 8 filas (huella por grupo: lo que ahorraría un caché); KB sin swizzle;
  texturas distintas por frame.
- Cómo se sabe cuándo termina la GPU: cada escena propia escribe su número en una palabra de las notificaciones de GXM
  (la última que Adrenaline no usa, de 511 hacia abajo; el log la anota al empezar: `gpu: fin de escenas en la
  notificación N`). Leer una superficie que la GPU no terminó da resultados falsos y `sceGxmFinish` para esperarla
  cuesta ~20 ms en KOF: por eso la revisión se salta en vez de esperar.

## Memoria de sublistas (v13.11)
Cada sublista guardada suma solo sus estadísticas distintas de 0 (hasta 48; con más no se guarda y el log lo avisa). En el
nivel mínimo, al aplicar una sublista solo se suman comandos y draws: lo demás de su línea no lo usa. Detalle en
`etapa2_fase2_estadisticas.md`.
v13.12: la línea de la memoria agrega `| adelanto/f: plan= comprobadas= no_iguales= tarde=` (comprobación por adelantado
de los lectores; `tarde` = veces que un lector se encontró con el capturador). Ver `etapa2_fase2_adelanto.md`.

## Compuerta (dev66 + v13.10, todos los niveles)
- `nativelink.log`, cada 10 s: `stats compuerta (dev66): <modo> | esperas antes de encolar=N (us c/u, max) |
  autocomprobaciones=N cambiaron=N`, y una línea `compuerta: ...` cada vez que cambia de modo (60 comprobaciones limpias
  → antes de encolar; la lista cambió → en el sync).
- `nativedraw_captura.log`, línea "impacto en el PSP" (mediano y extremo): al final `compuerta (v13.10): <modo>, esperas
  antes de encolar/seg=N (us c/u)`. La espera total por frame sigue en `compuerta_us` (en el sync o antes de encolar).
- Extremo: `nativedraw_tarde.log` anota también las esperas antes de encolar, con tipo `e`.

## Archivos del nivel extremo
- `nativedraw_frames.log` (CSV, una línea por frame del juego): `frame_us`, `compuerta_us` (espera a la Vita), `esperas`,
  `sin_esperar`, `sync_GE_us` (espera al GE dentro del sync), `cpu_libre_x1000`, `espejo_us` / `espejo_paginas`,
  `ganchos_us`, y de la Vita: `comandos`, `draws`, `captura_us` (reloj), `captura_cpu_us` (procesador recibido),
  `aplicadas`, `comprobadas_lectores`, `lecturas_propias`, `de_lectores`, `listas_pendientes`, `syncs_atrasados`,
  `lista_dir`, `lista_kb`, `memoria_us` (todo memoTry), `aplicar_us`.
- `nativedraw_tarde.log` (CSV, cada sync en que la Vita terminó la lista después de que el juego llegó al sync): `espera_us`,
  `atraso_us`, `comandos`, `listas_antes`, `encolada_hace_us` y `ultimo_stall_hace_us` (respecto del sync),
  `vita_empezo_tras_us` (desde el encolado), `vita_recorrio_us`, `trabajo_us`, `lecturas_propias`, `de_lectores`,
  `aplicadas`, `procesador_us`. Las horas de la Vita se pasan a horas del PSP con la diferencia mínima de relojes.
- Resumen: `python3 tools/analisis_fase1.py <carpeta> [<carpeta> ...] [--tabla]` (una columna por corrida).
