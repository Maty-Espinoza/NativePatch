#!/bin/sh
# Compila GePatch y gepresent en el contenedor (despues de preparar.sh) y deja los binarios en /mnt/user-data/outputs,
# desde donde device_commit_files los copia al PC (Descargas y vitabridge/enviar: ver docs/FLUJO_DE_TRABAJO.md).
# gepresent se compila en una copia ($T/gp): build.sh borra su carpeta o/.
# Uso (desde la raiz del repo): sh tools/contenedor/compilar.sh [nombre, p. ej. dev65.1 o v13.7.2]
set -e
T=${T:-/home/claude/tc}; . $T/env.sh
O=/mnt/user-data/outputs; mkdir -p $O
make -s ADRENALINE=$ADRENALINE
rm -rf $T/gp && cp -r tools/gepresent $T/gp && (cd $T/gp && sh build.sh >/dev/null)
cp ge_patch.prx $O/ge_patch.prx; cp $T/gp/gepresent.suprx $O/gepresent.suprx
[ -n "$1" ] && { cp ge_patch.prx $O/ge_patch_$1.prx; cp $T/gp/gepresent.suprx $O/gepresent_$1.suprx; git diff > $O/cambios_$1.patch; }
md5sum ge_patch.prx $T/gp/gepresent.suprx
