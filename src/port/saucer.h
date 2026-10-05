// The flying saucer of level 21, which stands off to one side of somebody
// and shoots at them, and now and then swoops.
//
// The saucer is a thread, `$82:873C`, and it is too large for sprites: its
// picture is a background (`port/bossbg.h`). Its loop sleeps a tick, runs one
// of four state bodies, and then blinks its lights. This is one pass of that
// loop as readable C, from where `thread_yield` returns to the next yield:
//
//   $82:87B1  saucer_frame        the state body, then the lights
//   $82:83E3  saucer_frame_shown  the rest of a pass that slept in the middle
//
// What is not here is the thread's setup, and its end once it is shot down.
//
// ## What a saucer does
//
// **It keeps a spot 116 to one side of itself**, the side its quarry is on,
// and it flies to put that spot on them. Its quarry is whoever `actor_nearest`
// knows. When they cross 16 past its middle the spot changes sides.
//
// **Hunting**, it moves two a pass along the way to the nearer player. A
// slanted way it takes three passes in four. Each axis of a move is taken by
// itself if the level reaches that far, so it slides along the level's edge.
//
// **With its quarry within 64 of the spot it shoots**, on a draw, and not
// again for 100 passes. The shot is a thread of its own, `$82:F49E`. It
// counts its shots down from two, and once that count and the wait together
// are below zero it does one of two things, by a draw: it swoops, or it opens
// where it is.
//
// **With its quarry within 12 of the spot it opens its hatch** and hangs
// there, wobbling in a small circle, shooting on the same draw. It stays
// while `player_bearing` finds a player within 14 of the spot.
//
// **With its quarry 250 or more away it swoops**: 16 to 31 passes, by a
// draw, at 16 or 22 a pass towards the nearer player, stopped short by the
// level's edge. Then it opens.
//
// **Open**, it wobbles in the same circle until its time is up or its quarry
// is near enough to the spot: 120 passes and 16 when it opened where it was,
// 200 passes and 175 after a swoop. Then it hunts again.
//
// ## The hatch
//
// The saucer can only be hit while it is open. What is hit is a display
// record it takes for the purpose and gives back, 34 left of its own position
// and 102 above. The port has not looked at the pictures: it is called a
// hatch here because it opens and closes.
//
// **Showing the hatch sleeps.** The routine that moves the record and turns
// its pictures, `$82:839C`, calls `thread_yield` itself, two calls deep. So a
// pass that shows it ends there, and the thread wakes inside the routine with
// two return addresses on its stack. `saucer_frame_shown` is the rest of that
// pass.
//
// A hit starts a flash of four such passes: other colours, and a mosaic on
// the background by how many hits it has taken.
//
// ## Its contract with the ROM
//
// It writes WRAM exactly as the ROM does. A pass ends at one of the two `JSL
// thread_yield`s with the tick count in A, or past the test of its health
// with that in A. Carry and overflow are left as the ROM leaves them. The two
// return addresses of a pass that sleeps showing the hatch are the caller's
// to write.
//
// Port code: libc only.

#ifndef PORT_SAUCER_H
#define PORT_SAUCER_H

#include <stdbool.h>
#include <stdint.h>

#include "assets/rom.h"
#include "port/oam.h"  // the works and registers of what it asks
#include "port/terrain.h"
#include "port/wram.h"

// The tables are in bank `$82`, which is the thread's data bank.
#define SAUCER_BANK 0x82u

#define SAUCER_FRAME_PC 0x8287b1u
#define SAUCER_YIELD_PC 0x8287adu  // `JSL thread_yield`, A already 1
#define SAUCER_DEAD_PC 0x8287c0u   // past the `BPL`, its health in A
#define SAUCER_YIELD_TICKS 1

// The yield inside the routine that shows the hatch, and where it returns.
#define SAUCER_SHOW_YIELD_PC 0x8283dfu
#define SAUCER_FRAME_SHOWN_PC 0x8283e3u
// What is on the stack there: the `JSR` of whichever body showed it, less
// one as `JSR` leaves it, and under that the computed `RTS`'s `PEA`.
#define SAUCER_RETURN_ON_TARGET 0x862fu
#define SAUCER_RETURN_OPEN 0x8716u
#define SAUCER_RETURN_LOOP 0x87b8u

// The state bodies, by the address the thread keeps in `$0E`.
#define SAUCER_STATE_HUNT 0x8505u
#define SAUCER_STATE_ON_TARGET 0x85f4u
#define SAUCER_STATE_SWOOP 0x867eu
#define SAUCER_STATE_OPEN 0x86d1u

// Fields on the saucer's page.
#define SAUCER_DP_SHOT_ASIDE 0x00   // for the shot it spawns: the spot, from it
#define SAUCER_DP_LIGHTS_WAIT 0x0a  // passes until the lights change
#define SAUCER_DP_LIGHTS_PHASE 0x0c // 0 or `$20`: which colours they show
#define SAUCER_DP_STATE 0x0e
#define SAUCER_DP_HEADING 0x10      // a bearing, doubled
#define SAUCER_DP_FACING 0x14       // `$FFFF` after a move right, 0 after left
#define SAUCER_DP_REFUSED 0x16      // how many axes of the last move were not taken
#define SAUCER_DP_TRY_X 0x18        // the move being tried
#define SAUCER_DP_TRY_Y 0x1a
#define SAUCER_DP_TRY_AIM_X 0x1c    // ...and where it would take the spot
#define SAUCER_DP_QUARRY 0x1e       // the record nearest the spot
#define SAUCER_DP_WOBBLE 0x20       // counts the steps of the circle
#define SAUCER_DP_SHOT_WAIT 0x22    // passes until it may shoot again
#define SAUCER_DP_SHOTS_LEFT 0x24
#define SAUCER_DP_AIM_X 0x26        // the spot, which is level with it
#define SAUCER_DP_LINE_X 0x28       // its quarry past this, and the spot changes sides
#define SAUCER_DP_TIMER 0x2a        // swooping and open: passes left
#define SAUCER_DP_REAIM_WAIT 0x2c   // hunting: passes until it looks again
#define SAUCER_DP_REACH 0x2e        // open: its quarry this near ends it
#define SAUCER_DP_HEALTH 0x30       // negative and the thread ends
#define SAUCER_DP_FLASH 0x32        // 4 when hit, counted down past zero
#define SAUCER_DP_HATCH 0x36        // the record, or `$FFFF` when shut
#define SAUCER_DP_HATCH_HOLD 0x38   // passes its picture has left
#define SAUCER_DP_HATCH_CYCLE 0x3a  // which of four pictures, in fours

// Where its picture is drawn from: 79 left of it and 196 above.
#define W_SAUCER_PICTURE_X 0x1e6eu
#define W_SAUCER_PICTURE_Y 0x1e70u
// How coarse the mosaic of a flash is, for the job that sets it.
#define W_SAUCER_MOSAIC 0x6db2u

// For the harness, and only for it: what happened, which with the path is
// what it takes to price the ROM's instructions around the calls.
typedef struct {
  BoundsExit exit;
  bool outside;
} SaucerEdge;

typedef enum {
  SAUCER_RANGE_ON_IT,   // under 12: it opens over them
  SAUCER_RANGE_FAR,     // 250 or more: it swoops
  SAUCER_RANGE_CLOSE,   // under 64: it may shoot
  SAUCER_RANGE_MIDDLE,
} SaucerRange;

typedef enum {
  SAUCER_FLASH_NONE,     // counted down past zero already
  SAUCER_FLASH_OVER,     // zero: its own colours again
  SAUCER_FLASH_BEGINS,   // four: it was hit since the last pass
  SAUCER_FLASH_RUNNING,
} SaucerFlash;

typedef enum {
  SAUCER_OPEN_NOT,        // the open body did not run
  SAUCER_OPEN_TIMED_OUT,
  SAUCER_OPEN_REACHED,    // its quarry came within reach
  SAUCER_OPEN_WAITED,     // not a pass it wobbles on
  SAUCER_OPEN_WOBBLED,
} SaucerOpen;

#define SAUCER_MAX_DRAWS 2

typedef struct {
  uint16_t state;  // the body that ran
  bool declined;   // something only the ROM can do: see `saucer_frame`

  // Hunting.
  SaucerRange range;
  bool spent;         // close, and out of shots
  bool spent_swoops;  // ...and the draw said swoop
  bool looked;        // the wait to look again ran out
  bool aim_left;      // ...with the spot left of it
  bool crossed;       // ...and its quarry past the line
  bool straight;      // a straight way, moved every pass
  bool held;          // a slanted way, on the pass in four it skips
  bool across;        // the way has a part across
  bool leftwards;     // ...which is to the left

  // On target.
  bool lost;    // no player within reach of the spot
  bool waited;  // not a pass it wobbles on

  // Swooping.
  bool swoop_began;
  bool swoop_over;     // its passes ran out
  bool swoop_stopped;  // the level's edge

  SaucerOpen open;

  // What the bodies ask.
  bool aimed;                // the spot was chosen again
  ActorNearestWork nearest;  // summed over every `actor_nearest` of the pass
  bool bearing_asked;
  PlayerPickRegs players;
  int draws;
  bool draw_overflow[SAUCER_MAX_DRAWS];
  bool shoot_asked;  // the draw for a shot was made
  bool shot_drawn;   // ...and said shoot
  bool shot;         // ...and it was not waiting
  int shot_slot;     // what `thread_spawn` answered
  bool moved;        // a move was tried
  bool clamped;      // ...from left of the level's left margin
  SaucerEdge edge[2];  // ...across, then down

  // The hatch.
  bool hatch_asked;
  bool hatch_opened;
  uint16_t hatch_record;
  bool hatch_shut;
  int shut_place;  // where in the display list it was; negative, not freed
  bool showed;
  SaucerFlash flash;
  bool next_picture;

  // The lights.
  bool lights;
  bool shot_waiting;
  bool blinked;

  // The colours and the job, which a pass asks for once at most: by the
  // flash when the hatch is shown, and by the lights otherwise.
  bool colours_asked;
  int colours_slot;
  bool job_asked;
  int job_slot;

  bool c, v;  // carry and overflow as the pass leaves them
} SaucerLog;

typedef enum {
  SAUCER_SLEEPS,          // at the loop's yield
  SAUCER_SLEEPS_SHOWING,  // at the yield inside the hatch's routine
  SAUCER_ENDS,            // shot down
} SaucerFate;

// Can `saucer_frame` take this pass? Not a state it does not know, nor a
// heading the tables do not reach. It only looks.
bool saucer_frame_supported(const Wram* w, uint16_t page);

// One pass, for the saucer whose page is `page`. `log` may be NULL, and
// `log->declined` says the pass is the ROM's: its quarry or its hatch is no
// record, or there was no record free for a hatch.
SaucerFate saucer_frame(Wram* w, const Rom* rom, uint16_t page,
                        SaucerLog* log);

// The rest of a pass that slept showing the hatch: the lights, and its
// health.
SaucerFate saucer_frame_shown(Wram* w, const Rom* rom, uint16_t page,
                              SaucerLog* log);

#endif
