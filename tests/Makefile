# Tests del plugin en el PC (no necesitan el SDK de PSP): make -C tests
# Requiere gcc con soporte de 32 bits (Ubuntu: apt install gcc-multilib)
CFLAGS = -m32 -fPIE -pie -O1 -g -Wall -Wno-unused-function -Wno-unused-variable -Iinclude -I..

test: test_main
	./test_main

test_main: test_main.c stubs.c ../sub.h ../main.c ../log.c ../log.h ../ge_constants.h ../menu.c ../menu.h ../dump.c ../dump.h
	$(CC) $(CFLAGS) -o $@ test_main.c stubs.c ../log.c ../menu.c ../dump.c

# Benchmark: tiempo de patchGeList con una lista tipo Gran Turismo (para comparar versiones)
.PHONY: test bench clean
bench: bench_bin
	./bench_bin

bench_bin: bench.c test_main.c stubs.c ../main.c ../log.c ../log.h ../menu.c ../dump.c
	$(CC) $(CFLAGS) -O2 -o $@ bench.c stubs.c ../log.c ../menu.c ../dump.c

clean:
	rm -f test_main bench_bin
