# Resumen para seguir en otra conversación: etapa 2, fase 2, puntos 2 y 3 (2026-10-29, décima conversación)

Leer: `flujo_de_trabajo.md` → este resumen → `etapa2_fase2_estadisticas.md` (lo hecho) → `etapa2_fase1_impacto.md`
(propuesta de la fase 2) → `registros.md` → `TODO.md`.

## Dónde quedamos
- **Commit `a27fb32` en `dev`, con push a GitHub: gepresent v13.11** (estadísticas compactas en la memoria de sublistas
  + `docs/etapa2_fase2_estadisticas.md`, `TODO.md`, flujo, registros y fase 1 actualizados). GePatch sigue en
  **0.2-dev66** (sin cambios). Push desde el clon del contenedor `/home/claude/maty-espinoza/nativepatch` (`add_repo`
  con push).
- El repo de Windows quedó igual a GitHub (**la VM de Windows ahora llega a GitHub: `git pull --ff-only` directo
  funcionó**, sin paquetes). El repo del Mac está atrasado (4 commits): pull antes de trabajar ahí.
- Vita: GePatch dev66 (`16755a0c…`) + gepresent v13.11 (`2149515e…`), ambos verificados. Registro mediano.
- Logs: `vitabridge/etapa2_v13.11.1/gt1` (cuelgue), `etapa2_v13.11.1/gt2`, `etapa2_v13.11.2/gt1`.

## Qué se hizo
- **Punto 2 (v13.11.1, descartado):** el capturador leía directo de a 256 B sin la caché compartida. En GT casi 3 veces
  más esperas (~690 → ~2 040 en la carrera): aprovecha ~2,3 de las 4 líneas de cada lectura y pierde el paralelismo de
  los lectores. La caché compartida se queda.
- **Punto 3 (v13.11, hecho):** cada sublista guardada guarda solo las estadísticas distintas de 0 (hasta 48) y suma solo
  esas; en mínimo solo comandos y draws. GT, Vita por frame: 60–75 mil comandos 15,6 → 14,9 ms (59,2 → 60,0 fps); más de
  75 mil 19,1 → 17,0 ms (54,3 → 57,3 fps). Registros iguales; 3 DISTINTAS de 51 121 (VRAM, lo conocido).
- La Vita se colgó una vez (primera corrida con v13.11.1, ~10 s después de largar). La segunda corrida igual terminó bien.
  El usuario notó la Vita "ligeramente afectada" y se le dieron pasos de recuperación sin perder Ensō (apagado completo
  → modo seguro → reconstruir base de datos → reinstalar Adrenaline → restaurar como último recurso; nunca actualizar el
  firmware). **Lo dejó para más adelante.**

## Siguiente (fase 2, de `etapa2_fase1_impacto.md`)
4. Revisar sublistas más barato o por adelantado (`comparar_us` ~5–6 ms por frame en la carrera).
5. Núcleo / prioridad del capturador (pierde ~17 %), con cuidado del audio.
Además: lo que queda al aplicar una sublista (registros y matrices; medir con extremo `aplicar_us`); probar Silent Hill,
Ridge Racer y LocoRoco con v13.11; la recuperación de la Vita cuando el usuario quiera.

## Lecciones
- Un costo por línea medido en promedio no dice cuántas líneas se aprovechan de cada lectura: medir el patrón real antes
  de cambiar de dónde lee el capturador.
- `device_commit_files` entregó una compilación vieja al reusar el nombre del archivo: al recompilar, mandar con otro
  nombre y comparar md5 en el PC antes de pedir la prueba.
- El relevo puede quedarse pegado sin errores (el latido no avanza): reiniciarlo. "timed out" seguidos = la Vita no
  responde (colgada o dormida).
- Comparar corridas por tramo de comandos por frame (líneas `seg:` del log de captura) sirve sin el registro extremo
  (comando en `etapa2_fase2_estadisticas.md`).

## Reglas de trabajo vigentes
- Versiones de prueba `.x` (siguiente: gepresent v13.12.x; GePatch dev67.x si cambia); commit solo con la confirmada.
- Pruebas: qué juego, qué hacer, en qué fijarse; opciones con el nombre del menú; resumen simple después de cada cambio.
- Registro: mediano durante el desarrollo, extremo para análisis, mínimo para publicar.
