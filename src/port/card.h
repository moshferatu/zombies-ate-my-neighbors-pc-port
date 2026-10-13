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
#define CARD_DROP_END_PC 0x82ae94u    // `JSL`, the sound in A
#define CARD_DROP_SOUND 0x0021
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
  CD_SOUND,        // LDA #$0021
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

// The rest of the top scores' screen, `$82:BA11`, each from where a call or
// a wait comes back to the next call, wait or return. It dims the picture,
// sends its colours and tiles and the backdrop's (`port/cursor.h`), prints
// the ten lines and its title over them, and fades up. Then the wait above,
// and it fades down and stops the backdrop's job. The two that write to the
// picture hardware are in `port/dma.h`.
#define SCORES_BACKDROP 0xfff8u  // which of the backdrop's colours
#define SCORES_TITLE 0xbafeu     // the screen's title, in this bank
#define SCORES_WAIT_FRAMES 0x0168u  // six seconds
#define SCORES_WAIT_TICKS 0x0001u

#define SCORES_STRETCHES(X) \
  X(screen_begin, "$82:BA11", 0x82ba11u, 0x82ba16u) \
  X(screen_dim, "$82:BA4F", 0x82ba4fu, 0x82ba58u) \
  X(backdrop_ask, "$82:BA7F", 0x82ba7fu, 0x82ba82u) \
  X(first_line, "$82:BA92", 0x82ba92u, 0x82bab5u) \
  X(title_ask, "$82:BAC9", 0x82bac9u, 0x82bad2u) \
  X(screen_shown, "$82:BAD6", 0x82bad6u, 0x82bad9u) \
  X(screen_undim, "$82:BADA", 0x82badau, 0x82bae3u) \
  X(pads_forget, "$82:BAF7", 0x82baf7u, 0x82bafdu) \
  X(wait_begin, "$82:BA19", 0x82ba19u, 0x82ba22u) \
  X(screen_dark, "$82:BA42", 0x82ba42u, 0x82ba48u) \
  X(screen_end, "$82:BA4C", 0x82ba4cu, 0x82ba4eu)

#define X(name, sym, entry, exit) \
  void scores_##name(Wram* w, const Rom* rom, PortCpu* c);
SCORES_STRETCHES(X)
#undef X

#endif
