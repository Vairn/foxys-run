@echo off
setlocal EnableExtensions
rem Bootable floppy: build\FoxysRun.adf (or build-debug, build-file, ...).
rem   build-adf.bat
rem   build-adf.bat file
py -3 "%~dp0tools\make_adf.py" %*
if errorlevel 1 exit /b 1
endlocal
