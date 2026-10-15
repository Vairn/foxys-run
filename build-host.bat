@echo off
setlocal EnableExtensions
set "ROOT=%~dp0"
set "ROOT=%ROOT:~0,-1%"

rem ------------------------------------------------------------------
rem Native PC build using ACE's SDL chipset backend (ACE_HOST).
rem
rem   build-host.bat          -> Release, output in build-host\
rem   build-host.bat debug    -> RelWithDebInfo + ACE_DEBUG, build-host-debug\
rem
rem This is the same game sources as build.bat, but ACE's software
rem Agnus/Denise/Paula/CIA stands in for $DFF000 and SDL presents the
rem result. It is not an emulator: there is no 68000 and no Kickstart,
rem so it will not reproduce CPU-timing or alignment bugs. Keep using
rem build.bat + WinUAE to validate the real Amiga build.
rem
rem Requires MSYS2 UCRT64 with gcc and SDL2:
rem   pacman -S mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-SDL2 ninja
rem ------------------------------------------------------------------
set "MODE=release"
if /I "%~1"=="debug" set "MODE=debug"

if "%MODE%"=="debug" (
  set "BUILD_DIR=%ROOT%\build-host-debug"
  set "BUILD_TYPE=RelWithDebInfo"
  set "ACE_DBG=ON"
) else (
  set "BUILD_DIR=%ROOT%\build-host"
  set "BUILD_TYPE=Release"
  set "ACE_DBG=OFF"
)

if not defined UCRT set "UCRT=C:\msys64\ucrt64"
if not exist "%UCRT%\bin\gcc.exe" (
  echo MSYS2 UCRT64 gcc not found at:
  echo   %UCRT%\bin\gcc.exe
  echo Install MSYS2, then: pacman -S mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-SDL2 ninja
  exit /b 1
)

rem UCRT64 first on PATH so CMake picks up its gcc and SDL2, and so the
rem freshly built exe can resolve SDL2.dll when run from this shell.
set "PATH=%UCRT%\bin;C:\Program Files\CMake\bin;%PATH%"

if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"
cd /d "%BUILD_DIR%"

echo Build mode: %MODE%  (type=%BUILD_TYPE%, dir=%BUILD_DIR%)

rem The host enforces a real CHIP/FAST budget, so the preset has to match the
rem machine the game is built for: the two double-buffered scroll bitmaps are
rem ~117K each and do not fit a stock 512K-chip A500. Override with
rem   set SUNNY_HOST_MACHINE=A600
rem before running this script. Presets: A500, A500_512_512, A500_1MB, A600, A1200.
if "%SUNNY_HOST_MACHINE%"=="" set "SUNNY_HOST_MACHINE=A500_1MB"

cmake "%ROOT%" -G Ninja ^
  -DACE_HOST=ON ^
  -DCMAKE_C_COMPILER="%UCRT:\=/%/bin/gcc.exe" ^
  -DCMAKE_PREFIX_PATH="%UCRT:\=/%" ^
  -DCMAKE_BUILD_TYPE=%BUILD_TYPE% ^
  -DACE_DEBUG=%ACE_DBG% ^
  -DACE_DEBUG_UAE=OFF ^
  -DACE_HOST_MACHINE=%SUNNY_HOST_MACHINE% ^
  -DACE_HOST_MEM_MODE=STRICT ^
  -DACE_HOST_DEBUG=ON ^
  -DACE_HOST_USE_VIRTUAL_JOYSTICK=ON ^
  -DACE_USE_ECS_FEATURES=OFF ^
  -DACE_USE_AGA_FEATURES=OFF ^
  -DACE_SCROLLBUFFER_ENABLE_SCROLL_Y=ON ^
  -DACE_SCROLLBUFFER_Y_MARGIN_SIZE=0 ^
  -DACE_SCROLLBUFFER_X_MARGIN_SIZE=2 ^
  -DACE_BOB_ALWAYS_ON_SCROLL_BUFFER=ON
if errorlevel 1 exit /b 1

cmake --build .
if errorlevel 1 exit /b 1

copy /y "%ROOT%\dist\data.pak" "%BUILD_DIR%\data.pak" >nul

cd /d "%ROOT%"

echo.
echo Built: %BUILD_DIR%\FoxysRun.exe
echo Run it from its own directory so data.pak is beside the exe:
echo   cd /d "%BUILD_DIR%" ^&^& FoxysRun.exe
echo.
echo In-game host keys: F10 timing log, F11 memory HUD, F12 DMA overlay.
echo.
endlocal
