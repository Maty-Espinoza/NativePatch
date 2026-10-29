# Avisos de terceros

NativePatch es software libre bajo la [GPLv3](LICENSE). Incluye o usa estas partes de terceros:

| Qué | Dónde | Licencia |
|---|---|---|
| Funciones mínimas de `sceGu` (copias del GE), basadas en el PSPSDK | `nativelink/gu.c` | BSD (aviso abajo) |
| Reglas del GE (tamaño de vértice, comandos) tomadas de cómo las implementa PPSSPP | `nativedraw/captura.c` | PPSSPP es GPL 2.0 o posterior: compatible con GPLv3 |
| Fuente 8x16 generada desde DejaVu Sans Mono Bold | `instalador/font.h` | Bitstream Vera / DejaVu (permite modificar y redistribuir) |
| Encabezados y biblioteca de Adrenaline (`systemctrl.h`, `libpspsystemctrl_kernel.a`), solo para compilar | se bajan con `tools/preparar.sh` | GPLv3 (Adrenaline) |
| pspdev, vitasdk, taiHEN | se bajan con `tools/preparar.sh` | sus licencias |

El proyecto nació como fork de [GePatch](https://github.com/TheOfficialFloW/GePatch) de TheFlow; el código que quedaba de
él (escalado en el PSP) se quitó en NativeLink 0.0.2 y está en la historia del repo (etiqueta `gepatch-dev66`).

## PSPSDK

PSPSDK BSD-compatible copyright notice (Some parts of the PSPSDK were used in this emulator (defines, constants, headers))

Copyright (c) 2005  adresd
Copyright (c) 2005  Marcus R. Brown
Copyright (c) 2005  James Forshaw
Copyright (c) 2005  John Kelley
Copyright (c) 2005  Jesper Svennevid
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:
1. Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright
   notice, this list of conditions and the following disclaimer in the
   documentation and/or other materials provided with the distribution.
3. The names of the authors may not be used to endorse or promote products
   derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE AUTHORS ``AS IS'' AND ANY EXPRESS OR
IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY DIRECT, INDIRECT,
INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
