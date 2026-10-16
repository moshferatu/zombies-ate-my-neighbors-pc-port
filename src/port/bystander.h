// Something standing in a level that waits for a player to come up to it.
//
// A thread, `$82:DD52`. Its setup takes a display record that collides with
// nothing, shows a picture in it, and works out a box around the spot, 40
// wide and 65 tall. Then it loops: sleep five ticks, and look for a player
// in the box. This is one pass of that loop as readable C, from where
// `thread_yield` returns to the next yield:
//
//   $82:DD5C  frame   is a player standing in the box?
//
// What is not here is the setup, and what it does when a player is there,
// `$82:DDA7`, which draws on the screen and reads the pads: those are
// stretches of `port/thread_stretches.h`, between their calls, and so is the
// thread's end. Two of them write the hardware, and are here.
//
// ## The screen dimmed, and lit again
//
// What it shows begins with the colour the screen is added to, `$2132`, at
// `$E8`: a dark grey on all three channels. Five words go from `$82:E6A5` to
// `$7E:6DA6`, which a table of the vblank's reads, and `$136E` says which
// channels of HDMA run, `$20`. When the player moves on, none run, and the
// colour is `$E5`.
//
// ## How it looks
//
// **Only at what is on screen.** It goes down the list of records being
// drawn this frame, `$7E:137E`, and a player is a record whose collide id is
// 5 or 6.
//
// **It never looks at the first on the list**, when that is the only one
// there: the count is tested for zero again after two are taken off it, and
// the search is skipped. With more than one drawn, the first is looked at
// last.
//
// **The box is closed on its near edges and open on its far ones**, left and
// top included.
//
// ## Its contract with the ROM
//
// It writes nothing. A frame ends at the `JSL thread_yield` with the tick
// count in A, at the `JSR $DDA7` with a player found, or at the `JML
// actor_slot_free` at `$82:DD6A` with the thread told to end and its slot
// in A. A, carry, zero and negative are left as the ROM
// leaves them, and overflow as it was.
//
// Port code: libc only.

#ifndef PORT_BYSTANDER_H
#define PORT_BYSTANDER_H

#include <stdbool.h>
#include <stdint.h>

#include "assets/rom.h"
#include "port/cpu.h"
#include "port/hw.h"
#include "port/wram.h"

#define BYSTANDER_FRAME_PC 0x82dd5cu
#define BYSTANDER_YIELD_PC 0x82dd58u  // `JSL thread_yield`, A already 5
#define BYSTANDER_MET_PC 0x82dd61u    // `JSR $DDA7`
#define BYSTANDER_END_PC 0x82dd6au    // `JML actor_slot_free`, its slot in A
#define BYSTANDER_YIELD_TICKS 5

// Fields on the thread's page.
#define BYSTANDER_DP_LEFT 0x08
#define BYSTANDER_DP_RIGHT 0x0a   // one past
#define BYSTANDER_DP_TOP 0x0c
#define BYSTANDER_DP_BOTTOM 0x0e  // one past
#define BYSTANDER_DP_SLOT 0x10    // its actor's
#define BYSTANDER_DP_END 0x12     // not zero, and the thread ends

typedef enum {
  BYSTANDER_SLEEPS,  // nobody there
  BYSTANDER_MET,     // a player in the box
  BYSTANDER_ENDS,    // nobody there, and it has been told to end
} BystanderFate;

// For the harness, and only for it: what the search did, which is what it
// takes to price the ROM's instructions, and A at the exit.
typedef struct {
  bool none_drawn;    // nothing on the list
  bool one_drawn;     // ...or one, which is not looked at
  int looked;         // records looked at
  int not_colliding;  // ...with no collide id
  int others;         // ...or one that is not a player's
  int first_players;  // players with id 5, and with id 6
  int second_players;
  int left_of, right_of, above, below;  // players outside, by the test failed
  uint16_t a;
  bool n, z;
} BystanderLog;

// `$82:DDE1` and `$82:DE72`, from where a call comes back to the next. A,
// X and the flags as the ROM leaves them.
#define BYSTANDER_DIM_PC 0x82dde1u
#define BYSTANDER_DIMMED_PC 0x82ddfeu  // `JSL $82:DC45`
#define BYSTANDER_LIT_PC 0x82de72u
#define BYSTANDER_LIT_CALL_PC 0x82de7eu  // ...the same
#define BYSTANDER_DIM 0xe8u
#define BYSTANDER_LIT 0xe5u
#define BYSTANDER_SHADE_SOURCE 0x82e6a5u
#define W_BYSTANDER_SHADE 0x6da6u        // of bank `$7E`, five words
#define BYSTANDER_SHADE_LAST 8
#define BYSTANDER_HDMA 0x0020u
#define REG_COLDATA 0x2132u

enum {
  BYS_SEP_IMM,  // `SEP #$20 : LDA #imm : STA $2132`, to its write
  BYS_REP_LDX,  // `REP #$20 : LDX #8`
  BYS_COPY,     // `LDA $E6A5,X : STA $7E6DA6,X : DEX : DEX : BPL`
  BYS_TAKEN,    // ...taken
  BYS_HDMA,     // `LDA #$20 : STA $136E`
  BYS_STZ,      // `STZ $136E`
  BYS_REP,      // `REP #$20`
  BYS_BLOCK_COUNT
};

void bystander_dim(Wram* w, const Rom* rom, PortCpu* c, HwTrace* t);
void bystander_lit(Wram* w, PortCpu* c, HwTrace* t);

// Can `bystander_frame` take this pass? Not a list longer than there is room
// for, nor a record outside the WRAM the thread's bank mirrors. It only looks.
bool bystander_frame_supported(const Wram* w);

// One pass, for the thread whose page is `page`. `log` may be NULL.
BystanderFate bystander_frame(const Wram* w, uint16_t page, BystanderLog* log);

#endif
