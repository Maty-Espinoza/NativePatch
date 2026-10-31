# Descubrimientos, mediciones y límites

Lo que se aprendió probando en la Vita, resumido y ordenado por tema. Cada punto dice en qué versión o etapa se vio;
el detalle (números completos, logs, versiones de prueba) está en `../../changelog/` y, para la etapa 3, en
`../../agent/es/stage3-plan.md`. Marcas: **[medido]** comprobado en la Vita; **[deducido]** conclusión sin prueba
directa.

## El hardware
- La Vita trae el hardware de la PSP dentro de su chip: CPU MIPS y chip gráfico (GE) reales. El juego no se emula por
  software. El MIPS no tiene acceso a la pantalla, la tarjeta, el audio ni los controles: todo eso lo atiende el ARM de
  la Vita a través de llamadas remotas (Kermit). Detalle: `adrenaline.md`.
- **La VRAM del PSP (2 MB) no está mapeada en el proceso de Adrenaline** [medido]: la Vita no puede leerla. Por eso
  existe el espejo: NativeLink le pide al GE que copie a la RAM normal las páginas que la Vita necesita.
- La RAM del PSP se ve desde el ARM en `0x68000000 + dirección` sin caché. **Leerla es lo caro** (~2 µs por línea de 64
  bytes) [medido]: casi todo el costo del capturador es esa espera, no interpretar. Leer de a 256 bytes con NEON solapa
  las esperas; el DMA (`sceDmacMemcpy`) copia a ~512 MB/s contra ~74 MB/s de la CPU, pero cada llamada cuesta ~100 µs.
- **La GPU de la Vita puede leer la RAM del PSP directo** si se registra con `sceGxmMapMemory` (0x70000000, 32 MB), a la
  misma velocidad que su propia memoria [medido, etapa 1]. Con 0x72000000 falla.
- ARM a 444 MHz, GPU SGX543MP4+ a 222 MHz, bus 222 MHz [medido, etapa 1]. En el proceso de Adrenaline hay ~33 MB de
  LPDDR libres y 4 MB de CDRAM.
- Reservar memoria **CDRAM** dentro de Adrenaline rompe los videos de la Vita (vistas previas, cinemáticas: estática o
  negro). Solo había 4 MB y los usan los videos. **Nunca CDRAM** [medido, 0.0.3.2 y 0.0.8.14].
- Con un destino de dibujo propio en GXM hay que usar **siempre una superficie z** (D24S8): sin z la GPU no dibuja o
  Adrenaline se congela [medido, 0.0.3.3–0.0.3.4].

## Por qué se cambió de arquitectura (GePatch → NativePatch)
- GePatch hacía dibujar al GE del PSP 4 veces más píxeles. Es un **límite físico del GE** (relleno de píxeles), no un
  error: la relación de tiempo con/sin GePatch era 3–4, igual que la cantidad de píxeles. Gran Turismo bajaba a 24–37
  fps, Silent Hill a 15–21 [medido]. Ninguna optimización de CPU podía arreglarlo.
- Además engañaba al juego con una VRAM falsa en la RAM, y de ahí venían casi todos los errores de imagen.
- La única otra GPU disponible es la de la Vita, y el único procesador libre es el ARM: el PSP dibuja normal (nadie ve
  esa imagen) y la Vita dibuja en HD. Si no sabe dibujar algo, muestra la imagen del PSP. Análisis completo:
  `architecture-and-roadmap.md`.

## Capturar sin frenar al juego (etapa 2)
- Con NativeLink y el capturador de NativeDraw activos, Gran Turismo va a 60,2 fps en lo más pesado (~80 mil comandos
  por frame, 13,4 ms de Vita por frame), Ridge Racer a 60 toda la carrera, LocoRoco a 59,9 y Silent Hill a su velocidad
  original [medido, v13.13 = NativeDraw 0.0.1].
- Lo que lo hizo posible: **memoria de sublistas** (una sublista idéntica al frame anterior se aplica sin recorrerla; en
  GT ~85 % de los comandos vienen de sublistas que se reconocen iguales), **lectores adelantados** en los núcleos 0 y
  2 que traen las líneas a una caché compartida, **comprobación por adelantado** de las sublistas que se van a aplicar,
  **interpretar de a línea** de 64 bytes, y la **compuerta antes de encolar** la lista siguiente en vez de en el sync.
- El capturador fijo en un núcleo pierde ~17 % de tiempo (comparte con el audio o el Media Engine): dejarlo libre en los
  3 núcleos [medido, v13.13].
- La compuerta espera como mucho 5 ms y nunca si la Vita no responde: el juego no se cuelga aunque NativeDraw falle.
- **El juego sí reescribe sus listas**: la memoria de la lista del frame N la reusa al encolar la siguiente. Por eso la
  compuerta va antes de encolar y NativeLink lo comprueba solo con huellas (si una lista cambia antes, vuelve al sync).
- Revisión permanente: 1 de cada 32 sublistas aplicadas se recorre igual y se compara (DISTINTAS en el log). Debe ser 0
  y lo es desde v13.12 (antes 1–7 por corrida por comparar contra el espejo equivocado).
- Falló: leer la RAM del PSP directo de a 256 bytes en vez de la caché compartida (v13.11.1), no releer líneas ya
  comprobadas (v13.8), FSR 1 del lado de la Vita como atajo (no mejoraba la imagen, daba tirones y tearing).

## Dibujar en la Vita (etapa 3)
- **Presentador:** reemplazar la textura de 512x272 que dibuja Adrenaline por una superficie propia del doble exacto
  funciona y cuesta ~0,15 ms por frame [medido, 0.0.3]. Adrenaline ubica y escala la superficie igual que su textura, así
  que se respetan la escala, la posición y la proporción de su menú. La imagen se ve un frame después.
- La matriz `wvp` de los sombreadores de vita2d va **por columnas** (desplazamiento en [12] y [13]; `mul(pos, wvp)`)
  [medido, 0.0.3.1: un trapecio naranja hasta entenderlo].
- Adrenaline copia la imagen del PSP a su textura con `sceDmacMemcpy` en cada vuelta. La GPU la lee para NativeDraw
  después que para Adrenaline, y la copia del frame siguiente la pisaba (franjas): se desvía a 4 copias propias
  alternadas [medido, 0.0.3.5 y 0.0.8.x].
- **Cuadros rotos de un frame** (texturas con pedazos, letras deformadas, colores equivocados): la GPU de la Vita iba
  3 a 8 escenas atrasada y la CPU reescribía vértices, paletas, texturas y la copia de la imagen mientras la GPU
  todavía los leía [medido con las notificaciones de GXM, 0.0.8.10]. Solución: cada escena propia anota su fin en una
  palabra libre de las notificaciones de GXM; no se manda otro frame si no terminó el de hace 2; antes de reescribir se
  espera (como mucho 30 ms); el registro siguiente es uno que la GPU ya terminó de leer. Reescritos con la GPU leyendo:
  0 [medido, 0.0.8.11].
- Leer una superficie que la GPU no terminó da resultados falsos, y esperarla con `sceGxmFinish` cuesta ~20 ms en KOF 96:
  las revisiones (frames distintos, "tapado", seguro de pantalla negra) solo miran superficies ya terminadas, o se saltan.
- **Texturas:** las del juego en la RAM del PSP hay que copiarlas al momento (el juego las cambia antes de que la GPU las
  lea); las de la VRAM se copian del espejo de la copia del flip de **ese** frame (KOF sube los sprites de un frame
  después del flip anterior: con la copia anterior salían mosaicos). Solo se copian los grupos de 8 filas que los
  vértices usan: copiar hojas de 512x512 enteras costaba 7–17 ms por frame [medido, 0.0.7.5].
- **Swizzle:** el GE guarda las texturas en bloques de 16 bytes x 8 filas. Ordenarlo en el sombreador costaba ~8 ms de
  GPU por frame en KOF 96 (intermedia de 16 ms); ordenarlo con la CPU al copiar (DMA a memoria de paso con caché, que el
  DMA sí deja al día) lo bajó a 7–8,7 ms y permite usar filtro bilineal [medido, 0.0.8.15–0.0.8.16].
- **Una escena por destino, y la superficie al final:** cada escena de GXM lee y escribe su superficie entera; si solo
  los draws a la superficie leen imágenes intermedias, primero van todas las intermedias y después la superficie en una
  sola escena (~3 ms menos). Con eso **KOF 96 a 60 fps en las peleas** (~16 ms de GPU por frame, GPU al 87–97 %)
  [medido, 0.0.8.18].
- **Lo que se repite entre frames:** ~98–99 % de los bytes de textura que se copian en cada frame son iguales a la copia
  anterior (KOF 96 y Metal Slug XX) [medido, 0.0.8.19]. Lo caro es el DMA (~100 µs por llamada; Metal Slug 10–17 ms
  por frame en 90–170 copias). De ahí el siguiente paso: un caché de texturas entre frames.
- **Capturador atrasado = vértices basura:** si copiar texturas tarda más que un frame, la compuerta (5 ms como mucho)
  deja pasar al juego y se copian vértices que el juego ya reescribía (posiciones de ~2^31, uv 65535; la GPU rellenaba
  toda la pantalla con una textura: "glitch blanco" de Metal Slug XX). Un vértice fuera de ±4096 manda el frame con la
  imagen normal [medido, 0.0.8.20–0.0.8.21]. La causa la ataca el caché de texturas.
- Los juegos de Neo Geo hacen transparencias dibujando algo un frame sí y otro no: si la Vita repite o salta frames,
  destella. Se mide con la línea `traductor: ritmo` del log.
- **Grabar la pantalla** (Vitastream + OBS a 60 fps) encontró los cuadros rotos de un frame que ningún volcado capturaba.
  Se usa solo cuando los volcados y logs no alcanzan.

## Gran Turismo y el 3D (lo que viene)
- GT tiene ~9000–15 000 draws por frame, ~5300 con textura nueva. En GXM un draw cuesta ~1 µs sin cambios, ~2 µs con
  textura o programa nuevo, ~1,3 µs de GPU aunque no pinte [medido, etapa 1]. **Grabar GT draw por draw no entra** ni
  en 3 núcleos (~24 ms de procesador y ~16 ms de GPU por frame; queda ~1,5 núcleo libre) [medido, etapa 2 v13.7].
- Camino previsto: grabar una vez cada sublista estable y reusar la lista GXM grabada (en GT ~85 % de los comandos
  vienen de sublistas que la captura ya reconoce iguales) y juntar draws con el mismo estado (2000 cuadros en un draw =
  0,02 µs por cuadro).
- GT y RR guardan datos fijos en la VRAM (texturas, sublistas, vértices): necesitan el espejo. SH no.

## Límites conocidos
| Límite | Por qué | Qué se hace |
|---|---|---|
| No se dibuja 3D | el traductor solo entiende el modo "through" (2D en píxeles) | imagen normal; etapa 4 |
| Un frame de atraso en la imagen HD | la Vita dibuja el frame N mientras el juego arma el N+1 | es la latencia típica de un emulador con triple buffer |
| Juegos de PS1 | Adrenaline 8 los muestra desde el emulador de PS1 de la Vita (`pops_tex`), no desde el GE | no se tocan; habría que enganchar ese emulador (otro proyecto) |
| Filtro "Original" de Adrenaline | Adrenaline no dibuja con GXM y el presentador no puede actuar | usar Bilinear |
| Lo que el sistema del PSP dibuja encima (diálogo del botón PS) | no pasa por las listas del juego | "tapado": se compara con la imagen normal y se muestra la normal mientras dure |
| Frames con sublistas aplicadas por la memoria | sus draws no pasan por el registro | imagen normal (los juegos 2D casi no usan sublistas) |
| Una textura cambiada solo en bytes que la revisión no ve puede verse vieja unos frames | riesgo aceptado del caché de texturas (diseño en curso) | revisión completa por turnos |
| Mezclas sin equivalente directo en GXM (alfa del destino, factores al doble), test de color, operaciones lógicas | GXM fijo no las tiene (sí tiene mezcla programable, sin usar todavía) | imagen normal, motivo `mezcla` / `test_color_o_logica` |
| Dependencia de Adrenaline | se enganchan importaciones de AdrenalineUser y ScePspemu | cada versión de Adrenaline puede romperlo; a futuro, proponer un punto de enganche oficial |

## Cosas que se midieron y no eran lo que parecían
- vpjuego: "GE en stall" siempre 0 %: para los tiempos del GE sirven las horas de encolado y FINISH (T6).
- El núcleo 0 de la línea `procesador` del log sale casi vacío: el gancho de CreateThread no ve los hilos que Adrenaline
  creó antes de cargar NativeDraw.
- Leer la hora en la Vita cuesta ~390 ns: el registro extremo completo altera lo que mide.
- `sceCommonDialogIsRunning` siempre dice "no" aunque el diálogo del botón PS esté abierto: no sirve para detectarlo.
- Las funciones de DMA del PSP (`sceDmacMemcpy`) no se encontraron con ningún nombre de módulo en Adrenaline 8 (T4
  incompleto; no hizo falta).
