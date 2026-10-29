# NativePatch

Proyecto para ver los juegos de PSP en la PS Vita a la resolución de la Vita (960x544) con
[Adrenaline 8](https://github.com/isage/Adrenaline), sin que el juego pierda velocidad.

> **En desarrollo, todavía no sirve para jugar.** Hoy los plugins capturan y miden lo que dibuja el juego, pero aún no
> lo dibujan en alta resolución (eso empieza en la etapa 3, ver más abajo). El juego se ve igual que sin plugins.

> **Proyecto hecho con IA ("vibecoded").** El código y los documentos los escribió Claude (Anthropic) siguiendo las
> pruebas, ideas y decisiones de [Maty-Espinoza](https://github.com/Maty-Espinoza), que lo prueba en su propia Vita.

## Cómo funciona
El PSP de la Vita corre el juego tal cual, a su velocidad original. Dos plugins trabajan juntos:

| Plugin | Dónde corre | Qué hace |
|---|---|---|
| **NativeLink** (`nativelink.prx`) | lado PSP (dentro de Adrenaline) | No cambia nada del juego: anota en una memoria compartida cada lista de dibujo que el juego le manda a su chip gráfico (el GE), y copia para la Vita lo que la Vita no puede ver (la VRAM del PSP). |
| **NativeDraw** (`nativedraw.suprx`) | lado Vita (plugin de taiHEN) | Lee esas mismas listas desde la Vita. Hoy las recorre y mide; desde la etapa 3 las va a dibujar con la GPU de la Vita, en alta resolución. |

Si la Vita se atrasa, NativeLink hace esperar un poco al juego (hasta 5 ms) para no perder información. Con la versión
actual Gran Turismo, Ridge Racer y LocoRoco van a 60 fps con los dos plugins activos.

Más detalle: [`docs/dev/funcionamiento.md`](docs/dev/funcionamiento.md) y la hoja de ruta en
[`docs/dev/arquitectura_dibujo_en_vita.md`](docs/dev/arquitectura_dibujo_en_vita.md) (§6).

## Etapas
| Etapa | Estado |
|---|---|
| 0–1: medir si la Vita puede dibujar lo del PSP (cantidad de dibujos, costo en GXM) | hecha |
| 2: capturar las listas del juego desde la Vita sin frenarlo | hecha (≥ 58 fps en lo más pesado de Gran Turismo) |
| 3: dibujo mínimo en la Vita (2D, copias, presentación en HD) | siguiente |
| 4–6: 3D, framebuffers en HD, rendimiento | pendiente |

## Requisitos
- PS Vita con HENkaku/Ensō y **Adrenaline 8** (las versiones anteriores no están probadas).
- VitaShell para instalar el VPK.

## Instalar
1. Instalar `NativePatch.vpk` con VitaShell y abrir **NativePatch** (instalador).
2. **Instalar / actualizar**: copia NativeLink a `ux0:pspemu/seplugins/` y lo activa en `plugins.txt` (el administrador
   de plugins de Adrenaline 8); copia NativeDraw a `ux0:data/nativedraw/` y lo agrega en la sección `*PSPEMUCFW` del
   `config.txt` de taiHEN. Antes de cambiar un archivo de configuración deja un respaldo en
   `ux0:data/nativepatch/respaldo/`.
3. Reiniciar la Vita si el instalador lo pide (cuando cambió `config.txt`).

Para actualizar: instalar el VPK nuevo y volver a usar **Instalar / actualizar**. El instalador también desinstala.

- Abrir un juego manteniendo **L** (o SELECT): NativeLink queda apagado para esa sesión (con L, Adrenaline 8 además
  apaga todos los plugins).
- Logs en `ux0:pspemu/`: `nativelink.log`, `nativedraw.log` y `nativedraw_captura.log`.

## Compilar
Linux x86_64 (o WSL en Windows; en Windows: `tools/windows/instalar.cmd`):
```sh
sh tools/preparar.sh     # una vez: compiladores del PSP (pspdev) y de la Vita (vitasdk) en ~/tc
sh tools/compilar.sh     # deja nativelink.prx, nativedraw.suprx y NativePatch.vpk en build/
```
Cómo se trabaja (pruebas en la Vita, relevo, versiones): [`docs/dev/FLUJO_DE_TRABAJO.md`](docs/dev/FLUJO_DE_TRABAJO.md).
Pendientes: [`TODO.md`](TODO.md). Cambios de cada versión: [`CHANGELOG.md`](CHANGELOG.md).

## Versiones
NativeLink y NativeDraw llevan cada uno su versión `0.MENOR.PARCHE`. Un cambio en un solo plugin sube su PARCHE; un
cambio que necesita a los dos (por ejemplo, en la memoria compartida) sube el MENOR de los dos. Dos plugins con el mismo
MENOR funcionan juntos. Detalle y equivalencias con las versiones viejas: [`docs/dev/versiones.md`](docs/dev/versiones.md).

## Origen y créditos
Nació como fork de [GePatch](https://github.com/TheOfficialFloW/GePatch) de TheFlow (abandonado en v0.2), que escalaba
la imagen haciendo dibujar al PSP más grande. Ese camino tenía un límite de rendimiento (el chip gráfico del PSP no da
abasto con 4 veces los píxeles) y errores que venían de engañar al juego, así que el proyecto cambió de arquitectura y
desde NativeLink 0.0.2 ya no usa esa forma de escalar. Gracias a TheFlow por GePatch y por Adrenaline, y a isage por
Adrenaline 8. El estado del proyecto cuando todavía era GePatch está en la etiqueta `gepatch-dev66`.

## Licencia
[GPLv3](LICENSE): cualquiera puede usar, estudiar, modificar y compartir el código, y hacer su propia versión, siempre
que lo que distribuya siga siendo GPLv3 con su código fuente disponible. Partes de terceros: [`AVISOS_TERCEROS.md`](AVISOS_TERCEROS.md).
