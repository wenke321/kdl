@echo off
setlocal
set "PYTHONHOME="
set "PYTHONPATH="
cd /d "%~dp0.."

start "srv" /b python tools\range_server.py 8097 1> downloads\srv3.log 2>&1
ping -n 4 127.0.0.1 >nul

del /q downloads\p.out downloads\p.out.kdlpart 2>nul
build\bin\Release\kdl.exe -t 8 -o downloads\p.out http://127.0.0.1:8097/big.bin > downloads\prog.txt 2>&1

echo === RAW PROGRESS OUTPUT (CR separated) ===
type downloads\prog.txt
echo.
echo === FRAMES CONTAINING 100 ===
powershell -NoProfile -Command "(Get-Content -Raw downloads\prog.txt) -split [char]13 | Where-Object { $_ -match '100' }"

taskkill /IM python.exe /F >nul 2>&1
endlocal
