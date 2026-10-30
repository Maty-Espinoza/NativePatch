# NativeLink (lado PSP). Normalmente se compila con tools/compilar.sh, que deja nativelink.prx en build/.
# Ruta al codigo de Adrenaline (para systemctrl.h y libpspsystemctrl_kernel.a). Por defecto, clonado al lado del repo:
# git clone https://github.com/TheOfficialFloW/Adrenaline
ADRENALINE ?= ../../Adrenaline/cef
INCDIR = $(ADRENALINE)/include
LIBDIR = $(ADRENALINE)/lib

TARGET = nativelink
OBJS = main.o log.o gu.o exports.o

# Optimizacion: make OPT=-Os para probar la version optimizada para tamano
OPT ?= -O2
CFLAGS = $(OPT) -G0 -Wall -fshort-wchar -fno-pic -mno-check-zero-division
CXXFLAGS = $(CFLAGS) -fno-exceptions -fno-rtti
ASFLAGS = $(CFLAGS)

BUILD_PRX = 1
PRX_EXPORTS = exports.exp

USE_KERNEL_LIBC = 1
USE_KERNEL_LIBS = 1

LIBS = -lpspsystemctrl_kernel

PSPSDK = $(shell psp-config --pspsdk-path)
include $(PSPSDK)/lib/build_prx.mak

# recompilar si cambia log.h (version, macros del log, descriptor)
main.o log.o: log.h
