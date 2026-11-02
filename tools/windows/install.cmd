@echo off
rem Installs the NativePatch environment on this PC: double-click. It does not change Windows environment variables.
rem Leaves the repo, the PSP and Vita compilers (in Ubuntu/WSL) and the relay with the Vita.
rem Works on its own (downloaded from GitHub): if it is not inside the repo, it fetches install.sh from the dev branch.
wsl -e true >nul 2>&1 || (echo WSL with Ubuntu is missing: in PowerShell as administrator run "wsl --install", reboot and run this again. & pause & exit /b 1)
where py >nul 2>&1 || echo Warning: Python for Windows is missing ^(python.org^): the relay with the Vita needs it.
if exist "%~dp0install.sh" (
  wsl --cd "%~dp0." -e bash ./install.sh
) else (
  wsl -e bash -c "curl -fsSL https://raw.githubusercontent.com/Maty-Espinoza/NativePatch/dev/tools/windows/install.sh | bash"
)
pause
