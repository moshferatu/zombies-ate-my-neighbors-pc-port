// The fishmen, who swim about a pool and leap out of it at somebody.
//
// A fishman is a thread, and there are two kinds: `$81:E481`, which comes
// ashore when it leaps, and `$81:E51A`, which keeps to its pool and patrols it.
// Both begin only on a tile of water. Each has a loop of its own that sleeps,
// runs the same state bodies, and shows itself. This is one pass of either,
// as readable C, from where `thread_yield` returns to the next yield:
//
//   $81:E4B2  fishman_frame          the one that comes ashore
//   $81:E558  fishman_patrol_frame   the one that patrols
//
// The one that patrols is begun and ended many times a level: whatever
// places it asks again and again, and a fishman with neither player within
// 208 leaves on its first pass. So its beginning and its plain end are here
// too:
//
//   $81:E51A  fishman_patrol_begin   from the thread's first instruction to
//                                    its first sleep, or to its `RTL` when
//                                    there is no room or the place is not
//                                    water
//   $81:E567  fishman_patrol_end     from the test of its fate to the `RTL`,
//                                    for one that left of itself
//
// The test of the spot a leap is to come down on is an entry of its own:
//
//   $81:E1D6  fishman_landing        are all six tiles under a point
//                                    somewhere to land? To either `RTS`.
//
// **Four bodies sleep in the middle**, or show a list of pictures, which
// sleeps too. A pass that gets to one stops there, with the returns of the
// body still on the thread's stack, and what is left of the pass is a
// stretch of its own that begins where the thread wakes:
//
//   $81:E076  fishman_leap_wake      a leap was decided on, and it waited a
//                                    few ticks: the leap is set up, and the
//                                    pass is finished
//   $81:DB10  fishman_dive_wake      the same for the dive back, which is
//                                    decided on in the middle of a pass on
//                                    land
//   $81:E346  fishman_sweep_begin    landed, and three pictures shown: its
//                                    second record, to the first sleep
//   $81:E381  fishman_sweep_tick     from where that sleep comes back to the
//                                    next, or to its second record freed
//                                    and a second list of pictures
//   $81:E397  fishman_sweep_after    after those: it walks
//   $81:DBE1  fishman_splash_after   back in the water, and two pictures
//                                    shown: it swims
//
// **A leap leaves a splash behind it**, where it left the water. That is a
// thread of its own, `$81:E72C`: a record with nothing to hit, a list of
// pictures, and its end, which is `port/begin.h`'s.
//
//   $81:E72C  fishman_splash_begin   from the thread's first instruction to
//                                    its pictures
//
// **That state sweeps a second record round it.** It takes a record with no
// picture and collide id 3, and for five ticks puts it at the next of eight
// places in a ring about itself: 24 to either side, or 8 above or below and
// up to 8 across. It starts from the way its target is. By what the code
// does that is a swipe at whoever is next to it. I have not seen it on a
// screen, and the name the port had for the state, a lurk, was a guess.
//
// What is here is the swimming, a leap out and the dive back from the
// deciding to the splash, the landing, the sweep, and the walk on land of the
// one that keeps to its pool. What is not is the setup of the one that comes
// ashore, its bite, what that one does on land, and an end that is not the
// plain one. A pass in one of those states is the ROM's. So is a pass that
// stops to look about, and one on land that is stopped by the ground and
// finds water just past it. Those are down to a random draw, so the port
// finds out by running the pass: `FishmanLog::declined` says so, and a guard
// asks it of a scratch copy first.
//
// ## What it does
//
// **It may only be where all six tiles under it are water.** That is the
// footprint test of `port/terrain.h` with another bit and the opposite sense,
// and the creature carries its own copy of it, `$81:DC24`.
//
// **Swimming**, it goes two pixels a pass the way it faces. Stopped, it turns
// a quarter and for a while tries a quarter turn the other way at every
// opening, which takes it round the edge of the pool. With somebody within
// 128 it closes in on them.
//
// **Closing in**, it faces whoever `actor_nearest` knows and steps at them,
// an axis at a time, and on three draws in four it steps twice. Within 24 it
// bites. With nobody within 175 it swims on.
//
// **Patrolling**, it goes back and forth along one line, turning about when
// stopped. With somebody within 24 of its row or its column it lines up with
// them along the other axis, a pixel a pass or two, until it is within 16.
//
// **On land**, the one that keeps to its pool walks at whoever is nearest,
// two pixels a pass, an axis at a time, over ground that may be walked on
// and has nobody on it. First it asks `port/wander.h` for water to go back
// to, which is the dive. Stopped by the ground, it looks for water a little
// past where it was stopped, by a draw, and that is the dive too. With
// somebody within 28 and eight or more across from it, it sweeps again. With
// nobody within 175 it leaves, or goes about as the other one does.
//
// **The dive waits to be asked for.** `port/wander.h` looks for water only
// when two words on its page differ. They begin the same. The end of a
// sweep doubles one of them, and finding water makes them the same again.
// So it walks until it has swept once, and then goes back at the first water
// a draw finds.
//
// **Every pass in water it may leap**, on a draw, at somebody within 200. It
// picks a spot near them, by three more draws, and leaps if the spot is on
// the level, is somewhere to land, and has nobody on it.
//
// **A leap is an arc**: so many steps along the longer of the two gaps, the
// other kept in proportion, and a height that rises by a quarter of a count
// that falls by one each pass. It has landed when its height is back to
// nothing. The dive back into the water is the same arc, and the same
// instructions at another address.
//
// **With neither player within 208 it leaves**, whatever it was doing.
//
// ## Its contract with the ROM
//
// It writes WRAM exactly as the ROM does. A pass ends at the `JSL
// thread_yield` with the tick count in A, or past the test of its fate with
// that in A. Carry and overflow are left as the ROM leaves them: the thread's
// own carry is the first thing a draw takes.
//
// Port code: libc only.

#ifndef PORT_FISHMAN_H
#define PORT_FISHMAN_H

#include <stdbool.h>
#include <stdint.h>

#include "assets/rom.h"
#include "port/oam.h"  // the works and registers of what it asks
#include "port/terrain.h"
#include "port/wander.h"
#include "port/wram.h"

// The tables are in bank `$81`, which is the thread's data bank.
#define FISHMAN_BANK 0x81u

#define FISHMAN_FRAME_PC 0x81e4b2u
#define FISHMAN_YIELD_PC 0x81e4aeu  // `JSL thread_yield`, the ticks in A
#define FISHMAN_FATE_PC 0x81e4c1u   // past the `BEQ`, its fate in A
#define FISHMAN_PATROL_FRAME_PC 0x81e558u
#define FISHMAN_PATROL_YIELD_PC 0x81e554u
#define FISHMAN_PATROL_FATE_PC 0x81e567u

// A loop pushes a return and then its state's address, and goes to the
// body by an `RTS`. The body's own `RTS` is to the loop's show.
typedef struct {
  uint32_t yield_pc;      // `JSL thread_yield`, the ticks in A
  uint32_t fate_pc;       // past the `BEQ`, its fate in A
  uint16_t body_return;   // what it pushes under a body
  uint16_t shown_return;  // ...and what its `JSR` to the show pushes
} FishmanLoop;

// The one that comes ashore, and the one that patrols.
extern const FishmanLoop FISHMAN_LOOP;
extern const FishmanLoop FISHMAN_PATROL_LOOP;

// Where a body stops in the middle.
#define FISHMAN_LEAP_SLEEP_PC 0x81e072u    // `JSL thread_yield`
#define FISHMAN_LEAP_WAKE_PC 0x81e076u
#define FISHMAN_DIVE_SLEEP_PC 0x81db0cu    // `JSL thread_yield`
#define FISHMAN_DIVE_WAKE_PC 0x81db10u
#define FISHMAN_SWEEP_LIST_PC 0x81e342u    // `JSL pictures_play`
#define FISHMAN_SWEEP_BEGIN_PC 0x81e346u
#define FISHMAN_SWEEP_AFTER_PC 0x81e397u
#define FISHMAN_SPLASH_LIST_PC 0x81dbddu   // `JSL pictures_play`
#define FISHMAN_SPLASH_AFTER_PC 0x81dbe1u

// What each body's `JSR` to the draw for a leap pushes. Each goes on the
// same way when the leap was drawn.
#define FISHMAN_LEAP_FROM_SWIM 0xde05u
#define FISHMAN_LEAP_FROM_SWIM_TURNED 0xde3bu
#define FISHMAN_LEAP_FROM_CLOSE_IN 0xde74u
#define FISHMAN_LEAP_FROM_PATROL 0xdf28u
// ...and what the walk on land's `JSR` to `port/wander.h` pushes.
#define FISHMAN_DIVE_FROM_STALK 0xda1fu
#define FISHMAN_PATROL_BEGIN_PC 0x81e51au
#define FISHMAN_PATROL_END_PC FISHMAN_PATROL_FATE_PC
#define FISHMAN_PATROL_RTL_PC 0x81e5b7u  // the thread's last instruction
#define FISHMAN_LANDING_PC 0x81e1d6u
#define FISHMAN_LANDING_YES_PC 0x81e28bu  // the `RTS` after the `CLC`
#define FISHMAN_LANDING_NO_PC 0x81e28eu   // ...and after the `SEC`

// The state bodies, by the address the thread keeps in `$1C`.
#define FISHMAN_STATE_SWIM 0xddfdu
#define FISHMAN_STATE_SWIM_TURNED 0xde36u
#define FISHMAN_STATE_CLOSE_IN 0xde72u
#define FISHMAN_STATE_PATROL 0xdefdu
#define FISHMAN_STATE_LINE_UP_DOWN 0xdf84u
#define FISHMAN_STATE_LINE_UP_ACROSS 0xdfcbu
#define FISHMAN_STATE_FLIGHT 0xe120u
#define FISHMAN_STATE_LANDED 0xe15bu
#define FISHMAN_STATE_DIVE 0xdb8bu
#define FISHMAN_STATE_STALK 0xda1du
#define FISHMAN_STATE_DIVE_BEGIN 0xdb63u
#define FISHMAN_STATE_SPLASH 0xdbcdu
#define FISHMAN_STATE_LURK 0xe31cu  // the sweep. The name was a guess.
// ...and three the port only names.
#define FISHMAN_STATE_BITE 0xe295u
#define FISHMAN_STATE_ASHORE 0xd9b8u
#define FISHMAN_STATE_ASHORE_TURNED 0xd9e9u

// Fields on its page.
#define FISHMAN_DP_RECORD 0x08
#define FISHMAN_DP_FATE 0x0a  // 0 alive, anything else and the thread ends
#define FISHMAN_DP_X 0x10
#define FISHMAN_DP_Y 0x12
#define FISHMAN_DP_TRY_X 0x14         // the step being tried
#define FISHMAN_DP_TRY_Y 0x16
#define FISHMAN_DP_WAY 0x18           // a bearing, doubled
#define FISHMAN_DP_TRY_WAY 0x1a       // the turn being tried
#define FISHMAN_DP_STATE 0x1c
#define FISHMAN_DP_PICTURE_WAIT 0x1e  // passes until its next picture
#define FISHMAN_DP_PICTURE 0x20       // which of its cycle
#define FISHMAN_DP_HIT_BY 0x22        // what hit it, cleared before each sleep
#define FISHMAN_DP_TARGET 0x24        // the record it is after
#define FISHMAN_DP_SCRATCH 0x26
#define FISHMAN_DP_FORM 0x28  // 0 in water, 1 on land, negative in the air
#define FISHMAN_DP_LAND_X 0x2a        // where a leap is to come down
#define FISHMAN_DP_LAND_Y 0x2c
#define FISHMAN_DP_RISE 0x2e  // a leap: four times what its height gains
#define FISHMAN_DP_SPAN 0x30  // ...the count its two gaps are stepped by,
#define FISHMAN_DP_PART_X 0x32        // ...what is left over of each,
#define FISHMAN_DP_PART_Y 0x34
#define FISHMAN_DP_GAP_X 0x36         // ...the gaps,
#define FISHMAN_DP_GAP_Y 0x38
#define FISHMAN_DP_SIGN_X 0x3a        // ...and which way each is
#define FISHMAN_DP_SIGN_Y 0x3c
#define FISHMAN_DP_LEAP_WAY 0x3e      // which way the spot is, left or right
#define FISHMAN_DP_TICKS 0x40         // how long it sleeps
#define FISHMAN_DP_STAYS 0x46         // not zero for the one that patrols
#define FISHMAN_DP_PLACED_X 0x00      // where whatever began it put it
#define FISHMAN_DP_PLACED_Y 0x02
#define FISHMAN_DP_WATER_WANTED 0x0c  // `port/wander.h`'s two words
#define FISHMAN_DP_WATER_HAD 0x0e
#define FISHMAN_DP_BLOW 0x42          // a bite's second record, or `$FFFF`
#define FISHMAN_DP_BLOW_AT 0x44       // the sweep: where in the ring, in fours
#define FISHMAN_SWEEP_PC 0x81e381u
#define FISHMAN_SWEEP_YIELD_PC 0x81e37du  // `JSL thread_yield`, A already 1
#define FISHMAN_SWEEP_DONE_PC 0x81e393u   // `JSL pictures_play`, the list in A
#define FISHMAN_SWEEP_RING 0xe3c0u        // across and down, eight of them
#define FISHMAN_SWEEP_RING_SIZE 0x20
#define FISHMAN_DP_UNREAD 0x7e        // cleared; nothing here reads it
// Sliding borrows the word a leap keeps its part across in.
#define FISHMAN_DP_AXES_TAKEN FISHMAN_DP_PART_X

// For the harness, and only for it: what happened, which with the path is
// what it takes to price the ROM's instructions around the calls.

// A test of the six tiles under a point: how many it looked at, and whether
// every one passed.
typedef struct {
  int tiles;
  bool all;
} FishmanTiles;

// May it swim there? The water is asked, and then who is there.
typedef struct {
  FishmanTiles water;
  bool someone_asked;
  bool someone;
} FishmanProbe;

// Stopped by the ground, it looks for water just past the place.
typedef struct {
  bool back;        // the draw was taken off, not put on
  int tiles;        // how many tiles it looked at, of two
  bool edge_asked;
  BoundsExit edge;
} FishmanSeek;

// May it walk there? The ground is asked, and then who is there.
typedef struct {
  TerrainRegs ground;
  FishmanSeek seek;
  bool someone;
} FishmanLandProbe;

// A step, an axis at a time: an axis the step does not change is not asked.
typedef struct {
  bool asked[2];
  FishmanProbe axis[2];  // across, then down
  bool stuck;            // neither was taken
} FishmanSlide;

typedef enum {
  FISHMAN_LOOK_NOT,     // it did not look
  FISHMAN_LOOK_NEAR,    // somebody within 128: it closes in
  FISHMAN_LOOK_WITHIN,  // ...or within 208
  FISHMAN_LOOK_FAR,     // ...or not, and the players are asked after
} FishmanLook;

typedef enum {
  FISHMAN_LEAP_NOT_ASKED,
  FISHMAN_LEAP_NO_DRAW,     // the draw said no
  FISHMAN_LEAP_NOBODY,      // nobody within 200
  FISHMAN_LEAP_OFF_LEVEL,   // the spot is off the level
  FISHMAN_LEAP_NO_LANDING,  // ...or nowhere to land
  FISHMAN_LEAP_TAKEN,       // ...or somebody is on it
  FISHMAN_LEAP_LEAPS,       // it leaps, and the pass is the ROM's
} FishmanLeap;

typedef enum {
  FISHMAN_RANGE_TOUCHING,
  FISHMAN_RANGE_NEAR,
  FISHMAN_RANGE_FAR,
} FishmanRange;

typedef enum {
  FISHMAN_PATROL_TOUCHING,   // somebody within 24: it bites
  FISHMAN_PATROL_IN_COLUMN,  // ...within 24 across
  FISHMAN_PATROL_IN_ROW,     // ...within 24 down
  FISHMAN_PATROL_APART,
} FishmanPatrol;

// One step of lining up.
typedef struct {
  bool negative;  // its target is the other way
  bool arrived;   // within 16
  bool stuck;     // ...or the step was refused
} FishmanLineStep;

typedef enum {
  FISHMAN_FLIGHT_UP,
  FISHMAN_FLIGHT_DOWN,   // its height came to nothing exactly
  FISHMAN_FLIGHT_UNDER,  // ...or went below, and was put back
} FishmanFlight;

// A pass on land.
typedef enum {
  FISHMAN_STALK_STEPPED,      // at whoever is nearest
  FISHMAN_STALK_FOUND_WATER,  // water to go back to: it sleeps, then dives
  FISHMAN_STALK_BESIDE,       // somebody within 28, under 8 across: a step
  FISHMAN_STALK_SWEEPS,       // ...or further across: it sweeps again
  FISHMAN_STALK_LEFT,         // nobody within 175, and no player near
  FISHMAN_STALK_GOES_ABOUT,   // ...or a player is, and it goes about
} FishmanStalk;

// Where a pass stopped.
typedef enum {
  FISHMAN_STOP_PASS,    // at its end: the loop's sleep, or its fate
  FISHMAN_STOP_LEAP,    // `$81:E072`: a sleep, and then the leap
  FISHMAN_STOP_DIVE,    // `$81:DB0C`: a sleep, and then the dive
  FISHMAN_STOP_SWEEP,   // `$81:E342`: three pictures, and then the sweep
  FISHMAN_STOP_SPLASH,  // `$81:DBDD`: two pictures, and then it swims
} FishmanStop;

typedef enum {
  FISHMAN_SHOW_GONE,      // neither player near: it leaves, and is not shown
  FISHMAN_SHOW_FLYING,
  FISHMAN_SHOW_SWIMMING,
  FISHMAN_SHOW_STANDING,
} FishmanShow;

#define FISHMAN_MAX_SLIDES 2
#define FISHMAN_MAX_PLAYER_ASKS 4
#define FISHMAN_MAX_DRAWS 8

typedef struct {
  uint16_t state;  // the body that ran
  bool declined;   // the ROM's: it stops to look about, or finds water
  FishmanStop stop;
  uint16_t a;      // at a stop in the middle: the ticks, or the list
  uint16_t s;      // ...and the stack, with the body's returns on it

  // Swimming.
  FishmanLook look;
  bool look_about_asked;
  FishmanLeap leap;
  BoundsExit leap_edge;
  FishmanTiles landing;
  bool opening_asked;
  FishmanProbe opening;
  bool ahead_asked;
  FishmanProbe ahead;

  // Closing in.
  FishmanRange range;
  int close_steps;

  // Patrolling.
  bool reversed;
  bool reversed_wrapped;
  FishmanPatrol patrol;
  bool gap_negative[2];

  // Lining up.
  int line_steps;
  FishmanLineStep line[2];

  // On land.
  FishmanStalk stalk;
  bool wander_asked;
  WanderWork wander;
  bool beside_negative;  // near: it is left of them
  bool sweep_drawn;      // the one that comes ashore draws for its sweep
  bool sweep_off;        // ...and the draw said walk on
  FishmanLandProbe land[2];  // across, then down
  bool faces_back;       // the sweep begins facing one of the mirrored ways

  // A leap.
  bool falling;
  int glide_steps[2];
  FishmanFlight flight;
  bool ashore;

  // What the bodies ask, in the order they asked.
  int slides;
  FishmanSlide slide[FISHMAN_MAX_SLIDES];
  int player_asks;
  PlayerPickRegs players[FISHMAN_MAX_PLAYER_ASKS];
  int bearings;
  ActorSnapRegs snap[FISHMAN_MAX_SLIDES];
  ActorBearingRegs bearing[FISHMAN_MAX_SLIDES];
  int draws;
  bool draw_overflow[FISHMAN_MAX_DRAWS];
  ActorNearestWork nearest;  // summed
  AtPointWork at_point;      // summed

  // Showing itself.
  FishmanShow show;
  bool new_picture;
  bool picture_wrapped;
  bool mirrored;

  uint16_t ticks;     // what it sleeps for
  bool c, v;          // carry and overflow as the pass leaves them
  bool c_set, v_set;  // ...or the thread's own, where it wrote none
} FishmanLog;

// How the one that patrols began.
typedef enum {
  FISHMAN_BEGIN_NO_ROOM,    // the level has no room for it
  FISHMAN_BEGIN_NOT_WATER,  // it was not put on deep water
  FISHMAN_BEGIN_BEGAN,
} FishmanBegin;

typedef struct {
  FishmanBegin how;
  bool declined;     // no record free, and the ROM goes on with what is none
  uint16_t record;
  bool alone;        // neither player near: it will leave on its first pass
  PlayerPickRegs players;
  bool v_unknown;    // a tile's lookup was the last to write overflow
} FishmanBeginLog;

typedef struct {
  bool declined;  // not one that left of itself, or one with a blow out
  int place;      // where in the display list its record was
} FishmanEndLog;

// A leap or a dive set up, where its sleep comes back.
typedef struct {
  FishmanLog pass;        // the rest of the pass
  bool same_row, same_column;  // of the spot, for `actor_bearing_point`
  bool negative[2];       // the spot is left of it, and above it
  bool stays;             // too little across: no leap, or no dive
  bool across_longer;
  bool faces_cleared;     // the leap's picture takes a flag off
  int splash_slot;        // the thread a leap begins, or -1
} FishmanWakeLog;

// X and Y are the point. It comes back as the ROM's does: carry set when a
// tile is not somewhere to land, and that tile's bits in A.
void fishman_landing(Wram* w, PortCpu* c, FishmanTiles* log);

// `c->d` is the thread's page. It comes back with A, the flags and `pc` as
// the ROM leaves them at the `JSL thread_yield` or the `RTL`.
void fishman_patrol_begin(Wram* w, const Rom* rom, PortCpu* c,
                          FishmanBeginLog* log);
// False unless it left of itself with its record its own.
bool fishman_patrol_end_supported(const Wram* w, uint16_t page);
void fishman_patrol_end(Wram* w, PortCpu* c, FishmanEndLog* log);

// `$81:E346`. False unless it faces a way the ring has. `s` is the thread's
// stack, which has to hold a loop's return.
bool fishman_sweep_begin_supported(const Wram* w, uint16_t page, uint16_t s);
// False with no record free, which the ROM does not ask about. `*record` is
// the one it took.
bool fishman_sweep_begin(Wram* w, const Rom* rom, PortCpu* c, uint16_t* record,
                         bool* wrapped);

// `$81:E381`. False for a place the ring does not have, or a record that is
// none, or on its last tick one the display list does not hold.
bool fishman_sweep_supported(const Wram* w, uint16_t page);
// True while it goes on. `*wrapped` says the ring came round to its start.
// On the last tick `*place` is where in the display list the record was.
bool fishman_sweep_tick(Wram* w, const Rom* rom, PortCpu* c, bool* wrapped,
                        int* place);

typedef enum {
  FISHMAN_SLEEPS,
  FISHMAN_ENDS,
} FishmanFate;

// Can `fishman_frame` take this pass? Only in a state it knows, facing a way
// the tables have, with a leap that ends. It only looks.
bool fishman_frame_supported(const Wram* w, uint16_t page);

// One pass, for the creature whose page is `page`, in `loop`. `s` is the
// thread's stack as the loop is entered, and `carry` its own, as it woke.
// `log` may be NULL. `log->declined` says the pass is the ROM's after all;
// WRAM is then part written. `log->stop` says where it stopped.
FishmanFate fishman_frame(Wram* w, const Rom* rom, const FishmanLoop* loop,
                          uint16_t page, uint16_t s, bool carry,
                          FishmanLog* log);

// The stretches that begin where a body wakes. Each `_supported` reads the
// returns on the stack at `s`, and false is a pass the port does not know.
// Each comes back with A, the flags, S and `pc` as the ROM leaves them at
// the loop's `JSL thread_yield` or past the test of its fate.
bool fishman_leap_wake_supported(const Wram* w, uint16_t page, uint16_t s);
FishmanFate fishman_leap_wake(Wram* w, const Rom* rom, PortCpu* c,
                              FishmanWakeLog* log);
// `log->pass.declined` says the rest of the pass is the ROM's.
bool fishman_dive_wake_supported(const Wram* w, uint16_t page, uint16_t s);
FishmanFate fishman_dive_wake(Wram* w, const Rom* rom, PortCpu* c,
                              FishmanWakeLog* log);
#define FISHMAN_SPLASH_BEGIN_PC 0x81e72cu
#define FISHMAN_SPLASH_BEGIN_LIST_PC 0x81e771u  // `JSL pictures_play`
// False with no record free, which the ROM does not ask about. `*record`
// is the one it took.
bool fishman_splash_begin(Wram* w, PortCpu* c, uint16_t* record);

bool fishman_after_supported(const Wram* w, uint16_t page, uint16_t s);
FishmanFate fishman_sweep_after(Wram* w, const Rom* rom, PortCpu* c,
                                FishmanLog* log, bool* stays);
FishmanFate fishman_splash_after(Wram* w, const Rom* rom, PortCpu* c,
                                 FishmanLog* log);

#endif
