// A level's name coming down the screen, and the wait for a button.
//
// `$82:AE6F` brings the layer a level's name is drawn on down from above:
// sixteen frames of sixteen lines, a sound, and then a bounce whose steps
// are a table of words at `$82:AEF1` that ends in a zero. Each frame it
// queues the job at `$82:AEB4`, which puts the layer's scroll shadow in the
// register. If the queue is full it waits a frame and asks again.
//
// `$82:BA11` waits for either player to press a button, or for six
// seconds. It is a thread, and its wait is the scheduler's. It is not the
// level's name that it waits on. The routine first draws a screen whose
// title, at `$82:BAFE`, is the cartridge's words for the top scores, and
// under it ten lines from the table at `$7E:2064`. I filed it here before I
// had read that far.
//
// `scores_line` is that screen's loop, `$82:BA9B`, from where the printer
// comes back with one line to where it is called for the next: last line
// first, a place for each from `$82:B9D5` and its row of the table, which
// has fifteen bytes a row. After the first line it ends where the ROM sends
// the map.
//
// These are a frame of each loop: from the instruction after a `WAI`, or
// after the return from a yield, to the next.
//
// Port code: libc only.

#ifndef PORT_CARD_H
#define PORT_CARD_H

#include <stdbool.h>
#include <stdint.h>

#include "assets/rom.h"
#include "port/cpu.h"
#include "port/wram.h"

#define CARD_DROP_PC 0x82ae76u
#define CARD_DROP_WAI_PC 0x82ae75u
#define CARD_DROP_END_PC 0x82ae91u    // `LDA #$0021`, for the sound
#define CARD_BOUNCE_PC 0x82aea2u
#define CARD_BOUNCE_WAI_PC 0x82aea1u
#define CARD_BOUNCE_END_PC 0x82aeb3u  // `RTS`
#define CARD_WAIT_PC 0x82ba26u
#define CARD_WAIT_YIELD_PC 0x82ba22u  // `JSL thread_yield`, A already 1
#define CARD_WAIT_END_PC 0x82ba36u
#define SCORES_LINE_PC 0x82bab9u
#define SCORES_LINE_PRINT_PC 0x82bab5u  // `JSL text_print`
#define SCORES_LINE_DONE_PC 0x82bac5u   // `JSL`, to send the map

#define CARD_BANK 0x82
#define CARD_JOB 0xaeb4u
#define CARD_BOUNCE_TABLE 0x82aef1u
#define CARD_BOUNCE_STEPS 34      // words in the table before its zero
#define CARD_DROP_LINES 0x0010
#define CARD_WAIT_BUTTONS 0xd0c0u
#define W_CARD_SCROLL_Y 0x1362u   // `W_LOGO_SCROLL_Y`: the first layer's
#define W_CARD_BOUNCE_AT 0x1e96u
#define W_CARD_BOUNCE_STEP 0x1e9au
#define W_CARD_FRAMES 0x1e9cu
#define W_CARD_WAIT_FRAMES 0x004eu
#define W_CARD_PADS 0x006eu       // what each player holds, a word each
#define SCORES_PLACES 0xb9d5u     // four bytes a line: where it is printed
#define SCORES_ROWS 0x82b9fdu     // ...and a word: its row of the table
#define SCORES_LINES 10
#define SCORES_PLACE_BYTES 4
#define SCORES_ROW_BANK 0x007e
#define W_SCORES_LINE 0x1e94u     // which line, times four
#define W_SCORES_ROW_BANK 0x004eu // `W_TEXT_STRING_BANK`

enum {
  CD_QUEUE,        // LDA #$AEB4 : LDY #$0082 : JSL : BCS, less the call's body
  CD_DROP,         // $AE82-$AE90
  CD_BOUNCE,       // JSR $AEC6, $AEC6-$AEEC
  CD_BOUNCE_END,   // SEC : RTS : BCC
  CD_BOUNCE_MORE,  // CLC : RTS : BCC
  CD_WAIT_PADS,    // $BA26-$BA30
  CD_WAIT_COUNT,   // DEC $004E : BPL
  CD_WAIT_AGAIN,   // LDA #$0001
  CD_TAKEN,
  CD_BLOCK_COUNT
};

typedef struct {
  uint16_t blocks[CD_BLOCK_COUNT];
  int slot;  // the queue's, or -1 if it was full
} CardWork;

// False once the bounce's place is past its table.
bool card_bounce_supported(const Wram* w);

void card_slide_frame(Wram* w, const Rom* rom, PortCpu* c, bool bounce,
                      CardWork* k);
void card_wait_frame(Wram* w, PortCpu* c, CardWork* k);

// False unless the line is one of the ten.
bool scores_line_supported(const Wram* w);
// True if there was another line to print.
bool scores_line(Wram* w, const Rom* rom, PortCpu* c);

#endif
