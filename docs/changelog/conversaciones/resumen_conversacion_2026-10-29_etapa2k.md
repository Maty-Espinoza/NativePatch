# Resumen para seguir en otra conversación: cierre de la etapa 2 (2026-10-29, undécima conversación)

Leer: `flujo_de_trabajo.md` → este resumen → `TODO.md` (sección "Preparación para la etapa 3" primero) →
`etapa2_ridge_racer.md` (lo último hecho) → `etapa2_fase2_adelanto.md`.

## Dónde quedamos
- **Etapa 2 de la arquitectura B cerrada** (decisión del usuario: ≥ 58 fps en lo más pesado de GT alcanza por ahora).
- Commits en `dev`, con push a GitHub: `e463aff` **gepresent v13.12** (cierre de la etapa 2), `0afb581` **relevo**
  (logs solo al cerrar el juego) y `795d461` **gepresent v13.13** (Ridge Racer a 60 fps). GePatch sigue en
  **0.2-dev66**. Push desde el clon del contenedor `/home/claude/maty-espinoza/nativepatch` (`add_repo` con push).
- Repo de Windows igual a GitHub (por paquete: hoy la VM de Windows **no** llegó a GitHub, 403). El repo del Mac está
  atrasado (7 commits): pull antes de trabajar ahí.
- Vita: GePatch dev66 + gepresent v13.13 (`8c6adb31…`, verificado), con `ux0:data/gepresent/nucleo_captura` = "libre"
  (igual que sin archivo). Registro mediano. Relevo nuevo en `Descargas\vitabridge.py`.
- Logs: `vitabridge/etapa2_v13.12.1/gt1`, `etapa2_v13.12.2/gt1`, `etapa2_v13.12/locoroco1`, `etapa2_v13.12/rr1`,
  `etapa2_v13.13.1/rr1`, `etapa2_v13.13.2/rr1`, `etapa2_v13.13.2/gt1`.

## Qué se hizo
- **Lista de la etapa 3 en el TODO** (pedido del usuario): GePatch → NativeLink (lado PSP: captura), gepresent →
  NativeDraw (lado Vita: dibuja con la GPU); README nuevo (proyecto hecho con IA, en proceso, ya no un fork); decidir la
  instalación (dos plugins / instalador / actualizador); versiones 0.0.x hasta el primer release NativeLink 0.1 +
  NativeDraw 0.1; quitar lo heredado de GePatch que no se usa; reemplazar ambientes compilados por scripts. **No se
  empezó nada de eso.** El usuario agregó el resumen a las instrucciones del proyecto.
- **DISTINTAS arregladas (v13.12):** las sublistas de la VRAM se comparaban contra el espejo en que se grabaron (en los
  frames del otro espejo, una copia de un frame atrás). Ahora se guarda la dirección de la VRAM y se traduce al espejo
  del frame. 0 DISTINTAS en ~100 000 revisiones (antes 1–7 por corrida). La prueba de qemu reproduce el error (vram 3).
- **Comprobación por adelantado (v13.12):** los lectores comprueban, al empezar la lista, las sublistas estables
  aplicadas en la anterior. v13.12.1 (en orden) salió peor: iban a la par del capturador y comparaban lo mismo.
  v13.12.2 (desde el final) quedó: GT en lo más pesado 57,3 → 58,2 fps, 17,4 → 16,8 ms de Vita por frame.
- **Lo que limita** es leer la memoria del PSP (~2 µs por línea): el capturador sigue comparando ~5,5 ms por frame. Leer
  más barato (vista con caché o DMA) quedó en `TODO.md` para el futuro, por decisión del usuario.

## Después del cierre: otros juegos
- **Relevo (commit `0afb581`):** dos veces la Vita dejó de abrir programas después de "550 Could not allocate memory"
  del FTP de vitacompanion, siempre tras ~600 descargas. El relevo listaba cada 1 s y bajaba los logs enteros cada vez
  que crecían. Ahora mira cada 15 s y baja un log solo cuando dejó de crecer (al cerrar el juego o Adrenaline): una
  sesión de RR hizo 7 transferencias y ningún error. Después de cerrar un juego hay que esperar 15–30 s antes del
  siguiente.
- **LocoRoco** (v13.12): 59,9 fps, 0 DISTINTAS; cambia una sublista antes de encolar → compuerta en el sync (correcto).
- **Ridge Racer** caía a ~30 fps en la curva antes de la zona con público (una vez por vuelta: ~20 mil comandos y ~3 700
  draws por frame; el PSP con ~55 % libre). RR no repite sublistas (todo se interpreta, ~1 µs por comando) y el capturador
  tenía ~65 % de su núcleo (el audio usa el resto). v13.13.1: interpretar de a línea de 64 bytes (~800 ns por comando,
  ese tramo a ~50 fps). v13.13.2: capturador en cualquier núcleo (~100 %): **RR a 60 fps toda la carrera**; GT probado
  también (58,2 → 60,2 en lo más pesado), sin cortes de audio. Commit como **v13.13** con el núcleo libre por defecto.

## Siguiente
- El usuario puede seguir **probando sus otros juegos instalados** con v13.13 (falta Silent Hill y LocoRoco con el
  núcleo libre).
- Después: la preparación para la etapa 3 (`TODO.md`), y luego la etapa 3 (renderizador mínimo).

## Lecciones
- Mirar el código que genera el compilador antes de optimizar a ciegas: `walkList` metida dentro de `walk` se quedaba
  sin registros (recalculaba `&q[...]` y usaba la pila en cada comando).
- El capturador fijo en el núcleo del audio perdía ~35 % en RR; con afinidad a los 3 núcleos el sistema lo mueve solo y
  el audio no sufrió (RR y GT).
- Repartir trabajo por adelantado entre hilos no sirve si van en el mismo orden y no son más rápidos que el que espera:
  hacerlos empezar desde el otro extremo.
- Para la prueba de qemu en el contenedor: `-I` a una carpeta con solo `psp2*` y `vitasdk*` de los encabezados de
  vitasdk (con todo `include` choca la libc).
- Si la Vita responde "550 Could not allocate memory" y después "timed out", está colgada o con poca memoria: el usuario
  la reinició y el relevo subió solo.
- El permiso de borrar en `Documents\GitHub` se puede perder a mitad de conversación: volver a pedirlo si git falla con
  "Operation not permitted".

## Reglas de trabajo vigentes
- Versiones de prueba `.x` (siguiente: gepresent v13.14.x; GePatch dev67.x si cambia); commit solo con la confirmada.
  Con la etapa 3 cambia a NativeLink/NativeDraw 0.0.x (a definir en la preparación).
- Pruebas: qué juego, qué hacer, en qué fijarse; opciones con el nombre del menú; resumen simple después de cada cambio.
- Registro: mediano durante el desarrollo, extremo para análisis, mínimo para publicar.
