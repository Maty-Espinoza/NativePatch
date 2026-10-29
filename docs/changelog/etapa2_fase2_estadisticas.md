# Etapa 2, fase 2 (2 y 3): lectura directa del capturador (descartada) y estadísticas compactas (gepresent v13.11, 2026-10-29)

**Commit:** gepresent **v13.11** (= v13.11.2 con el número final), con GePatch **0.2-dev66** sin cambios.

Sigue a `etapa2_fase2_compuerta.md` (punto 1). Puntos 2 y 3 de la propuesta de `etapa2_fase1_impacto.md`.
Marcas: [medido], [deducido].

## En simple
- **Punto 2 (descartado):** que el capturador de la Vita lea la memoria del PSP él mismo, de a 256 bytes, en vez de tomar
  las líneas que le dejan los lectores en la caché compartida. Salió peor: en GT casi 3 veces más esperas del juego en la
  carrera. El capturador salta mucho entre zonas y aprovecha poco de cada lectura; los lectores, en cambio, hacen esas
  esperas en paralelo en otros núcleos. **La caché compartida se queda.**
- **Punto 3 (hecho):** cada sublista guardada sumaba las 127 estadísticas del capturador al aplicarse, casi todas en 0.
  Ahora guarda solo las que no son 0 y suma solo esas. En los tramos más pesados de GT la Vita tarda ~2 ms menos por
  frame (19,1 → 17,0 ms con más de 75 000 comandos) y el juego pasa de 54,3 a 57,3 fps ahí. Los registros quedan iguales.

## Punto 2: v13.11.1, el capturador lee directo (descartado)
- Cambio: en `rdc`, el capturador (`R0`) no usaba `clGet`/`clPut`; los lectores seguían con la caché entre ellos.
  Interruptor `cache_compartida` para volver a lo anterior (no quedó en el código).
- Supuesto de la fase 1: línea de la caché compartida 1,13 µs contra 0,75 µs por línea leyendo 256 B (3,0 µs).
- [medido] En GT: lecturas propias del capturador ~500 → ~1 500–1 700 por frame para reemplazar ~2 600 líneas de la caché
  → aprovecha ~2,3 de las 4 líneas de cada lectura (~1,3 µs por línea útil, más que la caché).
- Resultado (`etapa2_v13.11.1/gt2`, registro mediano):

| Comandos por frame | Vita por frame dev66.3 | v13.11.1 | fps dev66.3 | v13.11.1 |
|---|---|---|---|---|
| 30–45 mil | 10,3 ms | 11,6 | 60,0 | 60,0 |
| 45–60 mil | 12,4 ms | 14,5 | 60,1 | 59,9 |
| 60–75 mil | 15,6 ms | 17,8 | 59,2 | 56,5 |
| más de 75 mil | 19,1 ms | 19,5 | 54,3 | 52,5 |
- Esperas del juego a la Vita en la carrera: ~690 → ~2 040.
- **La Vita se colgó** en la primera corrida con v13.11.1 (`etapa2_v13.11.1/gt1`): los logs se cortan ~10 s después de
  largar, sin errores, y vitacompanion dejó de responder hasta que el usuario la reinició. La segunda corrida con la misma
  versión terminó bien: no se sabe si fue la versión. Vigilar si vuelve a pasar.
- Si se quiere abaratar la caché compartida más adelante: sacar la línea con NEON y menos barreras (`clGet` hace dos
  `__sync_synchronize`). Ganancia incierta (décimas de ms).

## Punto 3: estadísticas compactas en la memoria de sublistas (v13.11)
- Antes: `Memo.d[SW]` (127 palabras, 508 B por sublista) con la diferencia de todas las estadísticas; aplicar = sumar las
  127 y leer esa memoria (en GT ~270 sublistas por frame, ~650 KB de entradas: casi todo fuera de la caché). [deducido de
  la fase 1: ~7 µs por sublista aplicada]
- Ahora (`recEnd`): `Memo.cmds`, `Memo.draws` y hasta `SN` = 48 pares (índice `si[]`, valor `sv[]`) de las estadísticas
  distintas de 0. Si una sublista suma más de 48, no se guarda (queda como `sin_guardar`) y el log lo avisa una vez
  ("memoria: sublista ... suma mas de 48 estadisticas"). En GT no pasó ninguna vez.
- Aplicar (`memoTry`): suma solo esos pares. En el nivel **mínimo** solo `cmds` y `draws` (lo que sale en su línea).
- Resultado (`etapa2_v13.11.2/gt1`, registro mediano, la misma carrera; mismo cálculo que la tabla de arriba):

| Comandos por frame | Vita por frame dev66.3 | v13.11.2 | ns por comando | fps dev66.3 | v13.11.2 |
|---|---|---|---|---|---|
| 30–45 mil | 10,3 ms | 10,3 | 256 → 253 | 60,0 | 60,0 |
| 45–60 mil | 12,4 ms | 12,1 | 241 → 234 | 60,1 | 60,0 |
| 60–75 mil | 15,6 ms | **14,9** | 228 → 218 | 59,2 | **60,0** |
| más de 75 mil | 19,1 ms | **17,0** | 239 → 214 | 54,3 | **57,3** |
- Esperas del juego (cada 10 s, desde la largada): dev66.3 63 / 229 / 51 / 5 / 9 / 9 / 8 / 9 / 2 / 5 / 300;
  v13.11.2 12 / 188 / 68 / 9 / 6 / 9 / 5 / 2 / 12 / 7 / 29 / 413. El final de la vuelta sigue esperando: ahí la Vita
  necesita más de 16,7 ms (17 ms de mediana con más de 75 mil comandos).
- Memoria de sublistas: 3 DISTINTAS en 51 121 revisadas, todas sublistas en la VRAM (lo conocido de `TODO.md`; el
  cambio no toca el estado del GE, solo los contadores).
- Imagen y audio: sin cambios según el usuario.

## Cómo medir esto (sin el registro extremo)
Con `ge_patch_captura.log` de cada corrida, promedio por tramo de comandos (las líneas `seg:` de más de 30 000):
`grep "seg: frames" ge_patch_captura.log | awk '{for(i=1;i<=NF;i++){split($i,a,"=");if(a[1]=="comandos/f")c=a[2];
if(a[1]=="us/f")u=a[2]}} c>=30000{b=int(c/15000);if(b>5)b=5;N[b]++;U[b]+=u} END{for(b=2;b<=5;b++)print b*15"k",U[b]/N[b]}'`

## Siguiente (resto de la propuesta de la fase 2)
4. Revisar sublistas más barato o por adelantado (hoy ~5–6 ms por frame en la carrera: `comparar_us`).
5. Núcleo / prioridad del capturador (pierde ~17 %), con cuidado del audio.
6. DISTINTAS de las sublistas en la VRAM; corrida `sin_captura` para el final de la vuelta.

## Versiones
| Versión | Qué | md5 gepresent |
|---|---|---|
| v13.11.1 | el capturador lee directo (punto 2) — descartada | `16cc1ca4…` |
| v13.11.2 | estadísticas compactas (punto 3) | `847f4a36…` |
| **v13.11 (commit)** | = v13.11.2 con el número final | `2149515e…` |
Parches en `vitabridge/gepresent_v13.11.1.patch` y `gepresent_v13.11.2.patch`; logs en `vitabridge/etapa2_v13.11.1/`
(`gt1` = el cuelgue, `gt2`) y `etapa2_v13.11.2/gt1`.
