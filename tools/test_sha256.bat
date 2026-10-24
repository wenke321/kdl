@echo off
setlocal
set "PYTHONHOME="
set "PYTHONPATH="
cd /d "%~dp0.."

set PORT=8098
set URL=http://127.0.0.1:%PORT%/big.bin
set OUT=downloads\sha.out
set EXE=build\bin\Release\kdl.exe

if not exist downloads\big.bin call tools\gen.bat

start "srv2" /b python tools\range_server.py %PORT% 1> downloads\srv2.log 2>&1
ping -n 4 127.0.0.1 >nul

certutil -hashfile downloads\big.bin SHA256 > downloads\hash_raw.txt
findstr /r "^[0-9a-f]" downloads\hash_raw.txt > downloads\hash.txt
set /p GOOD=<downloads\hash.txt
echo source sha256 = %GOOD%
echo.

echo === CASE 1: correct digest (expect verified + exit 0) ===
del /q "%OUT%" "%OUT%.kdlpart" >nul 2>&1
"%EXE%" -t 8 --no-progress --sha256 %GOOD% -o "%OUT%" "%URL%"
echo exit=%errorlevel%
echo.

echo === CASE 2: wrong digest (expect Verify failure + exit 1) ===
del /q "%OUT%" "%OUT%.kdlpart" >nul 2>&1
"%EXE%" -t 8 --no-progress --sha256 0000000000000000000000000000000000000000000000000000000000000000 -o "%OUT%" "%URL%"
echo exit=%errorlevel%
echo.

echo === CASE 3: progress bar reaches 100%% ===
del /q "%OUT%" "%OUT%.kdlpart" >nul 2>&1
"%EXE%" -t 8 --sha256 %GOOD% -o "%OUT%" "%URL%"
echo exit=%errorlevel%

taskkill /IM python.exe /F >nul 2>&1
endlocal
