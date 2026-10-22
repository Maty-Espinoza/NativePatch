#!/usr/bin/env python3
"""Arma la bateria GEPTEST para la consola a partir de pspautotests (tests/gpu, .prx ya compilados).

  armar.py PSPAUTOTESTS SALIDA

SALIDA queda con la estructura de ux0:/pspemu (ms0:):
  PSP/GAME/GEPTEST/EBOOT.PBP     lanzador (tools/geptest/lanzador)
  PSP/GAME/GEPTEST/t/NNN/EBOOT.PBP  una prueba por EBOOT (en subcarpetas: el XMB no las lista)
  geptest/lista.txt              "ms0:/PSP/GAME/GEPTEST/t/NNN/EBOOT.PBP gpu/ge/edram" por linea
  seplugins/geptest.prx          plugin (agregar 'ms0:/seplugins/geptest.prx 1' a game.txt)
Las pruebas que pueden colgar el GE o Adrenaline van al final.
"""
import os, shutil, subprocess, sys

RIESGO = ('gpu/ge/', 'gpu/signals/', 'gpu/callbacks/', 'gpu/timing/')  # al final

def main(src, out):
    here = os.path.dirname(os.path.abspath(__file__))
    tests = []
    for root, _, files in os.walk(os.path.join(src, 'tests', 'gpu')):
        if '/kernel' in root:
            continue  # modulos de kernel: necesitan psplink
        for f in files:
            if f.endswith('.prx') and os.path.exists(os.path.join(root, f[:-4] + '.expected')):
                tests.append(os.path.relpath(os.path.join(root, f[:-4]), os.path.join(src, 'tests')))
    tests.sort(key=lambda t: (t.startswith(RIESGO), t))
    game = os.path.join(out, 'PSP', 'GAME', 'GEPTEST')
    os.makedirs(os.path.join(game, 't'), exist_ok=True)
    os.makedirs(os.path.join(out, 'geptest'), exist_ok=True)
    os.makedirs(os.path.join(out, 'seplugins'), exist_ok=True)
    sfo = os.path.join(out, 'PARAM.SFO')
    lista = []
    for i, t in enumerate(tests):
        subprocess.run(['mksfo', 'GEPTEST ' + t, sfo], check=True, stdout=subprocess.DEVNULL)
        os.makedirs(os.path.join(game, 't', '%03d' % i), exist_ok=True)
        pbp = os.path.join(game, 't', '%03d' % i, 'EBOOT.PBP')
        subprocess.run(['pack-pbp', pbp, sfo, 'NULL', 'NULL', 'NULL', 'NULL', 'NULL',
                        os.path.join(src, 'tests', t + '.prx'), 'NULL'], check=True, stdout=subprocess.DEVNULL)
        lista.append('ms0:/PSP/GAME/GEPTEST/t/%03d/EBOOT.PBP %s' % (i, t))
    os.remove(sfo)
    open(os.path.join(out, 'geptest', 'lista.txt'), 'w').write('\n'.join(lista) + '\n')
    shutil.copy(os.path.join(here, 'lanzador', 'EBOOT.PBP'), os.path.join(game, 'EBOOT.PBP'))
    shutil.copy(os.path.join(here, 'geptest.prx'), os.path.join(out, 'seplugins', 'geptest.prx'))
    print('%d pruebas en %s' % (len(tests), out))

if __name__ == '__main__':
    sys.exit(main(sys.argv[1], sys.argv[2]))
