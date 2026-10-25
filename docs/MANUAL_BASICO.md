# Manual básico de gráficos para NativePatch

Para Matías. La idea es entender lo justo para seguir lo que hacemos, saber qué mirar cuando algo
se ve mal y poder mandar reportes más completos. No hace falta saber de gráficos: se explica
desde cero, con ejemplos de Ridge Racer (RR) y Silent Hill (SH).

---

## 1. La PSP por dentro: cuatro piezas

```
   CPU  ──escribe──►  RAM (32/64 MB)  ◄──lee── GE (chip gráfico)
    │                  listas, texturas,         │
    │                  vértices, datos           ▼
    └──────────────────────────────────►  VRAM (2 MB) ──► pantalla
```

- **CPU**: el procesador. Corre el juego: física, lógica, sonido. También **arma las listas de
  dibujo** (la "receta" de cada imagen).
- **GE** (Graphics Engine): el chip gráfico. Lee la receta y dibuja. Es rápido pero tonto: hace
  exactamente lo que dice la lista.
- **RAM**: memoria general. Ahí viven el código del juego, las listas, los modelos 3D, las
  texturas...
- **VRAM**: memoria de video, chica (2 MB) y rápida. Ahí el GE dibuja la imagen que se ve.

La CPU y el GE trabajan **en paralelo**: mientras el GE dibuja el frame 1, la CPU ya está
armando la receta del frame 2. Esto importa (sección 11.4).

## 2. ¿Qué es una imagen para la máquina?

- Una imagen es una grilla de **píxeles**. La PSP muestra **480x272** píxeles; la Vita, **960x544**
  (el doble en cada lado, cuatro veces más píxeles).
- Cada píxel es un número que dice su color. Formatos que vas a ver:
  - **565**: 16 bits por píxel (5 rojo, 6 verde, 5 azul). Es el de la pantalla.
  - **8888**: 32 bits (rojo, verde, azul y **alfa** = transparencia). Más calidad y más memoria.
  - **4444 / 5551**: otras variantes de 16 bits con alfa.
- **Framebuffer (fb)**: el pedazo de memoria donde se dibuja una imagen. Tiene una dirección
  (dónde empieza) y un **ancho de línea** (pitch). Por ejemplo, 512 píxeles por fila aunque solo
  se vean 480.
- **Doble buffer**: el juego dibuja en un fb mientras muestra el otro, y al terminar los cambia.
  Ese cambio es el **flip** (`sceDisplaySetFrameBuf`). En RR los dos fb son `0a000000` y
  `0a044000`: por eso en los volcados alternan.
- **Frame**: una imagen completa. A 60 fps hay 60 por segundo. El log numera cada línea con el
  frame (`009297 ...`).

## 3. La receta: display lists

El juego no le dice al GE "dibuja un auto". Le pasa una **lista de comandos**: cada comando es
una palabra de 32 bits, con 8 bits para *qué* (el número de comando) y 24 bits para *el valor*.

```
FRAMEBUFPTR   044000    ← dibuja en este fb
TEXADDR0      0fc7c0    ← usa esta textura
VERTEXTYPE    00019f    ← así vienen los vértices
VADDR         72e520    ← y están en esta dirección
PRIM          040004    ← ¡dibuja! (tipo 4 = tira de triángulos, 4 vértices)
```

- Un **draw** es un comando que dibuja (PRIM). Todo lo anterior es "estado": configura cómo se
  va a dibujar.
- **CALL / RET**: saltar a una sublista y volver (como llamar a una función). RR guarda cada
  modelo (auto, cielo, edificio) en una sublista que reusa en cada frame.
- **JUMP**: saltar sin volver.
- **Stall**: el juego puede encolar la lista a medias y avisar "puedes leer hasta aquí". El
  plugin parcha cada tramo nuevo.
- **FINISH / END**: fin de la lista.

**GePatch se mete justo aquí**: lee la lista antes que el GE y cambia palabras. Por ejemplo,
`FRAMEBUFPTR 044000 -> 000000` (dibuja en el buffer grande) o `PRIM -> CALL` (cambia un draw por
una lista propia del plugin). En el informe de `gpd.py` ves exactamente eso: lo que mandó el juego
y en qué lo convirtió el plugin.

## 4. Geometría: puntos, triángulos y cámara

- Todo lo 3D está hecho de **triángulos**. Cada esquina es un **vértice**, con posición (x, y, z) y,
  si hace falta, color y coordenada de textura (u, v).
- Una **tira de triángulos** (tira_tri) reusa vértices: con 4 vértices salen 2 triángulos.
- **2D o "through"**: el juego da coordenadas de pantalla directas (HUD, menús, texto, sprites).
  GePatch las **duplica** a mano (x2) para que calcen en 960x544.
- **3D**: el juego da posiciones "del mundo" y el GE las proyecta a la pantalla con tres
  **matrices** (tablas de números):
  1. **Mundo**: dónde está el objeto (el auto en la pista).
  2. **Vista**: dónde está la cámara y hacia dónde mira.
  3. **Proyección**: la perspectiva (lo lejano se ve chico).
- **Viewport**: cómo se pasa del espacio de la cámara a píxeles: un centro y una escala. GePatch
  dobla la escala para que la imagen salga al doble.
- **Scissor / región**: un rectángulo fuera del cual no se dibuja. También se dobla.

## 5. Texturas

- Una **textura** es una imagen que se "pega" sobre los triángulos (el asfalto, las nubes del
  cielo, la carrocería).
- Puede estar en la RAM o en la VRAM. En RR el cielo usa la textura `0a0fc7c0`, que está en la
  VRAM falsa (sección 11).
- **Paleta (CLUT)**: algunas texturas no guardan colores sino números (0..255) que se buscan en
  una tabla de colores. Ahorra memoria. Si la paleta está mal, la textura sale con colores raros.
- **Swizzle**: la textura guardada en bloques desordenados para que el GE la lea más rápido. Es
  solo un detalle de orden.
- **Textura desde un fb**: un juego puede dibujar algo y después usar *esa imagen* como textura
  (sección 8). Es una de las cosas más difíciles para GePatch.

## 6. Profundidad: el z-buffer

- Problema: si dibujas el auto y después un edificio que está detrás, ¿quién tapa a quién?
- Solución: además del color, cada píxel guarda **qué tan lejos está** lo que se dibujó ahí. Esa
  imagen de distancias es el **z-buffer** (o depth buffer).
- **Prueba de profundidad**: al dibujar un píxel nuevo, el GE compara su distancia con la
  guardada y solo lo pinta si está más cerca.
- En la Vita el z-buffer del buffer grande está en `0x04100000`. Como la VRAM está llena, GePatch
  **presta esa zona** para otras cosas (render targets a escala 1). Antes guarda el z en la RAM y
  después lo devuelve (Z_SAVE). Si eso falla, se ven cosas atravesando paredes o que desaparecen.

## 7. Recorte: por qué al doble desaparecen triángulos

- **Plano cercano**: la cámara no ve lo que está detrás de ella ni pegado al lente. Un triángulo
  que cruza ese plano se **recorta**: se corta el pedazo de atrás.
- **w**: un número que sale de la proyección. Para lo que está delante de la cámara es positivo;
  detrás, negativo. Si ves "w<0" en una conversación, significa "detrás de la cámara".
- **El límite del GE**: el GE trabaja con coordenadas de pantalla de 0 a 4096. Si **un solo
  vértice** de un triángulo cae fuera de ese rango, el GE **tira el triángulo entero**.
  - A 1x (480x272) la pantalla ocupa el centro de ese espacio y sobra mucho margen.
  - **Al doble, el margen se achica a menos de la mitad.** Los triángulos grandes y cercanos (el
    asfalto bajo el auto en RR, el cielo) tienen vértices lejos, fuera de pantalla, y ahora caen
    fuera del rango. El GE los tira y aparecen huecos.
- **Recorte 3D de GePatch** (`sub.h`, opción `recorte3d`): el plugin calcula en la CPU dónde
  caerá cada vértice. Si un triángulo se va a perder, lo corta en pedazos que sí quepan y dibuja
  esos pedazos en vez del original. Así volvió el camino en RR (dev48), pero desde ahí salió el
  **cielo negro**. Entender esta sección es entender el bug más importante de ahora.

## 8. Render targets: dibujar en una imagen escondida

- A veces el juego no dibuja en la pantalla sino en un fb escondido, para usarlo después como
  textura:
  - el **retrovisor** de RR (la vista de atrás se dibuja aparte y se pega en el espejo);
  - **reflejos** en la carrocería (mapa de entorno, fb `0a088000` de 256 de ancho);
  - **desenfoque o brillo**;
  - la **linterna** de SH (pase de luz que se mezcla sobre la escena).
- Eso es un **render target (rt)**. Los fb con ancho distinto de la pantalla suelen serlo (64,
  256...).
- GePatch:
  - Los dibuja a **escala 1** (tamaño original) en la zona del z-buffer (**rt1**).
  - Al terminar los copia a la VRAM falsa, para que el juego los encuentre donde los dejó.
- Errores típicos: el rt sale vacío (retrovisor negro), con basura (cuadrados o franjas
  moradas) o del frame anterior.

## 9. Copias (TRANSFER)

- El GE puede copiar un rectángulo de memoria a otro lado sin dibujar. Se usa para videos,
  pantallas de carga y para guardar o restaurar cosas.
- GePatch escala algunas copias (las de videos) y usa copias propias (Z_SAVE, rt1).

## 10. Mezcla (blending) y alfa

- **Alfa**: cuánto se ve un píxel (0 = invisible, 255 = sólido).
- **Mezcla**: cómo se combina lo que se dibuja con lo que ya había: sumar (brillos), multiplicar
  (sombras, la linterna de SH) o promediar (transparencias).
- Si la mezcla usa una imagen equivocada, salen colores lavados, negros o fluorescentes.

## 11. Memoria y direcciones

### 11.1 Direcciones en hexadecimal
- Las direcciones se escriben en base 16 (0–9 y a–f): `0x0A000000`. El `0x` solo avisa que es
  hexadecimal.
- Truco para leerlas: los primeros dígitos dicen la zona.
  - `0x04......` es **VRAM**.
  - `0x08......` y `0x09......` es la **RAM del juego**.
  - `0x0A......` es la **RAM extra que usa el plugin**.
- En los volcados muchas veces se ve solo una parte (`044000` en vez de `0x0A044000`): el
  comando guarda 24 bits y la "base" pone el resto.

### 11.2 El mapa
```
0x04000000  VRAM real (2 MB)  ── buffer grande 960x544 + z-buffer (0x04100000)
0x08000000  RAM del juego (código, listas, modelos, texturas)
0x0A000000  RAM extra: aquí vive GePatch
   0x0A000000  VRAM falsa (el juego cree que es su VRAM)
   0x0A400000  imagen final que Adrenaline muestra en la Vita
   0x0A600000  tabla de parches (qué palabra original había en cada comando)
   0x0A800000  listas propias del plugin (anillo de 512 en dev51)
   0x0AA00000  zona de los volcados
   0x0AE00000  recorte 3D (vértices nuevos)
   0x0AF60000  Z_SAVE (z-buffer guardado)
0x0B000000  memoria de Adrenaline  ── NO TOCAR
```

### 11.3 ¿Por qué una VRAM falsa?
- El juego necesita su VRAM para sus fb, texturas y z-buffer de 480x272. Pero la VRAM real (2 MB)
  la ocupa el buffer grande de 960x544.
- Solución: GePatch le dice al juego "tu VRAM está en 0x0A000000" (RAM). El juego guarda ahí
  sus texturas y cree que dibuja ahí. El plugin redirige los dibujos de pantalla al buffer grande.

### 11.4 El GE va atrasado
- El plugin parcha la lista cuando el juego la entrega. El GE la ejecuta **más tarde**, hasta uno
  o dos frames después.
- Si el plugin escribe una lista propia en un lugar y antes de que el GE la use la **pisa** con
  otra, el GE ejecuta la equivocada: falta algo o sale basura.
- Por eso las listas propias van en un **anillo** grande (como una cinta circular: se escribe
  siempre en el siguiente lugar y se vuelve al principio cuando se acaba). En dev51 se agrandó
  de 64 a 512 lugares porque con el recorte 3D se usaban ~42 por frame y la cinta daba la vuelta
  demasiado rápido.

### 11.5 Caché (nivel básico)
- La CPU tiene una memoria intermedia rápida (caché). Lo que escribe puede quedarse ahí un rato
  antes de llegar a la RAM. El GE lee la RAM, no la caché.
- Por eso el plugin "vacía la caché" (writeback) antes de que el GE lea lo que escribió. Si no,
  el GE ve datos viejos.

### 11.6 ¿Por qué se cae el juego?
- Si el plugin (o el GE con una lista mala) escribe en memoria que no es suya, pisa datos del
  juego o de Adrenaline: una dirección de función, una lista, una variable. Cuando el juego la
  usa, salta a cualquier lado. Por ejemplo "Bad Execution Address", "Jump to 00000000" o una Vita
  que se cierra.
- Otra causa: leer un número de 16 bits desde una dirección impar. La CPU de la PSP no lo permite
  y se cae. Es lo que pasó con dev49 en la prueba automática.

## 12. Un frame con GePatch, paso a paso

1. El juego arma la lista del frame y la encola.
2. GePatch la recorre comando por comando:
   - fb de pantalla → al buffer grande;
   - fb chico → render target a escala 1 (rt1);
   - viewport y scissor → x2;
   - coordenadas 2D → x2;
   - draws 3D que se salen del rango al doble → recorte 3D;
   - texturas que leen un fb → se redirigen (texmode).
3. El GE ejecuta la lista ya parchada y dibuja en el buffer grande de la VRAM real.
4. En el flip, GePatch copia el buffer grande a 0x0A400000 y Adrenaline lo muestra en la Vita.

## 13. Log y volcado: qué son y cómo sacar buenos

- **Log** (`ge_patch.log`): texto con eventos.
  - Al inicio: versión, memoria, fb vistos.
  - Cada 600 frames: estadísticas (fps, costo del plugin, draws, recortes).
  - Eventos nuevos, una vez cada uno ("textura desde fb...", "CALL a dirección inválida...").
  - El número del principio de cada línea es el frame.
- **Volcado** (`.gpd`, L+R+SELECT): todo lo que pasó en un frame:
  - cada comando (original y parchado);
  - cada draw con sus vértices y texturas;
  - la VRAM al principio y al final;
  - los eventos.
  - Con eso se puede reconstruir el frame en el PC, incluso cómo se vería **sin GePatch**.
- Tips para un buen volcado:
  - Captura **justo cuando el error está en pantalla**. Si el error parpadea, saca 2 o 3.
  - Anota qué estabas viendo ("cielo negro en el puente, cámara exterior").
  - Si puedes, saca el mismo lugar en **modo pasivo** (mantener L al iniciar el juego): es la
    referencia de cómo debería verse.
  - Probar el mismo lugar con otra versión (carpeta `versiones/` del laboratorio) ayuda a saber
    **desde cuándo** está el error.
  - Desactivar una opción en el menú (L+R+START), por ejemplo `recorte3d`, y ver si el error se
    va, dice mucho de dónde está.

## 14. Lo mínimo de `gpd.py` para ti

- `python3 tools/gpd.py resumen archivo.gpd`: versión, opciones y en qué se dibujó.
- `python3 tools/gpd.py original archivo.gpd carpeta --ppsspp=<PPSSPPHeadless>`: deja
  `carpeta/comparacion.png` con tres paneles: **sin GePatch | con GePatch | diferencias en
  rojo**. Es la forma más rápida de ver qué rompió el plugin.
- `python3 tools/gpd.py imagenes archivo.gpd carpeta`: PNG de cada fb y del buffer grande.

## 15. Glosario rápido

| Término | En simple |
|---|---|
| GE | el chip gráfico de la PSP |
| display list | la receta de comandos que el juego le pasa al GE |
| draw / PRIM | un comando que dibuja |
| fb / framebuffer | memoria donde se dibuja una imagen |
| buffer grande | la pantalla de 960x544 que arma GePatch en la VRAM real |
| VRAM falsa | RAM que el juego cree que es su VRAM (0x0A000000) |
| flip | el momento en que se muestra el frame terminado |
| vértice | esquina de un triángulo |
| through / 2D | coordenadas de pantalla directas (HUD, menús) |
| matriz | tabla que mueve, gira o proyecta los vértices |
| viewport | cómo se pasa de la cámara a píxeles |
| scissor | rectángulo fuera del cual no se dibuja |
| textura | imagen pegada sobre triángulos |
| CLUT / paleta | tabla de colores de una textura indexada |
| z-buffer | distancia de cada píxel, para saber qué tapa a qué |
| plano cercano | límite delante de la cámara; lo que está detrás no se dibuja |
| w | número de la proyección; negativo = detrás de la cámara |
| recorte 3D | cortar triángulos para que quepan en el rango del GE |
| render target / rt / rt1 | fb escondido que después se usa como textura |
| TRANSFER | copia de memoria hecha por el GE |
| alfa / mezcla | transparencia / cómo se combinan los colores |
| slot / anillo | lugar para una lista propia del plugin / cinta circular de esos lugares |
| setOp / parche | cambiar una palabra de la lista recordando la original |
| modo pasivo | el plugin no cambia nada (referencia) |
| volcado / .gpd | grabación completa de un frame |

## 16. Checklist antes de reportar

- [ ] Versión del plugin (sale en la primera línea del log).
- [ ] ¿Vita o laboratorio?
- [ ] Juego, escena y qué se ve mal, con tus palabras.
- [ ] ¿Desde qué versión pasa? ¿Pasa en modo pasivo?
- [ ] ¿Cambiaste opciones del menú?
- [ ] Log y volcado de la escena (y el número de frame de la captura).
- [ ] Si se cayó: foto o captura del mensaje y el log de esa sesión.
