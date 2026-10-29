# Etapa 2, fase 2 (1): la espera a la Vita pasa a "antes de encolar la lista siguiente" (GePatch dev66 + gepresent v13.10, 2026-10-29)

**Commit:** GePatch **0.2-dev66** + gepresent **v13.10** (= dev66.3 + v13.10.3 con los números finales).

Sigue a `etapa2_fase1_impacto.md` (punto 1 de la propuesta para la fase 2). Marcas: [medido], [deducido].

## En simple
- Antes, el juego esperaba a la Vita al final de cada imagen (en el sync). Ahora espera justo antes de mandar la lista
  siguiente: la Vita gana el rato entre el fin de una imagen y el comienzo de la otra (~2,4 ms en GT).
- Para no romper juegos que reescriben su lista antes de eso, la Vita deja unas "huellas" de cada lista y GePatch las
  revisa antes de mandar la siguiente. Si cambiaron, se vuelve a esperar en el sync.
- Resultado en GT (registro mediano, la misma carrera): **3 veces menos esperas en la carrera** (~2 150 → 690) y en la
  mitad de la vuelta casi ninguna (~1 000 → ~45; 59,7–59,9 fps). El arranque y el final de la vuelta siguen esperando:
  ahí la Vita necesita más de una imagen entera (hasta 20–25 ms), y eso se arregla haciéndola más rápida (puntos 2–5).
- Silent Hill: la revisión no vio cambios (~3 000 revisiones) y usó la espera nueva todo el rato, sin esperas.

## Cómo funciona
- **GePatch** (`main.c`, "dev66 (fase 2)"): en `sceGeDrawSync`/`sceGeListSync` ya no espera; anota la última lista que
  pasó por un sync (`gateSync`). En `sceGeListEnQueue`/`EnQueueHead`, `gateBeforeEnq` espera (hasta 5 ms, como antes,
  con `sbGate`) a que la Vita haya capturado esa lista, y después hace la autocomprobación.
- **Autocomprobación:** gepresent deja la muestra de huellas (16 líneas de 64 B del nivel de arriba y la primera línea de
  hasta 16 sublistas, `lifeTake`) de la lista más grande de cada frame, ahora en todos los niveles de registro, con su
  número de orden en `SB[0x53]`. GePatch rehace las huellas con la caché (lo que ve la CPU del juego) solo si la muestra es
  de una lista encolada después del sync anterior (`sync_lo < orden <= sync_ord`).
- **Modos** (`SB[0x54]`, en el log de GePatch "stats compuerta" y en la línea "impacto en el PSP" de gepresent):
  0 = en el sync, probando (al abrir el juego y después de un cambio); 1 = antes de encolar (tras 60 comprobaciones
  limpias seguidas); 2 = en el sync hasta cerrar el juego (10 cambios).
- Interruptor `ux0:data/gepresent/compuerta_sync` (`SB[0x4A] & 8`): siempre en el sync, como antes (las comprobaciones
  siguen). Se apaga con `no` adentro.
- `EV_ENQ`/`EV_HEAD` llevan en el dato la espera antes de encolar (us | 1); gepresent la suma (`eg_n`, `eg_us`) y en
  extremo la anota en `ge_patch_tarde.log` con tipo `e`.

## Versiones de prueba [medido]
| Versión | Qué | Resultado | Carpeta |
|---|---|---|---|
| dev66.1 + v13.10.1 | compuerta antes de encolar; un cambio la deja en el sync hasta cerrar el juego | menús bien; en la pantalla de carga antes de la carrera "cambió" la lista 4613 → toda la carrera en el sync | `etapa2_dev66.1/gt1` |
| dev66.2 + v13.10.2 | un cambio vuelve a "probando" (60 limpias), 10 cambios = hasta cerrar | 10 "cambios" en la carrera, todos en frames con espera → en el sync desde el s 91 | `etapa2_dev66.2/gt1` |
| **dev66.3 + v13.10.3** | gepresent deja la muestra antes de marcar la lista capturada; GePatch solo acepta la muestra de una lista posterior al sync anterior | **0 cambios en toda la corrida**; Silent Hill igual | `etapa2_dev66.3/gt1`, `sh1` |

**Falsa alarma de dev66.1/dev66.2 [deducido]:** gepresent marcaba la lista como capturada (`SB[3]`) y recién después
dejaba su muestra. Cuando la compuerta soltaba al juego justo en ese momento, GePatch revisaba la muestra de la lista
anterior, cuya zona GT ya está reescribiendo (alterna dos zonas para sus listas): parecía un cambio. Por eso solo
aparecía en frames con espera (nunca en los menús). La primera "falsa alarma" de dev66.1 (pantalla de carga) es del
mismo tipo: con dev66.3 la carga pasó sin cambios.

## Resultados en GT (dev66.3, registro mediano; comparación con dev66.1, que hizo la carrera con la espera en el sync)
| Tramo (s del log de GePatch) | fps dev66.1 | fps dev66.3 | esperas cada 10 s dev66.1 → dev66.3 | espera por frame (µs) |
|---|---|---|---|---|
| 42–48 (arranque) | 56,2 | 56,5 | 143 → 63 | 451 → 367 |
| 48–54 | 56,3 | 59,2 | 525 → 229 | 1 861 → 624 |
| 54–60 | 59,7 | 59,7 | 245 → 51 | 545 → 166 |
| 60–102 (mitad) | 59,3–59,9 | **59,8–59,9** | 1 031 → 47 | 116–441 → 3–74 |
| 102–108 (final) | 56,0 | 54,7 | 281 → 300 | 1 283 → 1 495 |
- El final de la vuelta (54,7 fps) es el tramo conocido donde la Vita tarda 20–25 ms por frame (`TODO.md`): mover la
  espera no alcanza; con una sola corrida no se sabe si la baja contra 56,0 es real o variación.
- Memoria de sublistas: 1 DISTINTA en 50 536 revisadas (lo de siempre: sublistas en la VRAM).
- Costo de la autocomprobación: no se nota en los ganchos (3 µs por frame en el log de GePatch).

## Siguiente (resto de la propuesta de la fase 2)
2. Capturador: leer directo de a 256 bytes en vez de la caché compartida (~0,8 ms).
3. Aplicar sublistas sin sumar todas las estadísticas (~1,5 ms).
4. Revisar sublistas más barato o por adelantado.
5. Núcleo / prioridad del capturador (con cuidado del audio).
6. DISTINTAS de las sublistas en la VRAM; corrida `sin_captura` para el final de la vuelta.

## Versiones
| Versión | md5 GePatch / gepresent |
|---|---|
| dev66.1 + v13.10.1 | `4e756743…` / `f80f245a…` |
| dev66.2 + v13.10.2 | `3ac67b21…` / `c79cdae4…` |
| dev66.3 + v13.10.3 | `d0854495…` / `d4504573…` |
| **dev66 + v13.10 (commit)** | `16755a0c…` / `5f08b9d1…` |
