# NPTEST: synthetic 2D battery (see main.c). make leaves EBOOT.PBP; it goes to ms0:/PSP/GAME/NPTEST/.
TARGET = nptest
OBJS = main.o scenes.o
CFLAGS = -O2 -G0 -Wall
LIBS = -lpspgu -lm
EXTRA_TARGETS = EBOOT.PBP
PSP_EBOOT_TITLE = NPTEST
BUILD_PRX = 1
PSPSDK = $(shell psp-config --pspsdk-path)
include $(PSPSDK)/lib/build.mak
