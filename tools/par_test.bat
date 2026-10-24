@echo off
start /b "" "D:\kevin\dev\test\build\bin\Release\kdl.exe" -f -t 32 --no-progress -o D:\kevin\dev\test\build\tmp\j.tar.xz https://cdn.kernel.org/pub/linux/kernel/v7.x/linux-7.2.9.tar.xz
start /b "" "D:\kevin\dev\test\build\bin\Release\kdl.exe" -f -t 32 --no-progress -o D:\kevin\dev\test\build\tmp\k.tar.xz https://mirrors.aliyun.com/linux-kernel/v7.x/linux-7.2.9.tar.xz
ping -n 120 127.0.0.1 >nul
