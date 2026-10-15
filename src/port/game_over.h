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

// `$82:BBED`: does either player's score belong in the table of top
// scores? It does when it is at least the tenth's. The port is the answer
// no: carry clear. Yes goes on to take their name, and that is the ROM's.
#define TOP_SCORES_CHECK_PC 0x82bbedu
#define TOP_SCORES_CHECK_RTL_PC 0x82bc40u
#define TOP_SCORES_LAST_ROW 0x0024u  // the tenth, four bytes a row
bool top_scores_qualify(const Wram* w, int player);
// ...and what it leaves when neither does: the count of those that did, at
// `$42`, nothing.
#define TOP_SCORES_COUNT 0x0042u
void top_scores_none(Wram* w);

#endif
