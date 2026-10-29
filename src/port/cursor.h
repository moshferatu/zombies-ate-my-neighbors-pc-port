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

// ## The rest of the screen, between its calls
//
// `$82:B232` is called with the screen's table in A and Y. It dims the
// picture, sends the screen's colours and tiles and the backdrop's, prints
// the screen's words, makes the cursor's sprite and fades up. Then the loop
// above. When that is done it calls the screen's own routine, which for a
// password is the check at `$82:B018`, and on carry clear flashes the entry
// twice. Carry set is two sounds a sixth of a second apart. It fades down,
// stops its two jobs and returns the entry's address in A.
//
// `$82:AFF3` is what the title's menu calls for a password: the table for
// the pad in X, and `$82:B232`.
//
// The backdrop is `$82:B1A7`'s, which the top scores' screen calls too: its
// colours, the third layer's map and tiles, and the job that slides it. It
// is called with which thirty-two colours in A, as an offset from
// `$92:FF91`. `$82:B229` asks for that job's end.
//
// The stretches below are each from where a call or a wait comes back to
// the next call, wait or return. They leave every register and flag as the
// ROM does. The two that write to the picture hardware are in `port/dma.h`.
//
// The screen whose number is 4 is the top scores' (`port/game_over.h`). It
// has a second line of words, the player's, by the table's pad, and no
// routine of its own: `$82:B378` finds a nought where the routine's address
// would be and returns with carry clear, so the entry always flashes.
//
// Not here: the grid's `$3A`, which takes a character back.

#define PASSWORD_SCREENS 0x82b000u  // a table's address for each pad
#define CURSOR_SCREEN_WORDS 0x82b6b2u    // each screen's words
#define CURSOR_SCREEN_PLACES 0x82b5e3u   // where its entry is printed
#define CURSOR_ENTRY_PLAIN 0x82b5e9u     // how the entry is printed
#define CURSOR_ENTRY_LIT 0x82b5fdu       // ...and when it flashes
#define CURSOR_SCREEN_TWO_LINES 0x0004u  // the top scores' screen
#define CURSOR_SECOND_LINES 0x82b7d7u    // its second line, by the pad
#define CURSOR_SECOND_LINE_PC 0x82b551u  // `JSL` the printer, for that line
#define CURSOR_SHOWN_PC 0x82b558u
#define W_CURSOR_CHECK 0x00e0u           // where the check is jumped through
#define CURSOR_SCREEN_BACKDROP 0x0008u   // which of the backdrop's colours
#define CURSOR_TURN_TICKS 0x0004u
#define CURSOR_CHECK_RETURN 0xb394u      // pushed for the check's `RTL`
#define W_CURSOR_FLASHES 0x1e92u
#define W_CURSOR_FLASH_SCREEN 0x1e96u    // `W_CURSOR_COLUMN`, when it is done
#define W_CURSOR_SPRITES_JOB 0x0068u     // where each job is in the queue
#define W_BACKDROP_SLIDE_JOB 0x006cu
#define CURSOR_SPRITES_JOB 0x8000u       // `$84:8000`
#define CURSOR_SPRITES_JOB_BANK 0x0084u
#define CURSOR_SPRITE 0xd7e5u            // `$8F:D7E5`
#define CURSOR_SPRITE_BANK 0x008fu
#define CURSOR_SOUND_RIGHT 0x0005u
#define CURSOR_SOUND_WRONG 0x0013u
#define CURSOR_FLASH_TICKS 0x0004u
#define CURSOR_WRONG_TICKS 0x000au
#define BACKDROP_COLOURS 0xff91u         // in bank `$92`
#define BACKDROP_COLOURS_BANK 0x0092u
#define BACKDROP_COLOURS_BYTES 0x0020u
#define BACKDROP_SLIDE_JOB 0xb1f9u       // `$82:B1F9`
#define BRIGHTNESS_DARK 0x0080u          // the brightness shadow's bit

#define CURSOR_CHECK_CALL_PC 0x82b378u
#define CURSOR_CHECKED_PC 0x82b2deu
#define CURSOR_RIGHT_PC 0x82b2e3u  // `JSL apu_play_sfx`, either way
#define CURSOR_WRONG_PC 0x82b318u
#define CURSOR_FLASH_NEXT_PC 0x82b3f0u
#define CURSOR_FLASH_AGAIN_PC 0x82b3a2u
#define CURSOR_FLASH_DONE_PC 0x82b3f5u  // `RTS`

// Is the far pointer at a table in the cartridge, for a screen the tables
// have and this takes?
bool cursor_screen_known(const Wram* w, const Rom* rom, uint16_t page);
// ...and is its routine the password's check, or none?
bool cursor_check_known(const Wram* w, const Rom* rom, uint16_t page);
bool cursor_check_none(const Wram* w, const Rom* rom, uint16_t page);

// `$82:B537`: true for the screen with a second line, which stops on the
// printer's `JSL` for it rather than going on to be shown.
bool cursor_screen_one_line(Wram* w, const Rom* rom, PortCpu* c);

// `$82:B378`: the screen's own routine, jumped to with a return pushed.
void cursor_check_call(Wram* w, const Rom* rom, PortCpu* c);
// `$82:B2DE`: true if it left carry clear.
bool cursor_checked(Wram* w, PortCpu* c);
// `$82:B3F0`: true if the entry is to flash again.
bool cursor_flash_next(Wram* w, PortCpu* c);

// What a stretch has to find before it can be the port's.
typedef enum {
  CURSOR_NEEDS_NOTHING,
  CURSOR_NEEDS_PAD,     // X a pad: 0 or 2
  CURSOR_NEEDS_SCREEN,  // `cursor_screen_known`
  CURSOR_NEEDS_FLASH,   // the screen it kept to flash is one the tables have
  CURSOR_NEEDS_RECORD,  // A a display record
} CursorNeeds;

bool cursor_needs_met(CursorNeeds needs, const Wram* w, const Rom* rom,
                      const PortCpu* c);

// A name, where it starts, the instruction it stops on and what it needs.
#define CURSOR_STRETCHES(X) \
  X(password_screen_ask, "$82:AFF3", 0x82aff3u, 0x82affbu, CURSOR_NEEDS_PAD) \
  X(screen_begin, "$82:B232", 0x82b232u, 0x82b23eu, CURSOR_NEEDS_NOTHING) \
  X(screen_dim, "$82:B4B7", 0x82b4b7u, 0x82b4c0u, CURSOR_NEEDS_NOTHING) \
  X(screen_backdrop_ask, "$82:B518", 0x82b518u, 0x82b51cu, CURSOR_NEEDS_NOTHING) \
  X(backdrop_colours_ask, "$82:B1A7", 0x82b1a7u, 0x82b1b2u, CURSOR_NEEDS_NOTHING) \
  X(backdrop_slide_ask, "$82:B1E7", 0x82b1e7u, 0x82b1eeu, CURSOR_NEEDS_NOTHING) \
  X(backdrop_slide_kept, "$82:B1F2", 0x82b1f2u, 0x82b1f8u, CURSOR_NEEDS_NOTHING) \
  X(backdrop_slide_job_ask, "$82:B229", 0x82b229u, 0x82b22cu, CURSOR_NEEDS_NOTHING) \
  X(screen_words_ask, "$82:B523", 0x82b523u, 0x82b533u, CURSOR_NEEDS_SCREEN) \
  X(screen_shown, "$82:B555", 0x82b555u, 0x82b558u, CURSOR_NEEDS_NOTHING) \
  X(screen_undim, "$82:B559", 0x82b559u, 0x82b562u, CURSOR_NEEDS_NOTHING) \
  X(sprites_ask, "$82:B588", 0x82b588u, 0x82b58eu, CURSOR_NEEDS_NOTHING) \
  X(sprites_job_kept, "$82:B596", 0x82b596u, 0x82b59cu, CURSOR_NEEDS_NOTHING) \
  X(record_begin, "$82:B5A4", 0x82b5a4u, 0x82b5deu, CURSOR_NEEDS_RECORD) \
  X(pads_forget, "$82:B574", 0x82b574u, 0x82b583u, CURSOR_NEEDS_NOTHING) \
  X(entry_clear, "$82:B241", 0x82b241u, 0x82b263u, CURSOR_NEEDS_NOTHING) \
  X(flash_begin, "$82:B396", 0x82b396u, 0x82b3bau, CURSOR_NEEDS_SCREEN) \
  X(flash_lit, "$82:B3A2", 0x82b3a2u, 0x82b3bau, CURSOR_NEEDS_FLASH) \
  X(flash_lit_sleep, "$82:B3C2", 0x82b3c2u, 0x82b3c5u, CURSOR_NEEDS_NOTHING) \
  X(flash_plain, "$82:B3C9", 0x82b3c9u, 0x82b3e1u, CURSOR_NEEDS_FLASH) \
  X(flash_plain_sleep, "$82:B3E9", 0x82b3e9u, 0x82b3ecu, CURSOR_NEEDS_NOTHING) \
  X(record_free_ask, "$82:B2EA", 0x82b2eau, 0x82b2edu, CURSOR_NEEDS_NOTHING) \
  X(screen_dark, "$82:B2FD", 0x82b2fdu, 0x82b303u, CURSOR_NEEDS_NOTHING) \
  X(sprites_job_ask, "$82:B307", 0x82b307u, 0x82b30au, CURSOR_NEEDS_NOTHING) \
  X(screen_end, "$82:B30F", 0x82b30fu, 0x82b314u, CURSOR_NEEDS_NOTHING) \
  X(wrong_sleep, "$82:B31C", 0x82b31cu, 0x82b31fu, CURSOR_NEEDS_NOTHING) \
  X(wrong_again, "$82:B323", 0x82b323u, 0x82b326u, CURSOR_NEEDS_NOTHING) \
  X(wrong_again_sleep, "$82:B32A", 0x82b32au, 0x82b32du, CURSOR_NEEDS_NOTHING) \
  X(entry_print_ask, "$82:B48E", 0x82b48eu, 0x82b4a6u, CURSOR_NEEDS_SCREEN) \
  X(pick_end, "$82:B4AE", 0x82b4aeu, 0x82b4b0u, CURSOR_NEEDS_NOTHING)

#define X(name, sym, entry, exit, needs) \
  void cursor_##name(Wram* w, const Rom* rom, PortCpu* c);
CURSOR_STRETCHES(X)
#undef X

#endif
