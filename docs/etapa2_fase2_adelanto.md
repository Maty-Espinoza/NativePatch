# Etapa 2, fase 2 (punto 4): DISTINTAS de la VRAM y comprobación por adelantado (gepresent v13.12, 2026-10-29)

**Commit:** gepresent **v13.12** (= v13.12.2 con el número final; mismo código), con GePatch **0.2-dev66** sin cambios.
**Con esto se cierra la etapa 2** (decisión del usuario: 58 fps como mínimo en lo más pesado de GT alcanza por ahora).

Sigue a `etapa2_fase2_estadisticas.md` (puntos 2 y 3). Punto 4 de la propuesta de `etapa2_fase1_impacto.md` ("revisar
sublistas más barato o por adelantado") y el pendiente de las DISTINTAS (`etapa2_espejo_doble.md`). Marcas: [medido],
[deducido].

## En simple
- **DISTINTAS arregladas.** La Vita lee la memoria de video del PSP a través de dos copias (espejos) que se alternan cada
  frame. Cada sublista guardada recordaba sus líneas con la dirección del espejo en que se grabó, y en los frames del
  otro espejo comparaba contra una copia de un frame atrás: si la sublista había cambiado justo ahí, la daba por igual y
  usaba la vieja. Ahora guarda la dirección de la memoria de video y al comparar la traduce al espejo del frame actual.
  En GT: **0 DISTINTAS en ~100 000 revisiones** (antes 1–7 por corrida, todas de la VRAM).
- **Comprobación por adelantado.** Los dos ayudantes (lectores) comprobaban cada sublista al llegar a su llamada y
  llegaban tarde a casi la mitad. Ahora, apenas empieza una lista, reciben el plan (las sublistas que se aplicaron en la
  lista anterior, en orden) y lo comprueban **desde el final**, hasta encontrarse con el capturador.
- Resultado: algo mejor en lo más pesado de GT (57,3 → 58,2 fps, 17,4 → 16,8 ms de Vita por frame), pero el capturador
  sigue comparando ~5,5 ms por frame. El límite es **leer la memoria del PSP (~2 µs por línea)**, no el orden: los
  ayudantes alcanzan a comprobar ~130–150 sublistas por frame de ~270. Leer más barato (vista con caché o DMA) queda en
  `TODO.md` para el futuro.

## Cómo funciona (tools/gepresent/captura.c)
### Direcciones de la VRAM (arreglo de las DISTINTAS)
- `recLine(b, rb)`: guarda en `Memo.ra` la dirección lógica (`pc`, 0x04xxxxxx para la VRAM) y copia los datos desde
  donde se leyeron (`rb`, el espejo).
- `mline(b)`: para comparar, `vtr(b)` si es de la VRAM (espejo de `esp_u`, el del frame), `b` si es RAM. Si la página no
  tiene copia válida en ese espejo, no se puede comparar: el capturador la regraba (`memo_fail`), el lector la deja.
- `memoLines` (lectores) y `memoTry` (capturador) usan `mline`. `nearStall` y "ya guardada" siguen con la dirección lógica.

### Plan por adelantado
- `memoTry`, al aplicar una sublista: `racha++` (veces seguidas aplicada sin cambios; `recBegin` la deja en 0) y, si
  `racha >= pv_min` (8), la anota en `pv[pv_c]` (hasta 1024). `pv_pos` = cuántas lleva anotadas en esta lista (por dónde
  va el capturador).
- `pubList` (lista nueva para los lectores): lo anotado en la lista anterior pasa a ser el plan (`pv_plan`, `pv_pn`).
- `pvRun` (cada lector, antes de recorrer): toma la siguiente libre (`pv_i`) **desde el final**; si es una que el
  capturador ya pasó (`k < pv_pos`), se detiene. `memoMark` compara y deja la marca `vok` como antes; si ya estaba
  marcada para esta lista no vuelve a leer. Después el lector sigue con su recorrido de siempre (`lecWalk`).
- Riesgo aceptado: si el juego cambia una sublista después de comprobada y antes de que el capturador llegue, se usa la
  vieja. GT encola la lista completa y no la toca hasta la siguiente (`etapa2_fase1_impacto.md`, vida de la lista); la
  racha de 8 deja solo sublistas estables; la revisión 1/32 lo vería como DISTINTAS.
- Interruptor `ux0:data/gepresent/sin_adelanto` (sin plan; se apaga con "no" adentro).
- Log, línea de la memoria: `| adelanto/f: plan= comprobadas= no_iguales= tarde=` (tarde: veces que un lector se
  encontró con el capturador; desde v13.12.2 ~1 por frame).

### Prueba fuera de la Vita (`prueba_memoria.c`, qemu)
- `./prueba <modo> 3`: cada frame escribe sus sublistas de la VRAM solo en el espejo de ese frame y la sublista 10 cambia
  en el frame 4. Con el código de v13.11, `./prueba 2 3` da **BAD=1** en el frame 4 (el error de GT reproducido); con
  v13.12, BAD=0 y el cambio se detecta en el mismo frame.
- `./prueba <modo> <vram> 3`: los lectores solo hacen el plan antes que el capturador (`PVMIN=n`, por defecto 1): el
  capturador compara 0–3 líneas por frame en vez de ~45; ~20 de 21 sublistas llegan comprobadas.
- Modos 1/2 × vram 0/1/2/3 × lectores 0/1/2/3: columnas iguales a sin memoria y 0 BAD.

## Versiones
| Versión | Qué | md5 |
|---|---|---|
| v13.12.1 | direcciones de la VRAM + plan recorrido en orden | `f877801e…` |
| v13.12.2 | plan recorrido desde el final | `88888798…` |
| v13.12 (commit) | = v13.12.2 con el número de versión final | `04ccd6e6…` |

## Resultados [medido] (GT UCUS-98632, la misma carrera y auto, registro mediano)
Logs: `vitabridge/etapa2_v13.12.1/gt1`, `etapa2_v13.12.2/gt1` (contra `etapa2_v13.11.2/gt1`). Medianas por segundo.

| Comandos por frame | | v13.11 | v13.12.1 | v13.12.2 |
|---|---|---|---|---|
| 45–60 mil | fps / Vita por frame | 60,0 / 11,8 ms | 59,9 / 12,0 | 59,9 / 12,7 |
| 60–75 mil | fps / Vita por frame | 60,0 / 15,1 ms | 59,2 / 15,7 | 59,9 / 14,9 |
| más de 75 mil | fps / Vita por frame | 57,3 / 17,4 ms | 55,6 / 18,1 | **58,2 / 16,8** |
| más de 75 mil | sublistas aplicadas / llegan comprobadas | 272 / 130 | 273 / 131 | 272 / 135 |
| más de 75 mil | líneas que compara el capturador / µs | 2 423 / 5 359 | 3 273 / 5 676 | 2 775 / 5 660 |
| más de 75 mil | plan / comprobadas por adelantado | – | 278 / 158 | 276 / 131 |
| toda la corrida | DISTINTAS / revisadas | 3 / 51 121 | **0 / 50 324** | **0 / 49 371** |

- v13.12.1 (plan en orden): los lectores comprobaban ~160 por adelantado, pero iban a la par del capturador (no son más
  rápidos que él) y comparaban las mismas sublistas que él estaba por usar: el capturador comparó ~30 % más líneas y lo
  más pesado quedó en 55,6 fps.
- v13.12.2 (desde el final): se encuentran ~1 vez por frame; ~135–150 sublistas llegan comprobadas. Las que compara el
  capturador quedan al principio de la lista y son más largas (~20 líneas cada una contra ~17).
- Costo de leer: capturador ~1,9–2,0 µs por línea comparada; lectores ~2,4 µs por línea de su procesador (~2 900
  líneas por frame en ~6,8 ms). [deducido] Es lo que limita: con ~270 sublistas de ~10–20 líneas por frame no alcanza.
- Sin cuelgues; el usuario no reportó problemas de imagen ni de audio.

## Lo que queda (en `TODO.md`)
- Leer la memoria del PSP más barato (vista con caché o DMA; KBENCH midió 1,7–4 veces más rápido en bloques grandes):
  lo único que bajaría de verdad los ~5,5 ms de comparar. Postergado por el usuario: 58 fps mínimo alcanza.
- Las líneas de la VRAM sin copia válida en el espejo del frame ahora se regraban en vez de compararse con la copia vieja:
  si en otros juegos aparecen muchas "cambiaron" de sublistas en la VRAM, mirar esto primero.
