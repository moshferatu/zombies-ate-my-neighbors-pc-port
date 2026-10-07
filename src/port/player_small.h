// Small things a player's changes of state are made of.
//
// A player who is hit, drowns, falls, turns monster or picks something up
// goes through a routine of its own, and most of those are the ROM's. They
// share nine leaves, each a `JSR` away, and these are the nine:
//
//   $80:F366  Nothing can touch the player: the collide id of their record
//             is cleared.
//   $80:F377  ...and put back: `$05` for the first player, `$06` for the
//             second.
//   $80:F36C  Nobody is told about the player: their thread has no handler,
//             and the word at `$50` is cleared.
//   $80:F382  ...and the handler put back, `player_collide`.
//   $80:F38D  Something else in the player's hands. The weapon and the item
//             they had are kept aside, and A and Y take their place.
//   $80:F3A5  ...and what was kept is back in their hands.
//   $80:F3B4  How far the point at `$60` and `$62` is from the player each
//             way, and which way: the distances at `$3C` and `$3E`, a one or
//             a minus one for each at `$40` and `$42`, and `$44` and `$46`
//             cleared.
//   $80:F3E3  The second of the walk's reactions (`port/walk.h`). It looks
//             through a list of tiles round the player, one list for each
//             way faced, for one with bit 3 of its attributes. The first
//             found, its middle goes to `$60` and `$62`, the leaf above is
//             called, and carry is set. A player in state `$06` is not
//             looked for: carry clear.
//
//   $80:ECFD  The weapon in the player's hand is not drawn, but in state
//             `$0C`. `port/pose.c` has this too, for the poses it has.
//
// What a tile with bit 3 is to the player I have not seen. The walk goes on
// from a set carry to `$80:DFAC`, which is the ROM's.
//
// And one wait. A player who has fired a shot that takes both hands stands
// for eight ticks, or until the way the pad is held changes: `$80:EECC`. A
// tick of that is here, from where the sleep comes back at `$80:EED3` to the
// next sleep or the routine's `RTS`.
//
// Port code: libc only.

#ifndef PORT_PLAYER_SMALL_H
#define PORT_PLAYER_SMALL_H

#include <stdbool.h>
#include <stdint.h>

#include "assets/rom.h"
#include "port/cpu.h"
#include "port/wram.h"

#define PLAYER_UNTOUCHABLE_PC 0x80f366u
#define PLAYER_UNTOUCHABLE_RTS_PC 0x80f36bu
#define PLAYER_UNHANDLED_PC 0x80f36cu
#define PLAYER_UNHANDLED_RTS_PC 0x80f376u
#define PLAYER_TOUCHABLE_PC 0x80f377u
#define PLAYER_TOUCHABLE_RTS_PC 0x80f381u
#define PLAYER_HANDLED_PC 0x80f382u
#define PLAYER_HANDLED_RTS_PC 0x80f38cu
#define PLAYER_HANDS_SWAP_PC 0x80f38du
#define PLAYER_HANDS_SWAP_RTS_PC 0x80f3a4u
#define PLAYER_HANDS_BACK_PC 0x80f3a5u
#define PLAYER_HANDS_BACK_RTS_PC 0x80f3b3u
#define PLAYER_SPAN_PC 0x80f3b4u
#define PLAYER_SPAN_RTS_PC 0x80f3e2u
#define TILE_SEARCH_PC 0x80f3e3u
#define TILE_SEARCH_RESTING_RTS_PC 0x80f3ebu
#define TILE_SEARCH_NONE_RTS_PC 0x80f42cu
#define TILE_SEARCH_FOUND_RTS_PC 0x80f451u

#define WEAPON_AWAY_PC 0x80ecfdu
#define WEAPON_AWAY_RTS_PC 0x80ed0fu
#define FIRED_WAIT_PC 0x80eed3u
#define FIRED_WAIT_YIELD_PC 0x80eecfu  // `JSL thread_yield`, A already 1
#define FIRED_WAIT_RTS_PC 0x80eee0u

#define WEAPON_AWAY_DP_WEAPON 0x0a   // the record of the weapon in hand
#define WEAPON_AWAY_KEEPS 0x000c     // the state that keeps it drawn
#define FIRED_WAIT_DP_LEFT 0x2e      // ticks left
#define FIRED_WAIT_DP_HELD 0x6e      // the way held when the shot went
#define W_FIRED_WAIT_HELD 0x0072     // ...and now, by the player

#define PLAYER_SMALL_BANK 0x80
#define PLAYER_SMALL_DP_RECORD 0x08
#define PLAYER_SMALL_DP_NUMBER 0x0c   // the player's number, doubled
#define PLAYER_SMALL_DP_PLAYER 0x0e   // ...and again: these index by it
#define PLAYER_SMALL_DP_UNTOLD 0x50
#define PLAYER_SMALL_IDS 0x80fe64u
#define PLAYER_SMALL_HANDLER 0xf7f7u
#define W_PLAYER_WEAPON_KEPT 0x1cc4
#define W_PLAYER_ITEM_KEPT 0x1cc8

#define PLAYER_SPAN_DP_X 0x30       // where the player is
#define PLAYER_SPAN_DP_Y 0x32
#define PLAYER_SPAN_DP_POINT_X 0x60
#define PLAYER_SPAN_DP_POINT_Y 0x62
#define PLAYER_SPAN_DP_FAR_X 0x3c
#define PLAYER_SPAN_DP_FAR_Y 0x3e
#define PLAYER_SPAN_DP_WAY_X 0x40
#define PLAYER_SPAN_DP_WAY_Y 0x42
#define PLAYER_SPAN_DP_CLEAR_A 0x44
#define PLAYER_SPAN_DP_CLEAR_B 0x46

#define TILE_SEARCH_LISTS 0x80f452u  // by the way faced: a list's address
#define TILE_SEARCH_DP_FACING 0x26
#define TILE_SEARCH_DP_AT 0x2c       // scratch: where in the list
#define TILE_SEARCH_DP_LEFT 0x2e     // ...and how many are left
#define TILE_SEARCH_DP_STATE 0x70
#define TILE_SEARCH_RESTING 0x0006   // the state that is not looked for
#define TILE_SEARCH_DP_COL 0x78      // the player's tile
#define TILE_SEARCH_DP_ROW 0x7a
#define TILE_SEARCH_ABOVE 0x000c     // pixels above the player's place
#define TILE_SEARCH_FACING_MAX 0x10
#define TILE_SEARCH_MOST 64          // tiles in a list, for the guard

typedef struct {
  bool back_x, back_y;  // the point is behind the player that way
} PlayerSpan;

typedef struct {
  bool resting;
  int looked;      // tiles looked at
  bool found;
  PlayerSpan span;
} TileSearch;

void player_untouchable(Wram* w, PortCpu* c);
void player_unhandled(Wram* w, PortCpu* c);
void player_touchable(Wram* w, const Rom* rom, PortCpu* c);
void player_handled(Wram* w, PortCpu* c);
void player_hands_swap(Wram* w, PortCpu* c);
void player_hands_back(Wram* w, PortCpu* c);
void player_span(Wram* w, PortCpu* c, PlayerSpan* did);

// True if it was put away.
bool weapon_away(Wram* w, PortCpu* c);

typedef enum {
  FIRED_WAIT_MOVED,  // the pad is held another way: over
  FIRED_WAIT_DONE,   // the eighth tick
  FIRED_WAIT_ON,     // another tick
} FiredWait;

FiredWait fired_wait(Wram* w, PortCpu* c);

// False for a list the table does not have, or one of no tiles.
bool tile_search_supported(const Wram* w, const Rom* rom, uint16_t page);
void tile_search(Wram* w, const Rom* rom, PortCpu* c, TileSearch* did);

#endif
