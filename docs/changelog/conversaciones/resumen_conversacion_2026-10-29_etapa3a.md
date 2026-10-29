# Resumen para seguir en otra conversación: nueva identidad, NativeLink + NativeDraw 0.0.2 (2026-10-29, duodécima)

Leer: `docs/dev/FLUJO_DE_TRABAJO.md` → este resumen → `TODO.md` → `docs/dev/funcionamiento.md` →
`docs/dev/arquitectura_dibujo_en_vita.md` §6 (la etapa 3 es lo siguiente). Detalle de lo hecho:
`docs/changelog/etapa3_preparacion.md`.

## Dónde quedamos
- **GitHub (`dev`, push desde el contenedor a nombre de Claude):** NativeLink 0.0.1 + NativeDraw 0.0.1 (cambio de
  nombre), NativeLink 0.0.2, NativeDraw 0.0.2, herramientas, instalador y documentos. `master` = `dev`. Etiqueta
  `gepatch-dev66` = último estado con los nombres viejos (ahí quedan la arquitectura A, volcados, laboratorio, bancos).
- **Vita:** instalada con el VPK. `ux0:pspemu/seplugins/nativelink.prx` (activado en `plugins.txt`, única línea),
  `ux0:data/nativedraw/nativedraw.suprx` (en `*PSPEMUCFW` de `ur0:tai/config.txt`), sin restos de GePatch ni de las
  pruebas. Respaldos en `ux0:data/nativepatch/respaldo/`. Registro mediano, sin interruptores (núcleo del capturador libre).
- **PC con Windows:** repo al día; relevo nuevo en `Descargas\vitabridge.py` (baja `native*.log`). Copias de la Vita de
  antes del cambio en `vitabridge/rebrand/`. Logs: `vitabridge/etapa3_0.0.1/gt1`, `etapa3_0.0.2.1/{gt1,gt2,instalador}`.

## Qué se hizo (en simple)
- GePatch → **NativeLink** (lado PSP: anota lo que el juego le pide al GE) y gepresent → **NativeDraw** (lado Vita: lo
  recorre; en la etapa 3, lo dibuja). Cada uno en su carpeta y con su versión `0.MENOR.PARCHE`.
- Se quitó lo heredado de GePatch (escalado en el PSP, menú viejo, volcados, banco de GXM). NativeLink: 3 889 → ~620 líneas.
- **Instalador VPK** (`instalador/`, `NativePatch.vpk`): instalar/actualizar, desinstalar, limpiar restos (temporal) y
  reiniciar. Ícono: solo los lentes, sin texto ni fondo; sin LiveArea.
- Repo: README nuevo (hecho con IA, en desarrollo), licencia GPLv3, `docs/dev` (vigentes) y `docs/changelog` (archivo +
  resúmenes de conversación, que se quitan antes del primer release), `CHANGELOG.md`, ambiente por scripts
  (`tools/preparar.sh`, `tools/compilar.sh` → `build/`), compilación automática de GitHub de todo.
- Pruebas: GT con 0.0.1 y con 0.0.2 igual que con v13.13 (60,3 fps en lo más pesado, 0 DISTINTAS); el instalador
  limpió e instaló bien en la Vita real.

## Decisiones del usuario (esta conversación)
- Ya no es un fork (separarlo en GitHub lo hace él). GPLv3.
- Versiones: cambio de un plugin = PARCHE; cambio que necesita a los dos = MENOR de los dos. Primer release aparte.
- VPK de instalación y actualización (actualizador en internet: a futuro); "limpiar restos" se quita antes de 1.0.
- Sin volcados .gpd, sin captura al log, sin laboratorio PPSSPP: pruebas solo en la Vita. Herramientas viejas fuera del
  repo (quedan en la etiqueta).
- **Menú L+R+START dibujado por NativeDraw**, con todas las opciones que hoy son archivos: en `TODO.md` (depende de la etapa 3).
- Documentos: `docs/dev` para trabajar, `docs/changelog` para archivar; los resúmenes de conversación van ahí por ahora.

## Lecciones
- Adrenaline 8 activa los plugins de PSP en `ux0:pspemu/seplugins/plugins.txt` (`game, ms0:/seplugins/x.prx, on`), o en
  `EPIplugins.txt` si existe; `game.txt` ya no se usa. En esta Vita taiHEN usa `ur0:tai/config.txt`.
- El relevo sube también a `ur0:` (`enviar/ur0/tai/config.txt`) y lista carpetas (`listar_*.txt`).
- Probar el instalador en el PC con funciones de archivo falsas y copias reales de los archivos de configuración antes de
  tocar la Vita.
- El md5 de NativeLink cambia en cada compilación (`log.c` lleva la hora): comparar contra el binario mandado.
- La herramienta `Projects` no la tienen los subagentes: copiar documentos del proyecto lo hace la conversación principal.

## Siguiente
1. Ridge Racer con 0.0.2 (opcional; la captura no cambió) y probar "Desinstalar" en la Vita.
2. Separar el fork en GitHub (usuario) y poner `master` como rama por defecto.
3. **Etapa 3**: renderizador mínimo en NativeDraw (clears, TRANSFER, 2D con texturas, presentación HD), y con eso el menú
   L+R+START dibujado por NativeDraw. Versiones de prueba 0.0.3.x / 0.1.0.x según toque a uno o a los dos plugins.

## Reglas de trabajo vigentes
- Versiones de prueba con cuarto número; commit solo con la confirmada en la Vita (`docs/dev/versiones.md`).
- Pruebas: qué juego, qué hacer, en qué fijarse; resumen simple después de cada cambio; registro mediano en desarrollo.
- Subir a GitHub después de confirmar (push desde el contenedor; commits a nombre de Claude).
