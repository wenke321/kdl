@echo off
setlocal
set "PYTHONHOME="
set "PYTHONPATH="
cd /d "%~dp0.."

set PORT=8099
set URL=http://127.0.0.1:%PORT%/big.bin
set OUT=downloads\big.out
set EXE=build\bin\Release\kdl.exe

echo [0] starting local range server ...
start "srv" /b python tools\range_server.py %PORT% 1> downloads\srv.log 2>&1
ping -n 4 127.0.0.1 >nul

del /q "%OUT%" "%OUT%.kdlpart" downloads\run1.log 2>nul

echo [1] background download for ~5s then hard kill ...
start "kdl" /b "%EXE%" -v -t 4 --min-segment 8388608 --no-progress -o "%OUT%" "%URL%" 1> downloads\run1.log 2>&1
ping -n 13 127.0.0.1 >nul
taskkill /IM kdl.exe /F >nul 2>&1
ping -n 3 127.0.0.1 >nul

echo === RUN1 LOG ===
type downloads\run1.log
echo === CHECKPOINT (.kdlpart) ===
type "%OUT%.kdlpart"
echo === PARTIAL FILE ===
dir /b "%OUT%"

echo.
echo [2] resuming (expect "resuming previous session") ...
"%EXE%" -t 4 --min-segment 8388608 --no-progress -o "%OUT%" "%URL%"

echo.
echo [3] verifying md5 (source vs downloaded) ...
certutil -hashfile downloads\big.bin MD5 | findstr /r /c:"^[0-9a-f]"
certutil -hashfile "%OUT%" MD5 | findstr /r /c:"^[0-9a-f]"

taskkill /IM python.exe /F >nul 2>&1
endlocal
