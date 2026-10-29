# Niveles de registro (GePatch dev65 + gepresent v13.9, 2026-10-29)

Los registros sirven para entender qué pasa, pero también cuestan rendimiento. Por eso hay tres niveles. Se elige uno en
la Vita con el archivo `ux0:data/gepresent/registro` (contenido `minimo`, `mediano` o `extremo`; **sin archivo: mediano**).
gepresent lo lee al abrir Adrenaline y se lo pasa a GePatch por el buzón (`SB[0x4E]`: 0, 1 o 2), así que vale para los dos.
Para cambiarlo: subir el archivo con el relevo (`enviar/ux0/data/gepresent/registro`) y cerrar las aplicaciones
(`cmd_*.txt` con `destroy`).

| Nivel | Para qué | Costo |
|---|---|---|
| **mínimo** | la versión que se publique | casi nada |
| **mediano** | el desarrollo de todos los días (por defecto) | bajo: contadores, una línea por segundo |
| **extremo** | análisis (fase 1 y similares) | se nota: con todo encendido (corrida 5) los frames lentos de GT en carrera subieron de ~17 % a 27 % |

## Qué hace cada nivel
| | mínimo | mediano | extremo |
|---|---|---|---|
| Log de GePatch cada 10 s (`ge_patch.log`) | sí | sí | sí |
| `ge_patch_captura.log` | 2 líneas cada 10 s (frames, comandos, tiempo de la Vita, syncs) | todas las líneas, cada segundo | ídem + espejo (muestreo), T4/vida de los datos, vida de la lista |
| Revisión de la memoria de sublistas (1 de cada 32, DISTINTAS) | no | sí | sí |
| Costo por frame para el juego (EV_FRAME: compuerta, espera al GE, espejo, ganchos) | no | sí (línea "impacto en el PSP") | sí |
| CPU libre del PSP (hilo de prioridad mínima, como vpjuego) | no | sí | sí |
| Procesador de los hilos del emulador (runClocks, cada segundo) | no | sí | sí |
| `ge_patch_frames.log` (una línea por frame, PSP + Vita) | no | no | sí |
| `ge_patch_tarde.log` (cada sync con la Vita atrasada, con sus tiempos) | no | no | sí |
| Vida de la lista (huellas al flip; GePatch revisa antes de encolar y en cada sync: EV_VIDA) | no | no | sí |
| Tiempos de lectura (propia 1/16, caché compartida 1/8) y de la memoria de sublistas (revisar, aplicar) | no | no | sí |
| Vida de los datos (vértices, texturas, paletas 1 y 2 frames después) | no | no | sí |
| Muestreo del espejo (cuántas páginas cambian) | no | no | sí |
| T4: avisos de la CPU del juego (vaciar caché, DMA) al buzón | no (solo cuenta) | no (solo cuenta) | sí |

Siguen aparte, con su propio archivo: T6 (`medir_ge`), bancos (`banco_nucleos`, `banco_v12`), `memo_revisar` (revisar
todas), `sin_memoria`, `captura_sin_lectores`, `off`.

## Interruptores de la fase de análisis (`ux0:data/gepresent/`; se apagan con `no` adentro, el relevo no borra)
| Archivo | Qué hace |
|---|---|
| `sin_compuerta` | el juego no espera a la Vita en el sync (GePatch solo cuenta cuántas veces iba atrasada) |
| `sin_espejo` | GePatch no copia la VRAM al espejo |
| `sin_libre` | sin el hilo de CPU libre del PSP |
| `sin_captura` | la Vita no recorre nada: marca las listas como capturadas (casi GePatch solo) |

## Archivos del nivel extremo
- `ge_patch_frames.log` (CSV, una línea por frame del juego): `frame_us`, `compuerta_us` (espera a la Vita), `esperas`,
  `sin_esperar`, `sync_GE_us` (espera al GE dentro del sync), `cpu_libre_x1000`, `espejo_us` / `espejo_paginas`,
  `ganchos_us`, y de la Vita: `comandos`, `draws`, `captura_us` (reloj), `captura_cpu_us` (procesador recibido),
  `aplicadas`, `comprobadas_lectores`, `lecturas_propias`, `de_lectores`, `listas_pendientes`, `syncs_atrasados`,
  `lista_dir`, `lista_kb`, `memoria_us` (todo memoTry), `aplicar_us`.
- `ge_patch_tarde.log` (CSV, cada sync en que la Vita terminó la lista después de que el juego llegó al sync): `espera_us`,
  `atraso_us`, `comandos`, `listas_antes`, `encolada_hace_us` y `ultimo_stall_hace_us` (respecto del sync),
  `vita_empezo_tras_us` (desde el encolado), `vita_recorrio_us`, `trabajo_us`, `lecturas_propias`, `de_lectores`,
  `aplicadas`, `procesador_us`. Las horas de la Vita se pasan a horas del PSP con la diferencia mínima de relojes.
- Resumen: `python3 tools/analisis_fase1.py <carpeta> [<carpeta> ...] [--tabla]` (una columna por corrida).
