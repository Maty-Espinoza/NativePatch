# Etapa 2: espejo doble de la VRAM (GePatch dev64.1–dev64.5 + gepresent v13.6.1–v13.6.5, 2026-10-28)

Sigue a `etapa2_t4_t6_vida_datos.md` (punto 6 de la etapa 2: "espejo estable + texturas de GT al día"). Marcas:
[medido], [deducido]. **Commit:** GePatch **0.2-dev64** (= dev64.5 con el número final) + **gepresent v13.6** (= v13.6.5).

## En simple
- La Vita no puede ver la memoria de video del PSP (VRAM): el PSP le deja una copia ("espejo") en memoria normal en cada
  cambio de cuadro. Había **una sola copia**, que el PSP sobrescribía mientras la Vita podía estar leyéndola, y las
  texturas de GT se actualizaban de a 8 trozos por cuadro (quedaban hasta ~0,3 s atrasadas).
- Ahora hay **dos copias que se turnan**. Cada lista de dibujo se lee con la última copia ya terminada y la Vita le avisa
  al PSP cuál está usando: el PSP nunca escribe encima de esa.
- En cada copia van **todas las instrucciones** (sublistas) pedidas y **hasta 256 KB de texturas** (si hay más, por
  turnos).
- Lo que el juego copia a la VRAM con el propio pintor (copias de bloque, TRANSFER) ya no se pide al espejo: la Vita lo
  repetirá por su cuenta (y en alta resolución). Casi todas las texturas de GT que cambian en carrera y la imagen que SH
  copia para sus efectos son de ese tipo.
- **Resultado:** GT a los mismos fps que antes (puente 58,5 contra 57,8); SH ya no copia nada (antes 1 MB que cambiaba en
  cada copia) y el exterior quedó en 43,4 fps (antes 40,8). La imagen sigue siendo la nativa: la Vita todavía no dibuja.

## Cómo funciona
### GePatch dev64 (main.c, "Espejo de la VRAM")
- Buzón versión 4 (`SB[1] = 4`). Espejos: copias pares en `MIRROR` (0x0A200000), impares en `MIRROR2` (0x0AC00000, sin
  uso en ningún modo de GePatch). Número de la última copia de cada página: `+0x800` (MIRROR) y `+0x21000` (MIRROR2,
  justo después del anillo de eventos), 512 palabras cada uno.
- `espejoFlip()`: todas las páginas de listas + hasta `ESP_DATOS` (64) de datos, empezando donde quedó la última vez que
  no entraron todas; tramos contiguos con `sceGuCopyImage` (hasta 200, listas de 8 KB alternadas en 0x0A100000).
- No copia si la copia de hace dos frames sigue en el GE (`SB[0x46]`) **o si la Vita está usando ese espejo**: `SB[0x48]`
  (copia en uso, lo escribe la Vita) con la misma paridad, solo si es una copia real (≤ la última encolada) y la Vita
  responde (`sbAlive`). Contador `SB[0x49]` ("vita_usando").
- Log: `stats espejo: pedidas listas= datos= (KB) | copias= paginas= (KB/frame)` y `stats espejo: tramos_max= ocupado=
  vita_usando= no_entraron= terminada=` (dos líneas: el LOG corta a 160 caracteres).

### gepresent v13.6 (tools/gepresent/captura.c, "Espejo de la VRAM")
- Al empezar cada lista (`walk`, antes de publicarla a los lectores): `esp_u = SB[0x40]` (última copia terminada) y
  `SB[0x48] = esp_u`. `vtr()` lee la VRAM del espejo de `esp_u`; una página sirve si su última copia en ese espejo es ≤
  `esp_u` (`espCheck`, bits por espejo `esp_ok[2][16]`). Sin copia todavía (0), nada es válido.
- TRANSFER con destino en la VRAM: el destino se marca como destino de dibujo (`markRt`: no se pide como dato) y el
  origen en la VRAM se pide al espejo.
- Hilo del espejo: prioridad 0xBE (debajo de los lectores), huella de 4 páginas pedidas cada 5 ms por turnos (antes:
  cada página después de cada copia; con v13.5 ocupaba ~37 % del núcleo 0 [medido]). `hilo_us` es tiempo de reloj:
  con prioridad baja incluye las esperas.
- Log: `espejo: paginas pedidas listas= datos= validas=<espejo 0>/<espejo 1> en_destinos= | por seg: copias=
  paginas_copiadas= (KB/f) ocupado= vita_usando= | sublistas_vram/f recorridas= cortadas= texturas_en_destino/f=` y
  `espejo (por seg): listas= con_la_copia_anterior= | muestreo: revisadas= nuevas= cambiaron listas= datos= hilo_us=`.
- Prueba fuera de la Vita: `prueba_memoria.c` simula las copias alternadas (`<modo> 1`: sublistas en la VRAM; desde el
  frame 1 iguales a la RAM; `<modo> 2`: una página nunca copiada).

## Versiones
| Versión | Qué | md5 (GePatch / gepresent) |
|---|---|---|
| dev64.1 + v13.6.1 | espejo doble; la Vita usaba la copia del flip | `662ce863…` / `3a314ded…` |
| dev64.2 + v13.6.2 | corrige el arranque (anunciaba la copia -1: GePatch no copiaba nunca) | `29c6f543…` / `40041583…` |
| dev64.3 + v13.6.3 | cada lista usa la última copia terminada, sin esperar; TRANSFER chicos como destino | `2f704796…` / `900eccfe…` |
| dev64.4 + v13.6.4 | tope de 64 páginas de datos por copia | `64944cde…` / `03a2a159…` |
| dev64.5 + v13.6.5 | todo destino de TRANSFER en la VRAM es destino (sin tope de tamaño) | `4aa7eb10…` / `e1c9068d…` |
| dev64 + v13.6 (commit) | = dev64.5 + v13.6.5 con los números finales | `32742693…` / `5a6e524e…` |
Desde esta etapa se compila en la VM del Mac (`tools/mac/`): pspdev para ARM64 y vitasdk (GCC 15). Los md5 no son
comparables con los compilados en el contenedor (otros compiladores).

## Resultados [medido]
Logs: `vitabridge/etapa2_dev64.1/gt1`, `etapa2_dev64.2/gt1`, `etapa2_dev64.3/gt1`, `etapa2_dev64.4/gt1`,
`etapa2_dev64.4/sh1`, `etapa2_dev64.5/sh1` (en el Mac).

### Lo que salió mal en el camino
- **dev64.1:** al empezar, la Vita anunciaba "uso la copia -1" (fallback con la copia 0) y GePatch se saltó todas las
  copias (`vita_usando=600` por cada 10 s). La prueba fuera de la Vita no simula ese aviso.
- **dev64.2:** la Vita esperaba (hasta ~4,5 ms) la copia encolada en el último flip. Por la compuerta del sync, GT
  esperaba a la Vita: puente 55,7 fps (antes 57,8). Además, GT encola parte del frame siguiente antes del flip; para esas
  listas la copia correcta es la anterior. Con dev64.3 (última terminada, sin esperar): `con_la_copia_anterior=0` en todas
  las corridas (la copia del flip ya terminó cuando empieza la lista siguiente).
- **dev64.2/.3:** a los ~2 min de GT, `en_destinos` pasó de 4 a 440–512 páginas: muchas copias de bloque chicas (al
  cargar la carrera) marcaron como destino casi toda la VRAM, y las texturas pedidas bajaron de 139 a 12. No era un
  error: esas texturas las escribe el GE con TRANSFER (la Vita las repetirá). En la vuelta de dev64.4 GT no hizo esas
  copias y siguieron pedidas las 145 páginas (el caso más pesado).
- **Copiar todas las texturas en cada frame** (dev64.2/.3, ~730–850 KB por frame) bajó el comienzo de la carrera de GT a
  55–57 fps (el juego espera ~0,8 ms por sync al GE contra ~0,4). Con el tope de 64 páginas (dev64.4): ~540 KB por frame,
  cada página de textura al día cada ~2 frames (antes ~18).
- **SH (dev64.4):** pedía 253 páginas (1 MB) que cambiaban en todas las copias: una copia de bloque por frame de su imagen
  480x272 (0x0404C000, línea 512, 32 bits = 136 páginas) a una textura. Quedó fuera del tope de 64 páginas de dev64.3;
  dev64.5 marca todo destino de TRANSFER y SH no copia nada.

### fps (GePatch, cada 10 s)
| | dev63.3 + v13.5.3 | dev64.3 | dev64.4 | dev64.5 |
|---|---|---|---|---|
| GT comienzo de la carrera | 59,8 | 55,5 / 57,2 | 55,9 (carga) / 58,2 | — |
| GT casi toda la vuelta | ~59,9 | 59,6–59,9 | 59,7–59,9 | — |
| GT puente | 57,8 | 57,5 | **58,5** | — |
| SH exterior (últimos ~30 s) | 40,8 | — | 39,4–40,7 | **43,4** |
- GT con dev64.4: 43–76 páginas de listas y 145 de datos pedidas; ~540 KB copiados por frame; 60–132 sublistas por
  frame leídas del espejo, 0 cortadas; GE: el juego espera ~0,65 ms por drawsync.
- SH con dev64.5: 0 páginas pedidas, 0 copias; `en_destinos=512`. ARM del capturador ~4 ms por frame.
- **DISTINTAS** (revisión de la memoria de sublistas, 1 de cada 32): 0 con dev64.1 (sin espejo), 1 / 4 / 7 con
  dev64.2 / .3 / .4 en ~50 000 revisadas por corrida (≈0,01 %); con v13.5 siempre 0. Causa sin investigar (`TODO.md`).

### Confirmación con dev64 + v13.6 (2026-10-29, Windows) [medido]
Logs `vitabridge/etapa2_dev64/gt1` y `sh1` (en el PC con Windows). Mismo código que dev64.5 + v13.6.5.
- **GT** (una vuelta, con ~10 min de pausa en medio): 59,7–60 fps casi toda la vuelta; 55–58 fps ~3 s al comenzar
  (~75 000 comandos, 15 000 draws por frame) y **48–56 fps los últimos ~12 s** (~80 000 comandos, 16 500 draws por
  frame): la captura tarda ~17–19 ms por frame (ARM) y el juego la espera en el sync. Durante la pausa el espejo sigue
  copiando ~548 KB por frame sin bajar los fps. DISTINTAS: 2 de 175 172 revisadas.
- **SH:** el juego **nunca esperó a la Vita** (0 µs por sync); captura 4–15 ms por frame, 0 copias. Los fps (26–59
  según la zona) son los del juego original. DISTINTAS: 0.

## Lo que falta / ideas
1. Entender las pocas DISTINTAS con el espejo doble antes de dibujar (etapa 3).
2. La Vita tendrá que repetir las copias de bloque (TRANSFER) a la VRAM y guardar el resultado (su propia VRAM): lo que se
   marca como destino ya no está en el espejo. Si el juego hizo la copia antes de que empezara la captura, la Vita no la
   tiene (en la práctica el capturador arranca con el juego).
3. Para dibujar (etapa 3) hay que registrar MIRROR2 (0x0AC00000–0x0AE00000) para la GPU (`mapZonas` de gepresent.c).
4. Grabación GXM en 2–3 núcleos fijos (siguiente punto de la etapa 2) y T2 (caja negra).
