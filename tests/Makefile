# Tests del plugin en el PC (no necesitan el SDK de PSP): make -C tests
# Requiere gcc con soporte de 32 bits (Ubuntu: apt install gcc-multilib)
CFLAGS = -m32 -fPIE -pie -O1 -g -Wall -Wno-unused-function -Wno-unused-variable -Iinclude -I..

test: test_main
	./test_main

test_main: test_main.c stubs.c ../main.c ../log.c ../log.h ../ge_constants.h
	$(CC) $(CFLAGS) -o $@ test_main.c stubs.c ../log.c

clean:
	rm -f test_main
