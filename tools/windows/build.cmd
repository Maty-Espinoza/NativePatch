@echo off
rem Compila NativeLink y NativeDraw en WSL (tools/build.sh) y los deja en Descargas y en vitabridge\enviar (el relevo
rem los sube a la Vita si esta abierto). Doble clic, o: build.cmd <version de prueba> (ademas, copias con version).
set R=%~dp0..\..
wsl --cd "%R%" -e bash -c "V=$(wslpath '%USERPROFILE%\Downloads\vitabridge') sh tools/build.sh %1" || goto fallo
echo listo: Descargas y vitabridge\enviar
pause
exit /b 0
:fallo
echo == fallo la compilacion (ver el error arriba; la primera vez: install.cmd)
pause
exit /b 1
