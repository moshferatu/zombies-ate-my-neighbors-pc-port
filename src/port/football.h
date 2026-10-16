// The football players of level 21, who run across the field and at anybody
// in their way.
//
// A footballer is a thread, `$81:C87B`. Its loop sleeps two ticks, runs one
// of its state bodies, and shows itself. This is one pass of that loop as
// readable C, from where `thread_yield` returns to the next yield:
//
//   $81:C8A1  footballer_frame
//
// And one piece of it, for the ROM to call in a pass that is its own:
//
//   $81:C824  footballer_show    its picture where it now is, to the `RTS`
//
// The thread's start is a stretch too, from its first instruction to its
// first sleep:
//
//   $81:C87B  footballer_start
//
// What is not here is the tackle it makes when it runs into a player, which
// sleeps in the middle, and its end. A pass in one of those states is the
// ROM's.
//
// ## What a footballer does
//
// **It runs four pixels a pass along one of eight ways**, and turns when the
// ground stops it: to the left if it was going up or any way to the right,
// and to the right otherwise.
//
// **With a player within 32 it goes at them**, if they are to one side of it
// or the other. Straight above or below it does not turn.
//
// **It begins by standing a moment.** Then, each pass, a draw may make it
// veer an eighth of a turn for sixteen passes or so. When those are up
// another draw either straightens it out again or has it veer on.
//
// **It comes on from the edge of the screen** nearer to where it was asked
// for (`footballer_enter`), and not at all if the ground there is solid or
// off the level: the thread ends at once. Otherwise it takes a record, adds
// ten to the census at `$00DE`, names its handler, and stands for 15 to 46
// passes, by a draw.
//
// **With neither player within 320 it leaves.**
//
// **Its handler leaves it a state of its own**, `$81:C6CA`. The pass in it
// takes the handler away and plays its pictures, which sleeps: the pass
// stops at that `JSL`, with the return of the loop's computed `RTS` under
// it. Where the pictures come back to sends it off for its next pass, and
// returns to the pass's end (`footballer_shown`):
//
//   $81:C6D9  footballer_off_shown   sent off, shown, and its fate tested
//
// The pass's end is not an entry of its own. Every state body returns to
// it, and a row there would take each pass that `footballer_frame` is
// being checked on.
//
// **Sent off, it runs eight pixels a pass** the way it was facing, turning
// where the ground stops it, and ends once its picture is off the screen:
// eight to the left of it, or more than 328 across or 272 down from its
// corner. Its handler sends it off: its pictures, and the state at
// `$81:C6F2` for its next pass. That pass its hits go nowhere, it turns at a
// player in reach, and then the other way, as the ground stopping it would
// turn it. It runs off in the same pass. The handler's end, `$81:C6D9`, is a
// stretch of `port/thread_stretches.h`.
//
// ## Its contract with the ROM
//
// It writes WRAM exactly as the ROM does. A pass ends at the `JSL
// thread_yield` with the tick count in A, or past the test of its fate with
// that in A. Carry and overflow are left as the ROM leaves them: the thread's
// own carry is the first thing a draw takes.
//
// Port code: libc only.

#ifndef PORT_FOOTBALL_H
#define PORT_FOOTBALL_H

#include <stdbool.h>
#include <stdint.h>

#include "assets/rom.h"
#include "port/cpu.h"
#include "port/oam.h"  // the works and registers of what it asks
#include "port/terrain.h"
#include "port/wram.h"

// The tables are in bank `$81`, which is the thread's data bank.
#define FOOTBALLER_BANK 0x81u

#define FOOTBALLER_FRAME_PC 0x81c8a1u
#define FOOTBALLER_YIELD_PC 0x81c89du  // `JSL thread_yield`, A already 2
#define FOOTBALLER_FATE_PC 0x81c8b0u   // past the `BEQ`, its fate in A
#define FOOTBALLER_YIELD_TICKS 2

// The state bodies, by the address the thread keeps in `$0A`.
#define FOOTBALLER_STATE_RUN 0xc55au
#define FOOTBALLER_STATE_STAND 0xc5ffu
#define FOOTBALLER_STATE_RUN_LOOSE 0xc61cu
#define FOOTBALLER_STATE_VEER 0xc665u
#define FOOTBALLER_STATE_RUN_OFF 0xc70bu
#define FOOTBALLER_STATE_SENT_OFF 0xc6f2u
#define FOOTBALLER_STATE_HANDLED 0xc6cau  // what its handler leaves

// The computed `RTS`'s return, `PEA $C8A8`, and the pictures of the state
// the handler leaves.
#define FOOTBALLER_BODY_RETURN 0xc8a8u
#define FOOTBALLER_HANDLED_PICTURES 0xc6dcu
#define FOOTBALLER_HANDLED_CALL_PC 0x81c6d5u  // `JSL pictures_resume`

// Fields on its page.
#define FOOTBALLER_DP_RECORD 0x08
#define FOOTBALLER_DP_STATE 0x0a
#define FOOTBALLER_DP_X 0x0c
#define FOOTBALLER_DP_Y 0x0e
#define FOOTBALLER_DP_WAY 0x10      // a bearing, doubled
#define FOOTBALLER_DP_COUNT 0x14    // passes: of a picture, a stand or a veer
#define FOOTBALLER_DP_PICTURE 0x16  // which of four, doubled
#define FOOTBALLER_DP_TRY_X 0x1c    // the step being tried
#define FOOTBALLER_DP_TRY_Y 0x1e
#define FOOTBALLER_DP_FATE 0x22     // 0 alive, anything else and the thread ends

// For the harness, and only for it: what happened, which with the path is
// what it takes to price the ROM's instructions around the calls.

typedef enum {
  FOOTBALLER_STEER_NOT_A_PLAYER,  // what the test handed back is no player
  FOOTBALLER_STEER_NOBODY,        // no player within reach
  FOOTBALLER_STEER_NOT_ASIDE,     // ...or one straight above or below
  FOOTBALLER_STEER_AT_THEM,
} FootballerSteer;

typedef enum {
  FOOTBALLER_VEER_ON,          // its passes are not up
  FOOTBALLER_VEER_STRAIGHTEN,  // the draw said straighten out
  FOOTBALLER_VEER_NO_WAY,      // ...or the table has no way on from this one
  FOOTBALLER_VEER_AGAIN,
} FootballerVeer;

// Sent off: which edge of the screen its picture was found past.
typedef enum {
  FOOTBALLER_ON_SCREEN,
  FOOTBALLER_OFF_LEFT,
  FOOTBALLER_OFF_RIGHT,
  FOOTBALLER_OFF_TOP,
  FOOTBALLER_OFF_BOTTOM,
} FootballerOff;

#define FOOTBALLER_MAX_DRAWS 2

typedef struct {
  uint16_t state;  // the body that ran
  bool declined;   // the ROM's: what the test of the players handed back

  bool ran;  // the body went on to a pass of the run
  FootballerSteer steer;
  bool first_player;  // ...and it was the first player
  bool new_picture;
  TerrainRegs ground;
  bool stood_on;       // standing: its passes are not up
  bool veered;         // running loose: the draw said veer
  FootballerVeer veer;

  bool camera_near_left;  // sent off: the camera is under eight across
  FootballerOff off;

  bool mask_clears;  // the table's word for its record's flags clears
  bool gone;         // neither player near: it leaves

  int player_asks;
  PlayerPickRegs players[2];  // steering, then showing itself
  int draws;
  bool draw_overflow[FOOTBALLER_MAX_DRAWS];

  bool c, v;          // carry and overflow as the pass leaves them
  bool c_set, v_set;  // ...or the thread's own, where it wrote none
} FootballerLog;

typedef enum {
  FOOTBALLER_SLEEPS,
  FOOTBALLER_ENDS,
  FOOTBALLER_TO_PICTURES,  // stopped at its pictures' `JSL`
} FootballerFate;

// Can `footballer_frame` take this pass? Only in a state it knows, facing a
// way and showing a picture the tables have. It only looks.
bool footballer_frame_supported(const Wram* w, uint16_t page);

// One pass, for the footballer whose page is `page`. `carry` is the thread's
// own, as it woke. `log` may be NULL, and `log->declined` says the pass is
// the ROM's after all. WRAM is then part written.
FootballerFate footballer_frame(Wram* w, const Rom* rom, uint16_t page,
                                bool carry, FootballerLog* log);

#define FOOTBALLER_SHOW_PC 0x81c824u
#define FOOTBALLER_SHOW_RTS_PC 0x81c860u

// `$81:C8A9`, the end of a pass from where a state body returns: it shows
// itself, and its fate says whether it sleeps or ends. `carry` is as the body
// left it. `log` may be NULL. For the pass after its pictures only.
FootballerFate footballer_shown(Wram* w, const Rom* rom, uint16_t page,
                                bool carry, FootballerLog* log);

// `$81:C824`, called. False for a way or a picture the tables do not have.
bool footballer_show_supported(const Wram* w, uint16_t page);
// It comes back as the ROM's does. `*v_known` is false when nothing it asked
// wrote overflow that the port follows.
void footballer_show(Wram* w, const Rom* rom, PortCpu* c, FootballerLog* log,
                     bool* v_known);

// Where one comes on, `$81:C7A5`: just off the edge of the screen nearer to
// where it was asked for, facing in. Asked for within 160 of the camera's
// left edge it comes on 8 to the left of it, running right; otherwise 328
// to the right of it, running left. It ends at the first of the two tests
// the ROM then makes of that place, with the place in X and Y.
#define FOOTBALLER_ENTER_PC 0x81c7a5u
#define FOOTBALLER_ENTER_TEST_PC 0x81c7cfu  // `JSL $80:AE14`
#define FOOTBALLER_DP_ASKED_X 0x00
#define FOOTBALLER_DP_ASKED_Y 0x02
#define FOOTBALLER_ENTER_NEAR 0x00a0
#define FOOTBALLER_ENTER_LEFT 0x0008        // off the left edge by this
#define FOOTBALLER_ENTER_RIGHT 0x0148       // ...or this far along from it
#define FOOTBALLER_WAY_RIGHT 0x0006
#define FOOTBALLER_WAY_LEFT 0x000e
// True if it comes on from the left.
bool footballer_enter(Wram* w, PortCpu* c);

// Its start, `$81:C7E6`, once both tests have passed: a record, and its page
// cleared, as far as the `RTS`. Where its record is on the screen is not
// set here. False with no record free, which is the ROM's: it only looks
// then.
#define FOOTBALLER_BEGIN_PC 0x81c7e6u
#define FOOTBALLER_BEGIN_RTS_PC 0x81c823u
#define FOOTBALLER_START_PICTURE 0xe3dfu
#define FOOTBALLER_PICTURE_BANK 0x0090
#define FOOTBALLER_COLLIDE_ID 0x0035
#define FOOTBALLER_START_ATTR 0x0c00
#define FOOTBALLER_DP_7E 0x7e  // cleared; I have not read what uses it
bool footballer_begin(Wram* w, PortCpu* c, uint16_t* record);

// `$81:C87B`, the thread's start, to the `JSL thread_yield` with the ticks in
// A. With the place it comes on at solid or off the level it stops at the
// thread's `RTL`, `$81:C8C2`, with carry set.
#define FOOTBALLER_START_PC 0x81c87bu
#define FOOTBALLER_START_ENDED_PC 0x81c8c2u
#define FOOTBALLER_HANDLER 0xc8c3u
#define FOOTBALLER_CENSUS 10    // what one adds to the census at `$00DE`
#define FOOTBALLER_STAND_DRAW 0x001f  // a stand's passes: a draw so masked...
#define FOOTBALLER_STAND_LEAST 0x000f // ...and this many more

typedef enum {
  FOOTBALLER_STARTED,
  FOOTBALLER_START_BLOCKED,  // the ground where it comes on is solid
  FOOTBALLER_START_OUTSIDE,  // ...or it is off the level
  FOOTBALLER_START_DECLINED, // no record, or a picture the tables lack: the ROM's
} FootballerStart;

// For the harness: what it did.
typedef struct {
  bool from_left;
  TerrainRegs ground;
  BoundsRegs bounds;
  uint16_t record;
  bool draw_overflow;
  FootballerLog show;
  bool v_known;  // ...and whether the show left an overflow the port follows
} FootballerStartLog;

// `log` may not be NULL. WRAM is part written when it declines, so a caller
// asks it of a copy first.
FootballerStart footballer_start(Wram* w, const Rom* rom, PortCpu* c,
                                 FootballerStartLog* log);

#endif
