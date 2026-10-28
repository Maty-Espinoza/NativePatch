# Etapa 0 de la arquitectura B: resultados (2026-10-28) — CERRADA

Primera etapa de `arquitectura_dibujo_en_vita.md` (§6): medir, **sin la Vita**, cuánto trabajo tendría la Vita si
dibujara ella cada frame del juego. Hecho en el PC con Windows (VM de Claude sobre `Documents\GitHub` y `Downloads`).
**Sin cambios en el plugin, en la Vita ni en el laboratorio.** Sin versión nueva de GePatch. `tools/carga_vita.py` entró
en el commit de la etapa 1.

## Herramienta: `tools/carga_vita.py`

```
python3 tools/carga_vita.py VOLCADO [VOLCADO...] [--detalle]
```
- Usa `gpd.py` y `carga_ge.py` (posiciones 3D como el GE). Solo biblioteca estándar. ~1 s por volcado.
- Cuenta **solo lo que mandó el juego** (valor original de cada comando; los parches del plugin no cuentan).
- **Unidad = el volcado entero, dividido por los flips.** No se corta en el flip porque **SH dibuja casi todo su
  frame después del flip, en la misma lista** (`L L F` y después ~1000 draws). GT: 1 lista y 1 flip; RR: 18 listas.
- En un volcado activo el juego ve su VRAM en la VRAM falsa (0x0A000000–0x0A3FFFFF): ahí se cuenta como VRAM. En B
  esos datos estarían en la VRAM real, que el ARM no ve.
- Grupos de cambios (como los pagaría GXM): **destino** (escena nueva), **programa/estado fijo** (tipo de vértice,
  mezcla, z, stencil, alfa, luces encendidas…), **textura** (dirección, tamaño, formato, CLUT, filtro), **constantes**
  (viewport, colores, luces, niebla, scissor) y **matrices**. VADDR/IADDR/BASE/CALL no cuentan como cambio.
- "Lotes" = draws que no se pueden juntar con el anterior (algo cambió, aunque sea una constante).
- Origen de cada textura en la VRAM: rt o z dibujado antes en el frame, TRANSFER del frame, fb que el juego dibuja en
  otro momento (lo tendría el administrador de fb), **fija en el volcado** (igual al empezar y al terminar: subida
  antes) o **desconocida** (cambió sin que el GE la escribiera).
- "Espejo" = KB de la VRAM que la Vita necesitaría (listas, vértices, índices, CLUT y texturas que no dibujó el GE).
  Con varios volcados, "igual" = % de ese espejo idéntico en el volcado anterior del mismo juego (misma dirección).
- Guard band: triángulos 3D con algún vértice fuera de 0..4096 (a 1x y con la escala al doble y el mismo offset);
  los que cruzan el plano cercano se cuentan aparte (se recortan, no se descartan). Sin huesos ni morph.

Resultados completos: `Downloads\etapa0\carga_vita_{GT,RR,SH}.txt`. Volcados usados: los 90 de
`vitabridge\recibidos` (GT dev50–dev60.1, RR dev39–dev59.12, SH dev28–dev58.2, 5 de SH pasivos). **No hay volcados
de LocoRoco.**

## Resultados (por frame; mediana y rango de los volcados con más de 100 draws)

| | GT (21 volcados) | RR (25) | SH (28) |
|---|---|---|---|
| Draws | **9100** (314–15 157) | 1100 (546–3058) | 316 (233–1396) |
| De ellos 2D | 3 | 111 | 58 |
| Comandos | 44 000 (hasta 76 000) | 9 000 (6 500–15 900) | 9 000 (6 400–27 300) |
| Destinos (escenas) | **1** | 5 | 9 (8–14) |
| Draws con cambio de programa/estado fijo | 133 (31–261) | 57 | 93 |
| Draws con cambio de textura | **5300** (hasta 8600) | 117 | 136 |
| Lotes (no se juntan con el anterior) | = draws con cambio caro (4800–8700) | 170–270 | 150–480 |
| Texturas distintas | 400–700 (460–1100 KB) | 20–124 (0,6–1,4 MB) | 17–109 (2,7–4,6 MB: sus propios rt de 8888) |
| Texturas en la VRAM | 180–490, **todas fijas** | 13–100 fijas + 2–3 rt | **2–6, todas rt del frame** |
| Texturas desconocidas | 0 | 0 | 0 |
| Vértices (KB) | 550–1080 (sin repetir 500–970) | 80–350 | 285–3100 (sin repetir 240–780) |
| Índices | no usa | no usa | 5–21 KB |
| Cargas de CLUT | 576 (33–1245) | 111 | 167 (154–437) |
| TRANSFER | 220 (3 KB) | 0 | 0–1 (hasta 256 KB) |
| **Comandos en la VRAM** | **3200–10 500** (~20 % de la lista: sus sublistas) | 0 | 0 |
| **Draws con vértices en la VRAM** | **1400–4900** (~35 %) | 0 | 0 |
| **Espejo de VRAM necesario** | **370–700 KB** | 70–460 KB | **0** |
| Espejo igual al volcado anterior | 77–100 % (mediana ~83 %) | 74–100 % | — |
| Triángulos 3D | 30 000–60 000 | 2 200–8 600 | 700–79 000 |
| Descartados por guard band 1x / 2x | 140–800 / 300–2100 | 20–80 / 100–270 | 0–490 / **26–1770** |
| Formatos | clut4 swizzle (casi todo), dxt1, dxt5, clut8 | clut4/clut8 swizzle, 5551, 8888 | clut4/clut8 swizzle, 8888 (rt), 5650, 4444, **clut32 desde la VRAM** (efecto tipo depal) |

Primitivas: casi todo **tiras de triángulos**; GT ~60 rectángulos, RR ~20 y ~33 abanicos, SH sprites 2D. Sin bézier
ni spline en estos juegos.

## Respuestas a las preguntas de la etapa 0

**1. ¿Cuánto trabajo por frame tendría la Vita?**
- **SH y RR: poco.** 150–480 lotes por frame, < 100 cambios de programa: entran holgados si un draw cuesta ≤ 2 µs
  (etapa 1: cuesta ~2 µs con textura nueva).
- **GT: mucho más de lo estimado.** El documento suponía ~4000 draws; son **~9000 (hasta 15 000)**, casi todos tiras
  chicas, y en ~5300 cambia la textura. Los cambios de programa son pocos (~130). Lo que decide es **cuánto cuesta en
  GXM un draw que solo cambia la textura** (etapa 1: ~2 µs; para GT queda como advertencia, ver `etapa1_banco_gxm.md`).
- Vértices: GT 0,5–1 MB por frame y SH hasta 3 MB (0,8 MB sin repetir). Copiarlos por DMA (7,2 ms/MB) costaría
  4–7 ms en GT: hay que **leerlos directo de la RAM del PSP con la GPU** (etapa 1: funciona a la misma velocidad).

**2. ¿Cuántas texturas en la VRAM no tienen origen conocido?**
- **Ninguna "desconocida"** en 90 volcados (ventana de 1 frame: ninguna cambió sin que la dibujara el GE).
- Pero **GT y RR guardan en la VRAM sus texturas fijas** (GT 180–490, RR 13–100 por frame), subidas antes del frame.
  En B no se pueden leer: necesitan espejo. SH en cambio solo lee de la VRAM sus propios render targets: **para SH no
  hace falta espejo**.

**3. Hallazgo nuevo: GT guarda listas y vértices en la VRAM.**
- ~20 % de los comandos de GT (sus sublistas, en 0x0A1BC000+ de la VRAM falsa) y los vértices de ~35 % de sus draws
  están en la VRAM. En B estarían en la VRAM real: **el capturador no podría recorrer las listas de GT sin espejo**.
  [deducido: en B el juego recibe la VRAM real de `sceGeEdramGetAddr`]
- El espejo que necesita GT es de 370–700 KB por frame; entre volcados distintos del mismo juego el 77–100 % es igual
  (datos cargados con la pista). Dentro de un frame no cambia nada.
- Copiarlo entero con el GE cada frame: ~1,6 ms de GE (2,3 ms/MB). Opciones a evaluar en la etapa 2: espejo completo
  al cargar + verificación periódica; espejo por frame solo de las sublistas (~40 KB, ~0,1 ms); o, solo para juegos
  así, una "VRAM falsa" para datos (sin fb) que exigiría tocar listas (es lo que hace hoy el modo A).

**4. Guard band.**
- A 1x (lo que haría B): GT descarta 140–800 triángulos, RR 20–80, SH 0–490. B tiene que descartar exactamente esos.
- A 2x (GePatch actual): GT 300–2100, RR 100–270 (los huecos conocidos del camino), **SH 26–1770**. Lo de SH a 2x no
  estaba registrado como error: candidato a huecos en SH con el modo A (no se agregó a `errores_conocidos.md`).

**5. T3 (`.ppdmp` de PPSSPP): HECHO.**
- `gpd.py ppsspp` genera archivos válidos (cabecera `PPSSPPGE` versión 6, zstd). PPSSPPHeadless dibuja un `.ppdmp`
  pasivo de SH igual que la Vita (`diagnostico.md`).
- Generados: `Downloads\etapa0\{UCUS-98632_010777, ULJS-00001_003603, ULUS-10450_005501, ULUS-10450_002052}.ppdmp`
  (de activos se reproducen los comandos del juego con su VRAM falsa como VRAM). GT pesa 75 MB (70 000 comandos).
- **Confirmado por el usuario (2026-10-28):** `~/gepatch-lab/ppsspp/build/PPSSPPSDL
  /mnt/c/Users/spect/Downloads/etapa0/ULJS-00001_003603.ppdmp` en WSL abre el volcado y muestra la imagen en el emulador.

## Consecuencias para el plan (aplicadas en `arquitectura_dibujo_en_vita.md`)
- §5: "GT ~4000 draws 3D/frame" → **~9000 (hasta 15 000)**, ~5300 con textura nueva, ~130 con programa nuevo.
- §6, criterio de la etapa 1: medir por separado (a) draw sin cambios, (b) draw que **solo cambia la textura** (y la
  paleta), (c) draw con programa nuevo, (d) escena nueva.
- §3.5: agregar como origen de datos en la VRAM las **texturas fijas, sublistas y vértices** que el juego sube a la
  VRAM antes del frame (GT, RR). El espejo es obligatorio para GT y RR.
- §3.6 vértices: leerlos de la RAM del PSP con la GPU en vez de copiarlos (salvo los de la VRAM, que van al espejo).
- SH es el mejor candidato para la primera prueba 3D de B: sin espejo, pocos draws, sus texturas de la VRAM son sus rt.

## Límites de la medición
- Un volcado = ~1 frame: "fija en el volcado" no prueba que la textura no cambie entre frames. El "igual" entre
  volcados compara momentos distintos (a veces otra pista o escena).
- En los volcados activos, la VRAM falsa al terminar puede incluir copias del plugin; no se vio ninguna textura del
  juego afectada.
- GT y RR tienen volcados de menú/carga (1–2 draws, o RR 006477 con 1 destino): se excluyen de la tabla por tener
  ≤ 100 draws.
- El guard band a 2x supone el mismo offset; GePatch puede moverlo.
