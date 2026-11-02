@echo off
rem Relay with the Vita (like vitabridge.py in the Mac Terminal). Leave this window open while testing.
rem Without arguments it finds the Vita on the network; or with the IP: vitabridge.cmd 192.168.0.10
cd /d "%~dp0"
set PY=python
where py >nul 2>&1 && set PY=py -3
rem start.txt: time and which Python was used (to check from Claude if the relay does not start)
(echo %date% %time% & where py python & %PY% --version) > vitabridge\start.txt 2>&1
%PY% -u vitabridge.py %*
pause
