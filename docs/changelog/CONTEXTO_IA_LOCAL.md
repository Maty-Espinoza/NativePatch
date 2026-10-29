# Contexto de NativePatch para una IA local

Este documento le explica a una IA local (Ollama u otra) el proyecto y la forma de trabajar,
para ayudar a Matías cuando no hay acceso a Claude. Está escrito para leerse entero de una vez.
Al final hay instrucciones concretas de qué hacer y qué no.

Estado: 2026-10-24, GePatch **0.2-dev52**, rama `dev` de `Maty-Espinoza/NativePatch`.

---

## 1. El proyecto en pocas palabras

- **Adrenaline** es el emulador oficial de PSP que corre dentro de la **PS Vita**. La Vita tiene
  el chip gráfico de la PSP (el **GE**) de verdad: los juegos de PSP dibujan con hardware real.
- **GePatch** (`ge_patch.prx`) es un plugin para Adrenaline. Hace que los juegos de PSP se vean
  a **960x544** en vez de 480x272 (el doble de resolución, la pantalla nativa de la Vita).
- No emula nada. **Reescribe las listas de comandos** que el juego le manda al GE (display
  lists) justo antes de que el GE las ejecute: dobla el tamaño del área de dibujo, mueve
  direcciones de memoria, duplica coordenadas 2D, etc.
- El autor original (TheOfficialFloW) lo abandonó en la versión 0.2 con muchos errores.
  NativePatch es el fork de Matías para revivirlo.
- Juegos de prueba:
  - **Ridge Racer** (ID ULJS-00001, versión japonesa): carreras 3D.
  - **Silent Hill Shattered Memories** (ULUS-10450): linterna, pases de luz, videos.
  - **Gran Turismo** (UCUS-98632): rendimiento.

## 2. Reglas de trabajo (respetarlas siempre)

1. Responder **siempre en español**.
2. Código **compacto pero entendible**: preferir simplicidad y legibilidad a constantes y
   verbosidad de "buenas prácticas".
3. **Commits solo** cuando un error se corrige y se confirma (en la Vita o en el laboratorio), o
   cuando Matías lo pide explícitamente. No hacer commits por cada intento.
4. **No hacer acciones que no se pidieron.** Si algo parece útil pero no fue pedido, proponerlo
   y esperar respuesta.
5. **No arreglar el retrovisor de Ridge Racer** hasta que Matías lo pida.
6. Un evento del log solo es un error si coincide con algo que se ve mal en la imagen.
7. Cuando no se sabe algo, decirlo. No inventar direcciones, valores ni resultados.

## 3. Dónde está cada cosa

| Qué | Dónde |
|---|---|
| Código del plugin | repo NativePatch: `main.c` (~3000 líneas), `sub.h` (recorte 3D), `dump.c` (volcados), `menu.c`, `log.c`, `gu.c` |
| Tests en PC | `tests/test_main.c` (`make -C tests`; hoy 115 comprobaciones, 0 fallas) |
| Lector de volcados | `tools/gpd.py` (solo biblioteca estándar de Python 3) |
| Documentos | `docs/` (este, `CONTEXTO_IA.md`, `diagnostico.md`, `regresiones_versiones.md`, `MANUAL_BASICO.md`...) |
| Laboratorio en el Mac | `~/Documents/gepatch-lab` (PPSSPP modificado que imita a Adrenaline) |
| Relevo con la Vita | `~/Downloads/vitabridge` (ver sección 6) |
| Plugin en la Vita | `ux0:/pspemu/seplugins/ge_patch.prx`, activado en `game.txt` con `ms0:/seplugins/ge_patch.prx 1` |
| Plugin en el laboratorio | `~/Documents/gepatch-lab/memstick/seplugins/ge_patch.prx` |
| Versiones viejas para comparar | `~/Documents/gepatch-lab/memstick/seplugins/versiones/` (v0.2 original, dev46, dev49, dev51) |

## 4. Cómo funciona el plugin (lo necesario para razonar)

### 4.1 La idea central
- El juego cree que dibuja en una pantalla de 480x272 en la VRAM (memoria de video, 2 MB).
- El plugin:
  - Le da al juego una **VRAM falsa** en la RAM (0x0A000000).
  - Hace que el GE dibuje la pantalla en un **buffer grande** de 960x544 en la VRAM real
    (0x04000000).
  - Copia ese buffer a 0x0A400000, de donde Adrenaline lo muestra.
- Para eso escala viewport, scissor y región ×2, y duplica las coordenadas de los dibujos 2D
  (modo "through": HUD, menús, sprites).

### 4.2 Recorrido de listas
- Ganchos:
  - `sceGeListEnQueue` y `sceGeListUpdateStallAddr` llaman a `patchGeList(list, stall)`, que
    lee cada comando nuevo.
  - También parchea `sceGeEdramGetAddr/GetSize` (VRAM falsa) y `sceDisplaySetFrameBuf` (el
    "flip", cuando un frame termina).
- `setOp(p, orig, op)` reemplaza una palabra de la lista y guarda la original en la tabla MEMO
  (`origOp`). Las listas se recorren muchas veces (algunas son estáticas), así que cada
  decisión se recalcula y se deshace lo que ya no aplica.
- `txSlot()` reserva 1 KB en un anillo de listas propias. `txCall(list, reg, sl)` cambia la
  palabra anterior al draw por BASE y el draw por CALL a esa lista. Así se insertan comandos sin
  mover la lista del juego.

### 4.3 Clasificación de framebuffers (fb)
Cada vez que el juego elige dónde dibujar (FRAMEBUFPTR + FRAMEBUFWIDTH):
- **Ancho 512/480/960 → buffer grande** (es la pantalla).
- **Ancho 16–479 → "rt1"**, render target a escala 1 dibujado en la zona del z-buffer
  (0x04100000). Al terminar se copia a la VRAM falsa. Si ya se usó la profundidad, el z se guarda
  antes en Z_SAVE y se restaura después.
- **Lo demás → ignorado** (sus draws se anulan).

### 4.4 Texturas (`texmode`)
| valor | qué hace |
|---|---|
| 0 | normal |
| 1 | omitir el draw |
| 3 | la textura estaba en la VRAM real: se lee de la falsa |
| 4 | la textura es el fb actual |
| 6 | mezcla del pase por intercambio (Silent Hill) |
| 7 | rt1 que lee la pantalla |
| 8 | rt1 que se lee a sí mismo |
| 9 | la pantalla que se lee a sí misma (arregló los cuadrados morados de SH) |

### 4.5 Recorte 3D (`sub.h`, opción del menú `recorte3d`)
- El GE descarta un triángulo entero si **un vértice** cae fuera de 0..4096 en pantalla. Al
  doble, los triángulos grandes cerca de la cámara (el camino bajo el auto en RR) desaparecían.
- El plugin sigue las matrices del juego, calcula en la CPU dónde cae cada vértice y recorta los
  triángulos que se salen. Los recorta contra el plano cercano y contra un rango seguro
  (128..3968). Después reemplaza el draw por un CALL a una lista con los triángulos nuevos.
- Desde dev50: si el recorte no deja nada, se dibuja el triángulo original.

### 4.6 Mapa de memoria del plugin (RAM 0x0A000000–0x0B000000)
| Dirección | Uso |
|---|---|
| 0x0A000000 | VRAM falsa (4 MB para el juego) |
| 0x0A400000 | imagen 960x544 que muestra Adrenaline |
| 0x0A500000 | escena guardada para el intercambio |
| 0x0A600000 | MEMO: palabras originales de los comandos parchados |
| 0x0A700000 | vértices 2D duplicados; 0x0A780000 caché de sublistas |
| 0x0A800000 | listas propias; desde +0x8000, anillo de **512** listas de 1 KB (dev51; antes 64) |
| 0x0A900000 | imagen del menú |
| 0x0A980000 | buffer del volcado; 0x0AA00000–0x0AE00000 instantáneas |
| 0x0AE00000 | recorte 3D: coordenadas, banderas, anillo de vértices hasta 0x0AF00000 |
| 0x0AF40000 | dibujos de la CPU (spinner) |
| 0x0AF60000 | Z_SAVE (hasta 0x0B000000) |
| 0x0B000000 | paquete de Adrenaline: **no tocar** |

### 4.7 Hechos de la plataforma (costaron caro, no olvidar)
- La VRAM de PSP en la Vita es de **2 MB** y está llena: buffer grande (0x04000000) y su
  z-buffer (0x04100000). No hay VRAM libre.
- El GE **no puede dibujar en la RAM**. Lo que no cabe se copia con TRANSFER.
- El GE va **detrás** del recorrido: el plugin parcha listas que el GE ejecuta más tarde (hasta
  uno o dos frames). Toda memoria que el plugin escribe y el GE lee (listas, vértices) tiene que
  durar varios frames.
- Encolar una lista con contexto desde el plugin congela Adrenaline. Escribir más allá de la VRAM
  (0x04800000) lo cierra.

## 5. Controles en la consola y el laboratorio

- **Mantener L al arrancar el juego:** modo pasivo (sin parches; sirve de referencia).
- **L+R+START:** menú del plugin. Las opciones se guardan por juego en `ms0:/gepatch_<ID>.txt`
  (por ejemplo `recorte3d=0`).
- **L+R+SELECT:** captura de texto en el log y **volcado binario de 1 frame**
  (`ms0:/gepatch_<ID>_<frame>.gpd`). dev49 grababa 3 frames; dev52 volvió a 1.
- **Log:** `ms0:/ge_patch.log` (la sesión anterior queda en `ge_patch_prev.log`).
- Además hay hasta 5 **capturas automáticas** de texto cuando aparece un evento nuevo.

## 6. Flujo de trabajo

### 6.1 Ciclo normal
1. Matías prueba en la Vita o en el laboratorio y describe lo que ve (texturas que faltan,
   colores, parpadeo, caídas).
2. Donde ve el error, aprieta **L+R+SELECT**: queda un log y un `.gpd`.
3. Se analizan (sección 7).
4. Se propone un cambio, se agrega un test en `tests/test_main.c`, se compila, se sube la
   versión (`GEPATCH_VERSION` en `log.h`: dev52 → dev53) y se copia el `.prx` al laboratorio y a
   la Vita.
5. Si Matías confirma que se arregló, se hace commit.

### 6.2 Compilar
- Plugin: toolchain pspdev en el PATH y luego
  `make ADRENALINE=<ruta a Adrenaline/cef>` (usa `systemctrl.h` y
  `libpspsystemctrl_kernel.a` de TheOfficialFloW/Adrenaline). Sale `ge_patch.prx`.
- Tests: `make -C tests` y después `tests/test_main` (necesita gcc con `-m32`).

### 6.3 Relevo con la Vita (`~/Downloads/vitabridge`)
`tools/vitabridge.py` lo corre Matías en la Terminal del Mac. La Vita está en 192.168.0.15 con
vitacompanion.
- Lo que se pone en `enviar/ux0/...` se sube a la Vita.
- Los logs y volcados nuevos bajan a `recibidos/`.
- `estado.txt` muestra si el relevo está vivo.

### 6.4 Laboratorio en el Mac (`~/Documents/gepatch-lab`)
- `bash jugar.sh rr.iso` abre Ridge Racer en PPSSPP como si fuera Adrenaline, con el plugin de
  `memstick/seplugins`. La ventana muestra la imagen nativa.
- Lo que hay en la Vita se ve igual en el laboratorio (salvo rendimiento), y el cielo negro
  también se reproduce ahí.
- El log y los volcados quedan en `memstick/`.
- Para probar otra versión: copiar un `.prx` de `versiones/` como `seplugins/ge_patch.prx`.

## 7. Cómo leer un volcado (`tools/gpd.py`)

Orden recomendado (de lo corto a lo largo):
1. `python3 tools/gpd.py resumen X.gpd`: versión, opciones, fbs usados, cuántos draws por fb
   y por decisión, eventos del log.
2. `gpd.py original X.gpd CARPETA --ppsspp=PPSSPPHeadless`: el mismo frame **sin GePatch**.
   Deja `comparacion.png` con tres paneles: sin GePatch | con GePatch | diferencias.
3. `gpd.py linaje X.gpd`: quién escribe cada fb y quién lo lee como textura.
4. `gpd.py diff X.gpd`: registros que el plugin cambió, por tramos de draws.
5. `gpd.py vram X.gpd`: mapa de la VRAM falsa y guardado/restauración del z.
6. Solo después, `gpd.py informe X.gpd > informe.txt` (todos los comandos, puede tener 30 000
   líneas: buscar con grep `>>> DRAW #N`) e `imagenes X.gpd CARPETA --texturas`.

Cómo leer el informe:
- `>>> DRAW #92 tira_tri n=4 vt=00019f 3D fb=0a044000 -> buffer grande tex=0a0fc7c0 [normal]`:
  - draw número 92, tira de triángulos de 4 vértices;
  - tipo de vértice 0x19f;
  - 3D, dibuja en el fb 0a044000, que va al buffer grande;
  - textura en 0x0A0FC7C0;
  - decisión "normal".
- `PRIM 040010 -> CALL 80dc00`: el plugin cambió ese draw por un CALL a su lista (recorte 3D,
  rt1 o intercambio).
- `(parche posterior)`: una palabra anterior que el plugin cambió después.

## 8. Estado actual y problemas abiertos

1. **Ridge Racer, cielo negro** (desde dev48; también en el laboratorio).
   - Los registros del GE en el primer draw del cielo son iguales a los de dev46, que se ve
     bien, y la textura está intacta.
   - Reproducir los comandos con el recorte 3D en PPSSPPHeadless sí dibuja el cielo.
   - Hipótesis: el anillo de 64 listas propias se reusaba antes de que el GE las ejecutara
     (dev50 usaba ~42 por frame). dev51 lo subió a 512. **Sin confirmar.**
2. **Caída del laboratorio con dev51** (pantalla "El juego crasheo", Bad Execution Address, hilo
   SasCore). En una prueba automática de una carrera de RR:
   - dev46 aguanta 240 s sin problemas.
   - dev49 se cae dentro del plugin, en `GetIndexBounds`: lee índices de 16 bits desde una
     dirección impar.
   - dev51 empieza a leer comandos basura a los ~155 s.
   - Sospecha: código nuevo de dev47–dev49 (el recorte 3D es lo más grande). **No investigado
     a fondo.** Una prueba útil: correr con `recorte3d=0` en `ms0:/gepatch_ULJS-00001.txt` y ver
     si la caída desaparece.
3. **Retrovisor de RR vacío** desde dev48: **no tocar** hasta que Matías lo pida.
4. **Silent Hill:** linterna (mezcla del pase de luz), pases de 1024 de ancho ignorados.
5. **Rendimiento:** SH 18–22 fps en escenas pesadas, GT 20–30 fps.

Historial reciente:
| versión | cambio |
|---|---|
| dev45 | RR sin franjas moradas (confirmado en la Vita) |
| dev46 | SH sin cuadrados morados (texmode 9) |
| dev47 | recorte 3D por subdivisión (no alcanzaba) |
| dev48 | recorte exacto contra el plano cercano; aparece el cielo negro y el retrovisor vacío |
| dev49 | volcado de 3 frames, `gpd.py linaje/diff/vram`, limpieza de código |
| dev50 | si el recorte no deja triángulos, se dibuja el original |
| dev51 | anillo de listas 64 → 512; SIGNAL SYNC solo salta si siguen FINISH+END |
| dev52 | volcado de vuelta a 1 frame |

Lecciones del GePatch original (detalle en `regresiones_versiones.md`):
- Anular draws en vez de redirigirlos rompió videos y efectos (v0.17).
- Reusar memoria que el GE aún no leyó dio parpadeo (v0.2).
- Las heurísticas por valor fijo (480, 512, ±2048) aciertan en un juego y fallan en otro.

## 9. Qué puede hacer la IA local (y qué no)

Puede:
- Explicar conceptos, el código y los documentos a Matías.
- Leer un log o la salida de `gpd.py` que Matías pegue, y decir qué llama la atención,
  siguiendo la sección 7.
- Ayudar a preparar un buen reporte para Claude (sección 10).
- Proponer hipótesis, siempre marcadas como hipótesis.
- Revisar un cambio pequeño de código y explicarlo.

No debe:
- Hacer commits, subir archivos ni cambiar opciones por su cuenta.
- Dar por confirmado algo que no se probó.
- Cambiar el mapa de memoria ni escribir fuera de 0x0A000000–0x0B000000.
- Tocar el retrovisor de RR.

## 10. Plantilla de reporte para Claude

```
Versión del plugin: dev__
Dónde: Vita / laboratorio
Juego y escena: (ej. RR, primera carrera, puente; SH, pasillo con linterna)
Qué se ve mal: (qué falta, color, parpadeo, caída; desde cuándo)
Cómo reproducirlo: (pasos)
Archivos: ge_patch.log, gepatch_<ID>_<frame>.gpd (frame de la captura)
Opciones cambiadas en el menú: (o "ninguna")
Comparación: ¿se ve bien en modo pasivo (L al iniciar)? ¿y con otra versión?
Lo que ya probé / lo que me dijo la IA local:
```
