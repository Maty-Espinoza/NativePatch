# Adrenaline: cómo funciona y qué recursos usa GePatch

Fuente: código de Adrenaline 7 (TheOfficialFloW/Adrenaline, commit 7d382b7). Revisado el 2026-10-21.

## Arquitectura
- La Vita trae el hardware del PSP dentro de su chip: CPU MIPS Allegrex y GE (motor gráfico). El código del juego y el de GePatch corren en ese MIPS real, no en un emulador por software.
- El MIPS no tiene acceso directo a la pantalla, la tarjeta de memoria, el audio ni los controles. Todo eso lo atiende el ARM de la Vita a través de llamadas remotas ("Kermit").
- **Pantalla:** un hilo en el ARM (`user/menu.c`, `AdrenalineDraw`) dibuja con la GPU de la Vita (vita2d):
  - Normal: copia el framebuffer del PSP (480x272, en `0x74000000` del lado ARM) y lo escala.
  - Si `DRAW_NATIVE` (`0xABCDEF00`) ≠ 0: usa como textura `NATIVE_FRAMEBUFFER` = `0x0A400000` en la RAM del PSP, de 960x544 en formato 565 BGR, a escala 1.
  - El ARM ve la RAM del PSP en `0x68000000 + (addr & 0x0FFFFFFF)`.
  - La lectura del buffer nativo no está sincronizada con los flips del juego: puede haber tearing si el GE escribe mientras la Vita lee.
- **CPU:** Adrenaline fija 333 MHz por defecto. Con la opción "velocidad de CPU para ISO/UMD", `SetSpeed` puede bajarla (222 MHz, etc.).

## Mapa de memoria (direcciones del PSP)
| Zona | Uso |
|---|---|
| 0x08000000–0x0C000000 | RAM de 64 MB |
| 0x08800000… | Partición del juego. Con "Force high memory layout" llega a 52 MB → hasta 0x0BC00000 |
| 0x0A000000–0x0B000000 | 16 MB que Adrenaline borra al arrancar. GePatch usa esta zona |
| 0x0B000000–0x0B920000 | Paquete flash0 (`SCE_PSPEMU_EXTRA_MEMORY`) |
| 0x0BD00000 | Scratchpad |
| 0x0BE00000 | VRAM (`SCE_PSPEMU_VRAM`) |

**Uso de GePatch dentro de 0x0A000000–0x0B000000:**
- 0x0A000000–0x0A400000: VRAM falsa. El juego cree que es la VRAM: `sceGeEdramGetAddr` y `GetSize` están parcheados para devolver esta dirección y 4 MB.
- 0x0A400000: buffer nativo 960x544 que lee Adrenaline (≈1 MB).
- 0x0A600000: memoria de parches de comandos (768 KB).
- 0x0A700000: memoria de vértices 2D (512 KB).
- 0x0A800000: listas propias del GE (copia final, copia reducida, copias escaladas en 0x0A808000).
- 0x0A900000: imagen del menú (dev18).
- 0x0A980000: buffer del volcado .gpd (512 KB); 0x0AA00000–0x0AF00000: instantáneas del volcado; 0x0AF00000: buffer de ceros (dev29+).

**Consecuencias:**
- "Force high memory layout" debe estar **apagado**. Con 52 MB el juego puede usar 0x0A000000 en adelante y chocaría con la VRAM falsa y con los buffers del plugin.
- La VRAM real (0x04000000) es de **2 MB**, no 4: `sceGeEdramSetSize(4 MB)` falla (0x80000104). Contiene el buffer grande de 960x544 (0x04000000) y su profundidad (0x04100000), y no queda espacio libre.
- 0x04200000–0x043FFFFF es un reflejo: el GE dibuja ahí sobre los 2 MB bajos y las lecturas ven los datos permutados. Antes de dev32 se usaba para render targets y eso corrompía la imagen (hallazgo del 2026-10-21, ver diagnostico.md).

## Rendimiento (medido en Gran Turismo, dev15)
- En modo pasivo muestreado (se mide 1 de cada 8 frames), el juego original corre a **59.9 fps** en carrera. Solo los frames medidos bajan de 60 fps.
- Recorrer las listas cuesta 13–22 ms por frame en el Allegrex: 40–70 mil comandos y 9–15 mil draws por frame, unos 300 ns por comando.
- Ese costo es casi todo el impacto de GePatch: con 16,6 ms disponibles por frame, el juego cae a 20–30 fps.
- Los fallos de la tabla de parches son pocos (memo≈170 por frame). Lo que domina es el recorrido mismo.
- Hay unas 1000 sublistas por frame (`CALL`), probablemente modelos estáticos. Es la mejor candidata a optimizar: no volver a recorrer una sublista que no cambió.

## Usar recursos de la Vita (análisis del 2026-10-21, sin implementar)

### Canal PSP → ARM (Kermit)
- El PSP llena una `SceKermitRequest` (comando + argumentos) y llama `sceKermitSendRequest661(req, modo, cmd, nargs, 0, &resp)`. Se bloquea hasta la respuesta, con un semáforo.
- En el ARM, un hilo espera con `ScePspemuKermitWaitAndGetRequest(modo, &req)` y responde con `ScePspemuKermitSendResponse`. Esas funciones están en offsets fijos del módulo ScePspemu (`user/main.c`, `GetFunctions`).
- Adrenaline usa `KERMIT_MODE_EXTRA_2` para sus comandos. **`KERMIT_MODE_EXTRA_1` está definido pero no se usa:** es un canal libre para GePatch.
- El ARM lee y escribe la RAM del PSP en `0x68000000 + addr`. Para no leer datos viejos de su caché usa `ScePspemuConvertAddress(addr, KERMIT_INPUT_MODE, size)` y `ScePspemuWritebackCache` al escribir. Del lado PSP hay que escribir la caché a memoria antes de pedir.

### Opciones
1. **Recorrer las listas en el ARM (lo más prometedor).**
   - El mismo `patchGeList` compilado para ARM (vitasdk), en un plugin taiHEN para ScePspemu (`*PSPEMUCFW`) o en un fork de `adrenaline_user`.
   - Del lado PSP, `sceGeListEnQueuePatched` mandaría (lista, stall) por EXTRA_1 y esperaría antes de encolar.
   - El Cortex-A9 a 444 MHz, con mejor caché y memoria, debería ser varias veces más rápido que el Allegrex. Estimación: GT con ~50k comandos pasaría de 15–20 ms a unos pocos ms, menos la latencia de Kermit (decenas de µs por petición, estimado).
   - Las tablas (memo, vértices) podrían vivir en la RAM de la Vita.
   - Riesgos:
     - Juegos con cientos de actualizaciones de stall por frame: la latencia se suma en cada una. Convendría mantener el recorrido en el PSP para trozos chicos.
     - Mantenimiento de caché en ambos lados.
     - Offsets de ScePspemu por versión de firmware.
     - Depurar dos plugins a la vez.
   - Plan: primero medir la latencia de ida y vuelta con un comando vacío, y el costo por comando del recorrido en el ARM.
2. **Presentar la imagen desde el ARM.** El ARM podría leer el buffer grande directo de la VRAM del PSP, en vez de que el GE lo copie a 0x0A400000, y sincronizar con el flip para evitar tearing. Ganancia chica: la copia la hace el GE, no la CPU. Requiere cambiar Adrenaline.
3. **Dibujar los comandos del GE con la GPU de la Vita.** Equivale a escribir un emulador del GE sobre GXM, como el backend de PPSSPP. Habría que reemplazar `sceGe*`, señales, callbacks y DrawSync en el PSP. Es un proyecto de meses, fuera de alcance.

### Logs y otros costos
- La escritura de archivos ya la hace la Vita: `sceIoWrite` a `ms0:` viaja por Kermit (`KERMIT_MODE_MSFS`) al ARM, que escribe en ux0 (`ScePspemuRemoteMsfs`). Además el log se escribe desde un hilo propio, con doble buffer.
- Lo que queda en el PSP es formatear el texto (vsnprintf): son unas pocas líneas cada 10 s, y LOG_ONCE solo actúa ante eventos. El costo es despreciable comparado con el recorrido.
- Excepción: la captura (L+R+SELECT) usa modo inmediato y genera cientos de líneas en un frame, pero es puntual.
- El escalado de videos por CPU (~19 ms por frame en Silent Hill) ya se reemplazó por el GE en dev18.
- No hay un segundo núcleo MIPS disponible: el ME del PSP lo atiende el ARM (`KERMIT_MODE_ME`).
