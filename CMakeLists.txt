cmake_minimum_required(VERSION 3.20)
project(zamn_port C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# ---------------------------------------------------------------------------
# Vendored SNES core (LakeSnes, MIT). Serves as both the reference emulator for
# the co-simulation diff harness and the emulated PPU/APU the finished port
# keeps using. See third_party/lakesnes/LICENSE.txt.
# ---------------------------------------------------------------------------
file(GLOB SNES_CORE_SRC third_party/lakesnes/snes/*.c)
add_library(snescore STATIC ${SNES_CORE_SRC})
target_include_directories(snescore PUBLIC third_party/lakesnes/snes)

if(MSVC)
  # The vendored core is third-party C; silence its warnings, keep ours honest.
  target_compile_definitions(snescore PRIVATE _CRT_SECURE_NO_WARNINGS)
  target_compile_options(snescore PRIVATE /w)
endif()

# ---------------------------------------------------------------------------
# Phase 0a: headless boot -> PNG (no SDL). Verifies the core boots the ROM.
# ---------------------------------------------------------------------------
add_executable(zamn_headless src/headless.c)
# zamn_analysis, defined below, is for the movie replayer: `-m` makes this the
# fast way to see where a candidate input script actually gets to.
target_link_libraries(zamn_headless PRIVATE snescore zamn_analysis)
target_include_directories(zamn_headless PRIVATE third_party/stb)
if(MSVC)
  target_compile_definitions(zamn_headless PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

# ---------------------------------------------------------------------------
# Phase 1: analysis toolchain. `analysis` holds the pieces the tracer and the
# disassembler share (the 65816 instruction table, the CDL format, movie
# input); the Phase 3 co-simulation harness will reuse them too.
# ---------------------------------------------------------------------------
add_library(zamn_analysis STATIC
  src/analysis/w65816.c
  src/analysis/cdl.c
  src/analysis/movie.c)
target_include_directories(zamn_analysis PUBLIC src)
if(MSVC)
  target_compile_definitions(zamn_analysis PUBLIC _CRT_SECURE_NO_WARNINGS)
endif()

# zamn_trace: steps the core one instruction at a time and records what the CPU
# touches (code/data log, WRAM map, call graph, DMA log, instruction traces).
add_executable(zamn_trace src/trace.c)
target_link_libraries(zamn_trace PRIVATE snescore zamn_analysis)
target_include_directories(zamn_trace PRIVATE third_party/stb)

# zamn_disasm: turns a ROM + its CDL into an annotated 65816 listing.
add_executable(zamn_disasm src/disasm.c)
target_link_libraries(zamn_disasm PRIVATE zamn_analysis)

# ---------------------------------------------------------------------------
# The branch-coverage instrument (src/port/coverage.h). Port code marks its
# decision points; this holds the counters the harness reads. It is separate
# from the two port libraries below only because both of them mark sites, and
# the asset library cannot depend on the one that depends on it.
#
# `PORT_COVERAGE` is what switches it on, and it is PUBLIC so everything
# downstream marks. A shipping build simply does not link this target: with the
# define absent every mark compiles to nothing.
# ---------------------------------------------------------------------------
add_library(zamn_cover STATIC src/port/coverage.c)
target_include_directories(zamn_cover PUBLIC src)
target_compile_definitions(zamn_cover PUBLIC PORT_COVERAGE=1)

# ---------------------------------------------------------------------------
# Phase 2: asset pipeline. Unlike `analysis`, this library is *port* code — it
# ships in the finished game, so it depends on nothing but libc and must stay
# free of anything emulator- or tooling-specific.
# ---------------------------------------------------------------------------
add_library(zamn_assets_lib STATIC
  src/assets/lzss.c
  src/assets/gfx.c
  src/assets/rom.c
  src/assets/level.c
  src/assets/actor.c
  src/assets/sprite.c
  src/assets/music.c)
target_include_directories(zamn_assets_lib PUBLIC src)
target_link_libraries(zamn_assets_lib PUBLIC zamn_cover)

# zamn_assets: decodes ROM data, and verifies the decoders against the ROM's
# own routines by intercepting them under the reference core.
add_executable(zamn_assets src/assets.c)
target_link_libraries(zamn_assets PRIVATE snescore zamn_analysis zamn_assets_lib)
target_include_directories(zamn_assets PRIVATE third_party/stb)

# ---------------------------------------------------------------------------
# Phase 3: the native game logic. Like `assets`, this is *port* code — it ships
# in the finished game and depends on nothing but libc. It keeps the SNES's WRAM
# layout byte for byte (src/port/wram.h explains why), which is what lets the
# co-simulation harness diff it against the ROM.
# ---------------------------------------------------------------------------
add_library(zamn_port STATIC
  src/port/fade.c
  src/port/oam.c
  src/port/sprite_cache.c
  src/port/thread.c)
target_include_directories(zamn_port PUBLIC src)
target_link_libraries(zamn_port PUBLIC zamn_assets_lib)

# The harness itself is tooling, not port code: it may use the emulator, and it
# goes away in Phase 4 when the reference is cut loose.
add_library(zamn_cosim_lib STATIC
  src/cosim/cosim.c
  src/cosim/routines.c)
target_include_directories(zamn_cosim_lib PUBLIC src)
target_link_libraries(zamn_cosim_lib PUBLIC snescore zamn_port zamn_analysis)

# zamn_cosim: runs the port against the ROM, either checked call-by-call
# (`verify`) or actually substituted and diffed per frame (`run`).
add_executable(zamn_cosim src/cosim.c)
target_link_libraries(zamn_cosim PRIVATE zamn_cosim_lib)

# ---------------------------------------------------------------------------
# Phase 0b: interactive SDL frontend (the playable baseline).
# Uses a system SDL2 if present, otherwise fetches & builds SDL2 from source.
# Disable with -DZAMN_BUILD_SDL=OFF (headless target always builds).
# ---------------------------------------------------------------------------
option(ZAMN_BUILD_SDL "Build the interactive SDL frontend" ON)

if(ZAMN_BUILD_SDL AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/main_sdl.c")
  find_package(SDL2 CONFIG QUIET)
  if(NOT SDL2_FOUND)
    message(STATUS "SDL2 not found on system: fetching & building from source")
    include(FetchContent)
    set(SDL_SHARED ON  CACHE BOOL "" FORCE)
    set(SDL_STATIC OFF CACHE BOOL "" FORCE)
    set(SDL_TEST   OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(SDL2
      GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
      GIT_TAG release-2.30.9
      GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(SDL2)
  endif()

  add_executable(zamn WIN32 src/main_sdl.c)
  target_link_libraries(zamn PRIVATE snescore SDL2::SDL2 SDL2::SDL2main)
  target_include_directories(zamn PRIVATE third_party/lakesnes/snes)
  if(MSVC)
    target_compile_definitions(zamn PRIVATE _CRT_SECURE_NO_WARNINGS)
  endif()

  # SDL2 builds as a DLL; place it next to the executable so zamn.exe runs.
  if(TARGET SDL2::SDL2)
    add_custom_command(TARGET zamn POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
              $<TARGET_FILE:SDL2::SDL2> $<TARGET_FILE_DIR:zamn>)
  endif()
  message(STATUS "Interactive 'zamn' target enabled")
endif()
