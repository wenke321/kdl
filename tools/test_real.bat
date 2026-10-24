@echo off
setlocal
cd /d "%~dp0.."

set URL=https://mirrors.aliyun.com/linux-kernel/v7.x/linux-7.2.9.tar.xz
set OUT=%TEMP%\ali_default.tar.xz
del /q "%OUT%" "%OUT%.kdlpart" "%TEMP%\ali.txt" 2>nul

echo === default settings (no -t) ===
build\bin\Release\kdl.exe -o "%OUT%" "%URL%" > "%TEMP%\ali.txt" 2>&1
findstr /v /c:"MiB/s" "%TEMP%\ali.txt"
echo --- last progress frame ---
powershell -NoProfile -Command "(Get-Content -Raw '%TEMP%\ali.txt') -split [char]13 | Where-Object { $_ -match 'MiB/s' } | Select-Object -Last 1"

del /q "%OUT%" "%OUT%.kdlpart" 2>nul
endlocal
