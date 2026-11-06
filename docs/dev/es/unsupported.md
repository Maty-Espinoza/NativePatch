# Lo que NativeDraw todavía no dibuja

Las técnicas de dibujo de los juegos de PSP que NativeDraw no soporta, o soporta solo lento. Para cada una: qué pasa hoy, el
motivo que da el log (`translator: result at frame N: <motivo>` y la línea `translator: HD frames`), la prueba que ya la
cubre y dónde se vio. Es la lista desde donde diseñar el soporte, de a una, con las pruebas que ya existen:
- **NPTEST** (`nptest.md`, `tests/nptest/`): un programa de PSP que dibuja una técnica por escena. Una escena con
  `expected_reason` `?` o un motivo es una técnica sin hacer; cuando se soporte, su motivo esperado pasa a `ok`. NPTEST es
  2D: lo marcado "falta una escena 3D" no tiene escena todavía.
- **Frames guardados y el banco del traductor** (`tools/perf/data/*.bin.gz`, `tools/perf/tr_gv.sh`, `tools/test_ci.sh`):
  frames reales pasados por el traductor real en el PC. `tr_gv.sh` compara los triángulos del camino de CPU y del de GPU:
  la forma de llevar trabajo de vértices a la GPU sin cambiar la imagen. Una técnica nueva necesita un frame de un juego que
  la use (un volcado L+R+SELECT, `dumps.md`).

Los vértices por frame salen de `tools/perf/data` (combate de God of War Ghost of Sparta, carrera y público de Ridge Racer).

## 1. El frame cae a la imagen normal
Todo el frame se muestra a la resolución del PSP (o se deja fuera un dibujo a una imagen intermedia, si nada lo necesita).

| Técnica | Motivo | Escena de NPTEST | Visto en |
|---|---|---|---|
| Puntos | `primitive` | `points` | - |
| Superficies bezier y spline | registro incompleto (`curves`) | `bezier`, `spline` | Puzzle Bobble, LocoRoco (rectángulos 2D) |
| Primitiva "mantener la anterior" | `primitive` | `primKeep` | - |
| Morphing (varios juegos de vértices mezclados) | `vertex` | `morph` | - |
| Pesos en modo through | `vertex` | `weights` | - |
| Posiciones de vértice sobre 4096 | `vertex_out` | `offscreen` | Público de Ridge Racer (listas tardías) |
| Texturas DXT1, DXT3, DXT5 | `format` | `dxt1`, `dxt3`, `dxt5` | - |
| Índices de paleta de 16 y 32 bits (T16, T32) | `format` | `t16`, `t32p` | - |
| Funciones de textura sin equivalente | `texture_function` | `decal`, `tfxBlend`, `tfxAdd` | - |
| Prueba de color que cambia la imagen | `color_test_or_logic` | `colorTest` | Ridge Racer (`fails/003`) |
| Operaciones lógicas | `color_test_or_logic` | `logicOp` | - |
| Colores fijos de mezcla con un color de destino (0xE1) que no es gris, o con una textura que no se modula o se copia de memoria | `blend` | `fixGray` | - |
| Mezcla ABSDIFF | `blend` | `blendAbs` | Dragon Ball Z: Tenkaichi Tag Team |
| Alfa de destino en la mezcla | `blend` | `blendDst` | Wipeout, God Eater |
| Alfa doblado mezclado con factores de alfa normales | `blend` | `blend2x` | - |
| Máscara de color por bits, no por canales enteros | `mask` | `pixelMask`, `blueAlpha` | Split/Second, DiRT 2, OutRun |
| Prueba alfa con una máscara que los shaders no pueden comparar | `mask` | `alphaMask` | Menú de God of War (`162000`) |
| Una imagen intermedia leída en otro formato | `bad_texture` | `cast565`, `castT8`, `castT16` | Burnout Dominator, SOCOM, Syphon Filter, Resistance |
| Profundidad leída como textura | `bad_texture` | `depthTex` | Me & My Katamari, Suikoden, Kurohyo 2 |
| Una paleta dibujada por el GE | `bad_texture` | `paletteRt` | Burnout Dominator (destello de lente) |
| Una imagen leída mientras se dibuja en ella, cuando comparte su superficie | `bad_texture` | `selfBlur`, `rtReread` | - |
| Imágenes intermedias que no caben en las dos superficies, o que comparten memoria al mismo tiempo | `other_target` | `overlapRt`, `strideGap` | God of War (una `091000` de pantalla completa con la cadena del brillo) |
| Más de 96K vértices por superficie, 64K en una llamada de GXM | `limit` | `manyDraws` | El combate de God of War necesitaba ~75K (subido en la 0.0.25.20) |
| La CPU lee o escribe la VRAM | imagen normal o mal | `cpuRead`, `cpuPixels` | Wipeout Pure, Dangan Ronpa |

Lado del capturador (el frame se descarta antes del traductor): un salto condicional por caja se supone no tomado
(`bboxJump`, Tekken); una sublista de VRAM con basura después de una pelea de God of War (`list_too_long`, `TODO.md`).

## 2. Se dibuja, pero la CPU hace el trabajo de vértices
El trabajo de vértices del GE (transformación, luces, UV) lo hace la CPU cuando el camino de GPU (`nd_v3`, `nd_v4`, `nd_v5`,
`rgVGpu` en `record.c`) no toma el dibujo. Queda bien, pero lento: en el combate de God of War los vértices en la CPU cuestan
~250 ms por frame HD (0.0.25.23). El camino de GPU hoy: tipos `0x19F` y `0x1BE` sin luces (`nd_v3`), `0x142` y `0x143` con una
luz direccional o UV desde la normal (`nd_v4`), `0x7B9` y `0x7BE` con 1 a 8 pesos y `0x1B9` y `0x1BE` (con luz) con luces
direccionales y puntuales, el color con luz sin luces o el mapa de entorno (`nd_v5`); vértices en RAM (`rgVLater`) o en VRAM con copia en el espejo (`rgVNow`);
sin índices, solo triángulos, un scissor que abarca toda la imagen, sombreado suave.

| Técnica | Vértices por frame | Prueba |
|---|---|---|
| Luces de foco, difuso con potencia | - | falta una escena 3D (`cube3d` tiene una luz) |
| Color de vértice con luces (`nd_v4` no tiene color) | - | falta una escena 3D |
| Otros tipos de vértice: `0x102`, `0x100`, `0x11E`, `0x19D`, `0x183` | Ridge Racer ~700; God of War `0x183` ~1.000 | frames guardados `rr_*`, `gow_enemies` |
| Dibujos con índices | - | `prims` (2D); falta una escena 3D |
| UV generada por proyección desde la posición | Sombras en el suelo de God of War (`0xC0` = 1) | frame guardado `gow_combat` |
| Un scissor que corta la imagen, sombreado plano | Ridge Racer: sombras y la zona del retrovisor; God of War: la familia `0x780` (solo pesos y posición) en imágenes intermedias, ~1.100 a 2.700 | frames guardados `rr_*`, `gow_*` |

## 3. Estado sin código (se ignora: la imagen puede diferir)
De NPTEST, escenas sin código detrás: nivel de mip forzado (`mipLevel`, Tactics Ogre guarda ahí una fuente), desplazamiento y
escala de UV (`uvOffset`), `dither`, niebla en 2D (`fog`), sombreado plano en 2D (`flat`), el orden de las esquinas de un
rectángulo (`rectCorners`), rectángulos impares (`rectOdd`), un búfer más ancho que la textura (`texStride`), anchos impares
(`texNpot`), filtro lineal en el borde de un cuadro de atlas (`atlasBleed`), dibujar en una imagen 4444 (`fb4444`).

## 4. Aproximaciones
- El alfa de destino no se calcula (no se escribe) en la mezcla.
- Los colores fijos de mezcla de 1/255 o menos cuentan como 0.
- Una imagen intermedia sola en su superficie se lee como toda la superficie: la repetición y el recorte se detienen en el
  borde de la superficie, no en el de la textura (las imágenes que comparten superficie se leen solo por su parte).
- Las imágenes que solo achican y difuminan quedan a tamaño PSP (`keep_hd` para comparar).
