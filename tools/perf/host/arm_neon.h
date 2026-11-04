// arm_neon.h for the x86 build of the bench (tools/perf/instr.sh): the few NEON operations capture.c uses, in plain C.
#include <stdint.h>
#include <string.h>
typedef struct { uint32_t v[4]; } uint32x4_t;
typedef struct { uint32_t v[2]; } uint32x2_t;
static inline uint32x4_t vld1q_u32(const uint32_t *p) { uint32x4_t r; memcpy(&r, p, 16); return r; }
static inline uint32x4_t veorq_u32(uint32x4_t a, uint32x4_t b) { for (int i = 0; i < 4; i++) a.v[i] ^= b.v[i]; return a; }
static inline uint32x4_t vorrq_u32(uint32x4_t a, uint32x4_t b) { for (int i = 0; i < 4; i++) a.v[i] |= b.v[i]; return a; }
static inline uint32x2_t vget_low_u32(uint32x4_t a) { uint32x2_t r = { { a.v[0], a.v[1] } }; return r; }
static inline uint32x2_t vget_high_u32(uint32x4_t a) { uint32x2_t r = { { a.v[2], a.v[3] } }; return r; }
static inline uint32x2_t vorr_u32(uint32x2_t a, uint32x2_t b) { a.v[0] |= b.v[0]; a.v[1] |= b.v[1]; return a; }
#define vget_lane_u32(a, i) ((a).v[i])
typedef struct { float v[4]; } float32x4_t;
static inline float32x4_t vld1q_f32(const float *p) { float32x4_t r; memcpy(&r, p, 16); return r; }
static inline void vst1q_f32(float *p, float32x4_t a) { memcpy(p, &a, 16); }
static inline float32x4_t vmulq_n_f32(float32x4_t a, float s) { for (int i = 0; i < 4; i++) a.v[i] *= s; return a; }
static inline float32x4_t vmlaq_n_f32(float32x4_t a, float32x4_t b, float s) { for (int i = 0; i < 4; i++) a.v[i] += b.v[i] * s; return a; }
static inline float32x4_t vmulq_f32(float32x4_t a, float32x4_t b) { for (int i = 0; i < 4; i++) a.v[i] *= b.v[i]; return a; }
#define vgetq_lane_f32(a, i) ((a).v[i])
