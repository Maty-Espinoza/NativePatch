# kbench.prx (plugin de kernel). systemctrl viene con el pspdev actual. Uso:
#   make ADRENALINE_SRC=<raiz de isage/Adrenaline>   (toma KERMIT_MODE_EXTRA_1 de adrenaline_vita.h)
TARGET = kbench
OBJS = main.o ../comun/walk.o
INCDIR = ../comun
CFLAGS = -O2 -G0 -Wall -fno-pic
ifdef ADRENALINE_SRC
CFLAGS += -DKB_USE_ADRENALINE_H -I$(ADRENALINE_SRC)
endif
ifdef KB_MODE
CFLAGS += -DKB_MODE=$(KB_MODE)
endif
ASFLAGS = $(CFLAGS)
BUILD_PRX = 1
PRX_EXPORTS = exports.exp
USE_KERNEL_LIBC = 1
USE_KERNEL_LIBS = 1
LIBS = -lpspsystemctrl_kernel -lpspge_driver -lpsppower_driver -lgcc
PSPSDK = $(shell psp-config --pspsdk-path)
include $(PSPSDK)/lib/build.mak  # build_prx.mak del pspdev actual ignora PRX_EXPORTS
