# Etapa 2: grabar en GXM desde varios núcleos (gepresent v13.7, 2026-10-29)

Sigue a `etapa2_espejo_doble.md`. Punto de la etapa 2 (`arquitectura_dibujo_en_vita.md` §6): "repetir la medición de
grabación en 2–3 hilos con afinidad fija" (en la etapa 1, 2 hilos sin núcleo fijo no escalaron). Marcas: [medido],
[deducido]. **Commit:** gepresent **v13.7** (= v13.7.1 con el número final); GePatch sigue en **0.2-dev64**.

## En simple
- Para dibujar en HD, la Vita tendrá que "grabar" cada dibujo del juego en el formato de su tarjeta gráfica (GXM).
  GT tiene ~9000 dibujos por cuadro (hasta 16 500). La prueba grabó 9000 dibujos de práctica mientras GT corría, con 1, 2
  o 3 hilos en distintos núcleos del procesador.
- **Grabar sí se puede repartir** entre núcleos (no hay un bloqueo interno), **pero no queda procesador libre**: en
  carrera el núcleo 1 lo usa casi entero la lectura del cuadro (captura) y el núcleo 2 la mitad el sonido del PSP. Lo
  mejor que se logró fue ~26 ms para 9000 dibujos (60 fps son 16,7 ms por cuadro).
- La tarjeta gráfica también tarda: ~16 ms para 9000 dibujos chicos.
- **Conclusión:** GT no puede ir a 60 fps grabando cada dibujo por separado. Hay que grabar menos: juntar dibujos y/o
  grabar una vez los trozos de lista que se repiten (en GT el 85 % de las instrucciones vienen de sublistas que no
  cambian) y reusar esas grabaciones. SH, RR y los juegos livianos (~300–1400 dibujos) sí tienen margen.

## Cómo funciona (tools/gepresent/banco.c, "banco de nucleos")
- Se activa con `ux0:data/gepresent/banco_nucleos` (contenido: segundos de espera, 30 si está vacío; **"no" lo apaga**,
  porque el relevo no puede borrar archivos). Con el archivo activo gepresent no reserva el anillo de volcados.
- Una grabación = 9000 draws del caso T del banco de la etapa 1 (textura + uniformes por draw, el más parecido a GT),
  repartidos entre 1, 2 o 3 hilos grabadores (prioridad 0xA0: debajo del capturador 0x61 y encima de los lectores 0xB0),
  cada uno con su contexto diferido. Las listas se ejecutan en la escena del hilo de Adrenaline (comprueba que sirven).
- 10 configuraciones: 1 hilo sin núcleo fijo / en el 0 / en el 1 / en el 2; 2 hilos sin fijo / 0+2 / 1+2 / 0+1; 3 hilos
  sin fijo / 0+1+2. Una ronda = cada una 3 veces (un frame cada una); 20 rondas cada 15 s.
- El contexto diferido se crea de nuevo antes de cada grabación sobre su pozo vacío (la GPU ya terminó con lo anterior):
  así el pozo se reusa sin el problema de v11.2. Pozos de 5 + 3 + 3 MB (9000 draws T = 3,2 MB).
- Log `ux0:pspemu/ge_patch_banco.log`: por grabación, reloj total, us/draw, por hilo reloj/procesador (`runClocks`),
  demora en despertar, ejecutar y GPU. `tools/resumen_corrida.py` lo resume por configuración.
- Efecto visible: durante cada ronda (~1 s) Adrenaline espera a la GPU y la imagen se traba; **el juego no se frena**
  (en los segundos de cada ronda la captura ve 60 frames, igual que antes).

## Resultados [medido] (GT, modo sombra, GePatch dev64; log `vitabridge/etapa2_v13.7.1/gt1`)
Rondas 1–3 en menús, 4–20 en carrera (17 × 3 = 51 grabaciones por configuración). Sin errores ni caídas de la GPU.

| Configuración (carrera) | Reloj (mediana) | µs/draw | Procesador total | Hilo más cargado |
|---|---|---|---|---|
| 1 hilo sin núcleo fijo | 31,8 ms | 3,54 | 23,8 ms | 23,8 ms |
| 1 hilo en el núcleo 0 | **27,6 ms** | 3,07 | 23,9 ms | 23,9 ms |
| 1 hilo en el núcleo 1 | 169 ms | 18,8 | 25,9 ms | 25,9 ms |
| 1 hilo en el núcleo 2 | 58,7 ms | 6,52 | 23,9 ms | 23,9 ms |
| 2 hilos sin núcleo fijo | **25,9 ms** | 2,88 | 28,0 ms | 14,4 ms |
| 2 hilos núcleos 0+2 | 38,9 ms | 4,32 | 24,3 ms | 13,1 ms |
| 2 hilos núcleos 1+2 | 86 ms | 9,6 | 24,9 ms | 13,7 ms |
| 2 hilos núcleos 0+1 | 89 ms | 9,9 | 25,5 ms | 13,6 ms |
| 3 hilos sin núcleo fijo | **25,8 ms** | 2,86 | 27,6 ms | 10,3 ms |
| 3 hilos núcleos 0+1+2 | 66 ms | 7,3 | 26,9 ms | 9,8 ms |

- **Trabajo de grabar:** ~2,65 µs de procesador por draw T (igual que la etapa 1: 3,3 µs de reloj en un hilo
  libre). Repartido no se encarece mucho (2,7–3,1 µs/draw sumando los hilos).
- **No hay bloqueo entre hilos:** con 0+2, el hilo del núcleo 0 termina su mitad en ~13 ms (≈ su procesador) mientras
  el del núcleo 2 sigue; si GXM obligara a grabar de a uno, el primero también esperaría.
- **Procesador libre en carrera** (para prioridad 0xA0) [medido con la línea `procesador` de la captura + tabla]:
  núcleo 0 ~85–95 % (emulador ~40 ms/s; el lector 0 tiene menos prioridad), núcleo 1 ~10–15 % (captura ~650–770 ms/s
  + audio y emulador ~200 ms/s), núcleo 2 ~40–50 % (Media Engine ~500–550 ms/s). En total ~1,5 núcleos.
- En menús el núcleo 0 está más ocupado (emulador) y el 1 más libre (la captura casi no trabaja).
- **GPU:** 15,5–16 ms para los 9000 draws (≈1,75 µs por draw de 16x16 con textura y uniformes nuevos), igual en todas
  las configuraciones. El tramo pesado de GT (16 500 draws) serían ~29 ms solo de GPU.
- Demora en despertar un hilo: ~50–100 µs (a veces 2–6 ms en el núcleo 0 u otro ocupado).

### La misma corrida, del lado de la captura (sin relación con el banco)
- fps de GT: 59,5–60 casi toda la carrera; bajan a 55–58 al comenzar la carrera, **53–57 en el tramo pesado** (~80 000
  comandos y 16 500 draws por frame, ARM de la captura ~16–17 ms por frame) y **40–48 en los últimos ~7 s** (ARM ~20–25
  ms por frame con solo ~12 000 draws: aparecen ~300 RECTANGLES y la memoria de sublistas se salta 70 % de los comandos
  en vez de 85 %, con ~50 sublistas por frame entrando con otro estado, `otra_entrada`). Lo mismo, sin el banco, en la
  corrida de confirmación de dev64 (`etapa2_espejo_doble.md`).
- DISTINTAS: 13 de 134 312 revisadas (≈0,01 %).

## Lectura
1. **Grabar draw por draw no entra para GT**, ni repartido: necesita ~24 ms de procesador por cada 9000 draws (≈44 ms en
   el tramo pesado) y quedan ~1,5 núcleos libres que además comparte la captura. Confirma con el juego real la
   advertencia de la etapa 1 (§5 de la arquitectura).
2. **La GPU tampoco** con un draw por cada draw del GE (≈1,75 µs cada uno).
3. Repartir sirve, pero solo entre núcleos con procesador libre: el traductor debería ir en el núcleo 0 (y lo que
   sobre del 2), no en el 1 junto a la captura. 2 hilos sin núcleo fijo rinden lo mismo que 3: el planificador los
   reparte bien.
4. **Lo que sí podría bajar el costo** [deducido, sin probar]:
   - *Grabar una vez cada sublista estable y reusar la lista grabada* (GXM ejecuta una misma lista grabada muchas veces;
     ejecutar 2000 draws grabados cuesta ~30 µs en el hilo de Adrenaline, etapa 1). En GT ~85 % de los comandos vienen
     de sublistas que la captura ya reconoce como iguales a las del frame anterior. Problema a resolver: el estado de
     entrada (matrices, texturas) que la sublista hereda de quien la llama.
   - *Juntar draws* con el mismo estado (etapa 1: 2000 cuadros en 1 draw = 0,02 µs por cuadro y la GPU a la mitad).
   - Bajar el costo de la captura misma en los tramos pesados (`TODO.md`, rendimiento de la captura).
   Esto adelanta parte de la etapa 6 (caché de sublistas traducidas, juntar draws): para GT tiene que existir desde el
   principio. SH/RR pueden seguir el orden previsto.

## Versiones
| Versión | Qué | md5 gepresent |
|---|---|---|
| v13.7.1 | banco de núcleos (compilado en el contenedor de Claude) | `1ef945bc…` |
| v13.7 (commit) | = v13.7.1 con el número final; queda en la Vita con `banco_nucleos` = "no" | `4a73f210…` |
GePatch en la Vita: dev64 (`32742693…`, del Mac). Desde esta etapa también se compila en el contenedor de Claude
(`tools/contenedor/`, ver `FLUJO_DE_TRABAJO.md`).
