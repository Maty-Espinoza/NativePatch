# Etapa 2: memoria de sublistas (gepresent v12.10–v12.13, 2026-10-28)

Sigue a `etapa2_avance.md` (modo sombra + capturador, dev61 + v12.9) y a `etapa2_diagnostico_v12.11.md` (dónde se va
el tiempo). Marcas: [medido], [deducido]. **Commit:** GePatch 0.2-dev61 (sin cambios) + **gepresent v12.13**.

## En simple
- La Vita lee las listas de dibujo del juego mientras corre (todavía sin dibujar). Lo caro era recorrerlas instrucción por
  instrucción, y GT repite casi todo de un frame a otro.
- **Memoria de sublistas:** la primera vez que la lista principal llama una sublista, la Vita la recorre y guarda una
  copia de sus trozos de memoria y lo que deja hecho. Las siguientes veces, si la memoria es idéntica y la llaman con el
  mismo estado (textura, tipo de vértice, vértices), aplica lo guardado sin recorrerla.
- **Resultado:** GT va a 56–60 fps en toda la vuelta, incluido el puente (antes 36–40); audio e imagen bien (usuario).

## Cómo funciona (tools/gepresent/captura.c, sección "memoria de sublistas")
- Solo para CALL a la RAM desde el nivel de arriba (sp = 0). Clave: (dirección, BASE, OFFSET de entrada); hasta 8
  versiones por clave en lugares seguidos de una tabla de 512 (una por estado de entrada).
- **Grabar** (`recBegin` / `recLine` / `recEnd`): mientras se recorre, cada línea de 64 bytes nueva que toca (incluidas
  las sublistas anidadas) se copia a un depósito (8192 líneas, 512 KB) en hasta 32 tramos. Al volver (RET a sp = 0) se
  guarda: registros escritos y su valor final (hasta 64), matrices tocadas, VADDR/IADDR (absoluto si se fijó dentro, o
  cuánto avanzó), dirty/vset/último primitivo/destino/paleta, hasta 32 texturas, y la diferencia de los contadores (Stats
  antes de `chg[]`, sin los que cuenta el hilo: frames, tiempos, eventos).
- **Estado de entrada** (`recRead`, `recDraw`): registros leídos antes de escribirlos dentro (0x12 tipo de vértice, 0xD3,
  0x1E, 0x9C/0x9D destino, 0xA0/0xA8 textura, 0xB0/0xB1 paleta, 0xB2–0xB5 copias; hasta 16) y si dibuja con los
  vértices/índices de entrada. Se comprueban antes de usarla (`depsOk`).
- **Usar** (`memoTry`): estado de entrada igual → se comparan las líneas (palabra por palabra, leídas como siempre: de la
  caché de los lectores o de la RAM del PSP) con la copia → iguales: `memoApply` + sumar contadores, y se sigue después
  del CALL. Distinta → se recorre y se graba encima (`victim`). Tramo cerca del stall (puede no estar escrito) → se recorre.
- **Revisión** (siempre, 1 de cada 32 aplicables; todas con `ux0:data/gepresent/memo_revisar`): se calcula lo guardado y
  además se recorre; si el estado final difiere (registros, matrices, índices de matriz, vértices/índices) cuenta
  `DISTINTAS` y escribe `memoria: sublista ... DISTINTA` (las 10 primeras). `ux0:data/gepresent/sin_memoria` la apaga.
- Aproximado (solo estadísticas): "cambios" y lotes del primer draw de una sublista aplicada dependen del estado de entrada;
  `chg[]` (registros "otro" más frecuentes) no suma las sublistas aplicadas.
- Memoria: 1488 KB en un bloque `gepresent_memoria` (tabla 512 × ~1,9 KB + depósito 512 KB). Cuando el depósito se llena
  se vacía entero (`deposito_vaciado`, ~40 veces en una vuelta de GT).
- Línea del log: `memoria de sublistas (por frame): aplicadas comandos_sin_recorrer (% de todos) lineas_comprobadas |
  nuevas cambiaron otra_entrada sin_guardar | por seg: deposito_vaciado revisadas DISTINTAS`.
- **Prueba fuera de la Vita:** `tools/gepresent/prueba_memoria.c` (listas sintéticas, ARM bajo qemu en el contenedor;
  instrucciones en su cabecera): sin memoria / con memoria / revisando todas deben dar las mismas estadísticas y el mismo
  estado. Cubre: sublista que cambia, llamada con otros vértices, otra textura, la misma dos veces, anidadas, BASE que se
  filtra a la salida. Encontró un cuelgue que venía de v12.9 (corregido en v12.12): `texAdd` se colgaba con `gen = 0`
  (antes del primer flip); ahora `gen` empieza en 1.

## Versiones
| Versión | Qué | md5 |
|---|---|---|
| v12.10 | tiempo de procesador por hilo (runClocks), lotes sin cortar por TEXLEVEL | `36a120f4…` |
| v12.11 | diagnóstico: segunda pasada cada 5 s, huellas de sublistas repetidas (quitado en v12.12) | `562d6354…` |
| v12.12 | memoria de sublistas + revisión 1/32 + `gen = 1` | `cb12626d…` |
| v12.13 | contadores de la memoria corregidos (al aplicar se sumaban frames y tiempos) | `d4141c1f…` |

## Resultados en GT (UCUS-98632), una vuelta con el puente [medido]
Logs: `vitabridge/etapa2_v12.10/gt1/`, `etapa2_v12.11/gt1/`, `etapa2_v12.12/gt1/`, `etapa2_v12.13/gt1/`.
| | v12.10 (sin memoria) | v12.13 (con memoria) |
|---|---|---|
| fps de GePatch (cada ~10 s), el más bajo | 37,1 | 55,7 (casi todo 59–60) |
| drawsync que esperaron a la Vita / que llegaron a 5 ms | 5122 / 2476 | 523 / 188 |
| espera media por drawsync | 961 µs | 77 µs |
| captura, ms de reloj por frame en carrera (media / peor segundo) | ~17 / ~29 | ~9,3 / ~16,5 |
| procesador del capturador (ms por segundo) | ~700 | ~475 |
- En carrera la memoria salta **~78 % de los comandos** (62–88 %; en el puente ~55 000 de ~67 000); ~126 sublistas
  aplicadas y ~1800 líneas comprobadas por frame; casi nunca cambian (`cambiaron` 0) ni se llaman con otro estado (~1).
- Revisión: **~28 700 sublistas revisadas, 0 distintas**.
- SH no se volvió a probar con la memoria (con v12.10 ya iba holgado: ~4,5 ms por frame, sin esperas).

## Lo que falta / ideas
1. **Espejo de la VRAM** (GT y RR): GT tiene 80–140 sublistas por frame en la VRAM (hoy se saltan), ~95 % de las texturas
   y ~80 % de las paletas. Sin espejo la captura de GT está incompleta. Ver `arquitectura_dibujo_en_vita.md` §6.
2. T2 (caja negra), T4, T6; grabación GXM en 2–3 hilos con afinidad fija; después, dibujar (etapa 3).
3. El bucle de interpretación sigue costando ~360 ns por comando: acortarlo (tramo de RAM una vez por línea, menos
   contadores) bajaría lo que no se salta.
4. Los lectores (núcleos 0 y 2) siguen recorriendo todo, aunque la memoria lo salte: podrían recorrer solo lo que no está
   guardado, o calcular huellas de líneas.
5. Para el dibujo: lo guardado por sublista será lo ya traducido a GXM. Además del estado de entrada de hoy, la traducción
   dependerá de matrices y otros registros que la sublista no fija (definir al diseñar la etapa 3). Los vértices y
   texturas en la RAM los lee la GPU directamente, así que no tienen que ser iguales; solo sus direcciones.
6. Mediciones no confiables: el núcleo 0 de la línea `procesador` (el gancho de CreateThread no ve los hilos creados
   antes de cargar gepresent).
