# Escena GEPVM (ver main.c). make: kcap/kcap.prx y EBOOT.PBP
TARGET = gepvm
OBJS = main.o kcap/kcap.o_stub
CFLAGS = -O2 -G0 -Wall
LIBS = -lpspgum -lpspgu -lm
EXTRA_TARGETS = EBOOT.PBP
PSP_EBOOT_TITLE = GEPVM - escena de prueba de GePatch
BUILD_PRX = 1
PSPSDK = $(shell psp-config --pspsdk-path)
include $(PSPSDK)/lib/build.mak

kcap/kcap.prx:
	$(MAKE) -C kcap
kcap/kcap.S: kcap/exports.exp
	cd kcap && psp-build-exports -s exports.exp
kcap/kcap.o_stub: kcap/kcap.S kcap/kcap.prx
	$(CC) $(CFLAGS) -I$(PSPSDK)/include -c -o $@ kcap/kcap.S
