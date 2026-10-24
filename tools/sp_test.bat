@echo off
for /l %%i in (0,1,7) do call :one %%i
ping -n 26 127.0.0.1 >nul
exit /b 0
:one
set /a LO=%1*4194304
set /a HI=LO+4194303
start /b cmd /c curl -s -o nul -w "id=%1 speed=%%{speed_download} size=%%{size_download}^\n" --max-time 20 -A Mozilla/5.0 -r %%LO%%-%%HI%% https://cdn.kernel.org/pub/linux/kernel/v7.x/linux-7.2.9.tar.xz
exit /b 0
