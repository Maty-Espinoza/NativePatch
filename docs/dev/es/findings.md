# Descubrimientos, mediciones y límites

Lo que se aprendió probando en la Vita, resumido y ordenado por tema. Cada punto dice en qué versión o etapa se vio;
el detalle (números completos, logs, versiones de prueba) está en `../../changelog/` y, para la etapa 3, en
`../../agent/en/stage3-plan.md`. Marcas: **[medido]** comprobado en la Vita; **[deducido]** conclusión sin prueba
directa.

## El hardware
- La Vita trae el hardware de la PSP dentro de su chip: CPU MIPS y chip gráfico (GE) reales. El juego no se emula por
  software. El MIPS no tiene acceso a la pantalla, la tarjeta, el audio ni los controles: todo eso lo atiende el ARM de
  la Vita a través de llamadas remotas (Kermit). Detalle: `adrenaline.md`.
- **La VRAM del PSP (2 MB) no está mapeada en el proceso de Adrenaline** [medido]: la Vita no puede leerla. Por eso
  existe el espejo: NativeLink le pide al GE que copie a la RAM normal las páginas que la Vita necesita.
- **Tampoco se puede leer desde el kernel de la Vita** [medido, NativeDraw 0.0.28.2-0.0.28.7, con una mitad de kernel que ya no está
  en el repo]. La VRAM es la "Compati SRAM" de la Vita (eDRAM Tachyon, 2 MB en la dirección física 0x1C000000, wiki de HENkaku
  *Physical Memory*). Un plugin de kernel la mapea en el proceso de Adrenaline sin problema (`USER_IO_DEVICE_R`), pero mientras el
  PSP corre: la primera lectura de la **CPU** (aunque sea una sola palabra de 32 bits) detiene la Vita; una copia por **DMA** nunca
  termina (el hilo que la pide queda bloqueado); el driver de la **GPU** se niega a mapearla (`sceGxmMapMemory` →
  `SCE_GXM_ERROR_DRIVER`), y si se pide un bloque CDRAM en esa dirección el kernel ignora la dirección. La causa calza con la página
  *Memory System* de la wiki: esa memoria tiene un firewall propio en el bus (`Compati_2MiB`), configurado por la parte segura, y
  todo bus error de la CPU detiene el sistema. Fuera del alcance de un plugin de kernel no seguro. Detalle:
  `claude/analisis_estructura_60fps_2026-11-06.md` §10 (proyecto) y los logs en `vitabridge/stage3_0.0.28.{2..7}/`.
- **Un "proxy" del lado PSP** (NativeLink atendiendo las lecturas de la VRAM que pide la Vita) es lo que ya es el espejo: solo el
  hardware de compatibilidad puede tocar la VRAM (el lado PSP y, muy probablemente, el DMA de pantalla de compatibilidad, abajo), y lo que se elige es quién copia y cuándo. El GE en el flip (hoy, 1,3-3 ms de GE por
  frame) copia la VRAM tal como quedó al terminar el frame. El DMA del PSP cuesta lo mismo (~2,3-2,7 ms por MB) y frena la CPU del
  juego un 10-20 % por el bus. Un hilo de la CPU del PSP que responda al instante leería la VRAM a mitad del frame que el juego está
  dibujando (imágenes mezcladas) y le cobraría el trabajo a la CPU del juego. El Media Engine no es un núcleo libre en la Vita
  (Adrenaline lo emula desde el ARM, el hilo `ScePspemuRemoteMe`). Serviría para errores (páginas viejas, texturas un frame tarde),
  no para fps: el espejo es una parte chica del costo del frame. Ideas, si algún día hace falta: copiar las páginas de una imagen
  intermedia al terminar la lista que la dibuja, no en el flip (un frame menos de latencia), y medir qué páginas pedidas ya tiene
  NativeDraw (sus propias imágenes intermedias en HD, copias repetidas desde la RAM) para dejar en el espejo solo lo que el juego
  escribe en la VRAM con la CPU (`TODO.md`, "VRAM mirror").
- **El DMA de pantalla de compatibilidad** (`SceCompatLCDDMA`, registros en 0xE5071000 del lado ARM: ancho, alto, una dirección
  física y banderas de "terminado"; wiki de HENkaku *PSP Emulator*) es muy probablemente lo que lleva la imagen del PSP desde su
  VRAM al búfer 0x74000000 que copia Adrenaline [deducido: por su nombre y sus registros]. O sea, el firewall del bus deja pasar
  al menos a esa unidad. Reprogramarlo para copiar otras páginas de la VRAM es un camino posible, no probado y riesgoso: solo
  desde el kernel, registros documentados a medias, y un error puede romper la imagen que hoy funciona.
- La RAM del PSP se ve desde el ARM en `0x68000000 + dirección` sin caché. **Leerla es lo caro** (~2 µs por línea de 64
  bytes) [medido]: casi todo el costo del capturador es esa espera, no interpretar. Leer de a 256 bytes con NEON solapa
  las esperas; el DMA (`sceDmacMemcpy`) copia a ~512 MB/s contra ~74 MB/s de la CPU, pero cada llamada cuesta ~100 µs.
- **La RAM del PSP vive en la CDRAM de la Vita**, la memoria de la GPU: los registros de SceGrab 0xE8300120-0xE830012C guardan
  "base de la CDRAM + 1" para tres ventanas de 16 MB y para flash0 en base + 0x3000000 (wiki de HENkaku *PSP Emulator*; la wiki
  marca la correspondencia con "?"). Calza con lo medido: el ARM llega a la CDRAM por un camino lento, así que el costo es la
  memoria y no los atributos de caché (una vista con caché solo era 1,7 veces más rápida, NEON de a 256 bytes da los mismos
  ~91 MB/s que `memcpy`), mientras que el DMA y la GPU la leen rápido. Leer más rápido con el ARM no rinde; sí rinde leer menos
  con el ARM, o que lea el DMA, el GE del PSP o la GPU (`performance-plan.md`, C). Probablemente también explica [deducido]: la
  partición de 112 MiB de CDRAM para juegos (wiki *Memory budget*) es casi toda la RAM del PSP, por eso quedan solo 4 MB libres en
  el proceso de Adrenaline; `sceCompatAllocCdramWithHole` (que las notas A3 tomaron como respaldo de la VRAM) reserva esa RAM; el
  gancho de kernel de Adrenaline `SceGrabForDriver_E9C25A28` (0x21000001 → 0x22000001) mueve una de esas ventanas a otro bloque
  de CDRAM.
- **La GPU de la Vita puede leer la RAM del PSP directo** si se registra con `sceGxmMapMemory` (0x70000000, 32 MB), a la
  misma velocidad que su propia memoria [medido, etapa 1]. Con 0x72000000 falla. Causa probable [deducido]: pasados los primeros
  32 MB la RAM no es un solo bloque continuo de CDRAM (las ventanas de 16 MB de arriba; Adrenaline guarda bloques extra en las
  direcciones físicas 0x23000000/0x24000000). Registrarla en trozos de 16 MB no se ha probado (`TODO.md`).
- ARM a 444 MHz, GPU SGX543MP4+ a 222 MHz, bus 222 MHz [medido, etapa 1]. En el proceso de Adrenaline hay ~33 MB de
  LPDDR libres y 4 MB de CDRAM.
- **Caché L2:** un PL310 de 16 vías × 128 KB = 2 MB, compartido por los cuatro núcleos, con prefetch por hardware activado (wiki
  de HENkaku *Caches*, valores configurados; el tamaño se calcula de ellos). El prefetch a mano aporta poco, como se midió
  (`performance-plan.md`), y cada byte de la foto, el récord y el traductor compite ahí con el propio Adrenaline: mantenerlos
  chicos vale más de lo que sugiere su tiempo de copia [deducido].
- **Programas de la GPU (SGX543, wiki *SGX543*):** el descarte de píxeles (`kill`), la profundidad escrita por el programa
  (`depthf`) y los formatos de media precisión (f16, fx10) son instrucciones normales del shader. En las GPU PowerVR, que ordenan
  cada baldosa y pintan solo los píxeles visibles, `kill` y `depthf` apagan eso para el dibujo [conocimiento general de PowerVR,
  no medido aquí]: de ahí los programas sin `discard` donde se pueda (`performance-plan.md`, C.3). No explica el costo de los
  planos de recorte: `discard` solo costó ~2 ms en la misma prueba.
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
- Revisión permanente: 1 de cada 32 sublistas aplicadas se recorre igual y se compara (MISMATCHES en el log). Debe ser 0
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
  por frame en 90–170 copias). De ahí el caché de texturas entre frames.
- **Caché de texturas (texturas de la RAM):** las texturas quedan ordenadas en un almacén que lee la GPU y se revisan por
  muestra. En Metal Slug XX el capturador bajó de 19,4 a 8,3 ms por frame, las texturas de 14,5 a 4,1 ms, lo copiado de 746
  a 3 KB y las llamadas al DMA de 44 a 0, sin nada fuera del almacén y sin errores visuales reportados [medido, 0.0.10.1].
  La revisión cuesta ~2,5 ms por frame. Las texturas de la VRAM (peleas de KOF 96, DMA ~1 ms + ordenar ~2 ms por frame) no
  pasan por el caché.
- **Muestra del caché y texturas chicas:** valores separados justo por una fila caen todos en la primera columna, normalmente
  transparente, y un sprite nuevo pasa inadvertido. Street Fighter Alpha 3 MAX arma sus personajes con texturas T4 de 64x64 y 2 KB en
  la RAM, y salían con piezas ajenas. Ahora cada valor está en otra columna, y las texturas de menos de 8 KB quedan fuera del
  caché: copiarlas cuesta poco y cambian seguido. Metal Slug XX queda en capturador 9,7 ms, texturas 5,3 ms, revisión ~3,2 ms
  [medido, 0.0.11.3].
- **Street Fighter Alpha 3 MAX en HD** (abanicos, 0.0.11.3): ~700 draws por frame, la imagen intermedia toma ~16 ms de GPU, la
  superficie ~4 ms y el traductor ~57 % de un núcleo: la pelea muestra ~32 frames HD por segundo con el juego a 60.
- **Capturador atrasado = vértices basura:** si copiar texturas tarda más que un frame, la compuerta (5 ms como mucho)
  deja pasar al juego y se copian vértices que el juego ya reescribía (posiciones de ~2^31, uv 65535; la GPU rellenaba
  toda la pantalla con una textura: "glitch blanco" de Metal Slug XX). Un vértice fuera de ±4096 manda el frame con la
  imagen normal [medido, 0.0.8.20–0.0.8.21]. La causa la ataca el caché de texturas.
- **Metal Slug XX en 0.0.8.23** (corrida `stage3_0.0.8.23/ms`): 30 fps sostenidos durante el juego (121 s, ninguno bajo
  30). El capturador tarda ~25 ms de cada frame de 33 ms (p90 30 ms); copias de texturas: 358 KB por frame, DMA ~5 ms +
  ordenar el swizzle ~3 ms. 2 puntos lentos: ~2 s a 38–40 ms por frame con picos de 70 ms. Una ráfaga de 174.353 comandos
  en un frame: 182 ms, 25.054 draws, 2 frames llegaron al tope del registro; 44 frames `vertex_out` en esa ventana
  [medido].
- **Costo de copiar texturas** (Metal Slug XX, 27 segundos de 0.0.8.24): DMA ≈ 98 µs por llamada + 2 µs por KB (R² 0,998).
  La mayoría de las copias pesa menos de 1 KB, así que el costo son las llamadas, no los bytes. Bajo 8 KB copia la CPU
  [medido, 0.0.8.26]: en segundos con ~390 draws el capturador bajó de 34 ms a 22 ms por frame. Ordenar se hizo más lento
  (~5,9 → 8,4 ms), porque lee directo la memoria del PSP, pero la ganancia total es de ~11 ms.
- **`vertex_out` en Metal Slug XX viene del capturador atrasado** (una muestra, 0.0.8.25): en la memoria de vértices había
  comandos del GE de una lista reescrita, con los vértices reales 20 bytes más adelante, y la compuerta estaba en su tope
  alrededor de ese frame. Con el capturador a tiempo (0.0.8.26) el mismo juego tuvo 0 `vertex_out`, 0 frames en el tope y
  mantuvo 30 fps [medido].
- **Street Fighter Alpha 3 MAX no abría** (C1-2858-3 o 80020148, solo con NativeLink): el juego cambia la imagen antes
  que cualquier otra cosa, y NativeLink leía el ID del juego desde el disco en esa primera llamada. La caída estaba dentro
  de esa función; el ID ahora sale de los datos del kernel [medido, 0.0.3.10].
- **Imagen negra por datos faltantes:** el capturador partía desde los eventos que había al ver el buzón y se saltaba las
  primeras listas del juego. Este juego configura la prueba de alfa solo ahí, así que cada draw tenía "nunca pasa". Ahora
  el capturador recorre el anillo desde el inicio [medido, 0.0.8.29]. Las peleas de este juego usan abanicos de triángulos,
  que aún no se dibujan: imagen normal.
- **3D en la Vita (Ridge Racer):** el traductor hace en la CPU el trabajo del GE por vértice; x, y, z conservan su w de recorte, así la GPU
  recorta y corrige la perspectiva. Selector [medido]: traductor ~30-38 ms por frame, GPU ~31 ms con 4,2 M de píxeles y 1000 dibujos.
- **La memoria de sublistas y el registro no se mezclan:** una sublista saltada deja el frame incompleto. Ahora solo corre en frames de más
  de 4096 dibujos. Ridge Racer se saltaba el 6 % de sus comandos y perdía casi todos los frames por eso [medido].
- **La captura puede perder estado del GE** fijado antes de leer las primeras listas; qué registro falta cambia entre corridas, por eso falla
  solo a veces (Ridge Racer: `c9`, fondos negros). `sceGeGetCmd` da los valores reales y funciona en Adrenaline [medido].
- **Qué decide los fps de Ridge Racer:** ~10 ms de trabajo propio más dos esperas en la compuerta de hasta 5 ms. Si pasa de 16,6 ms baja
  directo a 30 fps [medido]. Su lista cambia antes de encolar la siguiente, así que la compuerta queda en el sync.
- **El límite es leer, no ordenar:** calcular huellas de texturas para no ordenarlas cuesta casi lo mismo que ordenarlas. Los vértices 3D de
  Ridge Racer están repartidos y fuera de la lista; el DMA por ventanas completas empeoró el capturador (26 ms) [medido].
- **Los tirones en la carrera de Ridge Racer venían de escribir `nativedraw.log` dentro del hilo de dibujo de Adrenaline** (cada
  cambio del resultado del traductor escribía una línea; una escritura en la tarjeta tarda de decenas a cientos de ms): picos del
  presentador de 50-558 ms con el traductor en ~0. Con el log por un anillo y el hilo escritor, los picos son de 5-8 ms [medido, 0.0.13.6].
- **Un frame HD en la carrera cuesta ~100 ms de CPU** (1900 dibujos; 76 ms son los vértices 3D), y en la carrera el juego alterna cada
  2 frames entre dibujable y dibujar a otro destino (`other_target`): un frame HD aislado es un tirón más un salto de un frame atrás.
  La HD ahora espera un segundo de frames dibujables; la carrera queda en imagen normal a 59,9 fps [medido, 0.0.13.7].
- **Adrenaline pone la GPU a 222 MHz** (crossbar 111, CPU 333, bus 222): los ~31 ms de GPU del selector no son un reloj bajo [medido].
- **La copia del espejo se ve terminada ~20 ms después del flip** (`mirror_ge`, es un tope: el capturador mira entre listas), y NativeLink
  nunca encontró la copia de hace dos frames todavía en el GE (`busy=0`) [medido, 0.0.13.6].
- **Reconstruir paletas por dibujo costaba 116 ms por frame** en el selector: 1000 dibujos de 256 entradas [medido].
- **Bloom de Ridge Racer:** reduce la pantalla a 088000 (la memoria del z), resta, difumina sumándose a sí misma 4 veces con mezcla gris fija (src y dst * 4b4b4b) y la suma a la pantalla con color test. Casi todos los frames [medido, 0.0.14].
- **La profundidad no sobrevive a un corte de escena:** con el corte entre el borrado y el mundo, el mundo desaparecía (la profundidad de fondo 1.0 es "cerca" para el GEQUAL del GE). Reordenar lo evita; si no, se fuerza guardar y cargar [medido, 0.0.14.5].
- **Un capturador atrasado lee listas reescritas y la basura queda en el estado:** valores de emisión como 0x400011 teñían todo el mundo por segundos, hasta que el juego volvía a escribir el registro. Lo resolvieron grabar solo los frames que el presentador toma y restaurar el último estado limpio [medido, 0.0.14.8-0.0.14.10].
- **El reflejo de lente y los halos los calcula el juego con el GE:** muestrea 64 píxeles con puntos, dibuja una paleta con el resultado y con ella dibuja sprites T8. La Vita lee el resultado del espejo, un frame tarde [medido, 0.0.14.13].
- **Superficies sin terminar:** Adrenaline podía mostrar una superficie a medio dibujar. Mostrar la anterior resolvió los pedazos de un frame [video, 0.0.14.11].
- **sceGxmCreateRenderTarget:** máximo 8 escenas por frame (con 24 da 805b0003 y el presentador queda apagado) [medido, 0.0.14.2].
- **La región válida también es el viewport:** una escena con región 128x128 en un target de 1024x544 dibujaba todo encogido por 128/1024 y 128/544 (16x30 píxeles), y el bloom salía de 228 de ancho en vez de 484 [medido con `peek`, 0.0.15.9]. La matriz debe ajustarse a la región. Los reflejos leían el resto viejo de la imagen: los bloques blancos.
- **Memoria sin caché:** leer una palabra cuesta 153 ns contra 21 con caché; escribir, 9 contra 6 [medido]. Lo que duele es releer, no escribir. El reloj (`sceKernelGetProcessTimeLow`) cuesta ~0,6 us: los cronómetros por draw suman milisegundos en desarrollo.
- **Un vértice a 333 MHz:** 0,92 us 3D, 1,73 con luz sin luces, 1,83 con niebla, 2,86 con una luz [medido en draws sintéticos, 0.0.15.5]. En Ridge Racer casi ningún vértice con luz tiene una encendida. 3,3 us en el juego, con datos fríos.
- **CPU y GPU una detrás de otra eran el límite:** 40 + 40 ms daban ~12 frames HD por segundo; solapadas, ~22 [medido]. Ahora la GPU marca el ritmo fuera del público (95 % ocupada) y la CPU en el público.
- **CPU 444 y crossbar 166** (`clocks`): traductor 50-65 ms -> 32-45 ms, y el capturador alcanza en el público [medido]. Plan B, el objetivo es no necesitarlos.
- **El nitro lee la imagen del frame anterior** (04000000 o 04044000) como textura; el 92 % de los draws de Ridge Racer comparte el estado del anterior.
- **La imagen 195140 se marca de 69 páginas de alto** por estimación, sobre texturas reales: los pedidos se caían y, con la lista de imágenes llena, esas texturas pasaban por imágenes.
- **128 texturas por frame eran pocas:** Metal Slug XX llegaba al tope con 0,8 de 1,5 MB en uso y perdía frames. Ahora son
  256 [medido, 0.0.8.25–0.0.8.26].
- Los juegos de Neo Geo hacen transparencias dibujando algo un frame sí y otro no: si la Vita repite o salta frames,
  destella. Se mide con la línea `translator: pace` del log.
- **Grabar la pantalla** (Vitastream + OBS a 60 fps) encontró los cuadros rotos de un frame que ningún volcado capturaba.
  Se usa solo cuando los volcados y logs no alcanzan.

- **La GPU puede hacer el trabajo de vértices del GE:** en Ridge Racer `nd_v3` dibuja los mismos triángulos que el camino CPU (`tools/perf/tr_gv.sh`), sin errores de imagen en carrera. Límites por ahora: el retrovisor y otros tipos de vértice siguen en la CPU.
- **Los frames guardados no son la carrera:** dieron 6-12 % de vértices con scissor y 71-87 % elegibles; la carrera dio hasta ~50 % con scissor y 54 % en la GPU.
- **Público en Ridge Racer:** con el capturador atrasado, una lista leída tarde puede pasar como frame completo con estado basura (0x27=ade1f9). El traductor la rechaza y la imagen normal queda hasta el siguiente frame HD, 4-16 frames del juego ahí. 1 a 8 por corrida, sin relación demostrada con C.1.

- **Las pasadas coplanares necesitan el mismo camino:** Ridge Racer dibuja un auto dos veces con prueba de profundidad GEQUAL. CPU y GPU redondean distinto la profundidad, así que mezclarlas hace parpadear la segunda pasada en dientes de sierra; un solo programa para ambas lo resuelve.
- **Los vértices no se pueden copiar después de soltar la lista:** el juego los reescribe de inmediato. El 0,5 % medido un frame después no lo anticipó: copiar unos ms tarde deformó la geometría de forma masiva.
- **`cap_split` arruina una carrera:** ~0,7 us por lectura de reloj frenan tanto al capturador que toda la pista se comporta como el público, con errores gráficos e imagen normal. Avisar al usuario antes de usarlo.

- **Combate de God of War en la GPU:** con huesos y luces en `nd_v5` el traductor baja de ~230 a ~50 ms por frame HD y todo el combate queda en HD; el límite pasa al capturador (~125 ms por frame, de ellos 52-61 ms copiando ~1,9 MB de vértices desde memoria sin caché) y a la GPU de la Vita (~82 ms por frame HD, `nd_v5` siempre hace 8 huesos y 4 luces).
- **El brillo de God of War es un color fijo de mezcla** (`0xE0` = 3f5343, destino 0): sin él la imagen HD quedaba sin el tono verde y saltaba la revisión de "tapado", que mostraba la imagen normal.
- **Las cinemáticas de God of War son videos:** el GE solo dibuja los subtítulos encima, así que no se pueden dibujar en HD (el "NAMCO presents" de Ridge Racer tampoco).
- **Los vértices basura se dibujan en el camino GPU:** el camino CPU rechaza un frame con posiciones locas (`vertex_out`), `nd_v5` dibuja en cambio un triángulo enorme. Revisar las posiciones al entrar al registro (0.0.26.6) no quitó los frames corruptos.

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
| Una textura cambiada solo en bytes que la revisión no ve puede verse vieja unos frames | riesgo aceptado del caché de texturas (hasta 16 frames) | revisión completa por turnos |
| Mezclas sin equivalente directo en GXM (alfa del destino, factores al doble), test de color, operaciones lógicas | GXM fijo no las tiene (sí tiene mezcla programable, sin usar todavía) | imagen normal, motivo `blend` / `color_test_or_logic` |
| Máscara de color por bit (0xE8) y máscaras del test de alfa que no son bits altos | GXM enmascara canales enteros y los shaders comparan el alfa entero | imagen normal, motivo `mask` |
| Profundidad y stencil entre escenas | viven en una escena de GXM (sin force load/store), no sobreviven a un cambio de destino o de frame | lo que dependa de ellos se dibuja mal |
| Dependencia de Adrenaline | se enganchan importaciones de AdrenalineUser y ScePspemu | cada versión de Adrenaline puede romperlo; a futuro, proponer un punto de enganche oficial |

## Cosas que se midieron y no eran lo que parecían
- vpjuego: "GE en stall" siempre 0 %: para los tiempos del GE sirven las horas de encolado y FINISH (T6).
- El núcleo 0 de la línea `processor` del log sale casi vacío: el gancho de CreateThread no ve los hilos que Adrenaline
  creó antes de cargar NativeDraw.
- Leer la hora en la Vita cuesta ~390 ns: el registro extremo completo altera lo que mide.
- `sceCommonDialogIsRunning` siempre dice "no" aunque el diálogo del botón PS esté abierto: no sirve para detectarlo.
- Las funciones de DMA del PSP (`sceDmacMemcpy`) no se encontraron con ningún nombre de módulo en Adrenaline 8 (T4
  incompleto; no hizo falta).
