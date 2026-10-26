# EBOOT KBENCH (homebrew). Llama a kbench.prx con sctrlHENFindFunction + kuKernelCall (ver main.c).
TARGET = kbench_eboot
OBJS = main.o
CFLAGS = -O2 -G0 -Wall
ASFLAGS = $(CFLAGS)
BUILD_PRX = 1
PSP_FW_VERSION = 661
LIBS = -lpspsystemctrl_user -lpspkubridge -lpspdebug -lpspctrl -lpspdisplay -lpsppower
EXTRA_TARGETS = EBOOT.PBP
PSP_EBOOT_TITLE = KBENCH
PSPSDK = $(shell psp-config --pspsdk-path)
include $(PSPSDK)/lib/build.mak
