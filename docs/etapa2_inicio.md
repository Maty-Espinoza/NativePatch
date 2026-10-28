# Para empezar la etapa 2 de la arquitectura B (resumen al cerrar la etapa 1, 2026-10-28)

> **Estado al cierre de la primera conversación de la etapa 2: ver `etapa2_avance.md`** (modo sombra + capturador,
> commit dev61 + gepresent v12.9).

Leer primero: `arquitectura_dibujo_en_vita.md` (el plan; §3 diseño, §5 presupuesto, §6 etapas), `etapa0_resultados.md`
(cuánto trabajo traen los juegos) y `etapa1_banco_gxm.md` (cuánto cuesta en la Vita). Este documento junta lo necesario
para arrancar.

## En simple
- La idea nueva ("B"): el PSP dibuja su imagen chica normal, sin GePatch metido en medio, y la Vita lee las mismas
  instrucciones de dibujo y las vuelve a dibujar en alta resolución con su propia tarjeta gráfica.
- Etapa 0 (en el PC): se contó cuánto dibuja cada juego por frame. GT es el más pesado (~9000 dibujos), RR ~1100,
  SH ~320.
- Etapa 1 (en la Vita): se midió cuánto le cuesta a la Vita cada dibujo (~1 µs sin cambios, ~2 µs si cambia la imagen
  o el programa), que puede prepararlos en otro hilo, que puede leer directo la memoria del PSP y que puede mostrar su
  propia imagen. Alcanza holgado para RR y SH. Para GT hay una **advertencia** sin confirmar.
- Etapa 2: hacer que la Vita **lea las instrucciones reales del juego mientras corre** (sin dibujarlas todavía) y medir
  si le da el tiempo, con el juego a su velocidad original.

## Estado al cerrar la etapa 1
| Qué | Estado |
|---|---|
| GePatch en la Vita | 0.2-dev57.1 (dev57 + línea de prueba de Windows en el log; sin commit, parche `vitabridge/gepatch_dev57.1.patch`) |
| gepresent en la Vita | **v9** (md5 `20f3c01d4344df5dba3bc9dc2b6b3172`) |
| `ur0:tai/config.txt` | `*PSPEMUCFW` solo con gepresent (adrfsr quitado; respaldo `vitabridge/respaldo/config_con_adrfsr.txt`) |
| adrfsr | fuera de `config.txt` + `ux0:data/adrfsr/off` |
| `ux0:data/gepresent/banco` | sigue en la Vita; v9 lo ignora. Borrarlo con VitaShell (el relevo solo sube archivos) |
| Repo (rama `dev`, Windows) | commit de gepresent v11.3 (banco), `tools/carga_vita.py` y `docs/` de la arquitectura. `log.c` sigue modificado sin commit (línea de prueba de Windows) |
| Cadena de compilación de la Vita | se rearma en el contenedor del asistente en ~5 min (`etapa1_banco_gxm.md` §5) |

## Datos que la etapa 2 usa
- **Costo en la Vita** (etapa 1, [medido]): draw sin cambios ~1,0 µs; textura nueva (8888 o P4 + paleta) ~2 µs; programa
  nuevo ~2,2 µs; textura + uniformes ~3–3,5 µs; GPU ~1,2–1,5 µs por draw aunque no pinte; 2000 cuadros en 1 draw =
  40 µs de CPU y ~2 ms de GPU. Contexto diferido: grabar cuesta lo mismo, en otro hilo; ejecutar ~30 µs.
- **Memoria** del proceso de Adrenaline: ~33 MB LPDDR + 4 MB CDRAM, **sin** el anillo de volcados de 24 MB de gepresent
  (con él: 6 + 7 MB). La captura (caja negra, copias) tiene que caber ahí junto con lo que vendrá.
- **Relojes:** ARM 444 MHz, GPU 222 MHz.
- **La GPU lee la RAM del PSP** (0x08800000 registrada con `sceGxmMapMemory`) a la misma velocidad que su memoria.
- **Trabajo por frame de los juegos** (etapa 0): GT ~44 000 comandos (hasta 76 000), ~9000 draws, ~20 % de sus
  comandos y ~35 % de sus vértices en la VRAM (espejo obligatorio, 370–700 KB); RR ~9000 comandos, ~1100 draws,
  texturas en la VRAM (70–460 KB); SH ~9000 comandos, ~320 draws, sin espejo.
- **Recorrido en el ARM** (KBENCH, sintético): 203 ns por comando con DMA previo → GT 9–15 ms por frame sin caché de
  sublistas; RR y SH ~2 ms.

## [advertencia] GT
Por la cuenta hecha con cuadros sintéticos, GT (~9000 draws, ~5300 con textura nueva) podría necesitar 17–30 ms de CPU
y 11–14 ms de GPU por frame solo en draws, es decir, **podría no alcanzar 60 fps sin juntar draws**. **No se toma como
verdadera ni como error** hasta medirla con las listas reales de GT. La etapa 2 tiene que dar los datos para decidir:
cuántos draws reales quedan después de juntar los consecutivos con el mismo estado, y si grabar en 2–3 hilos con núcleo
fijo escala.

## Qué hacer en la etapa 2 (de `arquitectura_dibujo_en_vita.md` §6)
1. **GePatch "modo sombra"** (lado PSP): pasivo (no toca listas, sin VRAM falsa ni escala) + los ganchos de encolado,
   stall, sync y flip anotan en un buzón para la Vita. Versión nueva de GePatch (dev61.x) con opción en el menú.
2. **Capturador** (gepresent, lado Vita, hilo propio): sigue la lista hasta el stall leyendo la RAM del PSP (0x70000000
   sin caché), recorre sublistas, anota por draw estado + rango de vértices + textura/CLUT. No dibuja.
3. **Espejo de VRAM** para GT y RR: copia con el GE a la RAM (al cargar + verificación; sublistas de GT por frame).
   Sin espejo el capturador no puede seguir las listas de GT.
4. **Telemetría**: T1 (estadísticas por segundo, incluidos **lotes que se podrían juntar**), T2 (caja negra: últimos
   frames capturados), T4 (traza de llamadas del juego), T6 (tiempos del GE por lista).
5. **Repetir la grabación en 2–3 hilos con afinidad fija** a núcleos distintos (en el banco no escaló sin afinidad).

**Preguntas que tiene que contestar:** ¿la captura llega al final antes del sync en GT y SH? ¿el juego va a sus fps
originales con el modo sombra (vpjuego)? ¿el espejo de GT se mantiene al día? ¿cuántos draws reales de GT quedan al
juntar?

## Reglas aprendidas en la etapa 1 (aplicar en la etapa 2)
- **No correr experimentos de GPU de la Vita con GePatch activo** (el banco v11.3 con GePatch a 2x apagó la Vita entera;
  causa sin confirmar). Probar lo nuevo con GePatch en pasivo o con el modo sombra.
- **No dejar experimentos instalados entre pruebas**: el banco corría solo cada vez que se abría Adrenaline. Activar
  por archivo y retirarlo al terminar; antes de empezar, comprobar que se ve la textura de 512x272 (sin GePatch).
- **Memoria de contextos diferidos:** no reciclarla mientras el contexto la tenga (v11.2 hizo caer la GPU).
- Al cambiar `config.txt`, reiniciar con `cmd_reinicio.txt` (`reboot`); para cerrar Adrenaline a distancia,
  `cmd_cerrar.txt` (`destroy`). Si la Vita se apaga de golpe, al prenderla pide fecha y hora (no se pierden datos).
- Los plugins de la Vita se actualizan cerrando Adrenaline desde la LiveArea y abriéndolo.
- Logs: guardar cada corrida en `vitabridge/<etapa>_<versión>/` (el log se reemplaza o se agrega).
- Al pedir pruebas: nombrar las opciones como en el menú de Adrenaline / del plugin y decir qué juego probar con cada una.

## Pendiente heredado (no bloquea la etapa 2)
- Pantalla negra durante la presentación del banco (la textura del juego llegó negra justo en esos 10 s; sin el banco
  la cinemática de GT se ve bien). Postergado por el usuario.
- Licencia de GePatch/NativePatch antes de copiar código de PPSSPP.
- `ambientes_de_prueba.md` sigue describiendo dev56/gepresent v6–v9 en varias tablas (estado actual: este documento).
