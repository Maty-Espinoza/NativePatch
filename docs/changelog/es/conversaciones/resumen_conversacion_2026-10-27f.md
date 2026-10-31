# Resumen de la conversación del 2026-10-27/28 (f): FSR como plugin independiente (adrfsr) y GePatch dev60.2

Conversación aparte de la línea dev58/dev59 (resúmenes c, d, e). Parte del análisis teórico de
`analisis_escalado_frame_vita.md` (§1–§9) y de la simulación de FSR sobre capturas (§9).

## Estado al cerrar (2026-10-28 08:18)
- **Decisión del usuario:** GePatch es el escalado real; FSR es un parche aparte (ampliación rápida de la imagen original).
  **No se mezclan**: se quitó FSR de GePatch.
- **GePatch 0.2-dev60.2, sin commit** = **dev59.13** (otra línea, sin commit; base dev57 `2c4e937`) + un solo cambio: **el modo
  pasivo vuelve a activarse solo con L** al abrir el juego (dev54 había agregado SELECT; el usuario pidió volver al original).
  En Adrenaline 8, mantener L al abrir el juego desactiva todos los plugins de PSP: en la práctica L = GePatch no se carga.
  Parche contra dev57: `vitabridge/gepatch_dev60.2.patch` (md5 `2f36396d…`). `.prx` md5 `b20d9867e9816906f19ce80d1e95674c`,
  instalado con `instalar.sh 0.2-dev60.2` (Descargas, Vita, laboratorio, `versiones/`). Tests: 205 comprobaciones, 0 fallas.
- **dev60.1 descartada** (FSR en el selector de escala: variantes "+ FSR", "FSR solo", START al abrir, marca FSR en el
  descriptor). Queda como referencia en `vitabridge/gepatch_dev60.1.patch`. Si dev59 sigue en otra conversación, el único
  cambio de dev60.2 a llevar es la tecla del pasivo.
- **adrfsr 3b** en la Vita (`ux0:data/adrfsr/adrfsr.suprx` md5 `29e32f09f65b93ddad9d3df1425678f4`, verificado en `enviados/`;
  copia `vitabridge/adrfsr/adrfsr_3b.suprx`; fuente `tools/adrfsr/adrfsr.c` en el clon del Mac, sin commit): **independiente
  de GePatch**. Si la imagen ya mide 960 de ancho o más (GePatch a 2x) no la toca. Es un **plugin de la Vita** (no de PSP):
  listado en `*PSPEMUCFW` de `ur0:tai/config.txt` (el config bajado de la Vita = el subido; respaldo del anterior en
  `vitabridge/adrfsr/config_antes_adrfsr.txt`).
- **adrfsr 1, 2 y 3 nunca cargaron**: la biblioteca de taiHEN se armó a mano con NIDs equivocados (los de taiHEN no son SHA1 del
  nombre: biblioteca `taihen` = 0x203D1A28, `taiHookFunctionImportForUser` = 0x53636E5D). Por eso no había log de adrfsr (el
  relevo sí baja de `ux0:/data/`: probado con `fsr.cg`). 3b = el mismo código de 3 enlazado con el paquete oficial de taiHEN.
  Lo mismo habría pasado con gepresent v10 (descartada sin probar).
- **gepresent v9** (la v10 con FSR adentro se descartó).
- **Nada de FSR probado todavía en la Vita** (el usuario no ha probado adrfsr 3b).

## Lo que mostró la prueba de GT con dev60.1 (log `recibidos/ge_patch.log`, 08:00)
- El usuario eligió "FSR solo" en el menú en medio de la sesión (frame 2460) y no reabrió el juego (ni guardó): la escala solo
  se aplica al abrir el juego, así que **GePatch siguió activo a 2x**. Los errores que vio eran de GePatch a 2x, no de FSR.
- Lección: una opción que solo cambia al reabrir el juego, mezclada con opciones que cambian al instante, confunde. Con FSR
  separado de GePatch el problema desaparece.

## Qué se hizo en la conversación
1. Análisis teórico (`analisis_escalado_frame_vita.md`).
2. Simulación de FSR en el Mac (`coleccion/fsr_sim.py`, `coleccion/fsr/<escena>/`). Sin veredicto visual del usuario.
3. gepresent v10 (FSR dentro de gepresent): descartado.
4. **adrfsr** (plugin de la Vita, FSR 1.0 EASU), sombreador compilado en el PC.
5. FSR como filtro de Adrenaline: preparado en `tools/adrfsr/filtro_adrenaline/`, sin compilar Adrenaline.
6. dev60.1 (FSR en la escala de GePatch): descartada. dev60.2 (pasivo con L).

## adrfsr (resumen simple)
- Adrenaline dibuja la imagen del juego como una textura estirada a toda la pantalla con el sombreador de su filtro. adrfsr
  intercepta tres pasos de ese dibujo (programa de vértices, textura, dibujar) y, si la textura es la del juego, cambia el
  sombreador por FSR justo antes de dibujar. Costo para el PSP: cero.
- Imagen del juego: la textura de 512x272 (pantalla normal del PSP, visible 480x272) o, con GePatch a escala < 2, la textura
  sobre la RAM del PSP (visible según el descriptor de GePatch). Lecturas recortadas a la parte visible.
- **Uso pensado: GePatch apagado** (abrir el juego con L, o `ge_patch.prx` en `off` en `plugins.txt`) y **filtro de
  Adrenaline en "Bilinear"** (en "Original" sin GePatch Adrenaline no dibuja con la GPU y adrfsr no tiene dónde actuar).
- `ux0:data/adrfsr/off` lo apaga (cerrar y abrir Adrenaline); `ux0:data/adrfsr/fsr.gxp` reemplaza al sombreador incluido;
  log en `ux0:data/adrfsr/adrfsr.log`.
- No tiene atajo para prender/apagar en el juego (se le ofreció al usuario; él elige los botones si lo quiere).

## FSR como filtro de Adrenaline (investigación)
- Adrenaline 8 usa `libvitashaders.a` de VitaArchive/vita-shader-collection (su CI compila los `.cg` con el compilador de
  Sony bajo qemu). Agregar el filtro = PR a vita-shader-collection (`fsr_v.cg`, `fsr_f.cg`, listos y compilan) + PR a
  isage/Adrenaline (`adrenaline_fsr.patch`, opción "AMD FSR 1.0", ~10 líneas). Licencias compatibles (MIT / GPLv3).
- Con GePatch a escala < 2 ese filtro recibiría un `IN.texture_size` equivocado (gepresent engancha GetWidth). Sin GePatch
  (el uso pensado) no hay problema.

## Cadena de compilación en el contenedor
- **Archivos Mac ↔ contenedor: `device_stage_files` (Mac → `/mnt/user-data/uploads/`) y `device_commit_files` (desde
  `/mnt/user-data/outputs/` → Mac).** Ojo: al volver a enviar un archivo con el **mismo nombre** en `outputs/`,
  `device_commit_files` entregó la versión anterior (pasó con `ge_patch.prx`): usar un nombre nuevo por versión y verificar
  el md5 en el Mac.
- GePatch: `git clone -b dev https://github.com/Maty-Espinoza/NativePatch` (lectura anónima) + `git apply` del parche;
  pspdev de `https://github.com/pspdev/pspdev/releases/latest/download/pspdev-ubuntu-latest-x86_64.tar.gz` +
  `TheOfficialFloW/Adrenaline` (`make ADRENALINE=.../cef`); tests: `apt-get install gcc-multilib`, `make` en `tests/`.
- Vita: `apt-get install gcc-arm-none-eabi cmake libzip-dev libyaml-dev qemu-user-static gcc-arm-linux-gnueabihf
  libc6-dev-armhf-cross`; libelf 0.8.13 de `http://archive.ubuntu.com/ubuntu/pool/main/libe/libelf/libelf_0.8.13.orig.tar.gz`
  (`--prefix=deps --disable-shared`, `ln -s . deps/include/libelf/libelf`); vita-toolchain (`git submodule update --init`,
  `PKG_CONFIG_PATH=deps/lib/pkgconfig cmake .. -DUSE_BUNDLED_ENDIAN_H=ON`); stubs `vita-libs-gen-2 -yml=vita-headers/db/360`.
- **taiHEN: usar el paquete oficial** `https://github.com/vitasdk/packages/releases/download/master/taihen.tar.xz` (baja bien;
  `include/taihen.h`, `lib/libtaihen_stub.a`). **No generar los stubs a mano**: el plugin no carga y no da ningún mensaje.
  Verificar: `arm-none-eabi-objdump -s -j .vitalink.fstubs.taihen x.elf` debe mostrar `281a3d20 5d6e6353`.
- Sombreadores Cg → gxp: `JagerDesu/vita-shaders`, en `resolve.c` `memmem(start, -1,` → `memmem(start, 0x300000,`; `make
  CC=arm-linux-gnueabihf-gcc`; `qemu-arm-static -B 0x100000000 -L /usr/arm-linux-gnueabihf ./shacc --fragment x.cg x.gxp`.
- adrfsr: `VH=tc/vita-headers STUBS=tc/stubs TAI=tc/taipkg SHACC=tc/vita-shaders sh build.sh` en `tools/adrfsr`.

## Lo que falta probar (en el menú de Adrenaline, "Graphics Filtering" = Bilinear)
1. Cerrar Adrenaline desde la LiveArea y abrirlo (carga adrfsr 3b; el config.txt ya lo lista).
2. **Gran Turismo abierto con L apretado** (GePatch no se carga): ¿se ve más nítido que Bilinear solo? ¿algún error?
3. Comparar sin FSR: subir `ux0:data/adrfsr/off`, cerrar y abrir Adrenaline, mismo juego con L.
4. Ridge Racer igual.
5. Bajar el log de adrfsr (`bajar_*.txt` con `ux0:/data/adrfsr/adrfsr.log`): debe decir "ganchos …" y "primer draw con FSR".
   **Solo cuando el usuario avise que probó** (pidió no buscar logs antes).
6. Después: RCAS (afilado), atajo en el juego si el usuario lo quiere, PR a Adrenaline.

## Dónde está cada cosa
| Qué | Dónde |
|---|---|
| GePatch dev60.2 | `vitabridge/gepatch_dev60.2.patch` (contra dev57; contiene dev59.13) |
| GePatch dev60.1 (descartada) | `vitabridge/gepatch_dev60.1.patch` |
| Código adrfsr | clon del Mac `tools/adrfsr/` (sin commit) |
| Filtro de Adrenaline | `tools/adrfsr/filtro_adrenaline/` |
| Binarios adrfsr | `vitabridge/adrfsr/adrfsr_3b.suprx` (1, 2 y 3 no cargan); en la Vita `ux0:data/adrfsr/` |
| config.txt | `vitabridge/adrfsr/config_adrfsr.txt` (actual) y `config_antes_adrfsr.txt` |
| Simulación FSR | `vitabridge/coleccion/fsr_sim.py`, `coleccion/fsr/` |
| Análisis | `claude/analisis_escalado_frame_vita.md` |
