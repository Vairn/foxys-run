# Foxy's Run

An Amiga 500 (OCS) platformer. This repository is the game and intro source, plus the packed data those programs load. It is enough to compile `sunnyland` and `sunnyintro`.

The playfield art comes from the [Sunny Land](https://ansimuz.itch.io/sunny-land-pixel-game-art) pack by Ansimuz. That pack is not in this repository. `dist/data.pak` and `dist/intro.pak` are the already converted, compressed archives.

The engine under `deps/ace` is [ACE](https://github.com/AmigaPorts/ACE), included here under the Mozilla Public License 2.0 (`deps/ace/LICENSE`).

## Target

Amiga 500 / A500+, OCS, 68000, 512 KB chip RAM + 512 KB slow RAM, PAL 320×256.

## Layout

```text
src/            Game and intro (C11)
CMakeLists.txt  Amiga build and the native host build
build.bat       Cross-compile with the Bartman m68k toolchain
build-host.bat  Native build (ACE_HOST, SDL)
launch.bat      Boot the Amiga build in WinUAE
dist/data.pak   Game archive (levels, graphics, music, sound effects)
dist/intro.pak  Intro archive
deps/ace/       ACE, with the changes this game builds against
data/           Headers and the tile-collision table compiled into the binary
```

## Prerequisites

- CMake 3.14+ and Ninja
- The Bartman Amiga GCC toolchain from the [amiga-debug](https://marketplace.visualstudio.com/items?itemName=bartmanabyss.amiga-debug) extension (Cursor or VS Code)
- [AmigaCMakeCrossToolchains](https://github.com/AmigaPorts/AmigaCMakeCrossToolchains)
- WinUAE or Amiga Forever, and a Kickstart 1.3 ROM you already own, if you want to boot the result

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

`build.bat debug`, `serial`, `ace`, and `lite` match the same modes as a development build. Each one copies the same prebuilt archives into its output directory.

```text
build\sunnyland.elf
build\sunnyland.exe
build\sunnyintro.exe
build\data.pak
build\intro.pak
```

Both programs open a file named `data.pak`. The copy in `build\` is the game. An intro disk uses `intro.pak` under that name.

Native host:

```bat
build-host.bat
```

`UCRT` overrides the default `C:\msys64\ucrt64`. Run `FoxysRun.exe` from the build directory so `data.pak` resolves.

## Running (WinUAE)

```bat
launch.bat
```

Mounts `build\` as `DH0:SunnyLand` on an OCS A500 with 512 KB chip and 512 KB slow RAM. `s/startup-sequence` launches the game.
