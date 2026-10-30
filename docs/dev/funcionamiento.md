# Cómo funcionan NativeLink y NativeDraw (NativeLink 0.0.2 + NativeDraw 0.0.2, 2026-10-29)

Documento de referencia de la versión actual (reemplaza a `docs/changelog/CONTEXTO_IA.md`, que describe GePatch hasta
dev49). La hoja de ruta está en `arquitectura_dibujo_en_vita.md` §6; cómo se trabaja, en `FLUJO_DE_TRABAJO.md`; los
registros, en `registros.md`. El detalle de cómo se llegó a cada pieza está en `docs/changelog/etapa2_*.md`.

## En simple
- La PS Vita tiene adentro el hardware de una PSP. Adrenaline corre los juegos de PSP en ese hardware: el procesador del
  PSP corre el juego y su chip gráfico (el **GE**) dibuja la imagen chica de siempre (480x272).
- **NativeLink** es un plugin que corre en el PSP, junto al juego. No cambia nada de lo que el juego dibuja: cada vez que
  el juego le manda al GE una lista de instrucciones de dibujo, NativeLink lo anota en una "libreta" (el buzón) que está
  en una memoria que la Vita también puede leer.
- **NativeDraw** es un plugin de la Vita. Lee la libreta y recorre las mismas instrucciones desde la Vita. Hoy solo las
  recorre y mide cuánto trabajo habría; desde la etapa 3 las va a dibujar con la GPU de la Vita en alta resolución.
- La Vita no ve la memoria de video del PSP (VRAM), y algunos juegos guardan ahí instrucciones y texturas: en cada cuadro
  NativeLink le pide al GE que copie a la memoria normal los pedazos que la Vita pidió (el **espejo**).
- Si la Vita va atrasada, NativeLink hace esperar al juego un poco (hasta 5 ms) antes de que reuse una lista, para que la
  Vita no lea instrucciones que ya cambiaron (la **compuerta**).

## Piezas y dónde está cada una
| Pieza | Archivo | Corre en |
|---|---|---|
| Ganchos del GE, buzón, espejo, compuerta, telemetría | `nativelink/main.c` | PSP (MIPS), dentro del juego |
| Log de NativeLink (lo escribe NativeDraw) | `nativelink/log.c`, `log.h` | PSP |
| Listas propias del GE (copias del espejo) | `nativelink/gu.c` | PSP |
| Arranque, log de NativeLink, hilos del emulador | `nativedraw/nativedraw.c` | Vita (ARM), proceso de Adrenaline |
| Capturador, lectores, memoria de sublistas, espejo, registros | `nativedraw/captura.c` | Vita |
| Prueba del capturador fuera de la Vita (qemu) | `nativedraw/prueba_memoria.c` | PC |
| Instalador | `instalador/main.c` | Vita (app) |

## Cómo se cargan
- **NativeLink:** Adrenaline 8 lo carga al abrir un juego si está activado en `ux0:pspemu/seplugins/plugins.txt`
  (`game, ms0:/seplugins/nativelink.prx, on`; ver `adrenaline_notas.md`). Abrir el juego manteniendo **L** o SELECT lo
  deja apagado (con L, Adrenaline 8 además apaga todos los plugins). Mantener **R** al abrir: log síncrono (cada línea
  al instante, para cuelgues).
- **NativeDraw:** taiHEN lo carga en el proceso de Adrenaline (sección `*PSPEMUCFW` de `config.txt`), al abrir Adrenaline.
  Para que cargue una versión nueva hay que cerrar Adrenaline (el relevo: `cmd_*.txt` con `destroy`). Un cambio en
  `config.txt` necesita reiniciar la Vita.
- Si existe `ux0:data/nativedraw/off`, NativeDraw no hace nada.

## Memoria compartida (zona 0x0A000000–0x0B000000 del PSP)
Adrenaline borra esa zona al arrancar el juego y NativeLink la reserva en la partición 11 (`zoneCheck`); si el juego la
ocupa, NativeLink queda apagado. La Vita la ve en `0x68000000 + (dirección & 0x0FFFFFFF)` (sin caché).

| Dirección | Qué |
|---|---|
| 0x0A000000 | Buzón (`SB`, ver abajo) |
| 0x0A100000 | Dos listas de 8 KB del GE para las copias del espejo (alternadas) |
| 0x0A200000 | Espejo de la VRAM 1 (`MIRROR`, 2 MB: copias pares) |
| 0x0A7FA000 | Texto del log de NativeLink (16 KB) |
| 0x0A7FF000 | Descriptor del log: +0 "GPV1" (NativeLink), +0x20 "LOGV" (NativeDraw), +0x40 seq, done, bytes |
| 0x0AC00000 | Espejo de la VRAM 2 (`MIRROR2`, 2 MB: copias impares) |

### Buzón (`SB`, palabras de 32 bits)
| Palabra | Quién escribe | Qué |
|---|---|---|
| 0 | PSP | "SOMB" (se repone en cada flip si algo lo borró) |
| 1 | PSP | versión del buzón (5) |
| 2 | PSP | eventos escritos |
| 3 | Vita | última lista capturada (número de orden) |
| 4 | Vita | latido |
| 5 | Vita | eventos leídos |
| 6 | PSP | listas encoladas (número de orden) |
| 0x20–0x2F / 0x30–0x3F | Vita | páginas de 4 KB de la VRAM pedidas: "listas" / "datos" (mapas de bits) |
| 0x40 | GE | última copia del espejo terminada |
| 0x44, 0x45, 0x46, 0x49 | PSP | copia pedida, páginas copiadas, veces que el GE seguía ocupado, veces que la Vita usaba ese espejo |
| 0x47 | Vita | 1 = encender T6 (`medir_ge`) |
| 0x48 | Vita | copia del espejo en uso (no se pisa) |
| 0x4A | Vita | interruptores: 1 sin compuerta, 2 sin espejo, 4 sin CPU libre, 8 compuerta en el sync |
| 0x4B, 0x4C | PSP | lista que espera la compuerta y desde cuándo |
| 0x4D | PSP | calibración de la CPU libre |
| 0x4E | Vita | nivel de registro (0 mínimo, 1 mediano, 2 extremo) |
| 0x50–0x53, 0x60+, 0x80+ | Vita | muestra de huellas de la última lista capturada (vida de la lista y autocomprobación de la compuerta) |
| 0x54 | PSP | modo de la compuerta (0 probando, 1 antes de encolar, 2 en el sync hasta cerrar el juego) |
| 0x200–0x3FF / 0x8400–0x85FF | PSP | número de la última copia de cada página en cada espejo |
| 0x400 + 8·k | PSP | anillo de 4096 eventos de 32 bytes: tipo | dato << 8, a, b, c, hora del PSP, orden, extra |

Eventos: 1 encolar, 2 encolar al frente, 3 stall, 4 DrawSync, 5 ListSync, 6 flip, 7 fin de un tramo del GE (T6), 8 fin
de un sync, 9 escritura de la CPU (T4), 10 resumen de T4, 11 costo del frame para el juego, 12 vida de la lista.

## Un frame, paso a paso
1. El juego encola una lista (`sceGeListEnQueue`). NativeLink, antes de dejarla pasar, espera a la Vita si toca
   (compuerta), anota el evento con el número de orden de la lista y la deja seguir.
2. El juego actualiza el stall (hasta dónde escribió) y llama a los sync: NativeLink anota cada uno.
3. NativeDraw (capturador) lee el evento, recorre la lista desde la RAM del PSP como lo haría el GE (sigue saltos,
   sublistas, BASE/OFFSET) y cuenta lo que habría que dibujar. Al terminar publica en `SB[3]` el número de la lista.
4. En el flip (`sceDisplaySetFrameBuf`), NativeLink anota el evento, encola la copia del espejo con las páginas pedidas y
   escribe sus estadísticas cada 600 frames (~10 s) en el log.

## Lado Vita: hilos del capturador
| Hilo | Núcleo / prioridad | Qué hace |
|---|---|---|
| capturador | cualquiera de los 3 (0x61; `nucleo_captura` lo fija) | sigue el buzón y recorre cada lista |
| lector 0 y 1 | 0 y 2 (0xB0, solo lo que sobra) | traen por adelantado las líneas de 64 bytes de la lista a una caché compartida y comprueban sublistas guardadas |
| espejo | 0 (0xBE) | repone los pedidos del espejo y (en extremo) mide cuánto cambia |
| registro | (0xB0) | escribe `nativedraw_captura.log` cada segundo |
| escritor | (0xA0) | escribe el log de NativeLink a la tarjeta |

- **Leer la RAM del PSP es lo caro** (~2 µs por línea sin caché): por eso los lectores adelantados y las lecturas de a
  256 bytes con NEON.
- **Memoria de sublistas:** el capturador guarda cada sublista que recorrió (sus líneas y lo que sumó). Si en el frame
  siguiente es idéntica (mismas líneas y mismo estado de entrada), suma lo guardado sin recorrerla. Los lectores la
  comprueban antes (incluso al empezar la lista, "comprobación por adelantado"). 1 de cada 32 se recorre igual para
  revisar que lo guardado es exacto (si no, el log dice **DISTINTAS**: debe ser 0).
- **Interpretar de a línea:** las comprobaciones (stall, tope, traducción de dirección) se hacen una vez por línea de 16
  comandos, no por comando (~580 ns por comando en Ridge Racer).

## Lado PSP: compuerta
- La espera a la Vita va **antes de encolar la lista siguiente** (el juego no reescribe una lista hasta encolar la
  siguiente). Para comprobarlo en cualquier juego, NativeLink rehace las huellas que dejó la Vita: si la lista cambió
  antes de encolar la siguiente, la espera vuelve al sync (60 comprobaciones limpias para volver; 10 cambios = en el sync
  hasta cerrar el juego).
- La espera es de hasta 5 ms; si la Vita no responde (latido parado 0,5 s), no se espera.

## Rendimiento medido (detalle en `docs/changelog/etapa2_ridge_racer.md`)
| Juego | fps con los dos plugins | Vita por frame en lo más pesado |
|---|---|---|
| Gran Turismo (UCUS-98632) | 60,2 en lo más pesado (~80 mil comandos por frame) | 13,4 ms |
| Ridge Racer (ULJS-00001) | 60 toda la carrera | 10,4 ms |
| LocoRoco (NPEG-00024) | 59,9 | 4–8 ms |
| Silent Hill Shattered Memories (ULUS-10450) | los del juego original (no espera a la Vita) | — |

## Lo que todavía no hace (siguiente: etapa 3)
- No dibuja: la imagen que se ve es la del juego, escalada por Adrenaline.
- 0.0.3: el presentador (`presentador.c`, interruptor `dibujar`) ya pasa la imagen del juego por una superficie propia,
  pero es una copia: el dibujo con los comandos del GE viene en los pasos 3.2–3.3 (`etapa3_plan.md`).
- Menú L+R+START (0.0.4): lo dibuja NativeDraw; por ahora solo las versiones. Las opciones siguen siendo archivos en
  `ux0:data/nativedraw/` (`registros.md`).
