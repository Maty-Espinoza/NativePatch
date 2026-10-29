# TODO: pendientes que no son urgentes

Cosas que no bloquean el trabajo actual pero pueden servir más adelante. Cada una dice de dónde salió (documento en
`docs/` o en el proyecto). Lo que sí falta para cerrar la etapa 2 está en `docs/arquitectura_dibujo_en_vita.md` §6, no
aquí. Actualizado: 2026-10-28 (GePatch 0.2-dev63 + gepresent v13.5).

## Rendimiento de la captura (gepresent, lado Vita)
- [ ] **Comprobar al empezar cada cuadro las sublistas estables.** Hoy un ayudante las comprueba cuando la lista llega
      al CALL, y solo ~40 % llega a tiempo. Comprobarlas al empezar la lista daría más tiempo, con un riesgo: si el juego
      cambia la sublista en ese cuadro, se usa la vieja (la revisión de 1 de cada 32 lo vería). Solo para sublistas
      estables hace muchos cuadros. Decidido dejarlo para después. (`etapa2_comparar_menos.md`)
- [ ] **Leer menos de la RAM del PSP.** Lo caro es la lectura sin caché (~6,5 µs cada 256 bytes). Ideas: una vista con
      caché (necesitaría un plugin de kernel y limpiar la caché a mano), o huellas calculadas en otro lado. Rinde más que
      comparar más rápido. (`etapa2_comparar_menos.md`)
- [ ] **Acortar el bucle de interpretación** (~360 ns por comando): revisar el tramo de RAM una vez por línea y no por
      comando, y usar menos contadores. Bajaría lo que la memoria de sublistas no se salta. (`etapa2_memoria_sublistas.md`)
- [ ] Comparar directo desde la caché compartida, sin copiar la línea a `R0.buf` (ganancia chica).
      (`etapa2_espejo_vram.md`)
- [ ] `memoVerify` compara todas las versiones (hasta 8) de una sublista aunque tengan las mismas líneas: comparar
      una vez por tramo. Hoy en GT casi siempre hay una sola versión, así que no se nota. (`etapa2_comparar_menos.md`)

## Espejo de la VRAM
- [ ] **SH: dejar de pedir 1 MB que cambia en cada copia.** Son destinos que SH escribe con TRANSFER y no se detectan
      como destino. Cuesta ~0,07 ms de GE por cuadro: no afecta hoy. Marcar como destino lo que escribe TRANSFER en la
      VRAM. (`etapa2_espejo_vram.md`)
- [ ] Buscar la dirección física de la VRAM desde el kernel de la Vita, para leerla sin espejo. Riesgoso y de baja
      prioridad. (`arquitectura_dibujo_en_vita.md` §7)

## Telemetría (T4/T6, dev63)
- [ ] **T4: no se encontraron las funciones de DMA** (`sceDmacMemcpy`/`sceDmacTryMemcpy`, biblioteca `sceDmac`) con
      ninguno de los nombres de módulo probados: falta el nombre correcto en Adrenaline 8. GT y SH igual se pudieron
      medir sin eso. (`etapa2_t4_t6_vida_datos.md`)
- [ ] T6: "GE ocupado al llamar" al sync sale siempre 0, aunque el juego espera ~0,7 ms (GT) o ~3,7 ms (SH) dentro de
      `sceGeDrawSync`. Entender qué espera ahí (¿el fin de la lista con el GE ya detenido, o los callbacks?).
- [ ] T6 apagado por defecto (le quitaba ~2–3 fps a GT): encender con `ux0:data/gepresent/medir_ge`. Si se quiere
      dejar siempre, muestrear cada ~1 ms o desde otro lado.

## Pruebas y herramientas
- [ ] Las pruebas del PSP (`tests/`) no compilan desde dev61: `asm("sync")` en `sbPost` con el compilador del PC.
      (`etapa2_espejo_vram.md`)
- [ ] En el contenedor, `gcc-multilib` (pruebas del PSP) desinstala `gcc-arm-linux-gnueabihf` (prueba de qemu de
      gepresent): no instalarlos a la vez, o buscar cómo tener los dos. (`resumen_conversacion_2026-10-28_etapa2c.md`)
- [ ] Probar Ridge Racer con el modo sombra y el espejo. Usa texturas en la VRAM, y en la etapa 2 solo se probaron GT y SH.

## Mediciones poco confiables
- [ ] El núcleo 0 de la línea `procesador` del log sale casi vacío: el gancho de CreateThread no ve los hilos que
      Adrenaline creó antes de cargar gepresent. (`etapa2_memoria_sublistas.md`)
- [ ] Estadísticas aproximadas de la memoria de sublistas: "cambios" y lotes del primer draw de una sublista aplicada
      dependen del estado de entrada; `chg[]` no suma las sublistas aplicadas. (`etapa2_memoria_sublistas.md`)
- [ ] vpjuego: "GE en stall" siempre da 0 %. Para los tiempos del GE usar las horas de encolado y FINISH (T6).
      (`arquitectura_dibujo_en_vita.md` §7)

## Pendientes heredados
- [ ] Pantalla negra durante la presentación del banco de la etapa 1: la textura del juego llegó negra en esos 10 s.
      Sin el banco, la cinemática de GT se ve bien. Postergado por el usuario. (`etapa1_banco_gxm.md` §3.3)
- [ ] **Licencia de GePatch/NativePatch** antes de copiar código de PPSSPP (GPL 2.0+; Adrenaline es GPLv3).
      `LICENSE.TXT` solo trae el aviso del PSPSDK. (`arquitectura_dibujo_en_vita.md` §3.10)
- [ ] La causa de que la Vita se apagara con el banco v11.3 y GePatch activo a 2x sigue sin confirmar. Mientras tanto,
      lo nuevo de la GPU se prueba con el modo sombra o el pasivo. (`etapa1_banco_gxm.md`)
- [ ] A mediano plazo: proponer a Adrenaline un punto de enganche oficial para un "presentador externo", para no
      depender de enganchar su hilo de dibujo. (`arquitectura_dibujo_en_vita.md` §8)

## Limpieza
- [ ] Borrar en la Vita `ux0:data/gepresent/banco` (con VitaShell: el relevo solo sube archivos) y los ~100 volcados
      `.gpd` viejos de `ux0:/pspemu` (el relevo los vuelve a bajar en su primera corrida).
- [ ] Borrar a mano las carpetas vacías `C:\Users\spect\GePatch` y `Documents\ambientepsp` del PC con Windows.
      (`ambiente_windows.md`)

## Documentación
- [ ] `ambientes_de_prueba.md` sigue describiendo dev56 y gepresent v6–v9 en varias tablas.
- [ ] `errores_conocidos.md` describe GePatch actual (arquitectura A, dev56.8). Cuando la Vita dibuje (etapa 3 en
      adelante), marcar cuáles desaparecen con B (ver la tabla de `arquitectura_dibujo_en_vita.md` §3.7).
- [ ] `dev` va varios commits adelante de `origin/dev` (desde `81d835b`, etapa 2): falta el push.
