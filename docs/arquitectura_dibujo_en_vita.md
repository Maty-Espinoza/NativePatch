# Arquitectura propuesta: el PSP dibuja normal y la Vita dibuja en HD (análisis del 2026-10-28)

Conversación de análisis (sin cambios de código, sin tocar la Vita ni el laboratorio). Pregunta del usuario: revisar
toda la documentación y otros proyectos (Adrenaline 8, PPSSPP, GePatch original, NativePatch) y proponer **la mejor
forma de implementar el escalado** con dos criterios: **rendimiento igual o parecido al original** y **juegos que se
dibujen bien**. La complejidad no es un criterio: se puede avanzar de a poco.

Marcas: **[medido]** comprobado en la Vita; **[estimado]** calculado de mediciones; **[deducido]** conclusión sin prueba
directa; **[verificar]** hay que confirmarlo antes de construir encima (con la prueba que se indica); **[advertencia]**
extrapolación de una prueba sintética que no se toma como verdadera ni como error hasta probarla con el juego real.

Leer después de `contexto_ia.md`. Este documento **reabre la ruta G de `filosofia.md`** ("emular el GE sobre GXM",
descartada solo por complejidad) y la pone como arquitectura objetivo. GePatch actual queda como modo de respaldo.

**Estado:**
- Etapa 0 **hecha** el 2026-10-28 → **`etapa0_resultados.md`** (herramienta `tools/carga_vita.py`). Corrigió §3.5
  (espejo obligatorio para GT y RR), §5 (GT tiene ~9000 draws, no ~4000) y el criterio de la etapa 1 (§6).
- Etapa 1 **hecha** el 2026-10-28 → **`etapa1_banco_gxm.md`** (banco de pruebas en gepresent v11.3). Midió el costo de
  un draw en GXM dentro de Adrenaline (~1 µs sin cambios, ~2 µs con textura o programa nuevo, ~3 µs textura + uniformes),
  la memoria disponible (~33 MB LPDDR + 4 MB CDRAM), el contexto diferido (funciona; el hilo de Adrenaline solo ejecuta,
  ~30 µs), la GPU leyendo la RAM del PSP (funciona, misma velocidad) y el presentador (funciona). Actualizó §5, §6 y §8.
- Etapa 2 **en curso** (2026-10-28) → **`etapa2_avance.md`**: modo sombra (GePatch dev61, por defecto) + capturador en la
  Vita (gepresent v12.9). SH a velocidad original; GT a 55–60 fps salvo el puente (~30 ms de captura por frame).

---

## 0. En simple (para alguien sin conocimientos técnicos)

- Hoy GePatch le pide al "pintor" del PSP (el GE) que pinte la imagen cuatro veces más grande. El pintor es el mismo
  de siempre y no da abasto: por eso los juegos pesados van lentos. Además, para que quepa la imagen grande hay que
  engañar al juego con una memoria falsa, y ese engaño es el origen de casi todos los errores de imagen.
- La propuesta es **no tocar al PSP**: el juego corre exactamente como sin el plugin, a su velocidad original, y su
  pintor dibuja la imagen chica de siempre (que nadie ve).
- Mientras tanto, **la Vita lee las mismas instrucciones de dibujo** que el juego le manda a su pintor y las vuelve a
  dibujar con su propia tarjeta gráfica, que es mucho más potente, en alta resolución. Esa es la imagen que se muestra.
- Ventajas: el juego no se vuelve lento (el PSP hace lo mismo que sin plugin), la Vita tiene memoria de sobra (adiós
  al límite de 2 MB), y como el juego no se engaña, desaparecen los errores que venían del engaño.
- Costo: es como escribir la parte gráfica de un emulador (lo que hace PPSSPP) dentro de la Vita. Es mucho trabajo,
  pero se puede hacer por etapas y cada etapa deja algo útil (la primera ya da una "caja negra" de diagnóstico).
- Si la Vita no sabe dibujar algo, se muestra la imagen normal del juego: **nunca peor que sin el plugin**.

---

## 1. Conclusión principal: la arquitectura actual no puede cumplir la meta de rendimiento

Con GePatch actual el GE dibuja `s²` veces los píxeles. Lo medido con vpjuego:

| Juego | GE sin GePatch | GE con GePatch 2x | Cuánto cabe en 16,7 ms (60 fps) |
|---|---|---|---|
| Ridge Racer | 4–5,7 ms | 22–27 ms (1,5x: 13–15) | 2x no; 1,5x apenas |
| Gran Turismo | 8–13 ms | 22–38 ms | ni a 1,5x (estimado 16–22 ms) |
| Silent Hill SM | 18–37 ms por flip | 40–60 ms | ni a 1x llega a 60; 2x lo baja a 15–21 fps |
| LocoRoco MC | 1–3 ms | 3–9 ms | sí |

- Es un **límite físico del GE** (relleno de píxeles), no un error de lógica: la relación con/sin es 3–4, igual a
  la cantidad de píxeles. [medido, B1]
- Por eso ninguna optimización de CPU (camino B, plan A del recorrido en el ARM) sube los fps a 2x en GT, RR o SH. [medido]
- La única palanca del GE es bajar la escala (1,25–1,5x), que es justo lo contrario de lo que se busca.
- Además la arquitectura actual arrastra por diseño: VRAM de 2 MB llena (rt a escala 1, Z_SAVE, intercambio), búfer
  565 sin stencil (linterna de SH, tablero de GT), guard band reducido (huecos de RR), VRAM falsa en la RAM (GE +19–75 %
  con texturas por el bus), parches dentro de la memoria del juego (las caídas de dev58).

**→ Para tener el rendimiento original y buena imagen hay que sacar el dibujo en alta resolución del GE.** El único
otro chip gráfico disponible es la GPU de la Vita (SGX543MP4+), y el único procesador libre es el ARM de la Vita.

---

## 2. Las familias de arquitectura

| | A. Actual (GE a escala) | A'. Actual mejorada | **B. PSP intacto + Vita dibuja en HD** | C. Mixta (GE sin draws + Vita) |
|---|---|---|---|---|
| Qué dibuja el GE | todo, a `s²` píxeles | todo, a ≤1,5x | **todo, a 1x, sin cambios** | solo lo que necesita la CPU del juego |
| Qué dibuja la Vita | nada (solo amplía) | nada | **todo, a 2x o más** | todo |
| Rendimiento del juego (PSP) | GE ×3–4, CPU +5–10 ms | GE ×2, CPU +5–10 ms | **= original** (+ganchos, µs) | mejor que original en GE |
| Imagen | errores por diseño | menos errores | errores solo del traductor; **respaldo = imagen nativa** | igual que B, menos datos para la CPU |
| VRAM | 2 MB (límite duro) | 2 MB, ~0,9 MB libres a 1,5x | **la de la Vita** (~33 MB libres en Adrenaline, etapa 1) | la de la Vita |
| Guard band | se achica al escalar | se achica menos | **mismo que el original** | igual que B |
| Complejidad | ya existe | media | **muy alta** (un renderizador tipo PPSSPP) | muy alta + riesgo |
| ¿Cumple la meta? | no | solo juegos livianos | **sí, en principio** (depende del ARM, ver §5) | no hace falta |

**Recomendación: B**, con A como respaldo por juego mientras B madura. C solo si algún juego necesitara bajar el GE
nativo (no es la meta).

---

## 3. Diseño B en detalle

### 3.1 Principio
**El PSP no se toca.** El juego corre su lista tal cual en el GE real, a 480x272. GePatch (lado PSP) pasa a ser un
plugin delgado que solo **avisa** (encolados, stall, sync, flip) y nunca reescribe la lista. La Vita lee la misma
lista desde la RAM del PSP, la traduce a GXM y la dibuja en su GPU a la escala que se quiera.

Consecuencias directas:
- Todo lo que el juego observa es **exacto**: VRAM, z, stencil, interrupciones del GE (FINISH/SIGNAL), lecturas de la
  CPU sobre lo dibujado, `sceGeEdramGetSize` (2 MB reales). No hay VRAM falsa, ni memoria de parches, ni `BASE_PLUG`,
  ni vértices escalados.
- El costo del GE es el original. El costo de CPU del PSP es el de los ganchos (escribir unas palabras en un buzón).
- Desaparece la copia por flip a 0x0A400000 (2,3 ms de GE a 2x) y la zona 0x0A000000 deja de ser necesaria (P1–P3).

### 3.2 Las preguntas del usuario

**¿Cómo redirigimos?** No se redirige nada. La Vita **lee** lo mismo que el GE: la lista (y sus sublistas, vértices,
índices, texturas, CLUT) está en la RAM del PSP, que el ARM ve en 0x70000000 (sin caché) y 0x61000000 (con caché) y que
la GPU de la Vita puede leer si se registra con `sceGxmMapMemory` (gepresent ya lo hace; **etapa 1: la GPU leyó vértices
desde 0x08800000 a la misma velocidad que desde su propia memoria** [medido]).
Los ganchos de GePatch (`sceGeListEnQueue/Head`, `sceGeListUpdateStallAddr`, flip) ya existen: solo cambian de "recorrer
y parchar" a "anotar en el buzón". **Excepción (etapa 0):** lo que el juego guarda en la VRAM (GT: sublistas, vértices,
texturas; RR: texturas) no está en la RAM: ver §3.5.

**¿Cuál es el costo de latencia?**
- Canal PSP→ARM por memoria compartida: p50 11 µs en ráfaga, 200–500 µs si el hilo del ARM duerme. [medido, KBENCH]
  El hilo del ARM tiene que sondear sin dormir mientras haya juego (dormir 100 µs son ~400 µs reales).
- Imagen: la Vita termina de dibujar el frame N después de que el juego lo envió; se muestra **un frame más tarde**
  que hoy (~16,7 ms a 60 fps, 33 ms a 30 fps). Es la latencia típica de un emulador con triple buffer. [estimado]
- El juego no espera esa latencia (ver "sincronizar", abajo): solo la ve el ojo.

**¿Cómo sincronizamos los dos CPUs?** Dos etapas con reglas distintas:
1. **Captura** (ARM): recorre la lista siguiendo el stall que publica el PSP y copia a la RAM de la Vita lo que va a
   necesitar después (comandos ya interpretados, rangos de vértices/índices, CLUT cargadas, huella de cada textura).
   Tiene que ir **al ritmo del GE**, porque el juego puede reusar sus búferes cuando el GE terminó.
2. **Dibujo** (ARM + GPU): traduce lo capturado a GXM y dibuja. Va **desacoplado**, con 1 frame de atraso. Si se
   atrasa más, la Vita repite el frame anterior en pantalla, pero el juego sigue a su velocidad.
   **Etapa 1:** el dibujo se graba en un contexto diferido de GXM en el hilo del traductor, y el hilo de Adrenaline
   solo ejecuta la lista (~30 µs por 2000 draws) [medido].

Dónde espera el PSP (y solo si la captura va atrasada):
- **Modo normal (por defecto): compuerta en el sync.** Los ganchos de `sceGeDrawSync`, `sceGeListSync` y el flip
  esperan a que la captura haya llegado al final de la lista. Es el punto donde casi todos los juegos reusan sus
  búferes. Sin hilos extra en el PSP.
- **Modo estricto (por juego): compuerta en el stall.** El gancho del stall le da al GE como máximo lo que la Vita ya
  capturó. Garantiza que la Vita lee todo antes que el GE. Necesita que alguien del PSP mueva el stall cuando la Vita
  avanza (un hilo que sondea): cuesta CPU del PSP. Solo para juegos que reusen búferes en medio del frame.

**¿Cómo le indicamos al GE que los comandos ya se aplicaron y deje de esperar?** No hace falta: en B **el GE real
ejecuta la lista él mismo** (a 1x) y genera sus propias señales; el juego ve exactamente el comportamiento original.
La Vita nunca está en el camino del GE (salvo el modo estricto, que solo atrasa el stall). Si algún día se quisiera la
variante C (GE sin dibujar), se cambian los PRIM por NOP (mecanismo reversible que ya existe) y se dejan SIGNAL/FINISH/
END/JUMP/CALL: el GE recorre la lista casi gratis y sigue dando las señales a tiempo (idea ya anotada en
`plan_a_recorrido_vita.md`).

**¿Cómo sincronizamos los frames con GePatch?** El gancho del flip (`sceDisplaySetFrameBuf`) anota en el buzón
"frame N: se muestra el fb X". La Vita presenta su versión HD de ese fb cuando la termina. Para presentar se usa lo que
ya funciona en gepresent/adrfsr: enganchar las importaciones GXM de `AdrenalineUser` y cambiar la textura que Adrenaline
dibuja por la superficie HD. [medido: adrfsr cambia el programa de fragmentos en el dibujo de Adrenaline sin GePatch;
**etapa 1: el banco cambió la textura de 512x272 por su superficie propia en 599 de 600 frames, enganchando
`sceGxmSetFragmentTexture` y dibujando su escena después de `sceGxmEndScene` de Adrenaline**]
Ojo: **SH dibuja casi todo su frame después del flip, en la misma lista** [medido, etapa 0]: el frame N de SH es lo
que se dibuja entre un flip y el siguiente, no "la lista encolada antes del flip".

### 3.3 Componentes

| Componente | Dónde | Qué hace | Base existente |
|---|---|---|---|
| **GePatch delgado** | PSP (kernel) | Ganchos de encolado, stall, sync, flip, `sceGuCopyImage`/TRANSFER no hace falta (se ve en la lista). Buzón. Menú. Pedidos de "espejo de VRAM" (§3.5). Modo A como respaldo por juego | GePatch actual (ganchos, buzones de dev56.5) |
| **Capturador** | Vita, hilo ARM 1 | Recorrido sombra de la lista; copia por DMA lo referenciado; caché de sublistas por huella | Plan A paso 2 (`plan_a_recorrido_vita.md`), KBENCH |
| **Traductor** | Vita, hilo ARM 2 (contexto diferido de GXM) | Estado del GE → estado GXM; caché de texturas; programas; administrador de framebuffers; **junta draws consecutivos con el mismo estado** | Partes de PPSSPP (§3.10); banco de la etapa 1 (`tools/gepresent/banco.c`) |
| **Renderizador** | GPU de la Vita | Escenas GXM en superficies HD propias (8888 + z/stencil) | adrfsr (patcher de sombreadores, compilación en el PC), banco de la etapa 1 |
| **Presentador** | Vita, hilo de dibujo de Adrenaline | Ejecuta las listas grabadas y muestra la superficie HD del fb que el juego mostró; si no hay, la imagen nativa | gepresent / adrfsr / banco de la etapa 1 |
| **Caja negra + telemetría** | Vita | Últimos frames capturados siempre en RAM; estadísticas; volcados `.ppdmp` | gepresent v9 (anillo y escritor en segundo plano; **el anillo de 24 MB tiene que achicarse**: §5) |

### 3.4 Un frame, paso a paso
1. El juego encola su lista → gancho: anota (dirección, stall) en el buzón. El GE real empieza al mismo tiempo.
2. El juego mueve el stall → gancho: actualiza la palabra del stall (sin cola: la Vita lee el último valor).
3. Capturador (ARM): avanza hasta el stall; por cada draw anota estado + rango de vértices/índices + huella de la
   textura y su CLUT. Sublistas iguales a las del frame anterior (misma huella) se reutilizan ya traducidas.
4. El juego llama a `sceGeDrawSync` → gancho: espera solo si la captura no llegó al final (normalmente no espera).
5. Flip → gancho: "frame N muestra fb X".
6. Traductor (frame N, mientras el juego ya arma el N+1): graba escenas GXM por cada render target, en el orden del juego,
   en un contexto diferido.
7. Presentador: en el hilo de Adrenaline ejecuta lo grabado y en el siguiente vblank muestra la superficie HD de X; si el
   frame N tuvo algo no soportado (o no hubo draws del GE en X: video, dibujo de la CPU), muestra la imagen nativa.

### 3.5 Lo que la Vita no puede ver: la VRAM real
La VRAM del PSP no está mapeada en el proceso de Adrenaline [medido, B4]. En B la Vita necesita su contenido cuando
una textura, una CLUT, **una sublista o un búfer de vértices** vive en la VRAM. De dónde puede venir ese contenido:

| Origen | Cómo lo obtiene la Vita | Costo |
|---|---|---|
| Un render target que dibujó el GE | **Su propia versión HD** (administrador de framebuffers, como PPSSPP) | ninguno extra |
| Copia de bloque (TRANSFER) desde la RAM | La Vita la ve en la lista y la repite desde la RAM | ninguno extra |
| CLUT cargada desde la VRAM | Igual que una textura | — |
| **Datos fijos que el juego subió a la VRAM antes del frame** (etapa 0: GT 370–700 KB por frame entre texturas, sublistas y vértices; RR 70–460 KB de texturas; SH nada) | **Espejo obligatorio**: copia con el GE a la RAM al cargar y cuando cambie; entre volcados del mismo juego el 77–100 % es igual | GE ~1,6 ms por copia de 700 KB; ver estrategias en `etapa0_resultados.md` |
| La CPU del juego escribió la VRAM (texturas copiadas a mano, dibujo por software, spinner de SH) | **Espejo pedido**: la Vita detecta una textura en la VRAM sin origen conocido y pide al PSP copiarla con el GE a la RAM al final del frame (se usa desde el frame siguiente) | GE: ~2,3 ms por MB; normalmente KB [estimado]. Etapa 0: ninguna textura cambió dentro de un frame |
| Pantalla dibujada por la CPU o un video (sin draws del GE en ese fb) | El presentador muestra la imagen nativa de ese frame | ninguno |
| Dibujo de la CPU **encima** de una escena del GE | Por juego: comparar la imagen nativa con la HD reducida y pegar la diferencia | ARM, por juego |

- **GT recorre sublistas que están en la VRAM** (~20 % de sus comandos) [medido en volcados, etapa 0]: sin espejo el
  capturador no puede seguir sus listas. Es el caso más exigente; SH es el más simple (solo lee sus propios rt).
- **La imagen nativa ya llega gratis a la Vita**: en el modo normal de Adrenaline (sin `DRAW_NATIVE`), el hardware de
  compatibilidad deja cada frame en el búfer 0x74000000 (512x272, 8888), Adrenaline lo copia a su textura de 512x272 y
  la dibuja [código]. **Etapa 1:** el banco la dibujó dentro de su superficie propia mientras reemplazaba la textura
  [medido]. Las cinemáticas de GT se ven por ese camino (usuario, gepresent v9). [verificar] por qué durante la
  presentación del banco la textura llegó negra (píxeles `ff020202`; el usuario vio negro + el cuadro del banco):
  pendiente, postergado por el usuario (`etapa1_banco_gxm.md` §3.3).

### 3.6 Coherencia de datos (lo que más puede fallar)
- Texturas: la Vita no puede copiar todas las texturas en cada frame. Caché por (dirección, tamaño, formato, CLUT) +
  huella. Para no recalcular huellas de todo cada frame: huella rápida (muestras) cada frame + completa cada N frames
  o al ver una escritura conocida (TRANSFER, `sceKernelDcache*` sobre ese rango). Es la misma estrategia de PPSSPP y su
  riesgo conocido son las texturas que cambian sin aviso (texto que se escribe letra a letra, paletas animadas): opción
  por juego "huella completa siempre". Etapa 0: GT usa 400–700 texturas distintas por frame (casi todas clut4 con
  swizzle), RR 20–124, SH 17–109.
- Vértices: GT 0,5–1 MB y SH hasta 3 MB por frame [medido, etapa 0]: copiarlos por DMA (7,2 ms/MB) no entra. **Leerlos
  directo de la RAM del PSP con la GPU** (ya registrada con `sceGxmMapMemory`; **etapa 1: misma velocidad que la memoria
  propia, 17 M vértices/s** [medido]) y copiar solo lo que el juego reescriba antes de que la GPU lo lea o lo que no tenga
  formato GXM. Los vértices que están en la VRAM (GT, ~35 % de los draws) salen del espejo.
- Listas que el juego reusa entre frames cambiando palabras (GT): la captura las relee cada frame; la caché de
  sublistas por huella del contenido (lo que ya se aprendió con `WalkCache`).
- `sceKernelDcacheWritebackInvalidate*`: el ARM lee sin caché o tras DMA, y las listas escritas con caché por el
  juego ya están en la RAM cuando el juego las encola (lo exige el GE real). [deducido]
- **Memoria de los contextos diferidos** (etapa 1): GXM pide memoria por callbacks mientras se graba y la sigue usando
  entre listas. Reciclarla antes de tiempo hizo caer la GPU (banco v11.2). Reciclar por bloques completos y solo cuando
  la GPU terminó con las listas que la usan. ~160–640 KB por 2000 draws [medido].

### 3.7 Cómo queda cada error conocido (`errores_conocidos.md`)

| # | Error | En B |
|---|---|---|
| V1 | Linterna de SH (8888 + stencil) | **Desaparece**: superficies 8888 con stencil (D24S8) |
| V2 | Tablero de GT (5551 + stencil) | **Desaparece** (mismo motivo) |
| V3 | Zonas negras en GT (draws omitidos) | **Desaparece**: nada se omite |
| V7 | Textura que lee una parte de un fb | Se resuelve en el administrador de framebuffers (desplazamiento dentro del fb) |
| V8 | Retrovisor RR, reflejos GT, pases de SH | **Desaparece**: todos los rt en HD (memoria de sobra) |
| V9 | Huecos en el camino de RR (guard band) | **Desaparece**: el descarte se decide con las coordenadas a 1x, igual que el GE real (§3.8) |
| V10 | Pases de 1024 de ancho | Sin límite de ancho del GE: se dibujan en HD |
| V11 | Bordes de 1 px en 2D | Riesgo nuevo-viejo de los emuladores: costuras entre sprites a escala alta (ver §3.9) |
| V12 | Texturas con basura (z como textura) | Si el juego lee su z como textura: la Vita lo tiene en HD (z de la superficie) o pide espejo |
| V13 | Dibujo de la CPU | Espejo / imagen nativa (§3.5) |
| V14 | Menú cortado a \< 2x | El menú lo dibuja la Vita en su superficie |
| R1 | GE como límite | **Desaparece**: el GE trabaja lo original |
| R2 | CPU del recorrido en el PSP | **Desaparece**: el recorrido está en el ARM |
| P1–P3 | Zona 0x0A000000, memoria alta | **Desaparecen**: no se usa esa zona (salvo un buzón chico) |

### 3.8 Restricciones del GE: cuáles se emulan y cuáles se superan
- **Guard band:** el GE solo recorta contra el plano cercano y descarta el triángulo entero si se sale de un área
  virtual de 4096x4096. PPSSPP implementa la regla ("vertex range culling": descarta si algún vértice queda fuera, salvo
  cuando actuaría el clamp de profundidad) y ya tuvo casos donde su aproximación falla (TDU). En B la regla se aplica
  **con las coordenadas a 1x**, así que lo descartado es lo mismo que en el original: no aparecen huecos nuevos.
  Opcional por juego: no descartar (puede mostrar geometría que el original ocultaba; probar).
  Etapa 0, triángulos descartados por frame a 1x / 2x: GT 140–800 / 300–2100, RR 20–80 / 100–270, SH 0–490 / 26–1770.
- **Formatos 565/5551/4444 con dither:** la Vita dibuja en 8888 (sin bandas). Cuando un juego lee un fb como textura
  esperando el formato chico (efectos "depal" de PPSSPP), se convierte en el sombreador. SH lee un rt como clut32
  desde la VRAM [medido, etapa 0].
- **Z de 16 bits, rango y clamp:** los problemas de profundidad son los más frecuentes en PPSSPP; usar sus reglas.
- **Límite de ancho 1024/2048, VRAM 2 MB, relleno:** no existen en la Vita.

### 3.9 GXM frente al GE: qué mapea directo y qué no

| Función del GE | En GXM | Nota |
|---|---|---|
| Texturas CLUT4/CLUT8 | **Directo**: formatos P4/P8 con paleta (alineación de paleta 64 bytes en `gxm.h`) | La CLUT de 16 bits del PSP se pasa a 8888 (≤ 256 entradas, barato). Shift/mask de CLUT: en el sombreador si no es el caso simple. **Etapa 1: un draw que cambia textura P4 + paleta cuesta lo mismo que uno que cambia una 8888 (~2 µs)** [medido] |
| Swizzle del PSP (bloques de 16 bytes × 8 filas) | No coincide con el swizzle de GXM | Des-swizzle en el ARM con NEON, solo al entrar a la caché |
| DXT1/3/5 | GXM tiene UBC1/2/3 | El PSP guarda los bloques DXT en otro orden: reordenar al cargar [verificar con PPSSPP `TextureDecoder`]. GT usa dxt1 y dxt5 |
| Mezcla con constantes (FIXA/FIXB), operaciones lógicas, máscara de color parcial | No en la mezcla fija | **GXM tiene mezcla programable** (el sombreador lee el color del fb): permite copiar el GE exacto. Vita3K documenta que pocos juegos la usan y que es un desafío para emuladores en PC, no en la Vita |
| Alpha test, color test, fog, funciones de textura, doble de color | Sombreador | |
| Stencil | D24S8 | |
| Luces (4), materiales, huesos (8), morph | Sombreador de vértices (o el ARM para morph) | |
| RECTANGLES (sprites) | No existe | Expandir a 2 triángulos en el ARM (pocos por frame: RR ~20, SH ~21, GT ~60) |
| Índices de 8 bits | GXM usa 16/32 | Convertir en la captura (solo SH usa índices: 5–21 KB) |
| Bézier/spline | No | Teselar en el ARM (PPSSPP ya lo hace). Ninguno de los 3 juegos los usa |
| Atributos de vértice del PSP | La mayoría se describen tal cual (U8/S8/S16/F32 con stride) | Colores 565/5551/4444 empaquetados: desempaquetar en el sombreador |

**Sombreadores:** dos caminos, de menos a más:
1. **Ubersombreadores precompilados** en el PC con el compilador de Sony bajo qemu (cadena que ya funciona para
   adrfsr y el banco de la etapa 1). Sin requisitos para el usuario final. Más costo de GPU por ramas.
2. **Compilación en la consola** con `libshacccg.suprx` (se instala con ShaRKBR33D, como para vitaGL/flycast) y caché en
   la tarjeta. Más rápido en GPU, pero cada sombreador nuevo tarda ~0,5 s en compilar (tirones la primera vez: lo
   reportan los ports grandes) → caché persistente por juego.
Empezar por 1; pasar a 1+2 si la GPU no alcanza. Etapa 1: cambiar de programa de fragmentos entre draws cuesta
~2,1–2,4 µs (contra ~1 µs sin cambios) [medido]: conviene pocos programas y ordenar/juntar por programa.

**Problemas conocidos de los emuladores al subir la resolución** (B los hereda): costuras entre sprites 2D, texturas
que "sangran" en los bordes, líneas de 1 px. PPSSPP los mitiga con opciones por juego (redondeo de UV, dibujar 2D a
escala entera). Ver la colección de capturas.

### 3.10 Reusar PPSSPP
- PPSSPP es GPL 2.0 o posterior; Adrenaline es GPLv3. **[verificar]** la licencia de GePatch/NativePatch antes de copiar
  código.
- Partes útiles (independientes del backend): decodificador de vértices, decodificador de texturas (swizzle, DXT,
  CLUT), caché de texturas, administrador de framebuffers (rt, copias, depal), reglas de profundidad y de descarte,
  teselado. Lo que no sirve: su CPU/HLE y su backend (Vulkan/GL/D3D).
- Trabajo grande: un backend GXM (escenas, superficies, programas) y adaptar su lectura de memoria a 0x70000000.
- Alternativa más simple para empezar: escribir el traductor propio mirando PPSSPP como referencia, con los
  sombreadores fijos de §3.9. Decidir después de la etapa 3 (§6).

### 3.11 Degradación elegante (compatibilidad garantizada)
- **Por frame:** si el traductor encuentra algo que no sabe dibujar, marca el frame y el presentador muestra la imagen
  nativa. Peor caso = el juego sin plugin.
- **Por juego (perfil):** `modo = nativo | GE (GePatch actual) | Vita HD`, más opciones de B (compuerta normal/estricta,
  huella completa, escala 2x/3x con reducción = antialiasing, MSAA 4x de GXM).
- **Detección automática:** comparar cada N frames la imagen HD reducida contra la nativa (§4.5); si la diferencia
  pasa un umbral, volcado automático de la caja negra y aviso en el log.

---

## 4. Telemetría: qué falta saber y cómo obtenerlo

Hoy hay log + volcado por combinación de teclas (3 frames hacia adelante), vpjuego (GE ocupado, CPU libre, fps), perfil
del recorrido y contadores del Allegrex. Faltan: ver qué hizo la CPU del juego, ver frames **anteriores** al problema,
reproducir un frame paso a paso y conocer las reglas exactas del GE.

| # | Herramienta | Qué responde | Costo para el PSP | Base |
|---|---|---|---|---|
| T1 | **Estadísticas de la captura** (cada segundo en el log): draws, prims por tipo, through/3D, cambios de estado reales, cambios de rt, texturas usadas (cantidad, KB, formato, swizzle, CLUT, en RAM/VRAM), KB de vértices, TRANSFER, cargas de CLUT, descartados por guard band, **lotes que se podrían juntar** | El presupuesto real del ARM y de la GPU por juego; qué funciones del GE usa cada juego | ninguno (lo hace el ARM) | capturador; **las mismas cifras ya salen de los volcados con `tools/carga_vita.py`** |
| T2 | **Caja negra**: los últimos 3–5 frames capturados siempre en la RAM de la Vita; L+R+SELECT guarda los **anteriores** al problema | Errores que se ven antes de reaccionar; lo que la ráfaga de 3 frames hacia adelante no ve | ninguno | anillo de gepresent v9 (achicar: §5) |
| T3 | **Volcado en formato `.ppdmp` de PPSSPP** | Abrir el frame en el depurador del GE de PPSSPP: avanzar draw por draw, ver estado, texturas y fb; reproducirlo en PPSSPPHeadless (con el renderizador por software, que modela el GE) y comparar PNG | igual que hoy | `gpd.py ppsspp`: **genera `.ppdmp` válidos (v6)**; headless dibujó uno de SH igual que la Vita y el usuario abrió uno con PPSSPPSDL en WSL (etapa 0) |
| T4 | **Traza de llamadas del juego** (anillo de eventos con hora): `sceGe*` (encolar, stall, sync, callbacks), `sceDisplay*`, `sceKernelDcache*`, `sceDmac*` hacia la VRAM, `sceGeEdram*`, `sceMpeg*` | "Qué hizo la CPU y cuándo": dónde espera el juego, si usa doble búfer, si escribe la VRAM con la CPU o con DMA, cuántos flips hay por frame (pregunta abierta de SH) | µs por llamada | ganchos de GePatch |
| T5 | **Comparación nativo vs HD** (imagen nativa de 0x74000000 contra la HD reducida) | Detección automática de draws que faltan o sobran; % de diferencia por frame; volcado automático | ninguno | presentador |
| T6 | **Tiempos del GE por lista** (hora del encolado y del FINISH) + muestreo de vpjuego integrado | Margen real del GE nativo; si la compuerta del sync hace esperar | µs | vpjuego |
| T7 | **Pruebas del GE en la Vita** (geptest ampliado): borde exacto del guard band (vértices en 4095/4096/4097, con y sin clamp de z), recorte del plano cercano, dither, LOD de texturas | Las reglas exactas que el traductor tiene que imitar; contrastadas con el renderizador por software de PPSSPP | homebrew aparte | `tools/geptest` |
| T8 | Contadores del Allegrex (`perfil=2`) en el modo B | Confirmar que el PSP gasta lo mismo que sin plugin | ya existe | |

---

## 5. Presupuesto de rendimiento del lado Vita (actualizado con la etapa 1)

| Trabajo | Dato conocido | Estimación |
|---|---|---|
| Recorrer la lista en el ARM | 53 000 comandos en 10,8 ms con DMA previo (203 ns/cmd) [medido, KBENCH, sintético] | Etapa 0: GT 44 000 comandos por frame (hasta 76 000) → 9–15 ms sin caché de sublistas; RR y SH ~9000 → ~2 ms |
| Vértices | 7,2 ms por MB por DMA [medido]; **la GPU lee la RAM del PSP a 17 M vértices/s, igual que su memoria** [medido, etapa 1] | GT 0,5–1 MB, SH hasta 3 MB por frame → no copiar: la GPU los lee de la RAM del PSP |
| Draws en GXM (CPU) | **Etapa 1 [medido]**: ~1,0 µs sin cambios; ~1,9–2,2 con textura nueva (8888 o P4 + paleta); ~2,1–2,4 con programa nuevo; ~1,9–2,2 con uniformes nuevos; ~3,0–3,5 con textura + uniformes; 1 draw de 2000 cuadros = 40 µs. Igual en contexto diferido (otro hilo); ejecutar lo grabado ~30 µs | RR (~1100 draws) ~2–3 ms; SH (~320, hasta 1400) < 1–3 ms. **[advertencia] GT** (~9000 draws, ~5300 con textura nueva): por la cuenta sintética 17–30 ms → podría no entrar en un hilo a 60 fps; **sin confirmar con listas reales** |
| Draws en GXM (GPU) | **Etapa 1 [medido]**: ~1,2–1,5 µs por draw aunque no pinte (2000 draws fuera de pantalla = 2,5–3,1 ms); +0,4 ms por escena extra; 1 draw de 2000 cuadros ≈ 2 ms en total | RR, SH holgados. **[advertencia] GT** ~11–14 ms solo en draws (misma salvedad) |
| Texturas | des-swizzle y huellas con NEON, sin medir | Etapa 0: GT 400–700 texturas distintas (0,5–1,1 MB), RR 20–124, SH 17–109 (lo grande de SH son sus rt, que ya están en la Vita) |
| Espejo de la VRAM | copia del GE ~2,3 ms por MB [medido] | GT 370–700 KB, RR 70–460 KB, casi todo fijo; SH 0 (§3.5) |
| Memoria de la Vita | **Etapa 1 [medido]**: ~33 MB de LPDDR + 4 MB de CDRAM en el proceso de Adrenaline, sin el anillo de volcados de 24 MB de gepresent (con él: 6 + 7 MB) | Superficie HD 960x544 8888 + z ≈ 4 MB; caché de texturas y contextos diferidos con el resto. **El anillo de volcados tiene que achicarse o pedirse solo al volcar** |
| GPU | SGX543MP4+ a **222 MHz** (ARM 444, bus 222) [medido, etapa 1]; renderizado por tiles con eliminación de superficies ocultas | SH tiene 22–24 pantallas de relleno con z por frame: la eliminación por tiles debería absorber la mayoría (opaco) [deducido]; la mezcla no. Triángulos por frame: GT 30 000–60 000, SH hasta 79 000 |

- El trabajo se reparte en 2 hilos del ARM (captura y traducción) + la GPU, en tubería con 1 frame de atraso.
- **Juntar draws** (draws consecutivos con el mismo estado/textura en un solo draw; reordenar opacos con z por textura)
  es la palanca más grande que midió la etapa 1: baja la CPU y la GPU a la vez. **Grabar en varios hilos** no escaló en
  el banco (hilos sin afinidad fija) [verificar en la etapa 2 con núcleos fijos].
- Juegos livianos (RR, SH, la mayoría del catálogo) deberían ir sobrados. **GT es el peor caso** por la cantidad de
  draws y por depender del espejo de VRAM; la advertencia de arriba se confirma o se descarta con sus listas reales.
- Batería y temperatura: el ARM sondeando y la GPU trabajando gastan más que hoy.
- Si el dibujo se atrasa: la pantalla repite frames pero **el juego no se frena**. Si la captura se atrasa: el juego
  espera en el sync (modo normal) → esto sí bajaría los fps del juego. Por eso la captura tiene que ser lo más liviana
  posible (copiar y anotar; traducir después).

---

## 6. Hoja de ruta por etapas (cada una con su pregunta y su criterio)

| Etapa | Qué | Pregunta / criterio para seguir | Vita / autorización |
|---|---|---|---|
| **0 (hecha)** | `tools/carga_vita.py` sobre los 90 `.gpd` (GT, SH, RR; no hay de LocoRoco) + `.ppdmp` generados. **Resultados en `etapa0_resultados.md`** | GT ~9000 draws (5300 con textura nueva), RR ~1100, SH ~320; ninguna textura de la VRAM con origen desconocido, pero GT y RR tienen datos fijos en la VRAM (espejo obligatorio; GT incluye sublistas y vértices); SH no necesita espejo | No (PC) |
| **1 (hecha)** | Banco de pruebas GXM dentro de Adrenaline (gepresent v11.3, apagado por defecto). **Resultados en `etapa1_banco_gxm.md`** | Draw sin cambios ~1 µs, (b) textura ~2 µs, (c) programa ~2,2 µs, textura + uniformes ~3 µs; contexto diferido funciona (el hilo de Adrenaline solo ejecuta); GPU ~1,3 µs por draw; GPU lee la RAM del PSP; presentador funciona; ~33 MB + 4 MB de memoria. → **Seguir con B**: RR/SH/catálogo liviano con margen [estimado]; **GT con advertencia** (podría no entrar sin juntar draws; confirmar en la etapa 2) | Sí (hecho) |
| **2** | GePatch "modo sombra" (pasivo + ganchos al buzón) + capturador en el ARM + T1 (incluidos **lotes que se podrían juntar**), T2, T4, T6 + **espejo de VRAM** (al cargar + verificación; sublistas de GT por frame). Repetir la medición de grabación en 2–3 hilos con afinidad fija | ¿La captura llega al final antes del sync en GT y SH? ¿El juego va a sus fps originales con el modo sombra? ¿El espejo de GT se mantiene al día? ¿Cuántos draws reales de GT quedan después de juntar? Ya deja la caja negra útil aunque B no siguiera | Sí |
| **3** | Renderizador mínimo: clears, TRANSFER, draws 2D (through) con texturas 565/5551/4444/8888 y CLUT, mezcla básica; presentación HD; respaldo a nativa por frame; T5 | ¿Funciona la cadena completa (captura → GXM → pantalla) sin frenar al juego? Probar en un juego 2D (Retro City Rampage DX) y en los menús | Sí |
| **4** | 3D: matrices, viewport, z, descarte del guard band a 1x, plano cercano, texturas swizzle/DXT, alpha/stencil, fog, luces, huesos | LocoRoco → **SH (no necesita espejo)** → RR → GT, contra la imagen nativa (T5) y la colección | Sí |
| **5** | Administrador de framebuffers: rt en HD, textura desde un fb (con desplazamiento), depal, espejos de VRAM pedidos | Reflejos de GT, retrovisor de RR (**solo cuando el usuario lo pida**), linterna de SH | Sí |
| **6** | Rendimiento: caché de sublistas traducidas, juntar draws, huellas por calendario, ubersombreadores → compilación en la consola si hace falta; opciones de calidad (3x reducido, MSAA) | fps de pantalla = fps del juego en los 4 juegos | Sí |

Mientras tanto, **GePatch actual (A)**: cerrar dev58/dev59 con commit y usarlo como modo "GE" por juego. No invertir en
los arreglos grandes de A que B resuelve por diseño (formato del juego en el búfer grande, rt en la VRAM libre, texturas
calientes): solo arreglos baratos y lo que se reaproveche en B (por ejemplo la tabla `struct Fb`, que es la semilla del
administrador de framebuffers).

---

## 7. Conclusiones previas que tocan este plan (y cómo comprobarlas)

| Conclusión anterior | ¿Bloquea B? | Qué revisar |
|---|---|---|
| "Pasar comandos a la GPU de la Vita no conviene" (`analisis_offload_gpu_vita.md`) | **No.** Era sobre repartir un frame entre el GE y la Vita (necesitaban compartir el z de la VRAM). En B la Vita dibuja el frame **entero** con su propio z | — |
| "Emular el GE sobre GXM: descartada" (`filosofia.md`, ruta G) | No: se descartó por complejidad, que ya no es criterio | — |
| El ARM recorre igual de rápido que el PSP (KBENCH) | Afecta el presupuesto | Fue con una lista sintética, 1 hilo. **ARM a 444 MHz confirmado** en el proceso de Adrenaline (etapa 1). Repetir el recorrido en la etapa 2 con listas reales |
| La VRAM no está mapeada en Adrenaline (B4) | Obliga a los espejos (§3.5) | Etapa 0: **ninguna textura sin origen**, pero GT (texturas, sublistas, vértices) y RR (texturas) guardan datos fijos en la VRAM → espejo obligatorio para ellos. Investigar la dirección física de la VRAM desde el kernel de la Vita (riesgoso, baja prioridad) |
| La imagen nativa llega a 0x74000000 en el modo normal | Es el respaldo de B | **Etapa 1: el banco la dibujó dentro de su superficie [medido]**; queda abierta la pantalla negra durante la presentación (§3.5) |
| vpjuego: "GE en stall" siempre 0 % | Afecta T6 | Usar horas de encolado/FINISH en vez del muestreo |
| ¿SH hace 2 flips por frame? | Afecta al presentador | Los volcados muestran 1 flip y casi todo el frame dibujado **después** del flip en la misma lista; T4 confirma el ritmo |
| Lista con contexto encolada desde el plugin congela Adrenaline | B casi no encola listas propias | Los espejos usan el mismo camino que la copia por flip actual |
| Un plugin de la Vita puede cambiar el sombreador y la textura del dibujo de Adrenaline | Es la base del presentador | [medido con adrfsr 3b y con el banco de la etapa 1] |
| La GPU solo lee memoria registrada con `sceGxmMapMemory` | Para que la GPU lea la RAM del PSP directo | Resuelto en gepresent; **etapa 1: lectura de vértices desde 0x08800000 sin costo extra** |
| **Experimentos de GPU con GePatch activo** (etapa 1) | Riesgo de estabilidad | El banco v11.3 corriendo con GePatch activo (2x, `DRAW_NATIVE`) apagó la Vita entera. Causa sin confirmar. Hasta entender eso: lo nuevo de B se prueba con GePatch en pasivo/apagado (L) |

---

## 8. Riesgos principales
1. **Costo de los draws en GXM y del ARM** (etapa 1 lo midió): ~2 µs por draw con textura o programa nuevo, ~1,3 µs de
   GPU por draw. **[advertencia]** GT tiene ~9000 draws por frame: por la cuenta sintética podría no entrar a 60 fps sin
   juntar draws o repartir la grabación en varios núcleos. No se toma como verdadera ni como error hasta medir con las
   listas reales de GT (etapa 2). Si se confirmara: GT necesitaría juntar draws (atlas, reordenar opacos por textura) o
   seguir en A a ≤ 1,5x.
2. **Espejo de VRAM** (GT, RR): datos fijos que el juego sube a la VRAM; si cambian sin aviso, la Vita dibuja con datos
   viejos. GT además necesita el espejo para poder recorrer sus listas.
3. **Integración con Adrenaline:** se engancha el hilo de dibujo de otro programa. Cada versión de Adrenaline puede
   romperlo. A mediano plazo, proponer a isage/Adrenaline un punto de enganche oficial ("presentador externo").
   **Etapa 1:** con GePatch activo + un experimento de GPU la Vita se apagó entera (causa sin confirmar).
4. **Memoria del proceso de Adrenaline:** ~33 MB de LPDDR + 4 MB de CDRAM (etapa 1). Alcanza para B, pero no junto con
   el anillo de volcados de 24 MB de gepresent; la caché de texturas tiene que tener tope.
5. **Compatibilidad del traductor:** PPSSPP tardó años en cubrir el catálogo; el respaldo a la imagen nativa evita
   que un error deje un juego inutilizable.
6. **Datos que cambian sin aviso** (texturas dinámicas, búferes reusados a mitad de frame): huella completa y
   compuerta estricta por juego.
7. **Tirones por compilación de sombreadores** si se usa `libshacccg`: caché por juego.
8. **Memoria de los contextos diferidos:** reciclarla antes de tiempo hace caer la GPU (banco v11.2) [medido].

---

## Fuentes externas consultadas
- PPSSPP, descripción del GE (recorte solo en el plano cercano, guard band de 4096): https://www.ppsspp.org/docs/psp-hardware/gpu/ge-overview
- PPSSPP, regla de descarte por rango de vértices: https://github.com/hrydgard/ppsspp/issues/11393 y su aproximación fallida en TDU: https://github.com/hrydgard/ppsspp/issues/13355
- PPSSPP, volcados `.ppdmp` y depurador del GE: https://github.com/hrydgard/ppsspp/issues/11216
- Vita3K, mezcla programable de GXM: https://vita3k.org/blog/2021-07-31-3-years-Progress-Report
- vitasdk `gxm.h` (paletas, contextos): https://docs.vitasdk.org/gxm_8h_source.html
- vitaGL / vitaShaRK (`libshacccg.suprx`): https://github.com/TheOfficialFloW/vitaGL, https://github.com/Rinnegatamante/vitaShaRK
- Tiempo de compilación de sombreadores en la Vita (port de Half-Life 2): https://github.com/LargeVegetable/source-engine/releases
