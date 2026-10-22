# Plugin de la bateria (geptest.prx). make ADRENALINE=... (como el de ge_patch)
ADRENALINE ?= ../../../Adrenaline/cef
INCDIR = $(ADRENALINE)/include
LIBDIR = $(ADRENALINE)/lib

TARGET = geptest
OBJS = geptest.o
CFLAGS = -O2 -G0 -Wall -fno-pic
ASFLAGS = $(CFLAGS)
BUILD_PRX = 1
USE_KERNEL_LIBC = 1
USE_KERNEL_LIBS = 1
LIBS = -lpspsystemctrl_kernel -lpspdisplay_driver

PSPSDK = $(shell psp-config --pspsdk-path)
include $(PSPSDK)/lib/build_prx.mak
