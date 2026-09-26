// The player's walk: where a player ends up this frame on ordinary ground.
//
// `$80:E4BA` is the movement handler the player's frame calls through `$2A`
// (see `port/bodies.h`, "player_body"). This is that routine as readable C,
// the first piece of the port written that way rather than transliterated.
// It is a function of the player's page and the board, and it calls the
// other movement code as C functions, not through the ROM.
//
// ## What the walk does
//
// The pad has already set a direction and a speed. The walk asks where that
// takes the player (`step_propose`, `port/step.h`), and then tries the move
// one axis at a time: across first, then up or down. Trying the axes apart is
// what lets a player slide along a wall instead of stopping dead against it.
//
// Each axis is put to four questions, in this order, and the first that
// refuses stops that axis for this frame:
//
//   1. Is the ground there solid?            `terrain_blocked`
//   2. Is it too far from the other player?  `step_tether_blocked`
//   3. Is someone standing there?            `actor_obstacle_at_point`
//   4. Is it off the map?                    `terrain_out_of_bounds`
//
// Two of them have a second part.
//
// * **Solid ground can have a reaction of its own.** The tile's attribute
//   word names one of six (`walk_tile_reaction`). Those belong to other parts
//   of the game, such as doors, and this file does not handle them. Ground
//   without one is only solid, and that is 98% of the solid tiles in play.
// * **Someone standing there only blocks a player who is standing clear.**
//   If someone is also standing where the player is now, the step goes ahead.
//   Two actors that already overlap can walk apart, and nothing holds a player
//   in place.
//
// ## Its contract with the ROM
//
// The port still runs inside the emulator, so what the walk hands back must
// be what the ROM's routine would have left. It writes WRAM exactly as the
// ROM does, including the scratch the four tests use. Of the registers, the
// frame after the walk keeps only carry and overflow, which `thread_yield`
// pushes into the thread's saved status byte. `WalkLog` carries those two,
// and what the call cost the 65816, to the harness. The walk itself never
// reads the log.
//
// Neither is tidy. Carry is the answer to the last question asked, which is
// clear after "someone is standing here too" and set after every other stop.
// Overflow is whatever the last add in the tests left, and on the big maps of
// levels 19 and 25 the tilemap address `terrain_blocked` adds up sets it.
//
// Two paths are the ROM's, and `walk_supported` says when:
//
// * the double step, bit 15 of `$54`, which runs the whole walk twice. No
//   input has taken it.
// * a solid tile with a reaction of its own. The reaction takes over from
//   inside the walk, and it is not ported.
//
// Port code: libc only.

#ifndef PORT_WALK_H
#define PORT_WALK_H

#include <stdbool.h>
#include <stdint.h>

#include "assets/rom.h"
#include "port/oam.h"  // ObstacleWork
#include "port/wram.h"

#define PLAYER_WALK_PC 0x80e4bau  // `$80:E4BA`, entered by the frame's `RTS`
#define PLAYER_WALK_RTS_PC 0x80e542u

// Fields on the player's page. `$30`/`$32` is where the player is, and
// `step_propose` leaves where the pad wants them at `$34`/`$36`.
#define WALK_DP_RECORD 0x08  // the player's actor record
#define WALK_DP_X 0x30
#define WALK_DP_Y 0x32
#define WALK_DP_WANT_X 0x34
#define WALK_DP_WANT_Y 0x36
#define WALK_DP_MODE 0x54  // bit 15: walk twice
#define WALK_TWICE 0x8000u

// `$80:E739`: the attribute bits that name a tile's reaction, and the six
// values that have one.
#define WALK_REACTION_BITS 0xcb38u
#define WALK_REACTIONS 6

// The questions, as the log counts them.
typedef enum {
  WALK_ASK_GROUND,      // `terrain_blocked`
  WALK_ASK_REACTION,    // `$80:E739`, only after solid ground
  WALK_ASK_TETHER,      // `step_tether_blocked`
  WALK_ASK_THERE,       // `actor_obstacle_at_point`, where the step lands
  WALK_ASK_HERE,        // ...and where the player stands, after a yes
  WALK_ASK_MAP,         // `terrain_out_of_bounds`
  WALK_ASK_COUNT
} WalkQuestion;

// For the harness, and only for it. `asked[q][yes]` counts how often each
// question was asked and how it answered, which is all it takes to price the
// ROM's instructions around the calls. `obstacle` is the obstacle test's own
// count, summed over both times it can be asked. `last_yes` is the carry the
// last question left, which is the carry the walk returns, and `overflow` the
// V the last add left.
typedef struct {
  uint16_t asked[WALK_ASK_COUNT][2];
  uint16_t taken;  // axes the player moved along
  ObstacleWork obstacle;
  bool last_yes;
  bool overflow;
} WalkLog;

// Would the port walk this frame the way the ROM does? False for the two
// paths above. It may run the walk to find out, so `w` is a copy.
bool walk_supported(Wram* w, const Rom* rom, uint16_t page);

// Walk the player whose page is `page` for one frame. `log` may be NULL.
void player_walk(Wram* w, const Rom* rom, uint16_t page, WalkLog* log);

// The reaction a solid tile has, 1 to 6, or 0 for none. `attrs` is the
// attribute word as `terrain_blocked` leaves it, shifted right one.
int walk_tile_reaction(uint16_t attrs);

#endif
