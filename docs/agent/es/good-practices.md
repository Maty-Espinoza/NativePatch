# Buenas prácticas (leer antes de tocar el repo)

Reglas para que el código, los comentarios, los archivos y la documentación de NativePatch no se vuelvan a desordenar.
Son obligatorias para cada conversación con Claude y valen también para cualquier persona que colabore. Si una regla
choca con lo que pide el usuario en el momento, manda el usuario; después se actualiza esta lista.

## 1. Comentarios del código
- Un comentario dice **qué hace** la función o el bloque y **cómo** lo hace. Nada más.
- Cada archivo empieza con un comentario corto: qué es, de dónde se llama, qué incluye. Sin historia.
- **Prohibido en el código:** números de versión (`0.0.8.16`, `dev66`, `v13.10`), fechas, "antes se hacía…", "en la
  prueba X pasó…", resultados de mediciones, nombres de juegos como justificación, "(prueba)". Todo eso va a la
  documentación (`docs/`) o al `CHANGELOG.md`.
- **Sin listas TODO en el código.** Los pendientes van a `TODO.md`, que es la única lista. Un `TODO:` suelto en una
  línea solo se acepta si el pendiente es tan local que no tiene sentido fuera del código, y aun así se anota también
  en `TODO.md`.
- Los comentarios se tocan **solo cuando cambia lo que hace el código**. No se reescriben "para mejorar" ni se les
  agrega lo que se descubrió probando.
- Los textos de los logs son parte del comportamiento, no comentarios: sin versiones ni historia adentro ("stats
  compuerta:" y no "stats compuerta (dev66):"). Si un script los lee (`tools/*.py`), cambiar los dos.
- Idioma: el código y sus comentarios están en español, sin tildes (las tildes rompen algunos compiladores y
  terminales). Nombres de funciones y variables cortos y consistentes con el archivo.

## 2. Código muerto y simplicidad
- Lo que no se usa, se borra: funciones, variables, interruptores, parámetros, ramas de `if` que ya no pueden darse,
  archivos de código que ya nadie incluye. Git guarda la historia; el código no.
- Un interruptor o modo de prueba que cumplió su función se quita en la misma tarea que lo descartó (o en la siguiente
  versión confirmada). No se deja "por si acaso".
- Código compacto pero entendible para un humano que lo lee por primera vez. Se prefiere simplicidad y legibilidad a
  buenas prácticas de manual (constantes para todo, capas de abstracción) cuando estas solo agregan ruido.
- Nada especulativo: no se agregan opciones, flexibilidad ni manejo de errores imposibles "para después".
- Antes de cambiar algo hay que entender el origen del problema (`workflow.md`, regla "primero el origen").
- Después de cada cambio: compilar sin warnings (`-Wall` ya está en los dos plugins) y probar en la Vita.

## 3. Archivos del repo
- **Lo que no se usa, fuera del repo.** Herramientas de experimentos terminados, scripts de una vez, volcados,
  binarios: se borran. Si vale la pena recordar que existieron, una línea en `CHANGELOG.md` o en `docs/changelog/` y la
  etiqueta de git donde quedaron (como `gepatch-dev66`).
- Las salidas de compilación van a `build/` (ignorado por git). Nunca se suben binarios ni `.o`.
- Los documentos tienen un solo lugar: vigentes en `docs/dev/` y `docs/agent/`; historia en `docs/changelog/`. No se
  crean carpetas nuevas ni copias "v2" de un documento: se edita el que está.
- Nombres de archivo en inglés y en minúsculas, con guiones (`how-it-works.md`); los mismos nombres en `en/` y `es/`.
- Al terminar una tarea se revisa con `git status` que no queden archivos sueltos (parches, logs, pruebas) en el repo.

## 4. Carpeta del relevo (`vitabridge/`) y la Vita
- El relevo borra solo lo de más de 7 días en `enviados/`, `recibidos/` y `respuestas/` (`relay.md`). Lo que vale la
  pena conservar de una prueba se copia **antes** a `vitabridge/etapaN_<versión>/<juego><n>/`.
- No se dejan en `vitabridge/` paquetes de git, parches ni carpetas de experimentos de otra época: se borran al cerrar
  una etapa.
- En la Vita, los archivos de pruebas (interruptores viejos, logs, volcados) se quitan cuando dejan de usarse; el
  instalador tiene "Limpiar restos de pruebas" para eso (hasta antes de la 1.0).

## 5. Versiones y commits
- Reglas en `../../dev/es/versions.md`. Resumen: hasta el primer release todo es `0.0.x`; cada versión que se manda a la
  Vita lleva un cuarto número (`0.0.8.22`) y va al repo como commit marcado "(prueba)"; la versión confirmada va sin el
  cuarto número y es la que sigue a la última confirmada en GitHub (no la numeración de las pruebas). Cada error
  arreglado = un commit con su versión. No deben quedar versiones perdidas.
- Un commit dice qué versión trae y qué cambió, en una línea. El detalle va al `CHANGELOG.md` cuando la versión se
  confirma.
- No se hacen commits solo de documentación si se pueden juntar con el cambio al que acompañan.
- El número de versión está en un solo lugar por plugin: `nativelink/log.h` y `nativedraw/version.h`.

## 6. Documentación
- `docs/dev/` es para humanos (qué es, cómo funciona, qué se midió, límites); `docs/agent/` para la IA (cómo se
  trabaja); `docs/changelog/` es archivo y no se actualiza. `TODO.md` es la única lista de pendientes; `CHANGELOG.md`,
  el resumen por versión.
- Al terminar cada tarea se actualizan los documentos afectados **en inglés y en español**, con los descubrimientos y
  las decisiones (no en el código). Si algo quedó pendiente, va a `TODO.md`.
- Los documentos vigentes se escriben para alguien que no sabe nada del proyecto: frases cortas, sin jerga sin
  explicar, sin historia de versiones salvo que importe para entender el presente.
- Los resúmenes de conversación (`claude/resumen_conversacion_*.md` en el proyecto de claude.ai) dicen dónde quedó
  todo y qué sigue; los anteriores al 2026-10-30 están también en `docs/changelog/*/conversaciones/`. Antes del primer
  release se quitan del repo.

## 7. Cómo mantener esto
- Si una regla de aquí ya no sirve o falta una, se cambia este archivo (en los dos idiomas) en la misma tarea.
- `CLAUDE.md` en la raíz del repo obliga a leer este archivo al empezar; no se quita esa obligación.
