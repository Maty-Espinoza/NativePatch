# Etapa 2: comprobación de sublistas por los lectores (gepresent v13.4.1–v13.4.2, 2026-10-28)

Sigue a `etapa2_espejo_vram.md` (GePatch dev62 + gepresent v13.3). Marcas: [medido], [deducido].
**Commit:** GePatch 0.2-dev62 (sin cambios) + **gepresent v13.4** (= v13.4.2 con el número de versión final; mismo código).

## En simple
- La Vita guarda las sublistas que GT repite en cada cuadro y, antes de reusarlas, comprueba que no cambiaron. En el
  puente esa comprobación era ~9 de los ~18 ms por cuadro, y lo caro no era comparar: era **leer** la memoria del PSP
  (lenta para la Vita: ~6,5 µs cada 256 bytes).
- Los dos ayudantes (lectores, núcleos 0 y 2, con el tiempo que sobra) recorrían instrucción por instrucción justo esas
  sublistas, que el capturador ya se saltaba.
- **Ahora:** al llegar a una sublista guardada, el ayudante compara él sus líneas y deja una marca "comprobada en esta
  lista". El capturador, si encuentra la marca, la usa sin leer nada; si no, compara como antes (nunca peor que v13.3).
  Cada ayudante toma la siguiente sublista libre (no la mitad fija) y se salta las que el capturador ya pasó.
- **Resultado:** el puente de GT a 57–60 fps (v13.3: ~52), el comienzo de la carrera a 59,4 (56–58). SH igual que antes.

## Cómo funciona (tools/gepresent/captura.c)
- `Memo` tiene al final `ver` (sube en `recBegin` antes de tocar nada; `recBegin` no borra esos dos campos) y `vok`
  (`vokOf(ord, ver)` = número de lista << 16 | versión). `recEnd` pone una barrera antes de `state = 1`.
- `memoVerify` (lector): en un CALL del nivel de arriba busca las versiones guardadas (hasta 8 con la misma clave,
  `state = 1`, `pgen` actual) y compara todas sus líneas con el depósito (`rdc` + `lineEq`; las líneas quedan también en la
  caché compartida). Cerca del stall no comprueba (lo ve el capturador). Si al final `ver` o `pool_gen` cambiaron (el
  capturador la regrabó o vació el depósito), se descarta. Igual → `vok`; distinta → `vok = 0`.
- `memoTry` (capturador): después de `depsOk` y `nearStall`, si `vok == vokOf(l->ord, ver)` aplica sin comparar
  (`memo_pre`). La revisión de 1 de cada 32 sigue igual (recorre y compara el estado).
- Reparto (`claim[2048]`, v13.4.2): el n-ésimo CALL del nivel de arriba de la lista lo toma el primer lector que llega
  (`claimTake`, CAS con número de lista << 2 | quién); el capturador marca los que alcanzó (`claimSet`) y un lector que
  llega después lo cuenta como `tarde` y se lo salta. Después del CALL 2048, uno sí y uno no como antes.
- El lector quedó en funciones (`Lec`, `lecStart`, `lecWalk`) para la prueba fuera de la Vita.
- Log, línea de la memoria: `aplicadas= (comprobadas_por_lectores=)` y `| lectores/f: lineas= comprobadas= no_iguales=
  tarde=`.
- Prueba: `prueba_memoria.c <modo> <vram> <lectores>`; lectores 1 = pasan antes que el capturador (mismas columnas que sin
  memoria, el capturador compara 0 líneas, el lector detecta la sublista que cambia en el frame 3); 2 = pasan después
  (se saltan todo, 0 líneas leídas). Probado con vram 0/1/2 y modos 1/2: iguales, 0 distintas.

## Versiones
| Versión | Qué | md5 |
|---|---|---|
| v13.4.1 | comprobación por los lectores (reparto uno sí, uno no) | `ccc116f0…` |
| v13.4.2 | reparto según el ritmo + saltar lo que el capturador ya pasó | `0ebc04d7…` |
| v13.4 (commit) | = v13.4.2 con el número de versión final (código idéntico) | `dc016491…` |

## Resultados [medido]
Logs: `vitabridge/etapa2_v13.4.1/gt1/`, `etapa2_v13.4.1/sh1/`, `etapa2_v13.4.2/gt1/`.

### GT (UCUS-98632), una vuelta con el puente (fps de GePatch cada 10 s)
| | v13.3 | v13.4.1 | v13.4.2 |
|---|---|---|---|
| comienzo de la carrera | 57,2 | 59,0 | 59,4 |
| último tramo (puente) | 53,6 | 57,3 | 58,4 |
| frames por seg en los segundos más pesados (~82 000 comandos) | 51–52 | 55–59 | 57–60 |
| captura, ms por frame en el puente | ~18,3 | ~16,6 | ~15,5 |
| comparar_us por frame / líneas que compara el capturador | 8 400–9 200 / ~4 050 | 6 500–7 200 / ~3 000 | 5 700–6 500 / ~2 750 |
| sublistas aplicadas / comprobadas por los lectores (por frame) | ~280 / – | ~277 / ~85 | ~277 / ~110 |
| revisadas / DISTINTAS | – / 0 | 51 102 / 0 | 52 990 / 0 |
- Los lectores reciben ~140 (núcleo 0) y ~280 (núcleo 2) ms de procesador por seg; con v13.4.1 el del núcleo 0 llegaba
  tarde a su mitad fija. Con v13.4.2 ~110 de ~277 sublistas llegan comprobadas; en ~107 por frame los dos lectores llegan
  después del capturador (`tarde`, ~215 por frame contando los dos). **No les alcanza el tiempo** para ir adelante.
- Lecturas propias del capturador: ~860 → ~570 por frame.

### SH (ULUS-10450), con v13.4.1
- 26–60 fps según la escena (como antes); casi no usa la memoria de sublistas (aplicadas ~0); 0 distintas; ~5,6 ms de
  captura por frame de media.

## Lo que falta / ideas
1. **Comprobar al empezar cada cuadro** (sin esperar a que el juego llegue al CALL): los lectores revisarían en cuanto
   empieza la lista las sublistas usadas en la anterior. Daría más tiempo, pero si el juego cambia una sublista después de
   revisarla ese cuadro usaría la vieja (la revisión 1/32 lo vería). Solo para sublistas estables hace muchos cuadros.
   Decidido dejarlo para después (usuario, 2026-10-28).
2. Leer menos de la RAM del PSP: lo caro es la lectura sin caché (~6,5 µs cada 256 bytes); cualquier forma de no leer
   (huellas calculadas en otro lado, una vista con caché) rinde más que comparar más rápido.
3. SH: marcar destinos de TRANSFER (sigue pendiente de `etapa2_espejo_vram.md`).
4. T2 (caja negra), T4, T6, grabación GXM en varios hilos → etapa 3 (dibujar).
