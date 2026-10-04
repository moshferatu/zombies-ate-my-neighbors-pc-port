// The clones: the player's doubles, which copy how the player moves and then
// come for them.
//
// A clone is a thread, `$81:8E89`. It draws from a table of the player's own
// pictures, Zeke's or Julie's, and once it has grown its loop is one pass a
// frame. This is that pass as readable C, from where `thread_yield` returns
// to the next yield:
//
//   $81:8EA8  frame   animate, pick a direction, step, count the mode down
//
// It was identified by swapping the two picture tables at `$81:8FA3` for a
// zombie's picture in a copy of the cartridge: on level 5 everything that
// looked like the player became a zombie. Its collision handler is
// `enemy_9063_collide` in `port/collide.h`.
//
// What is not here is the thread's setup, the growing animation it opens
// with, and its ending.
//
// ## What a clone does
//
// **It has two modes, and swaps between them.** In one it copies the player:
// its direction is whatever that player's pad is holding, read from the same
// word the player's own thread reads. In the other it comes for the nearer
// player, by `player_bearing`, within `$F0`. When it is chasing and neither
// player is that near, it leaves the level.
//
// Each mode lasts a number of frames drawn from the random number generator,
// 1 to 255. A draw of zero is not a short mode but a very long one: the count
// is decremented before it is tested, so it runs for 65,536 frames.
//
// **It moves three quarters of a pixel a frame.** Its position is kept in
// quarter pixels. A step tries the two axes on their own, across and then up
// or down, and each is refused by solid ground or by anyone standing there,
// so it slides along walls as a fast zombie does.
//
// **It walks in the player's pictures**, four to a direction, the next every
// seven frames. A direction of 6 or more, the three that face left, is drawn
// mirrored.
//
// ## Directions
//
// The game's numbers: 0 for none, 1 for up, and clockwise to 8 for up-left.
// The pad's direction word holds them doubled.
//
// ## Its contract with the ROM
//
// It writes WRAM exactly as the ROM does. A frame ends at the `JSL
// thread_yield` with the tick count in A, or at the loop's way out. Carry and
// overflow are the last step's, or the random draw's on the frame the mode
// changes, and the thread's own on the frame it finds itself told to leave.
// See `port/flags.h`.
//
// Port code: libc only.

#ifndef PORT_CLONE_H
#define PORT_CLONE_H

#include <stdbool.h>
#include <stdint.h>

#include "assets/rom.h"
#include "port/oam.h"  // AtPointWork, PlayerPickRegs
#include "port/wram.h"

// The tables are in bank `$81`, which is the thread's data bank.
#define CLONE_BANK 0x81u

#define CLONE_FRAME_PC 0x818ea8u
#define CLONE_YIELD_PC 0x818ea4u  // `JSL thread_yield`, A already 1
#define CLONE_LEAVE_PC 0x818ed5u
#define CLONE_YIELD_TICKS 1

// Fields on the clone's page.
#define CLONE_DP_RECORD 0x08
#define CLONE_DP_PICTURE_TIMER 0x0a  // frames until the next picture
#define CLONE_DP_CYCLE 0x0c          // the walk cycle, 0 to 3
#define CLONE_DP_DIRECTION 0x0e
#define CLONE_DP_X 0x12              // where it is, in pixels...
#define CLONE_DP_Y 0x14
#define CLONE_DP_FINE_X 0x16         // ...and in quarter pixels
#define CLONE_DP_FINE_Y 0x18
#define CLONE_DP_TO_X 0x1a           // the step being tried, in pixels...
#define CLONE_DP_TO_Y 0x1c
#define CLONE_DP_TO_FINE_X 0x1e      // ...and in quarter pixels
#define CLONE_DP_TO_FINE_Y 0x20
#define CLONE_DP_COPYING 0x24        // 1 copying the player, 0 chasing
#define CLONE_DP_MODE_LEFT 0x26      // frames left of this mode
#define CLONE_DP_PICTURES 0x2a       // the player's pictures it draws from
#define CLONE_DP_PLAYER 0x2c         // which player it copies, 0 or 2
#define CLONE_DP_LEAVE 0x2e          // nonzero ends the thread

// For the harness, and only for it: what happened, which with the path is
// what it takes to price the ROM's instructions around the calls.
typedef struct {
  bool ground;   // the ground there was solid
  int tiles;     // ...as the ground test found after this many of its six
  bool someone;  // ...or it was not, and someone was standing there
} CloneProbe;

typedef struct {
  bool told_to_leave;  // `$2E` was set, so nothing else ran
  bool new_picture;    // the walk cycle moved on
  bool copying;        // it copied the player; otherwise it chased
  PlayerPickRegs players;  // what `player_bearing` did, chasing
  bool nobody;         // ...and found neither player near enough
  CloneProbe probe[2]; // the step: across, then up or down
  AtPointWork at_point;  // summed over both
  bool mirrored;       // it faces left
  bool mode_changed;   // the mode ran out...
  bool drew_overflow;  // ...on a draw that overflowed, which costs more
  bool c, v;           // carry and overflow as the frame leaves them
  bool c_set, v_set;   // ...and whether it wrote each at all
} CloneLog;

// One frame, for the clone whose page is `page`. False when the clone is
// leaving. `log` may be NULL.
bool clone_frame(Wram* w, const Rom* rom, uint16_t page, CloneLog* log);

#endif
