// Recorte de triangulos 3D grandes (incluido desde main.c: usa su estado).
//
// El GE descarta un triangulo entero si un vertice cae fuera de las coordenadas de pantalla
// 0..4096 (no lo recorta). A 480x272 el juego tiene ~1800 px de margen a cada lado; al doble,
// ~780 px del juego: los triangulos grandes cerca de la camara (el camino bajo el auto en
// Ridge Racer) desaparecian. Aqui se calcula en la CPU donde caen los vertices de cada draw
// 3D (con las matrices que carga el juego) y los triangulos que el juego si dibuja a 1x pero
// que al doble se salen se recortan contra el rango (y el plano cercano): se queda la parte
// que el GE puede dibujar, con los atributos interpolados en el espacio del modelo (la
// perspectiva y las texturas quedan iguales). El draw pasa a un CALL a la lista nueva.
// dev47 los partia en 4 con puntos medios: la esquina que salia nunca entraba en el rango.
// Comprobado reproduciendo un volcado de Ridge Racer en el modelo (el camino vuelve).

#define SUB_CLIP 0x0AE00000 // coordenadas de recorte de los vertices del draw (4096 x 16 bytes)
#define SUB_MAXV 4096
#define SUB_FLAGS 0x0AE10000 // banderas de subFlags de cada vertice (4096)
#define SUB_RING 0x0AE11000 // vertices nuevos (anillo hasta 0x0AF00000)
#define SUB_END  0x0AF00000
// Rango seguro al doble: el GE transforma con floats de 24 bits y un vertice recortado justo
// en 0 o 4095 podia caer afuera (en el modelo los triangulos desaparecian igual)
#define SUB_LO 128
#define SUB_HI 3968

static struct { float w[12], v[12], p[16]; u8 iw, iv, ip, ok; } mtx; // ok: bits 1 mundo, 2 vista, 4 proyeccion

static inline float f24f(u32 d) { union { u32 i; float f; } t; t.i = d << 8; return t.f; }

static void mtxCmd(u32 cmd, u32 data) {
  switch (cmd) {
    case GE_CMD_WORLDMATRIXNUMBER: mtx.iw = data & 0xf; break;
    case GE_CMD_VIEWMATRIXNUMBER: mtx.iv = data & 0xf; break;
    case GE_CMD_PROJMATRIXNUMBER: mtx.ip = data & 0xf; break;
    case GE_CMD_WORLDMATRIXDATA: if (mtx.iw < 12) { mtx.w[mtx.iw++] = f24f(data); if (mtx.iw == 12) mtx.ok |= 1; } break;
    case GE_CMD_VIEWMATRIXDATA: if (mtx.iv < 12) { mtx.v[mtx.iv++] = f24f(data); if (mtx.iv == 12) mtx.ok |= 2; } break;
    case GE_CMD_PROJMATRIXDATA: if (mtx.ip < 16) { mtx.p[mtx.ip++] = f24f(data); if (mtx.ip == 16) mtx.ok |= 4; } break;
  }
}

// Formato del vertice: por atributo (tc, color, normal, posicion) desplazamiento, cantidad y
// tipo de componente. Tipos: 1 u8, 2 u16, 3 float, 4 s8, 5 s16, 6 565, 7 5551, 8 4444, 9 8888
typedef struct { u8 off, n, type; } SubAttr;
typedef struct { SubAttr a[4]; u8 size, npos; } SubFmt;

static int subFormat(u32 vt, SubFmt *f) {
  static const u8 tcT[] = { 0, 1, 2, 3 }, nT[] = { 0, 4, 5, 3 }, colT[] = { 0, 0, 0, 0, 6, 7, 8, 9 };
  static const u8 sz[] = { 0, 1, 2, 4, 1, 2, 2, 2, 2, 4 };
  u32 tc = vt & 3, col = (vt >> 2) & 7, nrm = (vt >> 5) & 3, pos = (vt >> 7) & 3, off = 0, al = 1, i;
  if (!pos || (vt & (GE_VTYPE_WEIGHT_MASK | GE_VTYPE_MORPHCOUNT_MASK)) || (col && col < 4))
    return 0;
  SubAttr *a = f->a;
  u8 types[4] = { tcT[tc], colT[col], nT[nrm], nT[pos] }, ns[4] = { 2, 1, 3, 3 };
  for (i = 0; i < 4; i++) {
    a[i].type = types[i]; a[i].n = types[i] ? ns[i] : 0;
    if (!types[i])
      continue;
    u32 s = sz[types[i]];
    off = (off + s - 1) & ~(s - 1);
    a[i].off = off; off += s * a[i].n;
    if (s > al) al = s;
  }
  f->size = (off + al - 1) & ~(al - 1);
  f->npos = a[3].type;
  return f->size;
}

// Interpolacion entre dos vertices (x + t (y - x)), componente por componente
static void subLerp(const SubFmt *f, const u8 *x, const u8 *y, float t, u8 *o) {
  int i, k;
  memcpy(o, x, f->size);
  #define L(a, b) ((a) + t * ((float)(b) - (float)(a)))
  for (i = 0; i < 4; i++) {
    const SubAttr *a = &f->a[i];
    for (k = 0; k < a->n; k++)
      switch (a->type) {
        case 1: o[a->off + k] = (u8)(L(x[a->off + k], y[a->off + k]) + 0.5f); break;
        case 4: ((signed char *)o)[a->off + k] = (signed char)L(((signed char *)x)[a->off + k], ((signed char *)y)[a->off + k]); break;
        case 2: ((u16 *)(o + a->off))[k] = (u16)(L(((u16 *)(x + a->off))[k], ((u16 *)(y + a->off))[k]) + 0.5f); break;
        case 5: ((short *)(o + a->off))[k] = (short)L(((short *)(x + a->off))[k], ((short *)(y + a->off))[k]); break;
        case 3: ((float *)(o + a->off))[k] = L(((float *)(x + a->off))[k], ((float *)(y + a->off))[k]); break;
        case 9: { int c; for (c = 0; c < 4; c++) o[a->off + c] = (u8)(L(x[a->off + c], y[a->off + c]) + 0.5f); break; }
        default: { // 16 bits empaquetado: por canal
          static const u8 bits[3][4] = { { 5, 6, 5, 0 }, { 5, 5, 5, 1 }, { 4, 4, 4, 4 } };
          const u8 *b = bits[a->type - 6];
          u32 cx = *(u16 *)(x + a->off), cy = *(u16 *)(y + a->off), r = 0, sh = 0, c;
          for (c = 0; c < 4; c++) {
            u32 m = (1 << b[c]) - 1;
            r |= ((u32)(L((cx >> sh) & m, (cy >> sh) & m) + 0.5f) & m) << sh;
            sh += b[c];
          }
          *(u16 *)(o + a->off) = r;
        }
      }
  }
  #undef L
}

static void subPos(const SubFmt *f, const u8 *v, float *p) {
  const SubAttr *a = &f->a[3];
  int k;
  for (k = 0; k < 3; k++)
    p[k] = a->type == 3 ? ((float *)(v + a->off))[k] : a->type == 5 ? ((short *)(v + a->off))[k] * (1.0f / 32768) :
           ((signed char *)(v + a->off))[k] * (1.0f / 128);
}

// Coordenadas de recorte de una posicion (proyeccion x vista x mundo)
static void subClip(const float *p, float *c) {
  float w[3], e[3];
  int k;
  for (k = 0; k < 3; k++)
    w[k] = p[0] * mtx.w[k] + p[1] * mtx.w[3 + k] + p[2] * mtx.w[6 + k] + mtx.w[9 + k];
  for (k = 0; k < 3; k++)
    e[k] = w[0] * mtx.v[k] + w[1] * mtx.v[3 + k] + w[2] * mtx.v[6 + k] + mtx.v[9 + k];
  for (k = 0; k < 4; k++)
    c[k] = e[0] * mtx.p[k] + e[1] * mtx.p[4 + k] + e[2] * mtx.p[8 + k] + mtx.p[12 + k];
}

// Viewport del juego (1x) y el del buffer grande (2x)
static struct { float sx, sy, cx, cy, sx2, sy2, cx2, cy2; int dc; } vp;

// Bits: 1 dentro del rango a 1x, 2 dentro a 2x, 4 a la izquierda/8 derecha/16 arriba/32 abajo
// de lo visible a 2x (para descartar trozos fuera de la pantalla), 0x40 detras del plano cercano
static int subFlags(const float *c) {
  // Detras del plano cercano: a 1x el GE lo recorta y no mira su rango; al doble los puntos de
  // corte suelen caer fuera (el camino bajo el auto), asi que lo recortamos nosotros
  if (vp.dc && c[2] < -c[3])
    return 0x41;
  if (c[3] <= 0)
    return vp.dc ? 0x41 : 0;
  float iw = 1.0f / c[3], x = c[0] * iw, y = c[1] * iw;
  float x1 = x * vp.sx + vp.cx, y1 = y * vp.sy + vp.cy, x2 = x * vp.sx2 + vp.cx2, y2 = y * vp.sy2 + vp.cy2;
  int r = (x1 >= 0 && x1 < 4096 && y1 >= 0 && y1 < 4096) | (x2 >= SUB_LO && x2 < SUB_HI && y2 >= SUB_LO && y2 < SUB_HI) << 1;
  r |= (x2 < 2048 - WIDTH / 2) << 2 | (x2 >= 2048 + WIDTH / 2) << 3 | (y2 < 2048 - HEIGHT / 2) << 4 | (y2 >= 2048 + HEIGHT / 2) << 5;
  return r;
}

static u32 sub_out, sub_end, sub_tris; // escritura en el anillo
static const SubFmt *sub_f;

static void subEmit(const u8 *a, const u8 *b, const u8 *c) {
  if (sub_out + 3 * sub_f->size > sub_end)
    return;
  u8 *o = (u8 *)(sub_out | 0x40000000);
  memcpy(o, a, sub_f->size); memcpy(o + sub_f->size, b, sub_f->size); memcpy(o + 2 * sub_f->size, c, sub_f->size);
  sub_out += 3 * sub_f->size;
  sub_tris++;
}

// Recorta el triangulo contra el plano cercano (con el recorte de profundidad del juego), w > 0
// y el rango seguro al doble. Todos los planos son lineales en las coordenadas de recorte
// (x2 >= L <=> s X + (c - L) W >= 0), asi que el punto de corte y sus atributos se interpolan
// con el mismo t. El poligono que queda (hasta 9 vertices) va en abanico, con el mismo giro.
static void subClipTri(const u8 *a, const u8 *b, const u8 *c, const float *ca, const float *cb, const float *cc) {
  static u8 vb[2][10][48];
  static float cl[2][10][4];
  int n = 3, s = 0, p, i, k;
  memcpy(vb[0][0], a, sub_f->size); memcpy(vb[0][1], b, sub_f->size); memcpy(vb[0][2], c, sub_f->size);
  memcpy(cl[0][0], ca, 16); memcpy(cl[0][1], cb, 16); memcpy(cl[0][2], cc, 16);
  for (p = vp.dc ? -2 : -1; p < 4 && n >= 3; p++) {
    float d[10];
    int m = 0;
    for (i = 0; i < n; i++) { // distancia al plano (>= 0 dentro)
      const float *q = cl[s][i];
      if (p == -2) d[i] = q[2] + q[3];          // z >= -w
      else if (p == -1) d[i] = q[3] - 0.001f;   // w > 0
      else {
        float sc = p < 2 ? vp.sx2 : vp.sy2, ce = p < 2 ? vp.cx2 : vp.cy2;
        d[i] = p & 1 ? (SUB_HI - ce) * q[3] - sc * q[p < 2 ? 0 : 1] : sc * q[p < 2 ? 0 : 1] + (ce - SUB_LO) * q[3];
      }
    }
    for (i = 0; i < n && m < 9; i++) {
      int j = (i + 1) % n;
      if (d[i] >= 0) {
        memcpy(vb[!s][m], vb[s][i], sub_f->size); memcpy(cl[!s][m], cl[s][i], 16); m++;
      }
      if ((d[i] >= 0) != (d[j] >= 0) && m < 9) {
        float t = d[i] / (d[i] - d[j]);
        subLerp(sub_f, vb[s][i], vb[s][j], t, vb[!s][m]);
        for (k = 0; k < 4; k++)
          cl[!s][m][k] = cl[s][i][k] + t * (cl[s][j][k] - cl[s][i][k]);
        m++;
      }
    }
    n = m; s = !s;
  }
  for (i = 1; i + 1 < n; i++)
    subEmit(vb[s][0], vb[s][i], vb[s][i + 1]);
}

static void subTri(const u8 *a, const u8 *b, const u8 *c, const float *ca, const float *cb, const float *cc) {
  int fa = subFlags(ca), fb = subFlags(cb), fc = subFlags(cc);
  if (fa & fb & fc & 0x3c)
    return; // todo fuera de la pantalla, del mismo lado
  if (fa & fb & fc & 2)
    subEmit(a, b, c);
  else
    subClipTri(a, b, c, ca, cb, cc);
}

// Vertices del triangulo t del draw (como lista, con el orden de giro de las tiras)
static inline void subTriIdx(u32 prim, u32 t, u32 *i) {
  if (prim == GE_PRIM_TRIANGLES) { i[0] = 3 * t; i[1] = 3 * t + 1; i[2] = 3 * t + 2; }
  else if (prim == GE_PRIM_TRIANGLE_STRIP) { i[0] = t + (t & 1); i[1] = t + 1 - (t & 1); i[2] = t + 2; }
  else { i[0] = 0; i[1] = t + 1; i[2] = t + 2; }
}

// 0: nada que hacer (lejos de los bordes), 1: cerca pero ningun triangulo se sale, 2: el draw
// paso a un CALL con los triangulos subdivididos
static int subDraw(u32 *list, u32 op, u32 count) {
  u32 vt = state.vertex_type, prim = (op >> 16) & 7, *reg = list - 1, c, sl, k, t, ntri, ix[3];
  int idx = (vt & GE_VTYPE_IDX_MASK) >> GE_VTYPE_IDX_SHIFT;
  u8 gs = 0, po = 0, to = 0;
  SubFmt f;
  if (mtx.ok != 7 || count < 3 || prim < GE_PRIM_TRIANGLES || prim > GE_PRIM_TRIANGLE_FAN || idx == 3 || !subFormat(vt, &f) || f.size > 48)
    return 0;
  vertexInfo(vt, &gs, &po, &to);
  if (gs != f.size)
    return 0;
  u16 lower = 0, upper = count;
  if (idx) {
    if (!validAddr(state.index_addr) || !validAddr(state.index_addr + count * idx))
      return 0;
    GetIndexBounds((void *)state.index_addr, count, vt, &lower, &upper);
    upper += 1;
  }
  if (upper - lower > SUB_MAXV || !validAddr(state.vertex_addr) || !validAddr(state.vertex_addr + upper * f.size))
    return 0;
  vp.sx = f24f(state.ge_cmd[GE_CMD_VIEWPORTXSCALE]); vp.sy = f24f(state.ge_cmd[GE_CMD_VIEWPORTYSCALE]);
  vp.cx = f24f(state.ge_cmd[GE_CMD_VIEWPORTXCENTER]); vp.cy = f24f(state.ge_cmd[GE_CMD_VIEWPORTYCENTER]);
  vp.sx2 = 2 * vp.sx; vp.sy2 = 2 * vp.sy;
  vp.cx2 = f24f(vpPatch(GE_CMD_VIEWPORTXCENTER, state.ge_cmd[GE_CMD_VIEWPORTXCENTER]));
  vp.cy2 = f24f(vpPatch(GE_CMD_VIEWPORTYCENTER, state.ge_cmd[GE_CMD_VIEWPORTYCENTER]));
  vp.dc = state.ge_cmd[GE_CMD_DEPTHCLAMPENABLE] & 1;
  // Caja del modelo: si sus 8 esquinas caen dentro del rango al doble, ningun triangulo se sale
  float lo[3] = { 1e30f, 1e30f, 1e30f }, hi[3] = { -1e30f, -1e30f, -1e30f }, p[3], cl[4];
  for (k = lower; k < upper; k++) {
    subPos(&f, (u8 *)(state.vertex_addr + k * f.size), p);
    for (c = 0; c < 3; c++) { if (p[c] < lo[c]) lo[c] = p[c]; if (p[c] > hi[c]) hi[c] = p[c]; }
  }
  int all = 2;
  for (k = 0; k < 8 && all; k++) {
    float q[3] = { k & 1 ? hi[0] : lo[0], k & 2 ? hi[1] : lo[1], k & 4 ? hi[2] : lo[2] };
    subClip(q, cl);
    all &= cl[3] > 0 && !(vp.dc && cl[2] < -cl[3]) ? subFlags(cl) : 0;
  }
  if (all)
    return 0;
  // Coordenadas de recorte de cada vertice y triangulos que se salen solo al doble
  float *clip = (float *)SUB_CLIP; // solo la CPU: memoria con cache
  u8 *fl = (u8 *)SUB_FLAGS;
  for (k = lower; k < upper; k++) {
    subPos(&f, (u8 *)(state.vertex_addr + k * f.size), p);
    subClip(p, clip + 4 * (k - lower));
    fl[k - lower] = subFlags(clip + 4 * (k - lower));
  }
  #define VI(n) ((idx == 1 ? ((u8 *)state.index_addr)[n] : idx == 2 ? ((u16 *)state.index_addr)[n] : (n)) - lower)
  ntri = prim == GE_PRIM_TRIANGLES ? count / 3 : count - 2;
  int need = 0;
  for (t = 0; t < ntri && !need; t++) {
    subTriIdx(prim, t, ix);
    int a = fl[VI(ix[0])], b = fl[VI(ix[1])], d = fl[VI(ix[2])];
    need = (a & b & d & 1) && !(a & b & d & 2) && !(a & b & d & 0x3c);
  }
  if (!need)
    return 1;
  c = origOp(reg) >> 24;
  if (!validAddr((u32)reg) || c == GE_CMD_CALL || c == GE_CMD_JUMP || c == GE_CMD_BJUMP || c == GE_CMD_RET || c == GE_CMD_END ||
      c == GE_CMD_FINISH || c == GE_CMD_SIGNAL || c == GE_CMD_PRIM || c == GE_CMD_BEZIER || c == GE_CMD_SPLINE ||
      c == GE_CMD_TRANSFERSTART || c == GE_CMD_ORIGIN || !(sl = txSlot()))
    return 1;
  // Triangulos nuevos en el anillo (si queda poco, vuelve al principio)
  if (sub_out < SUB_RING || sub_out + 0x20000 > SUB_END)
    sub_out = SUB_RING;
  u32 start = sub_out;
  sub_end = SUB_END; sub_f = &f;
  u32 tris0 = sub_tris;
  for (t = 0; t < ntri; t++) {
    subTriIdx(prim, t, ix);
    u32 i0 = VI(ix[0]), i1 = VI(ix[1]), i2 = VI(ix[2]);
    float *c0 = clip + 4 * i0, *c1 = clip + 4 * i1, *c2 = clip + 4 * i2;
    if (!(fl[i0] & fl[i1] & fl[i2] & 1))
      continue; // el juego tampoco lo dibuja (fuera del rango a 1x)
    subTri((u8 *)(state.vertex_addr + (i0 + lower) * f.size), (u8 *)(state.vertex_addr + (i1 + lower) * f.size),
           (u8 *)(state.vertex_addr + (i2 + lower) * f.size), c0, c1, c2);
  }
  #undef VI
  u32 n = (sub_out - start) / f.size, *l = (u32 *)(sl | 0x40000000), prev = *reg;
  #define C(c, d) (*l++ = ((c) << 24) | ((d) & 0xffffff))
  C(GE_CMD_BASE, state.base >> 8);
  *l++ = prev;
  C(GE_CMD_VERTEXTYPE, vt & ~GE_VTYPE_IDX_MASK);
  for (k = 0; k < n; k += 65535) {
    u32 a = start + k * f.size, m = n - k < 65535 ? n - k : 65535;
    C(GE_CMD_BASE, (a >> 8) & 0x0f0000); C(GE_CMD_VADDR, a);
    C(GE_CMD_PRIM, (GE_PRIM_TRIANGLES << 16) | m);
  }
  C(GE_CMD_VERTEXTYPE, vt);
  u32 nv = idx ? state.vertex_addr : state.vertex_addr + count * f.size; // como lo deja el draw del juego
  C(GE_CMD_BASE, (nv >> 8) & 0x0f0000); C(GE_CMD_VADDR, nv);
  if (idx) {
    u32 ni = state.index_addr + count * idx;
    C(GE_CMD_BASE, (ni >> 8) & 0x0f0000); C(GE_CMD_IADDR, ni);
  }
  C(GE_CMD_BASE, state.base >> 8);
  C(GE_CMD_RET, 0);
  #undef C
  txCall(list, reg, sl);
  st.sub_draws++;
  st.sub_tris += sub_tris - tris0;
  LOG_ONCE(0x5B000000, "triangulos 3D que se salian del rango al doble: se subdividen (primer draw: %lu vertices -> %lu)\n", count, n);
  return 2;
}
