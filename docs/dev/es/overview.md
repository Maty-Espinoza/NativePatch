# NativePatch: qué es y en qué estado está

Documentación para desarrolladores. Si no sabes nada del proyecto, empieza por aquí; después, `how-it-works.md`.
Si tampoco sabes de gráficos (listas de dibujo, texturas, framebuffers), antes `graphics-basics.md`.

## Qué es
NativePatch muestra los juegos de PSP en la PS Vita a la resolución de la Vita (960x544, el doble en cada lado) con
[Adrenaline 8](https://github.com/isage/Adrenaline), sin que el juego pierda velocidad. El PSP que la Vita lleva
adentro corre el juego tal cual, a su velocidad original; la Vita lee las instrucciones de dibujo que el juego le manda
a su chip gráfico y las vuelve a dibujar con su propia GPU, más grande.

Es un proyecto **en desarrollo** y **hecho con IA ("vibecoded")**: el código y los documentos los escribió Claude
(Anthropic) siguiendo las pruebas, ideas y decisiones de [Maty-Espinoza](https://github.com/Maty-Espinoza), que lo
prueba en su propia Vita. Nació como fork de [GePatch](https://github.com/TheOfficialFloW/GePatch) de TheFlow, pero ya no
comparte código con él (detalle en "Origen", abajo). Licencia GPLv3.

## Las dos partes
| Plugin | Dónde corre | Qué hace |
|---|---|---|
| **NativeLink** (`nativelink.prx`) | lado PSP, dentro de Adrenaline, junto al juego | No cambia nada del juego: anota en una memoria compartida ("buzón") cada lista de dibujo que el juego le manda a su chip gráfico (el GE), copia para la Vita lo que la Vita no puede ver (la memoria de video del PSP, con el "espejo") y, si la Vita va atrasada, hace esperar un poco al juego (la "compuerta"). |
| **NativeDraw** (`nativedraw.suprx`) | lado Vita, plugin de taiHEN en el proceso de Adrenaline | Lee el buzón, recorre las mismas listas desde la Vita (el "capturador"), guarda lo que cada frame necesita (el "registro del frame") y lo dibuja con la GPU de la Vita al doble (el "traductor"); reemplaza la imagen que Adrenaline muestra por la suya (el "presentador"). Si un frame trae algo que no sabe dibujar, muestra la imagen normal del juego. |

Además: un **instalador** (`NativePatch.vpk`, `instalador/`) que copia los dos plugins a su lugar y los activa, y
**herramientas del PC** (`tools/`): el relevo con la Vita, scripts de compilación y lectores de logs y volcados.

Cómo funciona cada parte: `how-it-works.md`.

## Estado (octubre de 2026)
| Etapa | Qué | Estado |
|---|---|---|
| 0–1 | Medir si la Vita puede dibujar lo del PSP (cantidad de dibujos por frame, costo de cada uno en GXM) | hecha |
| 2 | Capturar las listas del juego desde la Vita sin frenarlo | hecha: Gran Turismo ≥ 58 fps en lo más pesado (60 casi siempre), Ridge Racer y LocoRoco a 60, Silent Hill a su velocidad |
| 3 | Dibujo mínimo en la Vita: 2D (menús, sprites), copias, presentación en HD | **en curso**: Retro City Rampage y KOF 96 dibujados por la Vita al doble (KOF a 60 fps), Metal Slug XX a sus 30 fps. Siguiente: caché de texturas entre frames |
| 4–6 | 3D, framebuffers en HD, rendimiento (reusar lo grabado, juntar draws) | pendiente |

Hoja de ruta completa, con las preguntas de cada etapa: `architecture-and-roadmap.md` §6. Plan y resultados de la
etapa 3: `../../agent/es/stage3-plan.md`.

Juegos con los que se prueba: Gran Turismo (UCUS-98632), Ridge Racer (ULJS-00001), Silent Hill Shattered Memories
(ULUS-10450), LocoRoco (NPEG-00024), Retro City Rampage DX (NPUH-10199), KOF 96 (NeoGeo Station, NPUH-10119) y
Metal Slug XX (ULUS-10495). Los juegos 3D todavía no se dibujan en la Vita: se ven como sin plugin.

## Instalar y usar
Requisitos: PS Vita con HENkaku/Ensō y **Adrenaline 8** (las versiones anteriores no están probadas) y VitaShell.
1. Instalar `NativePatch.vpk` con VitaShell y abrir **NativePatch** (el instalador).
2. **Instalar / actualizar**: copia NativeLink a `ux0:pspemu/seplugins/` y lo activa en `plugins.txt` (el administrador
   de plugins de Adrenaline 8); copia NativeDraw a `ux0:data/nativedraw/` y lo agrega en la sección `*PSPEMUCFW` del
   `config.txt` de taiHEN. Antes de cambiar un archivo de configuración deja un respaldo en `ux0:data/nativepatch/respaldo/`.
3. Reiniciar la Vita si el instalador lo pide (cuando cambió `config.txt`).

Para actualizar: instalar el VPK nuevo y volver a usar **Instalar / actualizar**. El instalador también desinstala.
- El dibujo en la Vita se activa con el archivo `ux0:data/nativedraw/dibujar` (sin él, NativeDraw solo mide).
- Abrir un juego manteniendo **L** (o SELECT): NativeLink queda apagado para esa sesión.
- **L+R+START** durante el juego: menú de NativeDraw (hoy muestra las versiones). **L+R+SELECT**: volcado para depurar.
- Logs en `ux0:pspemu/`: `nativelink.log`, `nativedraw.log` y `nativedraw_captura.log` (`logs-and-switches.md`).
- Con el filtro "Original" de Adrenaline la Vita no dibuja con GXM y NativeDraw no puede actuar: usar Bilinear.

## Compilar
Linux x86_64 (o WSL en Windows; en Windows: `tools/windows/instalar.cmd` lo deja todo listo):
```sh
sh tools/preparar.sh     # una vez: compiladores del PSP (pspdev) y de la Vita (vitasdk) en ~/tc
sh tools/compilar.sh     # deja nativelink.prx, nativedraw.suprx y NativePatch.vpk en build/
```
Detalle de los ambientes (contenedor, VM del Mac, WSL, GitHub Actions): `../../agent/es/environment.md`.

## Versiones
NativeLink y NativeDraw llevan cada uno su versión `0.MENOR.PARCHE`; hasta el primer release (0.1.0) todo es 0.0.x.
Las versiones que se prueban en la Vita llevan un cuarto número (`0.0.8.21`) y van al repo marcadas "(prueba)". Reglas
e historial: `versions.md`. Cambios de cada versión: `../../../CHANGELOG.md`. Pendientes: `../../../TODO.md`.

## Qué no hace (todavía) y límites conocidos
- **No dibuja 3D:** los juegos 3D (GT, RR, SH) se capturan y miden pero se ven como sin plugin. Es la etapa 4.
- **Un frame de atraso:** la imagen de la Vita se ve un frame después que la del PSP (16,7 ms a 60 fps).
- **Juegos de PS1:** no se tocan. En Adrenaline 8 la imagen de PS1 sale del emulador de PS1 de la Vita, no del chip
  gráfico del PSP: no hay nada que capturar.
- **Lo que el sistema dibuja encima** (diálogo del botón PS): se detecta comparando con la imagen normal y se muestra la
  imagen normal mientras dure.
- Más detalle, con los números medidos y lo que se descartó: `findings.md`.

## Origen y créditos
Nació como fork de [GePatch](https://github.com/TheOfficialFloW/GePatch) de TheFlow (abandonado en v0.2), que escalaba
la imagen haciendo dibujar al PSP más grande. Ese camino tenía un límite de rendimiento (el chip gráfico del PSP no da
abasto con 4 veces los píxeles) y errores que venían de engañar al juego, así que el proyecto cambió de arquitectura:
el PSP dibuja normal y la Vita lee y dibuja (`architecture-and-roadmap.md`). Desde NativeLink 0.0.2 no queda código de
GePatch; el estado del proyecto cuando todavía era GePatch está en la etiqueta `gepatch-dev66`. Gracias a TheFlow por
GePatch y por Adrenaline, y a isage por Adrenaline 8. Partes de terceros: `../../../THIRD_PARTY_NOTICES.md`.

## Mapa de la documentación
| Carpeta | Para quién | Qué hay |
|---|---|---|
| `docs/dev/` (en, es) | desarrolladores | este documento, `how-it-works.md`, `architecture-and-roadmap.md`, `logs-and-switches.md`, `dumps.md`, `adrenaline.md`, `findings.md`, `versions.md`, `graphics-basics.md` |
| `docs/agent/` (en, es) | la IA que trabaja en el proyecto (y quien quiera repetir su flujo) | cómo se trabaja, ambientes, relevo con la Vita, buenas prácticas, plan de la etapa en curso |
| `docs/changelog/` (en, es) | historia | lo que se hizo y se midió en cada versión, tal como quedó en su momento (no se actualiza) |
| raíz | todos | `README.md`, `TODO.md`, `CHANGELOG.md`, `THIRD_PARTY_NOTICES.md`, `CLAUDE.md` (instrucciones para la IA) |
