# Estrategia de diagnóstico con pocos tokens

Propuesta del usuario (preparada con Gemini, 2026-10-24) para no grabar segundos de juego.
Grabar mucho gasta contexto y mete ruido. La causa de un error visual casi siempre está en
unos pocos comandos de un frame, o en el frame anterior. Estado: implementado en dev49.

| Propuesta | Dónde quedó |
|---|---|
| 1. Ráfaga de 3 frames: guardar N-2, N-1 y N para ver los render targets que se dibujan un frame antes de usarse (retrovisor, sombras, agua) | `dump.c` (`DUMP_FRAMES 3`): L+R+SELECT graba **3 frames seguidos** (N, N+1, N+2). Un búfer circular de los frames *anteriores* obligaría a grabar todo el tiempo y el plugin iría lento; con 3 frames hacia adelante, el frame N+1 ya ve lo que el N dejó en sus render targets. |
| 2. Linaje de texturas y render targets, y viewports con escala inesperada | `gpd.py linaje VOLCADO` |
| 3. Diff de comandos, solo los registros críticos (VIEWPORT, SCISSOR, REGION, FRAMEBUFPTR, TEXADDR) | `gpd.py diff ACTIVO [PASIVO]`: lo que cambió el plugin por tramos de draws (solo los cambios entre tramos) y, con un volcado pasivo del mismo punto, dónde el juego mandó otra cosa |
| 4. Inspector de VRAM falsa y z-buffer (rt1, Z_SAVE) | `gpd.py vram VOLCADO`: fb y texturas en la VRAM falsa con choques, y cuándo se guardó o restauró la VRAM del z respecto de los draws con profundidad |

## Orden recomendado para un volcado nuevo
1. `gpd.py original VOLCADO CARPETA`: el mismo frame sin GePatch. ¿El error es del plugin?
2. `gpd.py linaje VOLCADO`: ¿qué fb está involucrado y de qué frame viene?
3. `gpd.py diff VOLCADO`: ¿qué cambió el plugin en esos draws?
4. `gpd.py vram VOLCADO`: ¿hay choques en la VRAM falsa o un z guardado en mal momento?
5. Recién entonces, `informe` (filtrado con grep por los draws que salieron arriba) e `imagenes`.

El primer caso para probarlo es el retrovisor de Ridge Racer, que quedó vacío con el recorte 3D (dev48).
