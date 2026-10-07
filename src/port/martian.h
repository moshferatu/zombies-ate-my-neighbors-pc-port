// The martians of level 21, who walk about with a bubble gun and shoot along
// rows and columns.
//
// A martian is a thread. Two loops run the same state bodies: `$81:99DF` for
// one that begins on the ground, and `$81:9A3E` for one that comes in over
// the top of the screen. Each sleeps a tick, runs the state, and looks at its
// fate. This is one pass of either, as readable C, from where `thread_yield`
// returns to the next yield:
//
//   $81:99F6  frame   the walker's loop
//   $81:9A5A  frame   the arrival's loop
//
// **A shot sleeps twelve ticks in the middle of the call that fires it.**
// So a pass that fires is two stretches. A walker's are both here: the frame
// ends at the `JSL` that asks for the shot's thread, three return addresses
// deep, and the rest of the pass is an entry of its own, at the `RTS` the
// sleep comes back to:
//
//   $81:99C9  wake    the rest of a walking pass that fired
//
// One that fires while it is arriving is left to the ROM, both halves.
// Whether a pass fires can be down to a random draw, so the port finds out
// by running it: `MartianLog::declined` says so, and a guard asks it of a
// scratch copy first.
//
// Two pieces of such a pass are here all the same, each for the ROM to call
// as it goes:
//
//   $81:9981  shoot   as far as the shot's thread being asked for, or the
//                     `RTS` when it is still cooling
//   $81:9C99  show    the walking picture, to its `RTS`
//
// And the thread either side of its loop:
//
//   $81:9AC0  begin   its record and its page, as far as its first pictures
//   $81:9A17  end     its weight given back: `record_end` in `port/begin.h`
//   $81:9A7B  end     ...and the arrival loop's copy
//
// ## What a martian does
//
// **It shoots at whatever is lined up with it.** Every pass it asks
// `actor_aligned` whether a player or a neighbour is within a tile of its row
// or its column. If one is, it fires that way, unless it fired in the last
// sixty passes, when it only counts one of them off.
//
// **Walking, it keeps its distance.** Once in sixty passes it finds whoever
// `actor_nearest` knows and picks a way to go, which it keeps until the next
// look:
//
//   * under `$3C` away, it backs off, straight away from them;
//   * under `$50`, it lines up: along whichever axis it is nearer on, so as
//     to close that gap and have them in its row or column;
//   * under `$E0`, it comes closer;
//   * beyond that it stands, and if neither player is within reach of it, it
//     leaves the level.
//
// **Arriving, it stays above them.** It keeps to one side of its target,
// chosen again every thirty-two passes, and holds itself between `$60` and
// `$78` above: nearer than that it climbs on a slant, further it comes down
// on one, and in between it goes straight across. On a slant it steps twice
// a pass. It fires downwards at random, about one pass in nine when it can.
// As soon as either player is above it, it becomes a walker.
//
// **A step is two pixels on each axis, three passes in four.** Each axis is
// tested by itself, against the ground, the level's edges and whoever is
// standing there, and the second from wherever the first left it. That is
// `actor_step_bearing` in `port/step.h`, written out again here so that the
// harness can be told what each test answered.
//
// ## Directions
//
// The thread keeps its direction doubled: 2 up, then clockwise in twos to 16
// for up and left, and 0 for standing. `actor_bearing` answers 1 to 8 the
// same way round.
//
// **A martian that has never chosen one has none.** The thread's setup does
// not clear the direction, and a first look that finds everything too far
// away returns without choosing. For the sixty passes until the next look it
// steps by whatever the tables hold far past their ends, at whatever was on
// its page. The port reads the same words, so it goes the same way.
//
// ## Its contract with the ROM
//
// It writes WRAM exactly as the ROM does. A frame ends at the `JSL
// thread_yield` with the tick count in A, or past the test of its fate with
// that in A. Carry and overflow are left as the ROM leaves them, where the
// port follows them: see `MartianLog`.
//
// Port code: libc only.

#ifndef PORT_MARTIAN_H
#define PORT_MARTIAN_H

#include <stdbool.h>
#include <stdint.h>

#include "assets/rom.h"
#include "port/cpu.h"
#include "port/oam.h"  // the works and registers of what it asks
#include "port/terrain.h"
#include "port/wram.h"

// The tables are in bank `$81`, which is the thread's data bank.
#define MARTIAN_BANK 0x81u

typedef enum {
  MARTIAN_LOOP_WALKER,   // `$81:99DF`
  MARTIAN_LOOP_ARRIVAL,  // `$81:9A3E`
  MARTIAN_LOOP_COUNT,
} MartianLoop;

#define MARTIAN_WALKER_FRAME_PC 0x8199f6u
#define MARTIAN_WALKER_YIELD_PC 0x8199f2u  // `JSL thread_yield`, A already 1
#define MARTIAN_WALKER_FATE_PC 0x819a02u   // past the `BEQ`, its fate in A
#define MARTIAN_ARRIVAL_FRAME_PC 0x819a5au
#define MARTIAN_ARRIVAL_YIELD_PC 0x819a56u
#define MARTIAN_ARRIVAL_FATE_PC 0x819a66u
#define MARTIAN_YIELD_TICKS 1

// The state bodies, by the address the thread keeps in `$0A`.
#define MARTIAN_STATE_WALK 0x9d30u
#define MARTIAN_STATE_ARRIVE 0x9dbbu
// What it fires with, by the address in `$22`.
#define MARTIAN_SHOOT 0x9981u

#define MARTIAN_SHOOT_PC 0x819981u
#define MARTIAN_SHOOT_SPAWN_PC 0x8199beu  // `JSL thread_spawn`, A and Y set
#define MARTIAN_SHOOT_RTS_PC 0x8199ccu    // cooling: the `RTS`
#define MARTIAN_SHOW_PC 0x819c99u
#define MARTIAN_SHOW_RTS_PC 0x819cd6u
#define MARTIAN_WAKE_PC 0x8199c9u         // the `RTS` after the shot's sleep
#define MARTIAN_BEGIN_PC 0x819ac0u
#define MARTIAN_BEGIN_PLAY_PC 0x819b2cu   // `JSL pictures_play`, the list in A
// The thread a shot is: see `port/bubble.h`.
#define MARTIAN_SHOT_THREAD 0xf380u
// What a martian begins with.
#define MARTIAN_START_PICTURE 0xd66du
#define MARTIAN_PICTURE_BANK 0x0090
#define MARTIAN_COLLIDE_ID 0x0003
#define MARTIAN_START_ATTR 0x0c00
#define MARTIAN_START_PICTURES 0x9969u    // the list `$14` names
#define MARTIAN_FIRST_PICTURES 0x9b3bu    // ...and what it plays first

// Fields on the martian's page.
#define MARTIAN_DP_SHOT_X 0x00      // where a shot starts from
#define MARTIAN_DP_SHOT_Y 0x02
#define MARTIAN_DP_SHOT_WAY 0x04    // ...and which way, doubled
#define MARTIAN_DP_SHOT_SLOT 0x06   // `$FFFF` until the shot's thread has one
#define MARTIAN_DP_RECORD 0x08
#define MARTIAN_DP_STATE 0x0a
#define MARTIAN_DP_X 0x0c
#define MARTIAN_DP_Y 0x0e
#define MARTIAN_DP_TRY_X 0x10       // the step being tried
#define MARTIAN_DP_TRY_Y 0x12
#define MARTIAN_DP_PICTURES 0x14    // its list of pictures, in bank `$81`
#define MARTIAN_DP_TARGET 0x16      // the record `actor_nearest` found
#define MARTIAN_DP_TARGET_DIST 0x18
#define MARTIAN_DP_GAP_X 0x1c       // the gaps `actor_nearest` left behind
#define MARTIAN_DP_GAP_Y 0x1e
#define MARTIAN_DP_GAP_WIDTH 0x20   // lining up: the gap across, unsigned
#define MARTIAN_DP_SHOOT 0x22
#define MARTIAN_DP_COOLDOWN 0x24    // passes until it may fire again
#define MARTIAN_DP_FATE 0x26        // 0 alive, anything else and the thread ends
#define MARTIAN_DP_DIRECTION 0x28   // doubled
#define MARTIAN_DP_LOOK_TIMER 0x2a  // walking: passes until it looks again
#define MARTIAN_DP_KILLED 0x30      // not zero when a player killed it
#define MARTIAN_DP_32 0x32          // cleared at the start; I have not read its use
#define MARTIAN_DP_7E 0x7e          // likewise
#define MARTIAN_DP_CYCLE 0x2c       // which of four pictures
#define MARTIAN_DP_PICTURE_TIMER 0x2e
#define MARTIAN_DP_SIDE_TIMER 0x34  // arriving: passes until it picks a side
#define MARTIAN_DP_SIDE 0x36        // ...and the side, a doubled direction

// For the harness, and only for it: what happened, which with the path is
// what it takes to price the ROM's instructions around the calls.

// One axis of a step: the three tests, each asked only if the one before it
// let the step by.
typedef struct {
  TerrainRegs ground;
  bool edge_asked;
  BoundsExit edge;
  bool outside;
  bool someone_asked;
  bool someone;
} MartianProbe;

typedef struct {
  bool rested;           // the one pass in four on which nothing moves
  MartianProbe axis[2];  // across, then down
  bool new_picture;      // arriving: the picture's timer ran out after it
} MartianStep;

typedef enum {
  MARTIAN_LOOK_NONE,      // the timer had not run out
  MARTIAN_LOOK_BACK_OFF,  // too close
  MARTIAN_LOOK_LINE_UP,
  MARTIAN_LOOK_APPROACH,
  MARTIAN_LOOK_STAND,     // too far, with a player within reach
  MARTIAN_LOOK_LEAVE,     // ...or with neither
} MartianLook;

typedef enum {
  MARTIAN_HEIGHT_KEEP,   // between the two: straight across
  MARTIAN_HEIGHT_CLIMB,  // too near below it
  MARTIAN_HEIGHT_DROP,   // too far
} MartianHeight;

#define MARTIAN_MAX_STEPS 2

typedef struct {
  uint16_t state;             // the body that ran
  bool aligned_asked;
  ActorAlignedRegs aligned;   // what `actor_aligned` did
  bool held_fire;             // something was lined up, and it was cooling
  bool declined;              // the ROM's: it fired while arriving
  bool fired;                 // walking, it fired: the frame ends at the spawn
  uint16_t shot_way;          // ...this way,
  bool shot_mirrored;         // ...which is to the left
  MartianLook look;
  ActorNearestWork nearest;   // looking: what `actor_nearest` did
  ActorSnapRegs snap;         // ...and `actor_snap_to` and `actor_bearing`
  ActorBearingRegs bearing;
  PlayerPickRegs players;     // what `player_bearing` did, when it was asked
  bool gap_x_negative;        // lining up: the gaps' signs,
  bool gap_y_negative;
  bool along_y;               // ...the axis it is nearer on,
  int lesser_sign;            // ...and that gap's sign, or 0 for none
  int steps;                  // none, one, or two on a slant
  MartianStep step[MARTIAN_MAX_STEPS];
  AtPointWork at_point;       // summed over every test of who is there
  bool capped;                // walking: the picture's number was out of range
  bool mirrored;              // ...and it faces left
  bool new_picture;           // ...and its timer ran out
  bool nobody;                // arriving: neither player is within reach
  bool player[2];             // ...each player there is, and whether they
  bool player_above[2];       //    are above it
  bool came_down;             // ...so it is a walker from now
  bool drew;                  // ...nothing lined up, so a draw
  bool drew_overflow;
  bool drew_fire;             // ...which said fire
  bool picked_side;           // ...the side's timer ran out
  bool target_close;          // ...with the target close, so the far side
  bool side_left;
  bool height_negative;       // ...the target is above it
  MartianHeight height;
  bool slant_left;
  bool c, v;                  // carry and overflow as the frame leaves them
  bool c_set, v_set;          // ...or the thread's own, where it wrote none
} MartianLog;

// Can `martian_frame` take this pass? Not a state it does not know, nor a
// direction that would read the tables from outside the cartridge's bank.
// It only looks.
bool martian_frame_supported(const Wram* w, uint16_t page);

// One pass, for the martian whose page is `page`. False when its fate is no
// longer zero, and the thread is to end. `log` may be NULL.
//
// `log->declined` says the pass is the ROM's after all. WRAM is then part
// written, which is why a guard asks on a scratch copy first.
//
// `log->fired` says a walker fired, and the pass has got as far as asking
// for the shot's thread. What it returns is then no matter.
bool martian_frame(Wram* w, const Rom* rom, uint16_t page, MartianLog* log);

// `$81:99C9`: the rest of a walking pass that fired, from where its sleep
// comes back. False when its fate is no longer zero.
bool martian_wake(Wram* w, const Rom* rom, uint16_t page, MartianLog* log);

// `$81:9AC0`, called by either loop's start with the place in `$00` and
// `$02`. False with no record free, which is the ROM's: it only looks then.
bool martian_begin(Wram* w, PortCpu* c, uint16_t* record);

// `$81:9C99`, called: the picture for the way it faces. `log` says what it
// did, in `capped`, `mirrored` and `new_picture`.
bool martian_show_supported(const Wram* w, uint16_t page);
void martian_show_walking(Wram* w, const Rom* rom, PortCpu* c,
                          MartianLog* log);

// `$81:9981`, called with the way in A. It ends at the `JSL` that asks for
// the shot's thread, having put up the picture it fires in, or at the `RTS`
// with one counted off.
typedef struct {
  bool fired;
  bool mirrored;
} MartianShot;

bool martian_shoot_supported(const Wram* w, const Rom* rom, uint16_t page,
                             uint16_t way);
void martian_shoot(Wram* w, const Rom* rom, PortCpu* c, MartianShot* shot);

#endif
