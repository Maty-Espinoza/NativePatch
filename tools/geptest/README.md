# GEPTEST: pruebas del GE en la consola

Corre las 112 pruebas de `pspautotests/tests/gpu` en la Vita (Adrenaline) en un solo
proceso, para compararlas con la PSP real (`.expected`) y con PPSSPPHeadless.

1. Copia de pspautotests (viene en el repo de PPSSPP) con `pspautotests.patch`:
   `common` con `-DGEPTEST_INPROC` (salida a `ms0:/geptest/out/<id>`, sin salir del juego,
   libera el heap al terminar) y `return 0` en cuatro pruebas de `signals` (GCC 14 pone un
   `break` al final de una funcion sin return).
2. Compilar `common` y cada carpeta de `tests/gpu` con el toolchain del PSP.
3. `make -C corredor` y `armar.py PSPAUTOTESTS SALIDA`: deja `PSP/GAME/GEPTEST` (el corredor y
   `m/<id>.prx`) y `geptest/lista.txt` con la estructura de `ux0:/pspemu`.
4. En la Vita: abrir GEPTEST. Resultados en `ms0:/geptest/out/<id>.{txt,err,res,bmp}`.
5. `comparar.py LISTA PSPAUTOTESTS VITA PPSSPP [informe.html]`.

El mismo `SALIDA` corre en PPSSPPHeadless: `PPSSPPHeadless --graphics=software
--memstick=SALIDA SALIDA/PSP/GAME/GEPTEST/EBOOT.PBP` (las 112 en ~7 s).

`geptest.c` y `lanzador/` son la version anterior (un EBOOT por prueba, el plugin salta por
el menu): mas lenta (~25 s por prueba) pero cada prueba en un proceso nuevo.
