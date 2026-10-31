# Etapa 2 de la arquitectura B: modo sombra y capturador (2026-10-28)

Leer antes `etapa2_inicio.md` (plan de la etapa) y `arquitectura_dibujo_en_vita.md`. Marcas: [medido], [estimado],
[deducido], [verificar]. Commit: **GePatch 0.2-dev61 + gepresent v12.9** (pruebas dev61.1–dev61.3, v12.1–v12.9).

## En simple
- **Modo sombra (GePatch, lado PSP):** el juego corre como sin plugin (VRAM real, imagen nativa de Adrenaline, no se
  recorren ni se tocan sus listas). Los ganchos de encolado, stall, sync y flip solo anotan en un buzón. **Es el modo por
  defecto desde dev61.3** (decisión del usuario: no tiene sentido probar el GePatch antiguo ahora). L+R+START durante el
  modo sombra lo apaga para la próxima vez que se abra ese juego (guarda `sombra=0` en `ms0:/gepatch_<ID>.txt`).
- **Capturador (gepresent, lado Vita):** un hilo lee el buzón y recorre las mismas listas desde la RAM del PSP, como lo
  haría el GE, sin dibujar. Cada segundo escribe estadísticas en `ux0:pspemu/ge_patch_captura.log` (la sesión anterior en
  `ge_patch_captura_prev.log`; el relevo las baja solas).
- **Compuerta en el sync:** cuando el juego espera al GE (`sceGeDrawSync(0)` / `sceGeListSync(qid, 0)`), GePatch espera
  hasta 5 ms a que la Vita haya capturado las listas que el juego espera. Si la captura no alcanza, el juego baja de fps
  (eso es lo que se ve en GT en las partes pesadas).
- **Resultado:** SH va a su velocidad original con la captura al día (la linterna se ve). GT: menús y la mayor parte de la
  pista a 55–60 fps; el puente del final baja a ~36–40 fps porque la captura tarda ~30 ms por frame. Audio bien, sin
  errores gráficos (la imagen es la nativa).

## Componentes
### GePatch dev61 (main.c, "Modo sombra")
- Buzón en 0x0A000000 (la VRAM falsa, sin uso en sombra), sin caché: `+0 "SOMB"`, `+4` versión, `+8` eventos escritos
  (PSP), `+0xC` última lista capturada (Vita, número de orden), `+0x10` latido de la Vita, `+0x14` eventos leídos (Vita),
  `+0x18` listas encoladas (PSP). Anillo de 4096 eventos de 32 bytes en `+0x1000`: `tipo | dato<<8, a, b, c, hora PSP,
  orden`. Tipos: 1 encolar (lista, stall, qid, orden), 2 encolar al frente, 3 stall (qid, stall), 4 drawsync (espera,
  capturadas, orden), 5 listsync, 6 flip (fb, ancho|formato<<16, frame).
- `modeDecide()`: primer gancho que llama el juego (pedir la VRAM, encolar o flip). Lee el ID y el perfil del juego; si
  `opt.sombra`, pasa a `sombra = passive = 1`, `DRAW_NATIVE = 0` (Adrenaline muestra la imagen nativa) y arma el buzón.
  `sceGeEdramGetAddr/Size` devuelven la VRAM real en sombra. Se reponen en cada flip la marca "SOMB" y "GPV1" (el log y los
  volcados los sigue escribiendo gepresent).
- **ID del juego:** SH pide la VRAM antes de montar el disco (`disc0:/UMD_DATA.BIN` no existe todavía): se toma de
  `sceKernelGetGameInfo` (SysMemForKernel 0xEF29061C, +0x44 "ULUS10450", lo mismo que usa Adrenaline). En ese momento
  tampoco se puede abrir `ms0:/gepatch_<ID>.txt` (por eso sombra quedó por defecto). [medido]
- Ganchos nuevos: `sceGeDrawSync` (0xB287BD61) y `sceGeListSync` (0x03444EB4) en sceGe_driver; fuera de sombra solo pasan.
- Log cada 600 frames: `stats sombra` (eventos, listas, si la Vita responde, capturadas), `stats sombra sync` (syncs que
  esperaron, cuántos llegaron a 5 ms, espera media/máx) y `stats sombra GE` (lo que el juego espera al GE en drawsync).

### gepresent v12.9 (tools/gepresent/captura.c)
- Arranca cuando la RAM del PSP está mapeada; se activa al ver "SOMB". La RAM del PSP está en **3 bloques de la Vita**:
  0x70000000 (PSP 0x08000000–0x0A000000), 0x72000000 y 0x73000000 [medido]; v12.1 miraba uno solo y no arrancaba.
- Sigue los eventos y recorre como el GE (una lista a la vez, hasta su stall; JUMP/CALL/RET, BASE, OFFSET, ORIGIN,
  END tras SIGNAL), sin dibujar. Por draw cuenta: tipo, 3D/2D/clear, cambios reales de estado (textura, matriz, destino,
  "otro"), lotes juntables, texturas distintas (RAM/VRAM), paletas, copias, vértices. Lo que está en la VRAM del PSP no se
  puede leer: CALL a la VRAM se cuenta y se salta; JUMP a la VRAM pierde la lista. Tope 400 000 comandos por lista.
- **Lectura** (casi todo el costo es esperar a la RAM del PSP desde el ARM):
  | Forma | Costo en GT | Versión |
  |---|---|---|
  | DMA en ventanas de 2 KB | ~200 µs por copia, ~24 comandos por ventana → ~10 µs/comando | v12.2 |
  | palabra a palabra (vista sin caché) | ~1,5 µs/comando | v12.3 |
  | 64 bytes con NEON (vld1) | ~350 ns/comando con el núcleo para él | v12.4 |
  | 256 bytes (8 cargas NEON antes de guardar) + 2 lectores adelantados | ~440 ns/comando en el núcleo 1 | v12.9 |
- **Lectores (v12.9):** dos hilos (núcleos 0 y 2, prioridad 0xB0: solo el tiempo que sobra, por debajo de AdrenalineDraw
  0xA0) recorren la lista que publica el capturador (sin interpretar draws) y se reparten las sublistas del nivel de
  arriba. Dejan líneas de 64 bytes en una caché compartida (8192 líneas, 512 KB, etiqueta = línea + número de orden de la
  lista; escritura con CAS y relectura de la etiqueta). Lo que está desde el stall hasta 256 KB después no se comparte
  (puede no estar escrito). En GT el capturador toma **~93 % de las líneas de los lectores** [medido].
  `ux0:data/gepresent/captura_sin_lectores` los apaga.
- **Hilos del emulador** (ScePspemu, medido con gepresent v12.6):
  - núcleo 0 (0x10000): ScePspemuMain 0x61 y servicios remotos 0x5d–0x63;
  - núcleo 1 (0x20000): audio: RemoteAudioOutOp 0x40, AudioCh0/1Buffer 0x43, AudioOutCh0/1Intr 0x41, Mixer 0x41;
  - núcleo 2 (0x40000): **ScePspemuRemoteMe 0x60 (Media Engine: decodifica el audio del juego)**, TitleInfo 0x60,
    AvcAuInput 0x60, ScePopsDisplay 0x7d;
  - Adrenaline: AdrenalineDraw 0xA0 y AdrenalineCompat 0x60 sin afinidad.
- **Dónde corre el capturador** (núcleo / prioridad): 2/0x40 → corta el audio (tapa al Media Engine); 2/0xA0 → audio
  bien pero ~1000 ns/comando; 2/0x40 con pausas → peor de todas; 2/0x61 → ~900 ns en carrera (el Media Engine usa más de
  la mitad del núcleo); **1/0x61 → ~510 ns, audio bien (elegido)**. Regla: un hilo que sondea sin pausa no puede estar
  por encima del Media Engine en el núcleo 2 ni de los hilos de audio en el 1.
- `banco.c` (etapa 1) sigue dentro, pero se activa con `ux0:data/gepresent/banco_v12` (el archivo `banco` viejo sigue en
  la Vita y v12 lo ignora).

## Números [medido]
- **SH (ULUS-10450):** 1 lista por frame; la captura al día casi siempre (pocas esperas, < 4 ms); fps = los originales.
- **GT (UCUS-98632), 1 lista + 2 drawsync + 1 listsync por frame:**
  - pista: 23 000–42 000 comandos y 5 000–10 000 draws por frame → 10–18 ms de captura (v12.9);
  - puente del final: ~67 000 comandos y ~13 800 draws por frame → ~30 ms (36–40 fps);
  - casi todo tiras de triángulos 3D; vértices en la RAM (0,5–0,9 MB por frame, ninguno en la VRAM);
  - ~95 % de los draws con textura en la VRAM; 120–230 texturas distintas por frame; ~1000 cargas de paleta por frame
    (~80 % desde la VRAM); 80–140 sublistas en la VRAM por frame (saltadas) → **el espejo de VRAM es obligatorio para GT**;
  - **juntar draws:** con todos los registros, ~6 450 lotes de 11 800 draws (~45 % menos). Lo que corta los lotes es casi
    solo **0xC8 TEXLEVEL** (~8 200 cambios por frame); el resto de "otro" es chico (0x22 alpha blend ~130, 0x5C ~70,
    0xC7 ~70, 0xC6 ~65, 0xCF ~55, 0xDB ~50, 0xE7 ~50). Si TEXLEVEL va por draw (atributo o constante) sin cortar el
    lote, juntar rendiría mucho más [deducido; medir en la próxima versión].
- El ARM con prioridad baja en un núcleo ocupado recibe solo una parte del tiempo; los ns/comando incluyen eso.

## Próximos pasos
1. Medir el tiempo de CPU que de verdad recibe el capturador (`sceKernelGetThreadInfo`: runClocks) para separar "núcleo
   compartido" de "trabajo por comando" (~440 ns por comando con 93 % de las líneas ya en la caché es mucho).
2. **Memoria de sublistas:** GT repite casi las mismas sublistas en cada frame; huella rápida + reutilizar lo ya
   recorrido (y más adelante lo ya traducido). Es lo que más debería rendir en el puente.
3. Contar lotes sin cortar por TEXLEVEL.
4. Espejo de la VRAM (GT y RR), T2 (caja negra), T4, T6, grabación GXM en varios hilos con afinidad fija.

## Archivos y estado de la Vita
- Vita: GePatch dev61 (`ux0:pspemu/seplugins/ge_patch.prx`, `plugins.txt` con ge_patch **on**), gepresent v12.9
  (`ux0:data/gepresent/gepresent.suprx`), opciones de GT y SH con `sombra=1` (y sombra por defecto en el código).
- Respaldo de las opciones anteriores: `Downloads/vitabridge/respaldo/opciones_antes_etapa2/`. Binarios de cada versión:
  `vitabridge/gepresent/gepresent_v12.*.suprx`, `vitabridge/respaldo/ge_patch_dev61*.prx`.
- Logs de cada prueba: `vitabridge/etapa2_dev61.1/`, `etapa2_dev61.2/prueba2/`, `etapa2_dev61.3/prueba3…prueba9_v12.9/`.
- Compilación: en el contenedor del asistente (pspdev + vita-toolchain, receta en `etapa1_banco_gxm.md` §5, ~5 min),
  archivos al PC con stage/commit; en el repo de Windows git necesita permiso de borrar en `Documents\GitHub`.
