@echo off
setlocal
cd /d "%~dp0.."

set URL=https://proof.ovh.net/files/100Mb.dat
set OUT=downloads\100Mb.dat
set EXE=build\bin\Release\kdl.exe

del /q "%OUT%" "%OUT%.kdlpart" downloads\run1.log downloads\100Mb.ref 2>nul

echo [1] background download for ~6s ...
start "kdl" /b "%EXE%" -t 4 --min-segment 1048576 --no-progress -o "%OUT%" "%URL%" 1> downloads\run1.log 2>&1
ping -n 7 127.0.0.1 >nul
taskkill /IM kdl.exe /F >nul 2>&1
ping -n 3 127.0.0.1 >nul

echo === RUN1 LOG ===
type downloads\run1.log
echo === PARTIAL OUTPUT ===
dir /b "%OUT%" "%OUT%.kdlpart"
echo === CHECKPOINT ===
type "%OUT%.kdlpart"

echo.
echo [2] resuming ...
"%EXE%" -t 4 --min-segment 1048576 --no-progress -o "%OUT%" "%URL%"

echo.
echo [3] verifying against curl ...
curl -s --max-time 600 -o downloads\100Mb.ref "%URL%"
certutil -hashfile "%OUT%" MD5 | findstr /r /c:"^[0-9a-f]"
certutil -hashfile downloads\100Mb.ref MD5 | findstr /r /c:"^[0-9a-f]"
endlocal
