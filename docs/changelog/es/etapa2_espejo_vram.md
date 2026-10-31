# Etapa 2: espejo de la VRAM (GePatch dev62 + gepresent v13.1–v13.3, 2026-10-28)

Sigue a `etapa2_memoria_sublistas.md` (memoria de sublistas, v12.13). Marcas: [medido], [deducido].
**Commit:** GePatch **0.2-dev62** (pruebas dev62.1) + **gepresent v13.3**.

## En simple
- La Vita no puede leer la memoria de video del PSP (la VRAM), y Gran Turismo guarda ahí parte de sus instrucciones de
  dibujo, texturas y paletas. Hasta v12.13 la Vita se saltaba esas instrucciones: su lectura de GT estaba incompleta.
- **Espejo:** la Vita le pide al PSP los trozos de 4 KB de la VRAM que necesita. En cada cambio de cuadro el pintor del PSP
  (el GE) copia esos trozos a una zona de memoria normal (0x0A200000) que la Vita sí puede leer. Las instrucciones se copian
  en cada cuadro; las texturas y paletas la primera vez y después de a poco, por turnos.
- **Resultado:** la Vita ya lee GT completo (100–140 sublistas por cuadro desde el espejo, ninguna saltada). GT va a
  59,4–59,9 fps casi toda la vuelta; el comienzo de la carrera a 56–58 y el puente a ~52 (antes ~56, pero leyendo menos).
  SH va igual que antes. Audio e imagen bien (usuario). La imagen sigue siendo la nativa: la Vita todavía no dibuja.

## Cómo funciona
### GePatch dev62 (main.c, "Espejo de la VRAM")
- Buzón del modo sombra versión 2 (`SB[1] = 2`). Palabras nuevas: `+0x80` mapa de páginas de **listas** y `+0xC0` de
  **datos** (16 palabras = 512 páginas de 4 KB cada uno, los escribe la Vita); `+0x100` número de la última copia
  terminada (lo escribe el GE); `+0x110` copias encoladas, `+0x114` páginas copiadas, `+0x118` veces que la copia anterior
  seguía en el GE (totales, PSP); `+0x800` número de la última copia de cada página (512 palabras, PSP).
- `espejoFlip()` en el gancho del flip (con k1 = 0): páginas de listas todas; de datos las nuevas (hasta 64 por frame) y
  8 ya copiadas por frame, rotando. Tramos contiguos con `sceGuCopyImage` 8888 de 1024 px de ancho (1 fila = 1 página),
  hasta 100 tramos; al final una copia de 16 bytes pone el número de la copia en `+0x100`. Dos listas alternadas en
  0x0A100000 (no en RENDER_LIST + 0x8000: ahí está TX_RING); si la de hace dos frames sigue en el GE
  (`sceGeListSync(qid, 1) > 0`) ese frame no copia. Sin pedidos no encola nada.
- Log cada 600 frames: `stats espejo: paginas pedidas listas= datos= | copias= paginas_copiadas= (KB/frame) tramos_max=
  ocupado= terminada=`.

### gepresent v13.x (tools/gepresent/captura.c, "Espejo de la VRAM")
- `vtr(a)`: dirección legible para `a`; la VRAM (0x04xxxxxx, `& 0x1FFFFF`) va al espejo si su página es válida. El
  capturador y los lectores leen todo a través de `vtr`. Una página es válida cuando su copia terminó (`+0x800[p]` ≤
  `+0x100`); desde v13.3 lo comprueba el mismo capturador (`espCheck`).
- Pedidos (`want`): CALL/JUMP a una página sin copia → página de destino y la siguiente (lista); se salta la sublista
  ese frame. Textura en la VRAM (bytes del nivel 0 según formato, ancho de búfer y alto) y paleta (`0xC4`: bloques de 32 B)
  → datos. Las páginas de un destino de dibujo (fb del draw y su z con prueba de z, alto = borde de abajo del scissor) se
  marcan `rt` (`markRt`) y no se piden como datos (la Vita las tendrá en HD).
- Sublista que sigue en una página sin copia: se vuelve al nivel de arriba (`UNWIND`, contador `cortadas`) en vez de
  perder la lista.
- Hilo del espejo (`gepresent_espejo`, núcleo 0, prioridad 0xB0, cada 5 ms): solo estadísticas. Calcula una huella de
  cada página después de cada copia y cuenta las que cambiaron (listas/datos). Reescribe los mapas de pedidos en el buzón
  cada 100 ms por si GePatch lo reinició.
- **v13.2:** memoria de sublistas de 2048 lugares y depósito de 2 MB (antes 512 y 512 KB; 5976 KB en total). Si no
  hay memoria, prueba con la mitad.
- **v13.3:** `espCheck` (antes solo el hilo del espejo marcaba páginas válidas y a veces no recibía tiempo: ~6 sublistas
  por frame seguían saltándose); comparación de líneas con NEON (`lineEq`); `comparar_us` en el log (tiempo de comparar
  las líneas de las sublistas guardadas, muestreado 1 de cada 8 × 8).
- Línea nueva del log: `espejo: paginas pedidas listas= datos= validas= en_destinos= | por seg: copias= paginas_copiadas=
  (KB/f) ocupado= revisadas= nuevas= cambiaron listas= datos= hilo_us= invalidadas= | sublistas_vram/f recorridas= cortadas=
  texturas_en_destino/f=`.
- Prueba fuera de la Vita: `prueba_memoria.c <modo> 1` pone las sublistas en la VRAM (contenido en el espejo); desde el
  frame 1 da los mismos conteos que en la RAM con y sin memoria y revisando todas. `<modo> 2`: una página sin copia.

## Versiones
| Versión | Qué | md5 |
|---|---|---|
| dev62.1 + v13.1 | espejo | `2faf13fa…` / `515e8ef3…` |
| v13.2 | memoria de sublistas ×4 | `81605a67…` |
| v13.3 | capturador valida páginas, NEON, `comparar_us` | `282b3a74…` |
| dev62 (commit) | = dev62.1 con el número de versión final | `d44ba997…` |

## Resultados [medido]
Logs: `vitabridge/etapa2_v13.1/gt1/`, `etapa2_v13.2/gt1/`, `etapa2_v13.3/gt1/`, `etapa2_v13.3/sh1/` (este último trae
también la sesión de GT: Adrenaline no se cerró entre juegos).

### GT (UCUS-98632)
| | v12.13 (sin espejo) | v13.1 | v13.2 | v13.3 |
|---|---|---|---|---|
| fps de GePatch, casi toda la vuelta | 59–60 | 33–60 | 59,5–59,9 | 59,4–59,9 |
| comienzo de la carrera / puente | 59 / ~56 | 38 / 33 | 52–53 / 52 | 56–58 / ~52 |
| comandos por frame en el puente | ~67 000 | ~82 000 | 77 000–82 000 | 75 000–83 000 |
| depósito de la memoria vaciado | ~40 en la vuelta | 4–5 por seg | ~0 | ~0 |
- Pedidas: 43–81 páginas de listas (hasta 324 KB por frame) y ~145 de datos. El GE copia 200–350 KB por frame; el juego
  espera al GE en drawsync 0,1–0,3 ms por sync (como antes): **la copia no es lo que baja los fps**.
- v13.1 bajaba por la memoria de sublistas: con las de la VRAM GT llama ~250 sublistas por frame y la tabla (512) y el
  depósito (512 KB) se vaciaban 4–5 veces por segundo (todo se volvía a grabar).
- v13.3: 100–140 sublistas por frame leídas desde el espejo, 0 cortadas, 0 saltadas; revisión de la memoria 0 DISTINTAS.
- **Lo que queda caro:** comparar las líneas de las sublistas guardadas cuesta ~2 µs por línea de 64 bytes: ~9 ms de
  los ~18 ms por frame del puente (4000 líneas). El resto es recorrer lo que no está guardado.
- **Cambios en el espejo:** páginas de listas que cambian entre copias: 0–78 por seg; páginas de datos (texturas):
  100–230 por seg en carrera en v13.3 (en v13.1 se midieron 0: el hilo casi no recibía tiempo). Para la etapa 3 el repaso
  de 8 páginas de datos por frame (~0,3 s por vuelta completa) no alcanza [deducido].

### SH (ULUS-10450)
- fps como con v12.10 (26–56 según la escena; SH no llega a 60 ni sin plugin); la captura casi nunca hace esperar.
- **Detalle:** pide 253 páginas de datos (1 MB) y todas cambian en cada copia (son destinos que no se detectan: SH copia
  a la VRAM con TRANSFER, ~160 copias por frame). Cuesta 32 KB de copia por frame (~0,07 ms de GE): no afecta, pero
  sobra.

## Lo que falta / ideas
1. **Comparar menos:** los lectores (núcleos 0 y 2) podrían comprobar por adelantado las líneas de las sublistas
   guardadas, o comparar directo desde la caché compartida sin copiar; es ~la mitad del tiempo del puente.
2. SH: marcar como destino lo que escribe TRANSFER en la VRAM y dejar de pedir páginas de datos que cambian en todas las
   copias.
3. Texturas de GT que cambian en carrera: repaso más rápido o pedido por textura cuando se use (etapa 3).
4. Pruebas del PSP (`tests/`): no compilan desde dev61 (`asm("sync")` en `sbPost` con el compilador del PC).
5. Lo pendiente de la etapa 2: T2 (caja negra), T4, T6, grabación GXM en varios hilos → etapa 3 (dibujar).
