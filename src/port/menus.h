// The two screens before a game: the title's menu and the players' screen,
// a frame at a time.
//
// Each is a loop in the main thread that sleeps a tick a pass, and gives up
// after 900 passes with nobody pressing anything. These are one pass of each
// as readable C, from where `thread_yield` returns to the next yield:
//
//   $80:9748  title_menu_frame      the menu under the title
//   $80:99C1  players_screen_frame  where each player presses Start to join
//
// What is not here is a pass on which a press counts: moving the menu,
// choosing from it, joining, and changing a player's character all make a
// sound, and those passes are the ROM's. Nor the screens' setup, nor what
// follows when a loop ends.
//
// ## The title's menu
//
// Two choices, each a display record, and the chosen one blinks: every tenth
// pass both records' flags are set again from a table, with `ACTOR_ATTR_SET`
// on the chosen one every other time.
//
// ## The players' screen
//
// Two pictures, one a player. Every tenth pass the picture of each player
// who has not joined is hidden or shown, so it blinks until they do. The
// count of passes for that is not on the thread's page: it is the word at
// `$7E:005E`.
//
// ## A press
//
// A pad is read as it is held, from `$006E` and `$0070`. Its buttons count
// when none of them was down the last time the pad was taken, and what is
// down is then remembered, nothing included. So a held button counts once.
// The second pad is looked at only when the first had nothing down.
//
// ## Their contract with the ROM
//
// WRAM exactly as the ROM writes it. A pass ends at the `JSL thread_yield`
// with the tick count in A, or past the test of the pass count with that in
// A and the test's flags. Carry and overflow at the yield are the ROM's:
// the overflow is bit 14 of a pad's remembered buttons, by `BIT`.
//
// Port code: libc only.

#ifndef PORT_MENUS_H
#define PORT_MENUS_H

#include <stdbool.h>
#include <stdint.h>

#include "assets/rom.h"
#include "port/wram.h"

#define MENUS_BANK 0x80u
#define MENUS_YIELD_TICKS 1
// Passes with no press before either screen gives up: fifteen seconds.
#define MENUS_PATIENCE 0x0384

#define TITLE_MENU_FRAME_PC 0x809748u
#define TITLE_MENU_YIELD_PC 0x809744u  // `JSL thread_yield`, A already 1
#define TITLE_MENU_OVER_PC 0x809754u   // past the `BCS`, the count in A

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

// The players' screen counts passes between blinks here, off the page.
#define W_PLAYERS_SCREEN_BLINK 0x005eu

// What a pad did on a pass.
typedef enum {
  MENUS_PAD_SKIPPED,  // not looked at
  MENUS_PAD_HELD,     // a button that was down before: not taken
  MENUS_PAD_IDLE,     // taken, with nothing down
  MENUS_PAD_PRESSED,  // taken, with a button down: the pass is the ROM's
} MenusPad;

// For the harness: the path, which is what the pass costs.
typedef struct {
  bool over;       // the count ran out
  bool blinked;    // the tenth pass
  bool joined[2];  // the players' screen: who has joined
  MenusPad pad[2];
  bool declined;   // a press that counts
  uint16_t a;
  bool n, z, c, v;
  bool v_set;      // a pad was looked at, so the overflow is `BIT`'s
} MenusLog;

// Can the frames take this pass? The records have to be records, and the
// title menu's blink and choice within its table.
bool title_menu_frame_supported(const Wram* w, uint16_t page);
bool players_screen_frame_supported(const Wram* w, uint16_t page);

// `$80:9748`. True while the menu goes on. `log` may be NULL, and
// `log->declined` says the pass is the ROM's.
bool title_menu_frame(Wram* w, const Rom* rom, uint16_t page, MenusLog* log);

// `$80:99C1`. The same.
bool players_screen_frame(Wram* w, uint16_t page, MenusLog* log);

#endif
