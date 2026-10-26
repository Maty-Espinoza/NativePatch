#pragma once
// Stub de los tests: particiones de memoria (solo lo que usa main.c)
typedef struct { unsigned int size, startaddr, memsize, attr; } PspSysmemPartitionInfo;
int sceKernelQueryMemoryPartitionInfo(int pid, PspSysmemPartitionInfo *info);
int sceKernelAllocPartitionMemory(int pid, const char *name, int type, unsigned int size, void *addr);
#define PSP_SMEM_Addr 2
