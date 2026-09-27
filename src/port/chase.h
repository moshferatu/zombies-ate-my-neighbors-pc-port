// The monster's chase: one frame of going after whoever is nearest.
//
// `$81:BEE3` is one of the state bodies of the monster's thread (see
// `port/monster.h`), the one `monster_seek` installs when something comes
// within `$B4`. The thread reaches it through `$12` by a computed `RTS`, as it
// reaches all its states, and it returns to the thread's loop with an `RTS`.
// This is that body as readable C. It calls the scan, the bearing, the random
// number generator and the terrain and actor tests as C functions.
//
// ## What the chase does
//
// Each frame it looks for the nearest of the four kinds of actor
// `actor_nearest` knows about.
//
// * **Nothing within `$B4`: it gives up.** It picks one of the four straight
//   directions at random and hands over to the wander state at `$81:BE14`,
//   which walks that way.
// * **Otherwise it takes one step towards it.** If the target lies straight
//   up, down, left or right, that is the way it steps. If the target lies on a
//   diagonal, it closes the smaller of the two gaps first, which lines it up
//   with the target. On about half its frames, as the random number
//   generator falls, the step is two pixels instead of one.
//
// The step is refused by solid ground or by anyone standing where it lands. A
// monster blocked by someone waits. A monster blocked by ground looks for a
// tile it can leap: one tile ahead or two, a tile with attribute bit 13, and
// clear ground 48 to 56 pixels beyond it. With all three it leaps, which is a
// state of its own at `$81:BD01` and is not ported. Without them it waits.
//
// ## Directions
//
// The game numbers them 1 for up and then clockwise to 8 for up-left, with 0
// for none. `actor_bearing` answers in those numbers and the step tables are
// indexed by them. Odd numbers are the straight ones.
//
// ## Its contract with the ROM
//
// It writes WRAM exactly as the ROM does, scratch included. Of the registers,
// nothing the chase leaves is read, and the reason is worth keeping.
//
// The thread's loop follows the chase with `monster_seek` or
// `monster_deliver`. Both begin with arithmetic that sets N, Z and C before
// anything reads them. `monster_seek`'s is `actor_nearest`, which runs an
// `ADC` only for an actor it matches. After a step it matches the one the
// chase just found, because nothing between the two calls changes who is on
// the board. After giving up, it may match nobody. Overflow can then survive
// to the thread's next `PHP`, so the give-up path hands back the random number
// generator's V, and only that path does.
//
// One path is the ROM's, and `chase_supported` says when: the leap.
//
// Port code: libc only.

#ifndef PORT_CHASE_H
#define PORT_CHASE_H

#include <stdbool.h>
#include <stdint.h>

#include "assets/rom.h"
#include "port/oam.h"  // ActorNearestWork, AtPointWork
#include "port/wram.h"

#define MONSTER_CHASE_PC 0x81bee3u  // entered by the thread's computed `RTS`
#define MONSTER_CHASE_RTS_PC 0x81bf98u

// The tables sit in the chase's own bank, which is where the thread keeps its
// data bank.
#define CHASE_BANK 0x81u

// Fields on the monster's page, beyond those `port/monster.h` names.
#define CHASE_DP_NEXT_X 0x0e     // the point being tried
#define CHASE_DP_NEXT_Y 0x10
#define CHASE_DP_STEP_INDEX 0x18  // the direction times four, for the tables
#define CHASE_DP_BLOCKER 0x32    // 0 when ground refused the step, 1 when an actor did
#define CHASE_DP_GAP_X 0x34      // target less monster, when the bearing was diagonal
#define CHASE_DP_GAP_Y 0x36
#define CHASE_DP_STEPS 0x38      // the step table in use this frame
#define CHASE_DP_USUAL_STEPS 0x3a  // ...and the one it goes back to

// `LDA #$B9B5`: the step table at two pixels a frame.
#define CHASE_FAST_STEPS 0xb9b5u
// `LDA #$BE14`: the wander state, installed on giving up.
#define MONSTER_STATE_WANDER 0xbe14u

// The leap test's tables, in `CHASE_BANK`: where to look for a tile to leap,
// and how far past it the monster lands. Both indexed by direction times four.
#define CHASE_LEAP_PROBE 0xbc99u
#define CHASE_LEAP_LANDING 0xbcbdu
// Attribute bit 13: a tile a monster can leap.
#define CHASE_LEAPABLE 0x2000u

// For the harness, and only for it: what the chase did, which is what it
// takes to price the ROM's instructions around the calls.
typedef enum {
  CHASE_STEPPED,       // the step was taken
  CHASE_MET_SOMEONE,   // an actor stood where it landed
  CHASE_MET_GROUND,    // solid ground, and nothing to leap
  CHASE_LEAPT,         // solid ground, and something to leap: the ROM's
} ChaseOutcome;

typedef struct {
  ActorNearestWork nearest;
  bool gave_up;        // nothing within range
  // The rest is for a chase that did not give up.
  bool straight;       // the target was straight up, down, left or right
  bool gap_x_negative; // ...or on a diagonal: which way each gap ran
  bool gap_y_negative;
  bool across;         // ...and whether it closed the horizontal gap
  bool fast;           // two pixels this frame
  ChaseOutcome outcome;
  bool asked_actors;   // the ground was clear, so the actor test ran
  AtPointWork at_point;
  int leap_probes;     // 0, 1 or 2 tiles looked at for something to leap
  bool leap_found;     // ...and one had bit 13, so the landing was tested
  bool overflow;       // V as the give-up path leaves it
} ChaseLog;

// Would the port chase this frame the way the ROM does? False when the
// monster would leap. It runs the chase to find out, so `w` is a copy.
bool chase_supported(Wram* w, const Rom* rom, uint16_t page);

// Chase for one frame, for the monster whose page is `page`. `log` may be
// NULL.
void monster_chase(Wram* w, const Rom* rom, uint16_t page, ChaseLog* log);

#endif
