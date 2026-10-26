# KBENCH: costo de usar el ARM de la Vita desde el PSP

Mide si conviene mover trabajo de GePatch (el recorrido de listas) al ARM de la Vita. Responde con
números a estas preguntas:

| Pregunta | Prueba | Archivo |
|---|---|---|
| ¿Cuánto tarda una ida y vuelta PSP → ARM → PSP? En ráfaga, cada 2 ms y al ritmo de un frame | pings | `ping.csv` |
| ¿Kermit o memoria compartida? ¿Cuánto cuesta sondear más seguido? | pings `kermit_*` y `mem_*` (sondeo 0/100/1000 µs) | `ping.csv` |
| ¿En qué núcleo y con qué prioridad conviene el hilo del ARM? | afinidad (4 máscaras × 2 prioridades) | `afinidad.csv` |
| ¿Cuánto cuesta que el ARM lea la RAM del PSP con la caché bien mantenida? ¿Qué pasa si no se invalida? | lectura (1 KB a 1 MB) | `lectura.csv` |
| ¿Cuánto cuesta escribir de vuelta (parches, listas propias)? ¿Llegan bien? | escritura | `escritura.csv` |
| ¿Cuánto más rápido recorre el ARM el mismo código que el Allegrex? (a 333 y 222 MHz, con y sin caché) | recorrido de una lista sintética tipo GT (55 mil comandos) | `recorrido.csv` |
| ¿Cuánto cuesta vaciar la caché del PSP (lo que GePatch hace en cada llamada)? | caché | `cache.csv` |
| ¿El modo asíncrono libera de verdad la CPU del juego? ¿Cuánto se atrasa el GE? | asíncrono con el GE real y stall | `asincrono.csv` |
| ¿Dos hilos pueden usar Kermit a la vez? | concurrencia | `ping`/`concurrencia.csv` |
| ¿Cambia la latencia con un juego corriendo? | modo juego | `juego.csv` |

El resumen legible queda en `resumen.txt` (y `resumen_juego.txt`). `kbench.py` lo junta todo y estima
el costo por frame con la regla de la fase 1 (`docs/filosofia.md`).

## Piezas

| Archivo | Dónde va | Qué es |
|---|---|---|
| `kbench.suprx` | `ux0:data/kbench/kbench.suprx` | lado ARM (plugin de usuario de la Vita, en el proceso de Adrenaline) |
| `kbench.prx` | `ux0:pspemu/seplugins/kbench.prx` | lado PSP (plugin de kernel) |
| `EBOOT.PBP` | `ux0:pspemu/PSP/GAME/KBENCH/EBOOT.PBP` | homebrew que arranca la batería y muestra el avance |

## Instalación (Adrenaline 8)

1. Copiar los tres archivos (con el relevo: ponerlos bajo `enviar/ux0/...` con esas rutas).
2. En `ur0:tai/config.txt` agregar al final y **reiniciar la Vita**:
   ```
   *PSPEMUCFW
   ux0:data/kbench/kbench.suprx
   ```
3. En `ux0:pspemu/seplugins/plugins.txt` agregar:
   ```
   hbw, ms0:/seplugins/kbench.prx, on
   ```
   Para la prueba con juego (opcional): `ULJS00001, ms0:/seplugins/kbench.prx, on` y **GePatch apagado
   para ese juego** (los dos usan 0x0A000000).

## Uso

1. Abrir Adrenaline y lanzar **KBENCH** desde el XMB.
2. La pantalla dice si el ARM está listo. X corre la batería (~1–2 min); Triángulo también corre la
   prueba de vista directa (lectura sin ConvertAddress).
3. Al terminar, traer `ux0:pspemu/kbench/` (con el relevo: `bajar_kbench.txt`) y
   `ux0:data/kbench/arm.log`.
4. `python3 kbench.py kbench/ --comandos 55000 --llamadas 40` (las llamadas por frame salen del log
   de GePatch: listas + stall).
5. **Después de la prueba:** cerrar Adrenaline (o quitar las líneas de los pasos 2 y 3). Ver "Seguridad".

## Seguridad

- **Kermit:** Adrenaline 7 y 8 reasignan el canal de PS1 (ScePspemuRemotePocs) al modo 14
  (KERMIT_MODE_EXTRA_1), el mismo que usa KBENCH. Por eso el hilo de Kermit del ARM **no arranca solo**:
  lo pide el PSP por memoria compartida y el ARM lo rechaza si Adrenaline está en modo PS1. Si después
  de la prueba se abre un juego de PS1 sin cerrar Adrenaline, ese hilo podría quedarse con peticiones
  de PS1: por eso hay que cerrar Adrenaline al terminar.
- **El PSP nunca envía si el buzón del ARM no está vivo** (magia, versión, modo y contador que avanza).
  Si nadie escucha un modo Kermit, el PSP se bloquea para siempre; por eso cada lote corre en un hilo
  con vigilante (3 s) y, ante un cuelgue, se anota cuántas peticiones recibió y respondió el ARM y se
  sigue por memoria compartida.
- La memoria compartida tiene tiempo límite (2 s) y no puede colgar.
- El ARM valida todo: direcciones dentro de la RAM del PSP, tamaños de hasta 4 MB, recorrido de
  hasta un millón de comandos.
- El ARM verifica que los offsets de ScePspemu (0x6364, 0x6490, 0x64D0, 0x6560) parezcan funciones o
  ganchos de taiHEN; si no, no hace nada (`arm.log` lo dice). `ux0:data/kbench/forzar.txt` salta
  esa verificación.
- Los resultados se escriben al terminar cada prueba: si Adrenaline se cierra, lo anterior queda.

## Compilar

Necesita pspdev (trae systemctrl), vitasdk y el código de isage/Adrenaline (para el modo Kermit de
`adrenaline_vita.h`):

```
make -C psp ADRENALINE_SRC=<isage/Adrenaline>
make -C eboot
cmake -S vita -B vita/build -DADRENALINE_SRC=<isage/Adrenaline> && make -C vita/build
gcc -O1 -fsanitize=address,undefined -Icomun host/test_walk.c comun/walk.c -o host/test_walk && host/test_walk
```

Protocolo, buzón y formato del registro: `comun/kbench.h`. Recorrido y generador: `comun/walk.c`
(mismo código en el PSP, el ARM y el PC).
