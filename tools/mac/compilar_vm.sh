#!/bin/sh
# Compila GePatch y gepresent en la VM del Mac (despues de preparar_vm.sh) y los deja donde se prueban: Descargas,
# vitabridge/enviar (el relevo los sube a la Vita) y el memstick del laboratorio. gepresent se compila en una copia
# ($HOME/tc/gp): build.sh borra su carpeta o/ y en la carpeta del repo no hay permiso de borrar.
# Uso: sh compilar_vm.sh [nombre de respaldo, por ejemplo dev64.1]
set -e
. $HOME/tc/env.sh
R=$HOME/mnt/GitHub/NativePatch D=$HOME/mnt/Downloads V=$HOME/mnt/Downloads/vitabridge
cd $R && make -s ADRENALINE=$ADRENALINE
rm -rf $HOME/tc/gp && cp -r tools/gepresent $HOME/tc/gp && (cd $HOME/tc/gp && sh build.sh >/dev/null)
mkdir -p $V/enviar/ux0/pspemu/seplugins $V/enviar/ux0/data/gepresent $V/respaldo $V/gepresent
cp ge_patch.prx $D/ge_patch.prx; cp ge_patch.prx $V/enviar/ux0/pspemu/seplugins/; cp ge_patch.prx $HOME/mnt/gepatch-lab/memstick/seplugins/
cp $HOME/tc/gp/gepresent.suprx $V/enviar/ux0/data/gepresent/
[ -n "$1" ] && { cp ge_patch.prx $V/respaldo/ge_patch_$1.prx; git diff > $V/gepatch_$1.patch; }
md5sum ge_patch.prx $HOME/tc/gp/gepresent.suprx
