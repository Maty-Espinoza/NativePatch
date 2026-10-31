# Regresiones por version: GePatch original (v0.1x-v0.2) y NativePatch (dev39-dev51)

Fuentes: historial git de TheOfficialFloW/GePatch (`git log 67a2f19`), la planilla de compatibilidad
(`docs/gepatch_compatibilidad.csv`, 816 juegos) y los volcados de RR de la Vita y del laboratorio.

## 1. La planilla: donde se rompieron juegos

271 de 816 juegos tienen una "mejor version" anterior a la ultima probada (regresion). Juegos cuya
mejor version fue cada una (es decir, que la siguiente los empeoro):

| mejor | juegos | lo que se reporta despues |
|---|---|---|
| v0.15 | 29 | FMV negros, cuelgues, 480x272 |
| v0.16 | 42 | FMV negros, efectos/fondos que faltan, lentitud |
| v0.17.1 | 35 | un cuarto de pantalla (Surf's Up), suelo que desaparece en las esquinas |
| v0.18.1 | 70 | parpadeo, FMV negros, cuelgues de Adrenaline |
| v0.19.1 | 31 | parpadeo en menus de guardado, tearing (Persona, Gurumin, Miku) |

## 2. Cambio de cada version, el riesgo que trajo y como esta hoy

| version | cambio | riesgo / regresion | hoy (dev51) |
|---|---|---|---|
| a67d616 | fb con ancho != 512: draw a 1 KB de VRAM | se pierde lo que dibuja en render targets | render targets reales (rt1, escala 1) |
| 4749115 | signo del viewport, centro fijo 2048 | juegos con centro distinto | `vpPatch` desde los valores del juego |
| a043e58 | vertices through x2 solo en (-2048, 2048) | coordenadas grandes sin escalar | igual (no visto como problema) |
| v0.15 | recorrido DFS, CALL/RET con pila; BJUMP siempre salta | draws del camino no tomado sin parchear | BJUMP recorre ambos caminos |
| v0.15 | dibujos indexados: rango `lower..upper` exclusivo | ultimo vertice sin duplicar (arreglado en v0.17.1 con `upper+1`) | `upper += 1` |
| v0.16 | `AdvanceVerts` solo en through | VADDR implicito mal tras un draw 3D: 2D posterior duplica vertices 3D | avanza siempre |
| v0.16 | NOP de draws a fb ignorados | personajes invisibles al reusar listas (DBZ) | se restaura el PRIM en cada pasada |
| v0.17 | `ignore_texture`: draws que leen un fb se anulan; texturas en 0x04000000-0x04200000 tambien | **FMV negros, efectos y fondos de texto que faltan** (la regresion mas citada) | texmodes 3/4/6/7/8/9 en vez de anular; `tex_fb` en el menu |
| v0.17 | FAKE_VRAM 0x0B000000 | cuelgues (Patapon); revertido | 0x0A000000 |
| v0.18 | marca de "ya duplicado" escrita en el relleno de los vertices | corrompe datos si el relleno se usa; falsos positivos | memoria de parches por direccion/hash |
| v0.18 | `sceGeEdramGetSize` = 4 MB | el juego cree tener 4 MB: ubica cosas hasta 0x0A400000 | igual; la VRAM falsa ocupa 4 MB |
| v0.18.1 | fb de ancho 480/960 como pantalla | "triangulos raros en Sonic" (quitado 480 en v0.19.1) | fb por ancho/formato |
| v0.19 | salto de matrices (`list += 12`) | se pierden comandos si el bloque no es exacto; quitado en v0.19.1 | se leen las matrices (recorte 3D) |
| v0.19 | SIGNAL CALL guarda BASE, CALL normal no (basura) | RET de SIGNAL restaura BASE al azar | CALL guarda BASE |
| v0.19.1 | dithering forzado | cambio de color; quitado en v0.2 | el del juego |
| v0.2 | copia a la pantalla al encolar cada lista | **parpadeo y tearing** (frames a medio dibujar) | solo si no hubo flip en 100 ms |
| v0.2 | SIGNAL SYNC: `list += 2` siempre | si no viene FINISH+END se saltan 2 comandos del juego | **dev51**: solo si siguen FINISH y END |
| v0.2 | CALL a bloques de matrices de huesos se salta | ninguno (no hay draws) | igual |

Patron comun en las regresiones del original:
1. Anular draws (v0.17) en vez de redirigirlos: cada heuristica nueva borraba algo en otro juego.
2. Memoria del plugin reutilizada mientras el GE aun la usa: lista unica de copia en v0.2, que se
   reescribia con la copia anterior pendiente (parpadeo).
3. Heuristicas por valor (480/272, ancho 512/480/960, rango +-2048) que aciertan en un juego y
   fallan en otro.

## 3. Aplicado a NativePatch: el cielo negro de Ridge Racer (dev48-dev50)

Volcados de RR por version (buffer grande al final del frame):

| version | donde | cielo |
|---|---|---|
| dev39, dev42-dev44, dev46 | Vita | bien |
| dev47 | laboratorio | bien |
| dev48 | laboratorio | **negro** |
| dev49, dev50 | Vita | **negro** |

El laboratorio reproduce el error, asi que no es una diferencia del GE real. Las otras pruebas:
- Los registros en el primer draw del cielo son iguales en dev46 y dev50.
- La textura y la paleta estan intactas en la VRAM falsa.
- Reproducir los comandos del juego con viewport x2 y el recorte de sub.h (Python o C) en
  PPSSPPHeadless dibuja el cielo.

Causa probable (a confirmar con dev51). Lo que si cambio es la cantidad de listas propias por frame: 15 en dev46 y 42 en dev50. dev48 hizo
que los vertices tras el plano cercano obliguen a recortar, y el cielo y el camino pasaron a usar el
recorte 3D. El anillo de listas del plugin (`TX_RING`) tenia 64. El GE va hasta un par de frames
detras del recorrido, asi que el anillo se daba la vuelta en 1.5 frames. Las primeras listas del
frame (render target del reflejo y cielo) se reescribian con las del frame siguiente antes de que
el GE las ejecutara. Es el patron 2 del original.

Los volcados no lo muestran: guardan cada lista al escribirla, no lo que el GE ejecuta despues. Por
eso todo se veia correcto al analizar.

**dev51 (sin confirmar):** el anillo pasa a 512 listas (512 KB, 0x0A808000-0x0A888000, ~12 frames con el uso de RR).

Regla para el futuro: toda memoria que el plugin escribe por draw o por frame y que lee el GE
(listas, vertices, copias) tiene que durar varios frames. Hoy cumplen:
- `SUB_RING`: ~1 MB, unos 16 frames.
- `TX_RING`: 512 listas.
- Lista de copia a la pantalla: doble buffer, con contenido igual cada frame.
- `Z_SAVE`: se usa en orden dentro del frame.

## 4. Pruebas regresivas

En el laboratorio, `memstick/seplugins/versiones/` tiene:
- `ge_patch_v0.2_original.prx`: v0.2 de TheOfficialFloW, con el arreglo de compilacion y los logs del primer commit.
- `ge_patch_dev46.prx`: ultimo con cielo bien.
- `ge_patch_dev49.prx`: cielo negro.
- `ge_patch_dev51.prx`.

Para probar uno, copiarlo como `seplugins/ge_patch.prx`. Las versiones anteriores a v0.2 no se
compilan: el codigo actual es otro, y la planilla ya dice que rompio cada una.
