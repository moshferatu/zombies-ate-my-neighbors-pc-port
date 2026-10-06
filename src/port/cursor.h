// A cursor a pad moves about a screen.
//
// `$82:B232` runs a screen in bank `$82` on which a player moves a cursor
// and presses a button. Every fourth frame it reads that player's pad. A
// direction newly held moves the cursor a step that way, and past an edge
// it comes back in at the other. A button is the ROM's to act on, and so
// are Start, and three hundred turns with nothing pressed.
//
// The eight movies that begin at a later level all run it before the level
// starts, so I take it for the screen a password is entered on. I have not
// seen it.
//
// This is a turn of its loop, from the return of the sleep at `$82:B267`
// to the next sleep, or to where the ROM goes on.
//
// The far pointer at `$E6` names two words: an index for the bottom edge,
// and which pad.
//
// Port code: libc only.

#ifndef PORT_CURSOR_H
#define PORT_CURSOR_H

#include <stdbool.h>
#include <stdint.h>

#include "assets/rom.h"
#include "port/cpu.h"
#include "port/wram.h"

#define CURSOR_PC 0x82b267u
#define CURSOR_SLEEP_PC 0x82b263u    // `JSL`, A already 4
#define CURSOR_BUTTON_PC 0x82b287u   // a button newly down
#define CURSOR_MOVED_PC 0x82b2beu    // moved: a sound, and the clock again
#define CURSOR_DONE_PC 0x82b2dbu     // Start, or time up

#define CURSOR_BANK 0x82
#define CURSOR_DP_SCREEN 0xe6       // the far pointer
#define CURSOR_STEPS 0x82b7fcu      // across and down, for each direction
#define CURSOR_BOTTOMS 0x82b617u
#define W_CURSOR_TIME 0x004cu       // turns left with nothing pressed
#define W_CURSOR_DIR 0x004eu        // the direction last seen
#define W_CURSOR_PADS 0x006eu
#define W_CURSOR_DIRS 0x0072u
#define W_CURSOR_BUTTON 0x1e90u     // not zero while a button stays down
#define W_CURSOR_WANT_X 0x1e96u
#define W_CURSOR_WANT_Y 0x1e9au
#define W_CURSOR_RECORD 0x1e9eu
#define W_CURSOR_START 0x1eb2u
#define CURSOR_PAD_START 0x1000u
#define CURSOR_PAD_BUTTONS 0xc0c0u
#define CURSOR_LEFT 0x0018
#define CURSOR_RIGHT 0x00e8
#define CURSOR_AT_LEFT 0x0020
#define CURSOR_AT_RIGHT 0x00e0
#define CURSOR_TOP 0x003f
#define CURSOR_AT_TOP 0x0047
#define CURSOR_ROW 8

enum {
  CU_HEAD,      // $B267-$B277
  CU_START,     // INC $1EB2 : BRA
  CU_BUTTONS,   // AND #$C0C0 : BEQ
  CU_HELD,      // LDA $1E90 : BNE
  CU_DIR,       // $B292-$B29C
  CU_MOVE,      // $B29D-$B2B8
  CU_MOVED,     // LDA $004E : BEQ
  CU_TIME,      // $B2CB-$B2D3
  CU_FLAG,      // LDA $1EB2 : BNE
  CU_AGAIN,     // BRA, LDA #$0004
  CU_PUT,       // $B333-$B33D
  CU_LEFT_EQ,   // CMP #$0018 : BEQ
  CU_BCC,       // BCC, either axis
  CU_RIGHT,     // CMP #$00E8 : BCS
  CU_BRA,       // BRA, either axis
  CU_TO_RIGHT,  // LDA #$00E0 : BRA
  CU_TO_LEFT,   // LDA #$0020
  CU_PUT_X,     // $B354-$B35E
  CU_BOTTOM,    // CMP $B617,Y : BCS
  CU_TO_BOTTOM, // $B368-$B370
  CU_TO_TOP,    // LDA #$0047
  CU_PUT_Y,     // STA $0006,X : RTS
  CU_TAKEN,
  CU_BLOCK_COUNT
};

typedef struct {
  uint16_t blocks[CU_BLOCK_COUNT];
} CursorWork;

// False unless the pointer is into the cartridge and what it names, the
// cursor's record and the direction are all ones the tables have.
bool cursor_frame_supported(const Wram* w, const Rom* rom, uint16_t page);
void cursor_frame(Wram* w, const Rom* rom, PortCpu* c, CursorWork* k);

#endif
