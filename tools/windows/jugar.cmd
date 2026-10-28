@echo off
rem Abre un juego en el laboratorio (PPSSPP como Adrenaline, corre en WSL). Sin nombre: lista los ISO de iso\
rem   jugar.cmd rr.iso        con PPSSPP_ADRENALINE=estricto se detiene en lo que en la Vita cuelga Adrenaline
wsl --cd "%~dp0." -e bash ./jugar_wsl.sh %*
if errorlevel 1 pause
