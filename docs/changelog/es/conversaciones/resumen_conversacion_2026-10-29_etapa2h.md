# Resumen para seguir en otra conversación: etapa 2, fase 1 de análisis (2026-10-29, octava conversación)

Leer: `flujo_de_trabajo.md` → este resumen → `etapa2_fase1_impacto.md` (resultados y propuesta de la fase 2) →
`registros.md` → `TODO.md`.

## Dónde quedamos
- **Commit `6193d07` en `dev`, con push a GitHub: GePatch 0.2-dev65 + gepresent v13.9** (fase 1 de análisis + niveles de
  registro + `docs/etapa2_fase1_impacto.md`, `docs/registros.md`, `tools/analisis_fase1.py`, flujo de trabajo
  actualizado). El push se hizo desde el contenedor (`add_repo` con push, con permiso del usuario): el commit quedó a
  nombre de Claude; al usuario no le importa la autoría, le importa que los cambios queden arriba.
- El repo de Windows quedó igual a GitHub (`dev...origin/dev` sin diferencias). El repo del Mac está atrasado
  (2 commits): pull antes de trabajar ahí.
- Vita: GePatch dev65 (`744ab734…`) + gepresent v13.9 (`9d3439d3…`), verificados (`vitabridge/etapa2_dev65/verif`).
  Registro mediano (sin archivo `registro`). `ux0:data/gepresent/sin_compuerta` = "no", `banco_nucleos` = "no".
- Logs de la fase 1: `Descargas\vitabridge\fase1\gt1 … gt6_confirmacion`.

## Plan acordado con el usuario
1. **Fase 1 (hecha): medir** por qué el modo sombra todavía le cuesta al PSP, sin importar el costo de medir.
2. **Fase 2 (siguiente): mejorar por iteraciones** con esos datos y probar distintas implementaciones.

## Resultados de la fase 1 (detalle en `etapa2_fase1_impacto.md`)
- Todo el costo para el juego es **la espera a la Vita en el sync**: sin ella (`sin_compuerta`) GT va a 59,9 fps y 0,2 %
  de frames lentos; con ella ~58 fps y 16–20 %. Avisos de GePatch (6 µs), espejo (0,07 ms de CPU), lecturas de la Vita:
  no se notan. CPU libre del PSP ~20 %.
- GT encola **una lista por frame** (50–80 mil instrucciones), completa, ~14,3 ms antes del sync. La Vita empieza en
  ~0,3 ms y tarda 12–18 ms (puro trabajo). ~1/3 de los frames llega tarde (mediana 1,5 ms; 12 % al tope de 5 ms).
- **La lista no cambia hasta que el juego encola la siguiente** (0 de 11 436); GT alterna dos zonas de lista. Mover la
  espera ahí: la Vita atrasada en 15 % de los frames en vez de 31 %.
- Tiempo de la Vita (45–60 mil comandos): 14,6 ms de reloj, 12,2 de procesador (pierde ~17 % del núcleo 1); revisar y
  aplicar sublistas 7,2 ms (aplicar ~7 µs c/u por sumar ~200 contadores); lectura propia de 256 B 3,0 µs, línea de la
  caché compartida 1,13 µs (más cara por línea que leer directo).
- Antes de la fase 1 se probó v13.8.1/v13.8.2 (no releer líneas ya comprobadas): leyó ~45 % menos pero no bajó el tiempo
  y subieron las DISTINTAS → descartado (sin commit; `vitabridge/gepresent_v13.8.2.patch`).

## Propuesta para la fase 2 (orden: ganancia / costo)
1. Mover la compuerta a "antes de encolar la lista siguiente" (con autocomprobación para otros juegos).
2. Capturador: leer directo de a 256 B en vez de la caché compartida; los lectores solo comprueban sublistas.
3. Aplicar sublistas sin sumar todas las estadísticas.
4. Revisar sublistas más barato / por adelantado.
5. Núcleo / prioridad del capturador (cuidado con el audio).
6. DISTINTAS de las sublistas en la VRAM: la memoria compara con el espejo de cuando se guardó la sublista, no con el
   del frame actual (hipótesis; todas las DISTINTAS son 0x041xxxxx). Arreglar antes de dibujar.

## Lecciones
- Medir cambia lo medido: leer la hora cuesta 390 ns en la Vita; el registro extremo completo subió los frames lentos
  de ~17 % a 27 %. Por eso los niveles de registro y las comparaciones A/B con interruptores.
- Leer menos bytes no acelera si el costo no está en la lectura: medir antes de optimizar (v13.8).
- El usuario hace siempre la misma carrera y auto; no tiene contador de fps (los da el log). Si no menciona el audio es
  porque estaba bien.
- El relevo puede perder la conexión con la Vita ("timed out"): el usuario lo reinicia; lo pendiente en `enviar/` se sube solo.
- En el contenedor, el clasificador puede bloquear `preparar.sh` (descargas) y el push la primera vez: con el permiso
  explícito del usuario funcionaron. Push: commit con `user.name=Claude`, `user.email=noreply@anthropic.com` y
  `add_repo` con push; después llevar el commit al repo del PC con un paquete (`git reset --hard origin/dev` si el
  contenido es el mismo).

## Reglas de trabajo vigentes
- Versiones de prueba `.x` (siguiente: GePatch dev66.x / gepresent v13.10.x o v14.0.x); commit solo con la confirmada.
- Pruebas: qué juego, qué hacer, en qué fijarse; opciones con el nombre del menú; resumen simple después de cada cambio.
- Registro: mediano durante el desarrollo, extremo para análisis, mínimo para publicar.
