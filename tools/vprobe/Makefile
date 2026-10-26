# VPROBE (homebrew de Adrenaline). make -> EBOOT.PBP
TARGET = vprobe
OBJS = main.o
CFLAGS = -O2 -G0 -Wall -Wno-format
ASFLAGS = $(CFLAGS)
BUILD_PRX = 1
PSP_FW_VERSION = 661
LIBS = -lpspgu -lpspsystemctrl_user -lpspkubridge -lpspdebug -lpspctrl -lpspdisplay -lpsppower -lpspdmac -lpspge
EXTRA_TARGETS = EBOOT.PBP
PSP_EBOOT_TITLE = VPROBE
PSPSDK = $(shell psp-config --pspsdk-path)
include $(PSPSDK)/lib/build.mak
