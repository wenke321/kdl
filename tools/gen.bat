@echo off
rem Qt sets PYTHONHOME which breaks the system Python; clear it for our tools.
set "PYTHONHOME="
set "PYTHONPATH="
cd /d "%~dp0.."
python tools\gen.py
echo exit=%errorlevel%
