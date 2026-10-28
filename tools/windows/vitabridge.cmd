@echo off
rem Relevo con la Vita (como vitabridge.py en la Terminal del Mac). Dejar esta ventana abierta mientras se prueba.
rem Sin argumentos busca la Vita en la red; o con la IP: vitabridge.cmd 192.168.0.10
cd /d "%~dp0"
set PY=python
where py >nul 2>&1 && set PY=py -3
(echo %date% %time% & where py python & %PY% --version) > vitabridge\inicio.txt 2>&1
%PY% -u vitabridge.py %*
pause
