# NativePatch

*[English version: `README.md`.]*

Jugar los juegos de PSP en la PS Vita a la resolución de la Vita (960x544) con
[Adrenaline 8](https://github.com/isage/Adrenaline), sin que el juego pierda velocidad.

> **En desarrollo, todavía no sirve para jugar.** Hoy la Vita dibuja juegos 2D (KOF 96 a 60 fps, Metal Slug XX, Retro
> City Rampage) al doble de tamaño; los juegos 3D se capturan y miden pero se ven como sin los plugins. Ver la tabla de
> estado.

> **Proyecto hecho con IA ("vibecoded").** El código y los documentos los escribió Claude (Anthropic) siguiendo las
> pruebas, ideas y decisiones de [Maty-Espinoza](https://github.com/Maty-Espinoza), que lo prueba en su propia Vita.

## Cómo funciona
El PSP de la Vita corre el juego tal cual, a su velocidad original. Dos plugins trabajan juntos:

| Plugin | Dónde corre | Qué hace |
|---|---|---|
| **NativeLink** (`nativelink.prx`) | lado PSP (dentro de Adrenaline, junto al juego) | No cambia nada del juego: anota en una memoria compartida ("buzón") cada lista de dibujo que el juego le manda a su chip gráfico (el GE), copia para la Vita lo que la Vita no puede ver (la VRAM del PSP, el "espejo") y, si la Vita se atrasa, hace esperar un poco al juego (la "compuerta", hasta 5 ms). |
| **NativeDraw** (`nativedraw.suprx`) | lado Vita (plugin de taiHEN en el proceso de Adrenaline) | Lee el buzón, recorre las mismas listas desde la Vita, guarda lo que cada frame necesita y lo vuelve a dibujar con la GPU de la Vita al doble, reemplazando la imagen que muestra Adrenaline. Si un frame trae algo que todavía no sabe dibujar, se muestra la imagen normal del juego: nunca peor que sin los plugins. |

Más detalle: [`docs/dev/es/overview.md`](docs/dev/es/overview.md) y [`docs/dev/es/how-it-works.md`](docs/dev/es/how-it-works.md).

## Estado
| Etapa | | Estado |
|---|---|---|
| 0–1 | Medir si la Vita puede dibujar lo del PSP (draws por frame, costo de cada uno en GXM) | hecha |
| 2 | Capturar las listas del juego desde la Vita sin frenarlo | hecha (Gran Turismo ≥ 58 fps en lo más pesado, Ridge Racer y LocoRoco a 60) |
| 3 | Dibujo mínimo en la Vita: 2D, copias de bloque, presentación en HD | **en curso** (KOF 96 a 60 fps, Metal Slug XX a sus 30 fps; sigue: caché de texturas entre frames) |
| 4–6 | 3D, framebuffers en HD, rendimiento | pendiente |

## Requisitos
- PS Vita con HENkaku/Ensō y **Adrenaline 8** (las versiones anteriores no están probadas).
- VitaShell para instalar el VPK.

## Instalar
1. Instalar `NativePatch.vpk` con VitaShell y abrir **NativePatch** (el instalador).
2. **Instalar / actualizar**: copia NativeLink a `ux0:pspemu/seplugins/` y lo activa en `plugins.txt` (el administrador
   de plugins de Adrenaline 8); copia NativeDraw a `ux0:data/nativedraw/` y lo agrega en la sección `*PSPEMUCFW` del
   `config.txt` de taiHEN. Antes de cambiar un archivo de configuración deja un respaldo en
   `ux0:data/nativepatch/respaldo/`.
3. Reiniciar la Vita si el instalador lo pide (cuando cambió `config.txt`).

Para actualizar: instalar el VPK nuevo y volver a usar **Instalar / actualizar**. El instalador también desinstala.
- El dibujo en la Vita se activa con el archivo `ux0:data/nativedraw/dibujar` (sin él, NativeDraw solo mide).
- Abrir un juego manteniendo **L** (o SELECT): NativeLink queda apagado para esa sesión.
- **L+R+START** durante el juego: menú de NativeDraw (por ahora muestra las versiones). **L+R+SELECT**: volcado para
  depurar.
- Logs en `ux0:pspemu/`: `nativelink.log`, `nativedraw.log` y `nativedraw_captura.log`.
- Con el filtro "Original" de Adrenaline la Vita no dibuja con GXM y NativeDraw no puede actuar: usar Bilinear.

## Compilar
Linux x86_64 (o WSL en Windows; en Windows `tools/windows/instalar.cmd` lo deja todo listo):
```sh
sh tools/preparar.sh     # una vez: compiladores del PSP (pspdev) y de la Vita (vitasdk) en ~/tc
sh tools/compilar.sh     # deja nativelink.prx, nativedraw.suprx y NativePatch.vpk en build/
```

## Documentación
| Carpeta | Para quién | Qué hay |
|---|---|---|
| [`docs/dev/`](docs/dev/es/README.md) (`en`, `es`) | desarrolladores | qué es, cómo funciona, arquitectura y hoja de ruta, logs e interruptores, volcados, notas de Adrenaline, descubrimientos y límites, versiones, manual básico de gráficos |
| [`docs/agent/`](docs/agent/es/README.md) (`en`, `es`) | la IA que trabaja en el proyecto (y quien quiera repetir su flujo) | flujo de trabajo, ambientes de compilación, relevo con la Vita, buenas prácticas, plan de la etapa en curso |
| [`docs/changelog/`](docs/changelog/es/README.md) (`en`, `es`) | historia | lo que se hizo y se midió en cada versión, tal como quedó en su momento (no se actualiza) |
| [`TODO.md`](TODO.md), [`CHANGELOG.md`](CHANGELOG.md) | todos | pendientes; cambios de cada versión (en inglés) |
| [`CLAUDE.md`](CLAUDE.md) | la IA | qué leer primero en cada conversación |

## Versiones
NativeLink y NativeDraw llevan cada uno su versión `0.MENOR.PARCHE`; hasta el primer release (0.1.0) todo es 0.0.x. Las
versiones que se prueban en la Vita llevan un cuarto número (`0.0.8.22`) y van al repo marcadas "(prueba)". Reglas e
historial: [`docs/dev/es/versions.md`](docs/dev/es/versions.md).

## Origen y créditos
Nació como fork de [GePatch](https://github.com/TheOfficialFloW/GePatch) de TheFlow (abandonado en v0.2), que escalaba
la imagen haciendo dibujar al PSP más grande. Ese camino tenía un límite de rendimiento (el chip gráfico del PSP no da
abasto con 4 veces los píxeles) y errores que venían de engañar al juego, así que el proyecto cambió de arquitectura:
el PSP dibuja normal y la Vita lee y dibuja. Desde NativeLink 0.0.2 no queda código de GePatch; el estado del proyecto
cuando todavía era GePatch está en la etiqueta `gepatch-dev66`. Gracias a TheFlow por GePatch y por Adrenaline, y a
isage por Adrenaline 8.

## Licencia
[GPLv3](LICENSE): cualquiera puede usar, estudiar, modificar y compartir el código, y hacer su propia versión, siempre
que lo que distribuya siga siendo GPLv3 con su código fuente disponible. Partes de terceros:
[`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md).
