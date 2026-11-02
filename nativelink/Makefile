# NativeLink (PSP side). Normally built with tools/compilar.sh, which leaves nativelink.prx in build/.
# Path to the Adrenaline code (for systemctrl.h and libpspsystemctrl_kernel.a). By default, cloned next to the repo:
# git clone https://github.com/TheOfficialFloW/Adrenaline
ADRENALINE ?= ../../Adrenaline/cef
INCDIR = $(ADRENALINE)/include
LIBDIR = $(ADRENALINE)/lib

TARGET = nativelink
OBJS = main.o log.o gu.o exports.o

# Optimization: make OPT=-Os to try the size-optimized build
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

# rebuild if log.h changes (version, log macros, descriptor)
main.o log.o: log.h
