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
target_link_libraries(zamn_headless PRIVATE snescore)
target_include_directories(zamn_headless PRIVATE third_party/stb)
if(MSVC)
  target_compile_definitions(zamn_headless PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

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
