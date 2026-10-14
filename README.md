# Foxy's Run

A platformer. This repository is the game source, plus the packed data it loads. It is enough to compile `sunnyland`.

The playfield art comes from the [Sunny Land](https://ansimuz.itch.io/sunny-land-pixel-game-art) pack by Ansimuz. That pack is not in this repository. `dist/data.pak` is the already converted, compressed archive.

The engine under `deps/ace` is [ACE](https://github.com/AmigaPorts/ACE), included here under the Mozilla Public License 2.0 (`deps/ace/LICENSE`).

## Target

The binary is 68000 code with AGA features off, PAL 320×256. `launch.bat` and the debugger boot it on a stock A1200: 68020, AGA, 2 MB chip RAM, Kickstart 3.1.

## Layout

```text
includes/       Headers, same module folders as src/
  sprites/      Generated sprite frame sizes
  levels/       Level entity type constants
  tiles/        Tile-collision constants
src/            Game sources (C11)
  game/         Play loop, screens, session, shared constants
  player/       Player
  world/        Levels, terrain, collision table, sky
  entities/     Monsters, pickups, shots, effects
  props/        Doors, switches, platforms, trampolines, vines, signs
  ui/           HUD, overlay, font
  audio/        Music and sound effects
  assets/       Pak loading
  debug/        Startup stack and serial log
CMakeLists.txt  Amiga build and the native host build
build.bat       Cross-compile with the Bartman m68k toolchain
build-host.bat  Native build (ACE_HOST, SDL)
launch.bat      Boot the Amiga build in WinUAE
dist/data.pak   Game archive (levels, graphics, music, sound effects)
deps/ace/       ACE, with the changes this game builds against
```

## Prerequisites

- CMake 3.14+ and Ninja
- The Bartman Amiga GCC toolchain from the [amiga-debug](https://marketplace.visualstudio.com/items?itemName=bartmanabyss.amiga-debug) extension (Cursor or VS Code)
- [AmigaCMakeCrossToolchains](https://github.com/AmigaPorts/AmigaCMakeCrossToolchains)
- WinUAE or Amiga Forever, and a Kickstart 3.1 A1200 ROM you already own (`amiga-os-310-a1200.rom`), if you want to boot the result

The host build also needs MSYS2 UCRT64 gcc and SDL2:

```text
pacman -S mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-SDL2 ninja
```

## Building

Point the Amiga build at your toolchain file, then run it:

```bat
set AMIGA_CMAKE_TOOLCHAIN=C:\path\to\AmigaCMakeCrossToolchains\m68k-bartman.cmake
build.bat
```

`build.bat` looks for `m68k-amiga-elf-gcc` under the `amiga-debug` extension. If it lives somewhere else, set `BART` to the directory that contains `bin\m68k-amiga-elf-gcc.exe`.

`build.bat` with no arguments is the Release build in `build\`. The other modes are:

```text
build.bat debug     RelWithDebInfo, symbols, ACE_DEBUG off      build-debug\
build.bat serial    RelWithDebInfo, ACE_DEBUG and serial        build-serial\
build.bat ace       Release, ACE_DEBUG and the UAE console      build-ace\
build.bat lite      Release, game log lines to serial only      build-serial-lite\
build.bat file      Release, ACE_DEBUG, log file sunnyland.log  build-file\
```

Each mode copies `dist\data.pak` into its output directory.

```text
build\sunnyland.elf
build\sunnyland.exe
build\data.pak
```

The game opens `data.pak` from the build directory.

Native host:

```bat
build-host.bat
```

`UCRT` overrides the default `C:\msys64\ucrt64`. Run `FoxysRun.exe` from the build directory so `data.pak` resolves.

## Running (WinUAE)

```bat
launch.bat
```

Writes `sunnyland_a1200.uae` and boots `build\` as `DH0:SunnyLand` on a stock A1200 (68020, AGA, 2 MB chip, Kickstart 3.1). `s/startup-sequence` launches `sunnyland.exe`.

```bat
launch.bat file
```

Same machine, using `build-file\`. ACE writes `sunnyland.log` in that directory.

F5 uses the Amiga debugger configurations in `.vscode/launch.json`, also an A1200 with Kickstart 3.1. **Foxy's Run A1200** rebuilds `build\` first. **Foxy's Run A1200 (no rebuild)** launches the ELF that is already there.
