# Ruta al codigo de Adrenaline (para systemctrl.h y libpspsystemctrl_kernel.a).
# Por defecto se espera clonado al lado de este repo: git clone https://github.com/TheOfficialFloW/Adrenaline
ADRENALINE ?= ../Adrenaline/cef
INCDIR = $(ADRENALINE)/include
LIBDIR = $(ADRENALINE)/lib

# make DEST=/Volumes/VITA/pspemu/seplugins copia el plugin ahi al terminar
all:
ifdef DEST
	cp $(TARGET).prx $(DEST)/$(TARGET).prx
endif

TARGET = ge_patch
OBJS = main.o log.o gu.o exports.o

CFLAGS = -O3 -Os -G0 -Wall -fshort-wchar -fno-pic -mno-check-zero-division
CXXFLAGS = $(CFLAGS) -fno-exceptions -fno-rtti
ASFLAGS = $(CFLAGS)

BUILD_PRX = 1
PRX_EXPORTS = exports.exp

USE_KERNEL_LIBC = 1
USE_KERNEL_LIBS = 1

LIBS = -lpspsystemctrl_kernel

PSPSDK = $(shell psp-config --pspsdk-path)
include $(PSPSDK)/lib/build_prx.mak
