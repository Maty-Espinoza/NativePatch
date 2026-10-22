#!/usr/bin/env python3
"""Arma la bateria GEPTEST (un solo proceso) a partir de pspautotests recompiladas.

  armar.py PSPAUTOTESTS SALIDA

PSPAUTOTESTS: copia de pspautotests con common/ compilado con -DGEPTEST_INPROC y las
pruebas de tests/gpu recompiladas (ver tools/geptest/README.md). SALIDA queda con la
estructura de ux0:/pspemu (ms0:):
  PSP/GAME/GEPTEST/EBOOT.PBP     el corredor (tools/geptest/corredor)
  PSP/GAME/GEPTEST/m/<id>.prx    una prueba por modulo
  geptest/lista.txt              "ms0:/PSP/GAME/GEPTEST/m/<id>.prx gpu/ge/edram" por linea
Las pruebas que pueden colgar el GE o Adrenaline van al final.
"""
import os, shutil, sys

RIESGO = ('gpu/ge/', 'gpu/signals/', 'gpu/callbacks/', 'gpu/timing/')  # al final
# Al final de todo: escribe mas alla de la VRAM (0x04800000); en la Vita eso es memoria de
# Adrenaline y lo cerro (c1-2858-3) en un solo proceso
ULTIMAS = ('gpu/transfer/invalid',)

def main(src, out):
    here = os.path.dirname(os.path.abspath(__file__))
    tests = []
    for root, _, files in os.walk(os.path.join(src, 'tests', 'gpu')):
        if '/kernel' in root:
            continue  # modulos de kernel: necesitan psplink
        for f in files:
            if f.endswith('.prx') and os.path.exists(os.path.join(root, f[:-4] + '.expected')):
                tests.append(os.path.relpath(os.path.join(root, f[:-4]), os.path.join(src, 'tests')))
    tests.sort(key=lambda t: (t in ULTIMAS, t.startswith(RIESGO), t))
    game = os.path.join(out, 'PSP', 'GAME', 'GEPTEST')
    os.makedirs(os.path.join(game, 'm'), exist_ok=True)
    os.makedirs(os.path.join(out, 'geptest'), exist_ok=True)
    lista = []
    for t in tests:
        tid = t.replace('/', '_')
        shutil.copy(os.path.join(src, 'tests', t + '.prx'), os.path.join(game, 'm', tid + '.prx'))
        lista.append('ms0:/PSP/GAME/GEPTEST/m/%s.prx %s' % (tid, t))
    open(os.path.join(out, 'geptest', 'lista.txt'), 'w').write('\n'.join(lista) + '\n')
    shutil.copy(os.path.join(here, 'corredor', 'EBOOT.PBP'), os.path.join(game, 'EBOOT.PBP'))
    print('%d pruebas en %s' % (len(tests), out))

if __name__ == '__main__':
    sys.exit(main(sys.argv[1], sys.argv[2]))
