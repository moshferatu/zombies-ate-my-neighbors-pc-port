// The thread that fades a level's colours to another set, a step at a time.
//
// A level lists it among its threads, `$82:AB95`, with two palettes to end
// on: one for the background and one for the sprites. Each time it wakes it
// moves one row of sixteen colours of each a step nearer. After the eighth
// row it asks for the colours to be sent to the screen and starts again from
// the first, until a whole sweep moves nothing. This is one waking as
// readable C, from where `thread_yield` returns to the next yield:
//
//   $82:ABBD  frame   a row of each palette, and the end of a sweep
//
// What is not here is the thread's setup, and its last two instructions,
// which say the fade is over and end it.
//
// ## What a step is
//
// A colour is three channels of five bits. Each channel that is not yet its
// target's goes one nearer, so a colour arrives in at most 31 steps and the
// three channels arrive separately.
//
// ## What a sweep is
//
// **Eight rows, the last of them the sprites' only.** The background's eighth
// row is left alone.
//
// **It speeds up.** The thread sleeps `$40` ticks after its first row, and one
// fewer after each row until it is waking every tick.
//
// **The background is kept twice**, at `$7E:5428` and at `$7E:5628`, and a
// step writes both. The sprites' colours are at `$7E:5528`.
//
// ## Its contract with the ROM
//
// It writes WRAM exactly as the ROM does, the working the two row routines
// leave on page zero included. A frame ends at the `JSL thread_yield` with
// the tick count in A, or with the fade over: one more on the count at
// `$7E:1F94`, and the `RTL` at `$82:AC06`. Carry and overflow are left as
// the ROM leaves them.
//
// Port code: libc only.

#ifndef PORT_PALFADE_H
#define PORT_PALFADE_H

#include <stdbool.h>
#include <stdint.h>

#include "assets/rom.h"
#include "port/wram.h"

#define PALFADE_FRAME_PC 0x82abbdu
#define PALFADE_YIELD_PC 0x82abb9u  // `JSL thread_yield`, the ticks already in A
#define PALFADE_OVER_PC 0x82ac06u   // the `RTL` that ends the thread

#define W_PALFADE_ENDED 0x1f94u      // a count of the fades that have ended

// The job that sends the colours to the screen, `$80:9FDF`.
#define PALFADE_UPLOAD_JOB 0x9fdfu
#define PALFADE_UPLOAD_BANK 0x0080u

// Fields on the thread's page.
#define PALFADE_DP_MOVED 0x0a        // rows that moved, this sweep
#define PALFADE_DP_ROW 0x0c          // the row, as a byte offset into a palette
#define PALFADE_DP_TICKS 0x0e        // how long it sleeps
#define PALFADE_DP_BACKGROUND 0x10   // where the background ends up, a far address
#define PALFADE_DP_SPRITES 0x14      // ...and the sprites

#define PALFADE_ROW_BYTES 0x20
#define PALFADE_ROW_COLOURS 16
#define PALFADE_ROWS_END 0x100
#define PALFADE_BACKGROUND_END 0xe0  // the background's rows stop one short

// For the harness, and only for it: what happened, which is what it takes to
// price the ROM's instructions.
typedef struct {
  int kept, raised, lowered;
} PalfadeChannelWork;

typedef struct {
  bool ran;      // not the background's eighth row
  int settled;   // colours already their target's
  PalfadeChannelWork blue, green, red;  // the channels of those that were not
} PalfadeRowWork;

typedef struct {
  PalfadeRowWork background, sprites;
  bool swept;      // that was the eighth row
  bool over;       // ...of a sweep that moved nothing
  uint16_t ended;  // ...and the count of fades ended, with this one
  int queue_slot;  // ...or where the upload went in the queue, -1 for nowhere
  bool sooner;     // it sleeps a tick less from now on
  uint16_t ticks;  // how long it sleeps
  bool c, v;       // carry and overflow as the frame leaves them
} PalfadeLog;

// Can `palfade_frame` take this waking? Not a row that is not one of the eight,
// nor a palette that is not in the cartridge. It only looks.
bool palfade_frame_supported(const Wram* w, uint16_t page);

// One waking, for the thread whose page is `page`. False when the fade is
// over, and the thread is to end. `log` may be NULL.
bool palfade_frame(Wram* w, const Rom* rom, uint16_t page, PalfadeLog* log);

#endif
