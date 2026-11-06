#!/usr/bin/env python3
"""Timing model of how the capturer reads PSP memory: game fps and HD frames per second for today's gate, the full
sync (hold_game + gate_sync) and the photo at the sync (docs/dev/en/performance-plan.md, "Reading PSP memory").

Per game it takes what the Vita logs measured (capture ms per frame, reads, transfers, vertices) and the unit costs
measured on the hardware; the parts not measured yet are marked "est." and are the ones phase 0 of the plan measures.
It is a budget, not a simulation: it says which design fits and what must hold for it to fit. tools/perf/sync_lab.c
checks the correctness side.
Usage: python3 tools/perf/sync_model.py [--dma-mbs N] [--dma-call-us N] [--ge-overlap-ms N] [--game NAME]
"""
import argparse, math

# Unit costs. m = measured (where), a = assumed until measured on the Vita
UNIT = {
    'line_own_us': 2.8,      # m: one 256-byte NEON read of uncached PSP RAM (performance-plan.md, B.1)
    'line_hit_us': 0.3,      # m: a 64-byte line from the readers' shared cache (capture_bench.c, C_HIT)
    'word_unc_us': 0.199,    # m: one uncached word read (performance-plan.md, translator split)
    'word_wr_us': 0.03,      # a: one uncached word write (KBENCH: 1 MB written in 4.4 ms = 0.017)
    'cpu_mbs': 91,           # m: CPU copy from uncached PSP RAM, MB/s (memcpy and neon256)
    'cached_mbs': 550,       # m: CPU copy cached to cached, MB/s ('memory bench' line of nativedraw_capture.log: 376-720)
    'ge_ram_ms_mb': 4.8,     # m: PSP GE copy RAM to RAM, ms per MB (VPROBE)
    'latency_ms': 0.4,       # m: the Vita sees a PSP request, polling at ~100 us (KBENCH: p50 0.2-0.5 ms)
}

# Games (per frame). tg: game frame without plugin; ge: time the game waits for its own GE at the sync (the photo
# overlaps it); cap: capture today; own/hits/cmp: reads; xfer_kb: RAM to VRAM transfers the capturer applies by
# word; vcopy_ms: vertex copy from uncached RAM; foot_kb/runs: RAM the frame needs and how many pieces (est.)
GAMES = {
    'gow_menu': dict(src='tests/perf/runs/0.0.24.8_gow_menu_play, hold_game', tg=33.3, ge=19.0, cap=68.0, own=236, hits=7590,
                     cmp=0.85, xfer_kb=580, vcopy_ms=0.0, foot_kb=1450, runs=70, est='ge, foot_kb, runs'),
    'gow_play': dict(src='tests/perf/runs/0.0.24.8_gow_menu_play, hold_game', tg=33.3, ge=6.0, cap=120.0, own=440, hits=6700,
                     cmp=1.7, xfer_kb=600, vcopy_ms=1.2, foot_kb=1700, runs=90, est='ge, xfer_kb, foot_kb, runs'),
    'rr_crowd': dict(src='performance-plan.md, crowd measured clean (0.0.21.3)', tg=16.7, ge=5.0, cap=32.0, own=525, hits=1415,
                     cmp=0.5, xfer_kb=0, vcopy_ms=7.3, foot_kb=560, runs=25, est='ge, foot_kb, runs'),
    'rr_race': dict(src='performance-plan.md, where the time goes (0.0.16.4)', tg=16.7, ge=5.0, cap=7.0, own=150, hits=500,
                    cmp=0.3, xfer_kb=0, vcopy_ms=2.0, foot_kb=300, runs=15, est='ge, own, hits, vcopy_ms, foot_kb, runs'),
}

def copy_ms(kb, runs, mbs, call_us):
    """Photo of kb in runs pieces: each piece by DMA or, if cheaper (small pieces), by the CPU from uncached RAM"""
    piece = kb / runs / 1024
    dma = call_us / 1000 + piece / mbs * 1000
    cpu = piece / UNIT['cpu_mbs'] * 1000
    return runs * min(dma, cpu)

def model(g, a):
    u = UNIT
    reads = (g['own'] * u['line_own_us'] + g['hits'] * u['line_hit_us']) / 1000 + g['cmp']
    xfer = g['xfer_kb'] * 256 * (u['word_unc_us'] + u['word_wr_us']) / 1000
    mem = reads + xfer + g['vcopy_ms']  # what reading PSP memory costs the capturer today
    cached = (g['foot_kb'] + g['xfer_kb']) / 1024 / u['cached_mbs'] * 1000 * 2  # the same reads over the photo (twice: read and copy)
    cap2 = max(g['cap'] - mem + cached, 1)
    ge = g['ge'] if a.ge_overlap_ms is None else a.ge_overlap_ms
    rows = []
    # today: the gate before the next enqueue waits at most 5 ms; behind that the capturer reads late
    wait = min(5, max(0, g['cap'] - g['tg']))
    late = g['cap'] > g['tg'] + 5
    rows.append(('stream (today)', g['tg'] + wait, 0 if late and g['xfer_kb'] else 1000 / max(g['cap'], g['tg'] + wait),
                 'lists read late' if late else 'ok'))
    hold = max(g['tg'], g['cap'] + u['latency_ms'])
    rows.append(('hold (gate_sync + hold_game)', hold, 1000 / hold, 'right, the game at the capturer speed'))
    for name, cost, cap in (('photo, Vita DMA', u['latency_ms'] + copy_ms(g['foot_kb'], g['runs'], a.dma_mbs, a.dma_call_us), cap2),
                            ('photo, PSP GE copy', g['foot_kb'] / 1024 * u['ge_ram_ms_mb'], cap2 + copy_ms(g['foot_kb'], g['runs'], a.dma_mbs, a.dma_call_us))):
        extra = max(0, cost - ge) if 'DMA' in name else cost  # the Vita copies while the game waits for its GE; the GE copy goes after
        fr = g['tg'] + extra
        hd = min(1000 / fr, 1000 / cap)
        rows.append((name, fr, hd, f'copy {cost:.1f} ms, game pays {extra:.1f}; capture {cap:.0f} ms'))
    return mem, cap2, rows

def main():
    p = argparse.ArgumentParser()
    p.add_argument('--dma-mbs', type=float, default=512, help='Vita DMA from PSP RAM, MB/s (512 textures in stage 3; 139 KBENCH 1 MB)')
    p.add_argument('--dma-call-us', type=float, default=100, help='cost of one DMA call, us (findings.md)')
    p.add_argument('--ge-overlap-ms', type=float, default=None, help='replace each game\'s GE wait at the sync')
    p.add_argument('--game', default=None)
    a = p.parse_args()
    print(f'DMA {a.dma_mbs:.0f} MB/s, {a.dma_call_us:.0f} us per call; fps of the game, HD frames per second the capturer allows '
          '(the translator and the GPU limit apart)')
    for k, g in GAMES.items():
        if a.game and a.game != k: continue
        mem, cap2, rows = model(g, a)
        print(f'\n{k} (game {1000 / g["tg"]:.0f} fps, capture today {g["cap"]:.0f} ms, of it reading memory {mem:.1f} ms; '
              f'source: {g["src"]}; est.: {g["est"]})')
        for name, fr, hd, note in rows:
            print(f'  {name:30} game {1000 / fr:5.1f} fps  HD {hd:5.1f}/s  {note}')

if __name__ == '__main__':
    main()
