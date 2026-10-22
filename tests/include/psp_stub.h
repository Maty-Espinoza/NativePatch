// Cabecera unica que reemplaza al PSPSDK para compilar el plugin en el PC (tests).
// Solo declara lo que usan main.c y log.c; las funciones estan en tests/stubs.c.
#ifndef PSP_STUB_H
#define PSP_STUB_H
#include <stdint.h>
#include <stddef.h>

typedef uint8_t u8; typedef uint16_t u16; typedef unsigned long u32; // en la PSP u32 es unsigned long
typedef int SceUID; typedef unsigned int SceSize;
typedef struct { unsigned int TimeStamp, Buttons; unsigned char Lx, Ly, Rsrv[6]; } SceCtrlData;
typedef struct { unsigned int context[512]; } PspGeContext;
typedef struct { unsigned int size; PspGeContext *context; } PspGeListArgs;

#define PSP_MODULE_INFO(name, attr, maj, min) static const char *module_name_unused = name
#define PSP_CTRL_SELECT   0x000001
#define PSP_CTRL_START    0x000008
#define PSP_CTRL_UP       0x000010
#define PSP_CTRL_RIGHT    0x000020
#define PSP_CTRL_DOWN     0x000040
#define PSP_CTRL_LEFT     0x000080
#define PSP_CTRL_CROSS    0x004000
#define PSP_CTRL_LTRIGGER 0x000100
#define PSP_CTRL_RTRIGGER 0x000200
#define PSP_CTRL_TRIANGLE 0x001000
#define PSP_CTRL_CIRCLE   0x002000
#define PSP_O_RDONLY 0x0001
#define PSP_O_WRONLY 0x0002
#define PSP_O_CREAT  0x0200
#define PSP_O_TRUNC  0x0400
#define PSP_SEEK_END 2

int pspSdkSetK1(int k1);
unsigned int sceKernelCpuSuspendIntr(void);
int sceKernelGetThreadId(void);
void sceKernelCpuResumeIntr(unsigned int flags);
SceUID sceIoOpen(const char *file, int flags, int mode);
int sceIoClose(SceUID fd);
int sceIoRead(SceUID fd, void *data, SceSize size);
int sceIoWrite(SceUID fd, const void *data, SceSize size);
long long sceIoLseek(SceUID fd, long long offset, int whence);
int sceIoRemove(const char *file);
int sceIoRename(const char *oldname, const char *newname);
char *sceKernelInitFileName(void);
SceUID sceKernelCreateThread(const char *name, int (*entry)(SceSize, void *), int prio, int stack, unsigned int attr, void *opt);
int sceKernelStartThread(SceUID thid, SceSize arglen, void *argp);
int sceKernelDelayThread(SceUID delay);
unsigned int sceKernelGetSystemTimeLow(void);
void sceKernelDcacheWritebackInvalidateAll(void);
void sceKernelDcacheWritebackInvalidateRange(const void *p, unsigned int size);
void sceKernelDcacheWritebackRange(const void *p, unsigned int size);
void sceKernelIcacheClearAll(void);
int sceCtrlPeekBufferPositive(SceCtrlData *pad, int count);
unsigned int FindProc(const char *mod, const char *lib, unsigned int nid);
void sctrlHENPatchSyscall(unsigned int addr, void *newaddr);
int sceGuFinish(void);
void sceGuCopyImage(int psm, int sx, int sy, int width, int height, int srcw, void *src, int dx, int dy, int destw, void *dest);
#endif
