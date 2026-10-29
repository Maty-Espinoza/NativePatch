@echo off
rem Instala el ambiente de NativePatch en este PC: doble clic. No cambia las variables de entorno de Windows.
rem Deja el repo, los compiladores del PSP y de la Vita (en Ubuntu/WSL) y el relevo con la Vita.
rem Sirve suelto (bajado de GitHub): si no esta dentro del repo, baja instalar.sh de la rama dev.
wsl -e true >nul 2>&1 || (echo Falta WSL con Ubuntu: en PowerShell como administrador "wsl --install", reiniciar y volver a correr esto. & pause & exit /b 1)
where py >nul 2>&1 || echo Aviso: falta Python para Windows ^(python.org^): el relevo con la Vita lo necesita.
if exist "%~dp0instalar.sh" (
  wsl --cd "%~dp0." -e bash ./instalar.sh
) else (
  wsl -e bash -c "curl -fsSL https://raw.githubusercontent.com/Maty-Espinoza/NativePatch/dev/tools/windows/instalar.sh | bash"
)
pause
