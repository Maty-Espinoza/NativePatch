# Resumen de la conversación del 2026-10-26: pruebas de hardware de la Vita

Para iniciar la próxima conversación (análisis y diseño de la nueva implementación de GePatch).
Leer junto con `sugerencias_nueva_implementacion.md`, que tiene las conclusiones y la propuesta.

## Punto de partida
- Antes de esta conversación, KBENCH había medido que el ARM de la Vita puede recorrer listas en paralelo
  (memoria compartida; Kermit inservible).
- El usuario propuso **sacar la VRAM a la memoria de la Vita y dejar en la VRAM del PSP solo lo que dibuja**:
  paginar la VRAM.
- Se armó un plan de pruebas en dos grupos (`pruebas_hardware_vita.md`):
  - **A**, de bajo costo, sin autorización;
  - **B**, con autorización.
- Además, la **ruta E** (ampliar la VRAM) se trató como base de concepto.

## Lo que se hizo
- **VPROBE** (homebrew, `tools/vprobe`, versiones 1–8): mapa de la VRAM, costo de copias, paginación, relleno
  del GE, 3D al doble, competencia por el bus, exploración de la ruta E y lado PSP de B4.
  - Errores de la prueba corregidos en el camino:
    - el scissor de pspgu dibujaba 1 px;
    - la suma de verificación incluía el relleno;
    - el ancho de línea del rt era incorrecto;
    - un color daba 0 en 565;
    - el log iba a una carpeta que el relevo no mira;
    - la búsqueda en la RAM baja cerraba Adrenaline;
    - el XOR de los reflejos estaba mal supuesto.
- **censo.py** (A2): rt por frame en 59 volcados de RR, SH y GT.
- **Lectura de fuentes** (A3): SceCompat, `sceCompatAllocCdramWithHole` y los ganchos de Adrenaline.
- **vpjuego** (B1, plugin de juegos): GE ocupado, CPU libre y fps en RR, LocoRoco, GT y SH, con y sin GePatch.
  - La v1 calculaba mal el GE por frame (100 veces más grande) y la calibración de la CPU.
- **vpcdram** (B4, plugin de la Vita, armado sin vitasdk): búsqueda de la VRAM en el proceso de Adrenaline.
  - La v1 solo miraba bloques de 2 MB o más.
- B2 no hizo falta (KBENCH ya lo midió). B3 y B5 quedan para la nueva implementación.

## Resultados clave
1. **VRAM:**
   - 2 MB de memoria propia, más rápida que la RAM para la CPU del PSP.
   - No está en la RAM del PSP (0x0BE00000 no es la VRAM) ni mapeada en el proceso de Adrenaline.
   - El hardware declara 2 MB y forzar 4 MB no tiene efecto.
   - Reflejos fijos: 0x042 = X^0x2040, 0x044 = X, 0x046 = X^0x2040 con los bits 5–9 rotados.
   - **Ruta E cerrada de los dos lados.** La copia por flip a 0x0A400000 sigue siendo obligatoria.
2. **Costos:**
   - Copiar 1 MB con el GE o con DMA: ~2,3–2,7 ms.
   - Borrar 960x544: 0,87 ms.
   - Textura estirada por franjas: 1,45 ms; con un sprite ancho y textura lineal: 32 ms.
   - Reducir la pantalla: 1,9 ms; ampliar 480x272: 3,3 ms.
   - Las copias del GE a la RAM frenan a la CPU un 10–20 %; dibujar desde la VRAM no frena a nadie.
3. **Paginación:**
   - Pantalla entera: 5 ms, descartada.
   - Rt al doble en el espacio del z: 0,5 ms (128x128), 4 ms (512 KB), 8 ms (1 MB). Solo conviene para rt chicos.
4. **Juegos reales (B1):** con GePatch el GE trabaja ~3–4 veces más (4 veces los píxeles). Es el límite de fps.

| Juego | GE sin → con (ms por frame) | fps con GePatch | CPU libre sin GePatch |
|---|---|---|---|
| RR | 5 → 22–27 | 20–30 | 29–42 % |
| LocoRoco | 1–3 → 3–9 | 60 (caídas a 30–55) | 25–47 % |
| GT | 8–13 → 25–38 | 24–30 | 3–8 % |
| SH | 18–37 por flip → 40–60 | 15–21 | ~0 % |

5. **En los juegos medidos, el costo de GePatch parece real, no un desperdicio de lógica** (la relación no pasa
   de 4). Queda por desglosar SH, donde los pases propios podrían sumar.

## Decisiones y propuestas
- **Candidata principal:** **escala por juego** (1,5x = 2,25 veces los píxeles) con ampliación final por franjas.
  Deja ~830 KB de VRAM libre para rt y amplía el margen del límite de 4096.
- **Todas las copias escaladas por franjas.** Juntar la copia por flip con la ampliación.
- **ARM asíncrono:** pasa a la fase 4, solo para juegos limitados por la CPU (GT, SH).
- **Telemetría de vpjuego dentro de GePatch.**
- **Adrenaline 8:**
  - L al abrir un juego desactiva todos los plugins, así que el modo pasivo de GePatch con L ya no sirve;
  - activar y desactivar plugins en `plugins.txt` es fácil y rápido;
  - en SH el menú de GePatch no abre, así que hay que usar el archivo de opciones.

## Estado al cerrar
- **Vita:**
  - `plugins.txt` = `vpjuego on`, `ge_patch on`, `kbench off`;
  - `ur0:tai/config.txt` original restaurado (vpcdram deja de cargarse al reiniciar);
  - VPROBE 8 en `ux0:/pspemu/PSP/GAME/VPROBE`.
- **Repo:**
  - nuevos `tools/vprobe/` (main.c, juego/, vita/, README), `tools/censo.py`;
  - documentos actualizados en el proyecto y en `docs/`;
  - **sin commits** (ni las herramientas ni los documentos).
- **Plugin:** sin cambios (dev53).
- **Toolchains:**
  - pspdev funciona en el contenedor del asistente;
  - para la Vita, vita-toolchain compilado desde el código fuente + stubs de vita-headers
    (`tools/vprobe/vita/build.sh`), porque vitasdk.org está bloqueado.

## Para la próxima conversación (análisis)
1. Leer `sugerencias_nueva_implementacion.md`, `filosofia.md` (hoja de ruta revisada) y este resumen.
2. Decidir la arquitectura de la nueva implementación (escala por juego, copias por franjas, perfiles,
   telemetría, fase 0 en Adrenaline 8).
3. Pruebas que siguen:
   - prototipo a 1,5x medido con vpjuego (LocoRoco demo + RR);
   - desglose del GE en SH por archivo de opciones;
   - ¿SH hace 2 flips por frame?;
   - GT con contra sin después de arreglar sus regresiones.
