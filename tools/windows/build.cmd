@echo off
rem Builds NativeLink and NativeDraw in WSL (tools/build.sh) and leaves them in Downloads and vitabridge\send (the relay
rem uploads them to the Vita if it is open). Double-click, or: build.cmd <test version> (also makes versioned copies).
set R=%~dp0..\..
wsl --cd "%R%" -e bash -c "V=$(wslpath '%USERPROFILE%\Downloads\vitabridge') sh tools/build.sh %1" || goto failed
echo done: Downloads and vitabridge\send
pause
exit /b 0
:failed
echo == build failed (see the error above; the first time: install.cmd)
pause
exit /b 1
