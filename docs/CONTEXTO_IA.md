# Contexto para continuar GePatch (traspaso a otra IA)

Actualizado el 2026-10-24 (GePatch 0.2-dev49, rama `dev`). Léelo
entero antes de tocar código; los demás documentos de `docs/` amplían cada tema.

## 1. Qué es y qué se busca

- **GePatch** (`ge_patch.prx`) es un plugin de kernel para **Adrenaline** (el emulador oficial
  de PSP dentro de la PS Vita). Hace que los juegos de PSP se vean a **960x544** (el doble de
  480x272) parchando las display lists del GE (la GPU del PSP) antes de que se ejecuten.
- El GE de la Vita es hardware real de PSP: el plugin no emula nada, reescribe comandos.
- Este repo (`Maty-Espinoza/NativePatch`, fork del GePatch original de TheFlow, abandonado
  en v0.2) busca **revivirlo y corregir sus bugs**. Juegos de prueba: **Silent Hill
  Shattered Memories (ULUS-10450)**, **Ridge Racer (ULJS-00001)** y Gran Turismo.
- Clon del usuario en su Mac: `~/Documents/GitHub/NativePatch`.

### Reglas del usuario (respetarlas)
- Responder **siempre en español**.
- Código **compacto pero entendible**: se prefiere simplicidad y legibilidad a constantes y
  verbosidad de "buenas prácticas".
- **Commits:** solo cuando se corrige un error (idealmente confirmado en la Vita), no en cada
  iteración. Si el usuario pide un commit explícitamente, se hace.
- El usuario prueba en su Vita y manda logs y volcados; describe lo que ve (parpadeos,
  colores, texturas faltantes).

## 2. Compilar y probar

| Qué | Cómo |
|---|---|
| Plugin | toolchain pspdev en el PATH; `make ADRENALINE=<ruta a Adrenaline/cef>` (necesita `include/systemctrl.h` y `lib/libpspsystemctrl_kernel.a` de TheOfficialFloW/Adrenaline). Sale `ge_patch.prx`. |
| Tests en PC | `make -C tests` (gcc con `-m32`, Ubuntu: `gcc-multilib`). Hoy: **115 comprobaciones, 0 fallas** (dev49 quitó los tests de lo que se eliminó). Simulan listas del GE con `tests/stubs.c` y memoria del PSP mapeada. CI en `.github/workflows/ci.yml`. |
| Benchmark | `make -C tests bench` (lista tipo Gran Turismo). |
| Versión | `GEPATCH_VERSION` en `log.h` (subirla en cada entrega a la Vita: dev45 → dev46…). |
| Instalar en la Vita | `ux0:/pspemu/seplugins/ge_patch.prx` y en `game.txt`: `ms0:/seplugins/ge_patch.prx 1`. "Force high memory layout" de Adrenaline **apagado**. |

En la consola:
- **L al arrancar el juego:** modo pasivo (sin parches; sirve de referencia).
- **L+R+START:** menú de diagnóstico con las opciones (`menu.c`). Se guardan por juego en `ms0:/gepatch_<ID>.txt`.
- **L+R+SELECT:** captura de texto en el log y volcado binario `ms0:/gepatch_<ID>_<frame>.gpd` de **3 frames seguidos** (desde dev49: lo que un frame dibuja en un render target, como el retrovisor, suele usarse en el siguiente).
- **Log:** `ms0:/ge_patch.log` (el anterior queda en `ge_patch_prev.log`).

## 3. Hechos de la plataforma que costaron caro (no olvidar)

- **La VRAM del PSP en la Vita es de 2 MB.** `sceGeEdramSetSize(4 MB)` falla (0x80000104).
  - 0x04200000–0x043FFFFF es un **reflejo**: lo que el GE dibuja ahí cae en `dir − 2 MB`; lo que se lee ahí sale permutado (`^0x8040` en esa zona).
  - Los 2 MB están llenos: buffer grande 960x544 565 en 0x04000000 y su z-buffer en 0x04100000 (0xFF000 bytes). **No hay VRAM libre** para render targets.
- **El alfa del destino** solo se escribe vía stencil en el GE (la reducción usa stencil REPLACE 0xff).
- Una **lista con contexto** encolada desde el plugin congela Adrenaline; LoadExec desde un juego o escribir más allá de la VRAM (0x04800000) cierran Adrenaline.
- El GE **no puede dibujar en la RAM**. Lo que no cabe en la VRAM se copia a la RAM con TRANSFER.
- Adrenaline lee la imagen nativa de **0x0A400000** (960x544 565) cuando `*(u32*)0xABCDEF00 != 0` (DRAW_NATIVE). Sin sincronizar con el flip.
- Adrenaline corre a 333 MHz. El costo del plugin es casi todo el **recorrido de listas** (~300 ns por comando en el Allegrex).

## 4. Mapa de memoria del plugin (RAM 0x0A000000–0x0B000000, libre en Adrenaline)

| Dirección | Uso |
|---|---|
| 0x0A000000 | **VRAM falsa** (4 MB): `sceGeEdramGetAddr/GetSize` parcheados; el juego cree que es su VRAM |
| 0x0A400000 | Imagen nativa 960x544 que muestra Adrenaline (DISPLAY_BUFFER) |
| 0x0A500000 | LAYER_SAVE: escena guardada durante el intercambio (960x544 565) |
| 0x0A600000 | MEMO_ADDR: tabla de parches (palabra original de cada comando parchado) |
| 0x0A700000 | VMEMO: vértices 2D duplicados; 0x0A780000 WC_ADDR (caché de sublistas) |
| 0x0A800000 | RENDER_LIST: listas propias del GE; +0x8000 anillo de 64 slots de 1 KB (`txSlot`) |
| 0x0A900000 | Imagen del menú |
| 0x0A980000 | Buffer del volcado .gpd; 0x0AA00000–0x0AE00000 instantáneas del volcado |
| 0x0AE00000 | Recorte 3D (`sub.h`): coordenadas de recorte (64 KB), banderas (4 KB) y anillo de vértices nuevos hasta 0x0AF00000 |
| 0x0AF40000 | OVL_BUF: dibujos de la CPU (spinner) al doble |
| 0x0AF60000 | Z_SAVE (hasta 0x0B000000, 640 KB): z-buffer guardado mientras se usa su VRAM para un rt |
| 0x0B000000 | Paquete flash0 de Adrenaline: **no tocar** |

## 5. Arquitectura del plugin (`main.c` ~3300 líneas, `sub.h`, `dump.c`, `menu.c`, `log.c`, `gu.c`)

- **Ganchos:** `sctrlHENPatchSyscall` sobre `sceGeListEnQueue` y `sceGeListUpdateStallAddr` → `patchGeList(list, stall)` recorre los comandos nuevos antes de que el GE los ejecute. También parchea `sceGeEdramGetAddr/GetSize` y el flip (`copyFrameBuffer` / `dumpOnFlip`).
- **Parches reversibles:** `setOp(p, orig, op)` escribe la palabra y recuerda la original (`origOp`) en la tabla MEMO. Las listas se recorren varias veces (juegos con listas estáticas), así que cada decisión se recalcula en cada recorrido y se restaura lo que ya no aplica.
- **Slots:** `txSlot()` da 1 KB del anillo; `txCall(list, reg, sl)` cambia la palabra anterior al draw por BASE y el draw por CALL a la lista del plugin. Así se insertan comandos "en su lugar" sin mover la lista del juego. La lista del slot repite la palabra reemplazada.
- **Framebuffers (fb):** cada FRAMEBUFPTR/WIDTH se clasifica en `state.framebuf_addr[16]` y queda en uno de estos destinos:
  - **buffer grande** (pantalla al doble);
  - **ignorado** (draws omitidos con NOP);
  - **rt a escala 1 en la VRAM del z** (`rt1`, dev41+, ver abajo).
  - (dev49 quitó lo que nunca se usaba en la Vita: render targets al doble en la VRAM alta, fb "intermedios" como render target, el fb de copia a escala 1 (csrc), la copia reducida al fb mostrado, el recorrido de listas antiguo y la prueba de VRAM de 4 MB. Está en el historial de git si hiciera falta.)
- **Escalado:** viewport, scissor y región ×2; vértices 2D (through) duplicados en VMEMO.
- **Texturas** (`texCheck`/`texCheckCalc`, con caché `tcache`). Valores de `texmode`:
  - **0** normal;
  - **1** omitir el draw;
  - **3** textura de la VRAM real llevada a la falsa;
  - **4** el fb actual;
  - **6** mezcla del pase por intercambio (`swapComposite`);
  - **7** rt1 que lee la pantalla (`rt1FromBig`, por cuartos del buffer grande);
  - **8** rt1 que se lee a sí mismo (`rt1SelfDraw`: la textura cambia solo para ese draw);
  - **9** la pantalla (buffer grande) que se lee a sí misma (`rt1FromBig` con los vértices ya al doble; arregló los cuadrados morados de SH).
- **Copias (TRANSFER):**
  - Las del juego a la VRAM se escalan con el GE (`txUpscale`, en franjas).
  - Las copias desde la pantalla se toman del buffer grande reducido (`emitDownscale` a 0x04100000 y luego a la RAM).
  - Las copias chicas (≤ 8 por frame) se dibujan al doble.
  - `fb_txbox` recuerda zonas copiadas.
- **rt1, render targets a escala 1 (dev41–dev45):** un fb que no es la pantalla, de pitch 16–479, se dibuja a escala 1 en la VRAM del z (0x04100000).
  - **Al entrar** (`rt1Words`): las palabras FBPTR/FBWIDTH pasan a un CALL que guarda el z en Z_SAVE si el buffer grande ya usó profundidad, carga el contenido previo desde la VRAM falsa y fija una profundidad propia.
  - **Al salir** (`rt1Emit`): copia el rt a su lugar en la VRAM falsa, restaura el z y vuelve al buffer grande.
  - Las texturas que lo leen van a la VRAM falsa.
  - Opción del menú `rt_esc1` (activa por defecto).
- **Intercambio** (pases de luz de Silent Hill, fb 0a170000):
  1. `swapLearn` detecta un fb nunca mostrado que se pega sobre la pantalla.
  2. Desde el frame siguiente `swapEnter` guarda la escena en LAYER_SAVE y el pase se dibuja en el buffer grande.
  3. `swapComposite` mezcla la escena encima con la mezcla invertida (`blendSwap`).
  4. Si no se puede, `swapRestore` devuelve la escena y `swapBackoff` espera 2, 4, 8… frames (máximo 600) antes de reintentar.
- **Recorte de triángulos 3D** (`sub.h`, dev47–dev48, opción `recorte3d`): el GE descarta un triángulo entero si un vértice cae fuera de 0..4096 en pantalla; al doble, los triángulos grandes cerca de la cámara (el camino bajo el auto en RR) desaparecían. GePatch sigue las matrices del juego (`mtxCmd`), calcula en la CPU dónde caen los vértices de cada draw 3D (primero las 8 esquinas de su caja) y, si un triángulo que el juego dibuja a 1x se sale al doble, lo recorta contra el plano cercano (con el recorte de profundidad del juego) y un rango seguro 128..3968 (el GE calcula con floats de 24 bits: en el borde exacto también descartaba). El draw pasa a un CALL a su lista con los triángulos nuevos. Formatos: sin pesos ni morph; posición s8/s16/float; color 16 bits u 8888.
- **Dibujos de la CPU** (`cpuOverlay`): el spinner de carga de SH lo dibuja la CPU en el fb mostrado; se detecta el recuadro y se pega al doble.
- **Volcado .gpd** (`dump.c`): 3 frames seguidos (`DUMP_FRAMES`): comandos, draws, eventos del log, VRAM real y falsa al inicio y al final, destinos de copias e instantáneas. Pesa ~3 veces más (40–50 MB).
- **Log** (`log.c`): hilo propio con doble buffer. `LOG_ONCE(key, ...)` escribe una sola vez; `LOG_ONCE_CAP` además dispara una captura automática (máximo 5).

## 6. Herramientas

- **`tools/gpd.py`** (solo biblioteca estándar): `resumen`, `informe` (todos los comandos juego → plugin, con draws y eventos), `imagenes` (PNG de fbs, buffer grande, rts, copias, texturas), `ppsspp` (a .ppdmp) y `comparar` (pasivo contra activo). Desde dev49, para diagnosticar con pocos tokens (ver `estrategia_diagnostico.md`):
  - `linaje VOLCADO`: quién escribe cada fb (destino y tramos de draws, por frame `f0..f2`) y quién lo lee como textura, con el último draw que lo escribió (también del frame anterior); avisa de viewports 3D cuya escala no es la de su destino (x2 en la pantalla, x1 en rt1).
  - `diff ACTIVO [PASIVO]`: registros críticos (viewport, offset, scissor, región, fb, z, textura, tipo de vértice) que el plugin cambió, por tramos de draws y solo lo que cambia de un tramo a otro (~50 líneas); marca los draws reemplazados por CALL (recorte 3D, rt1, intercambio). Con PASIVO, los draws donde el juego mismo mandó otros valores.
  - `vram VOLCADO`: mapa de la VRAM falsa (fb y texturas, con choques) y cuándo se guardó o restauró la VRAM del z (rt1/Z_SAVE) respecto de los draws con profundidad.
  - `original VOLCADO CARPETA [--ppsspp=PPSSPPHeadless]`: el mismo frame **sin GePatch**, rearmado desde un volcado activo (comandos originales + VRAM falsa) y dibujado por PPSSPPHeadless. Deja `comparacion.png` (sin GePatch | con GePatch reducido | diferencias). Es fiel en lo que se dibuja dentro del frame (Ridge Racer sale limpio). Avisa de los fb que GePatch no deja en la VRAM falsa (pases ignorados o por intercambio): su contenido previo es ruido. Le pasa a la linterna de Silent Hill.
  - Mejora posible: repetir la reproducción usando como contenido previo el resultado de la primera.
- **`tools/vitabridge.py`** (relevo; lo corre el usuario en la Terminal del Mac, carpeta `~/Downloads/vitabridge`, Vita en 192.168.0.15 con vitacompanion: FTP 1337, comandos 1338):
  - `enviar/ux0/...` → se sube a la Vita.
  - `cmd_*.txt` → comandos (`launch`, `press`, `wait`…).
  - `bajar_*.txt` → baja rutas a `recibidos/`, o "ruta -> destino".
  - `listar_*.txt` → listados de carpetas.
  - Baja solo los `ge_patch*.log` y `*.gpd` nuevos.
  - `estado.txt` es el latido.
  - Al copiar archivos al Mac, usar nombres únicos en el origen: la herramienta de copia cachea por nombre.
- **`tools/geptest`:** las 112 pruebas del GE de pspautotests corridas en la Vita en un proceso. Resultado: el GE de la Vita se comporta como el de una PSP, salvo lo de Adrenaline como sistema (ver `geptest_resultados.md`).
- **`tools/ppsspp-vita` (modelo PPSSPP-Adrenaline):** `adrenaline.patch` sobre hrydgard/ppsspp **1adf3c5**; se activa con `PPSSPP_ADRENALINE=1` (o `=estricto`).
  - Carga GePatch **desde el .prx** vía `seplugins/game.txt`.
  - 64 MB con partición de usuario de 32 MB.
  - Stubs de systemctrl.
  - VRAM de 2 MB con reflejos permutados en todos los accesos (por eso usa el intérprete IR, no el JIT).
  - 333 MHz.
  - `memset`/`memmove` que devuelven el destino.
  - Las capturas headless dejan además `X_nativo.bmp`.
  - Uso: `modelo.sh PPSSPPHeadless MEMSTICK JUEGO [opciones]`.
- **`tools/geptest/escena` (GEPVM):** escena determinista (render a textura, alfa, paleta, copias, cubo 3D, recuadro de la CPU, RT2 de 64x64 que lee la pantalla y acumula). `kcap.prx` guarda capturas en los frames 5/30/60/90/120 y `comparar_vm.py VITA MODELO [REF] [html]` las compara.
  - **Resultado:** Vita y modelo idénticos byte a byte (dev40–dev43). Con el modelo, los bugs de la escena se depuran sin la consola.
- **Laboratorio en el Mac** (`~/Documents/gepatch-lab`, ver `laboratorio_mac.md`): PPSSPP con interfaz + parche para correr SH y RR mirando la imagen nativa. **Compilado y funcionando**: da lo mismo que la Vita, salvo algo de rendimiento. `jugar.sh` abre con `--debugger=45678` (API WebSocket de PPSSPP, para un futuro control desde Claude). Los volcados quedan en `memstick/`. Usa el renderer por software a propósito (es el que imita al GE byte a byte).

## 7. Historial resumido

- **v0.2 original:** buffer grande, VRAM falsa, recorrido DFS.
- **dev1–dev20:** recorrido corregido y optimizado; menú; modo pasivo; videos de SH escalados con el GE; captura L+R+SELECT.
- **dev21–dev30:** sublistas sin cambios saltadas; copias desde la pantalla; formulario de SH; volcado .gpd.
- **dev31–dev33:** descubrimiento de la VRAM de 2 MB; alfa en la reducción (formulario de SH arreglado, commit e4530d5).
- **dev34–dev40:** puerta y linterna de SH por intercambio; spinner de la CPU; GEPTEST.
- **dev41:** rt1 (rt a escala 1 en la VRAM del z), copias chicas, cpuOverlay 565/8888, modelo PPSSPP-Adrenaline y GEPVM.
- **dev42:** guardado del z (Z_SAVE) → en RR aparece el auto. **dev43:** SH sin parpadeo; RR con artefactos morados.
- **dev44:** carga del contenido previo del rt1; `swapRestore`; espera fija de 10 s tras fallar el intercambio.
- **dev45:** RR sin franjas moradas (`rt1SelfDraw`: el rt que se lee a sí mismo ya no deja parchadas las palabras de textura del juego). SH: espera creciente al fallar el intercambio (el auto sin texturas por 10 s). Confirmado en la Vita.
- **dev46:** SH sin cuadrados morados: el pase de luz en el buffer grande se leía a sí mismo en la dirección del juego (el z-buffer). Confirmado en el laboratorio. `gpd.py original` (el mismo frame sin GePatch).
- **dev47:** recorte 3D con subdivisión en 4 (no alcanzaba: la esquina que salía nunca entraba en el rango).
- **dev48:** recorte exacto contra el plano cercano y el rango seguro (comprobado reproduciendo un volcado de RR en el modelo: el camino vuelve). **Efecto secundario abierto:** el retrovisor de RR (cámara en tercera persona) ya no muestra nada.
- **dev49:** volcado de 3 frames, `gpd.py linaje/diff/vram`, limpieza de código sin uso (ver sección 5), documentos al día.

## 8. Abierto / próximos pasos

1. **RR, retrovisor vacío con dev48** (el usuario pidió no resolverlo todavía). Para investigarlo: volcado de 3 frames con el retrovisor visible y `gpd.py linaje` + `diff` (¿lo dibuja un draw con CALL de recorte? ¿viewport/scissor del retrovisor?).
2. **RR:** confirmar en la Vita que el camino quedó bien con dev48/dev49 (en el laboratorio mejoró mucho).
3. **SH:**
   - **Linterna:** mezcla 0x032 del pase de luz; el plan es hacer el pase en 4444 y la mezcla en la zona del z.
   - **Pases de 1024 de ancho ignorados** (~250 draws por frame, p. ej. fb 0a04c000 con textura 08bb3600).
   - Leer en el log el motivo de los fallos del intercambio ("intercambio: la mezcla no se puede hacer…").
4. **Rendimiento:** en SH, 18–22 fps en escenas pesadas (plugin 4–9 ms por frame); GT, 20–30 fps. Idea grande: recorrer las listas en el ARM de la Vita por Kermit (`KERMIT_MODE_EXTRA_1` libre; ver `adrenaline_notas.md`).
5. **Laboratorio en el Mac:**
   - Fase 2: monitor por WebSocket (`--debugger=45678`) y comandos de Claude: pausar, guardar/cargar estado, capturar, leer/escribir la memoria del plugin.
   - Fase 3: repetir el mismo frame con otros parámetros (estado guardado + tabla de ajustes del plugin) y regresión entre versiones.

## 9. Flujo de trabajo que funcionó

1. El usuario corre GEPVM, luego los juegos; captura con L+R+SELECT donde ve el error.
2. Se leen `ge_patch.log` y los `.gpd`: primero `gpd.py original` (¿el juego sin GePatch se ve bien?), `linaje`, `diff` y `vram` (resúmenes cortos); recién después `informe` e `imagenes` para el draw concreto (estado del GE, palabras parchadas — "parche posterior" — y lo que lee o escribe).
   - Para probar una idea sin compilar el plugin, se puede simular en Python sobre el volcado (reproducir con viewport x2 en PPSSPPHeadless): así se encontró el recorte 3D.
3. Se corrige con un test nuevo en `tests/test_main.c`, se corre GEPVM en el modelo (comparando con el modelo de la versión anterior: debe seguir igual o mejor) y se sube el `.prx` con el relevo.
4. Un evento del log solo es un error si coincide con una diferencia en la imagen.

## 10. Documentos

| Archivo | Contenido |
|---|---|
| `diagnostico.md` | Volcado .gpd, relevo, VRAM de 2 MB, hallazgos de SH, análisis dev45–dev48 |
| `estrategia_diagnostico.md` | Propuesta de diagnóstico con pocos tokens (ráfaga de 3 frames, linaje, diff, VRAM) y su estado |
| `adrenaline_notas.md` | Cómo funciona Adrenaline, mapa de memoria, rendimiento, Kermit |
| `modelo_vita.md` | Diseño del modelo de pruebas (GEPTEST, grabación, perfil Vita) |
| `geptest_resultados.md` | Resultados de las 112 pruebas del GE en la Vita |
| `modelo_adrenaline.md` | Modelo PPSSPP-Adrenaline, GEPVM y verificación byte a byte |
| `laboratorio_mac.md` | Laboratorio en el Mac (PPSSPP con interfaz, monitor, inputs) |
| `../tools/ppsspp-vita/README.md`, `../tools/geptest/README.md` | Uso de las herramientas |
| `gepatch_compatibilidad.csv` | Lista de compatibilidad original de GePatch |
