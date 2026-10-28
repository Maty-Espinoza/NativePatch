@echo off
rem Instala el ambiente de NativePatch en este PC: doble clic. No cambia las variables de entorno de Windows.
rem   instalar.cmd              repo, compilador del PSP, relevo con la Vita y scripts
rem   instalar.cmd laboratorio  lo mismo + PPSSPP-Adrenaline (laboratorio y headless, 20-40 min mas)
rem Sirve suelto (bajado de GitHub): si no esta dentro del repo, baja instalar.sh de la rama dev.
wsl -e true >nul 2>&1 || (echo Falta WSL con Ubuntu: en PowerShell como administrador "wsl --install", reiniciar y volver a correr esto. & pause & exit /b 1)
where py >nul 2>&1 || echo Aviso: falta Python para Windows ^(python.org^): el relevo con la Vita lo necesita.
if exist "%~dp0instalar.sh" (
  wsl --cd "%~dp0." -e bash ./instalar.sh %1
) else (
  wsl -e bash -c "curl -fsSL https://raw.githubusercontent.com/Maty-Espinoza/NativePatch/dev/tools/windows/instalar.sh | bash -s -- %1"
)
pause
