# GEPTEST: resultados en la Vita

Complementa `modelo_vita.md` (parte A del diseño).

## Resultados de la batería (2026-10-22)

**Cómo se corrió:** `tools/geptest`, con las 112 pruebas de pspautotests/gpu recompiladas y corridas en un solo proceso por el corredor (unos 0,6–0,9 s por prueba en la Vita).
- Hay 7 pruebas cuyo resultado cambia por el propio arnés (la recompilación o el orden de ejecución): `dither`, `clipping/homogeneous`, `simple` y `signals/*`. En ellas PPSSPP, corrido de la misma forma, se aleja de la PSP igual que la Vita.
- Por eso la comparación justa es Vita contra PPSSPP con los mismos binarios y el mismo orden.

**Texto (lo que cada prueba imprime):**
- Vita = PSP real en 101 de 112.
- PPSSPP (mismo arnés) = PSP en 101.
- Vita = PPSSPP en 103.

**Diferencias reales:**
- **`ge/edramswizzle` (reflejos de la VRAM):** la Vita es idéntica a la PSP en las 389 líneas. PPSSPP difiere en 229: en esto el que se equivoca es el emulador. Es lo primero que tiene que corregir el perfil Vita.
- **`exact/pixel`:** cambia una suma de verificación de un caso de dither o de caché de texturas. En cada ejecución cambia un caso distinto, así que parece depender del tiempo, no de un comportamiento fijo.
- **`signals/*`:** la Vita se acerca más a la PSP que PPSSPP. El resto de las diferencias en este grupo vienen del orden de ejecución.

**Pruebas que tumban Adrenaline en un solo proceso** (con un proceso por prueba terminaron bien):
- `transfer/invalid`: escribe más allá de la VRAM (0x04800000), donde en la Vita hay memoria de Adrenaline. Va al final de la lista.
- `ge/context`: choca con la restauración del contexto del GE que hace el corredor.

**Imágenes:** la Vita y PPSSPP (mismo arnés) son iguales en todas las pruebas de dibujo: `texmtx`, `complex`, `rendertarget` y `primitives`. Las diferencias con las capturas de la PSP vienen de la recompilación, no de la Vita.

**Conclusión:**
- En lo que cubren estas pruebas, el GE de la Vita bajo Adrenaline se comporta como el de una PSP: mezclas, stencil, texturas, filtros, recorte, copias de bloque y reflejos de la VRAM.
- Las diferencias que costaron trabajo no son del GE, sino de Adrenaline como sistema:
  - VRAM de 2 MB;
  - las listas con contexto congelan el juego;
  - LoadExec desde un juego cierra Adrenaline;
  - escribir más allá de la VRAM cierra Adrenaline.
- Para el modelo:
  - el perfil Vita de PPSSPP necesita, como mínimo, los reflejos de la VRAM de la PSP real;
  - los riesgos del sistema se documentan, no se emulan;
  - el paso siguiente son las pruebas propias del punto A (TEXSYNC, copias RAM↔RAM, escritura del alfa, orden entre listas y flips).
