// The game over's thread, between its calls.
//
// After the last life the main thread calls `$80:8A00`. It sleeps half a
// second, clears the text map, and then asks for one job a frame:
//
//   $80:8A7D  the third layer's scroll shadow put 32 lines down, and the job
//             that sends the text map asked for, `$80:8B17`
//   $80:8A92  ...the job that puts the third layer's map at `$4800` and
//             sends its tiles, `$80:8B2B`
//   $80:8A9D  ...the job that sends the first half of the words, `$80:8B48`
//   $80:8AA8  ...the job that sends the shadow, `game_over_scroll_job`
//   $80:8AB3  ...and, on the queue after the picture, the colours' job
//   $80:8ABE  its four sprites begun: `game_over_sprite_begin` called with X
//             at 0, 2 and 4, and the fourth falls into it with X at 6
//
// Then the first loop of `game_over_frame` (`port/frontend.h`), the job that
// sends the second half of the words, `$80:8B5C`, asked for at `$80:8A1F`,
// and the second loop. It rests five seconds at `$80:8A3E`, and `$80:8A45`
// takes the brightness down a step a frame, a `WAI` each. At nothing it
// blanks the screen, waits one frame more and returns.
//
// The `WAI`s between the stretches, and the calls and returns either side of
// them, are links (`port/links.h`). So are the main thread's calls round
// this one: `top_scores_check` below, the top scores' screen, and the clear.
//
// The three jobs that only send are in `port/dma.h`'s table of what a send
// is called with, and each ends `PLA : CLC : RTL`, which is
// `game_over_job_sent` here: run once, and taken off the queue.
//
// Port code: libc only.

#ifndef PORT_GAME_OVER_H
#define PORT_GAME_OVER_H

#include <stdbool.h>
#include <stdint.h>

#include "assets/rom.h"
#include "port/cpu.h"
#include "port/wram.h"

#define GAME_OVER_JOB_BANK 0x0080u
#define GAME_OVER_SCROLL_FIRST 0xffe0u  // 32 lines down
#define GAME_OVER_WAIT_PC 0x808a45u
#define GAME_OVER_FADE_WAI_PC 0x808a4du
#define GAME_OVER_BLANK_WAI_PC 0x808a56u
#define GAME_OVER_SPRITE_PC 0x808ad3u  // `game_over_sprite_begin`

enum {
  GO_SCROLL_SET,  // $8A7D-$8A8C: the shadow, and the job's two words
  GO_ASK,         // LDA # : LDY #
  GO_SPRITE,      // LDX # : JSR $8AD3
  GO_LAST_SPRITE, // LDX #$0006, into it
  GO_REST,        // LDA #
  GO_FADE_TEST,   // LDA $136C : BEQ
  GO_FADE,        // DEC $136C
  GO_BLANK,       // LDA #$0080 : STA $136C
  GO_JOB_SENT,    // PLA : CLC
  GO_TAKEN,
  GO_BLOCK_COUNT
};

typedef struct {
  uint16_t blocks[GO_BLOCK_COUNT];
} GameOverWork;

// name, symbol, entry, and for an ask the job, for a sprite X, and for a
// rest the ticks. The exit is the next call or `WAI`.
//
// An ask's job is the stock ROM's, for the reader: the shim reads it from the
// `LDA #` in the cartridge, as the 65816 would, because `maskline.h` exchanges
// the second and the fourth in the game's copy.
#define GAME_OVER_ASKS(X) \
  X(text_map_ask, "$80:8A92", 0x808a92u, 0x8b2bu) \
  X(words_ask, "$80:8A9D", 0x808a9du, 0x8b48u) \
  X(scroll_ask, "$80:8AA8", 0x808aa8u, 0x8b70u) \
  X(colours_ask, "$80:8AB3", 0x808ab3u, 0x8b82u) \
  X(words_2_ask, "$80:8A1F", 0x808a1fu, 0x8b5cu)

#define GAME_OVER_SPRITES_BEGIN(X) \
  X(sprite_0, "$80:8ABE", 0x808abeu, 0) \
  X(sprite_1, "$80:8AC4", 0x808ac4u, 2) \
  X(sprite_2, "$80:8ACA", 0x808acau, 4)

#define GAME_OVER_RESTS(X) \
  X(first_frame, "$80:8A0A", 0x808a0au, 0x0001) \
  X(second_frame, "$80:8A29", 0x808a29u, 0x0001) \
  X(rest, "$80:8A3E", 0x808a3eu, 0x012c)

#define GAME_OVER_JOBS_SENT(X) \
  X(text_map_sent, "$80:8B28", 0x808b28u) \
  X(layer_sent, "$80:8B45", 0x808b45u) \
  X(words_sent, "$80:8B59", 0x808b59u) \
  X(words_2_sent, "$80:8B6D", 0x808b6du)

#define GAME_OVER_SCROLL_SET_PC 0x808a7du
#define GAME_OVER_SCROLL_SET_EXIT 0x808a8du  // `JSL vbl_queue_a_add`
#define GAME_OVER_TEXT_MAP_JOB 0x8b17u
#define GAME_OVER_LAST_SPRITE_PC 0x808ad0u

// `$80:8A7D`.
void game_over_scroll_set(Wram* w, PortCpu* c, GameOverWork* k);
// One of the asks: A the job, Y its bank.
void game_over_ask(PortCpu* c, uint16_t job, uint32_t exit, GameOverWork* k);
// One of the sprites, `JSR` and all; `last` is the fourth, with no `JSR`.
void game_over_sprite(Wram* w, PortCpu* c, uint16_t x, bool last,
                      GameOverWork* k);
// One of the sleeps: A the ticks.
void game_over_rest(PortCpu* c, uint16_t ticks, uint32_t exit,
                    GameOverWork* k);
// `$80:8A45`, to its `WAI`.
void game_over_wait(Wram* w, PortCpu* c, GameOverWork* k);
// A job's `PLA : CLC`, to its `RTL`.
void game_over_job_sent(Wram* w, PortCpu* c, GameOverWork* k);

// ## The top scores, after a game over
//
// `$82:BBED` asks of each player in turn whether their score belongs in the
// table: it does when it is at least the tenth's. For one that does,
// `$82:B232` takes their name on the cursor's screen (`port/cursor.h`), the
// one whose number is 4: a table at `$82:BD56` for the first player and at
// `$82:BD60` for the second, with no routine of its own to check the entry
// and a second line of words, which names the player. The second player is
// asked after the first is filed, so against the table as it then is.
// Carry is set at the end if either went in, and `$42` counts them.
//
// **Filing one** (`$82:BC4C`, where the screen comes back):
//
//   * the entry's blanks, of its first fourteen places, become spaces;
//   * its places 6 to 13 are the score, eight digits from its four bytes of
//     BCD, with spaces for the noughts in front of the first that is not;
//   * its row is found by going up from the tenth while the score is at
//     least the one above;
//   * the rows from there down move one down, the scores and then the
//     names, by `MVP`, and the tenth goes;
//   * the score goes in, and the entry's first fifteen bytes are the name.
//
// So of two equal scores the newer is the higher.
//
// `top_scores_ask` is from the start and `top_scores_file` from where the
// screen comes back. Each goes on to the next screen's `JSL` or to the
// `RTL`, and leaves the machine as the ROM does.
#define TOP_SCORES_CHECK_PC 0x82bbedu
#define TOP_SCORES_FILE_PC 0x82bc4cu
#define TOP_SCORES_SCREEN_PC 0x82bc48u   // `JSL $82:B232`
#define TOP_SCORES_RTL_PC 0x82bc40u
#define TOP_SCORES_SCREENS 0x82bd52u     // a table's address for each player
#define TOP_SCORES_RET_FIRST 0xbc18u     // what each `JSR $BC41` pushes
#define TOP_SCORES_RET_SECOND 0xbc36u
#define TOP_SCORES_LAST_ROW 0x0024u      // the tenth, four bytes a row
#define TOP_SCORES_COUNT 0x0042u
#define TOP_SCORES_NAME_ROWS 0x82b9fdu   // the bank `$7E` address of each name
#define TOP_SCORES_NAME_BYTES 0x000fu
#define TOP_SCORES_DIGITS_AT 6           // where in the entry the score goes
#define TOP_SCORES_BLANKS 0x0e           // places of the entry made spaces
#define TOP_SCORES_SPACE 0x2f
#define TOP_SCORES_NOUGHT 0x30

bool top_scores_qualify(const Wram* w, int player);

// The straight runs of the listing, each priced in the harness.
enum {
  TOPS_HEAD,         // $BBED-$BBFB
  TOPS_FIRST,        // LDA $1E74 : CMP : BCC
  TOPS_SECOND,       // PLX : LDA $1E78 : CMP : BCC
  TOPS_EQUAL,        // BNE
  TOPS_LOW,          // LDA : CMP : BCC
  TOPS_QUALIFIES,    // INC $42 : LDX # : JSR $BC41
  TOPS_SCREEN,       // PHX : LDY $BD52,X : LDA #$0082
  TOPS_TAIL,         // CLC : LDA $42 : BEQ
  TOPS_SEC,          // SEC
  TOPS_END,          // PLB : PLB
  TOPS_FILE_HEAD,    // $BC4C-$BC51
  TOPS_BLANK_TEST,   // LDX $1EA0,Y : BNE
  TOPS_BLANK,        // STA $1EA0,Y
  TOPS_BLANK_NEXT,   // INY : CPY : BNE
  TOPS_SCORE_AT,     // $BC5F-$BC73
  TOPS_HIGH,         // $BC74-$BC80, to the `JSR`
  TOPS_LOW_NIBBLE,   // $BC81-$BC89
  TOPS_NEXT_BYTE,    // DEX : DEC $1E96 : BPL
  TOPS_DIGIT,        // `$82:BD35`, a digit
  TOPS_DIGIT_NOUGHT, // ...a nought after one
  TOPS_DIGIT_SPACE,  // ...or before any
  TOPS_ROW_HEAD,     // $BC90-$BC9B
  TOPS_ROW_HIGH,     // LDA $0002,X : CMP : BEQ
  TOPS_ROW_HIGHER,   // BCS
  TOPS_ROW_BRA,      // BRA
  TOPS_ROW_LOW,      // LDA $0000,X : CMP : BCS
  TOPS_ROW_UP,       // TYA : SEC : SBC : TAY : BPL
  TOPS_ROWS,         // $BCBA-$BCE0
  TOPS_DEC,          // DEC
  TOPS_MOVED,        // a byte an `MVP` or `MVN` moves
  TOPS_SCORE_IN,     // $BCE5-$BCFB
  TOPS_NAMES,        // $BCFC-$BD22
  TOPS_NAME_IN,      // $BD26-$BD2E
  TOPS_FILE_END,     // PLB : PLB : RTS
  TOPS_TAKEN,
  TOPS_RUN_COUNT
};

typedef struct {
  uint16_t runs[TOPS_RUN_COUNT];
} TopScoresWork;

// True if the stack under a filing is the one `$82:BC41` leaves: the
// player and the return to the check, and for the first player the tenth's
// row.
bool top_scores_file_known(const Wram* w, uint16_t s);

void top_scores_ask(Wram* w, const Rom* rom, PortCpu* c, TopScoresWork* k);
void top_scores_file(Wram* w, const Rom* rom, PortCpu* c, TopScoresWork* k);

#endif
