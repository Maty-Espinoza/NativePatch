# Cambios

Versiones: `docs/dev/versiones.md`. El detalle de cada paso está en `docs/changelog/` (antes del primer release se quitan
de ahí los resúmenes de conversación; este archivo queda como el resumen).

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
