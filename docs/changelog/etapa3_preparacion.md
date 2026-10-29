# Preparación para la etapa 3: NativeLink + NativeDraw (2026-10-29)

**Commits:** NativeLink 0.0.1 + NativeDraw 0.0.1 (cambio de nombre), NativeLink 0.0.2, NativeDraw 0.0.2, herramientas,
instalador y documentos. Antes: GePatch 0.2-dev66 + gepresent v13.13 (etiqueta `gepatch-dev66`).

## En simple
- El proyecto se separó del GePatch original: el plugin del lado PSP se llama **NativeLink** y el del lado Vita
  **NativeDraw**, cada uno en su carpeta y con su versión (`docs/dev/versiones.md`).
- Se quitó todo lo que venía de GePatch y ya no se usaba (el escalado en el PSP, el menú viejo, los volcados, el banco
  de pruebas). Lo que hace el trabajo (la captura de la etapa 2) quedó igual, y las pruebas lo confirman.
- Un **instalador** (`NativePatch.vpk`) deja los dos plugins instalados y activados, y por ahora también borra los
  restos de las pruebas.
- El repo quedó con un README nuevo, licencia GPLv3, documentos separados en vigentes (`docs/dev`) y archivo
  (`docs/changelog`), y un ambiente que se arma con scripts.

## Decisiones del usuario
- Ya no es un fork: hay que separarlo en GitHub (lo hace el usuario). Licencia GPLv3 (permite forks y versiones propias,
  siempre como GPLv3 con su código).
- Versiones `0.MENOR.PARCHE` por plugin; un cambio de un plugin sube su PARCHE, uno que necesita a los dos sube el MENOR.
  El primer release se marca aparte.
- Instalación con un VPK; actualizador a futuro; la opción de limpiar restos se quita antes de la 1.0.
- Quitar el volcado .gpd, la captura al log y el laboratorio PPSSPP (las pruebas pasan a ser solo en la Vita); sacar del
  repo las herramientas de pruebas pasadas (quedan en la etiqueta).
- El menú L+R+START lo tiene que dibujar NativeDraw en la Vita y desde ahí se deben poder cambiar todas las opciones que
  hoy son archivos: queda en `TODO.md` (depende de la etapa 3).
- El ícono del instalador: solo los lentes (dos pantallas unidas), sin texto ni fondo, y sin imágenes de LiveArea.

## Descubrimientos
- **Adrenaline 8 carga los plugins de PSP desde `ms0:/seplugins/plugins.txt`** (o `EPIplugins.txt` si existe), con líneas
  `game, ms0:/seplugins/x.prx, on`. `game.txt` solo lo convierte una vez el modo recuperación (código de isage/Adrenaline,
  `cef/core/systemctrl/src/plugin.c`). Detalle en `docs/dev/adrenaline_notas.md`.
- En la Vita del usuario taiHEN usa `ur0:tai/config.txt` (no hay `ux0:tai/config.txt`).
- exFAT no distingue mayúsculas: el plugin viejo estaba como `GE_PATCH.PRX` y lo borra igual `ge_patch.prx`.
- `log.c` lleva la fecha y hora de compilación: el md5 de NativeLink cambia en cada compilación aunque el código no cambie.
- Restos de las pruebas que había en la Vita: `ge_patch.prx`, `ge_patch_anterior.prx`, `geptest.prx`, `kbench.prx`,
  `vpjuego.prx`, archivos `._*` del Finder, `ux0:data/{gepresent,adrfsr,kbench,vprobe}`, `ux0:pspemu/{kbench,vprobe,
  gepvm,geptest}`, `PSP/GAME/{GEPTEST,GEPVM,KBENCH,VPROBE}`, ~100 volcados `.gpd`, logs `ge_patch*` y opciones
  `gepatch_*.txt` (`PSP/GAME/UPDATE` no es del proyecto y no se toca).

## Pruebas en la Vita [medido]
| Prueba | Carpeta | Resultado |
|---|---|---|
| 0.0.1 (subida a mano: `plugins.txt` y `config.txt` editados con el relevo, reinicio) | `vitabridge/etapa3_0.0.1/gt1` | GT una vuelta: igual que v13.13 (tabla abajo), 0 DISTINTAS en 51 405 |
| Instalador (con la configuración original restaurada) | `vitabridge/etapa3_0.0.2.1/instalador` | Limpiar restos + instalar: `plugins.txt` con solo NativeLink, `config.txt` = original con la línea de NativeDraw en vez de la de gepresent, respaldos en `ux0:data/nativepatch/respaldo/`, sin restos |
| 0.0.2 (instalada con el VPK) | `vitabridge/etapa3_0.0.2.1/gt1` (menús), `gt2` (vuelta) | GT una vuelta: igual que v13.13, 0 DISTINTAS en 50 638; la compuerta cambia una vez en la carga (como en dev66.2) |

GT (UCUS-98632), fps / ms de Vita por frame según comandos por frame:

| Comandos por frame | v13.13.2 | 0.0.1 | 0.0.2 |
|---|---|---|---|
| 30–45 mil | 60,0 / 7,8 | 60,0 / 7,9 | 60,0 / 8,2 |
| 45–60 mil | 60,0 / 10,0 | 60,0 / 10,0 | 60,0 / 10,3 |
| 60–75 mil | 59,9 / 12,0 | 59,9 / 12,4 | 60,0 / 12,6 |
| más de 75 mil | 60,2 / 13,4 | 60,1 / 13,5 | 60,3 / 13,6 |

Ridge Racer no se volvió a correr con 0.0.2 (la captura no cambió; queda en `TODO.md`).

## Instalador probado antes en el PC
`instalador/main.c` se compiló para el PC con funciones de archivo falsas sobre una "tarjeta" en una carpeta, con copias
reales de `config.txt` y `plugins.txt`: instalar, reinstalar (no duplica líneas), línea apagada (`off`) reemplazada,
`config.txt` sin la sección `*PSPEMUCFW` (la agrega), sin `plugins.txt` (lo crea), limpiar y desinstalar.
