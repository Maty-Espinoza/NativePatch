@echo off
rem Compila NativePatch en WSL y copia ge_patch.prx a Descargas, a la Vita (vitabridge\enviar) y al laboratorio.
rem Va en Documents\GitHub\gepatch-lab (lo copia instalar.cmd); el repo esta al lado, en ..\NativePatch
set R=%~dp0..\NativePatch
wsl --cd "%R%" -e bash -c "export PATH=$PATH:$HOME/pspdev/bin; make -s" || goto fallo
copy /y "%R%\ge_patch.prx" "%USERPROFILE%\Downloads\" >nul
set V=%USERPROFILE%\Downloads\vitabridge\enviar\ux0\pspemu\seplugins
if not exist "%V%" mkdir "%V%"
copy /y "%R%\ge_patch.prx" "%V%\" >nul
copy /y "%R%\ge_patch.prx" "%~dp0memstick\seplugins\" >nul
certutil -hashfile "%R%\ge_patch.prx" MD5 | findstr /v ":"
echo listo: Descargas, vitabridge\enviar (sube solo si el relevo esta abierto) y laboratorio
pause
exit /b 0
:fallo
echo == fallo la compilacion (ver el error arriba)
pause
exit /b 1
