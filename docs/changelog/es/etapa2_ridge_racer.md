# Ridge Racer a 60 fps con la captura: interpretar de a línea y capturador en cualquier núcleo (gepresent v13.13, 2026-10-29)

**Commit:** gepresent **v13.13** (= v13.13.2 con el número final y el núcleo libre por defecto), GePatch **0.2-dev66** sin
cambios. Después del cierre de la etapa 2 (`etapa2_fase2_adelanto.md`), probando otros juegos. Marcas: [medido], [deducido].

## En simple
- **Ridge Racer bajaba a ~30 fps** en el mismo lugar de cada vuelta: una curva antes de una zona llena de público. Ahí
  cada frame tiene ~20 000 instrucciones y ~3 700 dibujos (el doble de lo normal; el público son muchas piezas chicas).
  El PSP tenía ~55–60 % de procesador libre: no era el juego, era la espera a la Vita.
- Ridge Racer **no repite sublistas**, así que la memoria de sublistas no ayuda (se salta ~1 % de las instrucciones;
  en GT ~85 %): la Vita interpreta todo, ~1 µs por instrucción. Además el capturador compartía su núcleo con el sonido
  del juego y recibía solo ~65 % de él.
- **Dos cambios:** 1) interpretar de a línea de 64 bytes (16 instrucciones): las comprobaciones que se hacían en cada
  instrucción ahora se hacen una vez por línea (~1 070 → ~800 ns por instrucción); 2) el capturador puede correr en
  cualquiera de los 3 núcleos, no fijo en el del sonido (recibe ~100 %).
- **Resultado:** Ridge Racer a 60 fps toda la carrera (1 segundo a 55); GT también mejoró en lo más pesado (58,2 → 60,2).
  Sin cortes de audio ni problemas de imagen (pruebas del usuario), 0 DISTINTAS.

## Por qué bajaba (`vitabridge/etapa2_v13.12/rr1`) [medido]
| Comandos por frame | Segundos | fps | Draws | Vita por frame |
|---|---|---|---|---|
| 3–9 mil | 101 | 59,9 | ~850 | 8,1 ms |
| 9–12 mil | 111 | 59,5 | ~1 250 | 10,9 ms |
| 12–15 mil | 56 | 57,7 | ~1 700 | 13,7 ms |
| 15–17 mil | 18 | 39,8 | ~2 300 | 16,8 ms |
| más de 17 mil | 23 | 30,8 | ~3 300 | 19,9 ms |
- Memoria de sublistas: ~3 aplicadas por frame, 0–1 % de los comandos sin recorrer.
- RR reescribe 16 líneas de su lista antes de encolar la siguiente: la compuerta de dev66 vuelve al sync (correcto: si
  no, la Vita podría leer la lista a medio cambiar) y la Vita tiene menos margen. La espera llega al tope de 5 ms.
- RR sincroniza cada imagen con la pantalla: si el frame no llega a tiempo baja directo de 60 a 30.
- Capturador: ~65 % del núcleo 1 (el audio de RR usa ~30 % de ese núcleo; en GT ~19 %).
- En el código generado, `walkList` estaba metida dentro de `walk`: el compilador se quedaba sin registros y en cada
  comando recalculaba la dirección de la lista (`&q[...]`) y guardaba y leía valores de la pila.

## Cómo funciona (tools/gepresent/captura.c)
### v13.13.1: de a línea
- `walkList` es una función aparte (`noinline`).
- La traducción de la dirección (`vtr`) se hace una vez por línea de 64 bytes (`lpc`/`lra`).
- Por línea: el stall (limita cuántos comandos quedan, `k`), la lectura (`rdc`), el tope `LIST_MAX`, `recLine` y `lo`.
  Dentro de la línea se sigue con `op = R0.buf[++i]` mientras: queden comandos (`k`), el recorrido sea seguido
  (`l->pc == pc + 4`), no cambie el nivel (`l->sp`) y el búfer de `R0` sea el mismo (`memoTry` lo reemplaza al comparar).
- `draw`: el tamaño del vértice se recuerda mientras no cambie el formato (`vs_vt`), y el muestreo de la vida de los datos
  (`smpAdd`) solo se llama con registro extremo.
- Prueba de qemu (`prueba_memoria.c`): resultados idénticos a v13.12 en modos 0/1/2 × vram 0/1/2/3 × lectores 0/1/2/3 y
  con NIVEL=0/1 y VIDA=1.

### v13.13.2 / v13.13: el capturador en cualquier núcleo
- `sceKernelCreateThread` del capturador con afinidad 0x70000 (núcleos 0, 1 y 2) en vez de 0x20000 (solo el 1), misma
  prioridad (0x61). Interruptor `ux0:data/gepresent/nucleo_captura`: "0", "1" o "2" lo fija a ese núcleo; sin archivo o
  "libre", cualquiera. En v13.13.2 el defecto todavía era el 1; v13.13 deja el libre por defecto (probado en RR y GT).
- Los lectores siguen fijos (núcleos 0 y 2, prioridad 0xB0): reciben menos tiempo (GT: ~265–430 → ~110–130 ms por segundo
  entre los dos; ~110–140 → ~50–70 sublistas por frame llegan comprobadas), pero el capturador con un núcleo entero compensa de sobra.

## Versiones
| Versión | Qué | md5 |
|---|---|---|
| v13.13.1 | de a línea + `noinline` + tamaño de vértice recordado | `186d4414…` |
| v13.13.2 | + interruptor `nucleo_captura` (probado con "libre") | `3be2a248…` |
| v13.13 (commit) | = v13.13.2 con el núcleo libre por defecto | `8c6adb31…` |

## Resultados [medido]
### Ridge Racer (ULJS-00001), la misma carrera (`etapa2_v13.12/rr1`, `etapa2_v13.13.1/rr1`, `etapa2_v13.13.2/rr1`)
| Comandos por frame | | v13.12 | v13.13.1 | v13.13.2 (libre) |
|---|---|---|---|---|
| 3–9 mil | fps / Vita por frame | 59,9 / 8,1 ms | 60,0 / 6,7 | 59,9 / 4,6 |
| 12–15 mil | fps / Vita por frame | 57,7 / 13,7 ms | 59,6 / 10,7 | 60,0 / 7,6 |
| 15–17 mil | fps / Vita por frame | 39,8 / 16,8 ms | 58,3 / 12,8 | 60,0 / 9,2 |
| más de 17 mil | fps / Vita por frame | 30,8 / 19,9 ms | 49,9 / 15,1 | **60,0 / 10,4** |
| todo | ns por comando (reloj) | ~1 070 | ~800 | ~580 |
| todo | procesador del capturador | ~65 % | ~65 % | ~100 % |
| carrera | segundos bajo 58 fps | 56 | 17 | **1** (55 fps) |
- En el tramo del público con v13.13.1: el PSP con ~55–60 % de CPU libre, ~120 texturas (como en el resto de la pista);
  solo crecen los draws. No hay un pico de carga del juego: es una carga pareja mientras el público está en pantalla.
- 0 DISTINTAS en las tres corridas.

### Gran Turismo (UCUS-98632), la misma carrera (`etapa2_v13.12.2/gt1` contra `etapa2_v13.13.2/gt1`, con "libre")
| Comandos por frame | fps / Vita por frame v13.12 | v13.13.2 |
|---|---|---|
| 30–45 mil | 60,0 / 10,1 ms | 60,0 / 7,8 |
| 45–60 mil | 59,9 / 12,8 ms | 60,0 / 10,0 |
| 60–75 mil | 59,9 / 14,9 ms | 59,9 / 12,0 |
| más de 75 mil | 58,2 / 16,7 ms | **60,2 / 13,4** |
- Segundos de carrera bajo 58 fps: 6 → 0. Espera a la Vita en lo más pesado: 0,91 → 0,08 ms por frame. 0 DISTINTAS.
- Comparar sublistas sube un poco (5,6 → 6,8 ms por frame en lo más pesado) porque los lectores llegan a menos, pero el
  total baja.

### LocoRoco (NPEG-00024) con v13.12 (`etapa2_v13.12/locoroco1`)
- 59,9 fps casi siempre; ~4–8 ms de Vita por frame; 0 DISTINTAS en 65 330. Cambia una sublista antes de encolar la
  siguiente: la compuerta se queda en el sync (correcto).

## Lo que queda (en `TODO.md`)
- Por qué RR reescribe 16 líneas de su lista antes de encolar la siguiente: si es algo que la Vita ya leyó, se podría
  usar la compuerta antes de encolar y darle más margen.
- Cuando la Vita dibuje (etapa 3), RR va a necesitar el margen que ganó aquí: ~10 ms de captura en lo más pesado.
- Probar LocoRoco y Silent Hill con v13.13 (el núcleo libre no se probó en ellos).
