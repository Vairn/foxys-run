cmake_minimum_required(VERSION 3.14)

# SunnyLand — OCS Amiga 500 platformer using the Sunny Land asset pack by Ansimuz,
# built with ACE (Amiga C Engine) and the Bartman Amiga GCC cross-toolchain.
#
# Two targets share this file:
#
# 1. Amiga (default). Configure with the Bartman m68k toolchain via
#    AmigaCMakeCrossToolchains, e.g.:
#
#      cmake .. -G Ninja \
#        -DCMAKE_TOOLCHAIN_FILE=<...>/m68k-bartman.cmake \
#        -DTOOLCHAIN_PATH=<...> \
#        -DTOOLCHAIN_PREFIX=m68k-amiga-elf \
#        -DM68K_CPU=68000 \
#        -DCMAKE_BUILD_TYPE=MinSizeRel
#
#    See build.bat and README.md.
#
# 2. Native PC (-DACE_HOST=ON). ACE's SDL host backend emulates the OCS chipset,
#    so the same sources run as a Windows/Linux executable. See build-host.bat.

# Must be declared before the AMIGA guard below, and before add_subdirectory(ace).
option(ACE_HOST "Build natively with ACE's SDL chipset backend instead of Amiga" OFF)

if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
	set(CMAKE_BUILD_TYPE Release CACHE STRING "Build configuration" FORCE)
endif()
# Release is speed-optimised (-O2). The CPU-bound work here is fixed-point
# physics and per-tile collision span scans; -O1 (MinSizeRel) left real speed
# on the table. -O2 (not -O3) keeps code size sane for the A500's RAM budget.
if(CMAKE_BUILD_TYPE STREQUAL "Release")
	set(CMAKE_C_FLAGS_RELEASE "-O2 -DNDEBUG" CACHE STRING
		"Flags used by the C compiler during RELEASE builds." FORCE)
endif()
if(CMAKE_BUILD_TYPE STREQUAL "MinSizeRel")
	set(CMAKE_C_FLAGS_MINSIZEREL "-O1 -DNDEBUG" CACHE STRING
		"Flags used by the C compiler during MINSIZEREL builds." FORCE)
endif()
if(CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
	set(CMAKE_C_FLAGS_RELWITHDEBINFO "-g -O1 -DNDEBUG" CACHE STRING
		"Flags used by the C compiler during RELWITHDEBINFO builds." FORCE)
endif()

project(sunnyland LANGUAGES C)

# Lowercase project name for binaries and packaging
string(TOLOWER ${PROJECT_NAME} PROJECT_NAME_LOWER)

if(NOT AMIGA AND NOT ACE_HOST)
	message(SEND_ERROR "This project only compiles for Amiga (use the Bartman m68k Amiga toolchain) or with -DACE_HOST=ON")
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DAMIGA -Wall -Wextra")
if(ACE_HOST)
	# AMIGA stays defined on host: the chipset is emulated, not abstracted away.
	# Frame pointers make the host crash handler's backtraces usable.
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DACE_HOST -g -fno-omit-frame-pointer")
else()
	# Toolchain INIT sometimes drops this; the Bartman toolchain needs soft-float.
	add_compile_options(-msoft-float)
endif()

# Readable stack traces for debugging (build.bat debug). Appended AFTER the
# toolchain flags so it overrides the release inlining/const-prop options
# without touching CMAKE_C_FLAGS/-nostdlib (which would break linking). This
# keeps *.constprop.N clones and collapsed frames out of crash backtraces.
option(SUNNY_TRACE "Disable inlining/const-prop cloning for legible stack traces" OFF)
if(SUNNY_TRACE)
	add_compile_options(-fno-inline -fno-ipa-cp-clone)
endif()
# Game logWrite() on RS-232 without ACE_DEBUG. Directory-level so every
# game .c sees the define (a target-only def after add_executable was easy
# to miss on the first lite configure).
option(SUNNY_SERIAL_LOG "logWrite to serial at 115200 8N1 without ACE_DEBUG" OFF)
if(SUNNY_SERIAL_LOG)
	add_compile_definitions(SUNNY_SERIAL_LOG)
endif()
message(STATUS "SUNNY_SERIAL_LOG: ${SUNNY_SERIAL_LOG}")

# ---- ACE configuration ------------------------------------------------
# OCS/ECS target, no AGA. Default ACE options are tuned for OCS already.
set(ACE_USE_ECS_FEATURES OFF CACHE BOOL "ACE: use OCS blitter (no ECS)" FORCE)
set(ACE_USE_AGA_FEATURES OFF CACHE BOOL "ACE: no AGA playfield" FORCE)
# Sunny Land tiles are square (16x16 default); non-square only if we switch.
set(ACE_USE_NON_SQUARE_TILES OFF CACHE BOOL "ACE: Sunny Land 16x16 tiles" FORCE)
# Y-scroll code is compiled in so a tall map can stamp new rows. 15-tall maps
# pass TAG_TILEBUFFER_Y_MARGIN 0 and keep the short (256+width-band) bitmaps.
# 32x64 vine BOBs need X_MARGIN 2 → 416px scroll width.
set(ACE_SCROLLBUFFER_ENABLE_SCROLL_Y ON CACHE BOOL "Y scroll code on; margin is per-level" FORCE)
set(ACE_SCROLLBUFFER_Y_MARGIN_SIZE 0 CACHE STRING "Default Y margin; tall maps pass 1" FORCE)
set(ACE_SCROLLBUFFER_X_MARGIN_SIZE 2 CACHE STRING "Room for 32px vine BOBs" FORCE)
# book/raid is the listing that pays for Y-scroll on a tall simpleBuffer (80 KB).
# This flag is the scroll-buffer / tilebuffer margin that listing does not use.
if(NOT DEFINED ACE_DEBUG)
	set(ACE_DEBUG OFF)
endif()
if(NOT DEFINED ACE_DEBUG_UAE)
	set(ACE_DEBUG_UAE OFF)
endif()
if(NOT DEFINED ACE_DEBUG_SERIAL)
	set(ACE_DEBUG_SERIAL OFF)
endif()
set(ACE_DEBUG ${ACE_DEBUG} CACHE BOOL "ACE: debug build" FORCE)
set(ACE_DEBUG_UAE ${ACE_DEBUG_UAE} CACHE BOOL "ACE: log to UAE/Bartman console (debug)" FORCE)
set(ACE_DEBUG_SERIAL ${ACE_DEBUG_SERIAL} CACHE BOOL "ACE: log to serial port (debug)" FORCE)
# BOBs are only ever drawn on the tile/scroll buffer. This swaps the per-blit
# Y wrap from a 68000 software modulo to SCROLLBUFFER_HEIGHT_MODULO (AND, with
# POT bitmap height).
# Pristine undraw: one extra scroll-sized CHIP bitmap (~55 KB) instead of two
# 16-wide undraw stacks (~223 KB at 46 BOBs). onTileDraw copies each finished
# terrain+prop tile into that bitmap so ACE can restore from it.
set(ACE_BOB_ALWAYS_ON_SCROLL_BUFFER ON CACHE BOOL "BOBs on scroll/tile buffer only" FORCE)
set(ACE_BOB_PRISTINE_BUFFER ON CACHE BOOL "Pristine undraw (one scroll copy)" FORCE)
# Prop-layer tile indices (tTileBufferTileIndex) exceed 255 (~630), so use UWORD
set(ACE_TILEBUFFER_TILE_TYPE UWORD CACHE STRING "Tilebuffer: Use UWORD for large tile index range." FORCE)

# ---- Sources -----------------------------------------------------------
# CONFIGURE_DEPENDS re-runs the glob at build time so newly added src files
# (e.g. hud.c/monsters.c/effects.c) are picked up without a manual reconfigure.
file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS src/*.c)
file(GLOB_RECURSE HEADERS CONFIGURE_DEPENDS src/*.h)

# Generated game data compiled into the binary (collision LUT).
# Tile maps + entities are loaded at runtime from data/levels/levelN.dat
# (campaign discovered at startup; optional per-level tileset/palette).
list(APPEND SOURCES ${PROJECT_SOURCE_DIR}/data/tiles/tilecollision.c)

include_directories(${PROJECT_SOURCE_DIR}/src)
# Generated asset headers live under data/ (sprites/*.h, tiles/*.h, levels/*.h).
include_directories(${PROJECT_SOURCE_DIR}/data)

add_subdirectory(deps/ace ace)
include_directories(${PROJECT_SOURCE_DIR}/deps/ace/include)
if(ACE_HOST)
	# NDK-shaped stubs (exec/, graphics/, hardware/, proto/) backed by the host.
	include_directories(${PROJECT_SOURCE_DIR}/deps/ace/host/include)
endif()

set(TARGET_NAME ${PROJECT_NAME_LOWER})

set(GAME_SOURCES ${SOURCES})
list(FILTER GAME_SOURCES EXCLUDE REGEX "main_intro\\.c$")
list(FILTER GAME_SOURCES EXCLUDE REGEX "src[/\\\\]intro\\.c$")
list(FILTER GAME_SOURCES EXCLUDE REGEX "src[/\\\\]splash\\.c$")
list(FILTER GAME_SOURCES EXCLUDE REGEX "src[/\\\\]slicecop\\.c$")

set(INTRO_SOURCES
	${PROJECT_SOURCE_DIR}/src/main_intro.c
	${PROJECT_SOURCE_DIR}/src/intro.c
	${PROJECT_SOURCE_DIR}/src/splash.c
	${PROJECT_SOURCE_DIR}/src/slicecop.c
	${PROJECT_SOURCE_DIR}/src/font.c
	${PROJECT_SOURCE_DIR}/src/assets.c
	${PROJECT_SOURCE_DIR}/src/music.c
	${PROJECT_SOURCE_DIR}/src/serial_log.c
)

# Bartman produces a raw ELF; elf2hunk converts it to a bootable Amiga hunk exe.
if(ELF2HUNK AND NOT ACE_HOST)
	set(GAME_EXECUTABLE ${TARGET_NAME}.elf)
	set(GAME_OUTPUT_EXECUTABLE ${TARGET_NAME}.exe)
	set(INTRO_EXECUTABLE sunnyintro.elf)
	add_executable(${GAME_EXECUTABLE} ${GAME_SOURCES} ${HEADERS})
	add_executable(${INTRO_EXECUTABLE} ${INTRO_SOURCES} ${HEADERS})
	target_compile_definitions(${INTRO_EXECUTABLE} PRIVATE SUNNY_INTRO)
	# Release hunks do not need HUNK_SYMBOL; LoadSeg skips it and Bartman
	# debug reads the ELF. -s drops ~20 KB of names from the floppy image.
	set(_ELF2HUNK_CMD ${ELF2HUNK} ${GAME_EXECUTABLE} ${GAME_OUTPUT_EXECUTABLE})
	set(_ELF2HUNK_INTRO ${ELF2HUNK} ${INTRO_EXECUTABLE} sunnyintro.exe)
	if(CMAKE_BUILD_TYPE MATCHES "^(Release|MinSizeRel)$")
		list(APPEND _ELF2HUNK_CMD -s)
		list(APPEND _ELF2HUNK_INTRO -s)
	endif()
	add_custom_command(
		TARGET ${GAME_EXECUTABLE} POST_BUILD
		COMMAND ${_ELF2HUNK_CMD}
		COMMENT "Converting ELF to Amiga hunk executable"
	)
	add_custom_command(
		TARGET ${INTRO_EXECUTABLE} POST_BUILD
		COMMAND ${_ELF2HUNK_INTRO}
		COMMENT "Converting intro ELF to Amiga hunk executable"
	)
else()
	set(GAME_EXECUTABLE ${TARGET_NAME})
	set(GAME_OUTPUT_EXECUTABLE ${TARGET_NAME})
	set(INTRO_EXECUTABLE sunnyintro)
	add_executable(${GAME_EXECUTABLE} ${GAME_SOURCES} ${HEADERS})
	add_executable(${INTRO_EXECUTABLE} ${INTRO_SOURCES} ${HEADERS})
	target_compile_definitions(${INTRO_EXECUTABLE} PRIVATE SUNNY_INTRO)
endif()

target_link_libraries(${GAME_EXECUTABLE} ace)
target_link_libraries(${INTRO_EXECUTABLE} ace)

if(ACE_HOST)
	if(NOT MSVC AND NOT APPLE)
		# Route the game's libc allocations through AllocMem() too, so the host's
		# CHIP/FAST budget reflects what the Amiga build would really use.
		target_link_options(${GAME_EXECUTABLE} PRIVATE
			"LINKER:--wrap,malloc"
			"LINKER:--wrap,calloc"
			"LINKER:--wrap,realloc"
			"LINKER:--wrap,free"
			"LINKER:--wrap,strdup"
			"LINKER:--wrap,_strdup"
			"LINKER:--wrap,_msize"
		)
		target_link_options(${INTRO_EXECUTABLE} PRIVATE
			"LINKER:--wrap,malloc"
			"LINKER:--wrap,calloc"
			"LINKER:--wrap,realloc"
			"LINKER:--wrap,free"
			"LINKER:--wrap,strdup"
			"LINKER:--wrap,_strdup"
			"LINKER:--wrap,_msize"
		)
	endif()

	if(WIN32)
		set_target_properties(${GAME_EXECUTABLE} PROPERTIES
			OUTPUT_NAME FoxysRun
			WIN32_EXECUTABLE TRUE
		)
		set_target_properties(${INTRO_EXECUTABLE} PROPERTIES
			OUTPUT_NAME FoxysIntro
			WIN32_EXECUTABLE TRUE
		)
	endif()

	# Both programs open "data.pak" in the working directory. The game archive
	# is staged under that name. intro.pak is the intro disk's archive; a boot
	# of the intro uses that file as data.pak.
	add_custom_command(
		TARGET ${GAME_EXECUTABLE} POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E copy_if_different
			${PROJECT_SOURCE_DIR}/dist/data.pak $<TARGET_FILE_DIR:${GAME_EXECUTABLE}>/data.pak
		COMMAND ${CMAKE_COMMAND} -E copy_if_different
			${PROJECT_SOURCE_DIR}/dist/intro.pak $<TARGET_FILE_DIR:${GAME_EXECUTABLE}>/intro.pak
		COMMENT "Staging prebuilt pak files next to the host executable"
	)

	# A shared MSYS2/UCRT SDL2 needs its DLL beside the exe. Static SDL2 (the
	# FetchContent fallback in ACE) needs nothing, hence the find rather than
	# a hard dependency on an imported target.
	if(WIN32)
		find_file(SUNNY_SDL2_DLL SDL2.dll
			HINTS ${CMAKE_PREFIX_PATH} ${SDL2_DIR}
			PATH_SUFFIXES bin ../bin
		)
		if(SUNNY_SDL2_DLL)
			add_custom_command(
				TARGET ${GAME_EXECUTABLE} POST_BUILD
				COMMAND ${CMAKE_COMMAND} -E copy_if_different
					${SUNNY_SDL2_DLL} $<TARGET_FILE_DIR:${GAME_EXECUTABLE}>
				COMMENT "Copying SDL2.dll next to the host executable"
			)
		endif()
	endif()
endif()
