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
# zamn_assets_lib, defined below, is for `src/widescreen.h`: the margins of a
# widened picture are drawn from the same metasprites and the same VRAM cache
# geometry the game draws from.
target_link_libraries(zamn_headless PRIVATE snescore zamn_analysis zamn_assets_lib)
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

# `src/scale.h` is arithmetic with no SDL in it — where the framebuffer lands on
# the screen and how it gets there — so it can be checked without a window, and
# is. Builds and runs in about a second; `zamn_test_scale` needs no ROM.
add_executable(zamn_test_scale tools/test_scale.c)
target_include_directories(zamn_test_scale PRIVATE src)
if(MSVC)
  target_compile_definitions(zamn_test_scale PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

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
  src/assets/music.c
  src/assets/password.c)
target_include_directories(zamn_assets_lib PUBLIC src)
target_link_libraries(zamn_assets_lib PUBLIC zamn_cover)

# zamn_assets: decodes ROM data, and verifies the decoders against the ROM's
# own routines by intercepting them under the reference core.
add_executable(zamn_assets src/assets.c)
target_link_libraries(zamn_assets PRIVATE snescore zamn_analysis zamn_assets_lib
                                          zamn_port)
target_include_directories(zamn_assets PRIVATE third_party/stb)

# ---------------------------------------------------------------------------
# Phase 3: the native game logic. Like `assets`, this is *port* code — it ships
# in the finished game and depends on nothing but libc. It keeps the SNES's WRAM
# layout byte for byte (src/port/wram.h explains why), which is what lets the
# co-simulation harness diff it against the ROM.
# ---------------------------------------------------------------------------
add_library(zamn_port STATIC
  src/port/apu.c
  src/port/boss.c
  src/port/bossbg.c
  src/port/collide.c
  src/port/fade.c
  src/port/floor.c
  src/port/hud.c
  src/port/camera.c
  src/port/levelmap.c
  src/port/lzss.c
  src/port/monster.c
  src/port/oam.c
  src/port/player.c
  src/port/rng.c
  src/port/score.c
  src/port/sprite_cache.c
  src/port/step.c
  src/port/terrain.c
  src/port/thread.c
  src/port/trig.c)
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
# The interactive frontend — the playable build, with the port substituted in.
#
# It links `zamn_cosim_lib` rather than the bare core, which is the whole of
# what makes it native: the same `COSIM_NATIVE` interception `zamn_cosim run`
# uses, pointed at one live core with a keyboard on the front instead of at a
# pair of cores driven by a movie. `--stock` gets the old Phase 0b baseline back
# by clearing the enable mask. See the header comment in src/main_sdl.c.
#
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
  # zamn_cosim_lib pulls in snescore, zamn_port and zamn_analysis transitively:
  # the core to run on, the port to substitute, and the movie reader for `-m`.
  target_link_libraries(zamn PRIVATE zamn_cosim_lib SDL2::SDL2 SDL2::SDL2main)
  target_include_directories(zamn PRIVATE third_party/lakesnes/snes third_party/stb)
  if(MSVC)
    target_compile_definitions(zamn PRIVATE _CRT_SECURE_NO_WARNINGS)
  endif()

  # `src/present.h` is the SDL half of the scaling, and it is checked the only
  # way a filter can be: draw through it into a software renderer, read the
  # pixels back and measure them. No GPU, no display — `SDL_VIDEODRIVER=dummy`
  # is set from inside — so this runs anywhere the frontend builds.
  add_executable(zamn_test_present tools/test_present.c)
  target_link_libraries(zamn_test_present PRIVATE SDL2::SDL2 SDL2::SDL2main)
  target_include_directories(zamn_test_present PRIVATE src)
  if(MSVC)
    target_compile_definitions(zamn_test_present PRIVATE _CRT_SECURE_NO_WARNINGS)
  endif()

  # `src/pad.h` is the controller layer, and the half of it that decides how the
  # game feels — deadzone, hysteresis, the eight-way snap, the button table — is
  # arithmetic. This checks it with no device attached and no subsystem started:
  # it links SDL only for the `SDL_CONTROLLER_BUTTON_*` names. `zamn_analysis`
  # comes along for `BTN_*`, which is a header, but the include path is its.
  add_executable(zamn_test_pad tools/test_pad.c)
  target_link_libraries(zamn_test_pad PRIVATE SDL2::SDL2 SDL2::SDL2main)
  target_include_directories(zamn_test_pad PRIVATE src)
  if(MSVC)
    target_compile_definitions(zamn_test_pad PRIVATE _CRT_SECURE_NO_WARNINGS)
  endif()

  # SDL2 builds as a DLL; place it next to the executable so zamn.exe runs.
  if(TARGET SDL2::SDL2)
    add_custom_command(TARGET zamn POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
              $<TARGET_FILE:SDL2::SDL2> $<TARGET_FILE_DIR:zamn>)
  endif()
  message(STATUS "Interactive 'zamn' target enabled")
endif()
