@echo off
rem ---------------------------------------------------------------------------
rem Throughput harness: compares the adaptive default against fixed connection
rem counts, on a real download.
rem
rem Override the URL / mirrors on the command line:
rem     tools\bench.bat https://host/file.tar.xz https://mirror1/file https://mirror2/file
rem
rem Everything is written to %TEMP%; nothing in the repository is touched.
rem ---------------------------------------------------------------------------
setlocal
cd /d "%~dp0.."
set EXE=build\bin\Release\kdl.exe
set FILTER=findstr /i "average elapsed adaptive delivered sources error"

set URL=%1
if "%URL%"=="" set URL=https://mirrors.aliyun.com/linux-kernel/v7.x/linux-7.2.8.tar.xz
set MIRRORARGS=
if not "%2"=="" set MIRRORARGS=-m "%2"
if not "%3"=="" set MIRRORARGS=%MIRRORARGS% -m "%3"

echo ===== 1. default (adaptive concurrency) =====
del /f /q "%TEMP%\bench1.out" "%TEMP%\bench1.out.kdlpart" 2>nul
"%EXE%" --no-progress --overwrite --no-resume -o "%TEMP%\bench1.out" "%URL%" 2>&1 | %FILTER%

echo ===== 2. fixed -t 16 =====
del /f /q "%TEMP%\bench2.out" "%TEMP%\bench2.out.kdlpart" 2>nul
"%EXE%" --no-adaptive -t 16 --no-progress --overwrite --no-resume -o "%TEMP%\bench2.out" "%URL%" 2>&1 | %FILTER%

echo ===== 3. fixed -t 128 =====
del /f /q "%TEMP%\bench3.out" "%TEMP%\bench3.out.kdlpart" 2>nul
"%EXE%" --no-adaptive -t 128 --no-progress --overwrite --no-resume -o "%TEMP%\bench3.out" "%URL%" 2>&1 | %FILTER%

if "%MIRRORARGS%"=="" goto :done
echo ===== 4. mirrors, adaptive =====
del /f /q "%TEMP%\bench4.out" "%TEMP%\bench4.out.kdlpart" 2>nul
"%EXE%" %MIRRORARGS% --no-progress --overwrite --no-resume -o "%TEMP%\bench4.out" "%URL%" 2>&1 | %FILTER%

:done
endlocal
