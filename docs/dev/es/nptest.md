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

## Todas las escenas
- **Primitivas y vértices:** clear con scissor (`clearS`); triángulos, tiras, rectángulos, índices de 8, 16 y 32 bits (`prims`); abanicos (`fan`); formatos de posición, color y UV, color de material (`vertFmts`).
- **3D, dibujado:** sprite ortográfico (`ortho`); cubo con luz, niebla y textura, con profundidad y cull (`cube3d`).
- **Texturas:** 5650, 5551, 4444, 8888, T4, T8 (`texFmts`); swizzle (`texSwz`); filtro punto y lineal, repeat y clamp (`filterWrap`); shift, máscara y start de paleta, 4 formatos de paleta (`palettes`).
- **Operaciones de píxel:** modulate, replace, TCC, doubling (`texFuncs`); add, subtract, reverse subtract, min, max (`blendOps`); factores de blend y fijos 0 o blanco (`blendFactors`); las 8 funciones de prueba de alfa (`alphaTest`); scissor (`scissorRect`); 200 capas (`layers`).
- **Destinos y copias:** una y dos imágenes intermedias (`rtOne`, `rtTwo`); dibujo en 565 y 5551 (`rtFmts`, `fb565`, `fb5551`); TRANSFER desde RAM y VRAM (`xferRam`, `xferVram`); lista enviada después del flip (`lateList`); estado configurado solo en las primeras listas (`firstState`).
- **Primitivas y vértices rechazados:** `points`, `bezier`, `spline`, `primKeep`; `weights`, `morph`.
- **Texturas y píxeles rechazados:** `decal`, `tfxBlend`, `tfxAdd`, `dxt1`, `dxt3`, `dxt5`, `t16`, `t32p`; `colorTest`, `logicOp`; color fijo gris (`fixGray`), `blendAbs`, alfa de destino (`blendDst`), alfa 2x (`blend2x`).
- **Límites:** destino de más de 512 de ancho (`wideTarget`), 3 intermedias (`threeRt`), dibujar sobre una imagen ya leída (`rtReread`), posición sobre 4096 (`offscreen`), 5000 dibujos (`manyDraws`), 300 texturas (`manyTex`), 512 filas (`tallTex`).
- **Profundidad, stencil y máscaras:** `depth2d`, `stencilMask`, `stencilAlpha`, `alphaMask`, `pixelMask`, `regionClip`, scissor vacío (`scissorEmpty`), scissor en triángulos (`scissorTri`).
- **Estado sin código:** nivel de mip forzado (`mipLevel`), offset y escala de UV (`uvOffset`), `dither`, `fog`, sombreado plano (`flat`), esquinas de rectángulo (`rectCorners`), rectángulos impares (`rectOdd`), buffer más ancho que la textura (`texStride`), ancho impar (`texNpot`), filtro lineal en atlas (`atlasBleed`), imagen 4444 (`fb4444`).
- **Imágenes leídas de formas raras:** parte de una imagen por dirección (`subRt`), más alta que la pantalla (`rtTall`), del frame anterior (`rtPrev`), la imagen mostrada (`readFront`), la que se está dibujando (`readBack`), sobre sí misma (`selfBlur`), `strideGap`, `overlapRt`, cambios de formato (`cast565`, `castT8`, `castT16`, `blueAlpha`), espejo de VRAM (`texMirror`), profundidad (`depthTex`).
- **Paletas:** dibujada por el GE (`paletteRt`), en VRAM (`clutVram`), cambiada sin cargar (`clutStale`), cambiada entre dibujos (`clutTwice`).
- **Datos que cambian:** textura, copia y vértices reescritos dentro de un frame (`texTwice`, `xferTwice`, `vtxTwice`), sublista parchada (`sublist`), textura con 1 texel o entera cambiada (`cache1`, `cache2`).
- **Comandos de lista:** sin dirección de vértices (`vaddrKeep`), salto condicional (`bboxJump`).
- **Copias sobre la imagen:** `xferFb`, `xferIntra`, `xferSelf`.
- **CPU y pantalla:** lectura de VRAM (`cpuRead`), píxeles escritos (`cpuPixels`), redibujo parcial (`noClear`), 30 fps (`fps30`), una sola imagen (`singleBuf`), dirección movida (`shake`), imagen mostrada en RAM (`ramShown`).

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
plugins, que solo corren en la Vita. PPSSPP no es la referencia: lo es la imagen normal en la Vita. La primera corrida en
la Vita confirmó que acierta en todo menos en `morph`, `readFront`, `selfBlur` y `clutStale`.

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
