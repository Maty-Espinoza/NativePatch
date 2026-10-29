# Resumen para seguir en otra conversación: etapa 2 de la arquitectura B (2026-10-28)

Leer en este orden: este resumen → `etapa2_avance.md` (todo el detalle técnico y los números) → `etapa2_inicio.md`
(plan de la etapa) → `arquitectura_dibujo_en_vita.md` (§3 diseño, §5 presupuesto, §6 etapas).

## Dónde quedamos
- **Commit `81d835b` en la rama `dev`** (repo de Windows `Documents\GitHub\NativePatch`, **sin push**): GePatch
  **0.2-dev61** + gepresent **v12.9** + `docs/etapa2_avance.md`.
- En la Vita: GePatch dev61 (md5 `48e6167d…`), gepresent v12.9 (md5 `ee1373e1…`), ge_patch **on** en `plugins.txt`.
- **El modo sombra está activado por defecto en todos los juegos** (pedido del usuario): el juego corre como sin
  plugin y la Vita sigue sus listas. Para volver al GePatch antiguo en un juego: L+R+START durante ese juego (vale
  desde la próxima vez que se abra).
- Resultado de la última prueba (v12.9, GT una vuelta entera): audio bien, sin errores gráficos, casi toda la pista
  fluida; **el puente del final baja a ~36–40 fps** porque la captura tarda ~30 ms por frame ahí (~67 000 comandos,
  ~13 800 draws). SH va a su velocidad original.

## En simple (qué hace cada pieza)
- **GePatch (PSP):** ya no amplía la imagen; anota en una libreta lo que el juego le pide a su pintor (GE) y, cuando el
  juego espera al pintor, espera hasta 5 ms a que la Vita haya terminado de leer.
- **gepresent (Vita):** un hilo principal (núcleo 1) lee y recorre esas instrucciones; dos ayudantes (núcleos 0 y 2,
  prioridad mínima) leen por adelantado y le dejan los datos listos en una memoria rápida. Todavía no dibuja nada: mide
  cuánto trabajo habría y escribe `ux0:pspemu/ge_patch_captura.log` cada segundo.

## Lecciones de esta conversación (no repetir)
- La RAM del PSP está en 3 bloques de la Vita (0x70000000, 0x72000000, 0x73000000).
- SH pide la VRAM antes de montar el disco: ID desde `sceKernelGetGameInfo` (+0x44); el archivo de opciones tampoco se
  puede abrir en ese momento.
- Leer la RAM del PSP desde el ARM: DMA en ventanas chicas es carísimo (~200 µs por copia); palabra a palabra ~1,5 µs
  por comando; con NEON de a 64/256 bytes ~350 ns. El costo es la espera de cada lectura.
- **Hilos del emulador:** núcleo 1 = audio (0x40–0x43), núcleo 2 = Media Engine (0x60, decodifica el audio del juego),
  núcleo 0 = ScePspemuMain (0x61). Un hilo que sondea sin pausa por encima del Media Engine **corta el audio**; con
  prioridad baja recibe poco núcleo. Capturador elegido: núcleo 1, prioridad 0x61. Ayudantes: 0xB0 (debajo de
  AdrenalineDraw 0xA0, que no tiene afinidad fija).
- En GT lo que impide juntar draws es casi solo **0xC8 TEXLEVEL** (~8 200 cambios por frame).
- GT tiene casi todas sus texturas, paletas y ~100 sublistas por frame en la VRAM: el espejo es obligatorio.

## Próximos pasos propuestos (el usuario aprobó seguir en este orden)
1. **Medir cuánto núcleo recibe de verdad el capturador** (`sceKernelGetThreadInfo` → runClocks por segundo): ~440 ns
   por comando con 93 % de las líneas ya en la caché compartida es mucho; separar "núcleo compartido" de "trabajo".
2. **Memoria de sublistas:** GT repite casi las mismas sublistas cada frame; huella rápida y reutilizar lo ya recorrido.
   Es lo que más debería rendir en el puente.
3. Contar lotes de draws sin cortar por TEXLEVEL.
4. Después: espejo de la VRAM (GT, RR), caja negra (T2), tiempos del GE (T6), grabación GXM en varios hilos.

## Reglas de trabajo vigentes (del usuario)
- Versiones de prueba `.x` (siguiente: GePatch dev62.x si cambia el PSP; gepresent v13.x o v12.10 según el cambio);
  commit solo con la versión confirmada; compilar y copiar a Descargas, al laboratorio y a la Vita (por `vitabridge`,
  verificar con md5 bajándolo de nuevo) después de cada cambio; cerrar Adrenaline con `cmd_cerrar.txt` (`destroy`)
  para que cargue un gepresent nuevo.
- Al pedir pruebas: qué juego, qué hacer y en qué fijarse; nombrar opciones como en el menú.
- Guardar los logs de cada corrida en `vitabridge/<etapa>_<versión>/` (se reemplazan al volver a abrir el juego).
- Resumen simple después de cada cambio, para alguien sin conocimientos técnicos.
- Herramientas: compilar en el contenedor del asistente (pspdev: tarball de GitHub + clon de Adrenaline; Vita: receta
  de `etapa1_banco_gxm.md` §5); el repo se edita en el PC (device_bash) y se pasa al contenedor con stage/commit; git
  en el PC necesita permiso de borrar en `Documents\GitHub`.
