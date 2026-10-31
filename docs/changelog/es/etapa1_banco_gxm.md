# Etapa 1 de la arquitectura B: banco de pruebas GXM en la Vita (2026-10-28) — CERRADA

Segunda etapa de `arquitectura_dibujo_en_vita.md` (§6). Pregunta: **cuánto le cuesta a la Vita (ARM + GPU) un draw en GXM
dentro del proceso de Adrenaline**, según qué cambia entre draws. Criterio del plan: (b) cambio de textura ≲ 1 µs y (c)
cambio de programa ≤ 2 µs por draw, y ≥ 16 MB para superficies → B sirve también para GT.

Marcas: **[medido]** en la Vita; **[estimado]** calculado de mediciones; **[advertencia]** conclusión sacada de una prueba
sintética que **no se toma como verdadera ni como error hasta probarla con el juego real** (decisión del usuario).

- Sin cambios en GePatch (sigue 0.2-dev57.1 en la Vita, sin commit nuevo de GePatch). Cambio solo en **gepresent**
  (v11.1 → v11.2 → v11.3). Commit: gepresent v11.3 + `tools/carga_vita.py` (etapa 0) + documentos en `docs/`.
- **Estado final en la Vita:** gepresent **v9** (md5 `20f3c01d4344df5dba3bc9dc2b6b3172`), sin adrfsr en `config.txt`.
  El archivo `ux0:data/gepresent/banco` sigue en la Vita (v9 lo ignora; borrarlo con VitaShell; el relevo solo sube).
- Pendiente postergado por el usuario: la pantalla negra de la presentación (ver abajo).

## 1. Antes de empezar: adrfsr (FSR) apagado
- Decisión anterior del usuario: no seguir con FSR 1. adrfsr seguía listado en `*PSPEMUCFW`.
- `ur0:tai/config.txt`: se quitó `ux0:data/adrfsr/adrfsr.suprx` (md5 nuevo `0fe51b1eff5a0b98ce2071b0e93bce31`). Respaldo
  del anterior: `vitabridge/respaldo/config_con_adrfsr.txt` y `vitabridge/gepresent/config_con_adrfsr.txt`. Además
  `ux0:data/adrfsr/off`. Vita reiniciada con `cmd_reinicio.txt` (`reboot`).
- `*PSPEMUCFW` queda solo con `ux0:data/gepresent/gepresent.suprx`.

## 2. gepresent v11 = v9 + `banco.c`
- Fuente: `tools/gepresent/banco.c`, `banco_v.cg`, `banco_f1.cg`, `banco_f2.cg`, `banco_shaders.h` (sombreadores ya
  compilados), `build.sh` (resuelve los stubs solo desde los símbolos que faltan; `sh build.sh shaders` regenera el
  `.h`), y en `gepresent.c`: llamada a `bancoStart`/`bancoStop` y, si existe el archivo del banco, no reserva el anillo
  de volcados de 24 MB (los volcados se escriben directo).
- **Apagado por defecto:** solo corre si existe `ux0:data/gepresent/banco` (contenido = segundos de espera, en frames de
  Adrenaline / 60, antes de empezar). Log: `ux0:pspemu/ge_patch_banco.log` (se agrega al final).
- Engancha las importaciones de `AdrenalineUser` `sceGxmEndScene` (0xFE300E2F) y `sceGxmSetFragmentTexture`
  (0x29C34DF5). Después de la escena de Adrenaline, en su hilo y su contexto, corre un caso por frame: 2000 draws de
  cuadros 16x16 (tiras de 4 vértices, 2 streams de vértices, 3 repeticiones), en una superficie propia 960x544 8888 +
  D24S8 con un destino de 8 escenas por frame. Tiempos con `sceKernelGetProcessTimeLow` (µs).
- Fases: relojes y memoria → contexto inmediato (casos A–M) → contexto diferido grabado en otro hilo (A, B, P, C, U, T,
  S y T en 2 hilos) → GPU leyendo vértices (LPDDR propia, CDRAM propia, RAM del PSP) → 10 s de presentación (Adrenaline
  muestra la superficie del banco: la imagen del juego redibujada + un cuadro de color que se mueve arriba).
- Binarios en `vitabridge/gepresent/`: v9 (`20f3c01d…`, **la que queda**), v11.1 (`1bae76a4…`), v11.2 (`c1fde749…`,
  hizo caer la GPU), v11.3 (`ed98588b42b9ed59d07c8a944057bfe5`, la del commit). Fuentes también en
  `vitabridge/respaldo/gepresent_v11_*.tgz`.
- Logs: `vitabridge/etapa1_v11.1/`, `etapa1_v11.2/`, `etapa1_v11.3/`, `etapa1_v11.3_apagado/`.

## 3. Resultados (GT abierto con L = sin GePatch, filtro Bilinear; v11.1 11:49, v11.2 12:00, v11.3 12:08)

### 3.1 Relojes y memoria [medido]
- ARM **444 MHz**, bus 222, **GPU 222**, xbar 111.
- Memoria que el proceso de Adrenaline puede pedir (bloque más grande / total en bloques de 1 MB):
  - con el anillo de volcados de gepresent reservado (24 MB): CDRAM 7, LPDDR 6, LPDDR física contigua 23;
  - **sin el anillo: CDRAM 4/4, LPDDR 33/33, LPDDR física contigua 23/23** (la física comparte con LPDDR [deducido]).
  - Con todo lo del banco (2 superficies + z ≈ 6 MB, 16 MB de pozos de contextos diferidos, 6,4 MB de vértices) quedan
    LPDDR 7–8 MB y CDRAM 1 MB. Una de las superficies y el z cayeron en LPDDR física (la CDRAM no alcanzó).

### 3.2 Costo por draw [medido; 2000 draws de 16x16; rango de 3 corridas en 3 versiones]

| Caso | Inmediato: CPU µs/draw (hilo de Adrenaline) | GPU después de enviar (ms) | Diferido: grabar µs/draw (otro hilo) | GPU (ms) |
|---|---|---|---|---|
| A sin cambios | **1,0–1,2** | 3,2–3,9 | 1,0–1,1 | 2,5 |
| B cambia textura 8888 | **1,9–2,2** | 4,4–4,6 | 1,8–2,3 | 3,3 |
| P cambia textura P4 + paleta | 1,9–2,3 | 4,2–4,4 | 2,0–2,4 | 3,3 |
| C cambia programa de fragmentos | **2,1–2,4** | 4,3–4,5 | 2,5–3,2 | 3,3 |
| U cambia 64 B de uniformes | 1,9–2,2 | 4,2 | 2,0–2,6 | 3,4 |
| T textura + uniformes | **3,0–3,5** | 4,0–4,3 | 3,3 | 4,1 |
| D = A en 8 escenas | 1,5–1,7 | 6,1–6,9 (≈ +0,4 ms por escena extra) | — | — |
| O = A fuera de pantalla (sin píxeles) | 1,1 | **2,5–3,1** | — | — |
| S = A con 1 stream por draw | 1,0–1,1 | 3,2–3,8 | 0,85–0,9 | 2,5 |
| M = los 2000 cuadros en 1 solo draw | **0,02** (40 µs en total) | **1,9–2,1** | — | — |
| T grabado en 2 hilos (1000 + 1000) | — | — | 5,2 / 3,3 / 2,7 de reloj (cada hilo 4,2–10 ms) | 4,1 |

- "GPU después de enviar" = desde que la CPU terminó de enviar hasta que la GPU terminó (`sceGxmFinish`). Con draws de
  4 vértices GXM manda casi todo al cerrar la escena, así que es casi el tiempo total de GPU de la escena.
- EndScene: ~100–250 µs. **Ejecutar una lista grabada dentro de la escena de Adrenaline: ~30 µs** por 2000 draws.
- Memoria de un contexto diferido: ~160–320 KB por 2000 draws (T ~640 KB).
- **Vértices:** 100 draws × 4096 vértices (6,4 MB con paso 16): **17 M vértices/s tanto desde LPDDR propia como desde la
  RAM del PSP** (0x08800000 registrada con `sceGxmMapMemory` de solo lectura, sin error). Leer directo de la RAM del PSP
  no cuesta más. CDRAM no se probó (no quedó memoria).

### 3.3 Presentación [medido]
- Funciona: 600 frames, la textura de 512x272 de Adrenaline se cambió 599 veces por la superficie del banco; 158–195 µs
  de CPU por frame para la escena propia. El juego siguió andando.
- **Pantalla negra (postergado por el usuario):** durante los 10 s el usuario vio negro con el cuadro de color encima
  (v11.1: "negro hasta que apreté X"; v11.3: GT parecía estar en su cinemática). A los 5 s, el píxel del juego (240,136)
  y (20,20) en la textura de Adrenaline era `ff020202` (casi negro) y la superficie del banco tenía lo mismo: la copia del
  banco es fiel; lo que llegaba en ese momento era negro.
- **Verificado después (12:32, usuario, gepresent v9, GT con L):** la cinemática de GT **se ve bien** sin el banco. O sea,
  la cinemática sí pasa por la imagen que Adrenaline dibuja. Queda sin explicar por qué en esos momentos la textura
  estaba negra: puede ser que GT estuviera en una transición negra justo ahí, o algo del banco (por ejemplo, el momento en
  que se lee la textura respecto de la copia que hace Adrenaline) [verificar más adelante: leer varios píxeles cada
  segundo durante la presentación y compararlos con los de antes del banco].

### 3.4 Dos fallas del banco (y sus lecciones)
1. **v11.2 hizo caer la GPU** ("GPU Driver detects GPU Crash", coredump) en el segundo caso diferido. Causa: antes de
   cada grabación el pozo de memoria del contexto diferido se vaciaba (`pos = 0`), pero el contexto sigue usando los
   pedazos que ya recibió; el pozo volvía a entregar esa misma memoria para otro anillo y la GPU leyó comandos pisados.
   v11.3: el pozo nunca se reusa. **Lección para B:** reciclar la memoria de un contexto diferido por bloques completos y
   solo cuando la GPU terminó con todas las listas que la usan.
2. **v11.3 apagó la Vita entera** (12:16–12:18) cuando GT se abrió **sin** L (GePatch dev57.1 activo a 2x, Adrenaline en
   modo `DRAW_NATIVE`). El banco se preparó bien (mismas cifras de memoria) y la conexión se cortó ~60 s después de abrir
   Adrenaline, justo cuando el banco empieza a dibujar. Al prenderla, la Vita pidió fecha y hora (lo que hace después de
   un apagado de golpe); las aplicaciones y datos quedaron bien y tenía batería. Causa no confirmada: la combinación
   GePatch activo (GE a 2x, gepresent registrando 0x0A000000/0x0AE00000 para la GPU) + banco con ~8 MB de LPDDR libres +
   GPU al máximo. Después del reinicio Adrenaline volvió a abrirse con v11.3: se cerraron las aplicaciones con
   `cmd_cerrar.txt` (`destroy`) y se volvió a v9. **Lección:** un experimento con la GPU de la Vita no debe correr con
   GePatch activo (comprobar que se ve la textura de 512x272 antes de empezar) ni quedar instalado entre pruebas.

## 4. Lectura

### 4.1 Lo que queda probado [medido]
- **Contexto diferido:** grabar cuesta lo mismo por draw que el inmediato, pero en otro hilo; el hilo de Adrenaline solo
  ejecuta la lista (~30 µs). Es la forma de B: el traductor graba en su hilo y el presentador solo ejecuta.
- **La GPU lee la RAM del PSP directo**, a la misma velocidad que su propia memoria.
- **El presentador funciona**: se puede reemplazar la imagen de Adrenaline por una superficie propia cada frame.
- **Juntar draws es la palanca más grande**: 1 draw con 2000 cuadros = 40 µs de CPU y ~2 ms de GPU, contra 2–7 ms de
  CPU y 3,5–4,5 ms de GPU en 2000 draws separados.
- **Memoria:** ~33 MB de LPDDR + 4 MB de CDRAM en el proceso de Adrenaline (sin el anillo de volcados). Una superficie
  HD 960x544 8888 + z ≈ 4 MB. El anillo de volcados de 24 MB de gepresent no puede convivir con B tal como está.
- Un segundo SetVertexStream por draw casi no cuesta (A ≈ S).

### 4.2 Estimaciones para los juegos [estimado con los draws sintéticos]
- Con ~2 µs por draw con textura nueva y ~1,2–1,5 µs de GPU por draw (caso O):
  - **RR** (~1100 draws, 170–270 lotes por frame): ~2–3 ms de CPU, ~2 ms de GPU → holgado.
  - **SH** (~320 draws, hasta 1400): < 1–3 ms → holgado.
- **[advertencia] GT** (~9000 draws por frame, ~5300 con textura nueva, matrices): por esta cuenta serían ~17–30 ms de CPU
  y ~11–14 ms de GPU solo en draws, es decir, **podría no alcanzar 60 fps sin juntar draws**. Es una extrapolación de
  cuadros sintéticos de 16x16 con un sombreador simple; **no se toma como verdadera ni como error** hasta medir con las
  listas reales de GT (etapa 2 en adelante). Lo que podría cambiarla: juntar draws consecutivos con el mismo estado,
  grabar en 2–3 hilos fijados a núcleos distintos, el costo real de los sombreadores del GE, cuántos draws de GT
  comparten textura de verdad.
- 2 hilos grabando a la vez no escalaron en el banco (mejor caso 2,7 contra 3,3 µs/draw; cada mitad tardó más que sola).
  Los hilos se crearon sin afinidad: probablemente compartieron núcleo [verificar en la etapa 2 fijando núcleos].

### 4.3 Criterio de §6
- (b) cambio de textura ≈ 2 µs (criterio ≲ 1 µs: **no se cumple en un hilo**); (c) programa ≈ 2,2 µs (criterio ≤ 2 µs:
  **en el límite**); memoria ≥ 16 MB para superficies: **sí, sin el anillo de volcados**.
- → Seguir con B. RR, SH y el catálogo liviano entran con margen [estimado]; para GT queda la **advertencia** de §4.2,
  a confirmar con datos reales.

## 5. Cadena de compilación (contenedor de Claude, ~5 min)
`apt-get install gcc-arm-none-eabi cmake libzip-dev libyaml-dev qemu-user-static gcc-arm-linux-gnueabihf
libc6-dev-armhf-cross`; libelf 0.8.13 (`http://archive.ubuntu.com/ubuntu/pool/main/libe/libelf/libelf_0.8.13.orig.tar.gz`,
se descomprime en `libelf-0.8.13.orig/`, `--prefix=<vita-toolchain>/deps --disable-shared`, `ln -s . deps/include/libelf/libelf`);
vita-toolchain (`git clone --recursive`, `PKG_CONFIG_PATH=deps/lib/pkgconfig cmake .. -DUSE_BUNDLED_ENDIAN_H=ON`);
stubs `vita-libs-gen-2 -yml=vita-headers/db/360 -output=stubs`; taiHEN del paquete oficial
(`https://github.com/vitasdk/packages/releases/download/master/taihen.tar.xz`); `JagerDesu/vita-shaders` con
`memmem(start, 0x300000,` en `resolve.c` y `make CC=arm-linux-gnueabihf-gcc`. En `tools/gepresent`:
`VH=<vita-headers> STUBS=<stubs> TAI=<taipkg> sh build.sh` (y `SHACC=<vita-shaders> sh build.sh shaders` para los
sombreadores). Verificar `.vitalink.fstubs.taihen` = `281a3d20 5d6e6353`.
Archivos contenedor → PC: `device_commit_files` a `Downloads` con un nombre nuevo por versión; en el repo de Windows
`tar` no puede sobrescribir (no hay permiso de borrar): extraer en una carpeta temporal y `cp`.
