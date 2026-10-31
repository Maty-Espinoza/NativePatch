# Etapa 2: T4, T6 y vida de los datos (GePatch dev63.1–dev63.3 + gepresent v13.5.1–v13.5.3, 2026-10-28)

Sigue a `etapa2_comparar_menos.md` (gepresent v13.4). Marcas: [medido], [deducido].
**Commit:** GePatch **0.2-dev63** (= dev63.3 con el número final) + **gepresent v13.5** (= v13.5.3, mismo código).

## Pregunta
La Vita va a dibujar un frame después de leer la lista, y su GPU leerá vértices, índices, texturas y paletas directo de la
RAM del PSP. **¿Siguen ahí los datos cuando la Vita los dibuje?** Además: cuánto trabaja el GE del PSP (T6) y qué le
avisa la CPU del juego (T4).

## En simple
- **GePatch** anota en el buzón, sin tocar el juego: cuándo empieza y termina de trabajar el pintor del PSP (con un
  hilo que lo mira cada 0,25 ms, **apagado por defecto**: le quitaba 2–3 fps a GT), cuánto espera el juego al pintor
  en cada sync, y cada vez que la CPU del juego vacía la caché (con dirección y tamaño).
- **gepresent** toma una muestra de los datos que usan los dibujos y la vuelve a mirar 1 y 2 frames después.
- **Respuesta:** en GT y SH los vértices, índices y paletas **no cambian durante el frame siguiente**; se reescriben 2
  frames después (los juegos usan dos juegos de búferes). Las texturas casi nunca cambian, salvo unas pocas de SH en la
  escena exterior que cambian dentro del mismo frame.
- **Regla para la etapa 3:** la Vita tiene que terminar de dibujar el frame N antes del flip N+2 (es decir, durante el
  frame siguiente). Si se atrasa más: repetir la imagen anterior o hacer esperar al juego. No hace falta copiar vértices.

## Cómo funciona
### GePatch dev63 (main.c, "T6 y T4")
- Buzón versión 3 (`SB[1] = 3`). Eventos nuevos: `EV_GEFIN` (7: a = inicio del tramo ocupado, b = muestras en el stall,
  c = muestras; la hora del evento es el fin, ±250 µs), `EV_SYNCEND` (8: después de `sceGeDrawSync`/`ListSync`: a = µs
  esperados, b = GE ocupado al llamar, c = 0 drawsync / 1 listsync), `EV_CPU` (9: dato = tipo 0 WritebackRange, 1
  WritebackInvalidateRange, 2 InvalidateRange, 3 WritebackAll, 4 WritebackInvalidateAll, 5/6 DMA; a = dirección o
  destino, b = tamaño u origen, c = tamaño del DMA; hasta 96 por frame) y `EV_CPUSUM` (10, en el flip: dato = eventos sin
  anotar, a = llamadas a la caché, b = KB, c = DMA llamadas | KB << 16).
- `geThread` (prioridad 0x11): con `SB[0x47] = 1` (lo pone gepresent si existe `ux0:data/gepresent/medir_ge`) mira
  `sceGeGetReg(3/5/6)` cada 250 µs mientras el GE tiene trabajo, 2 ms si no; sin el pedido duerme 100 ms. El inicio del
  tramo es la hora del encolado si el GE estaba libre.
- Ganchos de `UtilsForUser` (`sceSystemMemoryManager`): las 5 funciones de la caché, encontradas. `sceDmac`: no se
  encontró con ningún nombre de módulo probado (pendiente en `TODO.md`).
- Log de GePatch cada 600 frames: `stats T6 (por frame): GE ocupado ... | T4 (por frame): cache ... dma ...`.

### gepresent v13.5 (tools/gepresent/captura.c, "Vida de los datos")
- `smpAdd`: 1 de cada 64 draws (sus vértices o índices), 1 de cada 8 texturas nuevas del frame, 1 de cada 16 cargas de
  paleta; hasta 8 vértices y 4 de lo demás por frame; solo en la RAM. Huella de los primeros 256 bytes al anotarla. Las
  sublistas aplicadas por la memoria cuentan con los vértices de su primer draw (`Memo.sv_a`, relativo a los vértices de
  entrada si no los fija adentro).
- `ltFlip` (capturador, en cada flip): huella otra vez en los 2 flips siguientes; cuenta las que cambiaron y si una
  escritura conocida de la CPU las cubría (rango o "vaciar toda la caché", `cwCovers`).
- **v13.5.1** lo hacía el hilo del espejo a los 17/33 ms: casi todas las muestras llegaban tarde y le quitaba tiempo al
  lector del núcleo 0 (puente de GT a 49–56 fps). v13.5.2 lo pasó al capturador en el flip.
- Líneas nuevas del log (con GePatch dev63): `T6 GE del PSP (por frame): ocupado= ... | sync/f= esperas_al_GE= ... | T4
  CPU del juego (por frame): cache= dma= a_la_vram/seg= vaciar_todo/seg= sin_anotar/seg=` y `vida de los datos (por seg;
  cambiaron 1 / 2 frames después; aviso: rango / vaciar_todo): vertices= ... | revisadas a los X / Y ms`.
- Prueba fuera de la Vita: `VIDA=1 ./prueba 1 0 0` (muestras que cambian antes del flip 1 o del 2, con aviso de rango y
  de vaciar todo; líneas `vida:` con lo esperado).

## Versiones
| Versión | Qué | md5 |
|---|---|---|
| dev63.1 + v13.5.1 | T4/T6 + vida de los datos en el hilo del espejo | `4bec29af…` / `38d60292…` |
| dev63.2 + v13.5.2 | revisión en el flip del capturador; correcciones de T6 | `7251a065…` / `35fcb9bc…` |
| dev63.3 + v13.5.3 | hilo de T6 apagado por defecto (`medir_ge`) | `382d8216…` / `aac61a52…` |
| dev63 + v13.5 (commit) | = dev63.3 + v13.5.3 con los números finales | `23589485…` / `5967a226…` |

## Resultados [medido]
Logs: `vitabridge/etapa2_dev63.1/gt1/`, `etapa2_dev63.2/gt1/`, `etapa2_dev63.2/sh1/`, `etapa2_dev63.3/gt1/`.

### Vida de los datos
| | GT carrera (dev63.2) | GT carrera (dev63.3) | GT menús/cargas | SH todo | SH últimos 30 s (exterior) |
|---|---|---|---|---|---|
| vértices: muestras / cambian 1 frame / 2 frames | 39 737 / 1 / 508 (1,3 %) | 47 497 / 2 / 884 (1,9 %) | 6 798 / 0 / 909 (13 %) | 28 729 / 1 / 260 (0,9 %) | 4 860 / 1 / 61 (1,3 %) |
| índices | — (GT no usa) | — | — | 5 933 / 0 / 212 (3,6 %) | 1 445 / 0 / 138 (9,5 %) |
| texturas | 23 889 / 1 / 1 | 28 751 / 0 / 0 | 5 648 / 3 / 4 | 24 976 / 97 (0,4 %) / 114 | 4 890 / **72 (1,5 %)** / 85 |
| paletas | 11 386 / 0 / 0 | 15 763 / 0 / 0 | 1 725 / 0 / 0 | 25 905 / 0 / 0 | 4 887 / 0 / 0 |
- La revisión "1 frame después" es en el flip siguiente a la captura: GT a los ~6–11 ms, SH ~7 ms; "2 frames" a los
  ~24–28 ms (GT) y ~30–60 ms (SH).
- Todos los cambios quedaron cubiertos por un "vaciar toda la caché" (GT lo hace ~3 veces por frame, ~200 por segundo):
  **no sirve para saber qué zona cambió**. Avisos por rango: casi ninguno (GT ~3 por frame de pocos KB; SH 3–7).
- **SH, exterior:** ~1,5 % de las texturas muestreadas en la RAM cambian dentro del mismo frame (¿la linterna u otro
  efecto que SH arma con copias? hint del usuario: lo que "pinta" SH puede ser la linterna). Para la etapa 3: releer o
  copiar esas texturas en cada uso (detectarlas como "dinámicas").

### T6 (GE del PSP)
- **GT** en carrera: ocupado ~11,4 ms por frame, de una vez (1 tramo por frame, 0 % en el stall); el juego llama 3 sync por
  frame y espera al GE ~0,9 veces por frame, ~735 µs. Margen del GE: ~5 ms.
- **SH**: el GE no se detiene nunca (tramos de 100–240 ms que cruzan frames; ~20–35 ms de GE por frame, 0 % en el stall
  en el juego, 16–58 % en las cinemáticas). SH está limitado por el GE (como sin plugin). Últimos 30 s: 1 sync por frame,
  espera al GE ~0,4 veces por frame, ~3,7 ms; 40,8 fps.
- "GE ocupado al llamar" sale siempre 0 (pendiente en `TODO.md`).

### fps de GT (GePatch, cada 10 s)
| | v13.4.2 (dev62) | dev63.1 + v13.5.1 | dev63.2 + v13.5.2 | dev63.3 + v13.5.3 |
|---|---|---|---|---|
| comienzo de la carrera | 59,4 | 59,1 | 56,7 | 59,8 |
| último tramo (puente) | 58,4 | 57,7 | 55,3 | 57,8 |
| frames por seg en los 6 segundos más pesados | 57–60 | 49–56 | 54–57 | 55–60 |
- dev63.2 (hilo de T6 encendido) le quitó 2–3 fps a GT; dev63.3 (apagado) vuelve a lo de v13.4. 0 DISTINTAS en todas.

## Lo que falta / ideas
1. Estrategia de datos para la etapa 3 (ver En simple): dibujar el frame N durante el frame N+1; si se atrasa, repetir
   o hacer esperar al juego en el flip N+2; texturas dinámicas de SH: releer.
2. Punto 6 de la etapa 2: espejo estable (dos copias alternadas) + texturas de GT al día.
3. Grabación GXM en 2–3 núcleos fijos (antes de GT en la etapa 4): GT pide ~16 500 draws por frame.
4. T2 (caja negra): al final de la etapa.
5. Pendientes chicos en `TODO.md` (DMA, "GE ocupado al llamar").
