# Etapa 2, fase 1: por qué el modo sombra todavía le cuesta a GT (GePatch dev65 + gepresent v13.9, 2026-10-29)

**Commit:** GePatch **0.2-dev65** + gepresent **v13.9** (= dev65.3 + v13.9.5 con los números finales).

Sigue a `etapa2_nucleos.md`. Plan acordado con el usuario: **fase 1** = medir todo lo necesario sin importar el costo;
**fase 2** = mejorar por iteraciones con esos datos. Marcas: [medido], [deducido]. Niveles de registro: `registros.md`.
Antes de esto se probó gepresent v13.8.1/v13.8.2 (comprobar sublistas sin releer líneas ya comprobadas, leer de a 64–256
bytes): leyó ~45 % menos de la RAM del PSP pero el tiempo no bajó y las DISTINTAS subieron (2 → 13, todas de sublistas en
la VRAM) → **descartado, sin commit** (parche en `vitabridge/gepresent_v13.8.2.patch`).

## En simple
- **Todo el costo para el juego es la espera a la Vita** ("compuerta" en el sync). Sin esa espera GT va a 59,9 fps toda la
  carrera; con ella, ~58 fps y 16–20 % de frames lentos. Lo demás (avisos de GePatch, copia del espejo, que la Vita lea la
  memoria del PSP) no se nota.
- GT encola **una lista grande por frame** (50 000–80 000 instrucciones), completa, y pide el sync ~14,3 ms después. La
  Vita empieza casi enseguida pero necesita ~13–16 ms (hasta 20 en lo pesado): ~1/3 de los frames llega tarde, en mediana
  por 1,5 ms. El PSP tiene ~20 % de CPU libre (~3 ms) y absorbe esperas chicas; las de 2–5 ms son las caídas de fps.
- **La lista de un frame no se toca hasta que el juego encola la siguiente** (0 cambios en 11 436 revisiones; en el sync
  siguiente ya cambió el 90 %). GT alterna dos zonas para su lista (`08d004c0` / `08d204c0`).
- Por eso la espera se puede mover a "antes de encolar la lista siguiente": la Vita tendría un frame entero (~16,7 ms).
  Con los tiempos medidos, la Vita quedaría atrasada en el 15 % de los frames en vez del 31 %. Además hay que hacerla más
  rápida: la mitad de su tiempo es revisar y aplicar sublistas guardadas, y pierde ~17 % de su núcleo.

## Corridas (GT UCUS-98632, la misma carrera y auto; logs en `vitabridge/fase1/`)
| Corrida | Versión | Qué | Carpeta |
|---|---|---|---|
| 1 | dev65.1 + v13.9.1 | todo normal | `gt1` |
| 2 | dev65.1 + v13.9.1 | `sin_compuerta` | `gt2_sin_compuerta` |
| 3 | dev65.1 + v13.9.2 | vida de la lista (huellas en el flip) | `gt3_vida_lista` |
| 4 | dev65.2 + v13.9.3 | vida de la lista según GePatch (antes de encolar, en cada sync) | `gt4_vida_gepatch` |
| 5 | dev65.2 + v13.9.4 | desglose del tiempo de la Vita | `gt5_desglose` |
Resumen: `python3 tools/analisis_fase1.py gt1 gt2_sin_compuerta gt3_vida_lista gt4_vida_gepatch gt5_desglose`.

## Resultados [medido] (frames de carrera: más de 30 000 comandos)
| | 1 normal | 2 sin compuerta | 3 | 4 | 5 |
|---|---|---|---|---|---|
| fps | 58,2 | **59,9** | 57,6 | 58,0 | 57,7 |
| frames > 17,5 ms | 15,8 % | **0,2 %** | 20,0 % | 18,6 % | 27,3 % (registro extremo completo) |
| frame p99 | 24,5 ms | 16,9 ms | 24,4 | 24,0 | 23,8 |
| espera a la Vita (por frame) | 0,76 ms | 0 | 0,99 | 0,89 | 1,31 |
| espera al GE dentro del sync | 1,29 ms | 1,25 | 1,23 | 1,17 | 1,14 |
| CPU libre del PSP | 20 % | 19 % | 20 % | 19 % | 19 % |
| espejo (CPU del flip / páginas) | 0,07 ms / 136 | 0,07 / 128 | | | |
| ganchos de GePatch | 6 µs | 6 µs | | | 8 µs |
| Vita: recorrer la lista (mediana, listas atrasadas) | 15,4 ms | 15,0 | 15,9 | 15,7 | 15,4 |
| juego: encolar → sync (mediana) | 14,3 ms | 14,3 | 14,5 | 14,4 | 14,1 |

### La espera (`ge_patch_tarde.log`, corrida 1)
- Listas atrasadas: `listas_antes` = 0 (una lista por frame), `ultimo_stall_hace` = `encolada_hace` (GT la encola
  completa, sin stall), la Vita empieza 0,3 ms después de encolarla (p90 1,6 ms), `trabajo` ≈ `recorrio` (no espera nada:
  es todo trabajo). Atraso: mediana 1,5 ms, p90 5,2 ms; 12 % de las esperas llegan al tope de 5 ms.
- Sin compuerta (corrida 2) la Vita se atrasa sin límite en el tramo pesado (hasta ~30 listas pendientes, atraso p90
  190 ms): la espera es necesaria mientras la Vita no alcance. Los pocos frames lentos de esa corrida son del final de la
  vuelta, con la Vita muy atrasada (falta una corrida `sin_captura` para ver si es GT solo).

### Vida de la lista (corridas 3 y 4)
| Momento (después de capturar la lista N) | Revisadas | Cambió el nivel de arriba | Cambiaron sublistas |
|---|---|---|---|
| drawsync del frame N | 11 414 | 0 | 0 |
| **antes de encolar la lista N+1** | **11 436** | **0** | **0** |
| drawsync del frame N+1 | 2 902 | 2 603 (90 %) | 178 (6 %) |
| flip del frame N+1 (carrera) | 6 831 | 6 808 (99,7 %) | 745 (11 %) |
- Del drawsync al encolado siguiente pasan ~1,0–1,3 ms. Con la Vita terminando a los X ms del encolado: más de 14,3 ms
  (hoy) en 31 % de los frames de carrera; más de 16,7 ms en 15 %; más de 20 ms en 4 % [corrida 4].

### En qué se va el tiempo de la Vita (corrida 5, mediana por frame)
| Comandos por frame | Reloj | Procesador recibido | Revisar + aplicar sublistas | de eso, aplicar | Lecturas propias | De la caché compartida |
|---|---|---|---|---|---|---|
| 30–45 mil | 12,4 ms | 10,9 | 5,6 | 1,4 (214 sublistas) | 321 | 1613 |
| 45–60 mil | 14,6 ms | 12,2 | 7,2 | 1,7 (235) | 406 | 1984 |
| 60–75 mil | 17,1 ms | 13,9 | 8,1 | 1,9 (263) | 446 | 2314 |
| más de 75 mil | 17,6 ms | 14,3 | 8,8 | 2,0 (270) | 467 | 2471 |
- Lectura propia de 256 bytes: **3,0 µs** (0,75 µs por línea). Una línea de la caché compartida (que llenan los lectores):
  **1,13 µs** → leer directo sale más barato por línea que la caché compartida [medido; la caché se paga en sincronizar
  entre núcleos].
- Aplicar una sublista guardada cuesta ~7 µs (sobre todo sumar ~200 contadores de estadística y copiar registros).
- El capturador recibe ~83 % de su núcleo (el 1, compartido con los hilos de audio): ~2,4 ms por frame perdidos.
- Leer la hora cuesta 390 ns: el registro extremo (miles de lecturas por frame) se nota.

## Propuesta para la fase 2 (por orden: ganancia / costo)
1. **Mover la compuerta a "antes de encolar la lista siguiente"** (GePatch): +2,4 ms para la Vita, seguro según la vida
   de la lista. Para otros juegos: comprobarlo solo (revisar unas líneas de la lista anterior antes de encolar, ~10 µs en
   el PSP) y volver al sync si cambian. **Hecho en dev66 + v13.10 (`etapa2_fase2_compuerta.md`): 3 veces menos esperas
   en la carrera de GT, casi ninguna en la mitad de la vuelta.**
2. Capturador: leer directo de a 256 bytes en vez de tomar líneas de la caché compartida (~0,8 ms); los lectores solo
   comprueban sublistas por adelantado. **Probado en v13.11.1 y descartado: casi 3 veces más esperas en GT (el capturador
   aprovecha ~2,3 de las 4 líneas de cada lectura y pierde el paralelismo de los lectores). `etapa2_fase2_estadisticas.md`.**
3. Aplicar sublistas sin sumar todas las estadísticas (solo lo necesario; el resto según el nivel de registro): ~1,5 ms.
   **Hecho en v13.11 (estadísticas compactas): ~2 ms menos de Vita por frame en lo más pesado de GT (19,1 → 17,0 ms),
   54,3 → 57,3 fps en ese tramo. `etapa2_fase2_estadisticas.md`.**
4. Revisar sublistas más barato o por adelantado (hoy ~5,5–6,8 ms): que los lectores lleguen antes a más sublistas.
   **Hecho en v13.12 (por adelantado, desde el final): 57,3 → 58,2 fps en lo más pesado de GT; el capturador sigue
   comparando ~5,5 ms porque leer la memoria del PSP cuesta ~2 µs por línea. Leer más barato queda para el futuro.
   `etapa2_fase2_adelanto.md`.**
5. Núcleo / prioridad del capturador (pierde ~17 %), con cuidado del audio.
6. Pendiente de la fase 1: corrida `sin_captura` para el final de la vuelta; DISTINTAS de las sublistas en la VRAM
   (la memoria compara con el espejo de cuando se guardó, no con el del frame) **→ arregladas en v13.12**.

**Etapa 2 cerrada el 2026-10-29** con GePatch 0.2-dev66 + gepresent v13.12 (decisión del usuario: 58 fps mínimo en lo
más pesado de GT alcanza). Los puntos 5 y 6 quedan en `TODO.md`.

## Versiones
| Versión | Qué | md5 |
|---|---|---|
| dev65.1 + v13.9.1 | EV_FRAME, CPU libre, interruptores, frames/tarde | `bf2ea3d6…` / `c7af2246…` |
| v13.9.2 | vida de la lista (huellas al flip) | `e0b4cf7e…` |
| dev65.2 + v13.9.3 | EV_VIDA (GePatch revisa la muestra antes de encolar y en cada sync) | `cfe999e2…` / `01461f8c…` |
| v13.9.4 | desglose: memoria, aplicar, lecturas, procesador por lista | `012268f3…` |
| dev65.3 + v13.9.5 | niveles de registro (mínimo / mediano / extremo) | `990f6afb…` / `8af10774…` |
| **dev65 + v13.9 (commit)** | = dev65.3 + v13.9.5 con los números finales; confirmada en GT (`gt6_confirmacion`, registro mediano) | `744ab734…` / `9d3439d3…` |
