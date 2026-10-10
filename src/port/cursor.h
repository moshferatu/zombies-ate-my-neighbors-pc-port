// A cursor a pad moves about a screen.
//
// `$82:B232` runs a screen in bank `$82` on which a player moves a cursor
// and presses a button. Every fourth frame it reads that player's pad. A
// direction newly held moves the cursor a step that way, and past an edge
// it comes back in at the other. A button is the ROM's to act on, and so
// are Start, and three hundred turns with nothing pressed.
//
// It is the screen a password is entered on, and by its second table the
// one a name is entered on for the top scores. The cartridge's words for
// the first are at `$82:B6C2`, and `docs/password.md` has how it is laid
// out and driven. I have not seen the second.
//
// `cursor_frame` is a turn of its loop, from the return of the sleep at
// `$82:B267` to the next sleep, or to where the ROM goes on.
//
// `cursor_pick` is what a button does, `$82:B3F6`: the character under the
// cursor, from a grid of thirteen to a row, put at the end of what has been
// entered. Three characters of the grid are not letters:
//
//   * `$3B` ends the entry, as Start does;
//   * `$3C` is entered as `$2F`, which the grid itself has between its
//     letters, so I take it for a space;
//   * `$3A` takes the last one back. That one is the ROM's.
//
// Past the last place the entry does not grow: the last letter is replaced.
//
// `cursor_after` is the end of a turn that did something, from where the
// pick or the move's sound comes back: the clock set to three hundred
// turns again, and the same count the plain turn ends with.
//
// The far pointer at `$E6` names two words: which screen, and which pad.
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
#define CURSOR_MOVED_PC 0x82b2c1u    // moved: `JSL`, the sound in A
#define CURSOR_DONE_PC 0x82b2dbu     // Start, or time up
#define CURSOR_PICK_PC 0x82b3f6u
#define CURSOR_PICK_SOUND_PC 0x82b48au  // `JSL`, the sound in A
#define CURSOR_PICK_RTS_PC 0x82b4b6u    // the end of the entry
#define CURSOR_AFTER_PICK_PC 0x82b28au
#define CURSOR_AFTER_MOVE_PC 0x82b2c5u

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
#define CURSOR_TURNS 0x012c         // with nothing pressed, and it is done

// The pick's.
#define CURSOR_DP_GRID 0xe0         // the grid's address, in the data bank
#define CURSOR_LENGTHS 0x82b5f7u    // how many places, for each screen
#define CURSOR_GRIDS 0x82b61du
#define CURSOR_GRID_ROWS 0x82b60bu  // where each row starts in a grid
#define CURSOR_GRID_ROW_COUNT 6
#define CURSOR_GRID_COLUMNS 13
#define CURSOR_COLUMN_SHIFT 4       // sixteen pixels a column
#define CURSOR_ROW_SHIFT 3          // ...and a row, as an index of words
#define W_CURSOR_ENTRY 0x1ea0u      // what has been entered, a byte each
#define CURSOR_ENTRY_MAX 0x10
#define W_CURSOR_ENTRY_AT 0x1e94u   // where the next goes
#define W_CURSOR_ENTRY_LAST 0x1e9cu // the last place there is
#define W_CURSOR_COLUMN 0x1e96u
#define CURSOR_CHAR_END 0x3b
#define CURSOR_CHAR_SPACE 0x3c
#define CURSOR_CHAR_BACK 0x3a
#define CURSOR_SPACE 0x2f
#define CURSOR_PICK_SOUND 0x0010
#define CURSOR_MOVE_SOUND 0x000e

enum {
  CU_HEAD,      // $B267-$B277
  CU_START,     // INC $1EB2 : BRA
  CU_BUTTONS,   // AND #$C0C0 : BEQ
  CU_HELD,      // LDA $1E90 : BNE
  CU_DIR,       // $B292-$B29C
  CU_MOVE,      // $B29D-$B2B8
  CU_MOVED,     // LDA $004E : BEQ
  CU_SOUND,     // LDA #$000E
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

enum {
  PK_HEAD,        // $B3F6-$B436
  PK_SPACE_TEST,  // CMP #$3C : BNE
  PK_SPACE,       // LDA #$2F : BRA
  PK_BACK_TEST,   // CMP #$3A : BNE
  PK_STORE,       // $B474-$B480
  PK_ADVANCE,     // INX : STX $1E94
  PK_SOUND,       // REP #$30 : LDA #$0010
  PK_END,         // REP #$30 : DEC $1EB2
  PK_CLOCK,       // LDA #$012C : STA $004C
  PK_TAKEN,
  PK_BLOCK_COUNT
};

typedef struct {
  uint16_t blocks[PK_BLOCK_COUNT];
  CursorWork turn;  // the count a turn ends with
} PickWork;

// False unless the pointer is into the cartridge and what it names, the
// cursor's record and the direction are all ones the tables have.
bool cursor_frame_supported(const Wram* w, const Rom* rom, uint16_t page);
void cursor_frame(Wram* w, const Rom* rom, PortCpu* c, CursorWork* k);

// False for a screen or a place of the cursor the tables do not have, for
// an entry past its room, and for the character that takes one back.
bool cursor_pick_supported(const Wram* w, const Rom* rom, uint8_t db,
                           uint16_t page);
// True if the character ended the entry.
bool cursor_pick(Wram* w, const Rom* rom, PortCpu* c, PickWork* k);

void cursor_after(Wram* w, PortCpu* c, bool picked, PickWork* k);

#endif
