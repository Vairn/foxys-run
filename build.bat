@echo off
setlocal EnableExtensions
set "ROOT=%~dp0"
set "ROOT=%ROOT:~0,-1%"

rem ------------------------------------------------------------------
rem Build modes:
rem   build.bat          -> Release (-O2),  ACE_DEBUG=OFF, output in build\
rem   build.bat debug    -> RelWithDebInfo, ACE_DEBUG=OFF, output in build-debug\
rem   build.bat serial   -> RelWithDebInfo, ACE_DEBUG=ON + ACE_DEBUG_SERIAL=ON,
rem                         output in build-serial\
rem   build.bat ace      -> Release (-O2),  ACE_DEBUG=ON + ACE_DEBUG_UAE=ON,
rem                         output in build-ace\
rem   build.bat lite     -> Release (-O2),  ACE_DEBUG=OFF, game logWrite() to
rem                         serial only, output in build-serial-lite\
rem
rem Graphics, levels, music, and sound effects are already packed in
rem dist\data.pak (levels, graphics, music, sound effects).
rem
rem Toolchain:
rem   BART                   Bartman opt directory (contains bin\m68k-amiga-elf-gcc.exe)
rem   AMIGA_CMAKE_TOOLCHAIN  path to m68k-bartman.cmake from AmigaCMakeCrossToolchains
rem
rem IMPORTANT: never pass -DCMAKE_C_FLAGS here. The Bartman toolchain file
rem injects the essential m68k flags (incl. -nostdlib) via CMAKE_C_FLAGS_INIT,
rem which only applies when the cache var is UNSET.
rem ------------------------------------------------------------------
set "MODE=release"
if /I "%~1"=="debug" set "MODE=debug"
if /I "%~1"=="serial" set "MODE=serial"
if /I "%~1"=="ace" set "MODE=ace"
if /I "%~1"=="lite" set "MODE=lite"

if "%MODE%"=="debug" (
  set "BUILD_DIR=%ROOT%\build-debug"
  set "BUILD_TYPE=RelWithDebInfo"
  set "TRACE_OPT=-DSUNNY_TRACE=ON"
  set "ACE_DBG=OFF"
  set "ACE_UAE=OFF"
  set "ACE_SER=OFF"
  set "SUNNY_SER=OFF"
) else if "%MODE%"=="serial" (
  set "BUILD_DIR=%ROOT%\build-serial"
  set "BUILD_TYPE=RelWithDebInfo"
  set "TRACE_OPT=-DSUNNY_TRACE=ON"
  set "ACE_DBG=ON"
  set "ACE_UAE=OFF"
  set "ACE_SER=ON"
  set "SUNNY_SER=OFF"
) else if "%MODE%"=="ace" (
  set "BUILD_DIR=%ROOT%\build-ace"
  set "BUILD_TYPE=Release"
  set "TRACE_OPT=-DSUNNY_TRACE=OFF"
  set "ACE_DBG=ON"
  set "ACE_UAE=ON"
  set "ACE_SER=OFF"
  set "SUNNY_SER=OFF"
) else if "%MODE%"=="lite" (
  set "BUILD_DIR=%ROOT%\build-serial-lite"
  set "BUILD_TYPE=Release"
  set "TRACE_OPT=-DSUNNY_TRACE=OFF"
  set "ACE_DBG=OFF"
  set "ACE_UAE=OFF"
  set "ACE_SER=OFF"
  set "SUNNY_SER=ON"
) else (
  set "BUILD_DIR=%ROOT%\build"
  set "BUILD_TYPE=Release"
  set "TRACE_OPT=-DSUNNY_TRACE=OFF"
  set "ACE_DBG=OFF"
  set "ACE_UAE=OFF"
  set "ACE_SER=OFF"
  set "SUNNY_SER=OFF"
)

if not defined BART call :find_bart
if not defined BART (
  echo Bartman toolchain not found.
  echo Install the amiga-debug extension, or set BART to its opt directory.
  exit /b 1
)
if not exist "%BART%\bin\m68k-amiga-elf-gcc.exe" (
  echo Bartman gcc not found at:
  echo   %BART%\bin\m68k-amiga-elf-gcc.exe
  exit /b 1
)

if not defined AMIGA_CMAKE_TOOLCHAIN (
  echo Set AMIGA_CMAKE_TOOLCHAIN to m68k-bartman.cmake from
  echo https://github.com/AmigaPorts/AmigaCMakeCrossToolchains
  exit /b 1
)
if not exist "%AMIGA_CMAKE_TOOLCHAIN%" (
  echo Toolchain file not found:
  echo   %AMIGA_CMAKE_TOOLCHAIN%
  exit /b 1
)

for %%I in ("%BART%") do set "BART_SHORT=%%~sI"
for %%I in ("%AMIGA_CMAKE_TOOLCHAIN%") do set "TOOLCHAIN_CMAKE=%%~sI"

set "PATH=%BART%\bin;C:\Program Files\CMake\bin;%PATH%"

if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"
cd /d "%BUILD_DIR%"

echo Build mode: %MODE%  (type=%BUILD_TYPE%, dir=%BUILD_DIR%, uae=%ACE_UAE%, serial=%ACE_SER%, serial_log=%SUNNY_SER%)

cmake "%ROOT%" -G Ninja ^
  -DCMAKE_TOOLCHAIN_FILE="%TOOLCHAIN_CMAKE:\=/%" ^
  "-DTOOLCHAIN_PATH=%BART_SHORT:\=/%" ^
  -DTOOLCHAIN_PREFIX=m68k-amiga-elf ^
  -DM68K_CPU=68000 ^
  -DCMAKE_BUILD_TYPE=%BUILD_TYPE% ^
  %TRACE_OPT% ^
  -DACE_DEBUG=%ACE_DBG% ^
  -DACE_DEBUG_UAE=%ACE_UAE% ^
  -DACE_DEBUG_SERIAL=%ACE_SER% ^
  -DSUNNY_SERIAL_LOG=%SUNNY_SER% ^
  -DACE_USE_ECS_FEATURES=OFF ^
  -DACE_USE_AGA_FEATURES=OFF ^
  -DACE_SCROLLBUFFER_ENABLE_SCROLL_Y=ON ^
  -DACE_SCROLLBUFFER_Y_MARGIN_SIZE=0 ^
  -DACE_SCROLLBUFFER_X_MARGIN_SIZE=2 ^
  -DACE_BOB_ALWAYS_ON_SCROLL_BUFFER=ON ^
  -DACE_BOB_PRISTINE_BUFFER=ON
if errorlevel 1 exit /b 1

cmake --build .
if errorlevel 1 exit /b 1

copy /y "%ROOT%\dist\data.pak" "%BUILD_DIR%\data.pak" >nul

if not exist "%BUILD_DIR%\s" mkdir "%BUILD_DIR%\s"
> "%BUILD_DIR%\s\startup-sequence" echo sunnyland.exe

cd /d "%ROOT%"

echo.
echo Built:
echo   %BUILD_DIR%\sunnyland.elf
echo   %BUILD_DIR%\sunnyland.exe
echo   %BUILD_DIR%\data.pak
echo.
echo The game opens data.pak.
echo.
endlocal
exit /b 0

:find_bart
for /d %%D in ("%USERPROFILE%\.cursor\extensions\bartmanabyss.amiga-debug-*") do (
  if exist "%%D\bin\win32\opt\bin\m68k-amiga-elf-gcc.exe" set "BART=%%D\bin\win32\opt"
)
if defined BART exit /b 0
for /d %%D in ("%USERPROFILE%\.vscode\extensions\bartmanabyss.amiga-debug-*") do (
  if exist "%%D\bin\win32\opt\bin\m68k-amiga-elf-gcc.exe" set "BART=%%D\bin\win32\opt"
)
exit /b 0
