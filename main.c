#include <pspsdk.h>
#include <pspkernel.h>
#include <pspctrl.h>
#include <pspge.h>
#include <pspgu.h>

#include <stdio.h>
#include <string.h>

#include <systemctrl.h>

#include "ge_constants.h"
#include "log.h"
#include "menu.h"
#include "dump.h"

PSP_MODULE_INFO("GePatch", 0x1007, 1, 0);

#define DRAW_NATIVE 0xABCDEF00

#define PITCH 960
#define WIDTH 960
#define HEIGHT 544
#define PIXELFORMAT GE_FORMAT_565

#define FAKE_VRAM 0x0A000000
#define DISPLAY_BUFFER 0x0A400000
#define RENDER_LIST 0x0A800000

// El buffer grande va en 0x000000, igual que el primer buffer de casi todos los juegos.
// (En dev12 se movio a 0x40 para distinguirlo y Gran Turismo dio pantalla negra en
// carrera: el emulador probablemente exige framebuffers alineados. La ambiguedad del
// doble buffer se resuelve en patchGeList mirando el FBWIDTH vecino.)
#define VRAM_DRAW_BUFFER_OFFSET 0x04000000
#define VRAM_DEPTH_BUFFER_OFFSET depth_vram // 0x04100000, o 0x04200000 si la VRAM alta desordena lo dibujado
static u32 depth_vram = 0x04100000;
#define VRAM_1KB 0x041ff000

#ifndef BUILD_OPT
#define BUILD_OPT "?"
#endif

#define STATS_EVERY 600 // frames entre cada linea de estadisticas (~10 s)

// Contadores baratos que se vuelcan al log cada STATS_EVERY frames
static struct {
  u32 lists, heads, stalls, draws, skipped, stack_full, transfers, fb_transfers, fallback, enq_copies, idle_frames;
  u32 rt_draws, rt_tex, rt_incomplete, rt_fail;
  // Rendimiento (microsegundos)
  u32 frames, frame_us, frame_max, slow60, slow30; // tiempo entre flips
  u32 patch_us, patch_max, cmds;                   // costo del plugin por frame
  u32 upscale_us, fb_copies, cpu_overlays;         // escalado por CPU (videos), copias del frame al fb del juego, dibujos de la CPU pegados
  u32 walked, calls, lookups, v2d, v2d_skip, vevict, call_cmds, tx_ge, tx_screen, vram_fix;
  u32 wc_hits, wc_miss, wc_cmds, wc_summed;                   // sublistas sin cambios que no se recorrieron
  u32 wc_why[8];                                   // por que no se guardaron (ver W_*) // recorrido: frames medidos, CALLs, busquedas en memo, vertices 2D
  u32 long_frame_logged;
} st;
static u32 patch_us_frame; // costo del plugin en el frame en curso

// Un draw anulado se reemplaza por un NOP con esta marca. Con NOP 0x00000000 (igual al
// NOP del juego), un NOP real escrito luego en esa posicion "revivia" el draw anulado
// de un frame anterior, con vertices de cualquier cosa (texturas y triangulos basura).
#define NOP_MARK ((GE_CMD_NOP << 24) | 0x0D1E07)
#define NOP_OMIT (opt.nop_mark ? NOP_MARK : (GE_CMD_NOP << 24)) // opcion del menu
#define compat_walk (!opt.walk_new)                               // opcion del menu

// Modo pasivo (L al iniciar): el mismo recorrido de listas y los mismos logs, pero sin
// escribir nada; el juego corre como en una PSP normal. Sirve de referencia para comparar.
static int passive;

// Captura automatica del frame siguiente cuando aparece algo raro por primera vez (fb
// intermedio, render target incompleto, lista invalida...): asi el log trae el detalle
// sin tener que presionar L+R+SELECT en el momento justo. Maximo 5 por sesion.
static void autoCapture(void);
#define LOG_ONCE_CAP(key, ...) do { if (logOnce(key)) { LOG(__VA_ARGS__); autoCapture(); } } while (0)
// En pasivo solo se recorre 1 de cada PASSIVE_SAMPLE frames (y el de la captura): recorrer
// todas las listas cuesta lo mismo que en modo activo (14-20 ms en Gran Turismo) y el
// "original" salia igual de lento. Las cifras por frame se calculan sobre los medidos.
#define PASSIVE_SAMPLE 8
static int walk_now = 1;

// En modo pasivo el juego usa la VRAM real (0x04...): se muestra como VRAM falsa (0x0A...)
// para que los logs de ambos modos se puedan comparar linea por linea.
static inline u32 normVram(u32 a) {
  return (passive && (a & 0x0f000000) == 0x04000000) ? 0x0A000000 | (a & 0x00ffffff) : a;
}

// Direccion de la VRAM real (0x04..., 4 MB) como VRAM falsa (0x0A...). Silent Hill usa la
// VRAM real fija en vez de la direccion que le da el plugin: en el hardware alli estan el
// buffer grande, la profundidad y los render targets del plugin.
static inline u32 toFake(u32 a) {
  return (a & 0x0fc00000) == 0x04000000 ? 0x0A000000 | (a & 0x3fffff) : a;
}

#define MAX_CMDS 200000 // tope de comandos por recorrido, para no colgarse en listas en bucle

// Memoria de parches. Muchos juegos vuelven a ejecutar la misma lista, cuyos comandos
// ya estan parcheados: sin esto se leia el valor parcheado como si fuera del juego
// (framebuffer mal identificado, y no se podria escalar viewport/scissor sin duplicar
// dos veces). Se guarda el original por direccion de comando.
// Va en RAM libre (0x0A600000, entre DISPLAY_BUFFER y RENDER_LIST): con 2048 entradas
// Ridge Racer la desbordaba y se perdian valores originales (y se duplicaban dos veces).
#define MEMO_ADDR 0x0A600000
#define MEMO_SIZE 65536 // 768 KB; dos comandos chocan solo si estan a 256 KB de distancia
typedef struct { u32 *addr; u32 orig, patched; } MemoEntry;
static MemoEntry *memo = (MemoEntry *)MEMO_ADDR;

static u32 origOp(u32 *p) {
  int i = ((u32)p >> 2) & (MEMO_SIZE - 1);
  return (memo[i].addr == p && *p == memo[i].patched) ? memo[i].orig : *p;
}

static u32 n_setops; // cuantos parches se hicieron (para saber si una sublista se modifica)
static u32 *dump_cur; // comando que se esta recorriendo (para el volcado)
static void setOp(u32 *p, u32 orig, u32 op) {
  n_setops++;
  if (passive)
    return;
  int i = ((u32)p >> 2) & (MEMO_SIZE - 1);
  memo[i].addr = p;
  memo[i].orig = orig;
  memo[i].patched = op;
  *p = op;
  if (dumping && p != dump_cur) // parche de un comando anterior: va al volcado con el bit 0
    dumpCmd((u32 *)((u32)p | 1), orig);
}

// Solo estos comandos pueden haber sido parcheados por el plugin (y NOP = draw anulado).
// Para el resto no hace falta buscar en la memoria de parches: buscar en cada comando
// costaba ~0.5 us por comando en la PSP (fallos de cache en una tabla de 768 KB) y en
// Gran Turismo (52 000 comandos por frame) el plugin solo ya tomaba 30 ms por frame.
// 2 = comandos de textura: solo se parchean si algun draw uso un render target.
static const u8 memo_cmd[256] = {
  [GE_CMD_NOP] = 1, [GE_CMD_FRAMEBUFPTR] = 1, [GE_CMD_FRAMEBUFWIDTH] = 1, [GE_CMD_FRAMEBUFPIXFORMAT] = 1,
  [GE_CMD_ZBUFPTR] = 1, [GE_CMD_ZBUFWIDTH] = 1, [GE_CMD_VIEWPORTXSCALE] = 1, [GE_CMD_VIEWPORTYSCALE] = 1,
  [GE_CMD_VIEWPORTXCENTER] = 1, [GE_CMD_VIEWPORTYCENTER] = 1, [GE_CMD_OFFSETX] = 1, [GE_CMD_OFFSETY] = 1,
  [GE_CMD_REGION1] = 1, [GE_CMD_REGION2] = 1, [GE_CMD_SCISSOR1] = 1, [GE_CMD_SCISSOR2] = 1,
  [GE_CMD_TEXADDR0] = 2, [GE_CMD_TEXBUFWIDTH0] = 2, [GE_CMD_TEXSIZE0] = 2, [GE_CMD_TEXFORMAT] = 2,
  [GE_CMD_BASE] = 3, [GE_CMD_CALL] = 3, // 3 = solo si se reescribio alguna copia (ver txUpscale)
};
static int tx_patched_any; // alguna copia grande se reemplazo por un CALL a una lista del plugin
static int tex_patched_any; // algun comando de textura se redirigio a un render target
static u32 rt_gen;          // cambia cuando cambia algo que afecta a texCheck (rts, framebuffers)

// Ultimos OFFSETX/Y del juego (en pixeles), para escalar el centro del viewport
static float game_offx = 2048 - 240, game_offy = 2048 - 136;

// Estado entre un flip (sceDisplaySetFrameBuf) y el siguiente
static u32 draws_since_flip, big_transfer, last_flip;
// Draws con color en el buffer grande en este frame, y el fb que el juego pego en un fb
// ignorado (el mostrado de 1024 de Silent Hill). Si no hubo ninguno con color, la imagen
// esta en ese fb (logo de Silent Hill: lo pone la CPU y el GE solo lo pasa al mostrado)
static u32 color_draws, disp_tex, disp_tex_w, disp_tex_fmt;
// Ultima copia grande (TRANSFER) a un fb en el frame: de ahi sale la imagen original
// escalada. Silent Hill copia el video a un fb de 512 que despues pasa por otro de 1024.
static struct { u32 addr, w, fmt; } big_tx;

// Uso de la VRAM del juego (VRAM falsa): direccion mas alta vista en fb, texturas y copias
static u32 vram_hi;
static inline void vramUse(u32 a) {
  if (a >= FAKE_VRAM && a < FAKE_VRAM + 0x400000 && a > vram_hi)
    vram_hi = a;
}

// El frame que vio el juego se copia reducido a su fb (opcion "copia"): asi los efectos
// que leen el frame anterior como textura (Gran Turismo) ven una imagen real
static int fbtex_seen, menu_shown;
static u32 copy_vram; // buffer temporal en la VRAM alta (de los render targets)


// 1 si se puede leer: VRAM real o RAM (incluye la VRAM falsa en 0x0A000000)
static inline int validAddr(u32 addr) {
  addr &= 0x0fffffff;
  return (addr >= 0x04000000 && addr < 0x04200000) || (addr >= 0x08000000 && addr < 0x0A400000);
}

// Fin de la region valida que contiene addr, o NULL si addr no es valida. El recorrido
// solo vuelve a validar al cruzar este limite o al saltar (antes validaba cada comando).
static inline u32 *regionEnd(u32 *p) {
  u32 a = (u32)p & 0x0fffffff;
  if (a >= 0x04000000 && a < 0x04200000)
    return (u32 *)(((u32)p & 0xf0000000) | 0x04200000);
  if (a >= 0x08000000 && a < 0x0A400000)
    return (u32 *)(((u32)p & 0xf0000000) | 0x0A400000);
  return NULL;
}

static const u8 tcsize[4] = { 0, 2, 4, 8 }, tcalign[4] = { 0, 1, 2, 4 };
static const u8 colsize[8] = { 0, 0, 0, 0, 2, 2, 2, 4 }, colalign[8] = { 0, 0, 0, 0, 2, 2, 2, 4 };
static const u8 nrmsize[4] = { 0, 3, 6, 12 }, nrmalign[4] = { 0, 1, 2, 4 };
static const u8 possize[4] = { 3, 3, 6, 12 }, posalign[4] = { 1, 1, 2, 4 };
static const u8 wtsize[4] = { 0, 1, 2, 4 }, wtalign[4] = { 0, 1, 2, 4 };

#define ALIGN(x, align) (((x) + ((align) - 1)) & ~((align) - 1))

void getVertexInfo(u32 op, u8 *vertex_size, u8 *pos_off, u8 *visit_off, u8 *tc_off) {
  int tc = (op & GE_VTYPE_TC_MASK) >> GE_VTYPE_TC_SHIFT;
  int col = (op & GE_VTYPE_COL_MASK) >> GE_VTYPE_COL_SHIFT;
  int nrm = (op & GE_VTYPE_NRM_MASK) >> GE_VTYPE_NRM_SHIFT;
  int pos = (op & GE_VTYPE_POS_MASK) >> GE_VTYPE_POS_SHIFT;
  int weight = (op & GE_VTYPE_WEIGHT_MASK) >> GE_VTYPE_WEIGHT_SHIFT;
  int weightCount = ((op & GE_VTYPE_WEIGHTCOUNT_MASK) >> GE_VTYPE_WEIGHTCOUNT_SHIFT) + 1;
  int morphCount = ((op & GE_VTYPE_MORPHCOUNT_MASK) >> GE_VTYPE_MORPHCOUNT_SHIFT) + 1;

  u8 biggest = 0;
  u8 size = 0;
  u8 aligned_size = 0;
  // u8 weightoff = 0, tcoff = 0, coloff = 0, nrmoff = 0;
  u8 posoff = 0;
  u8 visitoff = 0;

  if (weight) {
    // size = ALIGN(size, wtalign[weight]);
    // weightoff = size;
    size += wtsize[weight] * weightCount;
    if (wtalign[weight] > biggest)
      biggest = wtalign[weight];
  }

  if (tc) {
    aligned_size = ALIGN(size, tcalign[tc]);
    if (!visitoff && aligned_size != size)
      visitoff = size;
    size = aligned_size;
    *tc_off = size;
    size += tcsize[tc];
    if (tcalign[tc] > biggest)
      biggest = tcalign[tc];
  }

  if (col) {
    aligned_size = ALIGN(size, colalign[col]);
    if (!visitoff && aligned_size != size)
      visitoff = size;
    size = aligned_size;
    // coloff = size;
    size += colsize[col];
    if (colalign[col] > biggest)
      biggest = colalign[col];
  }

  if (nrm) {
    aligned_size = ALIGN(size, nrmalign[nrm]);
    if (!visitoff && aligned_size != size)
      visitoff = size;
    size = aligned_size;
    // nrmoff = size;
    size += nrmsize[nrm];
    if (nrmalign[nrm] > biggest)
      biggest = nrmalign[nrm];
  }

  if (pos) {
    aligned_size = ALIGN(size, posalign[pos]);
    if (!visitoff && aligned_size != size)
      visitoff = size;
    size = aligned_size;
    posoff = size;
    size += possize[pos];
    if (posalign[pos] > biggest)
      biggest = posalign[pos];
  }

  aligned_size = ALIGN(size, biggest);
  if (!visitoff && aligned_size != size)
    visitoff = size;
  size = aligned_size;
  size *= morphCount;

  *vertex_size = size;
  *pos_off = posoff;
  *visit_off = visitoff;
}

// getVertexInfo con cache: los juegos repiten el mismo formato en muchos draws seguidos
static void vertexInfo(u32 vt, u8 *size, u8 *pos_off, u8 *tc_off) {
  static u32 last = 0xffffffff;
  static u8 s, p, t;
  if (vt != last) {
    u8 visit;
    s = p = t = 0;
    getVertexInfo(vt, &s, &p, &visit, &t);
    last = vt;
  }
  *size = s; *pos_off = p; *tc_off = t;
}

void GetIndexBounds(const void *inds, int count, u32 vertType, u16 *indexLowerBound, u16 *indexUpperBound) {
  int i;
  int lowerBound = 0x7FFFFFFF;
  int upperBound = 0;
  u32 idx = vertType & GE_VTYPE_IDX_MASK;
  if (idx == GE_VTYPE_IDX_8BIT) {
    const u8 *ind8 = (const u8 *)inds;
    for (i = 0; i < count; i++) {
      u8 value = ind8[i];
      if (value > upperBound)
        upperBound = value;
      if (value < lowerBound)
        lowerBound = value;
    }
  } else if (idx == GE_VTYPE_IDX_16BIT) {
    const u16 *ind16 = (const u16 *)inds;
    for (i = 0; i < count; i++) {
      u16 value = ind16[i];
      if (value > upperBound)
        upperBound = value;
      if (value < lowerBound)
        lowerBound = value;
    }
  } else if (idx == GE_VTYPE_IDX_32BIT) {
    const u32 *ind32 = (const u32 *)inds;
    for (i = 0; i < count; i++) {
      u16 value = (u16)ind32[i];
      if (value > upperBound)
        upperBound = value;
      if (value < lowerBound)
        lowerBound = value;
    }
  } else {
    lowerBound = 0;
    upperBound = count - 1;
  }
  *indexLowerBound = (u16)lowerBound;
  *indexUpperBound = (u16)upperBound;
}

typedef struct {
  u32 *list;
  u32 base;
  u32 offset;
  int ncmds; // comandos recorridos al entrar (estadistica de sublistas)
} StackEntry;

typedef struct {
  u32 ge_cmd[0x100];

  u32 framebufptr;
  u32 framebufwidth;
  u32 *framebufptr_addr;
  u32 *framebufwidth_addr;
  u32 framebuf;
  struct RenderTarget *cur_rt; // render target en uso como destino, o NULL

  // Ultimos comandos de textura (direccion en la lista y valor original), para
  // redirigirlos a un render target al momento del draw: TEXADDR0, TEXBUFWIDTH0, TEXSIZE0, TEXFORMAT
  u32 *tex_p[4];
  u32 tex_o[4];

  u32 base;
  u32 offset;
  u32 address;

  u32 index_addr;
  u32 vertex_addr;
  u32 vertex_type;

  u32 ignore_framebuf;

  u32 has_draws;

  StackEntry stack[64];
  u32 curr_stack;

  u32 framebuf_addr[16];
  u8 fb_tx[16];      // lo ultimo que escribio en ese fb fue una copia (TRANSFER), no un draw
  u8 fb_shown[16];   // el juego mostro este fb (o una parte) en pantalla
  u8 fb_layer[16];   // fb intermedio: nunca se muestra y se usa como textura en la pantalla
  u8 fb_swap[16];    // pase que se mezcla con la pantalla: se dibuja por intercambio (ver swapEnter)
  u8 fb_csrc[16];    // el juego copia desde este fb (formulario de Silent Hill): ver csrc_rt
  u8 fb_cfail[16];   // veces que eso fallo (con 3 se deja de intentar)
  u32 *fmt_p, fmt_o; // ultimo FRAMEBUFPIXFORMAT (lugar y valor original)
  u8 fmt_new;        // hubo un FRAMEBUFPIXFORMAT desde que se armo el ultimo fb
  u8 fmt_live;       // formato que tiene de verdad el GE (565, o el del juego en csrc_rt)
  int big_fb;        // 1 + indice del fb que se dibujo por ultima vez en el buffer grande (0: ninguno)
  int cur_fb;        // indice del fb actual en framebuf_addr (-1: ninguno)
  int offx, offy;    // desplazamiento del area visible en el fb actual (0 en render targets)
  u32 framebuf_count;
  u32 framebuf_next; // al llenarse la tabla se reemplaza el mas antiguo
} GeState;

static GeState state;

// --- Captura de un frame (L+R+SELECT): cada draw y cambio de estado relevante ---
static int capturing;
static u32 cap_last[0x100]; // ultimo valor logueado por comando, para no repetir
static u32 cap_hist[0x100]; // cuantos comandos de cada tipo hubo en el frame capturado
static struct { u32 prim, vt, tex, flags, n, verts; } cd; // draws iguales seguidos se agrupan

static void capFlush(void) {
  if (cd.n)
    LOG("  draw prim=%lu vt=%06lx tex=%08lx%s%s%s%s%s%s x%lu (%lu vert)\n", cd.prim, cd.vt, cd.tex,
        cd.flags & 2 ? " 2D" : "", cd.flags & 1 ? " OMITIDO" : "", cd.flags & 4 ? " tex=RT" : "",
        cd.flags & 8 ? " en_RT" : "", cd.flags & 16 ? " tex=pantalla_reducida" : "",
        cd.flags & 32 ? " escala1" : "", cd.n, cd.verts);
  cd.n = cd.verts = 0;
}

static void capDraw(u32 prim, u32 count, int omit) { // omit: 1 omitido, 4 textura rt, 8 dibuja en rt, 16 pantalla reducida, 32 fb de copia
  u32 tex = state.ge_cmd[GE_CMD_TEXTUREMAPENABLE] & 1 ?
            ((state.ge_cmd[GE_CMD_TEXBUFWIDTH0] & 0x0f0000) << 8) | state.ge_cmd[GE_CMD_TEXADDR0] : 0;
  u32 flags = omit | (((state.vertex_type & GE_VTYPE_THROUGH_MASK) == GE_VTYPE_THROUGH) << 1);
  if (cd.n && (cd.prim != prim || cd.vt != state.vertex_type || cd.tex != tex || cd.flags != flags))
    capFlush();
  cd.prim = prim; cd.vt = state.vertex_type; cd.tex = tex; cd.flags = flags;
  cd.n++; cd.verts += count;
}

// Comandos de estado que interesan en la captura (nombre corto) o NULL
static const char *capName(u32 cmd) {
  switch (cmd) {
    case GE_CMD_FRAMEBUFPTR: return "fb_ptr";
    case GE_CMD_FRAMEBUFWIDTH: return "fb_ancho";
    case GE_CMD_FRAMEBUFPIXFORMAT: return "fb_fmt";
    case GE_CMD_VIEWPORTXSCALE: return "vp_xscale";
    case GE_CMD_VIEWPORTYSCALE: return "vp_yscale";
    case GE_CMD_VIEWPORTXCENTER: return "vp_xcentro";
    case GE_CMD_VIEWPORTYCENTER: return "vp_ycentro";
    case GE_CMD_OFFSETX: return "offset_x";
    case GE_CMD_OFFSETY: return "offset_y";
    case GE_CMD_SCISSOR1: return "scissor1";
    case GE_CMD_SCISSOR2: return "scissor2";
    case GE_CMD_REGION1: return "region1";
    case GE_CMD_REGION2: return "region2";
    case GE_CMD_TEXADDR0: return "tex_ptr";
    case GE_CMD_TEXBUFWIDTH0: return "tex_ancho";
    case GE_CMD_TEXSIZE0: return "tex_tam";
    case GE_CMD_TEXFORMAT: return "tex_fmt";
    case GE_CMD_TRANSFERSTART: return "transfer";
    case GE_CMD_TEXTUREMAPENABLE: return "tex_on";
    case GE_CMD_TEXMAPMODE: return "tex_map";
    case GE_CMD_TEXMODE: return "tex_modo";
    case GE_CMD_TEXFUNC: return "tex_func";
    case GE_CMD_CLEARMODE: return "clear";
    case GE_CMD_ALPHABLENDENABLE: return "blend_on";
    case GE_CMD_BLENDMODE: return "blend";
    case GE_CMD_ALPHATESTENABLE: return "atest_on";
    case GE_CMD_ZTESTENABLE: return "ztest_on";
    case GE_CMD_ZWRITEDISABLE: return "zwrite_off";
    case GE_CMD_ZBUFPTR: return "z_ptr";
    default: return NULL;
  }
}

// Se llama al encolar una lista nueva. Solo se reinicia el recorrido (pila, offset):
// los registros del GE (BASE, direcciones de vertices, framebuffer, texturas...) se
// mantienen entre listas igual que en el hardware. Antes se borraba todo, y una lista
// que dibujaba sin volver a fijar BASE o VADDR hacia que se leyera/parcheara memoria
// equivocada (posible causa de glitches y cuelgues).
static int nrec; // sublistas que se estan anotando (ver wcRecord)
static void csrcNewList(void);
void resetGeState() {
  csrcNewList();
  nrec = 0;
  state.offset = 0;
  state.curr_stack = 0;
  state.has_draws = 0;
}

int push(StackEntry *data) {
  if (state.curr_stack < (sizeof(state.stack) / sizeof(StackEntry))) {
    memcpy(&state.stack[state.curr_stack++], data, sizeof(StackEntry));
    return 0;
  }

  st.stack_full++;
  LOG_ONCE_CAP(0xE1000000, "pila de display list llena (CALL ignorado)\n");
  return -1;
}

StackEntry *pop() {
  if (state.curr_stack > 0)
    return &state.stack[--state.curr_stack];
  return NULL;
}

int findFramebuf(u32 framebuf) {
  int i;
  for (i = 0; i < state.framebuf_count; i++) {
    // Some textures point to within the framebuf.
    // If we want to be correct we should record the pixelsize, linewidth and height.
    if (framebuf >= state.framebuf_addr[i] && framebuf < (state.framebuf_addr[i] + 0x400)) {
      return i;
    }
  }

  return -1;
}

int insertFramebuf(u32 framebuf) {
  int res = findFramebuf(framebuf);
  if (res >= 0)
    return res;

  int n = sizeof(state.framebuf_addr) / sizeof(u32);
  rt_gen++;
  if (state.framebuf_count < n) {
    state.fb_tx[state.framebuf_count] = state.fb_shown[state.framebuf_count] = state.fb_layer[state.framebuf_count] = 0;
    state.fb_swap[state.framebuf_count] = 0;
    state.fb_csrc[state.framebuf_count] = state.fb_cfail[state.framebuf_count] = 0;
    state.framebuf_addr[state.framebuf_count] = framebuf;
    return state.framebuf_count++;
  }

  LOG_ONCE(0xE2000000, "tabla de framebuffers llena, se reemplaza el mas antiguo\n");
  int i = state.framebuf_next++ % n;
  state.framebuf_addr[i] = framebuf;
  state.fb_tx[i] = state.fb_shown[i] = state.fb_layer[i] = state.fb_csrc[i] = state.fb_cfail[i] = state.fb_swap[i] = 0;
  if (state.big_fb == i + 1)
    state.big_fb = 0;
  return i;
}

// 1 si addr cae dentro de algun framebuffer conocido (480x272 a 32 bpp como maximo)
int inFramebuf(u32 addr) {
  int i, best = 0;
  u32 bo = 512 * 272 * 4;
  for (i = 0; i < state.framebuf_count; i++) // el mas cercano por debajo: Silent Hill tiene un fb en
    if (addr >= state.framebuf_addr[i] && addr - state.framebuf_addr[i] < bo) { // 0a000000 y otro en 0a04c000
      bo = addr - state.framebuf_addr[i];
      best = i + 1;
    }
  return best; // indice + 1 (0 = no)
}

static inline int wideScreen(u32 pitch) { return pitch == 1024 && opt.wide1024; }

// ---------------------------------------------------------------------------
// Render targets (render a textura)
//
// Muchos juegos dibujan en buffers chicos (reflejos, sombras, desenfoque) y luego los
// usan como textura. La VRAM baja (0-2 MB) esta ocupada por el buffer de 960x544 y su
// profundidad, asi que estos buffers se dibujan al doble en la zona 2-4 MB, que existe
// en Adrenaline aunque el sistema diga que hay 2 MB (ver probeVram). Si la prueba de
// VRAM falla, o no hay espacio, se vuelve al comportamiento anterior: se omiten.
// ---------------------------------------------------------------------------
// En la Vita (dev30, formulario de SH): lo que el GE DIBUJA en los 2 MB altos queda con las
// direcciones permutadas (bloques de 16 px y bandas de 16 filas) al leerlo despues con una
// copia o como textura; las copias y la CPU si los ven en orden. Si probeVram lo detecta
// (vram_swz), arriba va solo el z-buffer (el GE lo escribe y lo lee por el mismo camino) y
// abajo, despues del buffer grande, el buffer temporal y los render targets que se leen.
// Los fb de 1024 (pantallas que se muestran, nunca se leen bien) van arriba como antes.
static int vram_swz;
#define RT_BASE (vram_swz ? 0x04100000 : 0x04200000)
#define RT_END  (vram_swz ? 0x04200000 : 0x04400000)
#define RT_HI_BASE 0x042FF000 // (con vram_swz) despues del z-buffer arriba
static u32 rt_hi_next = RT_HI_BASE;
#define RT_MAX  8
// Los render targets se dibujan por defecto a escala 1 (tamano original). A escala 2 el
// limite de 4096 px del GE descarta la geometria cercana a la camara, y el mapa de
// reflejos de Ridge Racer quedaba solo con el color de fondo (reflejos cyan). Se puede
// cambiar a 2 desde el menu (se vuelven a reservar).
#define RT_SCALE opt.rt_scale

typedef struct RenderTarget {
  u32 addr, pitch;   // framebuffer del juego (direccion en VRAM falsa) y ancho de linea
  u32 vram, w, h;    // donde quedo en VRAM real, ya al doble
  u32 incomplete;    // se omitio algo al dibujarlo: su contenido no sirve como textura
  u32 frame;         // ultimo frame en que se uso como destino
  u32 draws, reads;  // en el periodo de estadisticas: draws hacia el y draws que lo leen
  u32 sc, fmt;       // escala (RT_SCALE al reservarlo; 1 en el fb de copia) y formato de pixel
  u32 hi;            // en la VRAM alta con vram_swz: no sirve como textura
} RenderTarget;

static RenderTarget rt[RT_MAX];
#define SCALE() (state.cur_rt ? (int)state.cur_rt->sc : 2) // escala del destino actual
static int rt_count, rt_enabled;
static u32 rt_next = 0x04200000; // RT_BASE sin vram_swz (probeVram lo ajusta)

// Fb del que el juego copia una parte a una textura (Silent Hill dibuja el formulario en
// el fb de la pantalla, lo copia a la RAM y despues dibuja la escena en el mismo fb). En
// el buffer grande (565, sin alfa) la copia salia sin transparencia y con texto cortado:
// ese tramo se dibuja como un render target a escala 1, en el formato del juego, en el
// buffer temporal; la copia se toma de ahi y luego se vuelve al buffer grande.
static RenderTarget csrc_rt;
static int csrc_fb = -1, csrc_copied; // fb (indice) y si ya se copio en esta lista (draws del tramo: csrc_rt.draws)

// Termina un tramo del fb de copia que no llego a la copia: si tenia draws, no se vieron
static void csrcEnd(void) {
  if (state.cur_rt != &csrc_rt || !csrc_rt.draws || csrc_fb < 0)
    return;
  state.fb_cfail[csrc_fb]++;
  LOG_ONCE_CAP(0x7C200000 ^ csrc_rt.addr, "fb %08lx: se dibujo a escala 1 pero no hubo copia (%d draws perdidos, fallo %d de 3)\n",
               csrc_rt.addr, (int)csrc_rt.draws, state.fb_cfail[csrc_fb]);
  csrc_rt.draws = 0;
}

// Lista nueva (Silent Hill: una por frame, formulario -> copia -> escena): un tramo sin
// copia quedo sin verse, y cada lista arma su propio fb
static void csrcNewList(void) {
  csrcEnd();
  csrc_copied = 0;
  if (state.cur_rt == &csrc_rt)
    state.cur_rt = NULL;
}

// Libera todos los render targets (al cambiar su escala o apagarlos desde el menu)
void rtReset(void) {
  memset(rt, 0, sizeof(rt));
  rt_count = 0;
  rt_next = RT_BASE;
  rt_hi_next = RT_HI_BASE;
  rt_gen++;
  copy_vram = 0; // estaba en la misma zona
  LOG("rt: se liberaron los render targets\n");
}

// Busca o reserva el render target para (addr, pitch)
static RenderTarget *rtGet(u32 addr, u32 pitch) {
  int i;
  for (i = 0; i < rt_count; i++)
    if (rt[i].addr == addr && rt[i].pitch == pitch)
      return &rt[i];

  // Alto desconocido: los de 256 o mas se asumen de pantalla (272), los chicos cuadrados
  u32 w = pitch * RT_SCALE, h = (pitch >= 256 ? 272 : pitch) * RT_SCALE, size = (w * h * 2 + 0x1fff) & ~0x1fff;
  if (vram_swz && pitch >= 1024 && rt_count < RT_MAX && rt_hi_next + size <= 0x04400000) { // pantalla de 1024: arriba
    RenderTarget *r = &rt[rt_count++];
    memset(r, 0, sizeof(*r));
    r->addr = addr; r->pitch = pitch; r->vram = rt_hi_next; r->w = w; r->h = h;
    r->sc = RT_SCALE; r->fmt = PIXELFORMAT; r->hi = 1;
    rt_hi_next += size;
    rt_gen++;
    LOG("rt %d: fb %08lx pitch=%lu -> vram alta %08lx %lux%lu (no se puede leer como textura)\n", rt_count - 1, addr, pitch, r->vram, w, h);
    return r;
  }
  if (rt_count >= RT_MAX || rt_next + size > RT_END) {
    st.rt_fail++;
    LOG_ONCE_CAP(addr ^ 0xC1000000, "rt: SIN ESPACIO para fb %08lx pitch=%lu (%lu KB, libres %lu KB, %d/%d usados) -> se omite\n",
             addr, pitch, size / 1024, (RT_END - rt_next) / 1024, rt_count, RT_MAX);
    return NULL;
  }
  RenderTarget *r = &rt[rt_count++];
  r->addr = addr; r->pitch = pitch; r->vram = rt_next; r->w = w; r->h = h;
  r->incomplete = 0; r->sc = RT_SCALE; r->fmt = PIXELFORMAT; r->hi = 0;
  rt_next += size;
  rt_gen++;
  LOG("rt %d: fb %08lx pitch=%lu -> vram %08lx %lux%lu (%lu KB), libres %lu KB\n",
      rt_count - 1, addr, pitch, r->vram, w, h, size / 1024, (RT_END - rt_next) / 1024);
  return r;
}

// Render target cuya direccion coincide exactamente con una textura. No se buscan
// texturas "dentro" del buffer: el alto real es desconocido y en Ridge Racer un render
// target de 256 px comparte zona con texturas normales de la pista, que se omitian.
static RenderTarget *rtFindTex(u32 tex) {
  int i;
  for (i = rt_count - 1; i >= 0; i--)
    if (rt[i].addr == tex)
      return &rt[i];
  return NULL;
}

// Reescribe un comando visto antes (puede estar en otra parte de la lista) solo si sigue
// siendo el mismo comando original; si la lista ya se reutilizo para otra cosa, no se toca.
static int canRepatch(u32 *p, u32 orig) {
  return p && validAddr((u32)p) && origOp(p) == orig;
}

// Redirige los comandos de textura al render target (direccion, ancho y tamano al doble,
// formato 565 porque los buffers se dibujan en 565). 1 si se pudo.
static int rtPatchTex(RenderTarget *r) {
  u32 fmt = state.ge_cmd[GE_CMD_TEXFORMAT] & 0xf;
  u32 sz = state.ge_cmd[GE_CMD_TEXSIZE0], lw = (sz & 0xf) + r->sc - 1, lh = ((sz >> 8) & 0xf) + r->sc - 1;
  u32 bw = (state.ge_cmd[GE_CMD_TEXBUFWIDTH0] & 0xffff) * r->sc;
  if (fmt > 3) {
    LOG_ONCE(r->addr ^ 0xC3000000, "rt: textura %08lx con formato %lu (paleta), no soportado -> se omite\n", r->addr, fmt);
    return 0;
  }
  if (lw > 9 || lh > 9 || bw > 1024) {
    LOG_ONCE(r->addr ^ 0xC4000000, "rt: textura %08lx de %dx%d no cabe escalada (max 512) -> se omite\n",
             r->addr, 1 << (lw - r->sc + 1), 1 << (lh - r->sc + 1));
    return 0;
  }
  if (!canRepatch(state.tex_p[0], state.tex_o[0]) || !canRepatch(state.tex_p[1], state.tex_o[1]) ||
      !canRepatch(state.tex_p[2], state.tex_o[2]) || (fmt && !canRepatch(state.tex_p[3], state.tex_o[3]))) {
    LOG_ONCE(r->addr ^ 0xC5000000, "rt: no se encontraron los comandos de textura de %08lx (lista reutilizada?) -> se omite\n", r->addr);
    return 0;
  }
  tex_patched_any = 1;
  setOp(state.tex_p[0], state.tex_o[0], (GE_CMD_TEXADDR0 << 24) | (r->vram & 0xffffff));
  setOp(state.tex_p[1], state.tex_o[1], (GE_CMD_TEXBUFWIDTH0 << 24) | ((r->vram >> 24) << 16) | bw);
  setOp(state.tex_p[2], state.tex_o[2], (GE_CMD_TEXSIZE0 << 24) | (lh << 8) | lw);
  if (fmt) {
    setOp(state.tex_p[3], state.tex_o[3], (GE_CMD_TEXFORMAT << 24) | 0);
    LOG_ONCE(r->addr ^ 0xC6000000, "rt: textura %08lx formato %lu -> 565 (si otras texturas cambian de color, es esto)\n", r->addr, fmt);
  }
  LOG_ONCE(r->addr ^ 0xC7000000, "rt: textura %08lx -> vram %08lx %dx%d ancho %lu\n", r->addr, r->vram, 1 << lw, 1 << lh, bw);
  return 1;
}

// Decide que hacer con la textura del draw: 0 normal, 1 omitir el draw,
// 2 textura redirigida a un render target (en 2D hay que duplicar sus coordenadas),
// 3 textura de la VRAM real llevada a la falsa, 4 el fb actual, 5 la pantalla leida desde
// un render target (txDrawFromScreen)
static struct { u32 on, addr, bw, fb, gen; void *cur; int res; } tcache;

static int texCheckCalc(void);
// fb en intercambio; pase en curso; ya hecho en esta lista; flips con el pase en curso; lista de la entrada
static int swap_fb = -1, swap_on, swap_done, swap_flips, swap_list;
int texCheck(void) {
  u32 on = state.ge_cmd[GE_CMD_TEXTUREMAPENABLE] & 1;
  if (tcache.on == on && tcache.addr == state.ge_cmd[GE_CMD_TEXADDR0] && tcache.bw == state.ge_cmd[GE_CMD_TEXBUFWIDTH0] &&
      tcache.fb == state.framebuf && tcache.cur == state.cur_rt && tcache.gen == rt_gen && tcache.res != 2 && tcache.res != 3 && tcache.res != 5 && tcache.res != 6)
    return tcache.res;
  tcache.on = on; tcache.addr = state.ge_cmd[GE_CMD_TEXADDR0]; tcache.bw = state.ge_cmd[GE_CMD_TEXBUFWIDTH0];
  tcache.fb = state.framebuf; tcache.cur = state.cur_rt; tcache.gen = rt_gen;
  return tcache.res = texCheckCalc();
}

static int texCheckCalc(void) {
  if (!(state.ge_cmd[GE_CMD_TEXTUREMAPENABLE] & 1))
    return 0;
  u32 raw_tex = ((state.ge_cmd[GE_CMD_TEXBUFWIDTH0] & 0x0f0000) << 8) | (state.ge_cmd[GE_CMD_TEXADDR0] & 0xffffff);
  // Silent Hill lee su fb con la direccion de la VRAM real (0x04...) y no la que le da el
  // plugin (0x0A...): para reconocerlo se mira igual que la VRAM falsa
  u32 tex = toFake(raw_tex);
  vramUse(tex);

  RenderTarget *r = rt_enabled && opt.rt_on ? rtFindTex(tex) : NULL;
  int lf = findFramebuf(tex);
  if (r && lf >= 0 && (r->pitch == 512 || r->pitch == 480) && !(opt.layers && state.fb_layer[lf]))
    r = NULL; // fb de pantalla que fue intermedio (o la opcion se apago): su render target ya no se usa
  if (r) {
    // Un fb que se creyo intermedio y se pega en el fb mostrado (el de 1024 de Silent Hill)
    // es la pantalla: vuelve al buffer grande
    int cf = findFramebuf(state.framebuf);
    if (lf >= 0 && state.fb_layer[lf] && state.cur_rt && r != state.cur_rt && cf >= 0 && state.fb_shown[cf]) {
      state.fb_layer[lf] = 0;
      state.fb_shown[lf] = 1;
      rt_gen++;
      LOG("fb %08lx: se pega en la pantalla -> no es intermedio\n", tex);
      return 1;
    }
    if (r == state.cur_rt) // se lee a si mismo (efectos de acumulacion): igual que en la PSP
      LOG_ONCE(tex ^ 0xC8000000, "rt: el rt %08lx se lee a si mismo -> se permite\n", tex);
    else if (r->hi) {
      LOG_ONCE_CAP(tex ^ 0xC9100000, "rt: %08lx esta en la VRAM alta (se leeria desordenado) -> se omite el draw\n", tex);
      return 1;
    } else if (r->incomplete) {
      st.rt_incomplete++;
      LOG_ONCE_CAP(tex ^ 0xC9000000, "rt: %08lx quedo incompleto (se omitieron draws al dibujarlo) -> no se usa como textura\n", tex);
      return 1;
    }
    if (!rtPatchTex(r)) {
      st.rt_fail++;
      return 1;
    }
    st.rt_tex++;
    r->reads++;
    return 2;
  }

  // Textura desde un framebuffer de pantalla (p.ej. el frame anterior): no hay copia
  int f = findFramebuf(tex);
  if (tex == state.framebuf)
    return 4; // el fb actual: se dibuja igual, pero depende del fb (las sublistas no se guardan)
  if (f < 0) {
    // Textura en la VRAM real que no es un fb (Silent Hill): se lee de la VRAM falsa, donde
    // el plugin dejo las copias del juego. En la real estan los buffers del plugin.
    if (raw_tex != tex && opt.vram_fix && !passive && canRepatch(state.tex_p[1], state.tex_o[1])) {
      setOp(state.tex_p[1], state.tex_o[1], (GE_CMD_TEXBUFWIDTH0 << 24) | ((tex >> 8) & 0x0f0000) | (state.tex_o[1] & 0xffff));
      tex_patched_any = 1;
      st.vram_fix++;
      LOG_ONCE_CAP(raw_tex ^ 0x7B000000, "textura en VRAM real %08lx (no es un fb) -> se lee de la VRAM falsa\n", raw_tex);
      return 3;
    }
    return 0;
  }
  // Fb llenado con copias (TRANSFER): video de Silent Hill decodificado por CPU y copiado
  // a un fb, que luego se dibuja como textura. Esa copia si esta en la VRAM falsa: sirve.
  if (state.fb_tx[f] && !passive) {
    LOG_ONCE(tex ^ 0x7D000000, "textura desde fb %08lx llenado por copia (video?) -> se dibuja\n", tex);
    return 0;
  }
  // Silent Hill dibuja formularios/menus en un fb de 512 que nunca muestra y luego lo pega
  // en la pantalla: como iba al mismo buffer grande que la pantalla, se veia vacio. Un fb
  // asi (nunca mostrado, leido mientras se dibuja en la pantalla) pasa a render target.
  // Un fb que se copia al fb mostrado (render target, como el de 1024 de Silent Hill) es
  // la pantalla de verdad: cuenta como mostrado.
  if (swap_on && f == swap_fb && !state.cur_rt && !state.ignore_framebuf)
    return 6; // la mezcla del pase con la pantalla: ver swapComposite
  int cf = findFramebuf(state.framebuf);
  if ((state.cur_rt || state.ignore_framebuf) && cf >= 0 && state.fb_shown[cf])
    state.fb_shown[f] = 1;
  if (opt.layers && !passive && !state.cur_rt && !state.ignore_framebuf && !state.fb_shown[f] && !state.fb_layer[f]) {
    state.fb_layer[f] = 1;
    rt_gen++;
    LOG("fb %08lx: intermedio (nunca se muestra y se usa como textura) -> render target desde ahora\n", tex);
    autoCapture();
  }
  // Un render target que lee la pantalla (en el buffer grande): se reduce en la lista. No si
  // el destino es el fb mostrado (el de 1024 de Silent Hill: la pantalla sale del buffer grande)
  if (state.cur_rt && state.cur_rt != &csrc_rt && opt.omit_fbtex == 1 && !passive && f + 1 == state.big_fb &&
      !(cf >= 0 && state.fb_shown[cf]) &&
      (state.ge_cmd[GE_CMD_TEXFORMAT] & 0xf) <= 3) {
    LOG_ONCE_CAP(tex ^ 0x7E100000, "rt %08lx lee la pantalla %08lx como textura -> se reduce el buffer grande antes del draw\n",
                 state.cur_rt->addr, tex);
    return 5;
  }
  fbtex_seen = 1;
  LOG_ONCE_CAP(tex ^ 0x7E000000, "textura desde fb %08lx -> %s\n", tex,
           opt.omit_fbtex == 1 ? "draws omitidos" : opt.omit_fbtex == 2 ? "se lee la copia reducida del frame" : "se lee la memoria tal cual");
  return opt.omit_fbtex == 1;
}

// Memoria de vertices 2D ya duplicados: direccion, cantidad y hash de las posiciones
// tal como quedaron. Si al volver a verlos el hash coincide, ya estan duplicados.
// Antes se marcaba en bytes de relleno del vertice, y los formatos sin relleno
// (p.ej. los sprites que limpian la pantalla) se duplicaban de nuevo cada frame.
// Con 512 entradas, en juegos con muchos draws 2D (Gran Turismo) dos buffers chocaban:
// el expulsado se volvia a duplicar al verlo otra vez (sprites estirados o corridos
// que aparecen "con el tiempo"). Ahora va en RAM libre con 32768 entradas (384 KB).
#define VMEMO_ADDR 0x0A700000
#define VMEMO_SIZE 32768
typedef struct { u32 addr, n, hash, hits; } VMemo; // 16 bytes: 512 KB. hits bit 31: quedaron a escala 1
#define VM_NATIVE 0x80000000
static VMemo *vmemo = (VMemo *)VMEMO_ADDR;

static u32 vertsHash(u32 start, u32 n, int size, int pos_off, int pos_size) {
  u32 h = n, i;
  for (i = 0; i < n; i++, start += size) {
    u32 p = start + pos_off;
    h = h * 31 + (pos_size == 2 ? (*(u16 *)p | (*(u16 *)(p + 2) << 16)) : *(u32 *)p ^ *(u32 *)(p + 4));
  }
  return h;
}

// 0: no se conocen, 1: ya duplicados, 2: conocidos a escala 1 (sin duplicar)
static int vertsPatched(u32 start, u32 n, int size, int pos_off, int pos_size) {
  VMemo *e = &vmemo[(start >> 2) & (VMEMO_SIZE - 1)];
  if (e->addr == start && e->n == n && e->hash == vertsHash(start, n, size, pos_off, pos_size)) {
    e->hits++;
    return e->hits & VM_NATIVE ? 2 : 1;
  }
  return 0;
}

static void vertsRemember(u32 start, u32 n, u32 hash, u32 native) {
  VMemo *e = &vmemo[(start >> 2) & (VMEMO_SIZE - 1)];
  // Choque solo si se expulsa un buffer estatico (ya visto de nuevo): ese se duplicaria otra
  // vez. Los buffers dinamicos (Gran Turismo: direccion nueva cada frame) no cuentan.
  if (e->addr && e->addr != start && (e->hits & ~VM_NATIVE))
    st.vevict++;
  e->addr = start; e->n = n; e->hash = hash; e->hits = native;
}

// Deshace la duplicacion de posiciones 2D (480/960 y 272/544 vuelven a 480 y 272)
static void vertsHalve(u32 va, u32 n, int size, int pos_off, int pos_size) {
  union { float f; u32 i; } t;
  u32 i, j;
  for (i = 0; i < n; i++, va += size)
    for (j = 0; j < 2; j++) {
      u32 addr = va + pos_off + j * pos_size;
      int d = j ? state.offy : state.offx;
      if (pos_size == 2) {
        short v = *(short *)addr;
        *(short *)addr = (v == 960 ? 480 : v == 544 ? 272 : v / 2) + d;
      } else {
        t.i = *(u32 *)addr;
        t.f = (t.f == 960 ? 480 : t.f == 544 ? 272 : t.f / 2) + d;
        *(u32 *)addr = t.i;
      }
    }
}

void AdvanceVerts(int count, int vertex_size) {
  if ((state.vertex_type & GE_VTYPE_IDX_MASK) != GE_VTYPE_IDX_NONE) {
    int index_shift = ((state.vertex_type & GE_VTYPE_IDX_MASK) >> GE_VTYPE_IDX_SHIFT) - 1;
    state.index_addr += count << index_shift;
  } else {
    state.vertex_addr += count * vertex_size;
  }
}

// Scissor/region escalados (x2, o la escala del render target) con el desplazamiento
static u32 scPatch(u32 cmd, u32 data) {
  int sc = SCALE(), end = cmd == GE_CMD_SCISSOR2 || cmd == GE_CMD_REGION2;
  int x = ((int)(data & 0x3ff) - state.offx) * sc + (end ? sc - 1 : 0);
  int y = ((int)((data >> 10) & 0x3ff) - state.offy) * sc + (end ? sc - 1 : 0);
  x = x < 0 ? 0 : x > 1023 ? 1023 : x;
  y = y < 0 ? 0 : y > 1023 ? 1023 : y;
  return (y << 10) | x;
}

// Escribe en l una lista que reduce el buffer grande (960x544) a 480x272 en el buffer
// temporal (formato fmt), con filtro lineal. Devuelve el final.
// En el hardware, 4 sprites de 240 px de ancho (480 texeles) salian con el texto corrido
// y letras cambiadas de lugar (formulario de Silent Hill); en PPSSPP la misma lista dibuja
// bien. Como en txUpscale: franjas de 32 px (64 texeles) y TEXSYNC antes de leer lo que se
// acaba de dibujar. Los vertices son siempre los mismos: se arman una vez (DOWN_VERTS).
#define DOWN_VERTS (RENDER_LIST + 0x5000) // 4 cuartos x 8 franjas x 2 vertices x 10 bytes
static u32 *emitDownscale(u32 *l, int fmt) {
  static int ready;
  short *v = (short *)(DOWN_VERTS | 0x40000000);
  int i, x;
  if (!ready) {
    for (i = 0; i < 4; i++)
      for (x = 0; x < 240; x += 32, v += 10) {
        int tx = (i & 1) * 240, ty = (i >> 1) * 136, x2 = x + 32 < 240 ? x + 32 : 240;
        short q[10] = { x * 2, 0, tx + x, ty, 0, x2 * 2, 272, tx + x2, ty + 136, 0 };
        memcpy(v, q, sizeof(q));
      }
    ready = 1;
  }
  #define C(c, d) (*l++ = ((c) << 24) | ((d) & 0xffffff))
  C(GE_CMD_TEXSYNC, 0); // lo dibujado antes en el buffer grande ya esta en la VRAM
  C(GE_CMD_FRAMEBUFPIXFORMAT, fmt);
  C(GE_CMD_FRAMEBUFPTR, copy_vram); C(GE_CMD_FRAMEBUFWIDTH, ((copy_vram >> 24) << 16) | 512);
  C(GE_CMD_SCISSOR1, 0); C(GE_CMD_SCISSOR2, (271 << 10) | 479);
  C(GE_CMD_REGION1, 0); C(GE_CMD_REGION2, (271 << 10) | 479);
  static const u8 off[] = { GE_CMD_ALPHATESTENABLE, GE_CMD_ZTESTENABLE, GE_CMD_ALPHABLENDENABLE, GE_CMD_STENCILTESTENABLE,
    GE_CMD_LOGICOPENABLE, GE_CMD_COLORTESTENABLE, GE_CMD_DITHERENABLE, GE_CMD_MASKRGB, GE_CMD_MASKALPHA,
    GE_CMD_TEXMAPMODE, GE_CMD_TEXMODE, GE_CMD_TEXFORMAT, GE_CMD_CLEARMODE };
  for (i = 0; i < (int)sizeof(off); i++)
    C(off[i], 0);
  C(GE_CMD_ZWRITEDISABLE, 1);
  // El alfa del destino solo se escribe con el stencil (sin el queda lo que habia: en
  // dev32 eran restos del z-buffer y el formulario de SH salia semitransparente). El
  // buffer grande es 565, sin alfa: se deja opaco, como lo dibuja el juego.
  C(GE_CMD_STENCILTESTENABLE, 1);
  C(GE_CMD_STENCILTEST, (0xff << 16) | (0xff << 8) | GE_COMP_ALWAYS);
  C(GE_CMD_STENCILOP, (GE_STENCILOP_REPLACE << 16) | (GE_STENCILOP_REPLACE << 8) | GE_STENCILOP_REPLACE);
  C(GE_CMD_TEXTUREMAPENABLE, 1);
  C(GE_CMD_TEXSIZE0, (9 << 8) | 9);                 // 512x512 (se usan 480x272)
  C(GE_CMD_TEXFILTER, (1 << 8) | 1);                // lineal: promedia los 4 pixeles
  C(GE_CMD_TEXWRAP, (1 << 8) | 1);
  C(GE_CMD_TEXFUNC, (1 << 8) | 3);                  // reemplazar, alfa de la textura (opaco)
  C(GE_CMD_VERTEXTYPE, GE_VTYPE_THROUGH | GE_VTYPE_TC_16BIT | GE_VTYPE_POS_16BIT);
  C(GE_CMD_BASE, (DOWN_VERTS >> 8) & 0x0f0000); C(GE_CMD_OFFSETADDR, 0); C(GE_CMD_VADDR, DOWN_VERTS);
  for (i = 0; i < 4; i++) { // 4 cuartos de 480x272: la textura del GE es de 512 como maximo
    u32 src = VRAM_DRAW_BUFFER_OFFSET + ((i >> 1) * 272 * PITCH + (i & 1) * 480) * 2;
    C(GE_CMD_TEXADDR0, src); C(GE_CMD_TEXBUFWIDTH0, ((src >> 24) << 16) | PITCH);
    C(GE_CMD_TEXFLUSH, 0);
    for (x = 0; x < 240; x += 32)
      C(GE_CMD_PRIM, (GE_PRIM_RECTANGLES << 16) | 2); // el VADDR avanza solo
  }
  #undef C
  return l;
}

// Copia grande (video) a la pantalla: en vez de escalar con la CPU al final del frame lo
// que se copio (y perder lo que el juego dibuja encima despues, como "PRESS START" en
// Silent Hill), se reemplaza la copia por un CALL a una lista del plugin que hace la copia
// original (la VRAM falsa queda igual) y ademas dibuja la imagen al doble en el buffer
// grande, en el mismo lugar de la lista. Luego deja el estado del GE como estaba.
// Las 8 palabras de la copia (sceGuCopyImage) se reescriben como BASE + CALL + NOPs.
#define TX_RING (RENDER_LIST + 0x8000) // 64 listas de 1 KB
#define TX_SLOT 1024

// Buffer temporal de 512x272 a 32 bpp en la VRAM alta (zona de los render targets), para
// la copia reducida del frame y para las copias escaladas. Los dos usos van en orden en
// el GE, asi que comparten el buffer.
static int copyVram(void) {
  if (copy_vram)
    return 1;
  if (vram_swz) { // VRAM de 2 MB: el unico lugar es la del z-buffer (se pisa mientras se usa)
    copy_vram = 0x04100000;
    LOG("buffer temporal en %08lx: comparte la VRAM del z-buffer (la VRAM real es de 2 MB)\n", copy_vram);
    return 1;
  }
  if (!rt_enabled || rt_next + 0x88000 > RT_END) {
    LOG_ONCE(0xC2000000, "buffer temporal: sin VRAM libre -> sin copia reducida ni copias escaladas con el GE\n");
    return 0;
  }
  copy_vram = rt_next;
  rt_next += 0x88000;
  LOG("buffer temporal en %08lx (512x272, 32 bpp)\n", copy_vram);
  return 1;
}
static const u8 tx_seq[7] = { GE_CMD_TRANSFERSRC, GE_CMD_TRANSFERSRCW, GE_CMD_TRANSFERSRCPOS, GE_CMD_TRANSFERDST,
                              GE_CMD_TRANSFERDSTW, GE_CMD_TRANSFERDSTPOS, GE_CMD_TRANSFERSIZE };
static const u8 tx_saved[] = { GE_CMD_TEXTUREMAPENABLE, GE_CMD_TEXMODE, GE_CMD_TEXFORMAT, GE_CMD_TEXADDR0,
  GE_CMD_TEXBUFWIDTH0, GE_CMD_TEXSIZE0, GE_CMD_TEXFILTER, GE_CMD_TEXWRAP, GE_CMD_TEXFUNC, GE_CMD_ALPHABLENDENABLE,
  GE_CMD_ALPHATESTENABLE, GE_CMD_ZTESTENABLE, GE_CMD_STENCILTESTENABLE, GE_CMD_LOGICOPENABLE, GE_CMD_COLORTESTENABLE,
  GE_CMD_ZWRITEDISABLE, GE_CMD_MASKRGB, GE_CMD_MASKALPHA, GE_CMD_VERTEXTYPE, GE_CMD_CLEARMODE, GE_CMD_TEXMAPMODE,
  GE_CMD_STENCILTEST, GE_CMD_STENCILOP };

static int lg2(u32 v) { int n = 0; while ((1u << n) < v) n++; return n; }

// La copia que termina en list (TRANSFERSTART) se puede reescribir como BASE + CALL: hace
// falta que la palabra anterior sea un registro de copia (la lista del plugin los vuelve a
// fijar todos). Antes se exigia el orden de sceGuCopyImage y Silent Hill usa otro.
static int txIsReg(u32 op) {
  u32 c = op >> 24, k;
  for (k = 0; k < sizeof(tx_seq); k++)
    if (c == tx_seq[k])
      return 1;
  return 0;
}
// Silent Hill pone otros comandos entre el ultimo registro y el TRANSFERSTART: se busca el
// registro hasta 8 palabras antes; lo que haya entre medio no debe depender de BASE
// (direcciones, saltos), porque se ejecutara con el BASE del plugin. Devuelve la palabra.
static u32 *txIsCopy(u32 *list, int temp) {
  int k;
  if (passive || ((u32)list >= TX_RING && (u32)list < TX_RING + 64 * TX_SLOT) || (temp && !copyVram()))
    return NULL;
  for (k = 1; k <= 8 && validAddr((u32)(list - k)); k++) {
    u32 c = origOp(list - k) >> 24;
    if (txIsReg(origOp(list - k)))
      return list - k;
    if (c == GE_CMD_VADDR || c == GE_CMD_IADDR || c == GE_CMD_BASE || c == GE_CMD_CALL || c == GE_CMD_JUMP ||
        c == GE_CMD_BJUMP || c == GE_CMD_ORIGIN || c == GE_CMD_OFFSETADDR || c == GE_CMD_RET || c == GE_CMD_END ||
        c == GE_CMD_TRANSFERSTART)
      break;
  }
  return NULL;
}

// Palabra del registro cmd de esta copia (hasta 16 palabras antes), o NULL
static u32 *txFind(u32 *list, u32 cmd) {
  int k;
  for (k = 1; k <= 16 && validAddr((u32)(list - k)); k++) {
    u32 c = origOp(list - k) >> 24;
    if (c == cmd)
      return list - k;
    if (c == GE_CMD_TRANSFERSTART)
      break;
  }
  return NULL;
}

// La copia original del juego, con los valores de sus registros
static u32 *txEmitOrig(u32 *l, u32 start) {
  u32 k;
  for (k = 0; k < sizeof(tx_seq); k++)
    *l++ = (tx_seq[k] << 24) | state.ge_cmd[tx_seq[k]];
  *l++ = (GE_CMD_TRANSFERSTART << 24) | start;
  return l;
}

// Una lista del ring alcanzable con CALL desde la lista del juego (con su OFFSETADDR), o 0
static u32 txSlot(void) {
  static int slot;
  u32 sl = TX_RING + (slot++ & 63) * TX_SLOT;
  return (FAKE_VRAM | ((sl - state.offset) & 0xffffff)) + state.offset == sl ? sl : 0;
}

// Reescribe el ultimo registro (reg) y el TRANSFERSTART como BASE + CALL a la lista sl
static void txCall(u32 *list, u32 *reg, u32 sl) {
  setOp(reg, origOp(reg), (GE_CMD_BASE << 24) | ((FAKE_VRAM >> 8) & 0x0f0000));
  setOp(list, origOp(list), (GE_CMD_CALL << 24) | ((sl - state.offset) & 0xffffff));
  tx_patched_any = 1;
  dumpTxList(sl);
}

// Viewport escalado al destino actual (x2 en el buffer grande, la escala del render target)
static u32 vpPatch(u32 cmd, u32 data) {
  union { float f; u32 i; } t;
  t.i = data << 8;
  if (cmd == GE_CMD_VIEWPORTXCENTER)
    t.f = (2048 - WIDTH / 2) + SCALE() * (t.f - game_offx - state.offx);
  else if (cmd == GE_CMD_VIEWPORTYCENTER)
    t.f = (2048 - HEIGHT / 2) + SCALE() * (t.f - game_offy - state.offy);
  else
    t.f *= SCALE();
  return t.i >> 8;
}

// Deja el estado del GE como lo tenia el juego (ya parcheado) despues de una lista del plugin
static u32 *txRestoreState(u32 *l, int fb) {
  u32 k;
  #define C(c, d) (*l++ = ((c) << 24) | ((d) & 0xffffff))
  for (k = 0; k < sizeof(tx_saved); k++)
    C(tx_saved[k], state.ge_cmd[tx_saved[k]]);
  C(GE_CMD_TEXFLUSH, 0);
  if (fb) { // se dibujo en el buffer temporal: framebuffer, scissor y region como estaban
    RenderTarget *r = state.cur_rt;
    C(GE_CMD_FRAMEBUFPIXFORMAT, r ? r->fmt : PIXELFORMAT); C(GE_CMD_DITHERENABLE, state.ge_cmd[GE_CMD_DITHERENABLE]);
    C(GE_CMD_FRAMEBUFPTR, r ? r->vram : VRAM_DRAW_BUFFER_OFFSET);
    C(GE_CMD_FRAMEBUFWIDTH, r ? ((r->vram >> 24) << 16) | r->w : ((VRAM_DRAW_BUFFER_OFFSET >> 24) << 16) | PITCH);
    C(GE_CMD_SCISSOR1, scPatch(GE_CMD_SCISSOR1, state.ge_cmd[GE_CMD_SCISSOR1]));
    C(GE_CMD_SCISSOR2, scPatch(GE_CMD_SCISSOR2, state.ge_cmd[GE_CMD_SCISSOR2]));
    C(GE_CMD_REGION1, scPatch(GE_CMD_REGION1, state.ge_cmd[GE_CMD_REGION1]));
    C(GE_CMD_REGION2, scPatch(GE_CMD_REGION2, state.ge_cmd[GE_CMD_REGION2]));
    if (r == NULL && state.fmt_live != PIXELFORMAT) // se sale del fb de copia
      state.fmt_live = PIXELFORMAT;
    static const u8 vp[] = { GE_CMD_VIEWPORTXSCALE, GE_CMD_VIEWPORTYSCALE, GE_CMD_VIEWPORTXCENTER, GE_CMD_VIEWPORTYCENTER };
    for (k = 0; k < sizeof(vp); k++) // por si se cambio de escala (fb de copia -> buffer grande)
      if (state.ge_cmd[vp[k]])
        C(vp[k], vpPatch(vp[k], state.ge_cmd[vp[k]]));
  }
  #undef C
  return l;
}
static u32 *txRestore(u32 *l, int fb) {
  #define C(c, d) (*l++ = ((c) << 24) | ((d) & 0xffffff))
  l = txRestoreState(l, fb);
  C(GE_CMD_BASE, (state.vertex_addr >> 8) & 0x0f0000); C(GE_CMD_VADDR, state.vertex_addr);
  C(GE_CMD_BASE, state.base >> 8);
  C(GE_CMD_RET, 0);
  #undef C
  return l;
}

// Copia DESDE la pantalla (Silent Hill dibuja el formulario/la pantalla del celular en su fb
// y lo copia a una textura): el juego lee su fb, que esta vacio porque se dibujo en el
// buffer grande. Se reemplaza por: reducir el buffer grande al temporal y copiar de ahi.
// Volcado: el GE copia un buffer de la VRAM a una instantanea en la RAM en este punto de la
// lista (para ver lo que habia a mitad del frame; ver dumpSnap)
static u32 *snapCopy(u32 *l, u32 src, u32 stride, u32 w, u32 h, int bpp, int fmt, const char *what) {
  u32 d = dumping ? dumpSnap(w, h, bpp, fmt, what) : 0;
  if (!d)
    return l;
  #define C(c, x) (*l++ = ((c) << 24) | ((x) & 0xffffff))
  C(GE_CMD_TRANSFERSRC, src); C(GE_CMD_TRANSFERSRCW, ((src >> 24) << 16) | stride); C(GE_CMD_TRANSFERSRCPOS, 0);
  C(GE_CMD_TRANSFERDST, d); C(GE_CMD_TRANSFERDSTW, ((d >> 24) << 16) | w); C(GE_CMD_TRANSFERDSTPOS, 0);
  C(GE_CMD_TRANSFERSIZE, ((h - 1) << 10) | (w - 1)); C(GE_CMD_TRANSFERSTART, bpp == 4);
  #undef C
  return l;
}

static int txFromScreen(u32 *list, u32 src, u32 dst, u32 w, u32 h, int bpp) {
  int f = inFramebuf(src) - 1;
  u32 sw = state.ge_cmd[GE_CMD_TRANSFERSRCW] & 0x7ff, sl = 0, *reg = NULL;
  if (f < 0 || (sw != 512 && sw != 480) || rtFindTex(state.framebuf_addr[f]) || state.fb_layer[f] || inFramebuf(dst))
    return 0;
  u32 off = (src - state.framebuf_addr[f]) / bpp, sp = state.ge_cmd[GE_CMD_TRANSFERSRCPOS];
  u32 x = off % sw + (sp & 0x3ff), y = off / sw + ((sp >> 10) & 0x3ff);
  const char *why = x + w > 480 || y + h > 272 ? "fuera de 480x272" : !(reg = txIsCopy(list, 1)) ? "no se puede reescribir"
                  : !(sl = txSlot()) ? "CALL inalcanzable" : NULL;
  if (why) {
    LOG_ONCE_CAP(0xEA000000 ^ src ^ dst, "copia desde la pantalla %lux%lu (%lu,%lu) -> %08lx NO reemplazada: %s (antes: %08lx %08lx %08lx)\n",
                 w, h, x, y, dst, why, origOp(list - 3), origOp(list - 2), origOp(list - 1));
    return 0;
  }
  u32 *l = (u32 *)(sl | 0x40000000);
  l = snapCopy(l, VRAM_DRAW_BUFFER_OFFSET, PITCH, WIDTH, HEIGHT, 2, 0, "pantalla antes de copia");
  l = emitDownscale(l, bpp == 4 ? 3 : state.ge_cmd[GE_CMD_FRAMEBUFPIXFORMAT] & 3);
  l = snapCopy(l, copy_vram, 512, 512, 272, bpp == 4 ? 4 : 2, bpp == 4 ? 3 : 0, "pantalla reducida");
  #define C(c, d) (*l++ = ((c) << 24) | ((d) & 0xffffff))
  C(GE_CMD_TRANSFERSRC, copy_vram); C(GE_CMD_TRANSFERSRCW, ((copy_vram >> 24) << 16) | 512);
  C(GE_CMD_TRANSFERSRCPOS, (y << 10) | x);
  C(GE_CMD_TRANSFERDST, dst); C(GE_CMD_TRANSFERDSTW, ((dst >> 8) & 0xff0000) | (state.ge_cmd[GE_CMD_TRANSFERDSTW] & 0xffff));
  C(GE_CMD_TRANSFERDSTPOS, state.ge_cmd[GE_CMD_TRANSFERDSTPOS]); C(GE_CMD_TRANSFERSIZE, state.ge_cmd[GE_CMD_TRANSFERSIZE]);
  C(GE_CMD_TRANSFERSTART, bpp == 4);
  #undef C
  txRestore(l, 1);
  txCall(list, reg, sl);
  st.tx_screen++;
  LOG_ONCE_CAP(0xE8000000 ^ src ^ dst, "copia desde la pantalla %lux%lu (%lu,%lu) -> %08lx: se toma del buffer grande reducido\n",
               w, h, x, y, dst);
  csrc_copied = 1; // lo que sigue en esta lista es la escena, no el formulario
  if (!state.fb_csrc[f] && state.fb_cfail[f] < 3) { // desde el proximo frame: a escala 1 (ver csrc_rt)
    state.fb_csrc[f] = 1;
    LOG("fb %08lx: el juego copia desde el -> hasta la copia se dibujara a escala 1 en su formato\n", state.framebuf_addr[f]);
  }
  return 1;
}

// Copia desde el fb de copia mientras se dibuja en el buffer temporal (csrc_rt): se toma de
// ahi (mismo lugar, escala 1) y la lista del plugin vuelve al buffer grande para lo que sigue
static int txFromCsrc(u32 *list, u32 src, u32 dst, int bpp) {
  if (state.cur_rt != &csrc_rt || csrc_fb < 0 || inFramebuf(src) - 1 != csrc_fb)
    return 0;
  u32 *reg = txIsCopy(list, 0), sl = reg ? txSlot() : 0;
  if (!sl || (bpp == 4) != (csrc_rt.fmt == 3)) {
    state.fb_cfail[csrc_fb] = 255;
    LOG_ONCE_CAP(0x7C300000 ^ src, "copia desde el fb de copia %08lx: no se puede reemplazar (%s) -> no se usa mas\n", src,
                 !reg ? "registros" : !sl ? "CALL inalcanzable" : "formato distinto");
    return 0;
  }
  u32 from = copy_vram + (src - csrc_rt.addr), *l = (u32 *)(sl | 0x40000000);
  l = snapCopy(l, copy_vram, 512, 512, 272, csrc_rt.fmt == 3 ? 4 : 2, csrc_rt.fmt, "fb de copia (escala 1)");
  #define C(c, d) (*l++ = ((c) << 24) | ((d) & 0xffffff))
  C(GE_CMD_TRANSFERSRC, from); C(GE_CMD_TRANSFERSRCW, ((from >> 24) << 16) | (state.ge_cmd[GE_CMD_TRANSFERSRCW] & 0xffff));
  C(GE_CMD_TRANSFERSRCPOS, state.ge_cmd[GE_CMD_TRANSFERSRCPOS]);
  C(GE_CMD_TRANSFERDST, dst); C(GE_CMD_TRANSFERDSTW, ((dst >> 8) & 0xff0000) | (state.ge_cmd[GE_CMD_TRANSFERDSTW] & 0xffff));
  C(GE_CMD_TRANSFERDSTPOS, state.ge_cmd[GE_CMD_TRANSFERDSTPOS]); C(GE_CMD_TRANSFERSIZE, state.ge_cmd[GE_CMD_TRANSFERSIZE]);
  C(GE_CMD_TRANSFERSTART, bpp == 4);
  #undef C
  csrc_copied = 1;
  state.cur_rt = NULL; // lo que sigue va al buffer grande, como la pantalla
  state.offx = opt.dx; state.offy = opt.dy;
  txRestore(l, 1);
  txCall(list, reg, sl);
  st.tx_screen++;
  LOG_ONCE_CAP(0x7C400000 ^ src ^ dst, "copia desde el fb de copia %08lx -> %08lx: se toma del buffer temporal (escala 1)\n", src, dst);
  return 1;
}

// Draw hacia un render target (fb intermedio) con la pantalla como textura (Silent Hill
// pasa la escena al fb del spinner/puerta): la pantalla esta en el buffer grande, asi que
// antes se omitia y el render target quedaba incompleto. Ahora el PRIM (y la palabra de
// antes) se reemplaza por BASE + CALL a una lista que reduce el buffer grande al temporal,
// dibuja con esa textura y deja la textura del juego como estaba.
static void primUnCall(u32 *list) {
  if (validAddr((u32)(list - 1)) && (list[-1] >> 24) == GE_CMD_BASE && origOp(list - 1) != list[-1]) {
    list[-1] = origOp(list - 1);
    if (dumping)
      dumpCmd((u32 *)((u32)(list - 1) | 1), list[-1]);
  }
}
static int txDrawFromScreen(u32 *list, u32 op) {
  u32 *reg = list - 1, sl, fmt = state.ge_cmd[GE_CMD_TEXFORMAT] & 0xf;
  if (!validAddr((u32)reg) || fmt > 3 || !copyVram())
    return 0;
  u32 c = origOp(reg) >> 24;
  if (c == GE_CMD_CALL || c == GE_CMD_JUMP || c == GE_CMD_BJUMP || c == GE_CMD_RET || c == GE_CMD_END || c == GE_CMD_FINISH ||
      c == GE_CMD_SIGNAL || c == GE_CMD_PRIM || c == GE_CMD_BEZIER || c == GE_CMD_SPLINE || c == GE_CMD_TRANSFERSTART ||
      c == GE_CMD_ORIGIN || !(sl = txSlot()))
    return 0;
  u32 prev = *reg, *l = (u32 *)(sl | 0x40000000); // con los parches que ya tenga
  l = emitDownscale(l, fmt);
  l = snapCopy(l, copy_vram, 512, 512, 272, fmt == 3 ? 4 : 2, fmt, "pantalla reducida (rt)");
  l = txRestoreState(l, 1);
  #define C(c, d) (*l++ = ((c) << 24) | ((d) & 0xffffff))
  C(GE_CMD_BASE, state.base >> 8);
  *l++ = prev; // la palabra que pasa a ser BASE, antes de cambiar la textura (puede ser de textura)
  C(GE_CMD_TEXADDR0, copy_vram); C(GE_CMD_TEXBUFWIDTH0, ((copy_vram >> 24) << 16) | 512);
  C(GE_CMD_TEXFLUSH, 0);
  C(GE_CMD_BASE, (state.vertex_addr >> 8) & 0x0f0000); C(GE_CMD_VADDR, state.vertex_addr);
  C(GE_CMD_BASE, state.base >> 8);
  *l++ = op;   // el draw (el GE avanza VADDR como siempre)
  C(GE_CMD_TEXADDR0, state.ge_cmd[GE_CMD_TEXADDR0]); C(GE_CMD_TEXBUFWIDTH0, state.ge_cmd[GE_CMD_TEXBUFWIDTH0]);
  C(GE_CMD_TEXFLUSH, 0);
  C(GE_CMD_RET, 0);
  #undef C
  txCall(list, reg, sl);
  st.tx_screen++;
  return 1;
}

// Capa intercambiada (VRAM de 2 MB: no hay lugar para render targets). Silent Hill dibuja
// la escena, luego un pase de luz en otro fb (0a170000, con la profundidad de la escena) y al
// final pega el pase sobre la escena multiplicando. Aqui: antes del pase, el GE copia la
// escena del buffer grande a la RAM (LAYER_SAVE); el pase se dibuja en el buffer grande al
// doble; y en vez de la mezcla del juego se dibuja la escena guardada encima del pase con
// los factores de mezcla intercambiados (escena x luz = luz x escena).
#define LAYER_SAVE 0x0A500000           // 960x544 en 565 (1020 KB): libre entre la imagen nativa y los parches
#define LAYER_VERTS (RENDER_LIST + 0x6000) // 4 cuartos x 8 franjas x 2 vertices x 16 bytes

// Mezcla vista desde el otro lado: lo que era el destino pasa a ser el origen
static u32 blendSwap(u32 bm) {
  static const u8 sw[16] = { 0, 1, 4, 5, 2, 3, 8, 9, 6, 7, 10, 11, 12, 13, 14, 15 };
  u32 eq = (bm >> 8) & 0xf;
  eq = eq == 1 ? 2 : eq == 2 ? 1 : eq; // resta <-> resta inversa
  return (eq << 8) | (sw[bm & 0xf] << 4) | sw[(bm >> 4) & 0xf];
}

// Draw 2D que pega todo un fb 1:1 sobre la pantalla (rectangulos, 16 bits, sin indices).
// Devuelve su color (todos iguales) o 0 si no es asi.
static u32 fullCopyColor(u32 count) {
  u32 vt = state.vertex_type, va = state.vertex_addr, k, col = 0xffffffff;
  u8 vs = 0, po = 0, to = 0;
  int minx = 9999, miny = 9999, maxx = -9999, maxy = -9999;
  if ((vt & ~(GE_VTYPE_COL_MASK)) != (GE_VTYPE_THROUGH | GE_VTYPE_TC_16BIT | GE_VTYPE_POS_16BIT) ||
      ((vt & GE_VTYPE_COL_MASK) && (vt & GE_VTYPE_COL_MASK) != GE_VTYPE_COL_8888) || count < 2 || count > 256)
    return 0;
  vertexInfo(vt, &vs, &po, &to);
  if (!validAddr(va) || !validAddr(va + count * vs))
    return 0;
  for (k = 0; k < count; k++, va += vs) {
    short *pp = (short *)(va + po);
    u16 *t = (u16 *)(va + to);
    if (t[0] != (u16)pp[0] || t[1] != (u16)pp[1])
      return 0;
    if (vt & GE_VTYPE_COL_MASK) {
      u32 c = *(u32 *)(va + to + 4);
      if (k == 0) col = c; else if (c != col) return 0;
    }
    if (pp[0] < minx) minx = pp[0];
    if (pp[0] > maxx) maxx = pp[0];
    if (pp[1] < miny) miny = pp[1];
    if (pp[1] > maxy) maxy = pp[1];
  }
  return minx <= 0 && miny <= 0 && maxx >= 480 && maxy >= 272 ? col : 0;
}

// Draw omitido que pega un fb nunca mostrado sobre la pantalla: desde el proximo frame ese
// fb se dibuja por intercambio
static void swapLearn(u32 count) {
  u32 tex = toFake(((state.ge_cmd[GE_CMD_TEXBUFWIDTH0] & 0x0f0000) << 8) | (state.ge_cmd[GE_CMD_TEXADDR0] & 0xffffff));
  int f = findFramebuf(tex);
  if (f < 0 || f == findFramebuf(state.framebuf) || state.fb_shown[f] || state.fb_swap[f] || state.cur_rt ||
      state.ignore_framebuf || !fullCopyColor(count))
    return;
  state.fb_swap[f] = 1;
  LOG("fb %08lx: pase que se mezcla con la pantalla -> desde el proximo frame se dibuja por intercambio con la RAM\n", tex);
}

// Entrando al fb del pase (word = la segunda de FRAMEBUFPTR/FRAMEBUFWIDTH, contiguas): esas
// dos palabras pasan a BASE + CALL a una lista que guarda la escena y fija el buffer grande
static int swapEnter(u32 *word) {
  u32 *a = state.framebufptr_addr, *b = state.framebufwidth_addr, sl;
  if (!(word == a || word == b) || (a != b + 1 && b != a + 1) || word != (a > b ? a : b) || !(sl = txSlot()))
    return 0;
  u32 *l = (u32 *)(sl | 0x40000000);
  #define C(c, d) (*l++ = ((c) << 24) | ((d) & 0xffffff))
  C(GE_CMD_TRANSFERSRC, VRAM_DRAW_BUFFER_OFFSET); C(GE_CMD_TRANSFERSRCW, ((VRAM_DRAW_BUFFER_OFFSET >> 24) << 16) | PITCH);
  C(GE_CMD_TRANSFERSRCPOS, 0);
  C(GE_CMD_TRANSFERDST, LAYER_SAVE); C(GE_CMD_TRANSFERDSTW, ((LAYER_SAVE >> 24) << 16) | PITCH);
  C(GE_CMD_TRANSFERDSTPOS, 0);
  C(GE_CMD_TRANSFERSIZE, ((HEIGHT - 1) << 10) | (WIDTH - 1)); C(GE_CMD_TRANSFERSTART, 0);
  C(GE_CMD_FRAMEBUFPTR, VRAM_DRAW_BUFFER_OFFSET); C(GE_CMD_FRAMEBUFWIDTH, ((VRAM_DRAW_BUFFER_OFFSET >> 24) << 16) | PITCH);
  C(GE_CMD_BASE, state.base >> 8);
  C(GE_CMD_RET, 0);
  #undef C
  txCall(word, word == a ? b : a, sl);
  return 1;
}

// La mezcla del juego (textura = el fb del pase) se reemplaza por la escena guardada
// dibujada sobre el pase. Devuelve 0 si no se puede (el draw se omite).
static int swapComposite(u32 *list, u32 op, u32 count) {
  u32 *reg = list - 1, sl, c, i, x, col = fullCopyColor(count);
  u32 bm = state.ge_cmd[GE_CMD_BLENDMODE], blend = state.ge_cmd[GE_CMD_ALPHABLENDENABLE] & 1;
  int mul = (bm & 0xfff) == 0x0a0 && !(state.ge_cmd[GE_CMD_BLENDFIXEDB] & 0xffffff); // origen x destino
  if (!col || !validAddr((u32)reg) || !(blend ? col == 0xffffffff || mul : col == 0xffffffff))
    return 0;
  c = origOp(reg) >> 24;
  if (c == GE_CMD_CALL || c == GE_CMD_JUMP || c == GE_CMD_BJUMP || c == GE_CMD_RET || c == GE_CMD_END || c == GE_CMD_FINISH ||
      c == GE_CMD_SIGNAL || c == GE_CMD_PRIM || c == GE_CMD_BEZIER || c == GE_CMD_SPLINE || c == GE_CMD_TRANSFERSTART ||
      c == GE_CMD_ORIGIN || !(sl = txSlot()))
    return 0;
  u32 prev = *reg, *l = (u32 *)(sl | 0x40000000);
  #define C(c, d) (*l++ = ((c) << 24) | ((d) & 0xffffff))
  C(GE_CMD_BASE, state.base >> 8);
  *l++ = prev;
  if (blend) { // sin mezcla y en blanco, el pase ya es el resultado: no se dibuja nada
    struct { u16 u, v; u32 c; short x, y, z, pad; } *v = (void *)(LAYER_VERTS | 0x40000000);
    for (i = 0; i < 4; i++)
      for (x = 0; x < 480; x += 64, v += 2) {
        u32 x2 = x + 64 < 480 ? x + 64 : 480, dx = (i & 1) * 480, dy = (i >> 1) * 272;
        v[0].u = x; v[0].v = 0; v[0].c = col; v[0].x = dx + x; v[0].y = dy; v[0].z = 0;
        v[1].u = x2; v[1].v = 272; v[1].c = col; v[1].x = dx + x2; v[1].y = dy + 272; v[1].z = 0;
      }
    C(GE_CMD_TEXTUREMAPENABLE, 1); C(GE_CMD_TEXMODE, 0); C(GE_CMD_TEXFORMAT, 0); // 565, como el buffer grande
    C(GE_CMD_TEXSIZE0, (9 << 8) | 9); C(GE_CMD_TEXFILTER, 0); C(GE_CMD_TEXWRAP, (1 << 8) | 1);
    C(GE_CMD_BLENDMODE, blendSwap(bm));
    C(GE_CMD_BLENDFIXEDA, state.ge_cmd[GE_CMD_BLENDFIXEDB]); C(GE_CMD_BLENDFIXEDB, state.ge_cmd[GE_CMD_BLENDFIXEDA]);
    C(GE_CMD_VERTEXTYPE, GE_VTYPE_THROUGH | GE_VTYPE_TC_16BIT | GE_VTYPE_COL_8888 | GE_VTYPE_POS_16BIT);
    C(GE_CMD_BASE, (LAYER_VERTS >> 8) & 0x0f0000); C(GE_CMD_VADDR, LAYER_VERTS);
    for (i = 0; i < 4; i++) {
      u32 src = LAYER_SAVE + ((i >> 1) * 272 * PITCH + (i & 1) * 480) * 2;
      C(GE_CMD_TEXADDR0, src); C(GE_CMD_TEXBUFWIDTH0, ((src >> 24) << 16) | PITCH);
      C(GE_CMD_TEXFLUSH, 0);
      for (x = 0; x < 480; x += 64)
        C(GE_CMD_PRIM, (GE_PRIM_RECTANGLES << 16) | 2);
    }
    static const u8 back[] = { GE_CMD_TEXTUREMAPENABLE, GE_CMD_TEXMODE, GE_CMD_TEXFORMAT, GE_CMD_TEXSIZE0, GE_CMD_TEXFILTER,
      GE_CMD_TEXWRAP, GE_CMD_TEXADDR0, GE_CMD_TEXBUFWIDTH0, GE_CMD_BLENDMODE, GE_CMD_BLENDFIXEDA, GE_CMD_BLENDFIXEDB,
      GE_CMD_VERTEXTYPE };
    for (i = 0; i < sizeof(back); i++)
      C(back[i], state.ge_cmd[back[i]]);
    C(GE_CMD_TEXFLUSH, 0);
    C(GE_CMD_BASE, (state.vertex_addr >> 8) & 0x0f0000); C(GE_CMD_VADDR, state.vertex_addr); // como un draw omitido
  }
  C(GE_CMD_BASE, state.base >> 8);
  C(GE_CMD_RET, 0);
  #undef C
  txCall(list, reg, sl);
  return 1;
}

static int txUpscale(u32 *list, u32 dst, u32 w, u32 h, int bpp) {
  u32 k, *reg;
  if (state.cur_rt || state.ignore_framebuf || dst != state.framebuf || !(reg = txIsCopy(list, 1)))
    return 0;
  u32 src = ((state.ge_cmd[GE_CMD_TRANSFERSRCW] & 0xff0000) << 8) | (state.ge_cmd[GE_CMD_TRANSFERSRC] & 0xffffff);
  u32 sw = state.ge_cmd[GE_CMD_TRANSFERSRCW] & 0x7ff, sp = state.ge_cmd[GE_CMD_TRANSFERSRCPOS];
  u32 dp = state.ge_cmd[GE_CMD_TRANSFERDSTPOS], tex = src + (((sp >> 10) & 0x3ff) * sw + (sp & 0x3ff)) * bpp;
  int dx = (int)(dp & 0x3ff) - state.offx, dy = (int)((dp >> 10) & 0x3ff) - state.offy;
  if ((tex & 15) || !validAddr(tex) || w > 512 || h > 272 || sw > 1024) // el buffer temporal es de 512x272
    return 0;
  u32 sl = txSlot();
  if (!sl)
    return 0; // el CALL no puede llegar a la lista con el OFFSETADDR del juego
  u32 *l = (u32 *)(sl | 0x40000000), va = sl + 512;
  short *v = (short *)(va | 0x40000000);
  #define C(c, d) (*l++ = ((c) << 24) | ((d) & 0xffffff))
  l = txEmitOrig(l, bpp == 4);                  // la copia original, tal cual
  C(GE_CMD_OFFSETADDR, 0);                      // el RET restaura el del juego
  // La imagen se pasa a la VRAM antes de usarla como textura: leer una textura grande
  // desde la RAM era muy lento (Silent Hill bajaba a 12 fps en los videos)
  C(GE_CMD_TRANSFERSRC, tex); C(GE_CMD_TRANSFERSRCW, ((tex >> 24) << 16) | sw); C(GE_CMD_TRANSFERSRCPOS, 0);
  C(GE_CMD_TRANSFERDST, copy_vram); C(GE_CMD_TRANSFERDSTW, ((copy_vram >> 24) << 16) | 512); C(GE_CMD_TRANSFERDSTPOS, 0);
  C(GE_CMD_TRANSFERSIZE, ((h - 1) << 10) | (w - 1)); C(GE_CMD_TRANSFERSTART, bpp == 4);
  C(GE_CMD_TEXSYNC, 0); // esperar a que termine la copia antes de leerla (sin esto: cuadros negros/basura)
  C(GE_CMD_TEXTUREMAPENABLE, 1); C(GE_CMD_TEXMODE, 0);
  C(GE_CMD_TEXFORMAT, bpp == 4 ? 3 : state.ge_cmd[GE_CMD_FRAMEBUFPIXFORMAT] & 3);
  C(GE_CMD_TEXADDR0, copy_vram); C(GE_CMD_TEXBUFWIDTH0, ((copy_vram >> 24) << 16) | 512);
  C(GE_CMD_TEXSIZE0, (lg2(h) << 8) | lg2(w));
  C(GE_CMD_TEXFILTER, (1 << 8) | 1); C(GE_CMD_TEXWRAP, (1 << 8) | 1); C(GE_CMD_TEXFUNC, 3); C(GE_CMD_TEXFLUSH, 0);
  for (k = 9; k < 15; k++)
    C(tx_saved[k], 0);                          // sin blend, alpha/z/stencil test, logic op, color test
  C(GE_CMD_ZWRITEDISABLE, 1); C(GE_CMD_MASKRGB, 0); C(GE_CMD_MASKALPHA, 0);
  C(GE_CMD_CLEARMODE, 0); C(GE_CMD_TEXMAPMODE, 0);
  C(GE_CMD_VERTEXTYPE, GE_VTYPE_THROUGH | GE_VTYPE_TC_16BIT | GE_VTYPE_POS_16BIT);
  // En franjas de 32 texeles (64 px): la cache de texturas del GE es chica y un sprite de
  // todo el ancho la vaciaba en cada linea
  C(GE_CMD_BASE, (va >> 8) & 0x0f0000); C(GE_CMD_VADDR, va);
  u32 x;
  for (x = 0; x < w; x += 32, v += 10) {
    u32 x2 = x + 32 < w ? x + 32 : w;
    short q[10] = { x, 0, (dx + x) * 2, dy * 2, 0, x2, h, (dx + x2) * 2, (dy + h) * 2, 0 };
    memcpy(v, q, sizeof(q));
    C(GE_CMD_PRIM, (GE_PRIM_RECTANGLES << 16) | 2); // el VADDR avanza solo
  }
  #undef C
  txRestore(l, 0);
  txCall(list, reg, sl);
  st.tx_ge++;
  LOG_ONCE(0xE7000000 ^ dst, "copia %lux%lu a fb %08lx -> se escala con el GE en su lugar de la lista\n", w, h, dst);
  return 1;
}

// ---------------------------------------------------------------------------
// Sublistas sin cambios (Gran Turismo: ~1500 CALL por frame con el 94% de los comandos).
// Al recorrer una sublista (CALL ... RET) se anota si el plugin no tuvo que hacer nada en
// ella: sin parches, sin draws 2D, sin draws omitidos ni texturas de render target, sin
// saltos ni sublistas dentro. Si lo es, se guarda su largo, una suma de sus palabras y
// como deja el estado (BASE, VADDR/IADDR, tipo de vertice, textura). La proxima vez, si la
// suma coincide y nada cambio (framebuffers, render targets, opciones), no se recorre:
// solo se suman sus palabras (~3 instrucciones por palabra en vez de ~40) y se aplica el
// estado guardado.
// ---------------------------------------------------------------------------
#define WC_ADDR 0x0A780000 // 2048 entradas de 248 bytes en RAM libre (hasta 0x0A7FC000)
#define WC_SIZE 2048
// Las sublistas suelen estar alineadas: con solo los bits bajos, dos a 16 KB chocaban
#define WC_SLOT(a) ((((a) >> 2) * 2654435761u) >> 21) // hash multiplicativo: 11 bits = 2048
enum { T_TME = 1, T_TADDR = 2, T_TBW = 4, T_TSIZE = 8, T_TFMT = 16, T_VTYPE = 32, T_BASE = 64 };
typedef struct {
  u32 addr, n, sum, base_in, offset_in, gen, fb_in, ndraws, touch, vset, iset;
  void *rt_in;
  u32 vaddr, iaddr, vtype, base_out, tex[5], nchild, child[32];
  u32 checked; // ultimo frame en que se comprobo la suma
  u32 *tex_p[4];
  u32 tex_o[4];
} WalkCache;
static WalkCache *wc = (WalkCache *)WC_ADDR;
// Lo que cambia la decision del plugin sobre los draws: framebuffers y render targets
// conocidos (rt_gen) y opciones del menu (opt_gen). El fb actual va en cada entrada.
#define WC_GEN ((rt_gen << 8) ^ opt_gen)
// Por que una sublista no se pudo guardar (estadistica, para saber que mejorar)
enum { W_PATCH, W_2D, W_OMIT, W_JUMP, W_NESTED, W_COPY, W_OFFSET };
// Sublistas que se estan anotando, una por nivel de CALL (recs[0] no se usa: es donde
// escriben los DIRTY/touch cuando no hay ninguna). Una sublista con otras dentro se puede
// guardar si las de dentro tambien se pudieron: se guardan sus direcciones y al saltarla
// se comprueba que ninguna cambio (Gran Turismo: ~150 por frame tenian sublistas dentro).
#define WC_DEPTH 8
#define WC_CHILD 32 // Gran Turismo: autos con muchas piezas
typedef struct {
  int depth, why, nchild;
  u32 addr, base, offset, setops, draws, touch, vset, iset, vaddr0, iaddr0, child[WC_CHILD];
} Rec;
static Rec recs[WC_DEPTH + 1];
#define rec recs[nrec]
#define DIRTY(w) (rec.why |= 1 << (w))

static inline u32 wcSum(const u32 *p, u32 n) {
  u32 h = n;
  while (n--)
    h = ((h << 5) | (h >> 27)) + *p++;
  return h;
}

static const u8 wc_tex_cmd[5] = { GE_CMD_TEXTUREMAPENABLE, GE_CMD_TEXADDR0, GE_CMD_TEXBUFWIDTH0, GE_CMD_TEXSIZE0, GE_CMD_TEXFORMAT };

// Empieza a anotar la sublista en addr (justo despues del CALL)
static void wcRecord(u32 addr) {
  if (nrec == WC_DEPTH) {
    DIRTY(W_NESTED);
    return;
  }
  Rec *r = &recs[++nrec];
  r->why = r->nchild = 0;
  r->depth = state.curr_stack;
  r->addr = addr; r->base = state.base; r->offset = state.offset;
  r->setops = n_setops; r->draws = st.draws; r->touch = r->vset = r->iset = 0;
  r->vaddr0 = state.vertex_addr; r->iaddr0 = state.index_addr;
}

// La sublista addr (ya guardada) quedo dentro de la que se anota
static void wcAddChild(u32 addr, u32 touch, u32 vset, u32 iset) {
  if (!nrec)
    return;
  if (rec.nchild == WC_CHILD)
    DIRTY(W_NESTED);
  else
    rec.child[rec.nchild++] = addr;
  rec.touch |= touch; rec.vset |= vset; rec.iset |= iset;
}

// En el RET de la sublista anotada: si quedo limpia, se guarda
static void wcStore(u32 *ret) {
  Rec *r = &recs[nrec--];
  if (n_setops != r->setops)
    r->why |= 1 << W_PATCH;
  if (r->why || ret < (u32 *)r->addr) {
    int i;
    for (i = 0; i < 7; i++)
      st.wc_why[i] += (r->why >> i) & 1;
    rec.why |= r->why ? r->why : 1 << W_NESTED; // la que la contiene tampoco se puede guardar
    return;
  }
  WalkCache *e = &wc[WC_SLOT(r->addr)];
  int i;
  e->addr = r->addr; e->n = ret - (u32 *)r->addr + 1; e->sum = wcSum((u32 *)r->addr, e->n);
  e->base_in = r->base; e->offset_in = r->offset; e->gen = WC_GEN;
  e->fb_in = state.ignore_framebuf; e->rt_in = state.cur_rt; // no el fb: en doble buffer cambia cada frame
  e->ndraws = st.draws - r->draws; e->touch = r->touch; e->vset = r->vset; e->iset = r->iset;
  e->vaddr = r->vset ? state.vertex_addr : state.vertex_addr - r->vaddr0;
  e->iaddr = r->iset ? state.index_addr : state.index_addr - r->iaddr0;
  e->vtype = state.vertex_type; e->base_out = state.base;
  e->nchild = r->nchild;
  e->checked = log_frame;
  for (i = 0; i < r->nchild; i++)
    e->child[i] = r->child[i];
  for (i = 0; i < 5; i++)
    e->tex[i] = state.ge_cmd[wc_tex_cmd[i]];
  for (i = 0; i < 4; i++) {
    e->tex_p[i] = state.tex_p[i];
    e->tex_o[i] = state.tex_o[i];
  }
  wcAddChild(r->addr, r->touch, r->vset, r->iset);
}

// La sublista guardada en addr (y las que tiene dentro) sigue igual; devuelve sus palabras
// La suma se comprueba una vez por frame (una sublista llamada varias veces en el frame, como
// las ruedas de los autos, no se vuelve a sumar), o cada N frames segun la opcion del menu.
static u32 wcValid(u32 addr, int depth) {
  WalkCache *e = &wc[WC_SLOT(addr)];
  if (e->addr != addr || depth > WC_DEPTH || !validAddr(addr + e->n * 4))
    return 0;
  u32 every = 1u << opt.wc_check; // 1, 2, 4 u 8 frames
  if (log_frame - e->checked >= every || log_frame < e->checked) {
    if (wcSum((u32 *)addr, e->n) != e->sum)
      return 0;
    e->checked = log_frame;
    st.wc_summed += e->n;
  }
  u32 n = e->n, i, c;
  for (i = 0; i < e->nchild; i++) {
    if (!(c = wcValid(e->child[i], depth + 1)))
      return 0;
    n += c;
  }
  return n;
}

// En un CALL: si la sublista esta guardada y no cambio, se aplica su efecto y no se recorre
static int wcSkip(u32 addr) {
  WalkCache *e = &wc[WC_SLOT(addr)];
  if (e->addr != addr || e->gen != WC_GEN || e->base_in != state.base || e->offset_in != state.offset ||
      e->fb_in != state.ignore_framebuf || e->rt_in != state.cur_rt || tx_patched_any || !opt.walk_cache)
    return 0;
  u32 n = wcValid(addr, 0);
  if (!n) {
    st.wc_miss++;
    e->addr = 0;
    return 0;
  }
  int i;
  state.vertex_addr = e->vset ? e->vaddr : state.vertex_addr + e->vaddr;
  state.index_addr = e->iset ? e->iaddr : state.index_addr + e->iaddr;
  if (e->touch & T_VTYPE) state.vertex_type = e->vtype;
  if (e->touch & T_BASE) state.base = e->base_out;
  for (i = 0; i < 5; i++)
    if (e->touch & (1 << i))
      state.ge_cmd[wc_tex_cmd[i]] = e->tex[i];
  if (e->touch & (T_TADDR | T_TBW | T_TSIZE | T_TFMT))
    for (i = 0; i < 4; i++) {
      state.tex_p[i] = e->tex_p[i];
      state.tex_o[i] = e->tex_o[i];
    }
  st.draws += e->ndraws;
  draws_since_flip += e->ndraws;
  if (e->ndraws)
    state.has_draws = 1;
  st.wc_hits++;
  st.wc_cmds += n;
  wcAddChild(addr, e->touch, e->vset, e->iset);
  return 1;
}

// Volcado: el draw con la decision del plugin y sus datos tal como los dejo el juego
static u32 dump_draws;
static void dumpDraw(u32 *list, u32 op, u32 count, int texmode, int omit) {
  u8 vs = 0, po = 0, to = 0;
  vertexInfo(state.vertex_type, &vs, &po, &to);
  u16 lower = 0, upper = count;
  u32 isz = ((state.vertex_type & GE_VTYPE_IDX_MASK) >> GE_VTYPE_IDX_SHIFT), k;
  isz = isz == 1 ? 1 : isz == 2 ? 2 : isz == 3 ? 4 : 0;
  int ivalid = isz && validAddr(state.index_addr) && validAddr(state.index_addr + count * isz);
  if (ivalid) {
    GetIndexBounds((void *)state.index_addr, count, state.vertex_type, &lower, &upper);
    upper++;
  }
  u32 flags = ((state.vertex_type & GE_VTYPE_THROUGH_MASK) == GE_VTYPE_THROUGH) | (state.ignore_framebuf ? 2 : 0) |
              (state.cur_rt ? 4 : 0) | (state.cur_rt == &csrc_rt ? 8 : 0);
  u32 d[20] = { dump_draws++, (u32)list, op, state.vertex_type, state.vertex_addr, state.index_addr, vs, lower, upper,
                texmode, omit, flags, state.framebuf, state.cur_rt ? state.cur_rt->vram : state.ignore_framebuf ? 0 : VRAM_DRAW_BUFFER_OFFSET,
                SCALE(), state.offx, state.offy,
                ((state.ge_cmd[GE_CMD_TEXBUFWIDTH0] & 0x0f0000) << 8) | (state.ge_cmd[GE_CMD_TEXADDR0] & 0xffffff),
                state.cur_fb, state.big_fb };
  dumpRecord(GPD_DRAW, d, sizeof(d), NULL, 0);
  if (ivalid) // despues del registro del draw (en dev28 iba antes y quedaba con el draw anterior)
    dumpBlob(B_INDICES, state.index_addr, count * isz);
  if (vs && validAddr(state.vertex_addr) && validAddr(state.vertex_addr + upper * vs))
    dumpBlob(B_VERTS, state.vertex_addr, upper * vs);
  // Texturas de cada nivel (formato, tamano y ancho de linea del juego)
  if ((state.ge_cmd[GE_CMD_TEXTUREMAPENABLE] & 1) || (state.vertex_type & GE_VTYPE_TC_MASK)) {
    static const u8 bits[16] = { 16, 16, 16, 32, 4, 8, 16, 32, 4, 8, 8 };
    u32 fmt = state.ge_cmd[GE_CMD_TEXFORMAT] & 0xf, levels = ((state.ge_cmd[GE_CMD_TEXMODE] >> 16) & 7) + 1;
    for (k = 0; k < levels && bits[fmt]; k++) {
      u32 a = ((state.ge_cmd[GE_CMD_TEXBUFWIDTH0 + k] & 0x0f0000) << 8) | (state.ge_cmd[GE_CMD_TEXADDR0 + k] & 0xffffff);
      u32 sz = state.ge_cmd[GE_CMD_TEXSIZE0 + k], w = 1 << (sz & 0xf), h = 1 << ((sz >> 8) & 0xf);
      u32 bw = state.ge_cmd[GE_CMD_TEXBUFWIDTH0 + k] & 0x7ff;
      u32 bytes = (bits[fmt] * (bw * h + (w > bw ? w - bw : 0))) / 8;
      if ((a & 0x0f000000) == 0x04000000 && !passive)
        a = toFake(a); // lo que el juego subio a la VRAM esta en la falsa (las copias del plugin aparte)
      // Las de la VRAM (fb usados como textura) ya estan en la VRAM guardada al empezar
      if (bytes && bytes <= 0x100000 && validAddr(a) && validAddr(a + bytes - 1) && (a & 0x0f000000) != 0x04000000 &&
          !(a >= FAKE_VRAM && a < FAKE_VRAM + 0x400000))
        dumpBlobOnce(B_TEX | (k << 8), a, bytes);
    }
  }
}

void patchGeList(u32 *list, u32 *stall) {
  union {
    float f;
    unsigned int i;
  } t;

  StackEntry *stack_entry;
  StackEntry stack_entry_buf;
  int ncmds = 0;
  u32 *lim = NULL; // fin de la region valida actual; NULL = validar de nuevo (tras un salto)

  for (; list && (!stall || (stall && list != stall)); list++) {
    // Nunca leer fuera de la memoria valida ni recorrer para siempre
    if (compat_walk || !lim || list >= lim) {
      if (!(lim = regionEnd(list))) {
        LOG_ONCE_CAP(((u32)list >> 12) ^ 0xD0000000, "lista apunta a direccion invalida %08lx, se corta\n", (u32)list);
        goto finish;
      }
    }
    if (++ncmds > MAX_CMDS) {
      LOG_ONCE(0xE6000000, "lista con mas de %d comandos (bucle?), se corta\n", MAX_CMDS);
      goto finish;
    }

    // Valor original del juego aunque el comando ya este parcheado (ver memo_cmd)
    u32 op = *list, m = memo_cmd[op >> 24];
    if (m == 1 && op == (GE_CMD_NOP << 24) && opt.nop_mark)
      m = 0; // NOP 0 del juego: con la marca activa no puede ser un draw anulado por el plugin
    if (m == 1 || (m == 2 && tex_patched_any) || (m == 3 && tx_patched_any)) {
      st.lookups++;
      op = origOp(list);
      // FBPTR 0x000000 del juego es igual al parche del buffer grande. Si el FBWIDTH de al
      // lado no tiene el parche (el juego reescribio la lista este frame), el valor es del
      // juego aunque la memoria diga otra cosa: en doble buffer alterna 0x044000/0x000000.
      if ((op >> 24) == GE_CMD_FRAMEBUFPTR && op != *list &&
          (((list[1] >> 24) == GE_CMD_FRAMEBUFWIDTH && ((list[1] >> 16) & 0xff) != (VRAM_DRAW_BUFFER_OFFSET >> 24)) ||
           ((list[-1] >> 24) == GE_CMD_FRAMEBUFWIDTH && ((list[-1] >> 16) & 0xff) != (VRAM_DRAW_BUFFER_OFFSET >> 24))))
        op = *list;
    }
    u32 cmd = op >> 24;
    u32 data = op & 0xffffff;
    u32 *cur = dump_cur = list; // el switch puede mover list (CALL, JUMP, RET)

    state.ge_cmd[cmd] = data;
    if (capturing)
      cap_hist[cmd]++;

    switch (cmd) {
      // Skip matrix data

      // case GE_CMD_BONEMATRIXNUMBER:
        // if (*(list+12) >> 24 == GE_CMD_BONEMATRIXDATA)
          // list += 12;
        // break;

      // case GE_CMD_WORLDMATRIXNUMBER:
        // if (*(list+12) >> 24 == GE_CMD_WORLDMATRIXDATA)
          // list += 12;
        // break;

      // case GE_CMD_VIEWMATRIXNUMBER:
        // if (*(list+12) >> 24 == GE_CMD_VIEWMATRIXDATA)
          // list += 12;
        // break;

      // case GE_CMD_TGENMATRIXNUMBER:
        // if (*(list+12) >> 24 == GE_CMD_TGENMATRIXDATA)
          // list += 12;
        // break;

      // case GE_CMD_PROJMATRIXNUMBER:
        // if (*(list+16) >> 24 == GE_CMD_PROJMATRIXDATA)
          // list += 16;
        // break;

      // Datos de matriz (12-16 seguidos por objeto 3D): el plugin no los toca, se saltan
      // los iguales seguidos sin pasar por todo el ciclo
      case GE_CMD_BONEMATRIXDATA:
      case GE_CMD_WORLDMATRIXDATA:
      case GE_CMD_VIEWMATRIXDATA:
      case GE_CMD_PROJMATRIXDATA:
      case GE_CMD_TGENMATRIXDATA:
        while (!compat_walk && !dumping && list + 1 < lim && list + 1 != stall && (list[1] >> 24) == cmd && ncmds < MAX_CMDS) {
          list++;
          ncmds++;
          if (capturing)
            cap_hist[cmd]++;
        }
        break;

      // Handle control flow commands

      case GE_CMD_BASE:
        state.base = (data << 8) & 0x0f000000;
        rec.touch |= T_BASE;
        break;

      case GE_CMD_OFFSETADDR:
        state.offset = data << 8;
        DIRTY(W_OFFSET);
        break;

      case GE_CMD_ORIGIN:
        state.offset = (u32)list;
        DIRTY(W_OFFSET);
        break;

      case GE_CMD_TEXTUREMAPENABLE:
        rec.touch |= T_TME;
        break;

      case GE_CMD_TRANSFERSRC: case GE_CMD_TRANSFERSRCW: case GE_CMD_TRANSFERSRCPOS: case GE_CMD_TRANSFERDST:
      case GE_CMD_TRANSFERDSTW: case GE_CMD_TRANSFERDSTPOS: case GE_CMD_TRANSFERSIZE:
        DIRTY(W_COPY); // una copia posterior usaria estos registros
        break;

      // TODO: need to save other states, too?
      case GE_CMD_CALL:
        state.address = ((state.base | data) + state.offset) & 0x0ffffffc;
        if (state.address >= TX_RING && state.address < TX_RING + 64 * TX_SLOT)
          break; // lista del plugin (copia escalada): no se recorre
        if (!validAddr(state.address) || !validAddr(state.address + 12 * 4)) {
          LOG_ONCE_CAP((state.address >> 12) ^ 0xD1000000, "CALL a direccion invalida %08lx, se ignora\n", state.address);
          break;
        }
        if (*(u32 *)(state.address) >> 24 == GE_CMD_BONEMATRIXDATA &&
            *(u32 *)(state.address+11*4) >> 24 == GE_CMD_BONEMATRIXDATA &&
            *(u32 *)(state.address+12*4) >> 24 == GE_CMD_RET) {
          break;
        }
        st.calls++;
        if (!capturing && !dumping && wcSkip(state.address))
          break;
        stack_entry = &stack_entry_buf;
        stack_entry->list = list;
        stack_entry->base = state.base; // antes quedaba sin inicializar y un SIGNAL RET lo restauraba
        stack_entry->offset = state.offset;
        stack_entry->ncmds = ncmds;
        if (push(stack_entry) == 0) {
          if (!capturing && !dumping)
            wcRecord(state.address);
          else
            nrec = 0;
          list = (u32 *)(state.address - 4);
          lim = NULL;
        }
        break;

      // BJUMP salta solo si la caja de BOUNDINGBOX quedo fuera de pantalla. Antes siempre
      // se saltaba, y los draws que el GE si ejecuta (caja visible) quedaban sin parchear:
      // geometria en la esquina o que aparece/desaparece. Seguir de largo recorre ambos caminos.
      case GE_CMD_BJUMP: // el recorrido sigue de largo igual que al saltar la sublista
        break;

      case GE_CMD_JUMP:
        DIRTY(W_JUMP);
        state.address = ((state.base | data) + state.offset) & 0x0ffffffc;
        list = (u32 *)(state.address - 4);
        lim = NULL;
        break;

      case GE_CMD_RET:
        // Ignore returns when the stack is empty
        if (nrec && rec.depth == (int)state.curr_stack)
          wcStore(list);
        stack_entry = pop();
        if (stack_entry) {
          if (!state.curr_stack)
            st.call_cmds += ncmds - stack_entry->ncmds;
          list = stack_entry->list;
          state.offset = stack_entry->offset;
          lim = NULL;
        }
        break;

      case GE_CMD_END:
      {
        u32 prev = *(list-1);
        if (nrec)
          st.wc_why[W_JUMP]++;
        nrec = 0; // SIGNAL/END: las sublistas que se anotaban no se guardan
        switch (prev >> 24) {
          case GE_CMD_SIGNAL:
          {
            u8 behaviour = (prev >> 16) & 0xff;
            u16 signal = prev & 0xffff;
            u16 enddata = data & 0xffff;
            u32 target;

            switch (behaviour) {
              case PSP_GE_SIGNAL_SYNC:
                list += 2;
                break;

              case PSP_GE_SIGNAL_JUMP:
                target = (((signal << 16) | enddata) & 0x0ffffffc);
                list = (u32 *)(target - 4);
                break;

              case PSP_GE_SIGNAL_CALL:
                target = (((signal << 16) | enddata) & 0x0ffffffc);
                stack_entry = &stack_entry_buf;
                stack_entry->list = list;
                stack_entry->base = state.base;
                stack_entry->offset = state.offset;
                stack_entry->ncmds = ncmds;
                if (push(stack_entry) == 0)
                  list = (u32 *)(target - 4);
                break;

              case PSP_GE_SIGNAL_RET:
                // Ignore returns when the stack is empty
                stack_entry = pop();
                if (stack_entry) {
                  list = stack_entry->list;
                  state.base = stack_entry->base;
                  state.offset = stack_entry->offset;
                }
                break;

              default:
                LOG_ONCE_CAP(0xE3000000 | behaviour, "signal no manejada: behaviour=%d\n", behaviour);
                break;
            }
            lim = NULL;
            break;
          }

          case GE_CMD_FINISH:
            if (dumping)
              dumpCmd(cur, op);
            goto finish;

          default:
            break;
        }
        break;
      }

      // Patch vertices

      case GE_CMD_IADDR:
        state.index_addr = ((state.base | data) + state.offset) & 0x0fffffff;
        rec.iset = 1;
        break;

      case GE_CMD_VADDR:
        state.vertex_addr = ((state.base | data) + state.offset) & 0x0fffffff;
        rec.vset = 1;
        break;

      case GE_CMD_VERTEXTYPE:
        state.vertex_type = data;
        rec.touch |= T_VTYPE;
        break;

      case GE_CMD_BEZIER:
      case GE_CMD_SPLINE:
      {
        DIRTY(W_2D);
        state.has_draws = 1;
        draws_since_flip++;

        int texmode = state.ignore_framebuf ? 0 : texCheck();
        if (texmode == 5 || texmode == 6)
          texmode = 1; // curvas con la pantalla como textura (en un rt o la mezcla de un pase): no se reemplazan
        int omit = state.ignore_framebuf || texmode == 1;
        if (capturing)
          capDraw(cmd == GE_CMD_PRIM ? (data >> 16) & 7 : cmd, cmd == GE_CMD_PRIM ? data & 0xffff : (data & 0xff) * ((data >> 8) & 0xff),
                  omit | (texmode == 2 ? 4 : 0) | (state.cur_rt ? 8 : 0) | (state.cur_rt == &csrc_rt ? 32 : 0));
        if (dumping)
          dumpDraw(list, op, (data & 0xff) * ((data >> 8) & 0xff), texmode, omit);
        if (omit) {
          setOp(list, op, NOP_OMIT);
          st.skipped++;
          if (state.cur_rt && !state.cur_rt->incomplete) {
            state.cur_rt->incomplete = 1;
            rt_gen++;
          }
          break;
        }
        if (*list != op && !passive) // se habia anulado en una pasada anterior: restaurar
          *list = op;
        st.draws++;
        if (state.cur_fb >= 0 && state.fb_tx[state.cur_fb] && !state.cur_rt) {
          state.fb_tx[state.cur_fb] = 0; // ahora se dibuja en el buffer grande
          rt_gen++;
        }
        if (state.cur_rt) {
          st.rt_draws++;
          state.cur_rt->draws++;
        }

        if ((state.vertex_type & GE_VTYPE_THROUGH_MASK) == GE_VTYPE_THROUGH) {
          u8 num_points_u = data & 0xff;
          u8 num_points_v = (data >> 8) & 0xff;

          u32 count = num_points_u * num_points_v;

          u8 vertex_size = 0, pos_off = 0, tc_off = 0;
          vertexInfo(state.vertex_type, &vertex_size, &pos_off, &tc_off);

          AdvanceVerts(count, vertex_size);
        }

        break;
      }

      case GE_CMD_BOUNDINGBOX: // (en 2D avanza VADDR: queda en el estado guardado)
      {
        if (state.ignore_framebuf)
          break;

        if ((state.vertex_type & GE_VTYPE_THROUGH_MASK) == GE_VTYPE_THROUGH) {
          u32 count = data;

          u8 vertex_size = 0, pos_off = 0, tc_off = 0;
          vertexInfo(state.vertex_type, &vertex_size, &pos_off, &tc_off);

          AdvanceVerts(count, vertex_size);
        }

        break;
      }

      case GE_CMD_PRIM:
      {
        state.has_draws = 1;
        draws_since_flip++;

        // Dragon Ball Z Tenkaichi Tag Team uses the same GE list again,
        // therefore NOPing it makes character invisible.
        if (!passive && (*list >> 24) == GE_CMD_CALL)
          primUnCall(list); // era un CALL de txDrawFromScreen: la palabra de antes vuelve a ser la del juego
        if (state.ignore_framebuf && (state.ge_cmd[GE_CMD_TEXTUREMAPENABLE] & 1) && (state.ge_cmd[GE_CMD_TEXFORMAT] & 0xf) <= 3) {
          disp_tex = toFake(((state.ge_cmd[GE_CMD_TEXBUFWIDTH0] & 0x0f0000) << 8) | (state.ge_cmd[GE_CMD_TEXADDR0] & 0xffffff));
          disp_tex_w = state.ge_cmd[GE_CMD_TEXBUFWIDTH0] & 0x7ff;
          disp_tex_fmt = state.ge_cmd[GE_CMD_TEXFORMAT] & 3;
        }
        int texmode = state.ignore_framebuf ? 0 : texCheck();
        int omit = state.ignore_framebuf || texmode == 1;
        if (capturing)
          capDraw(cmd == GE_CMD_PRIM ? (data >> 16) & 7 : cmd, cmd == GE_CMD_PRIM ? data & 0xffff : (data & 0xff) * ((data >> 8) & 0xff),
                  omit | (texmode == 2 ? 4 : 0) | (state.cur_rt ? 8 : 0) | (texmode == 5 ? 16 : 0) |
                  (state.cur_rt == &csrc_rt ? 32 : 0));
        if (dumping)
          dumpDraw(list, op, data & 0xffff, texmode, omit);
        if (omit || texmode) {
          DIRTY(W_OMIT); // draw omitido o con textura de render target: hay que verlo cada vez
        }
        if (texmode == 6) { // mezcla del pase por intercambio
          swap_on = 0;
          swap_done = 1;
          rt_gen++;
          if (swapComposite(list, op, data & 0xffff)) {
            LOG_ONCE(0x5A000000 ^ state.framebuf_addr[swap_fb], "fb %08lx: mezcla con la escena guardada en la RAM (intercambio)\n", state.framebuf_addr[swap_fb]);
            break;
          }
          LOG_ONCE(0x5A100000 ^ state.framebuf_addr[swap_fb], "fb %08lx: mezcla que no se puede intercambiar -> se omite y no se intenta mas\n", state.framebuf_addr[swap_fb]);
          state.fb_swap[swap_fb] = 0;
          omit = 1;
        }
        if (omit && texmode == 1 && opt.swap && !passive && !rt_enabled)
          swapLearn(data & 0xffff);
        if (omit) {
          setOp(list, op, NOP_OMIT);
          st.skipped++;
          if (state.cur_rt && !state.cur_rt->incomplete) {
            state.cur_rt->incomplete = 1;
            rt_gen++;
          }
          break;
        }
        if (*list != op && !passive) { // se habia anulado en una pasada anterior: restaurar
          *list = op;
          DIRTY(W_OMIT);
        }
        if (!state.cur_rt)
          state.big_fb = state.cur_fb + 1; // lo que hay en el buffer grande es de este fb
        if (texmode == 5 && !txDrawFromScreen(list, op)) {
          LOG_ONCE_CAP(0x7E200000 ^ (u32)list, "draw con la pantalla como textura en rt %08lx: no se pudo reemplazar -> se omite\n",
                       state.cur_rt->addr);
          setOp(list, op, NOP_OMIT);
          st.skipped++;
          if (!state.cur_rt->incomplete) {
            state.cur_rt->incomplete = 1;
            rt_gen++;
          }
          u8 vs = 0, po = 0, to = 0;
          vertexInfo(state.vertex_type, &vs, &po, &to);
          AdvanceVerts(data & 0xffff, vs);
          break;
        }
        st.draws++;
        if (!state.cur_rt && (state.ge_cmd[GE_CMD_CLEARMODE] & 0x101) != 0x001) // un clear sin color no cuenta
          color_draws++;
        if (state.cur_fb >= 0 && state.fb_tx[state.cur_fb] && !state.cur_rt) {
          state.fb_tx[state.cur_fb] = 0; // ahora se dibuja en el buffer grande
          rt_gen++;
        }
        if (state.cur_rt) {
          st.rt_draws++;
          state.cur_rt->draws++;
        }

        u16 count = data & 0xffff;
        u8 vertex_size = 0, pos_off = 0, tc_off = 0;
        vertexInfo(state.vertex_type, &vertex_size, &pos_off, &tc_off);

        // El GE avanza VADDR/IADDR despues de cada draw, este o no en modo through.
        // Antes solo se avanzaba en through, y un draw 2D posterior sin VADDR propio
        // parcheaba (duplicaba) los vertices del draw 3D anterior.
        int indexed = (state.vertex_type & GE_VTYPE_IDX_MASK) != GE_VTYPE_IDX_NONE;
        if ((state.vertex_type & GE_VTYPE_THROUGH_MASK) != GE_VTYPE_THROUGH ||
            !validAddr(state.vertex_addr) || (indexed && !validAddr(state.index_addr))) {
          AdvanceVerts(count, vertex_size);
          break;
        }

        {
          DIRTY(W_2D); // 2D: vertices que se duplican
          u16 lower = 0;
          u16 upper = count;
          if (indexed) {
            GetIndexBounds((void *)state.index_addr, count, state.vertex_type, &lower, &upper);
            upper += 1;
          }
          if (!validAddr(state.vertex_addr + upper * vertex_size))
            goto exit_loop;

          int pos = (state.vertex_type & GE_VTYPE_POS_MASK) >> GE_VTYPE_POS_SHIFT;
          int pos_size = possize[pos] / 3;
          u32 start = state.vertex_addr + lower * vertex_size, n = upper - lower;

          // Si estos vertices ya se duplicaron y el juego no los reescribio, no tocarlos
          // (los buffers estaticos se reutilizan cada frame)
          if (passive || !opt.v2d)
            goto exit_loop;
          // En un render target a escala 1 las coordenadas quedan como las dejo el juego. Si
          // ya se habian duplicado (el mismo buffer se dibujo antes en la pantalla, como el
          // formulario de Silent Hill al pasar al fb de copia) se vuelven a la mitad.
          int known = vertsPatched(start, n, vertex_size, pos_off, pos_size), native = SCALE() == 1;
          if (known == (native ? 2 : 1)) {
            st.v2d_skip++;
            goto exit_loop;
          }
          st.v2d++;
          if (native) {
            if (known == 1)
              vertsHalve(start, n, vertex_size, pos_off, pos_size);
            vertsRemember(start, n, vertsHash(start, n, vertex_size, pos_off, pos_size), VM_NATIVE);
            goto exit_loop;
          }

          // En 2D las coordenadas de textura estan en texeles: si la textura es un
          // render target (ahora al doble) tambien se duplican
          int tc = (state.vertex_type & GE_VTYPE_TC_MASK) >> GE_VTYPE_TC_SHIFT;
          if (texmode == 2 && tc >= 2 && RT_SCALE == 2) {
            u32 va = start, k;
            for (k = 0; k < n; k++, va += vertex_size) {
              if (tc == 2) {
                *(u16 *)(va + tc_off) *= 2;
                *(u16 *)(va + tc_off + 2) *= 2;
              } else {
                t.i = *(u32 *)(va + tc_off); t.f *= 2; *(u32 *)(va + tc_off) = t.i;
                t.i = *(u32 *)(va + tc_off + 4); t.f *= 2; *(u32 *)(va + tc_off + 4) = t.i;
              }
            }
          }

          u32 vertex_addr = start, i;
          for (i = 0; i < n; i++, vertex_addr += vertex_size) {
            int j;
            for (j = 0; j < 2; j++) {
              u32 addr = vertex_addr + pos_off + j * pos_size;
              int d = j ? state.offy : state.offx; // desplazamiento del area visible (menu / auto)
              if (pos_size == 2) {
                short val = *(short *)addr - d;
                if (val == 480 || val == 960)
                  *(short *)addr = 960;
                else if (val == 272 || val == 544)
                  *(short *)addr = 544;
                else if (val > -2048 && val < 2048)
                  *(short *)addr = val * 2;
              } else if (pos_size == 4) {
                t.i = *(u32 *)addr;
                t.f -= d;
                if (t.f == 480 || t.f == 960)
                  t.f = 960;
                else if (t.f == 272 || t.f == 544)
                  t.f = 544;
                else if (t.f > -2048 && t.f < 2048)
                  t.f *= 2;
                *(u32 *)addr = t.i;
              }
            }
          }
          vertsRemember(start, n, vertsHash(start, n, vertex_size, pos_off, pos_size), 0);

exit_loop:
          if (dumping && !passive && validAddr(state.vertex_addr + upper * vertex_size)) // como quedaron (duplicados o no)
            dumpBlob(B_VERTS_FINAL, state.vertex_addr, upper * vertex_size);
          AdvanceVerts(count, vertex_size);
        }

        break;
      }

      // Patch GE commands

      // case GE_CMD_DITHERENABLE:
        // *list = (cmd << 24) | 1;
        // break;

      case GE_CMD_FRAMEBUFPIXFORMAT:
        if (data != PIXELFORMAT)
          LOG_ONCE(0xE4000000 | data, "el juego pide formato de fb %lu (se fuerza 565)\n", data);
        state.fmt_p = list; state.fmt_o = op; state.fmt_new = 1; state.fmt_live = PIXELFORMAT;
        if (state.cur_rt == &csrc_rt) { // dentro del tramo del fb de copia: el formato del juego
          setOp(list, op, op);
          csrc_rt.fmt = state.fmt_live = data & 3;
          break;
        }
        setOp(list, op, (cmd << 24) | PIXELFORMAT);
        break;

      case GE_CMD_FRAMEBUFPTR:
      case GE_CMD_FRAMEBUFWIDTH:
      {
        if (cmd == GE_CMD_FRAMEBUFPTR) {
          setOp(list, op, (cmd << 24) | (VRAM_DRAW_BUFFER_OFFSET & 0xffffff));
          state.framebufptr = op;
          state.framebufptr_addr = list;
        } else {
          u16 pitch = ((op & 0xffff) == 512 || (op & 0xffff) == 480 || wideScreen(op & 0xffff)) ? 960 : (op & 0xffff);
          setOp(list, op, (cmd << 24) | ((VRAM_DRAW_BUFFER_OFFSET >> 24) << 16) | pitch);
          state.framebufwidth = op;
          state.framebufwidth_addr = list;
        }

        if (state.framebufptr && state.framebufwidth) {
          state.framebuf = FAKE_VRAM | (state.framebufptr & 0xffffff);

          // This allows more games to work, but causes weird triangles in Sonic.
          u32 pitch = state.framebufwidth & 0xffff;
          // 480 tambien es un buffer de pantalla completa (Ridge Racer lo usa al iniciar)
          RenderTarget *r = NULL;
          int was_big = !state.cur_rt && !state.ignore_framebuf;
          csrcEnd();
          state.cur_rt = NULL;
          state.offx = state.offy = 0;
          int fi = findFramebuf(state.framebuf), layer = opt.layers && fi >= 0 && state.fb_layer[fi];
          u32 gfmt = state.ge_cmd[GE_CMD_FRAMEBUFPIXFORMAT] & 3; // formato que pidio el juego
          int fmt_ok = gfmt == PIXELFORMAT || (state.fmt_new && canRepatch(state.fmt_p, state.fmt_o));
          if (opt.csrc && fi >= 0 && state.fb_csrc[fi] && state.fb_cfail[fi] < 3 && !csrc_copied && (pitch == 512 || pitch == 480) &&
              fmt_ok && !passive && copyVram() && canRepatch(state.framebufptr_addr, state.framebufptr) &&
              canRepatch(state.framebufwidth_addr, state.framebufwidth)) {
            // fb del que el juego copia: a escala 1 y en su formato en el buffer temporal,
            // hasta la copia (txFromCsrc), que vuelve al buffer grande
            setOp(state.framebufptr_addr, state.framebufptr, (GE_CMD_FRAMEBUFPTR << 24) | (copy_vram & 0xffffff));
            setOp(state.framebufwidth_addr, state.framebufwidth, (GE_CMD_FRAMEBUFWIDTH << 24) | ((copy_vram >> 24) << 16) | pitch);
            if (gfmt != PIXELFORMAT) {
              setOp(state.fmt_p, state.fmt_o, state.fmt_o); // el formato original del juego
              state.fmt_live = gfmt;
            }
            memset(&csrc_rt, 0, sizeof(csrc_rt));
            csrc_rt.addr = state.framebuf; csrc_rt.pitch = pitch; csrc_rt.vram = copy_vram;
            csrc_rt.w = pitch; csrc_rt.h = 272; csrc_rt.sc = 1; csrc_rt.fmt = gfmt;
            state.cur_rt = &csrc_rt;
            state.ignore_framebuf = 0;
            csrc_fb = fi;
            LOG_ONCE_CAP(state.framebuf ^ 0x7C000000, "fb %08lx: se dibuja a escala 1 (formato %lu) hasta la copia desde el\n",
                         state.framebuf, gfmt);
          } else if ((pitch == 512 || pitch == 480 || pitch == 960 || wideScreen(pitch)) && !layer) {
            state.ignore_framebuf = 0;
            state.offx = opt.dx;
            state.offy = opt.dy;
            // Una vez por lista del juego (no por flip: a 18 fps a veces llegan dos listas entre
            // dos flips y la segunda salia sin mezcla: la puerta parpadeaba)
            if (opt.swap && !passive && fi >= 0 && state.fb_swap[fi] && (!swap_on || swap_list != (int)(st.lists + st.heads)) &&
                !swap_done && was_big && state.big_fb && state.big_fb != fi + 1 && swapEnter(list)) {
              swap_on = 1;
              swap_fb = fi;
              swap_flips = 0;
              swap_list = st.lists + st.heads;
              rt_gen++; // texCheck: la mezcla cambia de decision
              LOG_ONCE(0x5A200000 ^ state.framebuf, "fb %08lx: la escena (fb %08lx) se guarda en la RAM y el pase va al buffer grande\n",
                           state.framebuf, state.framebuf_addr[state.big_fb - 1]);
            }
          } else if (rt_enabled && opt.rt_on && pitch >= 16 && (r = rtGet(state.framebuf, pitch)) &&
                     canRepatch(state.framebufptr_addr, state.framebufptr) &&
                     canRepatch(state.framebufwidth_addr, state.framebufwidth)) {
            // Render target: se dibuja al doble en su espacio de la VRAM alta
            setOp(state.framebufptr_addr, state.framebufptr, (GE_CMD_FRAMEBUFPTR << 24) | (r->vram & 0xffffff));
            setOp(state.framebufwidth_addr, state.framebufwidth, (GE_CMD_FRAMEBUFWIDTH << 24) | ((r->vram >> 24) << 16) | r->w);
            state.ignore_framebuf = 0;
            state.cur_rt = r;
            if (r->incomplete)
              rt_gen++;
            r->incomplete = 0;
            r->frame = log_frame;
          } else {
            if (r)
              LOG_ONCE(state.framebuf ^ 0xCA000000, "rt: no se encontraron los comandos de fb de %08lx -> se omite\n", state.framebuf);
            setOp(state.framebufwidth_addr, state.framebufwidth, (GE_CMD_FRAMEBUFWIDTH << 24) | ((VRAM_1KB >> 24) << 16) | 0);
            state.ignore_framebuf = 1;
          }

          if (passive) // el plugin no actua: indicar que haria con este framebuffer
            LOG_ONCE(state.framebuf ^ (pitch << 20) ^ 7, "fb %08lx pitch=%lu%s\n", state.framebuf, pitch,
                     (pitch == 512 || pitch == 480 || pitch == 960) ? "" : pitch >= 16 ? " (activo: seria render target)" : " (activo: se ignoraria)");
          else
            LOG_ONCE(state.framebuf ^ (pitch << 20) ^ 7, "fb %08lx pitch=%lu%s\n", state.framebuf, pitch,
                     state.cur_rt ? " -> render target" : state.ignore_framebuf ? " -> ignorado (no se dibuja)" : "");
          state.cur_fb = insertFramebuf(state.framebuf);
          if (dumping) {
            u32 d[8] = { state.framebuf, pitch, gfmt, state.cur_rt == &csrc_rt ? 2 : state.cur_rt ? 1 : state.ignore_framebuf ? 3 : 0,
                         state.cur_rt ? state.cur_rt->vram : VRAM_DRAW_BUFFER_OFFSET, state.cur_rt ? state.cur_rt->w : PITCH,
                         SCALE(), (u32)state.framebufptr_addr };
            dumpRecord(GPD_FB, d, sizeof(d), NULL, 0);
          }
          // El formato del juego (8888) que se dejo para el fb de copia sigue puesto en el GE
          // si este fb no trae su FRAMEBUFPIXFORMAT: ya no se puede corregir
          if (state.fmt_live != PIXELFORMAT && state.cur_rt != &csrc_rt) {
            LOG_ONCE_CAP(0x7C100000, "fb %08lx sin FRAMEBUFPIXFORMAT tras el fb de copia: formato equivocado -> no se usa mas\n", state.framebuf);
            if (csrc_fb >= 0)
              state.fb_cfail[csrc_fb] = 255;
            state.fmt_live = PIXELFORMAT;
          }
          state.fmt_new = 0;
          vramUse(state.framebuf + pitch * 272 * ((state.ge_cmd[GE_CMD_FRAMEBUFPIXFORMAT] & 3) == 3 ? 4 : 2) - 1);

          state.framebufptr = 0;
          state.framebufwidth = 0;
        }

        break;
      }

      case GE_CMD_ZBUFPTR:
        setOp(list, op, (cmd << 24) | (VRAM_DEPTH_BUFFER_OFFSET & 0xffffff));
        break;

      case GE_CMD_ZBUFWIDTH:
        setOp(list, op, (cmd << 24) | ((VRAM_DEPTH_BUFFER_OFFSET >> 24) << 16) | PITCH);
        break;

      // Comandos de textura: se dejan con su valor original y se recuerda donde estan;
      // si el draw usa un render target, texCheck los redirige
      case GE_CMD_TEXADDR0:
      case GE_CMD_TEXBUFWIDTH0:
      case GE_CMD_TEXSIZE0:
      case GE_CMD_TEXFORMAT:
      {
        int k = cmd == GE_CMD_TEXADDR0 ? 0 : cmd == GE_CMD_TEXBUFWIDTH0 ? 1 : cmd == GE_CMD_TEXSIZE0 ? 2 : 3;
        rec.touch |= T_TADDR << k;
        if (*list != op && !passive)
          *list = op;
        state.tex_p[k] = list;
        state.tex_o[k] = op;
        break;
      }

      case GE_CMD_LOADCLUT: // volcado: la paleta que se carga
        if (dumping) {
          u32 a = ((state.ge_cmd[GE_CMD_CLUTADDRUPPER] & 0x0f0000) << 8) | (state.ge_cmd[GE_CMD_CLUTADDR] & 0xffffff);
          u32 n = ((data & 0x7f) == 0x40 ? 0x40 : (data & 0x3f)) * 32;
          if ((a & 0x0f000000) == 0x04000000 && !passive)
            a = toFake(a);
          if (n && validAddr(a) && validAddr(a + n - 1))
            dumpBlobOnce(B_CLUT, a, n);
        }
        break;

      // Copias de bloque (sceGuCopyImage). No pasan por los vertices, asi que no se
      // ven en el buffer de alta resolucion. Videos y logos suelen dibujarse asi.
      case GE_CMD_TRANSFERSTART:
      {
        DIRTY(W_COPY);
        u32 rdst = ((state.ge_cmd[GE_CMD_TRANSFERDSTW] & 0xff0000) << 8) | (state.ge_cmd[GE_CMD_TRANSFERDST] & 0xffffff);
        u32 rsrc = ((state.ge_cmd[GE_CMD_TRANSFERSRCW] & 0xff0000) << 8) | (state.ge_cmd[GE_CMD_TRANSFERSRC] & 0xffffff);
        u32 dst = toFake(rdst), src = toFake(rsrc);
        u32 w = (state.ge_cmd[GE_CMD_TRANSFERSIZE] & 0x3ff) + 1;
        u32 h = ((state.ge_cmd[GE_CMD_TRANSFERSIZE] >> 10) & 0x3ff) + 1;
        st.transfers++;
        u32 pitch = state.ge_cmd[GE_CMD_TRANSFERDSTW] & 0x7ff, bpp = (data & 1) ? 4 : 2;
        if (capturing) {
          capFlush();
          LOG("  copia %08lx (ancho %lu) -> %08lx (ancho %lu) %lux%lu %lu bpp\n", rsrc, state.ge_cmd[GE_CMD_TRANSFERSRCW] & 0x7ff,
              rdst, pitch, w, h, bpp * 8);
        }
        if (dumping) { // origen (si esta en la RAM) y destino (se guarda al terminar)
          u32 sp = state.ge_cmd[GE_CMD_TRANSFERSRCPOS], dp = state.ge_cmd[GE_CMD_TRANSFERDSTPOS];
          u32 sw = state.ge_cmd[GE_CMD_TRANSFERSRCW] & 0x7ff;
          u32 sb = ((((sp >> 10) & 0x3ff) + h - 1) * sw + (sp & 0x3ff) + w) * bpp;
          u32 db = ((((dp >> 10) & 0x3ff) + h - 1) * pitch + (dp & 0x3ff) + w) * bpp;
          if ((src & 0x0f000000) != 0x04000000 && validAddr(src) && validAddr(src + sb - 1))
            dumpBlob(B_XFER_SRC, src, sb);
          if (validAddr(dst) && validAddr(dst + db - 1))
            dumpCopyDst(dst, db);
        }
        if (!passive && opt.vram_fix) {
          if (txFromCsrc(list, src, dst, bpp) || txFromScreen(list, src, dst, w, h, bpp))
            break;
          // Copia que usa la VRAM real: se lleva a la VRAM falsa, donde estan los datos del juego
          u32 *ps = rsrc != src ? txFind(list, GE_CMD_TRANSFERSRCW) : NULL, *pd = rdst != dst ? txFind(list, GE_CMD_TRANSFERDSTW) : NULL;
          if (ps || pd) {
            if (ps)
              setOp(ps, origOp(ps), (GE_CMD_TRANSFERSRCW << 24) | ((src >> 8) & 0xff0000) | (state.ge_cmd[GE_CMD_TRANSFERSRCW] & 0xffff));
            if (pd)
              setOp(pd, origOp(pd), (GE_CMD_TRANSFERDSTW << 24) | ((dst >> 8) & 0xff0000) | (pitch & 0xffff));
            st.vram_fix++;
            LOG_ONCE_CAP(0xE9000000 ^ rsrc ^ rdst, "copia con VRAM real %08lx -> %08lx: se usa la VRAM falsa\n", rsrc, rdst);
          }
        }
        vramUse(dst + pitch * h * bpp - 1);
        int f = inFramebuf(dst) - 1;
        if (f >= 0) {
          st.fb_transfers++;
          if (w * h >= 240 * 136 && pitch >= 480 && !(opt.tx_ge && txUpscale(list, dst, w, h, bpp))) {
            big_transfer = 1;
            big_tx.addr = dst; big_tx.w = pitch;
            big_tx.fmt = (data & 1) ? 3 : state.ge_cmd[GE_CMD_FRAMEBUFPIXFORMAT] & 3;
          }
          if (!state.fb_tx[f]) { // su contenido valido esta en la VRAM falsa (ver texCheckCalc)
            state.fb_tx[f] = 1;
            rt_gen++;
          }
          LOG_ONCE(dst ^ (w << 20) ^ h, "transfer a fb %08lx %lux%lu\n", dst, w, h);
        }
        break;
      }

      // Viewport, offset y scissor: se escalan x2 a partir de los valores del juego.
      // Antes se forzaban a pantalla completa, y lo que el juego dibuja en una zona
      // (espejo retrovisor, HUD 3D, pantalla dividida) salia agrandado y fuera de lugar.
      // OFFSET queda fijo para que el area de dibujo siga centrada en 2048.
      // En un render target se usa su escala (RT_SCALE) en vez de 2.
      case GE_CMD_VIEWPORTXSCALE:
      case GE_CMD_VIEWPORTYSCALE:
      case GE_CMD_VIEWPORTXCENTER:
      case GE_CMD_VIEWPORTYCENTER:
        setOp(list, op, (cmd << 24) | vpPatch(cmd, data));
        break;

      case GE_CMD_OFFSETX:
        game_offx = (data & 0xffff) / 16.0f;
        setOp(list, op, (cmd << 24) | ((2048 - (WIDTH / 2)) << 4));
        break;

      case GE_CMD_OFFSETY:
        game_offy = (data & 0xffff) / 16.0f;
        setOp(list, op, (cmd << 24) | ((2048 - (HEIGHT / 2)) << 4));
        break;

      case GE_CMD_REGION1:
      case GE_CMD_SCISSOR1:
      case GE_CMD_REGION2:
      case GE_CMD_SCISSOR2:
        setOp(list, op, (cmd << 24) | scPatch(cmd, data));
        break;

      default:
        break;
    }

    if (capturing && capName(cmd) && cap_last[cmd] != (data | 0x80000000)) {
      cap_last[cmd] = data | 0x80000000;
      capFlush();
      if ((*list & 0xffffff) != data)
        LOG("  %s %06lx -> %06lx\n", capName(cmd), data, *list & 0xffffff);
      else
        LOG("  %s %06lx\n", capName(cmd), data);
    }
    if (dumping)
      dumpCmd(cur, op);
  }

finish:
  st.cmds += ncmds;
  // Escribe los parches a memoria y descarta la cache: muchos juegos escriben sus listas
  // por la direccion sin cache, y sin invalidar se leerian comandos viejos la proxima vez
  sceKernelDcacheWritebackInvalidateAll();
}

void *(* _sceGeEdramGetAddr)(void);
unsigned int *(* _sceGeEdramGetSize)(void);
int (* _sceGeGetList)(int qid, void *list, int *flag);
int (* _sceGeListUpdateStallAddr)(int qid, void *stall);
int (* _sceGeListEnQueue)(const void *list, void *stall, int cbid, PspGeListArgs *arg);
int (* _sceGeListEnQueueHead)(const void *list, void *stall, int cbid, PspGeListArgs *arg);

int (* _sceDisplaySetFrameBuf)(void *topaddr, int bufferwidth, int pixelformat, int sync);

void *sceGeEdramGetAddrPatched(void) {
  return (void *)FAKE_VRAM;
}

unsigned int sceGeEdramGetSizePatched(void) {
  return 4 * 1024 * 1024;
}

void guStart(int cid, void *list); // gu.c

// Copia el buffer de alta resolucion al que muestra Adrenaline, con una lista propia del GE.
// Se alternan dos listas: con una sola, una copia nueva podia reescribir la lista
// mientras el GE aun ejecutaba la anterior (imagen corrupta o cuelgue).
// Dibujo de la CPU a pegar en la copia final (ver cpuOverlay)
#define OVL_BUF 0x0AF40000 // hasta 192x192 al doble en 565
#define OVL_MAX 96
static struct { int x, y, w, h; } ovl;

void copyFrameBuffer(int menu) {
  static int which;
  u32 rl = RENDER_LIST + (which ^= 1) * 0x1000;
  *(u32 *)DRAW_NATIVE = 1;

  guStart(0, (void *)(rl | 0xA0000000));
  // El menu se pone en el buffer grande antes de copiarlo, en la misma lista: si se copiaba
  // despues, Adrenaline a veces mostraba la imagen entre las dos copias (menu parpadeando)
  if (menu)
    sceGuCopyImage(PIXELFORMAT, 0, 0, MENU_W, MENU_H, MENU_W, (void *)MENU_BUF, MENU_X, MENU_Y, PITCH, (void *)VRAM_DRAW_BUFFER_OFFSET);
  sceGuCopyImage(PIXELFORMAT, 0, 0, WIDTH, HEIGHT, PITCH,
                 (void *)VRAM_DRAW_BUFFER_OFFSET,
                 0, 0, PITCH, (void *)DISPLAY_BUFFER);
  if (ovl.w) { // lo que dibujo la CPU (ver cpuOverlay), encima de la imagen final
    sceGuCopyImage(PIXELFORMAT, 0, 0, ovl.w * 2, ovl.h * 2, ovl.w * 2, (void *)OVL_BUF, ovl.x * 2, ovl.y * 2, PITCH, (void *)DISPLAY_BUFFER);
    ovl.w = 0;
  }
  sceGuFinish();
  _sceGeListEnQueue((void *)rl, NULL, -1, NULL);
}

int sceGeListUpdateStallAddrPatched(int qid, void *stall) {
  int k1 = pspSdkSetK1(0);
  u32 t0 = sceKernelGetSystemTimeLow();
  st.stalls++;
  char info[64];
  if (_sceGeGetList(qid, info, NULL) == 0) {
    u16 state = *(u16 *)(info + 0x08);
    if (state != 3) { // completed
      void *list = *(void **)(info + 0x18); // previous stall
      if (!list)
        list = *(void **)(info + 0x14); // list
      dumpOnList((u32)list, (u32)stall, 2);
      if ((walk_now || dumping) && ((u32)list & 0x0fffffff) < ((u32)stall & 0x0fffffff)) {
        patchGeList((u32 *)((u32)list & 0x0fffffff), (u32 *)((u32)stall & 0x0fffffff));
      }
    }
  }
  patch_us_frame += sceKernelGetSystemTimeLow() - t0;
  pspSdkSetK1(k1);
  return _sceGeListUpdateStallAddr(qid, stall);
}

int sceGeListEnQueuePatched(const void *list, void *stall, int cbid, PspGeListArgs *arg) {
  st.lists++;
  swap_done = 0; // lista nueva del juego: el pase se puede intercambiar otra vez
  if (capturing) {
    capFlush();
    LOG(" lista %08lx\n", (u32)list);
  }
  // Juegos con un solo buffer casi no llaman sceDisplaySetFrameBuf: para ellos se copia aqui.
  // Antes se copiaba al encolar cada lista, y en juegos con varias listas por frame eso
  // mostraba frames a medio dibujar (parpadeo, tearing). Ahora solo si no hubo flip en 100 ms.
  if (!passive && state.has_draws && sceKernelGetSystemTimeLow() - last_flip > 100000) {
    copyFrameBuffer(menu_shown);
    st.enq_copies++;
  }
  u32 t0 = sceKernelGetSystemTimeLow();
  resetGeState();
  if (dumpOnList((u32)list, (u32)stall, 0) || walk_now)
    patchGeList((u32 *)((u32)list & 0x0fffffff), (u32 *)((u32)stall & 0x0fffffff));
  patch_us_frame += sceKernelGetSystemTimeLow() - t0;
  return _sceGeListEnQueue(list, stall, cbid, arg);
}

int sceGeListEnQueueHeadPatched(const void *list, void *stall, int cbid, PspGeListArgs *arg) {
  st.heads++;
  swap_done = 0;
  u32 t0 = sceKernelGetSystemTimeLow();
  resetGeState();
  if (dumpOnList((u32)list, (u32)stall, 1) || walk_now)
    patchGeList((u32 *)((u32)list & 0x0fffffff), (u32 *)((u32)stall & 0x0fffffff));
  patch_us_frame += sceKernelGetSystemTimeLow() - t0;
  return _sceGeListEnQueueHead(list, stall, cbid, arg);
}

// Copia el frame de alta resolucion, reducido a 480x272, al fb que el juego acaba de
// mostrar (en la VRAM falsa), para los efectos que lo leen como textura. Se hace con una
// lista propia del GE: 4 sprites con filtro lineal hacia un buffer temporal en la VRAM
// alta, y una copia de bloque de ahi al fb del juego. El GE guarda y restaura su estado
// (contexto de la lista), asi que las listas del juego no notan nada.
// Lista propia con contexto (el GE guarda y restaura su estado: las listas del juego no
// notan nada). El contexto esta en la memoria del plugin (kernel): con el k1 del juego (se
// llama desde el flip) sceGeListEnQueue lo rechazaba y la lista nunca se ejecutaba (el
// spinner de Silent Hill no aparecia).
static void geEnqueueCtx(u32 rl, PspGeContext *ctx, const char *what) {
  PspGeListArgs args = { 8, ctx };
  int k1 = pspSdkSetK1(0);
  int r = _sceGeListEnQueue((void *)rl, NULL, -1, &args);
  pspSdkSetK1(k1);
  if (r < 0)
    LOG_ONCE(0x5B000000 ^ (u32)r, "lista propia (%s) rechazada por el GE: %08x\n", what, r);
}

static void fbCopyDown(u32 dst, int width, int fmt) {
  static PspGeContext ctx;
  static int which;
  if (!copyVram())
    return;
  u32 rl = RENDER_LIST + 0x2000 + (which ^= 1) * 0x1000, *l = (u32 *)(rl | 0x40000000); // sin cache
  l = emitDownscale(l, fmt);
  #define C(c, d) (*l++ = ((c) << 24) | ((d) & 0xffffff))
  C(GE_CMD_TRANSFERSRC, copy_vram); C(GE_CMD_TRANSFERSRCW, ((copy_vram >> 24) << 16) | 512);
  C(GE_CMD_TRANSFERDST, dst); C(GE_CMD_TRANSFERDSTW, ((dst >> 24) << 16) | width);
  C(GE_CMD_TRANSFERSRCPOS, 0); C(GE_CMD_TRANSFERDSTPOS, 0);
  C(GE_CMD_TRANSFERSIZE, (271 << 10) | 479);
  C(GE_CMD_TRANSFERSTART, fmt == 3);
  C(GE_CMD_FINISH, 0); C(GE_CMD_END, 0);
  #undef C
  geEnqueueCtx(rl, &ctx, "copia reducida al fb mostrado");
  st.fb_copies++;
}

// Lo que la CPU del juego dibuja directo en el fb mostrado (el spinner de carga de Silent
// Hill) queda en la VRAM falsa, y la pantalla sale del buffer grande: no se veia. Antes se
// pegaba con una lista propia con contexto, pero en Adrenaline encolarla congelaba el juego
// (c1-2858-3). Ahora la CPU busca el recuadro con algo dibujado, lo duplica en OVL_BUF y lo
// borra del fb (como lo tapa el juego al pegar el frame siguiente); la lista de la copia
// final lo pone encima con una copia de bloque (no cambia el estado de dibujo del GE). El
// negro alrededor del dibujo se copia tambien: el recuadro es chico.
static void cpuOverlay(u32 src, int width, int fmt) {
  int x, y, x0 = 480, y0 = 272, x1 = -1, y1 = -1;
  u16 *fb = (u16 *)src;
  ovl.w = 0;
  if (fmt != 0) // solo 565 (el de Silent Hill)
    return;
  sceKernelDcacheWritebackInvalidateRange(fb, width * 272 * 2);
  for (y = 0; y < 272; y += 2) // a saltos: el dibujo es mas grande que 2 px
    for (x = 0; x < 480; x += 2)
      if (fb[y * width + x] | fb[y * width + x + 1]) {
        if (x < x0) x0 = x;
        if (x > x1) x1 = x;
        if (y < y0) y0 = y;
        if (y > y1) y1 = y;
      }
  if (x1 < 0)
    return;
  x0 = x0 > 2 ? x0 - 2 : 0; y0 = y0 > 2 ? y0 - 2 : 0; // margen por los saltos
  x1 = x1 + 4 < 480 ? x1 + 4 : 480; y1 = y1 + 4 < 272 ? y1 + 4 : 272;
  if (x1 - x0 > OVL_MAX || y1 - y0 > OVL_MAX) { // no es un spinner: no se toca
    LOG_ONCE(0xB3000000 ^ src, "fb mostrado %08lx: dibujo de la CPU de %dx%d (muy grande, no se pega)\n", src, x1 - x0, y1 - y0);
    return;
  }
  ovl.x = x0; ovl.y = y0; ovl.w = (x1 - x0 + 3) & ~3; ovl.h = y1 - y0; // ancho al doble multiplo de 8
  if (ovl.x + ovl.w > 480)
    ovl.x = 480 - ovl.w;
  u16 *d = (u16 *)(OVL_BUF | 0x40000000);
  for (y = 0; y < ovl.h; y++) {
    u16 *row = fb + (ovl.y + y) * width + ovl.x, *o = d + y * 2 * ovl.w * 2;
    for (x = 0; x < ovl.w; x++)
      o[2 * x] = o[2 * x + 1] = o[2 * ovl.w + 2 * x] = o[2 * ovl.w + 2 * x + 1] = row[x];
    memset(row, 0, ovl.w * 2);
  }
  sceKernelDcacheWritebackRange(fb, width * 272 * 2);
  LOG_ONCE(0xB2100000 ^ src, "fb mostrado %08lx: dibujo de la CPU en (%d,%d) %dx%d -> se pega al doble\n", src, ovl.x, ovl.y, ovl.w, ovl.h);
  st.cpu_overlays++;
}

// Memoria y CPU en este momento (al iniciar, en cada estadistica y desde el menu)
void memSnapshot(const char *why) {
  static int (*freeTotal)(void), (*freeMax)(void), (*cpu)(void), (*bus)(void), init;
  if (!init) {
    init = 1;
    freeTotal = (void *)FindProc("sceSystemMemoryManager", "SysMemUserForUser", 0xF919F628);
    freeMax = (void *)FindProc("sceSystemMemoryManager", "SysMemUserForUser", 0xA291F107);
    cpu = (void *)FindProc("scePower_Service", "scePower", 0xFDB5BFE9);
    bus = (void *)FindProc("scePower_Service", "scePower", 0xBD681969);
  }
  int k1 = pspSdkSetK1(0);
  int ft = freeTotal ? freeTotal() / 1024 : -1, fm = freeMax ? freeMax() / 1024 : -1;
  int c = cpu ? cpu() : -1, b = bus ? bus() : -1;
  pspSdkSetK1(k1);
  LOG("memoria (%s): ram del juego libre=%d KB (bloque mayor %d KB) | cpu=%d MHz bus=%d MHz\n", why, ft, fm, c, b);
  LOG("memoria (%s): vram del juego usada hasta %lu KB de 4096 | vram real: pantalla+z 2040 KB, rt+copia %lu KB, libre %lu KB\n",
      why, vram_hi ? (vram_hi - FAKE_VRAM) / 1024 + 1 : 0, (rt_next - RT_BASE) / 1024, rt_enabled ? (RT_END - rt_next) / 1024 : 0);
}

// Convierte un pixel del formato del juego a 565
static inline u16 to565(u32 c, int fmt) {
  switch (fmt) {
    case 0: return c; // 565
    case 1: return (c & 0x1f) | (((c >> 5) & 0x1f) << 6) | (((c >> 10) & 0x1f) << 11); // 5551
    case 2: return ((c & 0xf) << 1) | (((c >> 4) & 0xf) << 7) | (((c >> 8) & 0xf) << 12); // 4444
    default: return ((c >> 3) & 0x1f) | (((c >> 10) & 0x3f) << 5) | (((c >> 19) & 0x1f) << 11); // 8888
  }
}

// 1 si el buffer que muestra el juego tiene algo dibujado (muestra 64 pixeles).
// El GE nunca escribe ahi (sus draws van al buffer grande), asi que si tiene contenido
// es porque lo puso la CPU o una copia de bloque: un video, un logo...
static int hasContent(u32 addr, int width, int fmt) {
  int bpp = fmt == 3 ? 4 : 2, x, y;
  if (!addr || !validAddr(addr) || !validAddr(addr + width * 272 * bpp - 1))
    return 0;
  u8 *p = (u8 *)((addr & 0x0fffffff) | 0x40000000); // sin cache
  for (y = 17; y < 272; y += 34)
    for (x = 30; x < 480; x += 60)
      if (bpp == 4 ? *(u32 *)(p + (y * width + x) * 4) & 0xffffff : *(u16 *)(p + (y * width + x) * 2))
        return 1;
  return 0;
}

// Escala x2 la imagen original del juego al buffer de alta resolucion (por CPU).
// Lee y escribe con cache (antes era sin cache y el video quedaba a pocos fps).
static void upscaleGameBuffer(u32 addr, int width, int fmt) {
  int bpp = fmt == 3 ? 4 : 2, x, y;
  u8 *src = (u8 *)(addr & 0x0fffffff);
  u32 *dst = (u32 *)DISPLAY_BUFFER;
  // Lo escribio otro (CPU del juego, ME o GE): escribir lo pendiente y descartar la copia vieja
  sceKernelDcacheWritebackInvalidateRange(src, width * 272 * bpp);
  for (y = 0; y < 272; y++) {
    u32 *row = dst + y * PITCH, *row2 = row + PITCH / 2;
    if (fmt == 3) {
      u32 *s32 = (u32 *)src + y * width;
      for (x = 0; x < 480; x++) {
        u32 c = to565(s32[x], 3);
        row[x] = row2[x] = c | (c << 16);
      }
    } else {
      u32 *s32 = (u32 *)((u16 *)src + y * width); // 2 pixeles por lectura
      for (x = 0; x < 240; x++) {
        u32 p = s32[x], a = to565(p & 0xffff, fmt), b = to565(p >> 16, fmt);
        row[x * 2] = row2[x * 2] = a | (a << 16);
        row[x * 2 + 1] = row2[x * 2 + 1] = b | (b << 16);
      }
    }
  }
  sceKernelDcacheWritebackRange((void *)DISPLAY_BUFFER, PITCH * HEIGHT * 2);
  *(u32 *)DRAW_NATIVE = 1;
}

static int cap_request; // captura pedida desde el menu
void menuRequestCapture(void) { cap_request = 1; }
static void autoCapture(void) {
  static int n;
  static u32 last;
  if (!opt.autocap || capturing || cap_request || n >= 5 || (n && log_frame - last < 300))
    return;
  n++;
  last = log_frame;
  cap_request = 1;
  LOG("captura automatica %d/5 en el frame siguiente (por el evento anterior)\n", n);
}

int sceDisplaySetFrameBufPatched(void *topaddr, int bufferwidth, int pixelformat, int sync) {
  log_frame++;
  if (log_frame == 30) {
    logGameId(); // en module_start el disco aun no esta montado
    menuLoad(game_id);
    memSnapshot("inicio");
  }

  // L+R+SELECT: captura detallada de lo que se procese hasta el proximo flip
  static int prev_combo;
  SceCtrlData pad;
  int k1 = pspSdkSetK1(0);
  sceCtrlPeekBufferPositive(&pad, 1);
  pspSdkSetK1(k1);
  u32 combo_mask = PSP_CTRL_LTRIGGER | PSP_CTRL_RTRIGGER | PSP_CTRL_SELECT;
  int combo = (pad.Buttons & combo_mask) == combo_mask;
  if (combo && !prev_combo) // con los botones (no las capturas automaticas): tambien el volcado binario
    dumpRequest();
  dumpOnFlip((u32)topaddr, bufferwidth, pixelformat);
  if (swap_on && ++swap_flips > 2) { // el pase nunca se mezclo: la escena quedo en la RAM
    LOG("fb %08lx: pase sin mezcla -> no se intercambia mas\n", state.framebuf_addr[swap_fb]);
    state.fb_swap[swap_fb] = 0;
    swap_on = 0;
  }
  dump_info.fake_vram_used = vram_hi ? ((vram_hi - FAKE_VRAM) | 0xffff) + 1 : 0;
  if (cap_request) { // desde el menu: como si se hubiera presionado la combinacion
    cap_request = 0;
    combo = 1;
    prev_combo = 0;
  }
  if (capturing) {
    capFlush();
    // Los 10 tipos de comando mas frecuentes del frame (para saber en que se va el tiempo)
    int k, i, best;
    for (k = 0; k < 10; k++) {
      for (best = -1, i = 0; i < 0x100; i++)
        if (cap_hist[i] && (best < 0 || cap_hist[i] > cap_hist[best]))
          best = i;
      if (best < 0)
        break;
      LOG("  comando %02x: %lu veces\n", best, cap_hist[best]);
      cap_hist[best] = 0;
    }
    LOG("== fin captura ==\n");
    capturing = 0;
    logEager(0);
  } else if (combo && !prev_combo) {
    memset(cap_last, 0, sizeof(cap_last));
    memset(cap_hist, 0, sizeof(cap_hist));
    logEager(1);
    LOG("== captura frame %lu: display %08lx ancho=%d fmt=%d, offset juego=(%d,%d)%s ==\n",
        log_frame, normVram((u32)topaddr), bufferwidth, pixelformat, (int)game_offx, (int)game_offy, passive ? " PASIVO" : "");
    capturing = 1;
  }
  prev_combo = combo;
  // Cada buffer distinto que el juego muestra; util para videos (FMV) que no pasan por el GE
  LOG_ONCE(normVram((u32)topaddr) ^ (bufferwidth << 20) ^ (pixelformat << 30), "display %08lx ancho=%d fmt=%d\n",
           normVram((u32)topaddr), bufferwidth, pixelformat);

  // Tiempo entre flips (fps) y costo del plugin en este frame
  u32 now = sceKernelGetSystemTimeLow(), dt = now - last_flip;
  if (last_flip && dt < 5000000) {
    st.frames++;
    st.frame_us += dt;
    if (dt > st.frame_max)
      st.frame_max = dt;
    st.slow60 += dt > 17500; // bajo 60 fps
    st.slow30 += dt > 34000; // bajo 30 fps
    if (dt > 200000 && log_frame > 120 && !st.long_frame_logged) {
      st.long_frame_logged = 1;
      LOG("frame lento: %lu ms (plugin %lu us, %lu draws en el frame)\n", dt / 1000, patch_us_frame, draws_since_flip);
    }
  }

  st.walked += walk_now;
  if (!draws_since_flip && walk_now)
    st.idle_frames++;
  if (!passive) {
    // Donde muestra el juego su imagen dentro de su fb (Silent Hill alterna x=0 y x=480
    // dentro de uno de 1024: doble buffer en un solo fb)
    int i, bpp = pixelformat == 3 ? 4 : 2, dfb = -1;
    u32 disp = normVram((u32)topaddr), best = 0x88000; // el fb mas cercano por debajo
    for (i = 0; i < state.framebuf_count && bufferwidth > 0; i++) {
      u32 off = disp - state.framebuf_addr[i];
      if (disp >= state.framebuf_addr[i] && off < best) {
        best = off;
        dfb = i;
      }
    }
    if (dfb >= 0 && !state.fb_shown[dfb]) {
      state.fb_shown[dfb] = 1;
      if (state.fb_layer[dfb]) { // se creyo intermedio pero se muestra: vuelve a ser pantalla
        state.fb_layer[dfb] = 0;
        rt_gen++;
      }
    }
    if (dfb >= 0 && best)
      LOG_ONCE(disp ^ 0xB1000000, "display dentro de fb %08lx: x=%lu y=%lu\n", state.framebuf_addr[dfb],
               best / bpp % bufferwidth, best / bpp / bufferwidth);

    // Si este frame no paso por el GE (video por CPU) o tuvo una copia de bloque grande a
    // un fb (video/logo), el buffer de alta resolucion no lo tiene: se muestra la imagen
    // original escalada. La fuente es lo que se copio, no lo que se muestra: Silent Hill
    // copia el video a un fb de 512 y lo pasa por GE a otro de 1024, que no se dibuja.
    // Desde el menu se puede forzar siempre (para comparar con el original) o nunca.
    int cpu_fb = draws_since_flip && !color_draws && !big_transfer && disp_tex && disp_tex_w >= 480 && disp_tex_w <= 512;
    int want = opt.fallback == 1 || (opt.fallback == 0 && (!draws_since_flip || big_transfer || cpu_fb));
    u32 src = big_transfer ? big_tx.addr : cpu_fb ? disp_tex : (u32)topaddr;
    int sw = big_transfer ? (int)big_tx.w : cpu_fb ? (int)disp_tex_w : bufferwidth;
    int sf = big_transfer ? (int)big_tx.fmt : cpu_fb ? (int)disp_tex_fmt : pixelformat;
    u16 *draw;
    if (want && hasContent(src, sw, sf)) {
      LOG_ONCE(0xE5000000 ^ src, "imagen original escalada desde %08lx ancho=%d fmt=%d (video/imagen fuera del GE)\n", src, sw, sf);
      u32 t0 = sceKernelGetSystemTimeLow();
      upscaleGameBuffer(src, sw, sf);
      st.upscale_us += sceKernelGetSystemTimeLow() - t0;
      st.fallback++;
      draw = (u16 *)DISPLAY_BUFFER;
    } else {
      draw = (u16 *)VRAM_DRAW_BUFFER_OFFSET;
      // Copia reducida del frame al fb mostrado, para los efectos que lo leen (opcion)
      if (opt.omit_fbtex == 2 && fbtex_seen && rt_enabled && draws_since_flip && dfb >= 0 && !best &&
          bufferwidth >= 480 && bufferwidth <= 512)
        fbCopyDown(disp, bufferwidth, pixelformat);
    }
    // El fb mostrado es un render target: lo que tiene en la VRAM falsa lo dibujo la CPU
    if (opt.cpu_draw && draw != (u16 *)DISPLAY_BUFFER && dfb >= 0 && bufferwidth == 1024 && !opt.wide1024 &&
        (disp & 15) == 0 && bufferwidth <= 1024) {
      LOG_ONCE(0xB2000000 ^ disp, "fb mostrado %08lx de 1024 (el GE no dibuja ahi): se pega encima lo que dibuje la CPU (spinner)\n", disp);
      cpuOverlay(disp, bufferwidth, pixelformat);
    }
    int menu = menu_shown = menuFrame();
    if (menu && draw == (u16 *)DISPLAY_BUFFER) { // imagen escalada por CPU: el menu se pone igual
      int y;
      for (y = 0; y < MENU_H; y++)
        memcpy(draw + (MENU_Y + y) * PITCH + MENU_X, (u16 *)MENU_BUF + y * MENU_W, MENU_W * 2);
      sceKernelDcacheWritebackRange(draw + MENU_Y * PITCH, MENU_H * PITCH * 2);
    }
    if (draw != (u16 *)DISPLAY_BUFFER)
      copyFrameBuffer(menu);
  }

  st.patch_us += patch_us_frame;
  if (patch_us_frame > st.patch_max)
    st.patch_max = patch_us_frame;
  if (capturing)
    LOG("  (plugin %lu us en este frame)\n", patch_us_frame);
  patch_us_frame = 0;
  draws_since_flip = big_transfer = color_draws = disp_tex = 0;
  last_flip = now;
  walk_now = !passive || capturing || log_frame % PASSIVE_SAMPLE == 0;

  if (log_frame % STATS_EVERY == 0) {
    LOG("stats listas=%lu head=%lu stall=%lu draws=%lu omitidos=%lu frames_sin_draws=%lu fbs=%lu\n",
        st.lists, st.heads, st.stalls, st.draws, st.skipped, st.idle_frames, state.framebuf_count);
    LOG("stats transfers=%lu a_fb=%lu escalados=%lu copias_en_lista=%lu copias_al_fb_juego=%lu pila_llena=%lu dibujos_cpu=%lu\n",
        st.transfers, st.fb_transfers, st.fallback, st.enq_copies, st.fb_copies, st.stack_full, st.cpu_overlays);
    memSnapshot("stats");
    if (st.frames) {
      u32 fps10 = st.frames * 100000 / (st.frame_us / 100 + 1); // fps x10, sin division de 64 bits
      LOG("stats rendimiento: fps=%lu.%lu frame_max=%lu ms bajo60=%lu bajo30=%lu\n",
          fps10 / 10, fps10 % 10, st.frame_max / 1000, st.slow60, st.slow30);
      u32 w = st.walked ? st.walked : 1;
      LOG("stats %s: %lu us/frame (max %lu) comandos/frame=%lu draws/frame=%lu escalado_cpu=%lu us/frame\n",
          passive ? "observacion" : "plugin", st.patch_us / w, st.patch_max, st.cmds / w, (st.draws + st.skipped) / w,
          st.fallback ? st.upscale_us / st.fallback : 0);
      LOG("stats recorrido: frames_medidos=%lu/%lu calls/frame=%lu comandos_en_sublistas/frame=%lu memo/frame=%lu\n",
          st.walked, st.frames, st.calls / w, st.call_cmds / w, st.lookups / w);
      LOG("stats 2d: vert2d=%lu ya2d=%lu choques2d=%lu | copias escaladas con GE=%lu desde_pantalla=%lu vram_real_corregida=%lu\n",
          st.v2d, st.v2d_skip, st.vevict, st.tx_ge, st.tx_screen, st.vram_fix);
      LOG("stats sublistas sin cambios: saltadas/frame=%lu cambiadas/frame=%lu comandos_no_recorridos/frame=%lu sumados/frame=%lu\n",
          st.wc_hits / w, st.wc_miss / w, st.wc_cmds / w, st.wc_summed / w);
      LOG("stats sublistas no guardadas/frame: parches=%lu 2d=%lu omitidos_o_rt=%lu saltos=%lu anidadas=%lu copias=%lu offset=%lu\n",
          st.wc_why[W_PATCH] / w, st.wc_why[W_2D] / w, st.wc_why[W_OMIT] / w, st.wc_why[W_JUMP] / w,
          st.wc_why[W_NESTED] / w, st.wc_why[W_COPY] / w, st.wc_why[W_OFFSET] / w);
    }
    if (rt_count || st.rt_fail) {
      LOG("stats rt: draws_en_rt=%lu draws_con_tex_rt=%lu rt_incompletos=%lu fallos=%lu rts=%d vram_libre=%lu KB\n",
          st.rt_draws, st.rt_tex, st.rt_incomplete, st.rt_fail, rt_count, (RT_END - rt_next) / 1024);
      int i;
      for (i = 0; i < rt_count; i++) {
        if (rt[i].draws || rt[i].reads)
          LOG("  rt %d %08lx %lux%lu: dibujos=%lu lecturas=%lu%s\n", i, rt[i].addr, rt[i].w, rt[i].h,
              rt[i].draws, rt[i].reads, rt[i].incomplete ? " (incompleto)" : "");
        rt[i].draws = rt[i].reads = 0;
      }
    }
    memset(&st, 0, sizeof(st));
  }

  return _sceDisplaySetFrameBuf(topaddr, bufferwidth, pixelformat, sync);
}

// Prueba de VRAM de 4 MB (solo diagnostico: no cambia como se dibuja).
// Los modelos PSP-2000 en adelante tienen 4 MB de VRAM, pero el sistema expone 2 MB.
// Se pide 4 MB con sceGeEdramSetSize y se comprueba que la zona 2-4 MB exista de verdad
// (CPU) y que el GE pueda escribir en ella (copia de bloque). Resultado en el log.
static void probeVram(void) {
  int (*setSize)(int) = (void *)FindProc("sceGE_Manager", "sceGe_driver", 0x5BAA5439);
  int (*drawSync)(int) = (void *)FindProc("sceGE_Manager", "sceGe_driver", 0xB287BD61);
  volatile u32 *lo = (u32 *)0x44000000, *hi = (u32 *)0x44300000; // sin cache
  u32 before = (u32)_sceGeEdramGetSize(), res = setSize ? setSize(0x400000) : 0xFFFFFFFF;
  u32 after = (u32)_sceGeEdramGetSize();
  LOG("vram: tamano=%lu KB, SetSize(4MB)=%08lx, ahora=%lu KB\n", before / 1024, res, after / 1024);
  logFlush(); // por si lo siguiente cuelga la consola

  // CPU: escribir arriba y comprobar que no pisa la parte baja (si fueran 2 MB reflejados)
  u32 save = *lo;
  *lo = 0x11111111;
  *hi = 0x22222222;
  int cpu_ok = *hi == 0x22222222, alias = *lo == 0x22222222;
  *lo = save;
  LOG("vram: CPU en 3 MB %s%s\n", cpu_ok ? "ok" : "FALLA", alias ? " (es un reflejo de los primeros 2 MB)" : "");
  logFlush();

  // GE: copiar 16x16 pixeles desde la parte baja a 3 MB y leerlos
  int ge_ok = 0;
  if (cpu_ok && !alias && drawSync) {
    int i;
    for (i = 0; i < 16 * 16 / 2; i++) {
      lo[i] = 0xA5A50000 | i;
      hi[i] = 0;
    }
    guStart(0, (void *)(RENDER_LIST | 0xA0000000));
    sceGuCopyImage(GE_FORMAT_565, 0, 0, 16, 16, 16, (void *)0x04000000, 0, 0, 16, (void *)0x04300000);
    sceGuFinish();
    _sceGeListEnQueue((void *)RENDER_LIST, NULL, -1, NULL);
    drawSync(0);
    for (ge_ok = 1, i = 0; i < 16 * 16 / 2; i++)
      if (hi[i] != (0xA5A50000 | i))
        ge_ok = 0;
  }
  LOG("vram: GE en 3 MB %s\n", ge_ok ? "ok -> hay 4 MB utilizables" : "no comprobado o FALLA");
  logFlush();

  // CPU en toda la zona de render targets, cada 256 KB
  int bad = 0;
  if (ge_ok) {
    u32 a;
    for (a = RT_BASE; a < RT_END; a += 0x40000) {
      volatile u32 *p = (u32 *)(a | 0x40000000), *q = (u32 *)((a + 0x3fffc) | 0x40000000);
      *p = a; *q = ~a;
      if (*p != a || *q != ~a) {
        LOG("vram: FALLA en %08lx\n", a);
        bad++;
      }
    }
  }
  rt_enabled = ge_ok && !bad;
  // GE DIBUJANDO arriba: un sprite de 512x64 desde la parte baja a 3 MB. La prueba anterior
  // usa una copia, que si llega en orden; lo dibujado puede quedar permutado (ver vram_swz)
  if (rt_enabled) {
    volatile u16 *src = (u16 *)0x44000000, *dst = (u16 *)0x44300000;
    u32 *l = (u32 *)(RENDER_LIST | 0x40000000), va = RENDER_LIST + 0x800, i, wrong = 0, xored = 0;
    short q[10] = { 0, 0, 0, 0, 0, 512, 64, 512, 64, 0 };
    memcpy((void *)(va | 0x40000000), q, sizeof(q));
    for (i = 0; i < 512 * 64; i++) {
      src[i] = (u16)(i * 2654435761u >> 16) | 1; // sin negro: distinto de lo que habia
      dst[i] = 0;
    }
    #define C(c, d) (*l++ = ((c) << 24) | ((d) & 0xffffff))
    C(GE_CMD_FRAMEBUFPIXFORMAT, 0); C(GE_CMD_FRAMEBUFPTR, 0x300000); C(GE_CMD_FRAMEBUFWIDTH, (0x04 << 16) | 512);
    C(GE_CMD_SCISSOR1, 0); C(GE_CMD_SCISSOR2, (63 << 10) | 511); C(GE_CMD_REGION1, 0); C(GE_CMD_REGION2, (63 << 10) | 511);
    static const u8 off[] = { GE_CMD_ALPHATESTENABLE, GE_CMD_ZTESTENABLE, GE_CMD_ALPHABLENDENABLE, GE_CMD_STENCILTESTENABLE,
      GE_CMD_LOGICOPENABLE, GE_CMD_COLORTESTENABLE, GE_CMD_DITHERENABLE, GE_CMD_MASKRGB, GE_CMD_MASKALPHA, GE_CMD_CLEARMODE,
      GE_CMD_TEXMAPMODE, GE_CMD_TEXMODE, GE_CMD_TEXFORMAT, GE_CMD_TEXFILTER };
    for (i = 0; i < sizeof(off); i++)
      C(off[i], 0);
    C(GE_CMD_ZWRITEDISABLE, 1); C(GE_CMD_TEXTUREMAPENABLE, 1);
    C(GE_CMD_TEXADDR0, 0); C(GE_CMD_TEXBUFWIDTH0, (0x04 << 16) | 512); C(GE_CMD_TEXSIZE0, (6 << 8) | 9);
    C(GE_CMD_TEXWRAP, (1 << 8) | 1); C(GE_CMD_TEXFUNC, (1 << 8) | 3); C(GE_CMD_TEXFLUSH, 0);
    C(GE_CMD_VERTEXTYPE, GE_VTYPE_THROUGH | GE_VTYPE_TC_16BIT | GE_VTYPE_POS_16BIT);
    C(GE_CMD_BASE, (va >> 8) & 0x0f0000); C(GE_CMD_VADDR, va);
    C(GE_CMD_PRIM, (GE_PRIM_RECTANGLES << 16) | 2);
    C(GE_CMD_FINISH, 0); C(GE_CMD_END, 0);
    #undef C
    _sceGeListEnQueue((void *)RENDER_LIST, NULL, -1, NULL);
    drawSync(0);
    for (i = 0; i < 512 * 64; i++) {
      wrong += dst[i] != src[i];
      xored += dst[((i * 2) ^ 0x8040) / 2] == src[i];
    }
    // dev31 lo confirmo: la VRAM real es de 2 MB. Lo que el GE dibuja en 0x04200000+ cae en
    // los 2 MB bajos (0x042... sobre el buffer grande, 0x043... sobre el z-buffer), y la CPU
    // y las copias ven esa zona como un reflejo desordenado. No hay lugar para render targets.
    vram_swz = wrong > 0;
    LOG("vram: GE dibujando en 3 MB: %s (%lu de %d pixeles distintos)\n",
        vram_swz ? "NO (la VRAM es de 2 MB: arriba es un reflejo) -> sin render targets" : "en orden", wrong, 512 * 64);
    if (vram_swz)
      rt_enabled = 0;
  }
  LOG("vram: render targets %s\n", rt_enabled ? "ACTIVADOS en 2-4 MB" : "desactivados (se omiten como antes)");
  logFlush();
}

// Estado del plugin para los registros del volcado
static void dumpState(u32 o[8]) {
  o[0] = state.base; o[1] = state.offset; o[2] = state.vertex_addr; o[3] = state.index_addr;
  o[4] = state.framebuf; o[5] = state.cur_rt ? state.cur_rt->vram : 0; o[6] = state.offx; o[7] = state.offy;
}
static void dumpSetup(void) {
  dump_info.opt = &opt; dump_info.opt_size = sizeof(opt);
  dump_info.shadow = state.ge_cmd; dump_info.passive = &passive; dump_info.state = dumpState;
}

int module_start(SceSize args, void *argp) {
  SceCtrlData pad;
  dumpSetup();
  sceCtrlPeekBufferPositive(&pad, 1);
  // L al iniciar: modo pasivo (antes desactivaba el plugin del todo y no habia log)
  passive = !!(pad.Buttons & PSP_CTRL_LTRIGGER);

  logInit(pad.Buttons & PSP_CTRL_RTRIGGER);
  // Botones al iniciar el juego (tambien se pueden cambiar despues desde el menu)
  if (pad.Buttons & PSP_CTRL_TRIANGLE)
    opt.nop_mark = 0;
  if (pad.Buttons & PSP_CTRL_CIRCLE)
    opt.walk_new = 0;
  LOG("opciones: nop_omitido=%s recorrido=%s compilado=%s\n", opt.nop_mark ? "marca" : "0 (antiguo)",
      opt.walk_new ? "nuevo" : "antiguo", BUILD_OPT);
  if (passive)
    LOG("modo PASIVO (L): el juego corre sin cambios; se mide y registra igual para comparar\n");

  _sceGeEdramGetAddr = (void *)FindProc("sceGE_Manager", "sceGe_driver", 0xE47E40E4);
  _sceGeEdramGetSize = (void *)FindProc("sceGE_Manager", "sceGe_driver", 0x1F6752AD);
  _sceGeGetList = (void *)FindProc("sceGE_Manager", "sceGe_driver", 0x67B01D8E);
  _sceGeListUpdateStallAddr = (void *)FindProc("sceGE_Manager", "sceGe_driver", 0xE0D68148);
  _sceGeListEnQueue = (void *)FindProc("sceGE_Manager", "sceGe_driver", 0xAB49E76A);
  _sceGeListEnQueueHead = (void *)FindProc("sceGE_Manager", "sceGe_driver", 0x1C0D95A6);

  if (!_sceGeEdramGetAddr || !_sceGeEdramGetSize || !_sceGeGetList ||
      !_sceGeListUpdateStallAddr || !_sceGeListEnQueue || !_sceGeListEnQueueHead)
    LOG("ERROR: no se encontro alguna funcion de sceGe_driver\n");

  if (!passive) { // en pasivo el juego usa la VRAM real
    sctrlHENPatchSyscall((u32)_sceGeEdramGetAddr, sceGeEdramGetAddrPatched);
    sctrlHENPatchSyscall((u32)_sceGeEdramGetSize, sceGeEdramGetSizePatched);
  }
  sctrlHENPatchSyscall((u32)_sceGeListUpdateStallAddr, sceGeListUpdateStallAddrPatched);
  sctrlHENPatchSyscall((u32)_sceGeListEnQueue, sceGeListEnQueuePatched);
  sctrlHENPatchSyscall((u32)_sceGeListEnQueueHead, sceGeListEnQueueHeadPatched);

  memset(memo, 0, MEMO_SIZE * sizeof(MemoEntry));
  memset(vmemo, 0, VMEMO_SIZE * sizeof(VMemo));
  memset(wc, 0, WC_SIZE * sizeof(WalkCache));
  if (!passive)
    probeVram();

  _sceDisplaySetFrameBuf = (void *)FindProc("sceDisplay_Service", "sceDisplay_driver", 0x289D82FE);
  if (!_sceDisplaySetFrameBuf)
    LOG("ERROR: no se encontro sceDisplaySetFrameBuf\n");
  sctrlHENPatchSyscall((u32)_sceDisplaySetFrameBuf, sceDisplaySetFrameBufPatched);
  if (!passive)
    menuInstallCtrlHooks();

  sceKernelDcacheWritebackInvalidateAll();
  sceKernelIcacheClearAll();

  return 0;
}
