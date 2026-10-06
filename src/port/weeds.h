// The weeds, which grow across the ground a tile at a time.
//
// A weed is a thread, `$81:D28C`. Its loop sleeps two ticks, runs one of
// its state bodies, and looks at its fate. This is one pass of that loop,
// from where `thread_yield` returns to the next yield:
//
//   $81:D2A5  weed_frame
//
// What is not here is the thread's setup, its death, and the pass on which
// it puts out the things that snap at a player: that plays an animation
// which sleeps inside the call. Whether a pass does is down to a draw, so
// the port finds out by running it: `WeedWork::declined` says so, and a
// guard asks it of a scratch copy first.
//
// ## What a weed does
//
// **It grows while nobody is near.** With neither player within `$48` of
// its root, each pass grows one of its arms, in turn. With one near it
// stops growing and is on its guard.
//
// **An arm feels its way.** It has a tip, and each pass it tries a step of
// four pixels from there, one of four ways that are the arm's own, by a
// draw. The step is good if no player is within `$18` of it, the ground
// there is a tile a weed can take, which is bit 7 of the tile's attributes,
// and it is on the level.
//
//   * A step onto ground it cannot take moves the tip there all the same,
//     and nothing grows.
//   * A step a player is near, or off the level, leaves the tip where it was.
//   * A good step, about half the time, moves the tip and plants one of
//     four small tiles there.
//   * The rest of the time the tip stays, and with room, which is no player
//     near and eight pixels more of level beyond, it plants a clump: four
//     tiles round the spot, each only where the ground can take one.
//
// A weed whose tiles are the kind `$1400` may also mark the spot, on a
// second draw, if it is `$20` or more from the root and nothing blocks it.
// The mark is the pair of words a thread hands to what it starts.
//
// **On its guard it draws.** Under `$19`, about one pass in ten, it puts
// out what snaps. Otherwise it goes back to growing.
//
// **Having snapped it rests** forty-five passes, and then grows again.
//
// ## What it puts out
//
// What snaps is a thread of its own, `$81:D4C9`, three to a snap. Each goes
// in a straight line from beside the root to where the player was, by
// `line_step` of `port/line.h`, and in an arc: its height takes a rise
// that is one less every pass, so it goes up, slows, and comes down. Below
// the ground it has landed, and the ROM tells whatever is there and ends
// it. A pass of its flight is here too:
//
//   $81:D4D6  weed_seed_frame
//
// "Seed" is a guess at what is drawn.
//
// ## The arms
//
// The setup gives it four tips, all at the root. The turn goes round eight.
// For the other four the ROM reads and writes the words that follow the
// tips on the page, which are this same routine's scratch: where the last
// step was from, where it was to, and so on. The port does as the ROM does,
// word for word, so those four are whatever the ROM makes of them.
//
// ## Why it is the weeds
//
// It plants tiles and goes on planting them, and it is on levels 9, 13 and
// 41. The swipe of `port/swipe.h`, which changes tiles by two bits of
// their attributes, reads like what cuts them down again; I have not
// checked that those are the bits these tiles have. I have not seen either
// on a screen.
//
// ## Its contract with the ROM
//
// It writes WRAM exactly as the ROM does. A pass ends at the `JSL
// thread_yield` with the tick count in A, or past the test of its fate with
// that in A. Carry is the ROM's, because the next pass may begin with a
// draw, and a draw takes the carry before it.
//
// Port code: libc only.

#ifndef PORT_WEEDS_H
#define PORT_WEEDS_H

#include <stdbool.h>
#include <stdint.h>

#include "assets/rom.h"
#include "port/cpu.h"
#include "port/line.h"
#include "port/oam.h"
#include "port/terrain.h"
#include "port/tile_put.h"
#include "port/wram.h"

// The tables are in bank `$81`, which is the thread's data bank.
#define WEED_BANK 0x81u

#define WEED_FRAME_PC 0x81d2a5u
#define WEED_YIELD_PC 0x81d2a1u  // `JSL thread_yield`, A already 2
#define WEED_FATE_PC 0x81d2b1u   // past the `BEQ`, its fate in A
#define WEED_YIELD_TICKS 2

// The state bodies, by the address the thread keeps in `$14`.
#define WEED_STATE_GROW 0xd12du
#define WEED_STATE_GUARD 0xd148u
#define WEED_STATE_REST 0xd1f0u

// Fields on its page.
#define WEED_DP_MARK_X 0x00
#define WEED_DP_MARK_Y 0x02
#define WEED_DP_RECORD 0x08
#define WEED_DP_TILE_BITS 0x0a  // set on every tile it plants
#define WEED_DP_FATE 0x0c       // 0 alive, anything else and the thread ends
#define WEED_DP_ROOT_X 0x10
#define WEED_DP_ROOT_Y 0x12
#define WEED_DP_STATE 0x14
#define WEED_DP_REST_LEFT 0x16
#define WEED_DP_ARM 0x18        // whose turn: 0 to `$1C`, by fours
#define WEED_DP_TIPS 0x1a       // across and down for each arm
#define WEED_DP_FROM_X 0x2a     // the step being tried
#define WEED_DP_FROM_Y 0x2c
#define WEED_DP_TO_X 0x2e
#define WEED_DP_TO_Y 0x30
#define WEED_DP_SCRATCH 0x34
#define WEED_DP_CLEARED 0x36    // zeroed on every pass that sleeps
#define WEED_DP_CLUMP_AT 0x38
#define WEED_ARM_END 0x0020

#define WEED_CLUMP_TILES 4

typedef struct {
  uint16_t state;  // the body that ran
  bool declined;   // the ROM's: it puts out what snaps

  // Growing.
  PlayerPickRegs seek;  // is a player near the root?
  bool player_near;
  bool grew;            // an arm's turn was taken
  bool pick_overflow;   // the draw of its step overflowed, which costs more
  PlayerPickRegs step_range;   // is a player near the step?
  bool step_near;
  bool step_ground_asked;
  bool step_bare;       // ground it cannot take
  bool step_edge_asked;
  BoundsExit step_edge;
  bool step_off;
  bool tip_moved;       // ...onto ground it cannot take
  bool plant_overflow;  // the draw that chooses a tile or a clump
  bool planted;         // one small tile
  bool tile_overflow;   // ...and the draw of which
  TilePutRegs tile;
  bool kind_marks;      // its tiles are the kind that may mark the spot
  bool gap_x_negative, gap_y_negative;
  bool near_root;
  bool mark_overflow;   // the draw for the mark
  bool mark_drawn, mark_wanted;
  TerrainRegs mark_ground;
  bool marked;
  PlayerPickRegs room_range;
  bool room_near;
  bool room_edge_asked;
  BoundsExit room_edge;
  bool room_off;
  bool clump;           // room: the four tiles were tried
  bool clump_put[WEED_CLUMP_TILES];
  TilePutRegs clump_tile[WEED_CLUMP_TILES];
  bool went_round;      // the turn came back to the first arm

  // On its guard.
  bool guard_overflow;
  // Resting.
  bool rest_over;

  bool c, v;     // carry and overflow as the pass leaves them
  bool v_known;  // ...or an overflow the port does not follow
} WeedWork;

// Can `weed_frame` take this pass? Only in a state it knows, on an arm the
// tables have. It only looks.
bool weed_frame_supported(const Wram* w, uint16_t page);

// One pass, for the weed whose page is `page`. `carry` and `overflow` are
// the thread's own, as it woke. True if it sleeps, false if its fate is no
// longer zero. `k->declined` says the pass is the ROM's after all. WRAM is
// then part written.
bool weed_frame(Wram* w, const Rom* rom, uint16_t page, bool carry,
                bool overflow, WeedWork* k);

#define WEED_SEED_PC 0x81d4d6u
#define WEED_SEED_SLEEP_PC 0x81d4d2u   // `JSL`, A already 2
#define WEED_SEED_LANDED_PC 0x81d4e3u
#define WEED_SEED_DP_RISE 0x22         // what its height takes this pass
#define WEED_SEED_TICKS 2

typedef struct {
  LineWork line;
  bool falling;  // the rise has gone negative
  bool landed;
} WeedSeedWork;

void weed_seed_frame(Wram* w, PortCpu* c, WeedSeedWork* k);

#endif
