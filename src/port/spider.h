// The spiders of level 17, which skitter along the walls until something
// comes near and then run at it.
//
// A spider is a thread, `$83:B277`. Its loop sleeps two ticks, runs one of
// three state bodies, and then shows its picture. This is one pass of that
// loop as readable C, from where `thread_yield` returns to the next yield:
//
//   $83:B299  frame   the state body, then the picture
//
// What is not here is the thread's setup and its death. Nothing in a pass
// sleeps, so every pass in a state it knows is the port's.
//
// ## What a spider does
//
// **It wanders in a straight line** until the ground or somebody stops it.
// Then it turns a quarter clockwise, and from then on it feels its way: each
// pass it tries the turn back anticlockwise first, and takes it if that way
// is clear, so it follows the wall it met.
//
// **It looks every pass.** With whoever `actor_nearest` knows under `$B4`
// away, it takes aim. With them `$F0` or more away and neither player within
// that either, it leaves the level.
//
// **Taking aim** it finds them again, and gives up if they are `$DC` or more
// away. Otherwise it faces them, or one turn of eight clockwise of them by a
// draw, and runs that way for up to seven passes, by the same draw. Then it
// takes aim again.
//
// **Running, it steps once or twice a pass**, by a draw each pass, and each
// axis of a step is taken by itself if it may be, so it slides along what it
// runs into. Wandering it steps once, both axes or neither.
//
// **A step is three pixels on each axis.**
//
// ## Directions
//
// The thread keeps its direction doubled: 2 up, then clockwise in twos to 16
// for up and left. `actor_bearing` answers 1 to 8 the same way round, and 0
// for the same spot, which a spider taking aim turns into up and left.
//
// ## Its contract with the ROM
//
// It writes WRAM exactly as the ROM does. A frame ends at the `JSL
// thread_yield` with the tick count in A, or past the test of its fate with
// that in A. Carry and overflow are left as the ROM leaves them.
//
// Port code: libc only.

#ifndef PORT_SPIDER_H
#define PORT_SPIDER_H

#include <stdbool.h>
#include <stdint.h>

#include "assets/rom.h"
#include "port/oam.h"  // the works and registers of what it asks
#include "port/terrain.h"
#include "port/wram.h"

// The tables are in bank `$83`, which is the thread's data bank.
#define SPIDER_BANK 0x83u

#define SPIDER_FRAME_PC 0x83b299u
#define SPIDER_YIELD_PC 0x83b295u  // `JSL thread_yield`, A already 2
#define SPIDER_FATE_PC 0x83b2a8u   // past the `BEQ`, its fate in A
#define SPIDER_YIELD_TICKS 2

// The state bodies, by the address the thread keeps in `$1A`.
#define SPIDER_STATE_WANDER 0xb46bu
#define SPIDER_STATE_FEEL 0xb499u
#define SPIDER_STATE_RUN 0xb4fdu

// Fields on the spider's page.
#define SPIDER_DP_RECORD 0x08
#define SPIDER_DP_X 0x0a
#define SPIDER_DP_Y 0x0c
#define SPIDER_DP_TRY_X 0x0e      // the step being tried
#define SPIDER_DP_TRY_Y 0x10
#define SPIDER_DP_TARGET 0x12     // the record it took aim at
#define SPIDER_DP_DIRECTION 0x14  // doubled
#define SPIDER_DP_OTHER_WAY 0x16  // feeling: the turn back it tried
#define SPIDER_DP_RUN_LEFT 0x18   // running: passes until it takes aim again
#define SPIDER_DP_STATE 0x1a
#define SPIDER_DP_CYCLE 0x1c      // which of two pictures
#define SPIDER_DP_PICTURE_TIMER 0x1e
#define SPIDER_DP_FATE 0x20       // 0 alive, anything else and the thread ends
#define SPIDER_DP_HIT_BY 0x24     // cleared every pass
#define SPIDER_DP_BEARING 0x26    // taking aim: the way to its target, less one

// For the harness, and only for it: what happened, which with the path is
// what it takes to price the ROM's instructions around the calls.
typedef struct {
  bool asked;        // this test ran
  TerrainRegs ground;
  bool someone;      // the ground was clear, and someone was standing there
} SpiderProbe;

typedef enum {
  SPIDER_LOOK_NONE,    // it did not look this pass
  SPIDER_LOOK_NEAR,    // someone close: it took aim
  SPIDER_LOOK_MIDDLE,  // someone, not close
  SPIDER_LOOK_FAR,     // nobody, with a player within reach
  SPIDER_LOOK_LEAVE,   // ...or with neither
} SpiderLook;

#define SPIDER_MAX_STEPS 2

typedef struct {
  uint16_t state;             // the body that ran
  SpiderLook look;
  ActorNearestWork nearest;   // looking: what `actor_nearest` did
  PlayerPickRegs players;     // ...and `player_bearing`, when it was asked
  bool aimed;                 // it took aim...
  ActorNearestWork aim_nearest;
  bool gave_up;               // ...and its target was too far
  ActorBearingRegs bearing;   // ...or was this way
  bool aim_overflow;          // ...and the draw overflowed, which costs more
  SpiderProbe back;           // feeling: the turn back
  bool turned_back;           // ...which was clear
  SpiderProbe ahead;          // wandering and feeling: the step
  bool ran;                   // running: it did not take aim this pass
  bool run_overflow;          // ...the draw for how many steps
  int steps;                  // ...and how many
  SpiderProbe step[SPIDER_MAX_STEPS][2];  // ...each one's two axes
  AtPointWork at_point;       // summed over every test of who is there
  bool new_picture;           // the picture's timer ran out
  bool mirrored;              // ...and the new one faces left
  bool c, v;                  // carry and overflow as the frame leaves them
  bool c_set, v_set;          // ...or the thread's own, where it wrote none
} SpiderLog;

// Can `spider_frame` take this pass? Not a state it does not know, nor a
// direction the tables do not reach. It only looks.
bool spider_frame_supported(const Wram* w, uint16_t page);

// One pass, for the spider whose page is `page`. `carry` is the thread's
// own, as it woke with it: a running spider's draw begins from it. False
// when its fate is no longer zero, and the thread is to end. `log` may be
// NULL.
bool spider_frame(Wram* w, const Rom* rom, uint16_t page, bool carry,
                  SpiderLog* log);

#endif
