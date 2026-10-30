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
- Seguro: si la superficie sale negra 30 frames seguidos y la imagen del juego no, deja de reemplazarla hasta cerrar
  Adrenaline. El log marca "primer frame dibujado" y "60 frames dibujados" (para ver dónde se cuelga si pasa).
- Después de la escena de Adrenaline, en su hilo y su contexto, dibuja `psp_tex` al doble en una de dos superficies
  propias (CDRAM, 1024x544 con 960x544 visibles, z D24S8). En el frame siguiente, cuando Adrenaline usa `psp_tex`, se le
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
(copiada en cada C4). Frames cerrados en el flip, 3 registros que se turnan (`rg_pub` = último completo). Completo =
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

## Estado
| Paso | Estado |
|---|---|
| 3.0 | **hecho** (NativeDraw 0.0.3) |
| 3.1 | **hecho** (NativeDraw 0.0.4) |
| 3.2 | **hecho** (NativeDraw 0.0.5) |
| 3.3 | **primera versión** (NativeDraw 0.0.6): Retro City dibujado por la Vita. Sigue KOF 96 |
| 3.4 | adelantado en parte: comparación con la imagen normal (detecta lo que el sistema dibuja encima) |
