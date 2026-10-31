# Third-party notices

NativePatch is free software under the [GPLv3](LICENSE). It includes or uses these third-party parts:

| What | Where | License |
|---|---|---|
| Minimal `sceGu` functions (GE block copies), based on the PSPSDK | `nativelink/gu.c` | BSD (notice below) |
| GE rules (vertex size, commands) taken from how PPSSPP implements them | `nativedraw/captura.c` | PPSSPP is GPL 2.0 or later: compatible with GPLv3 |
| 8x16 font generated from DejaVu Sans Mono Bold | `instalador/font.h` | Bitstream Vera / DejaVu (allows modification and redistribution) |
| Compiled shaders `texture_v`/`texture_f` from vita2d (the ones from vita-shader-collection, which Adrenaline 8 uses) | `nativedraw/sombreadores.h` | vita2d (xerpi): MIT [verify the vita-shader-collection license before the release] |
| Adrenaline headers and library (`systemctrl.h`, `libpspsystemctrl_kernel.a`), only to build | downloaded by `tools/preparar.sh` | GPLv3 (Adrenaline) |
| pspdev, vitasdk, taiHEN | downloaded by `tools/preparar.sh` | their own licenses |

The project started as a fork of [GePatch](https://github.com/TheOfficialFloW/GePatch) by TheFlow; the code that
remained from it (scaling on the PSP) was removed in NativeLink 0.0.2 and is in the repo history (tag `gepatch-dev66`).

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
