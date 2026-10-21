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

PSP_MODULE_INFO("GePatch", 0x1007, 1, 0);

#define DRAW_NATIVE 0xABCDEF00

#define PITCH 960
#define WIDTH 960
#define HEIGHT 544
#define PIXELFORMAT GE_FORMAT_565

#define FAKE_VRAM 0x0A000000
#define DISPLAY_BUFFER 0x0A400000
#define VERTICES_BUFFER 0x0A600000
#define RENDER_LIST 0x0A800000

#define VRAM_DRAW_BUFFER_OFFSET 0x04000000
#define VRAM_DEPTH_BUFFER_OFFSET 0x04100000
#define VRAM_1KB 0x041ff000

#define STATS_EVERY 600 // frames entre cada linea de estadisticas (~10 s)

// Contadores baratos que se vuelcan al log cada STATS_EVERY frames
static struct {
  u32 lists, heads, stalls, draws, skipped, stack_full, transfers, fb_transfers, fallback, enq_copies, idle_frames;
} st;

#define MAX_CMDS 200000 // tope de comandos por recorrido, para no colgarse en listas en bucle

// Memoria de parches. Muchos juegos vuelven a ejecutar la misma lista, cuyos comandos
// ya estan parcheados: sin esto se leia el valor parcheado como si fuera del juego
// (framebuffer mal identificado, y no se podria escalar viewport/scissor sin duplicar
// dos veces). Se guarda el original por direccion de comando.
#define MEMO_SIZE 2048
static struct { u32 *addr; u32 orig, patched; } memo[MEMO_SIZE];

static u32 origOp(u32 *p) {
  int i = ((u32)p >> 2) & (MEMO_SIZE - 1);
  return (memo[i].addr == p && *p == memo[i].patched) ? memo[i].orig : *p;
}

static void setOp(u32 *p, u32 orig, u32 op) {
  int i = ((u32)p >> 2) & (MEMO_SIZE - 1);
  memo[i].addr = p;
  memo[i].orig = orig;
  memo[i].patched = op;
  *p = op;
}

// Ultimos OFFSETX/Y del juego (en pixeles), para escalar el centro del viewport
static float game_offx = 2048 - 240, game_offy = 2048 - 136;

// Estado entre un flip (sceDisplaySetFrameBuf) y el siguiente
static u32 draws_since_flip, big_transfer, last_flip;

// 1 si se puede leer: VRAM real o RAM (incluye la VRAM falsa en 0x0A000000)
int validAddr(u32 addr) {
  addr &= 0x0fffffff;
  return (addr >= 0x04000000 && addr < 0x04200000) || (addr >= 0x08000000 && addr < 0x0A400000);
}

static const u8 tcsize[4] = { 0, 2, 4, 8 }, tcalign[4] = { 0, 1, 2, 4 };
static const u8 colsize[8] = { 0, 0, 0, 0, 2, 2, 2, 4 }, colalign[8] = { 0, 0, 0, 0, 2, 2, 2, 4 };
static const u8 nrmsize[4] = { 0, 3, 6, 12 }, nrmalign[4] = { 0, 1, 2, 4 };
static const u8 possize[4] = { 3, 3, 6, 12 }, posalign[4] = { 1, 1, 2, 4 };
static const u8 wtsize[4] = { 0, 1, 2, 4 }, wtalign[4] = { 0, 1, 2, 4 };

#define ALIGN(x, align) (((x) + ((align) - 1)) & ~((align) - 1))

void getVertexInfo(u32 op, u8 *vertex_size, u8 *pos_off, u8 *visit_off) {
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
    // tcoff = size;
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
} StackEntry;

typedef struct {
  u32 ge_cmd[0x100];

  u32 texbufptr[8];
  u32 texbufwidth[8];
  u32 framebufptr;
  u32 framebufwidth;
  u32 *framebufwidth_addr;
  u32 framebuf;

  u32 base;
  u32 offset;
  u32 address;

  u32 index_addr;
  u32 vertex_addr;
  u32 vertex_type;

  u32 ignore_framebuf;
  u32 ignore_texture;

  u32 has_draws;

  StackEntry stack[64];
  u32 curr_stack;

  u32 framebuf_addr[16];
  u32 framebuf_count;
  u32 framebuf_next; // al llenarse la tabla se reemplaza el mas antiguo
} GeState;

static GeState state;

// Se llama al encolar una lista nueva. Solo se reinicia el recorrido (pila, offset):
// los registros del GE (BASE, direcciones de vertices, framebuffer, texturas...) se
// mantienen entre listas igual que en el hardware. Antes se borraba todo, y una lista
// que dibujaba sin volver a fijar BASE o VADDR hacia que se leyera/parcheara memoria
// equivocada (posible causa de glitches y cuelgues).
void resetGeState() {
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
  LOG_ONCE(0xE1000000, "pila de display list llena (CALL ignorado)\n");
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
  if (state.framebuf_count < n) {
    state.framebuf_addr[state.framebuf_count] = framebuf;
    return state.framebuf_count++;
  }

  LOG_ONCE(0xE2000000, "tabla de framebuffers llena, se reemplaza el mas antiguo\n");
  int i = state.framebuf_next++ % n;
  state.framebuf_addr[i] = framebuf;
  return i;
}

// 1 si addr cae dentro de algun framebuffer conocido (480x272 a 32 bpp como maximo)
int inFramebuf(u32 addr) {
  int i;
  for (i = 0; i < state.framebuf_count; i++)
    if (addr >= state.framebuf_addr[i] && addr < state.framebuf_addr[i] + 512 * 272 * 4)
      return 1;
  return 0;
}

void AdvanceVerts(int count, int vertex_size) {
  if ((state.vertex_type & GE_VTYPE_IDX_MASK) != GE_VTYPE_IDX_NONE) {
    int index_shift = ((state.vertex_type & GE_VTYPE_IDX_MASK) >> GE_VTYPE_IDX_SHIFT) - 1;
    state.index_addr += count << index_shift;
  } else {
    state.vertex_addr += count * vertex_size;
  }
}

// TODO: when ignore_framebuf=1 or ignore_texture=1, dummy all non-control-flow instructions
void patchGeList(u32 *list, u32 *stall) {
  union {
    float f;
    unsigned int i;
  } t;

  StackEntry *stack_entry;
  StackEntry stack_entry_buf;
  int ncmds = 0;

  for (; list && (!stall || (stall && list != stall)); list++) {
    // Nunca leer fuera de la memoria valida ni recorrer para siempre
    if (!validAddr((u32)list)) {
      LOG_ONCE(((u32)list >> 12) ^ 0xD0000000, "lista apunta a direccion invalida %08lx, se corta\n", (u32)list);
      goto finish;
    }
    if (++ncmds > MAX_CMDS) {
      LOG_ONCE(0xE6000000, "lista con mas de %d comandos (bucle?), se corta\n", MAX_CMDS);
      goto finish;
    }

    u32 op = origOp(list); // valor original del juego aunque ya este parcheado
    u32 cmd = op >> 24;
    u32 data = op & 0xffffff;

    state.ge_cmd[cmd] = data;

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

      // Handle control flow commands

      case GE_CMD_BASE:
        state.base = (data << 8) & 0x0f000000;
        break;

      case GE_CMD_OFFSETADDR:
        state.offset = data << 8;
        break;

      case GE_CMD_ORIGIN:
        state.offset = (u32)list;
        break;

      // TODO: need to save other states, too?
      case GE_CMD_CALL:
        state.address = ((state.base | data) + state.offset) & 0x0ffffffc;
        if (!validAddr(state.address) || !validAddr(state.address + 12 * 4)) {
          LOG_ONCE((state.address >> 12) ^ 0xD1000000, "CALL a direccion invalida %08lx, se ignora\n", state.address);
          break;
        }
        if (*(u32 *)(state.address) >> 24 == GE_CMD_BONEMATRIXDATA &&
            *(u32 *)(state.address+11*4) >> 24 == GE_CMD_BONEMATRIXDATA &&
            *(u32 *)(state.address+12*4) >> 24 == GE_CMD_RET) {
          break;
        }
        stack_entry = &stack_entry_buf;
        stack_entry->list = list;
        stack_entry->base = state.base; // antes quedaba sin inicializar y un SIGNAL RET lo restauraba
        stack_entry->offset = state.offset;
        if (push(stack_entry) == 0) {
          list = (u32 *)(state.address - 4);
        }
        break;

      // BJUMP salta solo si la caja de BOUNDINGBOX quedo fuera de pantalla. Antes siempre
      // se saltaba, y los draws que el GE si ejecuta (caja visible) quedaban sin parchear:
      // geometria en la esquina o que aparece/desaparece. Seguir de largo recorre ambos caminos.
      case GE_CMD_BJUMP:
        break;

      case GE_CMD_JUMP:
        state.address = ((state.base | data) + state.offset) & 0x0ffffffc;
        list = (u32 *)(state.address - 4);
        break;

      case GE_CMD_RET:
        // Ignore returns when the stack is empty
        stack_entry = pop();
        if (stack_entry) {
          list = stack_entry->list;
          state.offset = stack_entry->offset;
        }
        break;

      case GE_CMD_END:
      {
        u32 prev = *(list-1);
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
                LOG_ONCE(0xE3000000 | behaviour, "signal no manejada: behaviour=%d\n", behaviour);
                break;
            }
            break;
          }

          case GE_CMD_FINISH:
            goto finish;

          default:
            break;
        }
        break;
      }

      // Patch vertices

      case GE_CMD_IADDR:
        state.index_addr = ((state.base | data) + state.offset) & 0x0fffffff;
        break;

      case GE_CMD_VADDR:
        state.vertex_addr = ((state.base | data) + state.offset) & 0x0fffffff;
        break;

      case GE_CMD_VERTEXTYPE:
        state.vertex_type = data;
        break;

      case GE_CMD_BEZIER:
      case GE_CMD_SPLINE:
      {
        state.has_draws = 1;
        draws_since_flip++;

        if (state.ignore_framebuf || (state.ignore_texture && state.ge_cmd[GE_CMD_TEXTUREMAPENABLE])) {
          setOp(list, op, 0);
          st.skipped++;
          break;
        }
        if (*list != op) // se habia anulado en una pasada anterior: restaurar
          *list = op;
        st.draws++;

        if ((state.vertex_type & GE_VTYPE_THROUGH_MASK) == GE_VTYPE_THROUGH) {
          u8 num_points_u = data & 0xff;
          u8 num_points_v = (data >> 8) & 0xff;

          u32 count = num_points_u * num_points_v;

          u8 vertex_size = 0, pos_off = 0, visit_off = 0;
          getVertexInfo(state.vertex_type, &vertex_size, &pos_off, &visit_off);

          AdvanceVerts(count, vertex_size);
        }

        break;
      }

      case GE_CMD_BOUNDINGBOX:
      {
        if (state.ignore_framebuf || (state.ignore_texture && state.ge_cmd[GE_CMD_TEXTUREMAPENABLE]))
          break;

        if ((state.vertex_type & GE_VTYPE_THROUGH_MASK) == GE_VTYPE_THROUGH) {
          u32 count = data;

          u8 vertex_size = 0, pos_off = 0, visit_off = 0;
          getVertexInfo(state.vertex_type, &vertex_size, &pos_off, &visit_off);

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
        if (state.ignore_framebuf || (state.ignore_texture && state.ge_cmd[GE_CMD_TEXTUREMAPENABLE])) {
          setOp(list, op, 0);
          st.skipped++;
          break;
        }
        if (*list != op) // se habia anulado en una pasada anterior: restaurar
          *list = op;
        st.draws++;

        u16 count = data & 0xffff;
        u8 vertex_size = 0, pos_off = 0, visit_off = 0;
        getVertexInfo(state.vertex_type, &vertex_size, &pos_off, &visit_off);

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

          // TODO: we may patch the same vertex again and again...
          u8 decoded = 0, encoded = 0;
          u32 vertex_addr = state.vertex_addr + lower * vertex_size;
          int i;
          for (i = lower; i < upper; i++, vertex_addr += vertex_size) {
            int j;
            for (j = 0; j < 2; j++) {
              u32 addr = vertex_addr + pos_off + j * pos_size;
              switch (pos_size) {
                case 2:
                {
                  short val = *(short *)addr;
                  if (val != 0) {
                    // Decode and check if we already doubled at least one of the vertices
                    // If that's the case, let's assume all other vertices have been doubled, too
                    if (!decoded && visit_off && upper - i >= 2) {
                      if (*(u8 *)(vertex_addr + visit_off + 0 * vertex_size) == ((val >> 0) & 0xff) &&
                          *(u8 *)(vertex_addr + visit_off + 1 * vertex_size) == ((val >> 8) & 0xff)) {
                        goto exit_loop;
                      }
                      decoded = 1;
                    }

                    if (val == 480 || val == 960)
                      *(short *)addr = 960;
                    else if (val == 272 || val == 544)
                      *(short *)addr = 544;
                    else if (val > -2048 && val < 2048)
                      *(short *)addr *= 2;

                    // Encode that we already doubled one of the vertices
                    if (!encoded && visit_off && upper - i >= 2) {
                      val = *(short *)addr;
                      *(u8 *)(vertex_addr + visit_off + 0 * vertex_size) = (val >> 0) & 0xff;
                      *(u8 *)(vertex_addr + visit_off + 1 * vertex_size) = (val >> 8) & 0xff;
                      encoded = 1;
                    }
                  }
                  break;
                }

                case 4:
                {
                  t.i = *(u32 *)addr;
                  if (t.f != 0) {
                    // Decode and check if we already doubled at least one of the vertices
                    // If that's the case, let's assume all other vertices have been doubled, too
                    if (!decoded && visit_off && upper - i >= 4) {
                      if (*(u8 *)(vertex_addr + visit_off + 0 * vertex_size) == ((t.i >> 0) & 0xff) &&
                          *(u8 *)(vertex_addr + visit_off + 1 * vertex_size) == ((t.i >> 8) & 0xff) &&
                          *(u8 *)(vertex_addr + visit_off + 2 * vertex_size) == ((t.i >> 16) & 0xff) &&
                          *(u8 *)(vertex_addr + visit_off + 3 * vertex_size) == ((t.i >> 24) & 0xff)) {
                        goto exit_loop;
                      }
                      decoded = 1;
                    }

                    if (t.f == 480 || t.f == 960) {
                      t.f = 960;
                      *(u32 *)addr = t.i;
                    } else if (t.f == 272 || t.f == 544) {
                      t.f = 544;
                      *(u32 *)addr = t.i;
                    } else if (t.f > -2048 && t.f < 2048) {
                      t.f *= 2;
                      *(u32 *)addr = t.i;
                    }

                    // Encode that we already doubled one of the vertices
                    if (!encoded && visit_off && upper - i >= 4) {
                      *(u8 *)(vertex_addr + visit_off + 0 * vertex_size) = (t.i >> 0) & 0xff;
                      *(u8 *)(vertex_addr + visit_off + 1 * vertex_size) = (t.i >> 8) & 0xff;
                      *(u8 *)(vertex_addr + visit_off + 2 * vertex_size) = (t.i >> 16) & 0xff;
                      *(u8 *)(vertex_addr + visit_off + 3 * vertex_size) = (t.i >> 24) & 0xff;
                      encoded = 1;
                    }
                  }
                  break;
                }
              }
            }
          }

exit_loop:
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
        setOp(list, op, (cmd << 24) | PIXELFORMAT);
        break;

      case GE_CMD_FRAMEBUFPTR:
      case GE_CMD_FRAMEBUFWIDTH:
      {
        if (cmd == GE_CMD_FRAMEBUFPTR) {
          setOp(list, op, (cmd << 24) | (VRAM_DRAW_BUFFER_OFFSET & 0xffffff));
          state.framebufptr = op;
        } else {
          u16 pitch = ((op & 0xffff) == 512 || (op & 0xffff) == 480) ? 960 : (op & 0xffff);
          setOp(list, op, (cmd << 24) | ((VRAM_DRAW_BUFFER_OFFSET >> 24) << 16) | pitch);
          state.framebufwidth = op;
          state.framebufwidth_addr = list;
        }

        if (state.framebufptr && state.framebufwidth) {
          state.framebuf = FAKE_VRAM | (state.framebufptr & 0xffffff);

          // This allows more games to work, but causes weird triangles in Sonic.
          u32 pitch = state.framebufwidth & 0xffff;
          // 480 tambien es un buffer de pantalla completa (Ridge Racer lo usa al iniciar)
          if (pitch == 512 || pitch == 480 || pitch == 960) {
            state.ignore_framebuf = 0;
          } else {
            setOp(state.framebufwidth_addr, state.framebufwidth, (GE_CMD_FRAMEBUFWIDTH << 24) | ((VRAM_1KB >> 24) << 16) | 0);
            state.ignore_framebuf = 1;
          }

          LOG_ONCE(state.framebuf ^ (pitch << 20), "fb %08lx pitch=%lu%s\n", state.framebuf, pitch,
                   state.ignore_framebuf ? " -> ignorado (no se dibuja)" : "");
          insertFramebuf(state.framebuf);

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

      case GE_CMD_TEXADDR0:
      case GE_CMD_TEXADDR1:
      case GE_CMD_TEXADDR2:
      case GE_CMD_TEXADDR3:
      case GE_CMD_TEXADDR4:
      case GE_CMD_TEXADDR5:
      case GE_CMD_TEXADDR6:
      case GE_CMD_TEXADDR7:
      case GE_CMD_TEXBUFWIDTH0:
      case GE_CMD_TEXBUFWIDTH1:
      case GE_CMD_TEXBUFWIDTH2:
      case GE_CMD_TEXBUFWIDTH3:
      case GE_CMD_TEXBUFWIDTH4:
      case GE_CMD_TEXBUFWIDTH5:
      case GE_CMD_TEXBUFWIDTH6:
      case GE_CMD_TEXBUFWIDTH7:
      {
        int index;
        if (cmd >= GE_CMD_TEXADDR0 && cmd <= GE_CMD_TEXADDR7) {
          index = cmd - GE_CMD_TEXADDR0;
          state.texbufptr[index] = op;
        } else {
          index = cmd - GE_CMD_TEXBUFWIDTH0;
          state.texbufwidth[index] = op;
        }

        if (state.texbufptr[index] && state.texbufwidth[index]) {
          u32 texaddr = ((state.texbufwidth[index] & 0x0f0000) << 8) | (state.texbufptr[index] & 0xffffff);
          if (texaddr != state.framebuf && findFramebuf(texaddr) >= 0) {
            LOG_ONCE(texaddr ^ 0x7E000000, "textura desde fb %08lx -> draws omitidos\n", texaddr);
            state.ignore_texture = 1;
          } else {
            state.ignore_texture = 0;
          }

          state.texbufptr[index] = 0;
          state.texbufwidth[index] = 0;
        }

        break;
      }

      // Copias de bloque (sceGuCopyImage). No pasan por los vertices, asi que no se
      // ven en el buffer de alta resolucion. Videos y logos suelen dibujarse asi.
      case GE_CMD_TRANSFERSTART:
      {
        u32 dst = ((state.ge_cmd[GE_CMD_TRANSFERDSTW] & 0xff0000) << 8) | (state.ge_cmd[GE_CMD_TRANSFERDST] & 0xffffff);
        u32 w = (state.ge_cmd[GE_CMD_TRANSFERSIZE] & 0x3ff) + 1;
        u32 h = ((state.ge_cmd[GE_CMD_TRANSFERSIZE] >> 10) & 0x3ff) + 1;
        st.transfers++;
        if (inFramebuf(dst)) {
          st.fb_transfers++;
          if (w * h >= 240 * 136)
            big_transfer = 1;
          LOG_ONCE(dst ^ (w << 20) ^ h, "transfer a fb %08lx %lux%lu\n", dst, w, h);
        }
        break;
      }

      // Viewport, offset y scissor: se escalan x2 a partir de los valores del juego.
      // Antes se forzaban a pantalla completa, y lo que el juego dibuja en una zona
      // (espejo retrovisor, HUD 3D, pantalla dividida) salia agrandado y fuera de lugar.
      // OFFSET queda fijo para que el area de dibujo siga centrada en 2048.
      case GE_CMD_VIEWPORTXSCALE:
      case GE_CMD_VIEWPORTYSCALE:
        t.i = data << 8;
        t.f *= 2;
        setOp(list, op, (cmd << 24) | (t.i >> 8));
        break;

      case GE_CMD_VIEWPORTXCENTER:
        t.i = data << 8;
        t.f = (2048 - WIDTH / 2) + 2 * (t.f - game_offx);
        setOp(list, op, (cmd << 24) | (t.i >> 8));
        break;

      case GE_CMD_VIEWPORTYCENTER:
        t.i = data << 8;
        t.f = (2048 - HEIGHT / 2) + 2 * (t.f - game_offy);
        setOp(list, op, (cmd << 24) | (t.i >> 8));
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
      {
        u32 x = (data & 0x3ff) * 2, y = ((data >> 10) & 0x3ff) * 2;
        setOp(list, op, (cmd << 24) | ((y > 1023 ? 1023 : y) << 10) | (x > 1023 ? 1023 : x));
        break;
      }

      case GE_CMD_REGION2:
      case GE_CMD_SCISSOR2:
      {
        u32 x = (data & 0x3ff) * 2 + 1, y = ((data >> 10) & 0x3ff) * 2 + 1;
        setOp(list, op, (cmd << 24) | ((y > 1023 ? 1023 : y) << 10) | (x > 1023 ? 1023 : x));
        break;
      }

      default:
        break;
    }
  }

finish:
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

void copyFrameBuffer() {
  *(u32 *)DRAW_NATIVE = 1;

  // memcpy((void *)DISPLAY_BUFFER, (void *)VRAM_DRAW_BUFFER_OFFSET, 960*544*2);
  guStart(0, (void *)(RENDER_LIST | 0xA0000000));
  sceGuCopyImage(PIXELFORMAT, 0, 0, WIDTH, HEIGHT, PITCH,
                 (void *)VRAM_DRAW_BUFFER_OFFSET,
                 0, 0, PITCH, (void *)DISPLAY_BUFFER);
  sceGuFinish();
  _sceGeListEnQueue((void *)RENDER_LIST, NULL, -1, NULL);
}

int sceGeListUpdateStallAddrPatched(int qid, void *stall) {
  int k1 = pspSdkSetK1(0);
  st.stalls++;
  char info[64];
  if (_sceGeGetList(qid, info, NULL) == 0) {
    u16 state = *(u16 *)(info + 0x08);
    if (state != 3) { // completed
      void *list = *(void **)(info + 0x18); // previous stall
      if (!list)
        list = *(void **)(info + 0x14); // list
      if (((u32)list & 0x0fffffff) < ((u32)stall & 0x0fffffff)) {
        patchGeList((u32 *)((u32)list & 0x0fffffff), (u32 *)((u32)stall & 0x0fffffff));
      }
    }
  }
  pspSdkSetK1(k1);
  return _sceGeListUpdateStallAddr(qid, stall);
}

int sceGeListEnQueuePatched(const void *list, void *stall, int cbid, PspGeListArgs *arg) {
  st.lists++;
  // Juegos con un solo buffer casi no llaman sceDisplaySetFrameBuf: para ellos se copia aqui.
  // Antes se copiaba al encolar cada lista, y en juegos con varias listas por frame eso
  // mostraba frames a medio dibujar (parpadeo, tearing). Ahora solo si no hubo flip en 100 ms.
  if (state.has_draws && sceKernelGetSystemTimeLow() - last_flip > 100000) {
    copyFrameBuffer();
    st.enq_copies++;
  }
  resetGeState();
  patchGeList((u32 *)((u32)list & 0x0fffffff), (u32 *)((u32)stall & 0x0fffffff));
  return _sceGeListEnQueue(list, stall, cbid, arg);
}

int sceGeListEnQueueHeadPatched(const void *list, void *stall, int cbid, PspGeListArgs *arg) {
  st.heads++;
  resetGeState();
  patchGeList((u32 *)((u32)list & 0x0fffffff), (u32 *)((u32)stall & 0x0fffffff));
  return _sceGeListEnQueueHead(list, stall, cbid, arg);
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

int sceDisplaySetFrameBufPatched(void *topaddr, int bufferwidth, int pixelformat, int sync) {
  log_frame++;
  if (log_frame == 30)
    logGameId(); // en module_start el disco aun no esta montado
  // Cada buffer distinto que el juego muestra; util para videos (FMV) que no pasan por el GE
  LOG_ONCE((u32)topaddr ^ (bufferwidth << 20) ^ (pixelformat << 30), "display %08lx ancho=%d fmt=%d\n",
           (u32)topaddr, bufferwidth, pixelformat);

  // Si este frame no paso por el GE (video por CPU) o tuvo una copia de bloque grande
  // al framebuffer (video/logo con sceGuCopyImage), el buffer de alta resolucion no lo
  // tiene: se muestra la imagen original escalada en vez de una pantalla negra.
  if (!draws_since_flip)
    st.idle_frames++;
  if ((!draws_since_flip || big_transfer) && hasContent((u32)topaddr, bufferwidth, pixelformat)) {
    LOG_ONCE(0xE5000000, "imagen original escalada (video/imagen fuera del GE)\n");
    upscaleGameBuffer((u32)topaddr, bufferwidth, pixelformat);
    st.fallback++;
  } else {
    copyFrameBuffer();
  }
  draws_since_flip = big_transfer = 0;
  last_flip = sceKernelGetSystemTimeLow();

  if (log_frame % STATS_EVERY == 0) {
    LOG("stats listas=%lu head=%lu stall=%lu draws=%lu omitidos=%lu frames_sin_draws=%lu transfers=%lu a_fb=%lu "
        "escalados=%lu copias_en_lista=%lu pila_llena=%lu fbs=%lu\n",
        st.lists, st.heads, st.stalls, st.draws, st.skipped, st.idle_frames, st.transfers, st.fb_transfers,
        st.fallback, st.enq_copies, st.stack_full, state.framebuf_count);
    memset(&st, 0, sizeof(st));
  }

  return _sceDisplaySetFrameBuf(topaddr, bufferwidth, pixelformat, sync);
}

int module_start(SceSize args, void *argp) {
  SceCtrlData pad;
  sceCtrlPeekBufferPositive(&pad, 1);
  if (pad.Buttons & PSP_CTRL_LTRIGGER)
    return 0;

  logInit(pad.Buttons & PSP_CTRL_RTRIGGER);

  _sceGeEdramGetAddr = (void *)FindProc("sceGE_Manager", "sceGe_driver", 0xE47E40E4);
  _sceGeEdramGetSize = (void *)FindProc("sceGE_Manager", "sceGe_driver", 0x1F6752AD);
  _sceGeGetList = (void *)FindProc("sceGE_Manager", "sceGe_driver", 0x67B01D8E);
  _sceGeListUpdateStallAddr = (void *)FindProc("sceGE_Manager", "sceGe_driver", 0xE0D68148);
  _sceGeListEnQueue = (void *)FindProc("sceGE_Manager", "sceGe_driver", 0xAB49E76A);
  _sceGeListEnQueueHead = (void *)FindProc("sceGE_Manager", "sceGe_driver", 0x1C0D95A6);

  if (!_sceGeEdramGetAddr || !_sceGeEdramGetSize || !_sceGeGetList ||
      !_sceGeListUpdateStallAddr || !_sceGeListEnQueue || !_sceGeListEnQueueHead)
    LOG("ERROR: no se encontro alguna funcion de sceGe_driver\n");

  sctrlHENPatchSyscall((u32)_sceGeEdramGetAddr, sceGeEdramGetAddrPatched);
  sctrlHENPatchSyscall((u32)_sceGeEdramGetSize, sceGeEdramGetSizePatched);
  sctrlHENPatchSyscall((u32)_sceGeListUpdateStallAddr, sceGeListUpdateStallAddrPatched);
  sctrlHENPatchSyscall((u32)_sceGeListEnQueue, sceGeListEnQueuePatched);
  sctrlHENPatchSyscall((u32)_sceGeListEnQueueHead, sceGeListEnQueueHeadPatched);

  _sceDisplaySetFrameBuf = (void *)FindProc("sceDisplay_Service", "sceDisplay_driver", 0x289D82FE);
  if (!_sceDisplaySetFrameBuf)
    LOG("ERROR: no se encontro sceDisplaySetFrameBuf\n");
  sctrlHENPatchSyscall((u32)_sceDisplaySetFrameBuf, sceDisplaySetFrameBufPatched);

  sceKernelDcacheWritebackInvalidateAll();
  sceKernelIcacheClearAll();

  return 0;
}
