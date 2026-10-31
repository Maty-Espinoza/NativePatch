# Cómo funcionan NativeLink y NativeDraw

Referencia de la versión actual (NativeLink 0.0.3, NativeDraw 0.0.8.x). Para la idea general y el estado del proyecto:
`overview.md`. Hoja de ruta: `architecture-and-roadmap.md` §6. Logs e interruptores: `logs-and-switches.md`. Qué se midió
y qué límites se encontraron: `findings.md`. Cómo se llegó a cada pieza, versión por versión: `../../changelog/`.

## En simple
- La PS Vita tiene adentro el hardware de una PSP. Adrenaline corre los juegos de PSP en ese hardware: el procesador del
  PSP corre el juego y su chip gráfico (el **GE**) dibuja la imagen chica de siempre (480x272).
- **NativeLink** es un plugin que corre en el PSP, junto al juego. No cambia nada de lo que el juego dibuja: cada vez que
  el juego le manda al GE una lista de instrucciones de dibujo, NativeLink lo anota en una "libreta" (el **buzón**) que
  está en una memoria que la Vita también puede leer.
- **NativeDraw** es un plugin de la Vita. Lee la libreta y recorre las mismas instrucciones desde la Vita (el
  **capturador**). Guarda lo que necesita para dibujar cada frame (el **registro del frame**) y lo vuelve a dibujar con
  la GPU de la Vita al doble de tamaño (el **traductor**). La imagen resultante reemplaza a la del PSP en la pantalla
  (el **presentador**).
- La Vita no ve la memoria de video del PSP (VRAM), y algunos juegos guardan ahí instrucciones y texturas: en cada
  cuadro NativeLink le pide al GE que copie a la memoria normal los pedazos que la Vita pidió (el **espejo**).
- Si la Vita va atrasada, NativeLink hace esperar al juego un poco (hasta 5 ms) antes de que reuse una lista, para que la
  Vita no lea instrucciones que ya cambiaron (la **compuerta**).
- Si un frame trae algo que la Vita todavía no sabe dibujar, se muestra la imagen normal del juego: nunca peor que sin
  el plugin.

## Piezas y dónde está cada una
| Pieza | Archivo | Corre en |
|---|---|---|
| Ganchos del GE, buzón, espejo, compuerta, telemetría | `nativelink/main.c` | PSP (MIPS), dentro del juego |
| Log de NativeLink (el texto lo escribe NativeDraw a la tarjeta) | `nativelink/log.c`, `log.h` | PSP |
| Listas propias del GE (copias del espejo) | `nativelink/gu.c` | PSP |
| Arranque, log de NativeLink, hilos del emulador | `nativedraw/nativedraw.c` | Vita (ARM), proceso de Adrenaline |
| Capturador, lectores, memoria de sublistas, pedidos al espejo, logs de la captura | `nativedraw/captura.c` | Vita |
| Registro del frame (lo que cada frame necesita para dibujarse) | `nativedraw/registro.c`, `registro.h` | Vita (dentro de `captura.c`) |
| Presentador: toma la pantalla de Adrenaline, menú L+R+START | `nativedraw/presentador.c` | Vita, hilo de dibujo de Adrenaline |
| Traductor: dibuja el registro con la GPU de la Vita | `nativedraw/traductor.c`, `sombreadores/` | Vita (dentro de `presentador.c`) |
| Cronómetro de la GPU, volcados, reproducción de volcados | `cronometro.c`, `volcado.c`, `reproduccion.c` | Vita (dentro de `presentador.c`) |
| Prueba del capturador fuera de la Vita (qemu) | `nativedraw/prueba_memoria.c` | PC |
| Instalador (`NativePatch.vpk`) | `instalador/main.c` | Vita (app) |
| Herramientas del PC: relevo con la Vita, resúmenes de logs, dibujo de volcados | `tools/` | PC |

## Cómo se cargan
- **NativeLink:** Adrenaline 8 lo carga al abrir un juego si está activado en `ux0:pspemu/seplugins/plugins.txt`
  (`game, ms0:/seplugins/nativelink.prx, on`; ver `adrenaline.md`). Abrir el juego manteniendo **L** o SELECT lo deja
  apagado (con L, Adrenaline 8 además apaga todos los plugins). Mantener **R** al abrir: log síncrono (cada línea al
  instante, para cuelgues).
- **NativeDraw:** taiHEN lo carga en el proceso de Adrenaline (sección `*PSPEMUCFW` de `config.txt`), al abrir
  Adrenaline. Para que cargue una versión nueva hay que cerrar Adrenaline. Un cambio en `config.txt` necesita reiniciar
  la Vita.
- Si existe `ux0:data/nativedraw/off`, NativeDraw no hace nada. Sin `ux0:data/nativedraw/dibujar`, NativeDraw captura y
  mide pero solo reemplaza la imagen mientras el menú L+R+START está abierto.

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
| 0x4A | Vita | interruptores: 1 sin compuerta, 2 sin espejo, 4 sin CPU libre, 8 compuerta en el sync, 16 espejo completo (lo pone NativeDraw cuando el registro da frames 2D completos), 32 el flip del PSP espera la copia del espejo (`espejo_espera`) |
| 0x4D | PSP | calibración de la CPU libre |
| 0x4E | Vita | nivel de registro (0 mínimo, 1 mediano, 2 extremo) |
| 0x50–0x53, 0x60+, 0x80+ | Vita | muestra de huellas de la última lista capturada (vida de la lista y autocomprobación de la compuerta) |
| 0x54 | PSP | modo de la compuerta (0 probando, 1 antes de encolar, 2 en el sync hasta cerrar el juego) |
| 0x200–0x3FF / 0x8400–0x85FF | PSP | número de la última copia de cada página en cada espejo |
| 0x400 + 8·k | PSP | anillo de 4096 eventos de 32 bytes: tipo \| dato << 8, a, b, c, hora del PSP, orden, extra |

Eventos: 1 encolar, 2 encolar al frente, 3 stall, 4 DrawSync, 5 ListSync, 6 flip, 7 fin de un tramo del GE (T6), 8 fin
de un sync, 9 escritura de la CPU (T4), 10 resumen de T4, 11 costo del frame para el juego, 12 vida de la lista.

## Un frame, paso a paso
1. El juego encola una lista (`sceGeListEnQueue`). NativeLink, antes de dejarla pasar, espera a la Vita si toca
   (compuerta), anota el evento con el número de orden de la lista y la deja seguir.
2. El juego actualiza el stall (hasta dónde escribió) y llama a los sync: NativeLink anota cada uno.
3. El capturador de NativeDraw lee el evento y recorre la lista desde la RAM del PSP como lo haría el GE (sigue saltos,
   sublistas, BASE/OFFSET). Lleva el estado del GE, cuenta lo que habría que dibujar y, con `dibujar`, agrega cada draw
   al registro del frame: el estado, los vértices e índices copiados, la paleta y las filas de textura que usan los
   vértices (copiadas por DMA a memoria que la GPU puede leer). Al terminar publica en `SB[3]` el número de la lista.
4. En el flip (`sceDisplaySetFrameBuf`), NativeLink anota el evento, encola la copia del espejo con las páginas pedidas y
   escribe sus estadísticas cada 600 frames (~10 s) en el log. El capturador cierra el frame: si el registro está
   completo (todos los draws pasaron por él) lo publica; si le faltan texturas de la VRAM, espera la copia del espejo de
   ese flip y después lo publica.
5. El presentador, enganchado al fin de cada escena de Adrenaline, toma el último frame completo publicado y el traductor
   lo dibuja con la GPU de la Vita en una superficie propia de 1024x544 (960x544 visibles), con la imagen normal
   estirada debajo (lo que el juego no redibuja queda como en el PSP). En la escena siguiente de Adrenaline, cuando
   Adrenaline va a dibujar la imagen del PSP, se le pasa esa superficie en su lugar. La imagen se ve un frame después
   que la del PSP.
6. Si el frame trae algo que el traductor no sabe dibujar (3D, una mezcla sin equivalente, una textura sin copia...),
   se muestra la imagen normal y el motivo se cuenta en el log (`traductor:` en `nativedraw.log`).

## Lado Vita: hilos
| Hilo | Núcleo / prioridad | Qué hace |
|---|---|---|
| capturador | cualquiera de los 3 (0x61; `nucleo_captura` lo fija) | sigue el buzón, recorre cada lista y arma el registro del frame |
| lector 0 y 1 | 0 y 2 (0xB0, solo lo que sobra) | traen por adelantado las líneas de 64 bytes de la lista a una caché compartida y comprueban sublistas guardadas |
| espejo | 0 (0xBE) | pasa a NativeLink los interruptores y el nivel; en extremo mide cuánto cambia el espejo |
| registro | (0xB0) | escribe `nativedraw_captura.log` cada segundo |
| escritor | (0xA0) | escribe el log de NativeLink a la tarjeta |
| presentador | (0xA0) | engancha a Adrenaline, pinta el menú, atiende volcados y reproducción, escribe `nativedraw.log` cada 10 s |
| cronómetro | (0x40) | con `medir_gpu`: mide cuánto tarda la GPU en cada escena |
| hilo de dibujo de Adrenaline | el de Adrenaline | ahí corren el traductor y el presentador propiamente dichos (en los ganchos de GXM) |

- **Leer la RAM del PSP es lo caro** (~2 µs por línea sin caché): por eso los lectores adelantados y las lecturas de a
  256 bytes con NEON.
- **Memoria de sublistas:** el capturador guarda cada sublista que recorrió (sus líneas y lo que sumó). Si en el frame
  siguiente es idéntica (mismas líneas y mismo estado de entrada), suma lo guardado sin recorrerla. Los lectores la
  comprueban antes (incluso al empezar la lista, "comprobación por adelantado"). 1 de cada 32 se recorre igual para
  revisar que lo guardado es exacto (si no, el log dice **DISTINTAS**: debe ser 0). Una sublista aplicada así no pasa por
  el registro del frame: ese frame queda incompleto y va con la imagen normal (por eso hoy la Vita dibuja juegos 2D, que
  casi no usan sublistas).
- **Interpretar de a línea:** las comprobaciones (stall, tope, traducción de dirección) se hacen una vez por línea de 16
  comandos, no por comando.

## Lado PSP: compuerta
- La espera a la Vita va **antes de encolar la lista siguiente** (el juego no reescribe una lista hasta encolar la
  siguiente). Para comprobarlo en cualquier juego, NativeLink rehace las huellas que dejó la Vita: si la lista cambió
  antes de encolar la siguiente, la espera vuelve al sync (60 comprobaciones limpias para volver; 10 cambios = en el sync
  hasta cerrar el juego).
- La espera es de hasta 5 ms; si la Vita no responde (latido parado 0,5 s), no se espera.

## Lado Vita: dibujo
- **Registro del frame** (`registro.c`): 4 registros que se turnan (uno se escribe, otro queda publicado y los que la
  GPU puede estar leyendo no se tocan). Por frame: draws, estados del GE (solo cuando cambian), vértices e índices
  copiados, paletas y las filas de textura que usan los draws. Las texturas con el "swizzle" del PSP (bloques de 16 bytes
  x 8 filas) la CPU las deja ordenadas al copiarlas, así la GPU las lee como una textura común. Las texturas de la RAM se
  copian al momento (el juego las puede cambiar); las de la VRAM se copian después del flip, de la copia del espejo de
  ese flip.
- **Traductor** (`traductor.c`): solo 2D en modo "through" (coordenadas en píxeles): rectángulos, triángulos y tiras;
  texturas 5650/5551/4444/8888 y paletas de 4 y 8 bits; borrados; color por vértice o del material; funciones de textura
  modular y reemplazar; alfa test; mezcla (las combinaciones que GXM tiene); scissor en los rectángulos; **imágenes
  intermedias** (un juego dibuja en una imagen escondida de la VRAM y después la estira: cada una va a una superficie
  propia al doble, hasta 2). Primero revisa todos los draws del frame; si alguno no se puede, no dibuja nada y devuelve el
  motivo. Sombreadores propios en `nativedraw/sombreadores/*.cg`, compilados a `sombreadores_nd.h`.
- **Presentador** (`presentador.c`): engancha `sceGxmSetFragmentTexture`, `sceGxmEndScene` y `sceDmacMemcpy` de
  AdrenalineUser. Reconoce la textura de 512x272 de Adrenaline y la reemplaza por su superficie, que es el doble exacto:
  Adrenaline la ubica y escala igual que siempre (se respetan su escala, posición y proporción). La copia de la imagen
  del PSP que hace Adrenaline se desvía a 4 copias propias alternadas (la GPU la lee para NativeDraw después que para
  Adrenaline). Todo en memoria LPDDR (nunca CDRAM, que la usan los videos de la Vita) y siempre con z.
- **Sincronización con la GPU:** cada escena propia escribe su número en una palabra libre de las notificaciones de GXM
  al terminar. No se le manda otro frame a la GPU si no terminó el de hace 2 (se repite la superficie), y antes de
  reescribir vértices, paletas o la copia de la imagen normal se espera a que termine la escena que los leía (como
  mucho 30 ms). Así no hay cuadros rotos de un frame.
- **"Tapado":** el diálogo del botón PS y los avisos del sistema del PSP se dibujan encima del juego sin pasar por las
  listas. Cada frame se comparan 64 puntos de la imagen normal con el dibujo de la Vita; si no se parecen 6 frames
  seguidos, se muestra la imagen normal hasta que vuelvan a parecerse.
- **Menú L+R+START:** lo dibuja NativeDraw encima de la imagen (cuadro de 256x128 al doble); mientras está abierto el
  juego no recibe botones. Hoy muestra las versiones; las opciones siguen siendo archivos en `ux0:data/nativedraw/`.
- **Volcados y reproducción:** `dumps.md`.
