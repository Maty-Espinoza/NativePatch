#!/usr/bin/env python3
"""Análisis de KBENCH: lee la carpeta ms0:/kbench (bajada de la Vita) y calcula el costo de usar el ARM.

Uso:
  python3 kbench.py CARPETA [--comandos 55000] [--llamadas 40] [--ns-psp 300] [--fps 30]

--comandos   comandos del GE recorridos por frame en el juego (GT: 40-70 mil)
--llamadas   llamadas a patchGeList por frame (log de GePatch: listas + stall por frame)
--ns-psp     costo medido de GePatch por comando en el PSP (GT, dev15: ~300 ns)
Solo biblioteca estándar.
"""
import argparse, csv, os, statistics as stt
from collections import defaultdict

def s32(x):  # diferencia de relojes de 32 bits con vuelta
    x &= 0xFFFFFFFF
    return x - (1 << 32) if x >= 1 << 31 else x

def pct(v, p):
    v = sorted(v)
    return v[min(len(v) - 1, int(len(v) * p / 100))] if v else 0

def resumen(v):
    if not v: return "sin datos"
    return f"n={len(v)} min={min(v)} p50={pct(v,50)} p90={pct(v,90)} p99={pct(v,99)} max={max(v)} us"

def leer(carpeta, nombre):
    p = os.path.join(carpeta, nombre)
    if not os.path.exists(p): return []
    with open(p, newline="") as f:
        return [r for r in csv.DictReader(f) if r]

def pings(filas, titulo):
    """Agrupa por prueba y x1; separa la ida y la vuelta con el desfase de la muestra de menor RTT."""
    g = defaultdict(list)
    for r in filas:
        if int(r["ret"]) >= 0: g[(r["prueba"], r["x1"], r["x2"])].append(r)
    res = {}
    print(f"\n== {titulo} ==")
    for (prueba, x1, x2), fs in g.items():
        rtt = [int(r["rtt_us"]) for r in fs]
        svc = [int(r["arm_svc_us"]) for r in fs if int(r["arm_t0"])]
        etiqueta = prueba + (f" sondeo={x1}us" if prueba.startswith("mem") and x1 != "0" else "") \
                   + (f" afinidad={x1} prio={x2}" if "afinidad" in prueba else "")
        print(f"{etiqueta}\n  ida y vuelta: {resumen(rtt)}\n  servicio ARM: {resumen(svc)}")
        con = [r for r in fs if int(r["arm_t0"])]
        if con:  # desfase de relojes con la muestra más rápida (supone ida = vuelta en esa muestra)
            m = min(con, key=lambda r: int(r["rtt_us"]))
            off = (s32(int(m["arm_t0"]) - int(m["psp_t0"])) + s32(int(m["arm_t3"]) - int(m["psp_t3"]))) / 2
            ida = [s32(int(r["arm_t0"]) - int(r["psp_t0"])) - off for r in con]
            vuelta = [s32(int(r["psp_t3"]) - int(r["arm_t3"])) + off for r in con]
            print(f"  ida PSP->ARM p50={pct(ida,50):.0f} p99={pct(ida,99):.0f} us; vuelta ARM->PSP p50={pct(vuelta,50):.0f} p99={pct(vuelta,99):.0f} us")
        res[(prueba, x1, x2)] = (pct(rtt, 50), pct(rtt, 99), pct(svc, 50) if svc else 0)
    return res

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("carpeta")
    ap.add_argument("--comandos", type=int, default=55000)
    ap.add_argument("--llamadas", type=int, default=40)
    ap.add_argument("--ns-psp", type=float, default=300)
    ap.add_argument("--fps", type=int, default=30)
    a = ap.parse_args()
    c = a.carpeta

    for nombre in ("resumen.txt", "resumen_juego.txt"):
        p = os.path.join(c, nombre)
        if os.path.exists(p):
            print(f"== {nombre} ==")
            print(open(p, encoding="utf-8", errors="replace").read())

    rp = pings(leer(c, "ping.csv"), "pings (EBOOT, sin juego)")
    pings(leer(c, "afinidad.csv"), "afinidad y prioridad")
    rj = pings(leer(c, "juego.csv"), "pings con el juego corriendo")

    print("\n== lectura de la RAM del PSP desde el ARM ==")
    conv = {}
    for fl in ("1", "0"):
        for r in sorted({int(x["bytes"]) for x in leer(c, "lectura.csv")}):
            fs = [x for x in leer(c, "lectura.csv") if int(x["bytes"]) == r and x["flags"] == fl]
            if not fs: continue
            cv = [int(x["arm_conv_us"]) for x in fs]; rd = [int(x["arm_leer_us"]) for x in fs]
            ok = sum(int(x["ok"]) for x in fs)
            print(f"  {r:8d} B {'invalidando' if fl == '1' else 'sin invalidar'}: rtt p50={pct([int(x['rtt_us']) for x in fs],50)} "
                  f"convertir p50={pct(cv,50)} leer p50={pct(rd,50)} us, datos correctos {ok}/{len(fs)}")
            if fl == "1": conv[r] = pct(cv, 50)
    if len(conv) >= 2:  # costo de invalidar por KB (recta por mínimos cuadrados)
        xs, ys = list(conv), [conv[k] for k in conv]
        mx, my = stt.mean(xs), stt.mean(ys)
        b = sum((x - mx) * (y - my) for x, y in zip(xs, ys)) / max(1, sum((x - mx) ** 2 for x in xs))
        inv_kb, inv_fijo = b * 1024, my - b * mx
        print(f"  invalidar en el ARM: ~{inv_fijo:.1f} us fijos + {inv_kb:.3f} us por KB")
    else:
        inv_kb = inv_fijo = 0

    print("\n== escritura del ARM en la RAM del PSP ==")
    porb = defaultdict(list)
    for x in leer(c, "escritura.csv"): porb[int(x["bytes"])].append(x)
    for b_, fs in sorted(porb.items()):
        malas = sum(int(x["malas_sin_cache"]) + int(x["malas_con_cache"]) for x in fs)
        print(f"  {b_:8d} B: escribir p50={pct([int(x['arm_escribir_us']) for x in fs],50)} writeback p50="
              f"{pct([int(x['arm_wb_us']) for x in fs],50)} us, palabras malas {malas}")

    print("\n== recorrido de la lista sintética (mismo código) ==")
    rec = defaultdict(list)
    for x in leer(c, "recorrido.csv"): rec[(x["lado"], x["variante"], x["cpu_mhz"])].append(x)
    ns = {}
    for (lado, var, mhz), fs in rec.items():
        if lado == "psp":
            v = pct([int(x["ns_por_cmd"]) for x in fs], 50); ns[(lado, var, mhz)] = v
            print(f"  PSP {var} a {mhz} MHz: {v} ns por comando")
        else:
            v = pct([int(x["ns_por_cmd"]) for x in fs], 50); ns[(lado, var)] = v
            print(f"  {lado} {var} (ARM a {mhz} MHz): recorrer {v} ns por comando, convertir p50="
                  f"{pct([int(x['arm_conv_us']) for x in fs],50)} us, writeback p50={pct([int(x['arm_wb_us']) for x in fs],50)} us, "
                  f"correctos {sum(int(x['ok']) for x in fs)}/{len(fs)}")
    psp_ns = next((v for (l, var, m), v in ns.items() if l == "psp" and var == "con_cache" and m == "333"), None) \
        or next((v for k, v in ns.items() if k[0] == "psp" and k[1] == "con_cache"), None)
    arm_ns = next((v for k, v in ns.items() if k[0].startswith("arm") and k[1] == "invalidar+wb"), None)

    print("\n== caché del PSP ==")
    for x in leer(c, "cache.csv"): print(f"  {x['operacion']}: {x['ns_por_op']} ns")

    print("\n== asíncrono con el GE (p50) ==")
    asi = defaultdict(list)
    for x in leer(c, "asincrono.csv"):
        if x["ok"] == "1": asi[x["escenario"]].append(x)
    med = {}
    for e, fs in asi.items():
        med[e] = pct([int(x["total_us"]) for x in fs], 50)
        print(f"  {e:14s} total {med[e]} us, lista lista para el GE a los {pct([int(x['listo_para_ge_us']) for x in fs],50)} us, "
              f"trabajo terminado a los {pct([int(x['trabajo_fin_us']) for x in fs],50)} us")

    # ---- modelo de costo ----
    print("\n== modelo de costo por frame ==")
    print(f"  supuestos: {a.comandos} comandos, {a.llamadas} llamadas, GePatch {a.ns_psp:.0f} ns/comando en el PSP, objetivo {a.fps} fps")
    hoy = a.comandos * a.ns_psp / 1e6
    print(f"  hoy (recorrer en el PSP): {hoy:.1f} ms por frame de {1000 / a.fps:.1f} ms")
    if not (psp_ns and arm_ns):
        print("  faltan datos del recorrido en el PSP o en el ARM: no se puede estimar")
        return
    razon = arm_ns / psp_ns
    ns_arm_real = a.ns_psp * razon
    kb_llamada = a.comandos * 4 / 1024 / max(1, a.llamadas)
    for nombre, fuente in (("EBOOT", rp), ("juego", rj)):
        cand = {k: v for k, v in fuente.items() if "16ms" in k[0]}
        for k, (p50, p99, svc) in sorted(cand.items()):
            transp = k[0] + (f" sondeo={k[1]}" if k[0].startswith("mem") else "")
            por_llamada = p50 + inv_fijo + inv_kb * kb_llamada
            sinc = (a.llamadas * por_llamada + a.comandos * ns_arm_real / 1000) / 1000
            print(f"  [{nombre}] {transp}: ARM {razon:.2f}x del PSP ({ns_arm_real:.0f} ns/cmd), "
                  f"{por_llamada:.0f} us por llamada -> síncrono {sinc:.1f} ms ({100 * sinc / hoy:.0f}% de hoy)"
                  f"{'  << pasa la puerta (<50%)' if sinc < hoy / 2 else ''}")
    if "psp_sincrono" in med and "arm_asincrono" in med and "solo_trabajo" in med:
        libre = med["psp_sincrono"] - med["arm_asincrono"]
        print(f"  asíncrono (medido con la lista sintética): el frame termina {libre} us antes que hoy "
              f"({med['psp_sincrono']} -> {med['arm_asincrono']} us; solo el trabajo: {med['solo_trabajo']} us)")

if __name__ == "__main__":
    main()
