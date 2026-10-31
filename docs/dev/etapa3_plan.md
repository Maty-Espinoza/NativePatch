# Etapa 3: renderizador mínimo (plan, 2026-10-29)

Etapa 3 de la hoja de ruta (`arquitectura_dibujo_en_vita.md` §6): **¿funciona la cadena completa (captura → GPU de la
Vita → pantalla) sin frenar al juego?** Plan acordado con el usuario al empezar la etapa. Estado de cada paso al final.

## En simple
Hasta ahora NativeDraw solo **lee** lo que el juego le pide al chip gráfico del PSP. En esta etapa empieza a **dibujarlo**
con la GPU de la Vita, en alta resolución, empezando por lo más simple (2D: menús, sprites) y siempre con un respaldo: si
un frame trae algo que todavía no sabe dibujar, se muestra la imagen normal del juego. Nunca peor que sin el plugin.

## Cómo dibuja Adrenaline 8 (`user/menu.c` de isage/Adrenaline, función `AdrenalineDraw`) [código]
- **Juegos de PSP:** en cada vuelta copia la imagen del PSP (0x74000000, 512x272 8888) a su textura `psp_tex` y la dibuja
  con vita2d centrada, con la escala de su menú (`psp_screen_scale`, 2.0 = pantalla completa).
- **Juegos de PS1:** la imagen sale del emulador de PS1 de la Vita en otra textura (`pops_tex`, 1024x544, ya en 4:3 y con
  la escala de PS1 del menú). No pasa por el GE: **NativeLink no la ve y NativeDraw no la toca.**
- **Filtro "Original"** (o el escalado directo de la 8: "sharp" a 2x sin f.lux): Adrenaline no dibuja con GXM y el
  presentador no puede actuar. Para probar NativeDraw hay que usar un filtro con GXM (Bilinear).
- **Proporción (4:3 / 16:9):** el presentador cambia solo la textura `psp_tex` por una superficie propia del **doble
  exacto** de su tamaño (1024x544). Adrenaline sigue ubicando y escalando la imagen como siempre, así que se respetan su
  escala, su posición y la proporción que tenga el juego. Si un juego de PSP dibuja en 4:3 dentro de sus 480x272 (con
  bordes), la versión HD queda igual. La imagen de PS1 no se toca.

## Pasos (cada uno se prueba en la Vita antes de seguir)

| Paso | Qué se ve | Qué valida | Versión |
|---|---|---|---|
| **3.0 Presentador transparente** | Nada distinto: la imagen del juego pasa por una superficie propia de NativeDraw | Tomar la pantalla de Adrenaline de forma estable y cuánto cuesta; la pantalla negra que quedó pendiente de la etapa 1 (`docs/changelog/etapa1_banco_gxm.md` §3.3) | NativeDraw 0.0.3 |
| **3.1 Menú L+R+START dibujado por NativeDraw** | Menú encima del juego con las opciones que hoy son archivos (`registros.md`) | Dibujar encima. Bloquear los botones del juego desde la Vita (ganchos de entrada de Adrenaline) [verificar]; si no se puede, lo hace NativeLink | 0.0.x |
| **3.2 Registro del frame** | Nada (solo el log) | El capturador, además de contar, guarda cada dibujo con su estado; los vértices 2D se copian (el juego puede reusar esa memoria en el frame siguiente). Organizado **por sublistas** desde el principio (base para reusar lo grabado en GT, `etapa2_nucleos.md`) | 0.0.x |
| **3.3 Traductor 2D** | Menús y juegos 2D en HD (960x544) | Borrar pantalla, sprites/triángulos en modo "through", texturas 8888/565/5551/4444, paletas CLUT4/8, swizzle del PSP, mezcla y alpha test básicos. Solo se muestra en HD el frame entendido completo | 0.0.x |
| **3.4 Copias de bloque + comparación** | Igual, más robusto | TRANSFER en la Vita; T5: la imagen HD reducida contra la nativa → si difieren mucho, la nativa y un volcado | 0.0.x |

Juegos de prueba: **Retro City Rampage DX** (2D), **KOF 96** (2D; [verificar] si es la versión de PSP o la de PS1: la de PS1 no pasa por el GE y solo sirve para comprobar que NativeDraw no la toca) y los menús de GT, RR, SH y LocoRoco.

## Decisiones
- **Versiones:** todo 0.0.x hasta el primer release, que será NativeLink 0.1.0 + NativeDraw 0.1.0 (`versiones.md`).
- **Interruptor `ux0:data/nativedraw/dibujar`, apagado por defecto** mientras se estabiliza (sin el archivo no se engancha
  nada del presentador). Lo nuevo de la GPU se prende a propósito: en la etapa 1 un experimento apagó la Vita entera.
- **Traductor propio**, con PPSSPP como referencia y copiando funciones puntuales cuando ahorre tiempo (licencia
  compatible, se anota en `AVISOS_TERCEROS.md`).
- **Sombreadores:** para 3.0 y el menú, los de vita2d ya compilados (`nativedraw/sombreadores.h`, los mismos que usa
  Adrenaline). Para 3.3, sombreadores propios precompilados con la cadena del FSR (compilador de Sony bajo qemu,
  `JagerDesu/vita-shaders`, documentada en `docs/changelog/conversaciones/resumen_conversacion_2026-10-27f.md`).
- **Escala:** 2x (960x544 visibles). Más (3x reducido = antialiasing) es una opción posterior.
- **Latencia:** la imagen HD se ve un frame después que la nativa (la Vita dibuja el frame N mientras el juego arma el
  N+1). El paso 3.0 ya funciona así.

## Paso 3.0: presentador transparente (`nativedraw/presentador.c`, NativeDraw 0.0.3)
- Engancha las importaciones de `AdrenalineUser` `sceGxmSetFragmentTexture`, `sceGxmEndScene` y `sceDmacMemcpy` (como
  el banco de la etapa 1). Reconoce `psp_tex` por su tamaño (512x272). La copia de la imagen del PSP que hace Adrenaline
  va a dos texturas propias alternadas (no a `psp_tex`).
- Memoria: superficies, z y copias en LPDDR física contigua; nada en CDRAM.
- Seguro: si la superficie sale negra 180 frames seguidos (3 s; 30 hasta 0.0.8.16, se activaba en las transiciones de KOF 96) y la imagen del juego no, deja de reemplazarla hasta cerrar
  Adrenaline. El log marca "primer frame dibujado" y "60 frames dibujados" (para ver dónde se cuelga si pasa).
- Después de la escena de Adrenaline, en su hilo y su contexto, dibuja `psp_tex` al doble en una de dos superficies
  propias (LPDDR física, 1024x544 con 960x544 visibles, z D24S8). En el frame siguiente, cuando Adrenaline usa `psp_tex`, se le
  pasa la superficie. Si en un frame Adrenaline no usó `psp_tex` (menú de Adrenaline, PS1, filtro Original), no se dibuja
  nada y al volver no se cambia hasta tener una superficie nueva.
- `nativedraw.log` cada 10 s: frames pasados por la superficie, CPU por frame (promedio y máximo) y cuántos frames casi
  negros tuvo la imagen del juego y la superficie (16 muestras), para la pantalla negra pendiente.
- Criterio: la imagen se ve igual que sin NativeDraw (con 1 frame de atraso), sin cortes ni negro, el juego a sus fps,
  CPU del presentador ≲ 0,3 ms por frame.

## Paso 3.0: resultado (NativeDraw 0.0.3, probado el 2026-10-29) [medido]
Menú de Adrenaline, selector de juegos (videos de vista previa y animación de inicio), Gran Turismo (cinemática y
carrera), Retro City Rampage y KOF 96 se ven igual que sin NativeDraw, a 60 fps, durante 15 minutos seguidos. CPU del
presentador: 0,14–0,2 ms por frame (máximo ~1 ms algún frame). Logs: `vitabridge/etapa3_0.0.3.x/`.

Lo que se aprendió en las versiones de prueba:
| Prueba | Qué pasó | Lección |
|---|---|---|
| 0.0.3.1 | La pantalla mostraba un trapecio naranja | La matriz `wvp` de los sombreadores de vita2d va **por columnas** (desplazamiento en [12] y [13]; `mul(pos, wvp)`) |
| 0.0.3.2 | Cinemática de GT negra, estática en la vista previa del selector, animación de inicio rara | **No usar CDRAM**: en el proceso de Adrenaline hay solo 4 MB y la necesitan los videos que decodifica la Vita. Explica también la pantalla negra del banco de la etapa 1 (pendiente cerrado) |
| 0.0.3.3 | Superficies en LPDDR normal y sin z: superficie negra | — |
| 0.0.3.4 | Superficies en LPDDR física contigua y sin z: Adrenaline congelado (sin volcado) | Con un destino propio, **usar siempre z** (D24S8, fondo 1.0), como el banco y 0.0.3.2 |
| 0.0.3.5 | Todo bien salvo una franja de ~20 px de vez en cuando | La GPU lee la imagen del PSP para NativeDraw después que para Adrenaline, y la copia del frame siguiente la pisaba |
| 0.0.3.6 = 0.0.3 | Todo bien | La copia de Adrenaline (`sceDmacMemcpy` 0x74000000 → `psp_tex`) se desvía a dos texturas propias alternadas |

Memoria con el presentador (libre de una vez): CDRAM 4 MB (sin tocar), LPDDR física 13 MB (usa 2 superficies + z + 2
copias de la imagen ≈ 11 MB), LPDDR 25 MB.

Pendiente menor: una línea de puntos fija sobre la fecha del menú de Adrenaline (solo ahí; en los juegos no). Falta ver
si también sale sin NativeDraw (`dibujar` con `no`).

## Paso 3.1: menú L+R+START (NativeDraw 0.0.4, probado el 2026-10-29) [medido]
- Botones: gancho a la importación de ScePspemu `sceCompatWriteSharedCtrl` (la misma que usa Adrenaline para su menú).
  L+R+START abre/cierra; abierto, el juego recibe "nada apretado" (también mientras se mantiene la combinación). Solo se
  abre si Adrenaline está mostrando la imagen del PSP (no en PS1).
- Dibujo: cuadro de 256x128 (textura en LPDDR, filtro punto) al doble, centrado, con mezcla, encima de la imagen en la
  superficie del presentador. Texto con la fuente 8x16 del instalador.
- Sin `dibujar`, el presentador se prepara igual (la memoria de siempre) y reemplaza la imagen solo con el menú abierto.
- Por ahora solo muestra las versiones (el usuario: es un lugar para las opciones; cuáles van, se decide después).
- Lección (0.0.4.1): la primera línea de `nativelink.log` (la versión) la escribe el PSP directo a la tarjeta, antes de
  que NativeDraw atienda el log: para saber la versión se lee el archivo.
- Probado en el menú de Adrenaline, Retro City Rampage y Gran Turismo: abre, cierra, bloquea los botones, sin saltos.

## Paso 3.2: registro del frame (NativeDraw 0.0.5, probado el 2026-10-29) [medido]
`nativedraw/registro.c` (incluido en `captura.c`), activo con `dibujar`. Por draw: tipo, cantidad, tipo de vértice,
vértices (e índices) copiados al momento, el estado (56 registros del GE, `RS`) solo cuando cambia y la paleta cargada
(copiada en cada C4). Frames cerrados en el flip, 4 registros que se turnan (`rg_pub` = último completo; eran 3 hasta la prueba 0.0.8.6:
el último que dibujó la Vita no se reescribe hasta que dibuje otro, porque la GPU lo lee después). Completo =
todos los draws contados pasaron por el registro (la memoria de sublistas, las curvas y lo que no se pudo leer lo dejan
incompleto); un frame incompleto deja de copiarse. Línea `registro:` por segundo en `nativedraw_captura.log`.
Prueba fuera de la Vita: `REG=1 ./prueba 0` (todos completos) y `REG=1 ./prueba 1` (incompletos por la memoria).

| Juego | Completos | Draws por frame | Lo que usa | Costo |
|---|---|---|---|---|
| Retro City Rampage (NPUH-10199) | 4 142 de 4 143 | **1**: un rectángulo de pantalla completa con textura T8 (paleta de 256) | el juego arma la imagen con la CPU | 5 µs |
| KOF 96 (NeoGeo Station, NPUH-10119) | 8 032 de 8 118 (7 por tope, en una transición) | ~400 en la pelea | rectángulos (sprites) T4 con swizzle (~98 %), mezcla y alfa test (~99 %), 2 fondos 5650 | ~2,4 ms; 59,9 fps |
| Gran Turismo (UCUS-98632) | 16 569 de 23 567 (menús sí; la carrera no: memoria de sublistas y 3D) | — | carrera: T4 con swizzle, alfa test 96 % | 59,8–59,9 fps en carrera |

Lección (0.0.5.1): el registro en memoria sin caché costaba 23–30 ms por frame en GT (6 µs por draw en KOF incluso con
caché: copiar de la RAM del PSP sin caché). Con caché y dejando de copiar un frame ya incompleto, GT vuelve a 60 fps.
Retro City no gana resolución (su imagen ya viene terminada en 480x272): sirve como primera prueba del dibujo (1 draw).

## Paso 3.3, primera versión: traductor 2D (NativeDraw 0.0.6, probado el 2026-10-30) [medido]
`nativedraw/traductor.c` (incluido en `presentador.c`): en el hilo de Adrenaline, en vez de copiar la imagen del PSP,
dibuja el último frame completo del registro al doble. Esta versión: modo through, rectángulos/triángulos/tiras, textura
5650/5551/4444/8888/T4/T8 sin swizzle, sin mezcla ni alfa test, sin color. Si un frame trae algo más, imagen normal.
- Las texturas de la RAM se copian en el registro al momento (DMA, LPDDR sin caché registrada para la GPU): leerlas de la
  RAM del PSP un frame después mezclaba dos frames (líneas en el título y el HUD de Retro City, 0.0.6.1).
- Un frame del registro se repite como mucho 2 veces (juego a 30 fps); si no llega otro, imagen normal (0.0.6.1 repetía
  el último para siempre y con el juego en pausa no se veía el diálogo del botón PS).
- **"Tapado"** (0.0.6.3): el diálogo del botón PS lo dibuja el sistema del PSP encima de los frames del juego, que siguen
  llegando: cada frame se comparan 64 puntos de la imagen normal con el dibujo de la Vita; con más de la mitad distintos 6
  frames seguidos se muestra la imagen normal hasta 10 frames iguales. En Retro City se activó solo con el diálogo. Las
  notificaciones y el volumen los dibuja la Vita encima de todo (no afectan).
- Retro City (pantalla de título y juego): 100 % de los frames dibujados por la Vita, 0,23 ms por frame, "perfecto".
  KOF 96: 0 % (cada frame empieza con un borrado: siguiente versión).
- Lección: registrar la RAM del PSP con `sceGxmMapMemory` (0x70000000, 32 MB) funciona; 0x72000000 da 805b0017.

## Paso 3.3 en KOF 96 (NativeLink 0.0.3 + NativeDraw 0.0.7, pruebas 0.0.7.1–0.0.8.12, 2026-10-30) [medido]
KOF 96 (NeoGeo Station, NPUH-10119) dibujado por la Vita en las peleas, sin cuadros rotos, ~40 fps de la Vita con el
juego a 59,9. Lo que se aprendió en el camino (los números de prueba sirven para ubicar los logs de
`vitabridge\etapa3_<versión>\` y los volcados):
- **Sombreadores propios** (`nativedraw/sombreadores/*.cg` → `sombreadores_nd.h`, 0.0.7): color por vértice o del
  material, función de textura modular/reemplazar (TCC, color al doble), alfa test, borrados (rectángulo del color
  pedido; sin el bit de color no dibuja nada), mezcla (un programa de fragmentos por combinación). El alfa de la
  superficie va siempre en 255: el alfa del PSP es su stencil y Adrenaline no lo usa.
- **Imágenes intermedias** (0.0.7.2): KOF dibuja la pelea a la resolución de la Neo Geo en una imagen escondida de la
  VRAM y después la estira a la pantalla. Los draws a otra imagen (ancho ≤ 512) van a una superficie propia al doble
  (hasta 2, 1024x544) y una textura en esa dirección se lee de ahí; los draws se agrupan en escenas por destino
  (`scenesPerFrame = 8`). Se reservan al iniciar el traductor (0.0.7.9: el volcado automático les quitaba la memoria).
- **Debajo, la imagen normal estirada** (0.0.7.5): KOF dibuja los bordes una vez y después solo el centro (con negro
  desaparecían). Los rectángulos se recortan al scissor (el borrado de KOF es de pantalla completa recortado al centro).
  Si ningún draw va a la imagen que se muestra (el flip llega con la otra del doble buffer), vale la del último draw.
- **Paletas de 16 colores en 64 bytes** (0.0.7.6) y una igual a la anterior se reusa: KOF carga ~260 por frame y con
  el tope de 256 de 1 KB casi todos los frames iban con la imagen normal.
- **Texturas de la VRAM** (los sprites de KOF están ahí): desde el espejo (0.0.7.3); solo los grupos de 8 filas que usan
  los draws (0.0.7.5: copiar las hojas de 512x512 enteras costaba 7–17 ms por frame y el juego bajaba a ~35 fps).
  0.0.8: se copian después del flip, de la copia del espejo de **ese** flip: el juego sube los sprites de un frame
  después del flip anterior, así que la copia con que se leyó la lista era de un frame antes (mosaicos; el volcado 013
  de 0.0.7.10 dibujado en el PC salía bien con la copia siguiente). Por DMA (~512 MB/s; la CPU lee la RAM del PSP a
  ~74 MB/s) y sin ordenar el swizzle: lo hace el sombreador `nd_fs`, por eso sin filtro.
- **Espejo completo para 2D** (NativeLink, interruptor 16, 0.0.7.6): con 64 páginas por turnos KOF (256 páginas de
  sprites) veía cada página cada 4 frames → sprites con pedazos viejos. `espejo_espera` (interruptor 32, 0.0.8.2): el
  flip del PSP espera la copia del espejo.
- **Cuadros rotos de un frame en las peleas** (0.0.8.6–0.0.8.11): primero 4 registros en vez de 3 (0.0.8.6). Después,
  midiendo con las notificaciones de GXM (0.0.8.10, líneas `gpu:` de `registros.md`): la GPU de la Vita iba 3 a 8
  escenas atrasada y ~140 frames por 10 s empezaban con el de hace 2 sin terminar; se reescribían con la GPU leyendo
  vértices y paletas (25–180 cada 10 s), la imagen normal (~200) y texturas del registro (~8–11 por segundo). Cada uno
  calzaba con un tipo de error del video: texturas → fondo con pedazos, vértices → "K.O." deformado, paletas → colores
  equivocados. Arreglo (0.0.8.11): si la GPU no terminó el frame de hace 2, no se le manda otro y se repite la
  superficie; antes de reescribir vértices/paletas o la copia de la imagen normal se espera a la GPU (como mucho 30 ms);
  el registro siguiente es uno que la GPU ya terminó de leer. Resultado: 0 reescritos.
- **Rendimiento** (0.0.8.12): la revisión de frames distintos usaba `sceGxmFinish` (~85 veces cada 10 s, ~20 ms cada
  una); ahora se salta si la GPU no terminó la superficie. Peleas: ~35 → ~40 fps de la Vita, CPU por frame ~13 →
  ~6,5 ms, esperas a la GPU ~1,5 s → 0 cada 10 s.
- **Ritmo** (0.0.8.9): los juegos de Neo Geo hacen transparencias dibujando algo un frame sí y otro no; con saltos o
  repeticiones destella (cuadro HOW TO PLAY). Línea `traductor: ritmo`.
- Lección: grabar la pantalla (Vitastream + OBS a 60 fps) encontró lo que los volcados no capturaban; el detector de
  cuadros rotos de un frame en un video está en `vitabridge\respaldo\limpieza_0.0.8.11\transitorios.py`. Usar videos
  solo cuando haga falta (`FLUJO_DE_TRABAJO.md`).
- Lo que sigue: la GPU de la Vita tarda ~20 ms por frame de KOF (~430 draws) y cada 10 s ~165 frames no se mandan
  porque va atrasada; el traductor gasta ~7 ms de CPU por frame. Medir el tiempo de GPU por escena antes de cambiar.

## Rendimiento de la GPU y Metal Slug XX (pruebas NativeDraw 0.0.8.13–0.0.8.21, 2026-10-30) [medido]
Cronómetro de la GPU (`cronometro.c`, interruptor `medir_gpu`, líneas `gpu tiempos:` de `registros.md`): cada escena
(nuestra y de Adrenaline) anota su fin con una notificación de GXM (510 para las de Adrenaline, 511 la nuestra); un hilo
calcula cuánto tardó cada una.
- **KOF 96, antes:** la GPU al 96–99 %, ~26 ms por frame del PSP (imagen intermedia ~16 ms con ~430 draws, dos escenas
  de la superficie ~3 + ~5,5 ms, Adrenaline ~2 ms) → ~40 fps. Modos de prueba (0.0.8.15, ya quitados): sin texturas la
  intermedia bajaba a ~6,5 ms y con el swizzle leído como lineal a ~7,9 ms → **~8 ms eran el cálculo del swizzle en el
  sombreador** `nd_fs`. La memoria de las superficies (CDRAM o LPDDR) casi no influía. Lección (0.0.8.14): reservar la
  intermedia en CDRAM dejó 1 MB libre y Adrenaline mostró estática en las vistas previas: **nunca CDRAM**.
- **0.0.8.16: texturas con swizzle ordenadas por la CPU** al copiarlas al registro (`rgTexCopia`: DMA a una memoria de
  paso con caché y la CPU deja cada grupo de 8 filas ordenado; la GPU las lee con `nd_ft`, con filtro si el juego lo
  pide). Se quitaron `nd_fs` y `trPrueba`. Al iniciar se comprueba que el DMA deja al día la memoria con caché
  (`rgCoherente`: sí). Volcados versión 4 (texturas ya ordenadas). Intermedia de KOF: ~16 → ~7–8,7 ms.
- **0.0.8.18: una sola escena para la superficie.** Si solo los draws a la superficie leen imágenes intermedias y
  ninguna se dibuja después de leída, primero van todas las intermedias y después la superficie en una escena (cada
  escena lee y escribe su superficie entera: KOF pasa de T → I → T a I → T, ~3 ms menos). **KOF 96 a 60 fps en las
  peleas** (615–633 frames dibujados cada 10 s, ~16 ms de GPU por frame, GPU al 87–97 %).
- **Seguro de pantalla negra quitado** (0.0.8.18, a pedido del usuario): en las peleas se activaba porque revisaba la
  superficie con la GPU sin terminar (0.0.8.17: todos los frames "negros" se leyeron así). En `TODO.md`.
- **Metal Slug XX (ULUS-10495):** el juego corre a 30 fps; la partida es 2D through (los menús, 3D). La Vita lo dibuja
  entero a su velocidad: intermedia ~13,5 ms (~2 millones de píxeles, ~3,6 veces la pantalla), superficie ~7,2 ms, GPU
  ~75 %. Texturas (línea `texturas:`, 0.0.8.19): todas de la RAM del PSP, 630–800 KB por frame en 90–170 copias (la
  mitad de menos de 1 KB), filas de 128 bytes, **~98 % iguales a la copia anterior** (KOF: de la VRAM, 140–220 KB en
  8–12 copias, ~99 % iguales). Lo caro es el DMA: ~100 µs por llamada (10–17 ms por frame en Metal Slug, ~1,5 ms en
  KOF); ordenar ~5,5 ms. Promedia 90–126 texturas distintas por frame (tope 128).
- **Glitch blanco de Metal Slug XX** (0.0.8.20 lo encontró): cuadros de 16x16 del fondo llegaron con posiciones de
  ~2^31 y uv de 65535; la GPU los rellenaba repitiendo una textura de 32x32 (pantalla blanca con colores, escenas de
  55–1900 ms). Causa: el capturador iba atrasado (copiar texturas costaba 20–28 ms de cada frame de 33 ms) y la
  compuerta espera como mucho 5 ms: copió vértices que el juego ya reescribía. 0.0.8.21: un vértice fuera de ±4096 o NaN
  manda ese frame con la imagen normal (motivo `vertice_fuera`). Lo que ataca la causa: el caché de texturas.
- Lo que sigue: **caché de texturas entre frames** (ordenadas en memoria propia de la Vita; revisión barata por muestra
  en cada frame, completa por turnos 1 de cada N grupos, e invalidación cuando el GE escribe en esa memoria; el usuario
  aceptó que en casos raros una textura se vea vieja hasta N frames). Después, cachés del traductor (paletas, draws
  traducidos, escenas iguales).

## Estado
| Paso | Estado |
|---|---|
| 3.0 | **hecho** (NativeDraw 0.0.3) |
| 3.1 | **hecho** (NativeDraw 0.0.4) |
| 3.2 | **hecho** (NativeDraw 0.0.5) |
| 3.3 | Retro City (NativeDraw 0.0.6), **KOF 96** (NativeDraw 0.0.7; 60 fps en las peleas desde la prueba 0.0.8.18) y **Metal Slug XX** (a sus 30 fps; glitch blanco evitado en 0.0.8.21). Sigue: caché de texturas |
| 3.4 | adelantado en parte: comparación con la imagen normal (detecta lo que el sistema dibuja encima) |
