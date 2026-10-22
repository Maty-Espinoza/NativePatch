#!/bin/sh
# Corre un juego o homebrew en el modelo de Adrenaline (PPSSPP con adrenaline.patch) con los
# plugins de MEMSTICK/seplugins/game.txt, como en la Vita.
#   modelo.sh PPSSPPHeadless MEMSTICK JUEGO [opciones de PPSSPPHeadless...]
# Las capturas (--screenshot-save=X.bmp) dejan ademas X_nativo.bmp: lo que muestra la Vita.
# PPSSPP_ADRENALINE=estricto detiene la emulacion en lo que en la Vita cae o congela Adrenaline.
PP=$1 MS=$2 JUEGO=$3; shift 3
PPSSPP_ADRENALINE=${PPSSPP_ADRENALINE:-1} exec "$PP" --graphics=software --memstick="$MS" "$@" "$JUEGO"
