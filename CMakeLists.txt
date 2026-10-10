cmake_minimum_required(VERSION 3.20)
project(zamn_port C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# The maths library is its own library on Unix, and the port, the frontend and
# several tests use it.
if(UNIX)
  link_libraries(m)
endif()

# ---------------------------------------------------------------------------
# Vendored SNES core (LakeSnes, MIT). Serves as both the reference emulator for
# the co-simulation diff harness and the emulated PPU/APU the finished port
# keeps using. See third_party/lakesnes/LICENSE.txt.
#
# It is two libraries. `snescore` is the whole console. `snescore_without_ppu`
# is all of it but the emulated PPU and the one file that calls it, for a
# program whose picture `src/video` draws alone: linked against that one, a
# call of the PPU from anywhere is an error and not a PPU quietly linked.
# ---------------------------------------------------------------------------
file(GLOB SNES_CORE_SRC third_party/lakesnes/snes/*.c)
set(SNES_PPU_SRC
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party/lakesnes/snes/ppu.c
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party/lakesnes/snes/snes_ppu.c)
list(REMOVE_ITEM SNES_CORE_SRC ${SNES_PPU_SRC})
add_library(snescore_without_ppu STATIC ${SNES_CORE_SRC})
target_include_directories(snescore_without_ppu PUBLIC third_party/lakesnes/snes)
add_library(snescore STATIC ${SNES_PPU_SRC})
target_link_libraries(snescore PUBLIC snescore_without_ppu)

if(MSVC)
  # The vendored core is third-party C; silence its warnings, keep ours honest.
  foreach(core snescore snescore_without_ppu)
    target_compile_definitions(${core} PRIVATE _CRT_SECURE_NO_WARNINGS)
    target_compile_options(${core} PRIVATE /w)
  endforeach()
endif()

# ---------------------------------------------------------------------------
# The picture, drawn natively. `src/video/video.c` draws a scanline from VRAM,
# the palette and the registers, and `chip.c` is the console's video chip:
# the registers, the memories and the picture. Neither knows anything of the
# emulator. `console.c` puts a chip where an emulated console's video chip
# is, and `state.c` writes the chip's share of a saved state with the
# emulator's handler. None of it calls the emulated PPU.
#
# A program then links one of two more, which is where it is settled whether
# it has a PPU at all. `zamn_video_with_ppu` makes a console with its PPU
# beside the chip (`beside.c`), to draw in its place or be checked against:
# all three renderers. `zamn_video_without_ppu` makes a console with none,
# and has the native renderer alone.
# ---------------------------------------------------------------------------
add_library(zamn_video STATIC
  src/video/video.c
  src/video/registers.c
  src/video/frame.c
  src/video/chip.c
  src/video/state.c
  src/video/console.c)
target_include_directories(zamn_video PUBLIC src)
target_link_libraries(zamn_video PUBLIC snescore_without_ppu)

add_library(zamn_video_with_ppu STATIC src/video/beside.c src/video/with_ppu.c)
target_link_libraries(zamn_video_with_ppu PUBLIC zamn_video snescore)

add_library(zamn_video_without_ppu STATIC src/video/without_ppu.c)
target_link_libraries(zamn_video_without_ppu PUBLIC zamn_video)

# ...and its test: PPUs filled with noise, every line drawn both ways. No ROM.
add_executable(zamn_test_video tools/test_video.c)
target_link_libraries(zamn_test_video PRIVATE zamn_video_with_ppu)
if(MSVC)
  target_compile_definitions(zamn_test_video PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

# ...and the registers': writes and reads at random, done to both. No ROM.
add_executable(zamn_test_registers tools/test_registers.c)
target_link_libraries(zamn_test_registers PRIVATE zamn_video_with_ppu)
if(MSVC)
  target_compile_definitions(zamn_test_registers PRIVATE _CRT_SECURE_NO_WARNINGS)
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
# ...and the port, because `src/widescreen.h` reads the sprite pass's owner
# table to anchor the sprites of a screen-space record.
# ...and `src/video`, which keeps the chip's registers that it reads.
target_link_libraries(zamn_headless PRIVATE snescore zamn_analysis zamn_assets_lib zamn_port
                      zamn_video_with_ppu)
target_include_directories(zamn_headless PRIVATE third_party/stb)
if(MSVC)
  target_compile_definitions(zamn_headless PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

# The launcher's icon (tools/make_icon.c): the title screen, drawn by the core
# from the player's cartridge, as a Windows .ico. The launcher target below
# runs it when there is a cartridge to run.
add_executable(zamn_icon tools/make_icon.c)
target_link_libraries(zamn_icon PRIVATE snescore)
target_include_directories(zamn_icon PRIVATE third_party/stb)
if(MSVC)
  target_compile_definitions(zamn_icon PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
if(UNIX)
  target_link_libraries(zamn_icon PRIVATE m)
endif()

# The launcher's heading (tools/make_logo.c): the title screen's logo, drawn
# by the core from the player's cartridge and cut from the title, as a C
# header. The launcher target below runs it when there is a cartridge to run.
add_executable(zamn_logo tools/make_logo.c)
target_link_libraries(zamn_logo PRIVATE snescore)
target_include_directories(zamn_logo PRIVATE third_party/stb)
if(MSVC)
  target_compile_definitions(zamn_logo PRIVATE _CRT_SECURE_NO_WARNINGS)
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

# `src/smooth.h` is the arithmetic of showing a picture between two of the
# game's frames — where a sprite or a scroll is part of the way from one tick to
# the next, and when it is not the same thing at all — and `pace_lock_k` is how
# many such pictures a display has room for. Neither needs SDL, a PPU or a ROM.
add_executable(zamn_test_smooth tools/test_smooth.c)
target_include_directories(zamn_test_smooth PRIVATE src)
if(MSVC)
  target_compile_definitions(zamn_test_smooth PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

# `src/layers.h` takes a frame apart into planes and sprites so that the
# frontend can draw the pictures between two ticks a fraction of a pixel apart.
# Its claim -- the list drawn unmoved is the frame the PPU drew -- can only be
# checked against frames, so this one runs a movie: hand it the cartridge and
# a movie and it reports, frame by frame, identical or not and why not.
add_executable(zamn_test_layers tools/test_layers.c)
target_link_libraries(zamn_test_layers PRIVATE zamn_cosim_lib zamn_video_with_ppu)
target_include_directories(zamn_test_layers PRIVATE src third_party/stb)
if(MSVC)
  target_compile_definitions(zamn_test_layers PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

# A movie as video: the same pictures, drawn in software at any size, as raw
# frames for ffmpeg, and the sound as a WAV. How a trailer is made at 4K on a
# display that is not 4K.
add_executable(zamn_record tools/record.c)
target_link_libraries(zamn_record PRIVATE zamn_cosim_lib zamn_video_with_ppu)
target_include_directories(zamn_record PRIVATE src)
if(MSVC)
  target_compile_definitions(zamn_record PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

# `src/twinstick.h` is the other half of the controller work and is a ROM patch,
# so it is checked the way a ROM patch has to be: exact bytes, against a
# synthetic cartridge built to the header's own description. No SDL and no ROM —
# hand it the cartridge as an argument and it checks the offsets against that
# too, which is the part a synthetic ROM cannot claim.
add_executable(zamn_test_twinstick tools/test_twinstick.c)
target_include_directories(zamn_test_twinstick PRIVATE src)
# It links the port because half of `--twin-stick` *is* the port: `$80:D1FF` is
# substituted, so the ROM patch is the `--stock` path and `player_set_aim` is the
# one the playable build takes. Only the section given a real cartridge runs it.
target_link_libraries(zamn_test_twinstick PRIVATE zamn_port)
if(MSVC)
  target_compile_definitions(zamn_test_twinstick PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

# The cheats (src/cheats.h): the flags, the patches into an image and back out
# of it, what a tick holds, the demo, and the port's two handlers -- against a
# cartridge built to the header's description, and the real one if it is there.
add_executable(zamn_test_cheats tools/test_cheats.c)
target_include_directories(zamn_test_cheats PRIVATE src)
target_link_libraries(zamn_test_cheats PRIVATE zamn_port)
if(MSVC)
  target_compile_definitions(zamn_test_cheats PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

# The sound effect overlay (src/sfx_overlay.h): a movie run with it and without
# it, which must leave the machine identical and add sound only where the
# driver lost some. Needs the cartridge; skips without it.
add_executable(zamn_test_sfx_overlay tools/test_sfx_overlay.c)
target_include_directories(zamn_test_sfx_overlay PRIVATE src)
target_link_libraries(zamn_test_sfx_overlay PRIVATE snescore zamn_analysis)
if(MSVC)
  target_compile_definitions(zamn_test_sfx_overlay PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

# The monster sounds of the other sample sets (src/bank_sfx.h): the checks where
# they were surveyed, a movie run watched and not, which must leave the machine
# identical and play every failed check, and each copy against the real APU on
# a level with its set. Needs the cartridge; skips without it.
add_executable(zamn_test_bank_sfx tools/test_bank_sfx.c)
target_include_directories(zamn_test_bank_sfx PRIVATE src)
target_link_libraries(zamn_test_bank_sfx PRIVATE zamn_cosim_lib snescore)
if(MSVC)
  target_compile_definitions(zamn_test_bank_sfx PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

# `src/radar.h`: the survivor radar with every square drawn, against the
# console's one at a time -- the same machine, and a picture changed only
# where the squares are.
add_executable(zamn_test_radar tools/test_radar.c)
target_link_libraries(zamn_test_radar PRIVATE zamn_cosim_lib zamn_video_with_ppu)
target_include_directories(zamn_test_radar PRIVATE src)
if(MSVC)
  target_compile_definitions(zamn_test_radar PRIVATE _CRT_SECURE_NO_WARNINGS)
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
  src/port/bodies.c
  src/port/boss.c
  src/port/bubble.c
  src/port/bossbg.c
  src/port/collide.c
  src/port/cursor.c
  src/port/fade.c
  src/port/doll_thread.c
  src/port/flame.c
  src/port/flinch.c
  src/port/hit.c
  src/port/knock.c
  src/port/player_small.c
  src/port/card.c
  src/port/jumper.c
  src/port/loads.c
  src/port/line.c
  src/port/lob.c
  src/port/shot5.c
  src/port/dim.c
  src/port/layers_setup.c
  src/port/password_check.c
  src/port/logo.c
  src/port/radar_thread.c
  src/port/riser.c
  src/port/seeker.c
  src/port/tile_rows.c
  src/port/floor.c
  src/port/follower.c
  src/port/football.c
  src/port/text.c
  src/port/textmap.c
  src/port/axe.c
  src/port/blinker.c
  src/port/bolt.c
  src/port/stepper.c
  src/port/wander.c
  src/port/carried.c
  src/port/pursuer.c
  src/port/tracker.c
  src/port/walker.c
  src/port/begin.c
  src/port/hold.c
  src/port/ant_thread.c
  src/port/ant_states.c
  src/port/boss_thread.c
  src/port/tentacle.c
  src/port/decoy.c
  src/port/lunge.c
  src/port/page.c
  src/port/neighbours.c
  src/port/objects.c
  src/port/swipe.c
  src/port/tile_put.c
  src/port/weeds.c
  src/port/clears.c
  src/port/hud.c
  src/port/camera.c
  src/port/chainsaw.c
  src/port/levelmap.c
  src/port/lzss.c
  src/port/ant.c
  src/port/oam.c
  src/port/player.c
  src/port/rng.c
  src/port/sched.c
  src/port/score.c
  src/port/sprite_cache.c
  src/port/step.c
  src/port/terrain.c
  src/port/thread.c
  src/port/trig.c
  src/port/vblank.c
  src/port/walk.c
  src/port/chase.c
  src/port/zombie.c
  src/port/doll.c
  src/port/pause.c
  src/port/pose.c
  src/port/werewolf.c
  src/port/clone.c
  src/port/mainloop.c
  src/port/martian.c
  src/port/slime.c
  src/port/spawner.c
  src/port/palfade.c
  src/port/spider.c
  src/port/squirt.c
  src/port/stuck.c
  src/port/fishman.c
  src/port/figure_colours.c
  src/port/bystander.c
  src/port/saucer.c
  src/port/screen_tiles.c
  src/port/menus.c
  src/port/dma.c
  src/port/palcycle.c
  src/port/player_frame.c
  src/port/demo.c
  src/port/frontend.c
  src/port/spawnlist.c)
target_include_directories(zamn_port PUBLIC src)
target_link_libraries(zamn_port PUBLIC zamn_assets_lib)

# The harness itself is tooling, not port code: it may use the emulator, and it
# goes away in Phase 4 when the reference is cut loose. It calls no PPU: the
# two consoles `cosim_lockstep` compares are handed to it.
add_library(zamn_cosim_lib STATIC
  src/cosim/cosim.c
  src/cosim/profile.c
  src/cosim/routines.c)
target_include_directories(zamn_cosim_lib PUBLIC src)
target_link_libraries(zamn_cosim_lib PUBLIC snescore_without_ppu zamn_port zamn_analysis)

# zamn_cosim: runs the port against the ROM, either checked call-by-call
# (`verify`) or actually substituted and diffed per frame (`run`).
add_executable(zamn_cosim src/cosim.c)
target_link_libraries(zamn_cosim PRIVATE zamn_cosim_lib snescore)

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

  # It is built twice from the one source. `zamn` is the game, and has no
  # emulated PPU in it: it is linked against the console without one, so that
  # it builds at all is the proof that nothing it runs calls the PPU.
  # `zamn_with_ppu` has the PPU beside `src/video`'s chip, and so `--renderer
  # check` and `emulated`. It is the test the game's picture is held to, and
  # is left in the build directory.
  #
  # zamn_cosim_lib pulls in the core, zamn_port and zamn_analysis transitively:
  # the core to run on, the port to substitute, and the movie reader for `-m`.
  add_executable(zamn WIN32 src/main_sdl.c)
  target_link_libraries(zamn PRIVATE zamn_video_without_ppu)
  add_executable(zamn_with_ppu WIN32 src/main_sdl.c)
  target_link_libraries(zamn_with_ppu PRIVATE zamn_video_with_ppu)
  foreach(game zamn zamn_with_ppu)
    target_link_libraries(${game} PRIVATE zamn_cosim_lib SDL2::SDL2 SDL2::SDL2main)
    if(WIN32)
      # The desktop compositor's frame statistics, for the cadence report.
      target_link_libraries(${game} PRIVATE dwmapi)
    endif()
    target_include_directories(${game} PRIVATE third_party/lakesnes/snes third_party/stb)
    if(MSVC)
      target_compile_definitions(${game} PRIVATE _CRT_SECURE_NO_WARNINGS)
    endif()
  endforeach()

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

  # `src/config.h` is the player's `zamn.ini`: settings and every binding. It is
  # parsing, and this holds it to the one thing parsing must not get wrong --
  # the file the frontend writes reads back as exactly the defaults -- and to
  # the rest of what its header says. SDL for the names of keys and buttons
  # only; no subsystem is started. Give it a scratch path and it also writes
  # the file and reads it back through the file system.
  add_executable(zamn_test_config tools/test_config.c)
  target_link_libraries(zamn_test_config PRIVATE SDL2::SDL2 SDL2::SDL2main)
  target_include_directories(zamn_test_config PRIVATE src)
  if(MSVC)
    target_compile_definitions(zamn_test_config PRIVATE _CRT_SECURE_NO_WARNINGS)
  endif()

  # The launcher (src/launcher.c): zamn.ini with a face on it, and a Play
  # button. It ships beside zamn.exe and starts it. SDL as the game is, and
  # `src/config.h` for the reading and the writing; stb_truetype draws the
  # system's font.
  add_executable(zamn_launcher WIN32 src/launcher.c)
  target_link_libraries(zamn_launcher PRIVATE SDL2::SDL2 SDL2::SDL2main)
  target_include_directories(zamn_launcher PRIVATE src third_party/stb)
  if(WIN32)
    # The file dialog behind Browse.
    target_link_libraries(zamn_launcher PRIVATE comdlg32)
  endif()
  if(MSVC)
    target_compile_definitions(zamn_launcher PRIVATE _CRT_SECURE_NO_WARNINGS)
  endif()
  # Its icon is the title screen and its heading the title's logo, made from
  # the cartridge at build time, since nothing of the cartridge's is kept in
  # the repository. Without one the launcher builds without an icon, and its
  # heading is text.
  set(ZAMN_ICON_ROM "${CMAKE_CURRENT_SOURCE_DIR}/Zombies Ate My Neighbors.sfc"
      CACHE FILEPATH "The cartridge the launcher's icon and heading are drawn from")
  if(EXISTS "${ZAMN_ICON_ROM}")
    set(ZAMN_LOGO "${CMAKE_CURRENT_BINARY_DIR}/zamn_logo.h")
    add_custom_command(OUTPUT "${ZAMN_LOGO}"
      COMMAND zamn_logo "${ZAMN_ICON_ROM}" "${ZAMN_LOGO}"
      DEPENDS zamn_logo "${ZAMN_ICON_ROM}"
      COMMENT "Drawing the launcher's heading from the title's logo"
      VERBATIM)
    target_sources(zamn_launcher PRIVATE "${ZAMN_LOGO}")
    target_include_directories(zamn_launcher PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
    target_compile_definitions(zamn_launcher PRIVATE ZAMN_LOGO)
  endif()
  if(WIN32 AND EXISTS "${ZAMN_ICON_ROM}")
    set(ZAMN_ICON "${CMAKE_CURRENT_BINARY_DIR}/zamn_launcher.ico")
    add_custom_command(OUTPUT "${ZAMN_ICON}"
      COMMAND zamn_icon "${ZAMN_ICON_ROM}" "${ZAMN_ICON}"
      DEPENDS zamn_icon "${ZAMN_ICON_ROM}"
      COMMENT "Drawing the launcher's icon from the title screen"
      VERBATIM)
    file(CONFIGURE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/zamn_launcher.rc"
         CONTENT "1 ICON \"${ZAMN_ICON}\"\n")
    target_sources(zamn_launcher PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/zamn_launcher.rc")
    set_source_files_properties("${CMAKE_CURRENT_BINARY_DIR}/zamn_launcher.rc"
      PROPERTIES OBJECT_DEPENDS "${ZAMN_ICON}")
  elseif(WIN32)
    message(STATUS "No cartridge at ${ZAMN_ICON_ROM}: the launcher is built without its icon")
  endif()

  # The game and the launcher go in the repository root, beside the ROM,
  # zamn.ini and the save files. The generator expression keeps a
  # multi-config generator from adding a Release/ or Debug/ of its own.
  set_target_properties(zamn zamn_launcher PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "$<1:${CMAKE_CURRENT_SOURCE_DIR}>")
  # Play starts the zamn.exe beside the launcher, and building it copies SDL2.
  add_dependencies(zamn_launcher zamn)

  # SDL2 builds as a DLL; place it next to the game and the launcher, and next
  # to the tests left in the build directory, so all of them run.
  if(TARGET SDL2::SDL2)
    add_custom_command(TARGET zamn POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
              $<TARGET_FILE:SDL2::SDL2> $<TARGET_FILE_DIR:zamn>
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
              $<TARGET_FILE:SDL2::SDL2> "${CMAKE_CURRENT_BINARY_DIR}")
  endif()

  # `cmake --build build --target zamn_release` zips what a player needs: the
  # launcher, the game, SDL2.dll, and the licences of the code inside them
  # (LakeSnes is MIT, which asks for its notice in every copy; SDL2 is zlib,
  # which only appreciates one). Never the ROM. Not part of the default build.
  if(TARGET SDL2::SDL2)
    if(sdl2_SOURCE_DIR)
      set(ZAMN_SDL_LICENSE_DEFAULT "${sdl2_SOURCE_DIR}/LICENSE.txt")
    else()
      set(ZAMN_SDL_LICENSE_DEFAULT "")
    endif()
    set(ZAMN_SDL_LICENSE "${ZAMN_SDL_LICENSE_DEFAULT}" CACHE FILEPATH
        "SDL2's LICENSE.txt, for the release zip (found by itself when SDL2 is fetched)")
    set(ZAMN_RELEASE_DIR "${CMAKE_CURRENT_BINARY_DIR}/release")
    set(ZAMN_RELEASE_ZIP "${CMAKE_CURRENT_BINARY_DIR}/zamn.zip")
    if(ZAMN_SDL_LICENSE AND EXISTS "${ZAMN_SDL_LICENSE}")
      # The zip takes only the files it names, so a stale file here is harmless.
      # The folder is made on every run, and the zip made from inside it only
      # after that, so a deleted build/release comes back instead of failing.
      add_custom_target(zamn_release
        COMMAND ${CMAKE_COMMAND} -E make_directory "${ZAMN_RELEASE_DIR}"
        COMMAND ${CMAKE_COMMAND} -E copy
                $<TARGET_FILE:zamn_launcher> $<TARGET_FILE:zamn> $<TARGET_FILE:SDL2::SDL2>
                "${ZAMN_RELEASE_DIR}"
        COMMAND ${CMAKE_COMMAND} -E copy
                "${CMAKE_CURRENT_SOURCE_DIR}/third_party/lakesnes/LICENSE.txt"
                "${ZAMN_RELEASE_DIR}/LakeSnes-LICENSE.txt"
        COMMAND ${CMAKE_COMMAND} -E copy "${ZAMN_SDL_LICENSE}"
                "${ZAMN_RELEASE_DIR}/SDL2-LICENSE.txt"
        COMMAND ${CMAKE_COMMAND} -E rm -f "${ZAMN_RELEASE_ZIP}"
        COMMAND ${CMAKE_COMMAND} -E chdir "${ZAMN_RELEASE_DIR}"
                ${CMAKE_COMMAND} -E tar cf "${ZAMN_RELEASE_ZIP}" --format=zip
                zamn_launcher.exe zamn.exe $<TARGET_FILE_NAME:SDL2::SDL2>
                LakeSnes-LICENSE.txt SDL2-LICENSE.txt
        DEPENDS zamn zamn_launcher
        COMMENT "Zipping the release into ${ZAMN_RELEASE_ZIP}"
        VERBATIM)
    else()
      message(STATUS "No SDL2 LICENSE.txt known (set ZAMN_SDL_LICENSE): no 'zamn_release' target")
    endif()
  endif()
  message(STATUS "Interactive 'zamn' target enabled")
endif()
