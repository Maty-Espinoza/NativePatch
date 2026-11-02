# NPTEST: batería sintética 2D

Un homebrew de PSP (`tests/nptest/`) que dibuja una técnica por escena, 2 s cada una (108 escenas, unos 3,5 min). No toca
los plugins. Cada escena muestra su número arriba a la derecha y como código de barras en la línea inferior.
`ms0:/native_test.log` (en la Vita `ux0:pspemu/native_test.log`, lo baja el relevo) recibe una línea por escena:
`número nombre motivo_esperado primer_frame hash_frame30 hash_frame119`. Los hashes son de la imagen del PSP: no deben
cambiar entre versiones de los plugins, ni con los plugins apagados.

Botones: DERECHA/IZQUIERDA siguiente/anterior, X quedarse en una escena (para sacar un volcado con L+R+SELECT), START salir.

## Grupos de escenas
| Grupo | `motivo_esperado` | Escenas | Para qué sirve |
|---|---|---|---|
| Funciona hoy | `ok` | 0, 2–22 | Regresión: debe dibujarse en HD e igual a la imagen normal |
| Rechazado con motivo conocido | el motivo | 1, 23–50 (bezier, spline, rtReread, manyDraws, manyTex: `?`) | Regresión: el frame cae a la imagen normal con ese motivo. La escena 1 (`sync`) además alinea los logs |
| Sin código en el traductor | `?` | 51–107 | Descubrir: la imagen y el motivo dicen qué pasa |

La escena 0 debe seguir primera: prueba el estado configurado solo en las primeras listas del programa.

## Técnicas raras y de dónde salen
| Escena | Técnica | Visto en |
|---|---|---|
| `castT16`, `castT8` | Una imagen leída como índices de paleta, por canal o byte a byte | Burnout Dominator, SOCOM, Syphon Filter, Resistance |
| `paletteRt` | Una paleta dibujada por el GE | Burnout Dominator (destello de lente) |
| `depthTex` | Profundidad como textura por un espejo de la VRAM | Me & My Katamari, Suikoden, Kurohyo 2 |
| `cpuRead` | El programa lee la VRAM con la CPU | Wipeout Pure, Dangan Ronpa |
| `blueAlpha` | El alfa de una imagen 4444 escrito en 565 con una máscara de bits | Split/Second, DiRT 2, OutRun |
| `stencilAlpha`, `blendDst` | Stencil como alfa | Wipeout, God Eater |
| `strideGap`, `overlapRt` | Imágenes dentro o encima de otra | Juegos de Tantalus, Juiced 2, God of War |
| `xferFb`, `xferIntra`, `xferSelf` | Copias de bloque sobre el framebuffer | Trails in the Sky, Grand Knights History, Gradius Collection, Colin McRae |
| `mipLevel` | Una fuente guardada en un nivel de mipmap | Tactics Ogre |
| `bezier` | Curvas usadas para rectángulos 2D | Puzzle Bobble, LocoRoco |
| `blendAbs` | Mezcla ABSDIFF | Dragon Ball Z: Tenkaichi Tag Team |
| `bboxJump` | Salto condicional por caja envolvente (el capturador supone que no se toma) | Tekken |
| `rectCorners` | Los UV de un rectángulo rotan según el orden de sus esquinas | Regla del GE (documentación de PPSSPP) |
| `depth2d` | Prioridad de sprites por profundidad | Heroes Phantasia, Phantasy Star |

Fuentes: documentación de PPSSPP (`psp-hardware/gpu`), `assets/compat.ini` y sus issues #13256, #15923, #22482, #15898, #15500,
#15888, #17810. Las demás escenas salen de leer `translator.c` y `capture.c` (estados guardados y no usados, casos sin código).

## En el PC
`sh tools/emu.sh` compila la batería y la corre en PPSSPP headless (dibujo por software, unos 6 s). Deja en `build/nptest/`
el log, las imágenes y hojas con todas las escenas. Dice si una escena se cuelga y cómo debería verse; no dice nada de los
plugins, que solo corren en la Vita. PPSSPP no es la referencia: lo es la imagen normal en la Vita. Diferencias por
confirmar: UV de 8 bits en coordenadas directas (`vertFmts`, celda 0), `bboxJump` (se ve el rojo), `mipLevel` y `clutStale`.

## Leer una corrida en la Vita
`python3 tools/nptest_report.py <carpeta> [native_test.log de PPSSPP]` muestra, por escena, el resultado esperado y el
obtenido (necesita compilación de desarrollo). `ok` solo dice que el frame se dibujó en HD, no que la imagen esté bien. Un
frame sin registro nuevo (`no_new_frame`, por ejemplo `limit`) no sale en esas líneas: mirar las líneas `record:` de
`nativedraw_capture.log`.

## Comparar las imágenes
Con `ms0:/nptest_dumps` empezando con `y` (`send/ux0/pspemu/nptest_dumps`), cada escena dura 5 s (unos 9 min) y en su frame
60 manda un NOP del GE con `0x4E5044`, que hace que un NativeDraw de desarrollo saque un volcado. Usar el nivel `screenshot`
(`ux0:data/nativedraw/dump`, se lee al abrir Adrenaline) y dejarlo como estaba al terminar.
`python3 tools/nptest_compare.py <volcados> <corrida> <salida>` asocia cada volcado a su escena por el código de barras,
reduce la imagen de la Vita y compara en bloques de 4x4: `same`, `DIFF`, `normal (motivo)` o `no dump`. Deja `compare.txt` y
páginas con PSP, Vita y diferencias. Las escenas animadas pueden diferir en un frame. Las que no mandan lista del GE
(`ramShown`) no generan volcado.
