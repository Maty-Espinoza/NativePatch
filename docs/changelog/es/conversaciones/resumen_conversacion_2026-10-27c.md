# Resumen de la conversación del 2026-10-27: análisis de arquitectura → dev58.1…dev58.7 (sin commit)

Para continuar en una conversación nueva. Leer antes `contexto_ia.md`, `errores_conocidos.md`,
`analisis_arquitectura_2026-10-27.md` (origen de esta versión) y **la sección "Metodología de prueba" de abajo**.

## Estado al cerrar (2026-10-27, 12:00)
- **dev58.7, sin commit**, sobre dev57 (`2c4e937`). Tests en PC: **189 comprobaciones, 0 fallas**. md5 `1121c3be…`.
  En la Vita, en `~/Downloads/ge_patch.prx`, en el laboratorio (`seplugins/` y `versiones/ge_patch_dev58.x.prx`,
  x = 1…7). Parches: `vitabridge/gepatch_dev58.7.patch` (`git apply` sobre `origin/dev`; también 58.1…58.6).
- **Lo confirmado:**
  - dev58.7 con todo por defecto = dev57 (GT media vuelta sin bugs, Vita).
  - **GT con "Vertices 2D (HUD, sprites)" = "por copia (dev58)"**: una vuelta en la Vita sin basura, 34–36 fps,
    GE ~25 ms (como dev57); en el laboratorio una vuelta limpia. Logs en `vitabridge/etapa6_dev58.7_vita/` y
    `etapa6_dev58.7_lab_GT_v2d2.log`.
  - **"Textura dentro de un fb = el fb"**: sin beneficio en GT ni en SH (ver abajo). Dejarla en **no**.
- **Abierto: RR con "por copia" en la Vita** se ve mal (no se ve la pista ni los autos). Volcados y log en
  `vitabridge/etapa6_dev58.7_vita_RR2/` (18400 = carrera, 6477 = menú). Hipótesis principal abajo.
- Opciones guardadas en la Vita al cerrar: GT `v2d=2` (+ `comprobar=1 tex_fb=0`, perfil); RR `v2d=2 escala=3`
  (+ posiblemente `fb_parte`/`sin_efecto` tocados: revisar `gepatch_ULJS-00001.txt` y dejar solo `escala=3`).
  Laboratorio: `memstick/gepatch_UCUS-98632.txt` = `comprobar=1 tex_fb=0 v2d=2`.

## Qué cambia dev58 respecto de dev57 (todo apagado por defecto salvo el menú y los diagnósticos)
1. **Tabla de framebuffers con tamaño real** (`struct Fb`; antes 10 arreglos). `findFramebuf` reconoce una textura
   dentro de un fb (`fb_off`). Opción "Textura dentro de un fb = el fb (dev58)" (`fb_parte`): **sin beneficio**.
   El tamaño se calcula "ancho × 272 × bpp" y sobreestima (GT guarda texturas ahí: 220–480 por frame "dentro del fb").
2. **Vértices 2D por copia** (`v2d=2`): los vértices del juego no se tocan; se copian escalados a un anillo propio
   (0x0A740000, 256 KB; VMEMO bajó a 16 384 entradas) y el PRIM pasa a `BASE 0A + CALL` a una lista del ring
   (`BASE juego; palabra anterior; OFFSETADDR 0; BASE; VADDR copia; PRIM; BASE; VADDR como lo deja el GE; BASE juego;
   RET`). Draws 2D seguidos se encadenan. Modos de prueba en la misma opción: `v2d=3` copia sin aplicar (CPU sola),
   `v2d=4` lista mínima sin copia (solo el mecanismo CALL).
3. **Menú reducido** (7 opciones); el resto solo por archivo (`copias_ge original sublistas comprobar fb1024
   vram_real dx dy autocap cpu intercambio rt_esc1`). Perfiles conocidos: GT, RR y LocoRoco.
4. **Diagnósticos**: `stats dev58: choques_memo texturas_dentro_de_fb`; `fb … nunca mostrado en 120 frames`;
   `lista con mas de N comandos … se corta en <dir>`; **volcado automático** al primer `CALL a direccion invalida`.
5. **`BASE_PLUG`**: el BASE que el plugin escribe en la lista del juego (copias 2D y `txCall` de copias/rt1) lleva
   una marca en los 16 bits bajos (`0x100AD1E7`); `mayBePatch` solo reconoce ese valor.

## Errores encontrados y corregidos en el camino (cada uno costó una corrida)
- dev58.1: la palabra anterior al PRIM podía ser una que el plugin parchea (viewport…): se emitía sin escalar y el
  recorrido siguiente la escalaba encima del BASE → CALL sin base → GE ejecutando basura. Corregido: solo palabras
  que el plugin nunca parchea, dentro del mismo tramo (`walk_from`) y con pila < 6.
- dev58.2: usar `txCall` prendía `tx_patched_any`, que **apaga la caché de sublistas entera** (GT: 15–27 ms de
  recorrido, cuelgue). Corregido: marca propia `v2d_patched_any`.
- dev58.3–58.4: el diagnóstico "textura dentro del fb" escribía una línea por textura y llenaba el log (se perdían
  las últimas líneas antes de cada caída). Ahora una por fb.
- dev58.6 → 58.7 (**la causa real de las caídas de GT**, vista en el volcado automático del laboratorio): GT reusa su
  lista entre frames y escribió `BASE 0A` (valor idéntico al parche del plugin) donde antes había un VERTEXTYPE
  parcheado; la memoria de parches lo tomó por el parche, devolvió el VERTEXTYPE, el recorrido siguió con otro BASE
  y la CALL del juego lo mandó a datos flotantes (`091f3800`), que escaló como si fueran viewports → corrupción
  del juego. Corregido con `BASE_PLUG`. Test `test_base_del_juego_no_es_parche`.

## Hipótesis para RR (lo primero de la próxima conversación)
- En RR, con `v2d=2`, el buffer grande al final del frame tiene cielo, mar y HUD pero **ni pista ni autos**, el
  z-buffer está limpio (el mundo no se dibujó en la pantalla) y el retrovisor muestra una pista de frente.
- El recorrido del plugin es **idéntico** en los dos modos (replay en el PC, 1562 draws): la diferencia está en el GE.
- RR usa rt1 (espejo), cuyas listas de entrada/salida también van por `BASE + CALL` (`txCall`), ahora con
  `BASE_PLUG`. **Si el GE real no ignora los 16 bits bajos de BASE** (PPSSPP sí los ignora), el CALL a la lista de
  salida del rt se pierde, el framebuffer se queda en el rt y el mundo se dibuja ahí. GT no usa rt1.
- **Cómo decidirlo:** (1) RR con `v2d=2` en el **laboratorio** (PPSSPP ignora los bits bajos): si RR se ve bien ahí,
  es el GE real → quitar la marca y reconocer el BASE del plugin por lo que sigue: **BASE 0A seguido de un CALL al
  ring del plugin** (el juego nunca llama ahí); (2) si en el laboratorio también falla, es otra cosa del mecanismo
  CALL con rt1 (orden rt1SelfDraw/rt1FromBig → v2dDraw, slots, VADDR restaurado) y hay que bisecar con `v2d=4`.
- Mientras tanto RR va con `v2d=1`.

## Metodología de prueba (lo que funcionó; usarla desde el inicio)
1. **Ambiente:** en el contenedor, pspdev (tarball de GitHub) + clon de Adrenaline en `/home/claude/tc`;
   `make ADRENALINE=/home/claude/tc/Adrenaline/cef`; tests `make -C tests` (gcc-multilib). Conectar las carpetas del
   Mac `~/Downloads` y `~/Documents/gepatch-lab`; entregar con `device_commit_files` a `~/Downloads/ge_patch.prx`,
   `~/Downloads/vitabridge/enviar/ux0/pspemu/seplugins/ge_patch.prx`, `gepatch-lab/memstick/seplugins/ge_patch.prx`
   y `versiones/ge_patch_<v>.prx`, más `vitabridge/gepatch_<v>.patch`; verificar md5 en `vitabridge/enviados/`.
2. **Cada cambio de comportamiento es una opción apagada por defecto**: la versión base debe comportarse como la
   anterior; se prende una opción por juego y por corrida. Decirle al usuario **qué juego, qué opción del menú y
   qué valor**, una prueba por mensaje.
3. **Laboratorio antes que Vita** para lo que cuelga: es determinista (misma basura en los mismos frames que la
   Vita), no se cuelga y permite bisecar (`v2d=3`/`v2d=4`). El usuario corre
   `PPSSPP_ADRENALINE=estricto bash ~/Documents/gepatch-lab/jugar.sh gt.iso > ~/Documents/gepatch-lab/ppsspp_salida.txt 2>&1`
   (la salida de PPSSPP con el renderer por software no muestra errores del GE: sirve poco). El log queda en
   `gepatch-lab/memstick/ge_patch.log`; los `.gpd` en `memstick/`. Las opciones del juego en
   `memstick/gepatch_<ID>.txt` las edito yo desde el shell del Mac.
4. **Replay en el PC** (`tests/replay.c`, nuevo): `gcc -m32 … -o replay_bin replay.c stubs.c ../log.c ../menu.c
   ../dump.c`; `W=<dir> SC=<escala> ./replay_bin VOLCADO.gpd FRAMES V2D` carga las listas (valores originales) y los
   vértices del volcado en la memoria simulada, recorre N frames y reporta palabras de lista alteradas sin memo y
   basura en el log; con `W=` imprime las palabras del juego, la lista del plugin y la copia de vértices; compilado
   con `-DREPLAY_TRACE=N` imprime los primeros N comandos y una línea por PRIM (estado del recorrido) para comparar
   modos (`diff`). Lo que el volcado no tiene (sublistas de huesos) se rellena con RET. Sirve para el recorrido;
   **no** modela al GE. Volcados útiles: GT dev57 frame 8047 (`recibidos/`), RR 6477/18400 (`etapa6_dev58.7_vita_RR2/`).
5. **Leer los volcados sin gpd.py** cuando la escala no es 2x: `gpd.py imagenes` supone 960x544; con un script
   corto (registros MEM kind 0/2 = VRAM real ini/fin, 1/3 = VRAM falsa) se dibuja el buffer grande con su pitch real
   (720 a 1,5x) y el z-buffer (en 0x04090000 a 1,5x) como PNG. Un z-buffer limpio = el mundo no se dibujó.
6. **Guardar cada log apenas llega** en `vitabridge/etapa6_<versión>_<prueba>/` (el log de vpjuego y `ge_patch.log`
   se pisan en cada corrida). Si la Vita se cuelga, el relevo queda con un `.parcial` hasta que vuelva.
7. **Códigos:** `C1-2858-3` es el genérico de la Vita ("la aplicación se cerró"), no distingue PSP/ARM.
   Las líneas `CALL a direccion invalida` / `fb … pitch=<raro>` / `offset juego=(…,592)` en el log significan que el
   recorrido está leyendo datos como comandos: el juego ya está corrompido; el volcado automático lo captura.
8. Hallazgos de plataforma: GT guarda sus **sublistas en la VRAM falsa** (0x0A1BC000+) y **reusa la lista entre
   frames** cambiando solo algunas palabras; RR enlista con direcciones sin caché (0x48…) y actualiza el stall cada
   dos palabras (`BASE; CALL sub`); `vpjuego` marca siempre "GE en stall 0 %" (no distingue CPU lenta de GE lento).
   La memoria de parches en SH tiene ~120 choques por frame (inofensivos porque SH rearma sus listas; pendiente).

## En simple
- La idea de copiar los dibujos planos en vez de modificar los datos del juego funciona en Gran Turismo (una vuelta
  completa, mismo rendimiento). Las tres caídas del día fueron tres errores distintos del mecanismo, el último
  encontrado gracias a un volcado automático en el emulador del Mac: el juego escribió por su cuenta un valor igual
  a la "nota" del plugin, y el plugin leyó números como instrucciones.
- En Ridge Racer todavía falla (no se ve la pista): la sospecha es que la firma nueva de esa nota no le gusta al
  chip gráfico real, y se decide con una corrida en el emulador.
- Quedó una forma de trabajo: cada mejora es un interruptor, se prueba en el emulador antes que en la consola, y
  hay una herramienta en el PC que reproduce el recorrido del plugin sobre capturas reales sin tocar la Vita.
