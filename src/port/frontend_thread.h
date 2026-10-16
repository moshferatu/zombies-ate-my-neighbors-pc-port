// The front end's thread, between its calls.
//
// `$80:9126` is everything outside a level: the screen before the title, the
// title, its menu, the players' portraits and the demonstration. It is one
// thread, and most of it is calls to routines that are ported on their own.
// What is left between them is here, each a stretch from where one call
// comes back to the next call, wait or return.
//
//   $80:9136  first_pass        has the screen before the title been shown?
//   $80:9167  title_left        the title's menu over: on, or the title again
//   $80:9176  players_left      the players' screen over: a game, or the
//                               demonstration
//   $80:9193  region_check      ...and is this the console it was made for
//   $80:9BEF  scroll_job_ask    the job that sends the layers' scroll, asked
//   $80:9BF9  scroll_job_asked  ...and where it is in the queue, kept
//   $80:9C4A  scroll_job_stop   ...and that job taken out of it
//   $80:9C52  fade_in_ask       a screen's brightness, counted up from black
//   $80:9C72  fade_out_ask      ...and down to it
//   $80:9242  show_sleep        the screen before the title's 128 frames
//   $80:94BE  title_begin       the title's backdrop and the wave on it
//   $80:94E9  title_song_ask    its song
//   $80:94FC  title_first_pass  has the wave come down the title yet?
//   $80:9F9D  vram_clear_ask    VRAM cleared a kilobyte a frame, asked
//
// The ones that write to the picture hardware are in `port/dma.h`, the two
// of the screen before the title's second wait in `port/frontend.h`, and the
// single calls and returns between them in `port/links.h`.
//
// ## The console
//
// `$80:918E` reads `$213F`, whose bit 4 says the console draws fifty frames
// a second. With it set the ROM shows a screen of its own and stops for
// good. `region_check` begins after the read, with the byte in A, and is the
// other way only: the bit clear, and back to the thread.
//
// `region_read` is the read as well. The port's console draws sixty frames
// a second, as the one the cartridge was made for does: its header names the
// United States. So bit 4 is clear. The rest of the byte is the picture
// chip's version and the state of its counters' latch, and the `AND` after
// the read keeps none of it. The read also resets that latch, which matters
// only to a read of the counters, and the ROM makes none.
//
// ## The first pass
//
// Two words on the thread's page are zero until something has been shown
// once. `$7C` is the screen before the title, which is shown the first time
// the front end runs and never again. `$78` is the wave that comes down the
// title, which the title waits for the first time and not when it is come
// back to. `frontend_reset` zeroes `$78` and leaves `$7C`.
//
// ## The first pass, skipped
//
// `--skip-intro` boots to the title by making every pass a later one. In
// the cartridge that is the `BNE` after the test of `$7C` made a `BRA`
// (`src/skipintro.h`), and here it is `frontend_opening_skip`, which
// `intro_bypass` calls when it has made the other. The word is then never
// marked, as it is not by the ROM's instructions with the branch changed.
//
// ## Their contract with the ROM
//
// WRAM as the ROM writes it, the stack with it, and every register and flag
// at the instruction a stretch stops on.
//
// Port code: libc only.

#ifndef PORT_FRONTEND_THREAD_H
#define PORT_FRONTEND_THREAD_H

#include <stdbool.h>
#include <stdint.h>

#include "port/cpu.h"
#include "port/wram.h"

#define FRONTEND_FIRST_PASS_PC 0x809136u
#define FRONTEND_OPENING_PC 0x80913cu       // `JSL $80:918E`
#define FRONTEND_TITLE_PC 0x809152u         // `JSL layers_reset`
#define FRONTEND_DP_OPENING_SHOWN 0x7c

#define FRONTEND_REGION_READ_PC 0x80918eu
#define FRONTEND_REGION_CHECK_PC 0x809193u
#define FRONTEND_REGION_RTL_PC 0x8091f6u
#define REGION_FIFTY_HERTZ 0x0010u          // of `$213F`

#define TITLE_FIRST_PASS_PC 0x8094fcu
#define TITLE_WAVE_BEGIN_PC 0x809502u       // `title_hdma_on`, in `port/dma.h`
#define TITLE_WAVE_OVER_PC 0x809529u        // `title_hdma_off`
#define TITLE_DP_WAVE_SHOWN 0x78

// The two screens before a game come back with carry: see `port/menus.h`.
#define FRONTEND_TITLE_LEFT_PC 0x809167u    // `BCC $915A`
#define FRONTEND_TITLE_AGAIN_PC 0x80915au
#define FRONTEND_PORTRAITS_PC 0x809169u     // `JSL $80:AC7E`
#define FRONTEND_PLAYERS_LEFT_PC 0x809176u  // `BCS $9179`
#define FRONTEND_GAME_PC 0x809178u          // `RTL`
#define FRONTEND_DEMO_PC 0x809179u          // `JSR $9AB0`

// True with carry set: on to the players' screen.
bool frontend_title_left(PortCpu* c);
// True with carry clear: somebody joined, and the thread returns to a game.
bool frontend_players_left(PortCpu* c);

// True the first time, when the screen before the title is to be shown.
bool frontend_first_pass(Wram* w, PortCpu* c);
// Whether `frontend_first_pass` takes every pass for a later one.
void frontend_opening_skip(bool skip);
// True the first time, when the wave is to come down.
bool title_first_pass(Wram* w, PortCpu* c);

// The bit clear in the byte in A.
bool frontend_region_check_supported(uint16_t a);
void frontend_region_check(PortCpu* c);
// ...or asked for first, of a console that draws sixty frames a second.
void frontend_region_read(PortCpu* c);

#define FRONTEND_DP_SCROLL_JOB 0x60  // where the scroll's job is in the queue
#define FRONTEND_SCROLL_JOB 0x9bfcu  // jobs in bank `$80`
#define BRIGHTNESS_UP_JOB 0x9c63u
#define BRIGHTNESS_DOWN_JOB 0x9c7du
#define VRAM_CLEAR_JOB 0x9f62u
#define FRONTEND_JOB_BANK 0x0080u
#define SHOW_SLEEP_FRAMES 0x0080u

// The title's backdrop is the third layer, set down here, and over it a wave
// that `$80:9570` makes a table for each frame. The words on the thread's
// page are the wave's. I have not read `$80:9570` closely: `$70` is how far
// down its table it writes, and falls by two a frame once `$76` has counted
// down to nothing.
#define TITLE_BACKDROP_X 0x002bu
#define TITLE_BACKDROP_Y 0x0050u
#define W_TITLE_0072 0x0072u         // zeroed. Not the thread's `$72`.
#define TITLE_DP_WAVE_LINES 0x70
#define TITLE_DP_WAVE_HOLD 0x76
#define TITLE_DP_74 0x74
#define TITLE_WAVE_LINES 0x01c0u
#define TITLE_WAVE_HOLD 0x0004u
#define BACKDROP_DRIFT_JOB 0x953bu
#define TITLE_DP_DRIFT_JOB 0x7a      // where the drift's job is in the queue
#define TITLE_SONG 0x0003u
#define TITLE_SOUNDS 0x0003u

// The rest: a name, where it starts and the instruction it stops on.
#define FRONTEND_STRETCHES(X) \
  X(scroll_job_ask, "$80:9BEF", 0x809befu, 0x809bf5u) \
  X(scroll_job_asked, "$80:9BF9", 0x809bf9u, 0x809bfbu) \
  X(scroll_job_stop, "$80:9C4A", 0x809c4au, 0x809c4cu) \
  X(fade_in_ask, "$80:9C52", 0x809c52u, 0x809c5eu) \
  X(fade_out_ask, "$80:9C72", 0x809c72u, 0x809c78u) \
  X(show_sleep, "$80:9242", 0x809242u, 0x809245u) \
  X(title_begin, "$80:94BE", 0x8094beu, 0x8094e5u) \
  X(title_song_ask, "$80:94E9", 0x8094e9u, 0x8094f1u) \
  X(vram_clear_ask, "$80:9F9D", 0x809f9du, 0x809fa6u)

#define X(name, sym, entry, exit) void frontend_##name(Wram* w, PortCpu* c);
FRONTEND_STRETCHES(X)
#undef X

#endif
