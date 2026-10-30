# Cambios

Versiones: `docs/dev/versiones.md`. El detalle de cada paso está en `docs/changelog/` (antes del primer release se quitan
de ahí los resúmenes de conversación; este archivo queda como el resumen).

## NativeLink 0.0.3 + NativeDraw 0.0.7 (2026-10-30)
Probadas en la Vita como NativeDraw 0.0.7.1–0.0.8.12 y NativeLink 0.0.3.1–0.0.3.3 (logs en
`Descargas\vitabridge\etapa3_<versión de prueba>\`); lo confirmado es 0.0.8.12 + 0.0.3.3. Detalle en
`docs/dev/etapa3_plan.md` (paso 3.3 en KOF 96).
- **Etapa 3, paso 3.3 en KOF 96:** la Vita dibuja las peleas al doble. Sombreadores propios (`nativedraw/sombreadores/`):
  borrados, color por vértice, mezcla, alfa test y swizzle; imágenes intermedias (KOF dibuja la pelea en una imagen
  escondida de la VRAM); paletas de 16 colores; scissor; la imagen normal va debajo.
- **Texturas por DMA:** las de la RAM al momento y las de la VRAM desde la copia del espejo de su flip; solo los grupos
  de 8 filas que usan los draws; el swizzle lo ordena la GPU.
- **NativeLink:** espejo completo para frames 2D (interruptor 16) y `espejo_espera` (32: el flip del PSP espera la copia
  del espejo). Buffer de cada línea del log de 160 a 256 bytes (las largas se cortaban).
- **Volcados** por niveles, reproducción de un volcado en la Vita y `tools/volcado.py` (`docs/dev/volcados.md`).
- **Cuadros rotos de un frame en las peleas (arreglado):** la Vita reescribía texturas, vértices, paletas y la copia de
  la imagen normal mientras su GPU todavía los leía (iba 3 a 8 escenas atrasada). Se midió con las notificaciones de GXM
  (líneas `gpu:`). Ahora no se manda otro frame si la GPU no terminó el de hace 2 (se repite la superficie), se espera a
  la GPU antes de reescribir (como mucho 30 ms) y el registro siguiente es uno que la GPU ya terminó de leer.
  Reescritos con la GPU leyendo: 0.
- **Rendimiento:** la revisión de frames distintos y el "tapado" leen la superficie solo si la GPU ya la terminó (sin
  `sceGxmFinish`, que costaba ~20 ms en KOF); 4 copias de la imagen normal.
- **Limpieza:** los comentarios dicen qué hace cada función y cómo (la historia pasó a `docs/dev/`); código y contadores
  sin uso quitados (el binario compilado salió idéntico).
- Medido en peleas de KOF 96 (cada 10 s), 0.0.8.11 → 0.0.8.12: frames de la Vita ~350 → ~385–410 (~35 → ~40 fps), CPU
  por frame ~13 → ~6,5 ms, esperas a la GPU ~1,5 s → 0, revisión de frames distintos ~2,3 s → ~0,03 s, frames
  repetidos 40–80 → 1–12. El juego sigue a 59,9 fps. Ahora el límite es la GPU de la Vita (~165 frames cada 10 s no se
  mandan porque va atrasada).
- Error menor conocido: el botón PS en KOF 96 (`TODO.md`).

## NativeDraw 0.0.6 (2026-10-30)
- **Etapa 3, paso 3.3 (primera versión):** con `dibujar`, la Vita dibuja el frame con su GPU a partir del registro, al
  doble (2D simple: rectángulos y triángulos con textura, sin transparencias). Retro City Rampage se dibuja entero así.
  Lo que todavía no sabe dibujar se muestra con la imagen normal.
- Si algo que no está en el registro tapa la imagen (el diálogo del botón PS del PSP), se muestra la imagen normal.

## NativeDraw 0.0.5 (2026-10-29)
- **Etapa 3, paso 3.2: registro del frame.** Con `dibujar`, el capturador guarda cada draw del frame (vértices copiados,
  estado del GE, paletas) para que la Vita lo dibuje en el paso 3.3. Retro City y KOF 96: ~99 % de frames completos;
  GT sigue a 60 fps. La línea `registro:` del log dice qué usa cada juego.

## NativeDraw 0.0.4 (2026-10-29)
- **Etapa 3, paso 3.1: menú L+R+START** dibujado por NativeDraw encima del juego; mientras está abierto el juego no
  recibe botones. Por ahora muestra las versiones de NativeLink y NativeDraw (las opciones que hoy son archivos se
  agregarán después). Funciona aunque `dibujar` esté apagado.

## NativeDraw 0.0.3 (2026-10-29)
- **Etapa 3, paso 3.0 (presentador transparente):** NativeDraw toma la pantalla de Adrenaline: la imagen del juego de PSP
  pasa por una superficie propia del doble de tamaño dibujada con la GPU de la Vita (por ahora una copia, sin más
  detalle). Respeta la escala, la posición y la proporción que elige Adrenaline; los juegos de PS1 no se tocan.
  Apagado por defecto: se prende con `ux0:data/nativedraw/dibujar`. Costo ~0,15 ms por frame. Detalle y lecciones
  (memoria CDRAM y videos, z obligatorio) en `docs/dev/etapa3_plan.md`.
- Relevo (`tools/vitabridge.py`): duerme sin pedidos (deja dormir a la Vita: `nosleep off`) y abre el FTP solo para lo
  necesario.

## NativeLink 0.0.2 + NativeDraw 0.0.2 (2026-10-29)
- **NativeLink:** se quita todo lo heredado de GePatch que ya no se usaba: escalado en el PSP (arquitectura A), VRAM
  falsa, presentación por descriptor, menú L+R+START viejo, opciones por juego, captura al log y volcados .gpd
  (L+R+SELECT), pruebas del PC. Queda lo de la etapa 2, sin cambios: buzón para NativeDraw, espejo de la VRAM, compuerta,
  telemetría y log. 3 889 → ~620 líneas.
- **NativeDraw:** se quita la presentación de la imagen escalada de GePatch, el banco de pruebas GXM de la etapa 1 y la
  escritura de volcados (anillo de 24 MB). Queda el capturador y el log de NativeLink.
- **Instalador** (`NativePatch.vpk`, nuevo): instala/actualiza los dos plugins (Adrenaline 8: `plugins.txt`; taiHEN:
  `*PSPEMUCFW`), desinstala y limpia los restos de las pruebas (temporal, hasta antes de 1.0).
- **Repo:** fuera las herramientas de pruebas pasadas (laboratorio PPSSPP, geptest, kbench, vprobe, volcados .gpd);
  ambiente por scripts (`tools/preparar.sh`, `tools/compilar.sh`, `tools/windows/`). Documentos separados en `docs/dev`
  (vigentes) y `docs/changelog` (archivo). README nuevo, licencia GPLv3.
- Probado en la Vita: Gran Turismo igual que con GePatch dev66 + gepresent v13.13 (≥ 60 fps en lo más pesado, 0
  DISTINTAS); el instalador instaló y limpió los restos (`docs/changelog/etapa3_preparacion.md`).

## NativeLink 0.0.1 + NativeDraw 0.0.1 (2026-10-29)
- GePatch pasa a llamarse **NativeLink** y gepresent **NativeDraw**; cada uno en su carpeta. Nombres nuevos de archivos,
  logs y rutas en la Vita. Mismo comportamiento que GePatch 0.2-dev66 + gepresent v13.13.

## Antes: GePatch y gepresent (2026-10-21 a 2026-10-29)
- **GePatch 0.2-dev1…dev60 (arquitectura A):** revivir el GePatch de TheFlow en Adrenaline 7/8: el GE del PSP dibuja a
  960x544 con una VRAM falsa. Se arreglaron muchos errores, pero el GE no da abasto con 4 veces los píxeles (GT 24–37 fps,
  SH 15–21) y el engaño de la VRAM causaba errores de imagen. Herramientas: menú, volcados .gpd, laboratorio en PPSSPP,
  relevo con la Vita (vitacompanion).
- **gepresent v1–v10:** plugin de la Vita que presentaba la imagen de GePatch con la GPU de la Vita (escala por juego) y
  escribía el log y los volcados por el PSP.
- **Análisis (2026-10-26/28):** pruebas de hardware (la VRAM del PSP no se ve desde la Vita; ampliarla no es posible) y
  nueva arquitectura **B**: el PSP corre el juego sin cambios y la Vita lee sus listas y las dibuja en HD.
- **Etapa 0–1 (2026-10-28):** cuánto dibuja cada juego (GT ~9 000 draws por frame) y cuánto cuesta en GXM (banco v11).
- **Etapa 2 (dev61–dev66 + v12–v13.13, 2026-10-28/29):** modo sombra + capturador en la Vita, memoria de sublistas,
  espejo doble de la VRAM, T4/T6, análisis del impacto, compuerta antes de encolar, comprobación por adelantado.
  Resultado: GT ≥ 58 fps en lo más pesado (60,2 con v13.13), Ridge Racer y LocoRoco a 60, Silent Hill a su velocidad
  original, 0 DISTINTAS.
- Descartados en el camino: FSR 1 (adrfsr, no mejoraba la imagen), vértices 2D "por copia", leer la RAM del PSP directo
  sin la caché compartida, no releer líneas comprobadas.
