# VPROBE: batería de referencia del GE y la VRAM, y pruebas del grupo B

Grupos A y B del plan `docs/pruebas_hardware_vita.md`.
- La **batería A** (EBOOT, X) es un homebrew suelto: no usa plugins y no toca `ur0:tai/config.txt`,
  `plugins.txt` ni juegos. Si algo sale mal, lo peor es cerrar Adrenaline.
- El **grupo B** sí necesita plugins: ver su sección.

**Desde la versión 7** quedan solo las mediciones que siguen sirviendo para diseñar GePatch. Lo ya respondido o
descartado está en `vprobe6_ruta_e.c` (código de la v6) y en el documento del plan:
- la VRAM dentro de la RAM y el mapa de reflejos;
- el modo de 4 MB y los registros de la EDRAM;
- intercambiar la pantalla entera;
- el sprite único con textura lineal.

## Qué mide

| # | Medición | Para qué sirve |
|---|---|---|
| 1 | Copias de 1 MB entre la VRAM y la RAM, con el GE y con DMA | Referencia del costo de mover memoria |
| 2 | Paginación de rt al doble en el espacio del z, con los tamaños del censo A2: 64x64 8888 (RR), 256x256 565, 256x256 8888 (RR, SH) | Costo real de dibujar cada rt al doble |
| 3 | Relleno: borrar; texturas por franjas (lineal o swizzle, RAM o VRAM, bilineal, mezcla); las dos copias escaladas de GePatch | Cómo conviene dibujar |
| 4 | 3D al doble: la misma escena con textura y profundidad a 480x272 y a 960x544 | Cuánto más le cuesta al GE dibujar al doble |
| 5 | GE y CPU a la vez | Cuánto se frenan entre sí por el bus de la RAM |

## Instalar y correr

1. Copiar `EBOOT.PBP` a `ux0:/pspemu/PSP/GAME/VPROBE/EBOOT.PBP` (con el relevo:
   `enviar/ux0/pspemu/PSP/GAME/VPROBE/EBOOT.PBP`).
2. En Adrenaline, abrir **VPROBE** desde el XMB **manteniendo L** (así Adrenaline no carga GePatch,
   que cambia la VRAM). Si GePatch está cargado, VPROBE lo dice y no corre.
3. X para correr (unos 10 s). O para salir. En la parte 4 la pantalla se ve rota un instante (usa toda la
   VRAM como GePatch).
4. El resultado queda en `ux0:/pspemu/ge_patch_vprobe.log`. El relevo lo baja solo a `recibidos/`, porque
   solo mira la raíz de `ux0:/pspemu` con el patrón `ge_patch*.log`.

## Seguridad
- Solo usa su propia memoria y la VRAM de 0x04088000 a 0x04200000 (la pantalla de texto queda en
  0x04000000). La parte 4, a 960x544, usa toda la VRAM y después borra la pantalla de texto.
- No escribe registros del hardware (la escritura de la EDRAM de la v6 se quitó).
- Escribe el log línea a línea: si Adrenaline se cierra, queda lo que alcanzó a medir.

## Grupo B (versión 8)

| Prueba | Pieza | Dónde va | Qué responde |
|---|---|---|---|
| **B1** margen real del GE en juegos | `juego/vpjuego.prx` (plugin de kernel) | `ux0:pspemu/seplugins/vpjuego.prx` | fps, % del tiempo que el GE trabaja (y cuánto espera en el stall), ms de GE por frame y % de CPU libre, cada 2 s |
| **B4** dónde vive la VRAM del lado ARM | `vita/vpcdram.suprx` (plugin de la Vita) + VPROBE con **Triángulo** | `ux0:data/vprobe/vpcdram.suprx` | bloque de memoria de la Vita que guarda la VRAM del PSP, si el ARM la lee y la escribe, y cuánto tarda en leer 1 MB |

B2 (DMA entre la RAM del PSP y la de la Vita) ya lo midió KBENCH (`kbench.md`). B3 (prototipo de paginación en
GePatch) depende de B1. B5 (regresión headless) depende de B3.

### B1: cómo correrla
1. Copiar `vpjuego.prx` a `ux0:pspemu/seplugins/` y agregar en `ux0:pspemu/seplugins/plugins.txt`:
   ```
   game, ms0:/seplugins/vpjuego.prx, on
   ```
2. Para cada juego (RR, SH, GT), dos corridas de ~1 minuto en una escena pesada:
   - **sin GePatch:** la línea de `ge_patch.prx` en `off`;
   - **con GePatch:** la línea en `on`.
   **No** abrir el juego con L: en Adrenaline 8, L desactiva todos los plugins, también vpjuego.
3. El log queda en `ux0:pspemu/ge_patch_vpjuego.log`; el relevo lo baja solo. Se sobrescribe en cada juego, así
   que hay que esperar a que llegue antes de abrir el siguiente.
4. Al terminar: poner la línea de vpjuego en `off`.

vpjuego no engancha nada: solo lee registros del GE (API oficial) y la dirección de la pantalla. Agrega un
hilo de prioridad mínima que cuenta vueltas (mide la CPU libre); en teoría solo usa tiempo que nadie más quiere.

### B4: cómo correrla
1. Copiar `vpcdram.suprx` a `ux0:data/vprobe/` y agregar al final de `ur0:tai/config.txt`:
   ```
   *PSPEMUCFW
   ux0:data/vprobe/vpcdram.suprx
   ```
   y **reiniciar la Vita**.
2. Abrir Adrenaline, abrir VPROBE manteniendo L y apretar **Triángulo**. El PSP deja una firma en la VRAM y
   espera hasta 60 s la respuesta del ARM.
3. Resultados: `ux0:pspemu/ge_patch_vprobe.log` (lado PSP) y `ux0:pspemu/ge_patch_vpcdram.log` (lado ARM,
   con la lista de bloques de memoria del proceso). El relevo baja los dos.
4. Al terminar: quitar las dos líneas de `config.txt` y reiniciar.

**Riesgo:** el ARM lee todos los bloques legibles de 2 a 128 MB del proceso de Adrenaline. Si alguno tiene un
hueco sin mapear, Adrenaline se cierra. El log anota cada bloque antes de leerlo. Fuera de la marca "VPB4" no
escribe en la RAM del PSP; con la marca, solo escribe en el buzón y en VRAM+1F8000.

### Compilar el grupo B
- `juego/`: `make` (pspdev).
- `vita/`: `sh build.sh` con `VH=<vita-headers>` y `STUBS=<stubs de vita-libs-gen-2>`. Usa arm-none-eabi-gcc y
  las herramientas de vita-toolchain, porque el vitasdk completo no se pudo descargar en este entorno.

## Censo de render targets (A2)
`tools/censo.py` lee volcados `.gpd` y estima el costo de dibujar cada rt al doble:
```
python3 tools/censo.py ~/Downloads/vitabridge/recibidos/*.gpd
```

## Compilar
```
make            # pspdev en el PATH; sale EBOOT.PBP
```
