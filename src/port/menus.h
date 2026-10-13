// The two screens before a game: the title's menu and the players' screen.
//
// Each is a loop in the front end's thread that sleeps a tick a pass, and
// gives up after 900 passes with nobody pressing anything. A pass is from
// where `thread_yield` returns to the next thing the thread calls:
//
//   $80:9748  title_menu_frame      the menu under the title
//   $80:99C1  players_screen_frame  where each player presses Start to join
//
// ...and the first pass of each, which is not come to from a yield:
//
//   $80:96C7  title_menu_pass
//   $80:98FC  players_screen_first_pass
//
// The rest of the two routines is in `MENUS_STRETCHES` below, each from
// where a call comes back to the next call or return, and in the two that
// leave by more than one way, `title_menu_result` and `players_screen_over`.
// The two that write to the picture hardware are in `port/dma.h`.
//
// ## The title's menu
//
// Two choices, each a display record, and the chosen one blinks: every tenth
// pass both records' flags are set again from a table, with `ACTOR_ATTR_SET`
// on the chosen one every other time.
//
// A button that confirms takes the choice, and the count of passes is set to
// its end so the loop is left on the next. Anything else moves to the other
// choice. What is taken is the game, or a password, and for a password the
// routine remembers which pad confirmed: that pad enters it.
//
// ## The players' screen
//
// Two pictures, one a player. Every tenth pass the picture of each player
// who has not joined is hidden or shown, so it blinks until they do. The
// count of passes for that is not on the thread's page: it is the word at
// `$7E:005E`.
//
// Start alone joins, and leaves the loop two seconds to run. Anything else
// does nothing once somebody has joined, and before that it has the two
// characters change places.
//
// ## A press
//
// A pad is read as it is held, from `$006E` and `$0070`. Its buttons count
// when none of them was down the last time the pad was taken, and what is
// down is then remembered, nothing included. So a held button counts once.
// The second pad is looked at only when the first did nothing.
//
// ## Their contract with the ROM
//
// WRAM exactly as the ROM writes it, and the stack with it. A pass ends on a
// call: `thread_yield` with the tick count in A, `apu_play_sfx` with the
// sound, or `$80:9A31` with the two characters in A and X. Or it ends past
// the test of the pass count, with that in A and the test's flags. Carry and
// overflow are the ROM's: the overflow is bit 14 of a pad's remembered
// buttons, by `BIT`. The stretches leave every register and flag as the ROM
// does.
//
// Port code: libc only.

#ifndef PORT_MENUS_H
#define PORT_MENUS_H

#include <stdbool.h>
#include <stdint.h>

#include "assets/rom.h"
#include "port/cpu.h"
#include "port/wram.h"

#define MENUS_BANK 0x80u
#define MENUS_YIELD_TICKS 1
// Passes with no press before either screen gives up: fifteen seconds.
#define MENUS_PATIENCE 0x0384
// What a join leaves of them: two seconds.
#define MENUS_PATIENCE_JOINED 0x030c
#define MENUS_SOUND_MOVE 0x000eu
#define MENUS_SOUND_CONFIRM 0x0027u
// B, Y, Start, A and X.
#define MENUS_CONFIRM_BUTTONS 0xd0c0u
#define MENUS_START_BUTTON 0x1000u

#define TITLE_MENU_PASS_PC 0x8096c7u
#define TITLE_MENU_MOVE_PC 0x80972du    // `JSL apu_play_sfx`, the sound in A
#define TITLE_MENU_CONFIRM_PC 0x809738u // the same
#define TITLE_MENU_FRAME_PC 0x809748u
#define TITLE_MENU_YIELD_PC 0x809744u   // `JSL thread_yield`, A already 1
#define TITLE_MENU_OVER_PC 0x809754u    // past the `BCS`, the count in A

#define PLAYERS_SCREEN_FIRST_PASS_PC 0x8098fcu
#define PLAYERS_SCREEN_JOIN_PC 0x80995du    // `JSL apu_play_sfx`
#define PLAYERS_SCREEN_JOIN_2_PC 0x809994u  // ...for the second player
#define PLAYERS_SCREEN_SWAP_PC 0x8099b0u    // `JSR $9A31`
#define PLAYERS_SCREEN_FRAME_PC 0x8099c1u
#define PLAYERS_SCREEN_YIELD_PC 0x8099bdu
#define PLAYERS_SCREEN_OVER_PC 0x8099cdu

// Fields on the thread's page, which the two screens share.
#define MENUS_DP_FIRST 0x50   // the first choice's record, or the first player's
#define MENUS_DP_SECOND 0x52
#define MENUS_DP_PASSES 0x5e  // passes since the last press that counted
#define MENUS_DP_PAD_A 0x62   // what was down when each pad was last taken
#define MENUS_DP_PAD_B 0x64
// ...and the title menu's own.
#define TITLE_MENU_DP_BLINK_WAIT 0x66  // passes until the next blink
#define TITLE_MENU_DP_BLINK 0x68       // 0 or 1: which half of a blink
#define TITLE_MENU_DP_CHOICE 0x6a      // 0 or 4
#define TITLE_MENU_DP_DRIFT_JOB 0x7a   // where the backdrop's job is in the queue
#define TITLE_MENU_CHOICE_PASSWORD 0x0004u
// ...and the players' screen's.
#define PLAYERS_SCREEN_DP_SCROLL_JOB 0x6c  // where its scroll's job is

// The players' screen counts passes between blinks here, off the page.
#define W_PLAYERS_SCREEN_BLINK 0x005eu
// Which character each player is.
#define W_PLAYERS_CHARACTERS 0x1e84u

// What a pad did on a pass.
typedef enum {
  MENUS_PAD_SKIPPED,  // not looked at
  MENUS_PAD_HELD,     // a button that was down before: not taken
  MENUS_PAD_IDLE,     // taken, with nothing down
  MENUS_PAD_PRESSED,  // taken, with a button down
} MenusPad;

// ...and what a press came to.
typedef enum {
  MENUS_PRESS_NONE,
  MENUS_PRESS_MOVE,     // the title: to the other choice
  MENUS_PRESS_CONFIRM,  // the title: this one
  MENUS_PRESS_JOIN,     // the players' screen: Start
  MENUS_PRESS_IGNORED,  // ...anything else, with somebody joined
  MENUS_PRESS_SWAP,     // ...and with nobody: the characters change places
} MenusPress;

// For the harness: the path, which is what the pass costs, and what the
// registers hold where it stops.
typedef struct {
  bool over;       // the count ran out
  bool blinked;    // the tenth pass
  bool joined[2];  // the players' screen: who had joined
  MenusPad pad[2];
  MenusPress press;
  int presser;     // whose pad it was
  uint32_t pc;     // the instruction the pass stops on
  uint16_t a;
  uint16_t x;      // a swap only
  bool n, z, c, v;
  bool v_set;      // a pad was looked at, so the overflow is `BIT`'s
} MenusLog;

// Can the passes take this one? The records have to be records, and the
// title menu's blink and choice within its table.
bool title_menu_frame_supported(const Wram* w, uint16_t page);
bool players_screen_frame_supported(const Wram* w, uint16_t page);

// `$80:9748`. True while the menu goes on. `log` may be NULL.
bool title_menu_frame(Wram* w, const Rom* rom, uint16_t page, MenusLog* log);
// `$80:96C7`: a pass without the count.
void title_menu_pass(Wram* w, const Rom* rom, uint16_t page, MenusLog* log);

// `$80:99C1`. The same.
bool players_screen_frame(Wram* w, uint16_t page, MenusLog* log);
// `$80:98FC`. `job` is where the scroll's job went in the queue, from X.
void players_screen_first_pass(Wram* w, uint16_t page, uint16_t job,
                               MenusLog* log);

// How the title's menu ended, `$80:9771`, with the flags of the count's last
// test on the stack.
#define TITLE_MENU_RESULT_PC 0x809771u
#define TITLE_MENU_PASSWORD_PC 0x809789u  // `JSL $80:AC7E`, the pad pushed
#define TITLE_MENU_GAME_PC 0x80979eu      // `JSL apu_fixed_2`

typedef enum {
  TITLE_MENU_TIMED_OUT,  // nobody chose
  TITLE_MENU_GAME,
  TITLE_MENU_PASSWORD,
} TitleMenuResult;

// `*pad` is 0 or 2, the pad that enters the password, when there is one.
TitleMenuResult title_menu_result(Wram* w, PortCpu* c, uint16_t* pad);

// The players' screen, with the count run out: `$80:99CD`. True when somebody
// joined. With nobody, both pictures are shown for a last frame.
#define PLAYERS_SCREEN_LEAVE_PC 0x8099d5u  // `JSR $9C72`
#define PLAYERS_SCREEN_IDLE_PC 0x809a09u   // `JSL thread_yield`
bool players_screen_over(Wram* w, PortCpu* c);

#define PORTRAIT_SCROLL_JOB 0x9a1bu  // jobs in bank `$80`
#define PORTRAITS_SEND_JOB 0x9a90u
#define MENUS_JOB_BANK 0x0080u
#define MENUS_FADE_FRAMES 0x0010u

// The rest: a name, where it starts and the instruction it stops on.
#define MENUS_STRETCHES(X) \
  X(title_menu_begin, "$80:96B9", 0x8096b9u, 0x8096c1u) \
  X(title_menu_moved, "$80:9731", 0x809731u, 0x809744u) \
  X(title_menu_confirmed, "$80:973C", 0x80973cu, 0x809744u) \
  X(title_menu_over, "$80:9754", 0x809754u, 0x809755u) \
  X(title_menu_fade_sleep, "$80:9758", 0x809758u, 0x80975bu) \
  X(title_menu_drift_stop, "$80:975F", 0x80975fu, 0x809761u) \
  X(title_menu_free_first, "$80:9765", 0x809765u, 0x809767u) \
  X(title_menu_free_second, "$80:976B", 0x80976bu, 0x80976du) \
  X(title_menu_password_pad, "$80:9791", 0x809791u, 0x809792u) \
  X(title_menu_go, "$80:97A2", 0x8097a2u, 0x8097a3u) \
  X(players_screen_begin, "$80:98EA", 0x8098eau, 0x8098f8u) \
  X(players_screen_joined, "$80:9961", 0x809961u, 0x8099bdu) \
  X(players_screen_joined_2, "$80:9998", 0x809998u, 0x8099bdu) \
  X(players_screen_swapped, "$80:99B3", 0x8099b3u, 0x8099b6u) \
  X(players_screen_sleep, "$80:99BA", 0x8099bau, 0x8099bdu) \
  X(players_screen_fade_sleep, "$80:99D8", 0x8099d8u, 0x8099dbu) \
  X(players_screen_scroll_stop, "$80:99DF", 0x8099dfu, 0x8099e1u) \
  X(players_screen_free_first, "$80:99E8", 0x8099e8u, 0x8099eau) \
  X(players_screen_free_second, "$80:99EE", 0x8099eeu, 0x8099f0u) \
  X(players_screen_go, "$80:99F4", 0x8099f4u, 0x8099f5u) \
  X(players_screen_idle_free_first, "$80:9A0D", 0x809a0du, 0x809a0fu) \
  X(players_screen_idle_free_second, "$80:9A13", 0x809a13u, 0x809a15u) \
  X(players_screen_idle, "$80:9A19", 0x809a19u, 0x809a1au) \
  X(portraits_first, "$80:9A31", 0x809a31u, 0x809a39u) \
  X(portraits_second, "$80:9A3C", 0x809a3cu, 0x809a44u) \
  X(portraits_send_ask, "$80:9A47", 0x809a47u, 0x809a4du) \
  X(portraits_sent, "$80:9AA1", 0x809aa1u, 0x809aa3u)

#define X(name, sym, entry, exit) void menus_##name(Wram* w, PortCpu* c);
MENUS_STRETCHES(X)
#undef X

#endif
