# vpjuego.prx (B1, plugin de kernel para juegos). make -> vpjuego.prx
TARGET = vpjuego
OBJS = vpjuego.o
CFLAGS = -O2 -G0 -Wall -Wno-format -fno-pic
ASFLAGS = $(CFLAGS)
BUILD_PRX = 1
USE_KERNEL_LIBC = 1
USE_KERNEL_LIBS = 1
LIBS = -lpspsystemctrl_kernel -lpsppower_driver -lgcc
PSPSDK = $(shell psp-config --pspsdk-path)
include $(PSPSDK)/lib/build.mak
