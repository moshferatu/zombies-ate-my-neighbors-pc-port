// The werewolves, who run at somebody, pounce on them from a distance, and
// strike when they are beside them.
//
// A werewolf is a thread, `$81:ABF5`. Its loop sleeps a tick, runs one of
// its state bodies, and looks at its fate. This is one pass of that loop as
// readable C, from where `thread_yield` returns to the next yield:
//
//   $81:AC1E  werewolf_frame
//
// What is here is the running, the passes of a strike and the flight of a
// pounce. What is not is the thread's setup, the crouch before a pounce and
// the landing after it, which sleep in the middle, and its end. A pass in
// one of those states is the ROM's. So are three kinds of pass that only
// running it finds, which `WerewolfLog::declined` says and a guard asks of a
// scratch copy first: one that goes on to choose where a pounce is to come
// down, the last of a strike, and one with nobody on the level to run at.
//
// ## What it does
//
// **Running**, it finds whoever `actor_nearest` knows and steps a pixel their
// way, each axis by itself if the ground there may be walked on and nobody
// is standing there. On a draw, half the time, it steps twice.
//
// **It pounces on a draw**, about one pass in seventeen, at somebody under
// 325 away and 70 or more by the sum of the two gaps. Hurt since it last
// looked, it hops away to somewhere near instead.
//
// **It strikes somebody within 4 of its row and 29 across.** It faces them,
// takes a second record for the blow, and shows nine pictures of three
// passes each. From the sixth the second record is drawn.
//
// **A pounce is an arc**: so many steps along the longer of the two gaps,
// the other kept in proportion, and a height that rises by a quarter of a
// count that falls by one each pass. It lands when its height is exactly
// nothing, and beside whoever it pounced at if they have not moved far.
//
// ## Its contract with the ROM
//
// It writes WRAM exactly as the ROM does. A pass ends at the `JSL
// thread_yield` with the tick count in A, or past the test of its fate with
// that in A. Carry and overflow are left as the ROM leaves them.
//
// Port code: libc only.

#ifndef PORT_WEREWOLF_H
#define PORT_WEREWOLF_H

#include <stdbool.h>
#include <stdint.h>

#include "assets/rom.h"
#include "port/oam.h"  // the works and registers of what it asks
#include "port/terrain.h"
#include "port/wram.h"

// The tables are in bank `$81`, which is the thread's data bank.
#define WEREWOLF_BANK 0x81u

#define WEREWOLF_FRAME_PC 0x81ac1eu
#define WEREWOLF_YIELD_PC 0x81ac1au  // `JSL thread_yield`, A already 1
#define WEREWOLF_FATE_PC 0x81ac2au   // past the `BEQ`, its fate in A
#define WEREWOLF_YIELD_TICKS 1

// The state bodies, by the address the thread keeps in `$0A`.
#define WEREWOLF_STATE_RUN 0xabc6u
#define WEREWOLF_STATE_STRIKE 0xaacfu
#define WEREWOLF_STATE_FLIGHT 0xa9f0u
// ...and two the port only names.
#define WEREWOLF_STATE_CROUCH 0xa994u
#define WEREWOLF_STATE_LANDED 0xaa71u

// Fields on its page.
#define WEREWOLF_DP_RECORD 0x08
#define WEREWOLF_DP_STATE 0x0a
#define WEREWOLF_DP_PICTURE 0x0c       // which of its cycle
#define WEREWOLF_DP_PICTURE_WAIT 0x0e  // passes until its next picture
#define WEREWOLF_DP_FATE 0x10  // 0 alive, anything else and the thread ends
#define WEREWOLF_DP_X 0x12
#define WEREWOLF_DP_Y 0x14
#define WEREWOLF_DP_TRY_X 0x16         // the step being tried
#define WEREWOLF_DP_TRY_Y 0x18
#define WEREWOLF_DP_WAY 0x1a           // a bearing, 1 to 8, or 0 for none
#define WEREWOLF_DP_QUARRY 0x1e  // who a pounce is at, or negative for nobody
#define WEREWOLF_DP_LAND_X 0x20        // where a pounce is to come down
#define WEREWOLF_DP_LAND_Y 0x22
#define WEREWOLF_DP_RISE 0x24  // a pounce: four times what its height gains
#define WEREWOLF_DP_SPAN 0x26  // ...the count its two gaps are stepped by,
#define WEREWOLF_DP_PART_X 0x28        // ...what is left over of each,
#define WEREWOLF_DP_PART_Y 0x2a
#define WEREWOLF_DP_GAP_X 0x2c         // ...the gaps,
#define WEREWOLF_DP_GAP_Y 0x2e
#define WEREWOLF_DP_SIGN_X 0x30        // ...and which way each is
#define WEREWOLF_DP_SIGN_Y 0x32
#define WEREWOLF_DP_GAP_ACROSS 0x36    // scratch: how far across its target is
#define WEREWOLF_DP_BLOW 0x38          // a strike's second record, or `$FFFF`
#define WEREWOLF_DP_HURT_SEEN 0x3a     // what `$3C` was when it last looked
#define WEREWOLF_DP_HURT 0x3c          // counted by what hits it
#define WEREWOLF_DP_KILLED 0x3e        // cleared when it leaves of itself
#define WEREWOLF_DP_TARGET 0x42        // whoever is nearest,
#define WEREWOLF_DP_TARGET_DIST 0x44   // ...and how far

// For the harness, and only for it: what happened, which with the path is
// what it takes to price the ROM's instructions around the calls.

// May it stand there? The ground is asked, and then who is there.
typedef struct {
  bool blocked;
  bool someone_asked;
  bool someone;
} WerewolfProbe;

// One step of a run.
typedef struct {
  bool left;             // its target is exactly 360 away: it leaves
  bool on_them;          // ...or on the same spot
  WerewolfProbe axis[2];  // across, then down
} WerewolfStep;

typedef enum {
  WEREWOLF_POUNCE_NOT_ASKED,
  WEREWOLF_POUNCE_NO_DRAW,   // the draw said no
  WEREWOLF_POUNCE_TOO_FAR,   // its target is 325 or more away
  WEREWOLF_POUNCE_TOO_NEAR,  // ...or under 70 by the sum of the gaps
  WEREWOLF_POUNCE_CHOOSES,   // it goes on to choose where, which is the ROM's
  WEREWOLF_POUNCE_HURT,      // hurt since it last looked: the same
} WerewolfPounce;

typedef enum {
  WEREWOLF_REACH_NOT_ASKED,
  WEREWOLF_REACH_TOO_FAR,   // its target is 325 or more away
  WEREWOLF_REACH_OFF_ROW,   // ...or 4 or more off its row
  WEREWOLF_REACH_OFF_SIDE,  // ...or 29 or more across
  WEREWOLF_REACH_WITHIN,    // it strikes
} WerewolfReach;

typedef enum {
  WEREWOLF_STRIKE_WAITED,     // not a pass with a new picture
  WEREWOLF_STRIKE_SHOWN,      // one of the first five
  WEREWOLF_STRIKE_BLOW_SHOWN, // ...or a later one, with the blow drawn
  WEREWOLF_STRIKE_OVER,       // the pictures ran out, which is the ROM's
} WerewolfStrike;

typedef enum {
  WEREWOLF_FLIGHT_UP,
  WEREWOLF_FLIGHT_DOWN_ALONE,   // down, having pounced at nobody
  WEREWOLF_FLIGHT_DOWN_MISSED,  // ...or at somebody who has moved 9 or more
  WEREWOLF_FLIGHT_DOWN_BESIDE,  // ...or beside them
} WerewolfFlight;

#define WEREWOLF_MAX_STEPS 2
#define WEREWOLF_MAX_GROUNDS 4

typedef struct {
  uint16_t state;  // the body that ran
  bool declined;   // the ROM's: see the top of this file

  // Running.
  int steps;
  WerewolfStep step[WEREWOLF_MAX_STEPS];
  bool new_picture;
  bool no_way;        // ...and it faces no way, so none is shown
  bool mask_clears;   // ...or the table's word for its record's flags clears
  WerewolfPounce pounce;
  bool gap_negative[2];
  WerewolfReach reach;
  bool reach_negative[2];  // down, then across
  uint16_t blow;           // the record taken for the blow
  bool blow_mask_clears;

  WerewolfStrike strike;

  // A pounce.
  bool falling;
  int glide_steps[2];
  WerewolfFlight flight;
  bool quarry_gap_negative;

  // What the bodies ask, summed over the pass.
  int draws;
  bool draw_overflow[2];
  ActorNearestWork nearest;
  int bearings;
  ActorSnapRegs snap[WEREWOLF_MAX_STEPS];
  ActorBearingRegs bearing[WEREWOLF_MAX_STEPS];
  int grounds;
  TerrainRegs ground[WEREWOLF_MAX_GROUNDS];
  AtPointWork at_point;

  bool c, v;          // carry and overflow as the pass leaves them
  bool c_set, v_set;  // ...or the thread's own, where it wrote none
} WerewolfLog;

typedef enum {
  WEREWOLF_SLEEPS,
  WEREWOLF_ENDS,
} WerewolfFate;

// Can `werewolf_frame` take this pass? Only in a state it knows, facing a way
// the tables have, with a pounce that ends. It only looks.
bool werewolf_frame_supported(const Wram* w, uint16_t page);

// One pass, for the creature whose page is `page`. `log` may be NULL, and
// `log->declined` says the pass is the ROM's after all. WRAM is then part
// written.
WerewolfFate werewolf_frame(Wram* w, const Rom* rom, uint16_t page,
                          WerewolfLog* log);

#endif
