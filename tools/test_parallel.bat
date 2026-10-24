@echo off
setlocal
cd /d "%~dp0.."
set URL=https://mirrors.aliyun.com/linux-kernel/v7.x/linux-7.2.9.tar.xz
set EXE=build\bin\Release\kdl.exe

del /q "%TEMP%\pa.tar.xz*" "%TEMP%\pb.tar.xz*" "%TEMP%\pa.log" "%TEMP%\pb.log" 2>nul

echo === TWO PROCESSES x 16 threads, run simultaneously ===
start "a" /b "%EXE%" -t 16 --no-progress -o "%TEMP%\pa.tar.xz" "%URL%" > "%TEMP%\pa.log" 2>&1
start "b" /b "%EXE%" -t 16 --no-progress -o "%TEMP%\pb.tar.xz" "%URL%" > "%TEMP%\pb.log" 2>&1

:wait
tasklist /fi "imagename eq kdl.exe" 2>nul | find /i "kdl.exe" >nul && goto wait

echo --- process A ---
findstr /v /c:"INFO" "%TEMP%\pa.log"
echo --- process B ---
findstr /v /c:"INFO" "%TEMP%\pb.log"

del /q "%TEMP%\pa.tar.xz*" "%TEMP%\pb.tar.xz*" 2>nul
endlocal
