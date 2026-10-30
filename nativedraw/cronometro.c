// cronometro.c (incluido en presentador.c): cuanto tarda la GPU de la Vita en cada escena. Solo con el interruptor
// ux0:data/nativedraw/medir_gpu.
// Las escenas de Adrenaline y las nuestras van por el mismo contexto, asi que la GPU las termina en el orden en que se
// mandaron. Cada sceGxmEndScene anota en una cola el tipo de escena, sus draws, la hora de envio y la notificacion que
// la GPU escribe al terminar (a las de Adrenaline, que no traen, se les pone una propia). Un hilo mira cada ~0,1 ms la
// escena mas vieja de la cola; cuando la GPU la termina, su tiempo es: hora de fin - (envio o fin de la anterior, la mas
// tarde). Si la GPU estaba sin nada cuando llego la escena, ese hueco se suma como tiempo ocioso. Cada 10 s el
// presentador anota los totales por tipo y, en detalle, las escenas que siguen al informe.
enum { C_ADR, C_NORMAL, C_TR, C_INT, CN };
static const char *cnom[CN] = { "Adrenaline (pantalla)", "imagen normal al doble", "traductor (superficie)", "traductor (intermedia)" };
typedef struct { volatile uint32_t *a; uint32_t v, t, tipo, dr; } CEsc;
#define CQ 64
static CEsc cq[CQ];
static volatile uint32_t c_in, c_out, c_on, c_lleno, c_vueltas, c_ocio;
static SceGxmNotification c_adr;  // la notificacion que se le pone a las escenas de Adrenaline (palabra c_slot)
static uint32_t c_slot, c_av;
// por tipo: escenas, us de GPU, maximo, draws, us en cola (enviada y la GPU ocupada con otra), histograma de ms
// (<5, 5-10, 10-15, 15-20, 20-30, 30 o mas)
static volatile uint32_t c_n[CN], c_us[CN], c_max[CN], c_dr[CN], c_cola[CN], c_h[CN][6];
#define CD 16
static struct { uint8_t tipo; uint16_t dr; uint32_t cola, us; } c_det[CD];  // detalle: las escenas que siguen al informe
static volatile uint32_t c_dn = CD;  // escenas ya anotadas en c_det (CD: lleno, esperando que el informe lo imprima)

// anota en la cola una escena recien mandada (n: su notificacion de fin; dr: draws)
static void cAnota(int tipo, const SceGxmNotification *n, uint32_t dr) {
  if (!c_on || !n) return;
  uint32_t i = c_in;
  if (i - c_out >= CQ) { c_lleno++; return; }  // (la escena siguiente se lleva su tiempo)
  CEsc *e = &cq[i % CQ];
  e->a = n->address; e->v = n->value; e->tipo = tipo; e->dr = dr; e->t = now();
  __sync_synchronize();
  c_in = i + 1;
}
// para el gancho de sceGxmEndScene de Adrenaline: si su escena no trae notificacion de fin, la nuestra
static const SceGxmNotification *cAdr(const SceGxmNotification *f) {
  if (!c_on || f) return f;
  c_adr.value = ++c_av;
  return &c_adr;
}
// hilo: saca de la cola las escenas que la GPU termino y suma sus tiempos
static int cHilo(SceSize args, void *argp) {
  uint32_t prev = now();
  for (;;) {
    sceKernelDelayThread(100);
    c_vueltas++;
    while (c_out != c_in) {
      CEsc *e = &cq[c_out % CQ];
      if ((int)(*e->a - e->v) < 0) break;
      uint32_t t = now(), ini = prev;
      if ((int)(e->t - prev) > 0) { c_ocio += e->t - prev; ini = e->t; }  // la GPU no tenia nada: hueco ocioso
      uint32_t us = t - ini, k = e->tipo, ms = us / 1000;
      c_n[k]++; c_us[k] += us; c_dr[k] += e->dr; c_cola[k] += ini - e->t;
      if (us > c_max[k]) c_max[k] = us;
      c_h[k][ms < 20 ? ms / 5 : ms < 30 ? 4 : 5]++;
      if (c_dn < CD) { c_det[c_dn].tipo = k; c_det[c_dn].dr = e->dr; c_det[c_dn].cola = ini - e->t; c_det[c_dn].us = us; c_dn++; }
      prev = t; c_out++;
    }
  }
  return 0;
}
// lo llama el presentador al marcar listo: elige la palabra para Adrenaline (la libre debajo de gq_slot; adr: bits de
// las que usa Adrenaline) y arranca el hilo
static void cInicio(volatile uint32_t *nr, const uint32_t *adr) {
  if (!nr || !gq_fin) return;
  for (c_slot = gq_slot - 1; c_slot > 400 && (adr[c_slot >> 5] >> (c_slot & 31) & 1); c_slot--) {}
  c_adr.address = nr + c_slot; *c_adr.address = 0;
  SceUID th = sceKernelCreateThread("nativedraw_cronometro", cHilo, 0x40, 0x1000, 0, 0, 0);
  if (th >= 0 && sceKernelStartThread(th, 0, 0) >= 0) c_on = 1;
  alog("cronometro de la GPU: %s (notificacion %u para las escenas de Adrenaline)\n", c_on ? "encendido" : "no se pudo crear el hilo", c_slot);
}
// informe de cada 10 s (dt: ms desde el anterior): una linea por tipo, el total y el detalle de unas escenas seguidas
static void cInforme(uint32_t dt) {
  uint32_t tot = 0, oc = c_ocio, vu = c_vueltas, ll = c_lleno;
  c_ocio = c_vueltas = c_lleno = 0;
  for (int k = 0; k < CN; k++) {
    uint32_t n = c_n[k], us = c_us[k], mx = c_max[k], dr = c_dr[k], co = c_cola[k], h[6];
    for (int j = 0; j < 6; j++) { h[j] = c_h[k][j]; c_h[k][j] = 0; }
    c_n[k] = c_us[k] = c_max[k] = c_dr[k] = c_cola[k] = 0;
    tot += us;
    if (!n) continue;
    alog("gpu tiempos: %s: escenas %u, GPU %u ms (promedio %u.%02u ms, max %u.%u) | draws por escena %u | en cola promedio %u.%u ms | "
         "ms: <5 %u, 5-10 %u, 10-15 %u, 15-20 %u, 20-30 %u, 30+ %u\n", cnom[k], n, us / 1000, us / n / 1000, us / n / 10 % 100,
         mx / 1000, mx / 100 % 10, dr / n, co / n / 1000, co / n / 100 % 10, h[0], h[1], h[2], h[3], h[4], h[5]);
  }
  alog("gpu tiempos: ocupada %u ms de %u (%u %%), ociosa %u ms | cola llena %u | vueltas del hilo %u (una cada %u us)\n",
       tot / 1000, dt, dt ? tot / 10 / dt : 0, oc / 1000, ll, vu, vu ? dt * 1000 / vu : 0);
  if (c_dn == CD) {  // detalle de las escenas que siguieron al informe anterior (en el orden en que las termino la GPU)
    char b[400]; int n = 0;
    static const char *corto[CN] = { "A", "N", "T", "I" };
    for (int i = 0; i < CD && n < (int)sizeof b - 40; i++)
      n += sceClibSnprintf(b + n, sizeof b - n, " %s%u:%u.%u/%u.%u", corto[c_det[i].tipo], c_det[i].dr, c_det[i].us / 1000,
                           c_det[i].us / 100 % 10, c_det[i].cola / 1000, c_det[i].cola / 100 % 10);
    b[n] = 0;
    alog("gpu tiempos: detalle (tipo y draws: ms de GPU / ms en cola; A Adrenaline, N imagen normal, T traductor, I intermedia):%s\n", b);
  }
  c_dn = 0;
}
