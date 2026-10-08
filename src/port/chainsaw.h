// The chainsaw maniac, who cuts his way through hedges to get at somebody.
//
// It is a thread, `$81:983A`. Its loop sleeps two ticks, runs one of its
// state bodies, and shows itself. This is one pass of that loop as readable
// C, from where `thread_yield` returns to the next yield:
//
//   $81:9878  chainsaw_frame
//
// What is here is how it gets about, four of its states, and the swing it
// makes at somebody beside it. What is not is the thread's setup, the
// cutting itself and being hit, each of which sleeps in the middle. A pass
// in one of those states is the ROM's.
//
// The swing sleeps in the middle too, so it is two stretches:
//
//   $81:9878  chainsaw_frame       in the swing's state: as far as its first
//                                  picture's sleep
//   $81:9598  chainsaw_swing_next  where it wakes: the next picture and
//                                  another sleep, or the swing's end and
//                                  the rest of the pass
//
// ## What it does
//
// **It keeps its place in quarters of a pixel**, and a step is seven of them
// along one of eight ways, or six on each axis of a slanted one. Each axis is
// taken by itself, if the ground there may be walked on and nobody is
// standing there.
//
// **Wandering**, with whoever `actor_nearest` knows 300 or more away, it
// strides on the way it faces. Nearer than that it gives chase.
//
// **A stride** first tries a quarter turn, except on a frame whose count is
// a multiple of four, and takes it if the next step that way is clear. So it
// turns the same way at every opening. Then it steps, and if either axis was
// refused it turns back the other way.
//
// **Chasing**, it faces whoever is nearest and steps twice a pass. A step
// that is refused and gets it less than two pixels is counted, and after 120
// of those it gives up and charges. So it does with nobody within 250.
// With somebody within 32 it swings, on a draw.
//
// **A swing is a full turn with the saw held out**: eight pictures, three
// ticks each, starting with the way it faces and going round. The saw is a
// record of its own with no picture, fifteen to nineteen pixels out, which
// is what hurts. Its picture's place in the cycle is borrowed to count the
// turn, and is left wherever the turn began, which is seldom a place in the
// cycle. The next picture it shows puts that right.
//
// **Charging**, it goes straight on until the ground stops it, and then
// turns a quarter left or right, by a draw.
//
// **Turned**, it strides for as many passes as it had counted, and then
// wanders.
//
// **Chasing or turned, it looks for a hedge.** After a step it asks whether
// the tile above it, to either side or below can be cut, and if one can it
// begins to cut there.
//
// ## Its contract with the ROM
//
// It writes WRAM exactly as the ROM does. A pass ends at the `JSL
// thread_yield` with the tick count in A, or past the test of its health
// with that in A. Carry and overflow are left as the ROM leaves them, which
// matters: the thread's own carry is the first thing a chase's draw takes.
//
// Port code: libc only.

#ifndef PORT_CHAINSAW_H
#define PORT_CHAINSAW_H

#include <stdbool.h>
#include <stdint.h>

#include "assets/rom.h"
#include "port/cpu.h"
#include "port/oam.h"  // the works and registers of what it asks
#include "port/terrain.h"
#include "port/wram.h"

// The tables are in bank `$81`, which is the thread's data bank.
#define CHAINSAW_BANK 0x81u

#define CHAINSAW_FRAME_PC 0x819878u
#define CHAINSAW_YIELD_PC 0x819874u  // `JSL thread_yield`, A already 2
#define CHAINSAW_DEAD_PC 0x819887u   // past the `BPL`, its health in A
#define CHAINSAW_YIELD_TICKS 2

// The state bodies, by the address the thread keeps in `$12`.
#define CHAINSAW_STATE_CHARGE 0x9195u
#define CHAINSAW_STATE_WANDER 0x921bu
#define CHAINSAW_STATE_TURNED 0x9235u
#define CHAINSAW_STATE_CHASE 0x9256u
#define CHAINSAW_STATE_SWING 0x9550u
// ...and one the port leaves to the ROM.
#define CHAINSAW_STATE_CUT 0x92d6u

// The swing's own stops.
#define CHAINSAW_SWING_YIELD_PC 0x819594u  // `JSL thread_yield`, A already 3
#define CHAINSAW_SWING_NEXT_PC 0x819598u   // where it wakes
#define CHAINSAW_SWING_TICKS 3
// What the pass's computed `RTS` leaves under a state's body.
#define CHAINSAW_PASS_RETURN 0x987fu
// ...and what the pass's `JSR` to its picture then writes over it.
#define CHAINSAW_PASS_SHOWN_RETURN 0x9882u

// Fields on its page.
#define CHAINSAW_DP_RECORD 0x08
#define CHAINSAW_DP_PICTURE_WAIT 0x0a  // passes until its next picture
#define CHAINSAW_DP_PICTURE 0x0c       // which of four
#define CHAINSAW_DP_WAY 0x0e           // a bearing, doubled
#define CHAINSAW_DP_TRY_WAY 0x10       // the turn being tried
#define CHAINSAW_DP_STATE 0x12
#define CHAINSAW_DP_X 0x16
#define CHAINSAW_DP_Y 0x18
#define CHAINSAW_DP_FINE_X 0x1a        // the same, in quarters of a pixel
#define CHAINSAW_DP_FINE_Y 0x1c
#define CHAINSAW_DP_TRY_FINE_X 0x1e    // the step being tried
#define CHAINSAW_DP_TRY_FINE_Y 0x20
#define CHAINSAW_DP_TRY_X 0x22
#define CHAINSAW_DP_TRY_Y 0x24
#define CHAINSAW_DP_MOVED 0x28         // how far the last step got it
#define CHAINSAW_DP_HEALTH 0x2a        // negative and the thread ends
#define CHAINSAW_DP_COUNT 0x30         // chasing: steps refused. Turned: passes
#define CHAINSAW_DP_SWING_LEFT 0x36    // pictures of a swing still to show
#define CHAINSAW_DP_TURN 0x38          // a quarter turn, one way or the other
#define CHAINSAW_DP_STEP_AT 0x3a       // chasing: its way, doubled twice
#define CHAINSAW_DP_REFUSED 0x3e       // how many axes of the last step failed
#define CHAINSAW_DP_SAW 0x40           // the saw's record in a swing, or $FFFF
#define CHAINSAW_DP_CUT_X 0x42         // where the hedge is,
#define CHAINSAW_DP_CUT_Y 0x44
#define CHAINSAW_DP_CUT_SIDE 0x46      // ...and on which side of it

// For the harness, and only for it: what happened, which with the path is
// what it takes to price the ROM's instructions around the calls.

// May it stand there? The ground is asked, and then who is there.
typedef struct {
  bool blocked;
  bool someone_asked;
  bool someone;
} ChainsawProbe;

// A step, an axis at a time.
typedef struct {
  ChainsawProbe axis[2];  // across, then down
  bool backwards;         // the sum of its place went down
  bool refused;           // either axis was
} ChainsawStep;

// Looking for a hedge beside it.
typedef struct {
  bool asked;
  int tiles;   // how many of the four were looked at
  bool found;  // ...and the last of them can be cut
} ChainsawHedge;

typedef struct {
  bool asked;
  bool turn_asked;     // not a frame that tries no turn
  ChainsawProbe turn;
  ChainsawStep step;
  bool turned_back;
} ChainsawStride;

typedef struct {
  ChainsawStep step;
  bool short_of;  // refused, and it got less than two pixels
  bool gave_up;   // ...for the 120th time
  ChainsawHedge hedge;
} ChainsawChaseStep;

#define CHAINSAW_MAX_GROUNDS 6
#define CHAINSAW_CHASE_STEPS 2

typedef struct {
  uint16_t state;  // the body that ran

  // Charging.
  ChainsawProbe ahead;
  bool turned_left;      // stopped, and the draw said left
  bool count_negative;  // stopped, with no count: it turns back and wanders

  // Wandering.
  bool someone_near;

  // Turned.
  bool time_up;
  ChainsawStride stride;
  ChainsawHedge hedge;

  // Chasing.
  bool swing_drawn;  // the draw let it swing
  bool swung;        // ...and somebody was beside it
  bool nobody;       // nobody near enough to chase
  bool on_them;      // ...or it is where they are
  int chase_steps;
  ChainsawChaseStep chase[CHAINSAW_CHASE_STEPS];

  // What the bodies ask, summed over the pass.
  int draws;
  bool draw_overflow;
  int nearests;
  ActorNearestWork nearest;
  bool bearing_asked;
  ActorSnapRegs snap;
  ActorBearingRegs bearing;
  int grounds;
  TerrainRegs ground[CHAINSAW_MAX_GROUNDS];
  AtPointWork at_point;
  int cuts_begun;

  // Showing itself.
  bool new_picture;
  bool mirrored;

  bool c, v;          // carry and overflow as the pass leaves them
  bool c_set, v_set;  // ...or the thread's own, where it wrote none
} ChainsawLog;

typedef enum {
  CHAINSAW_SLEEPS,
  CHAINSAW_ENDS,  // its health ran out
} ChainsawFate;

// Can `chainsaw_frame` take this pass? Only in a state it knows, facing a
// way the tables have. It only looks. A pass in the swing's state is
// `chainsaw_swing_begin`'s.
bool chainsaw_frame_supported(const Wram* w, uint16_t page);

// What a stretch of the swing did.
typedef struct {
  bool declined;    // no record for the saw: the ROM's to go on with
  uint16_t record;  // the one it took
  bool wrapped;     // the turn went past the table's first way
  bool more;        // another picture
  int free_place;   // where in the display list the saw's record was
  ChainsawLog pass; // the rest of the pass, when the swing ended
  ChainsawFate fate;
} ChainsawSwingLog;

// A pass in the swing's state, as far as the first picture's sleep. It
// leaves the pass's return on the stack, as the ROM has it there.
void chainsaw_swing_begin(Wram* w, const Rom* rom, PortCpu* c,
                          ChainsawSwingLog* log);

// Can `chainsaw_swing_next` take it from here? It only looks.
bool chainsaw_swing_next_supported(const Wram* w, uint16_t page, uint16_t s);

// Where the swing wakes. The next picture and its sleep, or the saw's
// record freed, a chase begun, and the rest of the pass.
void chainsaw_swing_next(Wram* w, const Rom* rom, PortCpu* c,
                         ChainsawSwingLog* log);

// One pass, for the creature whose page is `page`. `carry` is the thread's
// own, as it woke. `log` may be NULL.
ChainsawFate chainsaw_frame(Wram* w, const Rom* rom, uint16_t page, bool carry,
                            ChainsawLog* log);

#endif
