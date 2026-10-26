#ifndef KB_WALK_H
#define KB_WALK_H
#ifndef KB_NO_MODE
#define KB_NO_MODE  // el recorrido no usa Kermit
#endif
#include "kbench.h"

// Recorre una lista del GE como lo hace patchGeList de GePatch: decodifica cada comando, sigue
// BASE/JUMP/CALL/RET, calcula el tamaño de los vértices, duplica los vértices 2D (through) y anota
// en una tabla los comandos que GePatch parcharía (fb, viewport, scissor, región, textura).
// No modifica la lista. Es el mismo código en el PSP y en el ARM, para comparar en igualdad.
//   mem      puntero a la dirección del PSP memBase (sin bits de segmento: 0x08xxxxxx)
//   out      salida (vértices duplicados y tabla de parches), outWords palabras como máximo
// Todo acceso fuera de [memBase, memBase + memSize) cuenta como error y detiene el recorrido.
uint32_t kbWalk(const uint8_t *mem, uint32_t memBase, uint32_t memSize, uint32_t list,
                uint32_t *out, uint32_t outWords, uint32_t maxCmds, KbWalkStats *st);

// Arma en buf una lista sintética de un frame parecido a Gran Turismo (~targetCmds comandos
// ejecutados, ~1 draw cada 4,5 comandos, 1 de cada 6 draws en 2D) y devuelve la dirección de la
// lista principal (0 si no cabe). Determinista: misma semilla, misma lista.
// Ocupa como máximo bufSize bytes; deja libres los últimos KB_GEN_OUT bytes para la salida.
#define KB_GEN_OUT 0x40000
uint32_t kbGen(uint8_t *buf, uint32_t bufAddr, uint32_t bufSize, uint32_t targetCmds, uint32_t seed);

#endif
