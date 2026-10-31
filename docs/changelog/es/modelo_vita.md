# Modelo de la Vita para pruebas: viabilidad y diseño

Escrito el 2026-10-22, a pedido del usuario.

**Idea:** medir en la consola cómo se comporta de verdad el GE dentro de Adrenaline, con pruebas controladas y grabaciones más largas que un frame. Correr lo mismo en PPSSPP headless, comparar, y ajustar PPSSPP con un "perfil Vita" hasta que coincidan. Así se podría probar el plugin sin la consola.

## Viabilidad: sí, por partes

**Lo que ya existe y reduce el trabajo:**
- **pspautotests** (repositorio de pruebas de PPSSPP; ya está en el clon local, en `ppsspp/pspautotests`).
  - Trae 112 pruebas del GE (`tests/gpu/*`: rendertarget, transfer, stencil, dither, ge/edram, ge/edramswizzle, ge/context, displaylist, timing…).
  - Cada prueba tiene su resultado esperado (`.expected`) medido en una PSP real y el `.prx` ya compilado. Se generan con `gentest.py` y psplink.
  - Ejemplos:
    - `edramswizzle.expected` describe los reflejos de la VRAM con direcciones permutadas, lo mismo que se vio en la Vita.
    - `context.expected` describe las listas con contexto, las mismas que congelaron el juego en Adrenaline.
- **Toolchain del PSP** (pspdev) y **PPSSPPHeadless** ya compilados en el entorno. Headless corre esos `.prx` directo y compara con `.expected`.
- **El relevo** (vitacompanion + `vitabridge.py`): sube archivos, baja resultados y simula botones.
- **Los volcados .gpd** y su conversión a `.ppdmp`.

**Comparación a tres bandas:** PSP real (`.expected`) vs Vita/Adrenaline (nuevo) vs PPSSPP.
- Donde la Vita difiere de la PSP: es Adrenaline. Esos son los datos que faltan.
- Donde PPSSPP difiere de la Vita: es lo que el perfil Vita tiene que corregir.

**Límites (para no prometer un modelo exacto):**
- **Tiempos y concurrencia:** cuándo corre cada lista respecto del flip, interrupciones, latencias de Kermit. No se pueden reproducir ciclo a ciclo. Se miden y se modelan como orden de eventos, no como tiempo exacto.
- **Aritmética del GE:** filtros, mezcla, dither. El renderer por software de PPSSPP es casi exacto en 16 bits, pero no en todo. Algunas pruebas necesitan tolerancia.
- **Caché de la CPU:** PPSSPP la ignora. Los errores por caché del plugin no se verían.
- **El plugin no corre dentro de PPSSPP:** usa systemctrl de Adrenaline y llamadas de kernel. Lo que sí se puede: reproducir en PPSSPP las listas que el plugin dejó (incluidas sus listas propias) y comparar con lo que mostró la consola.

## Diseño

### A. Batería de pruebas en la consola (geptest)
- **Un EBOOT de Adrenaline** (`ux0:/pspemu/PSP/GAME/GEPTEST/EBOOT.PBP`) que carga uno por uno los `.prx` de pspautotests/gpu, más pruebas propias. La salida de cada prueba va a `ms0:/geptest/<prueba>.txt` y, si corresponde, a una captura `.bmp`.
  - **Opción A** (se decide al implementarlo): recompilar las pruebas con `common` modificado para escribir a archivo.
  - **Opción B:** redirigir la salida estándar desde el cargador.
- **Seguridad:**
  - Cada prueba tiene un vigilante: `sceGeListSync` con tiempo límite, y después `sceGeBreak` y reinicio. Una prueba colgada queda anotada como "CUELGA" y no detiene las demás.
  - Los resultados se escriben en sync, así que un cierre deja lo hecho hasta ese momento.
  - Las pruebas que pueden colgar Adrenaline van al final y se piden aparte.
- **Pruebas propias**, por los problemas que ya aparecieron:
  1. Mapa de la VRAM: escritura y lectura por CPU, GE, transferencia y textura en 0x04000000–0x047FFFFF.
  2. Escritura del alfa según stencil, máscara y formato.
  3. TEXFLUSH y TEXSYNC al leer lo recién dibujado.
  4. Copias RAM↔VRAM y RAM↔RAM con distintos anchos y alineaciones.
  5. Listas con contexto, encolar al frente y stall.
  6. Orden entre listas y flips (marca de tiempo por evento).
  7. Dibujar en la RAM: confirmar que el GE no puede.
- **Flujo:** subo el EBOOT por el relevo. Tú lo abres una vez desde Adrenaline. Corre todo solo (unos minutos) y el relevo baja `ms0:/geptest/*`.

### B. Grabación larga en el plugin
- **Modo "grabar N frames"** (300 por defecto), liviano. Por cada lista guarda:
  - dirección, stall y tiempos;
  - un resumen de cada comando (hash y cambios de estado);
  - el encabezado de cada draw, las decisiones del plugin, los flips con su hora y lo que pasó entre ellos (por ejemplo, las dos listas por flip que hacían parpadear la puerta).
- **Volcado completo:** cada K frames (por ejemplo 30) se agrega un `.gpd` completo, con datos y VRAM.
- **Memoria y escritura:** buffer en la RAM libre (0x0AA00000–0x0AF00000), escrito por el hilo del log.
- **`gpd.py`:**
  - `linea` dibuja la línea de tiempo (listas, flips, intercambios, copias) y avisa de patrones raros.
  - `ppsspp` acepta varios frames seguidos (un `.ppdmp` con varios DISPLAY) para ver una secuencia y no solo un frame.

### C. Perfil Vita en PPSSPP (el intermediario)
- **Fork de PPSSPP en un commit fijo.** Los cambios viven como parches en el repo (`tools/ppsspp-vita/*.patch`) y se activan con una opción de headless (`--vita`).
- **Cada ajuste está ligado a una prueba de A que falla.** Candidatos (a confirmar con datos):
  - VRAM de 2 MB con los reflejos medidos en Adrenaline;
  - error o cuelgue al encolar con contexto;
  - orden de listas y flips según lo grabado en B;
  - memoria de Adrenaline: 0x0A000000–0x0B000000, flash0, scratchpad.
- **Ejecutor `vitamodel.py`:**
  1. Corre la batería en PPSSPP con y sin `--vita`.
  2. La compara con los resultados de la consola y con la PSP real.
  3. Arma un informe HTML por prueba (`frametests.py` de PPSSPP ya hace algo parecido para frame dumps y sirve de base).
- **Meta:** que todas las pruebas relevantes coincidan.
  - Desde ahí, `gpd.py ppsspp --plugin` puede incluir también las listas propias del plugin, lo que hoy no se puede.
  - Cada volcado de la consola se reproduce en el modelo y se compara píxel a píxel con lo que mostró la Vita.

### Orden propuesto y esfuerzo estimado
1. **A con las 112 pruebas existentes**, sin agregar nada. Mide qué tan lejos está PPSSPP de la Vita; puede que la distancia sea chica. Estimado: 1 sesión.
2. **Pruebas propias de A** para los problemas conocidos. Estimado: 1 sesión.
3. **B, grabación larga.** Estimado: 1 sesión.
4. **C, perfil Vita**: según lo que salga de 1 y 2.
