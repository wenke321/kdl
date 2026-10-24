@echo off
setlocal
cd /d "%~dp0.."

set URL=https://mirrors.aliyun.com/linux-kernel/v7.x/linux-7.2.8.tar.xz
set EXE=build\bin\Release\kdl.exe

for %%t in (16 32 64 128) do (
    echo === threads=%%t ===
    "%EXE%" -t %%t --no-progress -o "%TEMP%\v728_t%%t.tar.xz" "%URL%" 2>&1 | findstr /i "elapsed average segments error"
)

echo === threads=32, big chunks (min-segment 8MiB) ===
"%EXE%" -t 32 --min-segment 8388608 --no-progress -o "%TEMP%\v728_big.tar.xz" "%URL%" 2>&1 | findstr /i "elapsed average segments error"

echo === threads=64, big chunks (min-segment 8MiB) ===
"%EXE%" -t 64 --min-segment 8388608 --no-progress -o "%TEMP%\v728_big64.tar.xz" "%URL%" 2>&1 | findstr /i "elapsed average segments error"

endlocal
