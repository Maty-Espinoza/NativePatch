@echo off
rem Compila NativeLink y NativeDraw en WSL (tools/compilar.sh) y los deja en Descargas y en vitabridge\enviar (el relevo
rem los sube a la Vita si esta abierto). Doble clic, o: compilar.cmd <version de prueba> (ademas, copias con version).
set R=%~dp0..\..
wsl --cd "%R%" -e bash -c "V=$(wslpath '%USERPROFILE%\Downloads\vitabridge') sh tools/compilar.sh %1" || goto fallo
echo listo: Descargas y vitabridge\enviar
pause
exit /b 0
:fallo
echo == fallo la compilacion (ver el error arriba; la primera vez: instalar.cmd)
pause
exit /b 1
