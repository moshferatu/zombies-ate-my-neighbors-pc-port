// The evil dolls: how they climb out of a toy box, close in, swing and throw
// their axes, charge, and are knocked back when hit.
//
// A doll is a thread, `$81:B2B0`, which the levels' spawn lists in bank `$9F`
// name on many levels. Its loop is the small state machine every enemy has:
// yield a frame, run the state body `$0E` names, tick its timers, and go round
// again until `$0A` says to stop. A state body is entered by a computed `RTS`
// and returns with one. This is the state bodies, the tick, and the loop that
// runs them, as readable C:
//
//   $AEA6  seek_begin   go back to closing in, at a step every four frames
//   $AEBA  seek         close in on whoever is nearest, and attack
//   $B12C  dash         charge in a straight line, two pixels a frame
//   $AF8C  knocked      fly back from a hit, and throw on landing
//   $B203  leap_out     the jump out of the toy box
//   $B377  tick         count the timers down, and move the picture
//   $B2DF  frame        the loop: one state body and the tick
//
// What is not here is the thread's setup and ending, and the opening
// animation at `$81:B1DA`, which plays a script that yields in the middle
// (`$81:832C`). A frame in that state is the ROM's.
//
// It was identified by swapping its pictures for a zombie's in a copy of the
// cartridge: on level 49 the doll with the axe became a zombie. The collision
// handler it installs on landing is `enemy_b41c_collide` in `port/collide.h`,
// which was named for its address before anything said what it was.
//
// ## What a doll does
//
// **It climbs out.** After the opening animation it jumps down onto the
// floor, aiming 40 pixels below where it appeared. A jump lasts twenty frames
// and covers an eighteenth of the distance on each, so it lands a little past
// that. Only on landing does it install its collision handler, so a doll in
// the air cannot be hit.
//
// **It closes in.** It goes after whoever `actor_nearest` finds within `$D0`,
// or failing that the nearer player within `$D0`. With neither it leaves the
// level. It steps a pixel at a time, on one frame in four, and it closes the
// smaller of the two gaps first, which lines it up with the target. A step
// is refused only by solid ground, never by someone standing there.
//
// **Within 16 pixels on both axes it swings its axe.** The axe is a second
// display record, shown beside the doll on the frames it swings and hidden on
// all the others.
//
// **Lined up, it charges.** When the target is straight up, down, left or
// right, the doll runs at it two pixels a frame until it is about 16 pixels
// short, ground permitting. It does not look again until the charge is spent.
//
// **It throws.** Exactly on a diagonal, and on one frame in 256 otherwise,
// it throws an axe (the thread at `$81:B4EA`), and then cannot throw again
// for 36 frames.
//
// **A hit knocks it back**, the opposite way to its facing, in the same
// twenty-frame arc, aimed 35 pixels away. If solid ground or the edge of the
// level is there instead, it stands its ground. On landing it throws an axe at
// once, along the wider of the two gaps it last measured, whatever its
// cooldown says.
//
// ## Facings
//
// `$46` is a facing times four: 0 up, 4 right, 8 down, 12 left, and 16 for a
// diagonal, which is no facing at all. The doll's animation tables are four
// frames for each facing, indexed by the facing plus the step of the walk
// cycle, so 16 runs off the end into the next table. Nothing in play asks
// for that.
//
// ## Its contract with the ROM
//
// It writes WRAM exactly as the ROM does, scratch included. Of the registers,
// carry and overflow outlive a state body: the tick after it leaves both
// alone, and `thread_yield`'s `PHP` parks them in the thread's status byte.
// So the port reports both as the ROM leaves them, from whichever comparison,
// sum or call wrote each one last. Some paths write neither, and those pass
// the thread's own through. The one path that leaves overflow to
// `actor_nearest` or `player_in_range` is the one that leaves the level,
// whose next write to it comes before anything can read it.
//
// The tick leaves A holding the doll's Y and Y holding its record.
//
// Port code: libc only.

#ifndef PORT_DOLL_H
#define PORT_DOLL_H

#include <stdbool.h>
#include <stdint.h>

#include "assets/rom.h"
#include "port/oam.h"  // ActorNearestWork, PlayerPickRegs
#include "port/terrain.h"  // BoundsExit
#include "port/wram.h"

// The tables are in bank `$81`, which is the thread's data bank.
#define DOLL_BANK 0x81u

// The entries, and an `RTS` of each.
#define DOLL_SEEK_BEGIN_PC 0x81aea6u
#define DOLL_SEEK_BEGIN_RTS_PC 0x81aeb9u
#define DOLL_SEEK_PC 0x81aebau
#define DOLL_SEEK_RTS_PC 0x81aeebu
#define DOLL_DASH_PC 0x81b12cu
#define DOLL_DASH_RTS_PC 0x81b1cau  // `$81:B1A1`'s: the second step ends in it
#define DOLL_KNOCKED_PC 0x81af8cu
#define DOLL_KNOCKED_RTS_PC 0x81af99u
#define DOLL_LEAP_OUT_PC 0x81b203u
#define DOLL_LEAP_OUT_RTS_PC 0x81b220u
#define DOLL_TICK_PC 0x81b377u
#define DOLL_TICK_RTS_PC 0x81b392u

// The state bodies, as `$0E` names them.
#define DOLL_STATE_SEEK_BEGIN 0xaea6u
#define DOLL_STATE_SEEK 0xaebau
#define DOLL_STATE_DASH 0xb12cu
#define DOLL_STATE_KNOCKED 0xaf8cu
#define DOLL_STATE_LEAP_OUT 0xb203u

// A whole frame of the loop, from where `thread_yield` returns: the state
// body, the tick, and `STZ $5A : LDA #$0001` back to the yield, or with `$0A`
// set the loop's way out.
#define DOLL_FRAME_PC 0x81b2dfu
#define DOLL_FRAME_YIELD_PC 0x81b2dbu  // `JSL thread_yield`, A already 1
#define DOLL_FRAME_LEAVE_PC 0x81b2eeu
#define DOLL_YIELD_TICKS 1

// The animation tables `$16` points at: four bytes a frame, a mask for the
// record's flags and a picture number. A mask with bit 15 is ANDed in, and
// any other is ORed in, which is how a frame mirrors or does not.
#define DOLL_FRAMES_WALK 0xad3eu
#define DOLL_FRAMES_SWING 0xad8eu
// Picture numbers index this, sixteen-bit metasprite pointers in bank `$90`.
#define DOLL_PICTURES 0xacd8u

// Fields on the doll's page. `$00` to `$07` are the arguments an axe is
// spawned with: where it starts, and which way it flies on each axis.
#define DOLL_DP_AXE_X 0x00
#define DOLL_DP_AXE_Y 0x02
#define DOLL_DP_AXE_DX 0x04
#define DOLL_DP_AXE_DY 0x06
#define DOLL_DP_RECORD 0x08
#define DOLL_DP_LEAVE 0x0a        // nonzero ends the thread
#define DOLL_DP_STATE 0x0e
#define DOLL_DP_STEP_WAIT 0x10    // frames until it may step; see the tick
#define DOLL_DP_STEP_EVERY 0x12   // ...and what that starts from
#define DOLL_DP_FRAMES 0x16       // the animation table in use
#define DOLL_DP_X 0x18            // where it is, which the record follows
#define DOLL_DP_Y 0x1c
#define DOLL_DP_TO_X 0x1e         // the point being tried, or the target's
#define DOLL_DP_TO_Y 0x20
#define DOLL_DP_RISE 0x24         // in the air: how fast it is going up
#define DOLL_DP_RISE_26 0x26      // set to 3 with it, and never read here
#define DOLL_DP_RISE_TICK 0x28    // ...which slows every fourth frame
#define DOLL_DP_FLIGHT_STEP_X 0x2a  // in the air: 1 or -1 on each axis
#define DOLL_DP_FLIGHT_STEP_Y 0x2c
#define DOLL_DP_FLIGHT_DX 0x2e    // ...the distance to cover on each
#define DOLL_DP_FLIGHT_DY 0x30
#define DOLL_DP_FLIGHT_ACC_X 0x32 // ...and how far through a pixel it is
#define DOLL_DP_FLIGHT_ACC_Y 0x34
#define DOLL_DP_STRIDE 0x36       // the walk cycle, 0 to 3
#define DOLL_DP_GAP_X 0x38        // how far the target is, on each axis
#define DOLL_DP_GAP_Y 0x3a
#define DOLL_DP_TARGET 0x3c       // its record
#define DOLL_DP_AIM_DX 0x3e       // from here to `$1E`/`$20`: the distance...
#define DOLL_DP_AIM_STEP_X 0x40   // ...and 1, 0 or -1, on each axis
#define DOLL_DP_AIM_DY 0x42
#define DOLL_DP_AIM_STEP_Y 0x44
#define DOLL_DP_FACING 0x46       // a facing times four; see above
#define DOLL_DP_TRIED 0x48        // a step: the way across was tried
#define DOLL_DP_DASH 0x4a         // charge steps left
#define DOLL_DP_HIT 0x4c          // set by the collision handler
#define DOLL_DP_AXE 0x4e          // the axe's display record
#define DOLL_DP_HIT_ID 0x5a       // what hit it, cleared every frame
#define DOLL_DP_THROW_WAIT 0x5c   // frames until it may throw; negative: now

// For the harness, and only for it: what happened, which with the path is
// what it takes to price the ROM's instructions around the calls.
typedef struct {
  bool blocked;  // the ground there was solid
  int tiles;     // ...as the ground test found after this many of its six
} DollProbe;

#define DOLL_MAX_PROBES 4

// How `seek` ended.
typedef enum {
  DOLL_LEFT,          // nobody within reach
  DOLL_KNOCKED_BACK,  // it had been hit
  DOLL_SWUNG,         // within 16 on both axes
  DOLL_SWUNG_ON_TOP,  // ...or the two gaps summed to zero
  DOLL_DASHED_DOWN,   // lined up across: the gaps summed to the vertical one
  DOLL_DASHED_ACROSS, // ...or to the horizontal one
  DOLL_THREW_DIAGONAL,  // exactly on a diagonal: a throw, then a step
  DOLL_THREW_AT_RANDOM, // one frame in 256
  DOLL_STEPPED,       // otherwise
} DollSeek;

// How a knock-back began, or did not.
typedef enum {
  DOLL_KNOCK_FACELESS,  // no facing to be knocked back from
  DOLL_KNOCK_GROUND,    // solid ground where it would land
  DOLL_KNOCK_EDGE,      // the edge of the level
  DOLL_KNOCK_FLEW,
} DollKnock;

typedef struct {
  uint16_t state;      // a frame: the state body that ran
  DollProbe probe[DOLL_MAX_PROBES];
  int probes;
  ActorNearestWork nearest;
  PlayerPickRegs players;  // what `player_in_range` did, when it was asked
  DollSeek seek;
  bool far;            // seek: nobody `actor_nearest` knows within `$D0`
  bool near_across;    // ...the horizontal gap was within 16
  int gaps_negated;    // ...and of the two gaps, how many were negative
  bool across_first;   // a step: the horizontal gap was the smaller
  bool step_waited;    // ...or the step timer had not run out
  bool facing_none;    // a step went nowhere it could face
  bool swing_waited;   // a swing: the step timer had not run out
  DollKnock knock;
  BoundsExit knock_edge;  // ...how the edge-of-the-level test left
  int flights_negated; // ...a knock-back's distances that were negative
  bool drew_overflow;  // seek: the random draw overflowed, which costs more
  bool threw;          // a throw was not waiting on its cooldown
  int axe_slot;        // ...the thread slot the axe took, doubled, or -1
  bool dash_short;     // a charge: too close to be worth it
  bool dash_spent;     // ...or the charge had run out
  bool landed;         // knocked or leap_out: back on the ground
  bool landed_below;   // ...leap_out: below it, not on it
  bool landing_across; // knocked: the axe thrown on landing went across
  bool showed;         // leap_out: the step timer let it change picture
  bool slowed;         // in the air: the rise slowed this frame
  int flight_x, flight_y;  // ...and pixels moved on each axis
  int frames, frames_masked;  // pictures shown, and of those with an AND
  int strides, wraps;  // walk cycles moved on, and of those back to 0
  int aims;            // `$81:B24D`s run...
  int aims_negated, aims_level;  // ...and their axes negative, or zero
  int faced;           // `$81:B27E`s run...
  int faced_down, faced_across;  // ...and those that had each to add
  bool tick_threw;     // tick: the throw cooldown was still counting
  bool tick_reset;     // ...the step timer went round
  bool c, v;           // carry and overflow as the call leaves them
  bool c_set, v_set;   // ...and whether it wrote each at all
} DollLog;

// One frame of each state body, for the doll whose page is `page`. `log` may
// be NULL.
void doll_seek_begin(Wram* w, const Rom* rom, uint16_t page, DollLog* log);
void doll_seek(Wram* w, const Rom* rom, uint16_t page, DollLog* log);
void doll_dash(Wram* w, const Rom* rom, uint16_t page, DollLog* log);
void doll_knocked(Wram* w, const Rom* rom, uint16_t page, DollLog* log);
void doll_leap_out(Wram* w, const Rom* rom, uint16_t page, DollLog* log);

// The tick the loop runs after every state body.
void doll_tick(Wram* w, uint16_t page, DollLog* log);

// Is the state `$0E` names one of the five here? The opening animation is not.
bool doll_frame_supported(const Wram* w, uint16_t page);

// A whole frame: the state body and the tick. False when the doll is leaving.
bool doll_frame(Wram* w, const Rom* rom, uint16_t page, DollLog* log);

#endif
