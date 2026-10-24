@echo off
setlocal
cd /d "%~dp0.."
set U1=https://mirrors.aliyun.com/linux-kernel/v7.x/linux-7.2.9.tar.xz
set U2=https://mirrors.tuna.tsinghua.edu.cn/kernel/v7.x/linux-7.2.9.tar.xz
set EXE=build\bin\Release\kdl.exe

del /q "%TEMP%\m1.tar.xz*" "%TEMP%\m2.tar.xz*" "%TEMP%\m1.log" "%TEMP%\m2.log" 2>nul

echo === A) ONE mirror (aliyun), -t 32 ===
"%EXE%" -t 32 --no-progress -o "%TEMP%\m1.tar.xz" "%U1%" 2>&1 | findstr /v INFO
del /q "%TEMP%\m1.tar.xz*" 2>nul

echo.
echo === B) TWO mirrors (aliyun + tuna), 16 threads each, simultaneous ===
start "a" /b "%EXE%" -t 16 --no-progress -o "%TEMP%\m1.tar.xz" "%U1%" > "%TEMP%\m1.log" 2>&1
start "b" /b "%EXE%" -t 16 --no-progress -o "%TEMP%\m2.tar.xz" "%U2%" > "%TEMP%\m2.log" 2>&1

:wait
tasklist /fi "imagename eq kdl.exe" 2>nul | find /i "kdl.exe" >nul && goto wait

echo --- aliyun half ---
findstr /v INFO "%TEMP%\m1.log"
echo --- tuna half ---
findstr /v INFO "%TEMP%\m2.log"

del /q "%TEMP%\m1.tar.xz*" "%TEMP%\m2.tar.xz*" 2>nul
endlocal
