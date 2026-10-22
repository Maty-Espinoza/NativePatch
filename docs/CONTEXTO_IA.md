# Contexto para continuar GePatch (traspaso a otra IA)

Actualizado el 2026-10-22 (GePatch 0.2-dev45, commit `1c05908` en la rama `dev`). Léelo
entero antes de tocar código; los demás documentos de `docs/` amplían cada tema.

## 1. Qué es y qué se busca

- **GePatch** (`ge_patch.prx`) es un plugin de kernel para **Adrenaline** (el emulador oficial
  de PSP dentro de la PS Vita). Hace que los juegos de PSP se vean a **960x544** (el doble de
  480x272) parchando las display lists del GE (la GPU del PSP) antes de que se ejecuten.
- El GE de la Vita es hardware real de PSP: el plugin no emula nada, reescribe comandos.
- Este repo (`Maty-Espinoza/nativepatch`, fork del GePatch original de TheFlow, abandonado
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
| Tests en PC | `make -C tests` (gcc con `-m32`, Ubuntu: `gcc-multilib`). Hoy: **171 comprobaciones, 0 fallas**. Simulan listas del GE con `tests/stubs.c` y memoria del PSP mapeada. CI en `.github/workflows/ci.yml`. |
| Benchmark | `make -C tests bench` (lista tipo Gran Turismo). |
| Versión | `GEPATCH_VERSION` en `log.h` (subirla en cada entrega a la Vita: dev45 → dev46…). |
| Instalar en la Vita | `ux0:/pspemu/seplugins/ge_patch.prx` y en `game.txt`: `ms0:/seplugins/ge_patch.prx 1`. "Force high memory layout" de Adrenaline **apagado**. |

En la consola:
- **L al arrancar el juego:** modo pasivo (sin parches; sirve de referencia).
- **L+R+START:** menú de diagnóstico con las opciones (`menu.c`). Se guardan por juego en `ms0:/gepatch_<ID>.txt`.
- **L+R+SELECT:** captura de texto en el log y volcado binario `ms0:/gepatch_<ID>_<frame>.gpd`.
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
| 0x0A980000 | Buffer del volcado .gpd; 0x0AA00000–0x0AF00000 instantáneas del volcado |
| 0x0AF40000 | OVL_BUF: dibujos de la CPU (spinner) al doble |
| 0x0AF60000 | Z_SAVE (hasta 0x0B000000, 640 KB): z-buffer guardado mientras se usa su VRAM para un rt |
| 0x0B000000 | Paquete flash0 de Adrenaline: **no tocar** |

## 5. Arquitectura del plugin (`main.c`, ~3500 líneas)

- **Ganchos:** `sctrlHENPatchSyscall` sobre `sceGeListEnQueue` y `sceGeListUpdateStallAddr` → `patchGeList(list, stall)` recorre los comandos nuevos antes de que el GE los ejecute. También parchea `sceGeEdramGetAddr/GetSize` y el flip (`copyFrameBuffer` / `dumpOnFlip`).
- **Parches reversibles:** `setOp(p, orig, op)` escribe la palabra y recuerda la original (`origOp`) en la tabla MEMO. Las listas se recorren varias veces (juegos con listas estáticas), así que cada decisión se recalcula en cada recorrido y se restaura lo que ya no aplica.
- **Slots:** `txSlot()` da 1 KB del anillo; `txCall(list, reg, sl)` cambia la palabra anterior al draw por BASE y el draw por CALL a la lista del plugin. Así se insertan comandos "en su lugar" sin mover la lista del juego. La lista del slot repite la palabra reemplazada.
- **Framebuffers (fb):** cada FRAMEBUFPTR/WIDTH se clasifica en `state.framebuf_addr[16]` y queda en uno de estos destinos:
  - **buffer grande** (pantalla al doble);
  - **ignorado** (draws omitidos con NOP);
  - **render target** (desactivados en la Vita por falta de VRAM);
  - **rt a escala 1 en la VRAM del z** (`rt1`, dev41+, ver abajo);
  - **csrc** (fb a escala 1 hasta que el juego lo copia).
- **Escalado:** viewport, scissor y región ×2; vértices 2D (through) duplicados en VMEMO.
- **Texturas** (`texCheck`/`texCheckCalc`, con caché `tcache`). Valores de `texmode`:
  - **0** normal;
  - **1** omitir el draw;
  - **2** textura en un render target;
  - **3** textura de la VRAM real llevada a la falsa;
  - **4** el fb actual;
  - **5** la pantalla leída desde un rt (`txDrawFromScreen`);
  - **6** mezcla del pase por intercambio (`swapComposite`);
  - **7** rt1 que lee la pantalla (`rt1FromBig`);
  - **8** rt1 que se lee a sí mismo (`rt1SelfDraw`).
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
- **Dibujos de la CPU** (`cpuOverlay`): el spinner de carga de SH lo dibuja la CPU en el fb mostrado; se detecta el recuadro y se pega al doble.
- **Volcado .gpd** (`dump.c`): comandos, draws, eventos del log, VRAM real y falsa al inicio y al final, destinos de copias e instantáneas.
- **Log** (`log.c`): hilo propio con doble buffer. `LOG_ONCE(key, ...)` escribe una sola vez; `LOG_ONCE_CAP` además dispara una captura automática (máximo 5).

## 6. Herramientas

- **`tools/gpd.py`** (solo biblioteca estándar): `resumen`, `informe` (todos los comandos juego → plugin, con draws y eventos), `imagenes` (PNG de fbs, buffer grande, rts, copias, texturas), `ppsspp` (a .ppdmp) y `comparar` (pasivo contra activo).
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
- **Laboratorio en el Mac** (`~/Documents/gepatch-lab`, ver `laboratorio_mac.md`): PPSSPP con interfaz + parche para correr SH y RR mirando la imagen nativa. Pendiente de compilar (faltaban SDL3 y SDL3_ttf; el script ya los instala).

## 7. Historial resumido

- **v0.2 original:** buffer grande, VRAM falsa, recorrido DFS.
- **dev1–dev20:** recorrido corregido y optimizado; menú; modo pasivo; videos de SH escalados con el GE; captura L+R+SELECT.
- **dev21–dev30:** sublistas sin cambios saltadas; copias desde la pantalla; formulario de SH; volcado .gpd.
- **dev31–dev33:** descubrimiento de la VRAM de 2 MB; alfa en la reducción (formulario de SH arreglado, commit e4530d5).
- **dev34–dev40:** puerta y linterna de SH por intercambio; spinner de la CPU; GEPTEST.
- **dev41:** rt1 (rt a escala 1 en la VRAM del z), copias chicas, cpuOverlay 565/8888, modelo PPSSPP-Adrenaline y GEPVM.
- **dev42:** guardado del z (Z_SAVE) → en RR aparece el auto. **dev43:** SH sin parpadeo; RR con artefactos morados.
- **dev44:** carga del contenido previo del rt1; `swapRestore`; espera fija de 10 s tras fallar el intercambio.
- **dev45** (en la Vita, **sin confirmar**):
  - **RR:** las franjas moradas venían de `rt1SelfTex`, que parchaba las palabras de textura del juego hacia la VRAM del z y el brillo a pantalla completa siguiente las heredaba. Ahora `rt1SelfDraw` cambia la textura solo para ese draw.
  - **SH:** el auto quedaba sin texturas porque el pase quedaba fuera 10 s tras un fallo. Ahora la espera es creciente, una copia con color negro cuenta como copia y el motivo del fallo va al log.

## 8. Abierto / próximos pasos

1. **Confirmar dev45 en la Vita** (RR y SH). Si se confirma, hacer el commit siguiente.
2. **RR:** camino de la pista que no se renderiza (anterior a dev44; revisar con un volcado nuevo). Mapa de entorno (fb 0a1fe000 de 64x64 en rt1, leído por 175 draws).
3. **SH:**
   - **Cuadrados morados** intermitentes: nunca capturados; sospechas: cpuOverlay o reflejo del z.
   - **Linterna:** mezcla 0x032 del pase de luz; el plan es hacer el pase en 4444 y la mezcla en la zona del z.
   - **Pases de 1024 de ancho ignorados** (~250 draws por frame, p. ej. fb 0a04c000 con textura 08bb3600).
   - Leer en el log el motivo de los fallos del intercambio ("intercambio: la mezcla no se puede hacer…").
4. **Rendimiento:** en SH, 18–22 fps en escenas pesadas (plugin 4–9 ms por frame); GT, 20–30 fps. Idea grande: recorrer las listas en el ARM de la Vita por Kermit (`KERMIT_MODE_EXTRA_1` libre; ver `adrenaline_notas.md`).
5. **Laboratorio en el Mac:**
   - Compilar.
   - Bajar las ISO de SH y RR.
   - Fase 2: monitor por WebSocket y comandos.
   - Fase 3: replay de inputs y regresión entre versiones.

## 9. Flujo de trabajo que funcionó

1. El usuario corre GEPVM, luego los juegos; captura con L+R+SELECT donde ve el error.
2. Se leen `ge_patch.log` y los `.gpd` (`gpd.py informe` e `imagenes`). Se busca el draw concreto: estado del GE, palabras parchadas ("parche posterior" en el informe) y lo que lee o escribe.
3. Se corrige con un test nuevo en `tests/test_main.c`, se corre GEPVM en el modelo (comparando con el modelo de la versión anterior: debe seguir igual o mejor) y se sube el `.prx` con el relevo.
4. Un evento del log solo es un error si coincide con una diferencia en la imagen.

## 10. Documentos

| Archivo | Contenido |
|---|---|
| `diagnostico.md` | Volcado .gpd, relevo, VRAM de 2 MB, hallazgos de SH, análisis dev45 |
| `adrenaline_notas.md` | Cómo funciona Adrenaline, mapa de memoria, rendimiento, Kermit |
| `modelo_vita.md` | Diseño del modelo de pruebas (GEPTEST, grabación, perfil Vita) |
| `geptest_resultados.md` | Resultados de las 112 pruebas del GE en la Vita |
| `modelo_adrenaline.md` | Modelo PPSSPP-Adrenaline, GEPVM y verificación byte a byte |
| `laboratorio_mac.md` | Laboratorio en el Mac (PPSSPP con interfaz, monitor, inputs) |
| `../tools/ppsspp-vita/README.md`, `../tools/geptest/README.md` | Uso de las herramientas |
| `gepatch_compatibilidad.csv` | Lista de compatibilidad original de GePatch |
