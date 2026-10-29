# Diagnóstico de GePatch: plan y volcado binario (.gpd)

Escrito el 2026-10-21, después de dev27. Motivo: los arreglos de Silent Hill (formulario, spinner, puerta) se estaban haciendo a ciegas, con capturas de texto incompletas y descripciones verbales de la imagen.

## Qué información faltaba
1. **Referencia:** qué debería verse en cada paso del frame (formulario, spinner, puerta). Solo existía la descripción del usuario.
2. **Resultado real del plugin:** imágenes del buffer grande, de los render targets, del buffer temporal y de las texturas que arma el juego con copias (por ejemplo, la textura del formulario en 097f6960).
3. **El frame completo:**
   - La captura de texto se corta en el flip, pero Silent Hill sigue recorriendo la misma lista después del flip.
   - La captura solo registra cambios de estado "interesantes" y no guarda los datos.
4. **Datos de cada draw:** vértices antes y después de duplicarlos, índices, texturas, paleta (CLUT) y origen de las copias.
5. **Decisiones del plugin por draw y por fb**, junto con la posición en el frame de cada línea del log.
6. **Estado inicial del frame:** registros reales del GE (`sceGeGetCmd`), matrices (`sceGeGetMtx`) y VRAM.

## Cómo distinguir un error del flujo normal
- **Dos volcados del mismo punto del juego:**
  - Uno **pasivo** (L al arrancar): el juego corre sin parches y es la referencia.
  - Uno **activo**: con el plugin.
- `gpd.py comparar` alinea los draws de ambos volcados, lista los draws que el plugin trató distinto y compara imágenes (destinos de copias, pantalla).
- **Criterio:** un evento del log solo es un error si coincide con una diferencia de imagen contra la referencia.
- **PPSSPP de referencia:**
  - `gpd.py ppsspp` convierte el volcado pasivo a `.ppdmp`. Verificado: PPSSPPHeadless con `--graphics=software` dibuja el frame del formulario igual que la Vita.
  - Con `--plugin --hasta=N --mostrar=ADDR,ANCHO,FMT`, PPSSPP ejecuta lo que mandó el plugin, sin sus listas propias.
- **Instantáneas (SNAP):** las listas del plugin copian con el GE buffers intermedios a la RAM. Sirven para separar en qué paso se rompe algo.

## Formato .gpd (dump.h / dump.c)
- Archivo `ms0:/gepatch_<ID>_<frame>.gpd`. Se genera con L+R+SELECT o con la opción del menú "Volcar el proximo frame (.gpd)".
- **Inicio y fin:** empieza en la siguiente lista encolada y termina en la primera lista nueva después de un flip.
- **Estructura:** registros `[u32 tipo][u32 bytes][datos]`.
- **Tipos de registro:**
  - HEADER, REGS (inicio y fin).
  - MEM:
    - VRAM real al inicio y al final.
    - VRAM falsa al inicio y al final.
    - Destinos de las copias a la RAM.
  - LIST, CMDS, DRAW, BLOB, EVENT, FLIP, FB, TXLIST, END, SNAP.
- **Errores de dev28:**
  - Hilos escribiendo a la vez.
  - Índices fuera de orden.
  - CLUT no repetida.

  Todos corregidos. `gpd.py` también lee los archivos dañados de dev28.

## Puente con la consola (2026-10-21)
- **En la Vita:** vitacompanion 1.06 con FTP en el puerto 1337 y comandos en el 1338 (`launch`, `kill`, `press`, `release`, `wait`, `screen`, `reboot`).
- **En el Mac:** `tools/vitabridge.py`, corriendo en la Terminal del usuario. Busca sola la Vita en la red (la del usuario está en 192.168.0.15).
- **Carpeta compartida:** `~/Downloads/vitabridge`:
  - `enviar/` → se sube a la Vita.
  - `cmd_*.txt` → se manda al puerto 1338.
  - `bajar_*.txt` → baja esas rutas.
  - `recibidos/` → logs, `.gpd` y archivos bajados.
  - `estado.txt` → latido.
- **Probado:**
  - Subir el plugin funciona.
  - Los comandos funcionan.
  - L+R+SELECT simulado con `press l1;press r1;press select;wait 2s;release all` llega a Adrenaline y dispara el volcado.
- **Límite:** Claude no tiene acceso a la red local (VM aislada; el proxy rechaza 192.168.x.x), así que el relevo lo tiene que correr el usuario.

## Hallazgo clave: la VRAM del PSP en la Vita es de 2 MB (dumps 8675 y 005349)
- `sceGeEdramSetSize(4 MB)` devuelve 0x80000104: no hay VRAM extra.
- **0x04200000–0x043FFFFF es un reflejo** de los 2 MB bajos:
  - Lo que el GE **dibuja** ahí cae linealmente en `dirección − 2 MB`: 0x042xxxxx pisa el buffer grande y 0x043xxxxx pisa el z-buffer.
  - Lo que se **lee** ahí (copias, texturas, CPU) ve un reflejo permutado (`dirección ^ 0x8040` en el buffer temporal 0x04310000 ↔ 0x04110000).
- **Consecuencia:** todos los render targets y el buffer temporal anteriores se solapaban con el buffer grande o el z-buffer:
  - formulario de SH con letras desordenadas;
  - glitches de fondo con "Fb intermedios";
  - probablemente reflejos y espejos de GT.
- dev31 (z-buffer en 0x04200000) empeoró todo porque el z-buffer se escribía encima del buffer grande. Eso confirmó el diagnóstico.
- Los 2 MB reales están llenos: buffer grande 1020 KB + z-buffer 1020 KB. **No queda VRAM para render targets.**
- **dev32:**
  - El probeVram hace una prueba de dibujo; si detecta el reflejo, apaga los render targets.
  - El buffer temporal (copias de pantalla, formulario) usa la zona del z-buffer (0x04100000), que está libre durante la copia.
  - Se quitó "Fb intermedios = 1" por defecto para SH.
- **dev33 (confirmado en la consola, commit e4530d5):** la reducción no escribía el alfa. En el GE del PSP el alfa del destino solo se escribe a través del stencil; ahora la reducción usa stencil REPLACE 0xff y luego restaura el del juego. Antes, la copia del formulario quedaba con restos del z-buffer en el alfa (0/92/105/106) y el juego la dibujaba semitransparente. Volcado 4830: alfa 255 y formulario limpio.
- **Abierto:** cómo hacer los fb intermedios (puerta de SH, 0a170000; reflejos de GT) sin VRAM libre. Opciones:
  - pedir prestado el z-buffer cuando el pase no usa profundidad;
  - dibujar a escala 1 en la VRAM falsa (RAM);
  - copiar a la RAM.

## Otros hallazgos en SH
### Formulario
- La textura original es opaca (alfa 255), así que la transparencia no era la causa.
- El texto al doble en el buffer grande está perfecto (instantánea "pantalla antes de copia"). Se rompía al pasar por el buffer temporal en la VRAM inexistente (dev32) y por el alfa sin escribir (dev33). **Resuelto.**

### Spinner
- Lo dibuja la CPU en el fb mostrado (0a0d4000, 1024 de ancho).
- Desde dev29 se pega encima del buffer grande y luego se borra esa zona. Pendiente de confirmar.

### Fb de copia a escala 1 (dev27)
Quedó como opción apagada: los frames que no copian perdían draws.

## dev45 (2026-10-22): volcados dev44 de RR 1922 y SH 6442
### Ridge Racer: franjas moradas en toda la pantalla
- **Causa (confirmada en el volcado):** el desenfoque dibuja el render target 0a088000 (escala 1, en la VRAM del z) leyéndose a sí mismo. `rt1SelfTex` parchaba las palabras TEXADDR/TEXBUFWIDTH **del juego** (08bce218/21c) a 0x04100000. El draw siguiente, el brillo a pantalla completa en el buffer grande (#2321, mezcla aditiva 0xaa), heredaba esa textura y sumaba el z-buffer leído como 565 → franjas moradas.
- **Arreglo:** `rt1SelfDraw`. Solo ese draw pasa a BASE + CALL a una lista del plugin: TEXSYNC, textura en la VRAM del z, el draw y, al final, la textura en la VRAM falsa (donde queda el rt al cerrarlo). Las palabras del juego no se tocan. Hay un test nuevo (171 comprobaciones).

### Silent Hill: el auto sin texturas por unos segundos
- **Causa:** en dev44, si la mezcla del pase 0a170000 no se podía intercambiar, el pase quedaba fuera 10 s. En el log: falló en el frame 6185 (al empezar la escena) y volvió en el 6785. Ese pase dibuja el auto (texturas 08bb3600/08bb4780…).
- **Arreglo:**
  - La espera crece 2, 4, 8… frames por cada fallo seguido (hasta 10 s) y se reinicia cuando la mezcla funciona.
  - Una copia con color negro (fundido desde negro) ya no se confunde con "no es una copia" (`fullCopy`).
  - El motivo del fallo queda en el log ("intercambio: la mezcla no se puede hacer: …") con autocaptura.

### Abierto
- Cuadrados morados de SH: no aparecen en los volcados. El spinner que pega la CPU (440,232) se ve bien en el fb 0a0d4000. Hace falta una captura.

## dev46–dev48 (2026-10-23)
### Silent Hill: cuadrados morados (dev46, confirmado en el laboratorio)
- **Captura:** volcado 10518 del laboratorio, con los rectángulos en (0,0)–(240,136) y (256,16)–(376,84).
- **Causa:** el pase de luz se dibuja por intercambio en el buffer grande. Dos draws (#403 y #404) leen el propio pase como textura, en la dirección del juego 0x04170000; en la Vita esa dirección es el z-buffer.
- **Arreglo:** texmode 9. La pantalla que se lee a sí misma toma la textura del buffer grande al doble, por cuartos (`rt1FromBig`, ahora con posiciones y texturas float y hasta 12 vértices).

### Ridge Racer: camino que no se dibuja (dev47–dev48)
- **Causa (confirmada en el modelo):** el GE descarta un triángulo entero si un vértice cae fuera de 0..4096. Al doble, el margen fuera de pantalla baja de ~1800 a ~780 px del juego. Al reproducir el frame original con solo el viewport x2 en PPSSPPHeadless sale la misma cuña de agua.
- **dev47:** subdivisión en 4. No alcanzaba: el pedazo con el vértice que sale nunca entra en el rango. Además, el asfalto se extiende detrás de la cámara; a 1x el GE lo recorta en el plano cercano, pero al doble los puntos de corte caen fuera.
- **dev48:** recorte exacto (Sutherland–Hodgman) contra el plano cercano (`z >= -w`, con el recorte de profundidad del juego), `w > 0` y el rango seguro 128..3968. En el borde exacto el GE también descartaba, porque calcula con floats de 24 bits. Se probó primero con una simulación en Python sobre el volcado 3624, y el camino vuelve.
- **Pendiente:** el retrovisor (cámara en tercera persona) quedó vacío con dev48. No resolver todavía.

## dev49 (2026-10-24): herramientas y limpieza
- Volcado de 3 frames y `gpd.py linaje / diff / vram` (ver `estrategia_diagnostico.md`).
- **Código quitado** (nunca se activaba en la Vita, o estaba reemplazado):
  - render targets al doble en la VRAM alta;
  - fb intermedios como render target;
  - fb de copia a escala 1 (csrc);
  - copia reducida al fb mostrado (`tex_fb` = 2);
  - recorrido de listas antiguo y NOP 0;
  - prueba de VRAM de 4 MB.
- El `.prx` bajó de 142 KB a 117 KB y la escena de prueba da lo mismo en el modelo.

## Viabilidad
- **Solo con el addon sigue siendo viable.** El GE es hardware real; el límite real es la VRAM de 2 MB, que obliga a usar la RAM para todo lo que no sea el buffer grande y el z-buffer.
- **Camino recomendado:**
  1. Volcados pasivo y activo.
  2. Comparación automática.
  3. PPSSPP headless como referencia.
  4. Pruebas en la consola a través del relevo.
- **Descartado por costo:** emular el GE con la GPU de la Vita.

## Regla de commits (pedida por el usuario)
Solo se sube un commit cuando se corrige un error confirmado, no en cada iteración.
