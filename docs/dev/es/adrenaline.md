# Adrenaline: cómo funciona y qué usa NativePatch

Lo que hay que saber de Adrenaline (el que corre los juegos de PSP en la Vita) para entender dónde se enganchan
NativeLink y NativeDraw. Fuentes: código de Adrenaline 8 (isage/Adrenaline, commit c8475f5, revisado el 2026-10-29) y
de Adrenaline 7 (TheOfficialFloW/Adrenaline, commit 7d382b7, revisado el 2026-10-21). Cómo lo usa NativePatch:
`how-it-works.md`.

## Adrenaline 8: cómo carga los plugins (código de isage/Adrenaline, commit c8475f5, revisado el 2026-10-29)
- **Plugins del PSP:** `cef/core/systemctrl/src/plugin.c` (`loadPlugins`) lee `ms0:/seplugins/plugins.txt`
  (= `ux0:pspemu/seplugins/plugins.txt`), o **`EPIplugins.txt` en su lugar si existe**. Una línea por plugin:
  `<runlevel>, <ruta>, <on|off>` (p. ej. `game, ms0:/seplugins/nativelink.prx, on`); `#`, `;` y `//` son comentarios.
  `game` = juegos; también hay `vsh` y `pops`. Una ruta sin `:` se toma relativa a `ms0:/seplugins/`.
- **`game.txt`/`vsh.txt` ya no se leen al cargar plugins**: solo el modo recuperación los convierte una vez a
  `plugins.txt` (`cef/core/recovery/src/plugins.c`). Los documentos viejos que dicen "activar en `game.txt`" son de
  Adrenaline 7.
- El administrador de plugins del XMB (`cef/core/xmbctrl/src/plugins.c`) lee y reescribe el mismo archivo: el usuario puede
  prender o apagar NativeLink desde el XMB.
- Mantener **L** al abrir un juego desactiva todos los plugins (`g_disable_plugins`).
- **Plugins de la Vita (taiHEN):** `ux0:tai/config.txt` si existe; si no, `ur0:tai/config.txt`. La sección `*PSPEMUCFW`
  carga plugins en el proceso de Adrenaline (ahí va NativeDraw). Un cambio en `config.txt` necesita reiniciar la Vita
  (o refrescar taiHEN desde la configuración de HENkaku).
- El instalador (`instalador/main.c`) sigue estas reglas.

## Adrenaline 8: cómo dibuja la pantalla (`user/menu.c`, función `AdrenalineDraw`)
- **Juegos de PSP:** en cada vuelta copia la imagen del PSP (0x74000000, 512x272 8888) a su textura `psp_tex` con
  `sceDmacMemcpy` y la dibuja con vita2d centrada, con la escala de su menú (`psp_screen_scale`, 2.0 = pantalla
  completa). NativeDraw engancha justo esto: reconoce `psp_tex` por su tamaño y la reemplaza por su superficie del doble.
- **Juegos de PS1:** la imagen sale del emulador de PS1 de la Vita en otra textura (`pops_tex`, 1024x544, ya en 4:3 y con
  la escala de PS1 del menú). No pasa por el GE: **NativeLink no la ve y NativeDraw no la toca.**
- **Filtro "Original"** (o el escalado directo "sharp" a 2x): Adrenaline no dibuja con GXM y el presentador no puede
  actuar. Para usar NativeDraw hay que elegir un filtro con GXM (Bilinear).
- **Proporción (4:3 / 16:9):** como la superficie de NativeDraw es el doble exacto de `psp_tex`, Adrenaline la ubica y
  escala igual, así que se respetan su escala, su posición y la proporción que tenga el juego.
- Los sombreadores compilados que usa Adrenaline (vita2d, de vita-shader-collection) son los mismos que NativeDraw usa
  para el presentador (`nativedraw/sombreadores.h`).

## Arquitectura (Adrenaline 7, código de TheOfficialFloW/Adrenaline, commit 7d382b7)
- La Vita trae el hardware del PSP dentro de su chip: CPU MIPS Allegrex y GE (motor gráfico). El código del juego y el de NativeLink corren en ese MIPS real, no en un emulador por software.
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
| 0x0A000000–0x0B000000 | 16 MB que Adrenaline borra al arrancar. NativePatch usa esta zona |
| 0x0B000000–0x0B920000 | Paquete flash0 (`SCE_PSPEMU_EXTRA_MEMORY`) |
| 0x0BD00000 | Scratchpad |
| 0x0BE00000 | VRAM (`SCE_PSPEMU_VRAM`) |

**Uso de NativePatch dentro de 0x0A000000–0x0B000000:** el buzón, los dos espejos de la VRAM, las listas del GE
para las copias del espejo y el canal del log (mapa en `how-it-works.md`). NativeLink reserva toda la zona en la
partición 11 al arrancar.

**Consecuencias:**
- "Force high memory layout" debe estar **apagado**. Con 52 MB el juego puede usar 0x0A000000 en adelante y chocaría con
  la zona de los plugins (NativeLink lo detecta y queda apagado).
- La VRAM real (0x04000000) es de **2 MB**, no 4: `sceGeEdramSetSize(4 MB)` falla (0x80000104).
- 0x04200000–0x043FFFFF es un reflejo: el GE dibuja ahí sobre los 2 MB bajos y las lecturas ven los datos permutados.

## El canal entre el PSP y el ARM

- El PSP llena una `SceKermitRequest` (comando + argumentos) y llama `sceKermitSendRequest661(req, modo, cmd, nargs, 0, &resp)`. Se bloquea hasta la respuesta, con un semáforo.
- En el ARM, un hilo espera con `ScePspemuKermitWaitAndGetRequest(modo, &req)` y responde con `ScePspemuKermitSendResponse`. Esas funciones están en offsets fijos del módulo ScePspemu (`user/main.c`, `GetFunctions`).
- Adrenaline usa `KERMIT_MODE_EXTRA_2` para sus comandos. `KERMIT_MODE_EXTRA_1` está definido pero no se usa (NativePatch no lo necesita: se comunica por la memoria compartida).
- El ARM lee y escribe la RAM del PSP en `0x68000000 + addr`. Para no leer datos viejos de su caché usa `ScePspemuConvertAddress(addr, KERMIT_INPUT_MODE, size)` y `ScePspemuWritebackCache` al escribir. Del lado PSP hay que escribir la caché a memoria antes de pedir.

### Logs y otros costos
- La escritura de archivos desde el PSP la hace la Vita: `sceIoWrite` a `ms0:` viaja por Kermit (`KERMIT_MODE_MSFS`) al
  ARM, que escribe en ux0 (`ScePspemuRemoteMsfs`). NativeLink evita incluso eso: deja el texto en la zona compartida y
  NativeDraw lo escribe a la tarjeta (`nativelink/log.h`).
- Lo que queda en el PSP es formatear el texto (vsnprintf): unas pocas líneas cada 10 s. Costo despreciable.
- No hay un segundo núcleo MIPS disponible: el ME del PSP lo atiende el ARM (`KERMIT_MODE_ME`).
