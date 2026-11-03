// prof.h: profile of a frame's whole journey (development builds, extreme log level). Each stage P_* adds up a count, its
// microseconds and its maximum; each stage is written by one thread. capture.c holds the table and writes it once per
// second as "prof <group>:" lines of nativedraw_capture.log, name=count/average_us/max_us; tools/prof_summary.py ranks
// them. Meaning of each stage: docs/dev/<language>/logs-and-switches.md.
#ifdef NP_DEV
#include <stdint.h>
enum {
  P_FRAME, P_GAME, P_VBLANK, P_GATE, P_SYNC, P_MIRROR, P_HOOKS, P_GE, P_GEMIR, P_LISTS,  // PSP
  P_C_TOTAL, P_C_OWN, P_C_SHARED, P_C_RECORD, P_C_STATE, P_C_VERT, P_C_TEX, P_C_MEMO, P_C_FLIP, P_C_VFLUSH, P_C_SLEEP,  // capturer
  P_R_MIRROR, P_R_COPY,  // record thread
  P_L_CLOSE, P_L_PUB, P_L_TAKE,  // time since the PSP flip, since the flip, since it was published
  P_A_PERIOD, P_A_END, P_A_DMA, P_A_PRE, P_A_WAIT, P_A_TOTAL,  // Adrenaline's drawing thread
  P_T_CHECK, P_T_TEX, P_T_VERT, P_T_GXM, P_T_SCENE, P_T_ALL, P_T_END,  // translator (first six in the order of t_us)
  P_F_COMPLETE, P_F_UNTAKEN, P_F_NOREC, P_F_BEHIND, P_F_DRAWN, P_F_REPEAT, P_F_NORMAL,  // what happened to the frames
  P_G_ADR, P_G_NORM, P_G_TR, P_G_INT,  // GPU, in the order of gpu_timer.c (gpu_timer switch)
  P_U_CAP, P_U_REC, P_U_GFX, P_U_RD0, P_U_RD1, P_U_FREE0, P_U_FREE1, P_U_FREE2,  // processor received per second
  PN
};
typedef struct { volatile uint32_t n, us, max; } PStage;
extern PStage P[PN];
extern volatile uint32_t p_on;  // 1 at the extreme log level
static inline void pAdd(int k, uint32_t us) { PStage *p = &P[k]; p->n++; p->us += us; if (us > p->max) p->max = us; }
#define PT() (p_on ? now() : 0)
#define PA(k, t) do { if (t) pAdd(k, now() - (t)); } while (0)
#endif
