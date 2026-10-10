// The places the game stands still until something else has happened.
//
// The ROM waits by going round a loop. It reads a word, tests it, and
// branches back if the answer is not the one it wants:
//
//     $80:9FAA  BIT $00C8 : BPL $9FAA
//
// Nothing in the loop changes the word. The vblank does, or a player does,
// and the loop is how the main program lets them: the NMI lands somewhere in
// it, and some turn after that reads what the NMI left.
//
// Twelve of them are here, in one table. Each is one turn of its loop,
// `hold_turn`: the read, the test, and where the branch goes. A turn that
// finds the wait over leaves at the instruction after the branch. Any other
// leaves at the loop's first instruction again, which is this port's entry
// again. So whoever drives it asks again after some time has gone by, and
// how much is theirs to say: the harness spends a turn's cycles, and a
// driver with no CPU can wait for the next vblank.
//
// What they wait for:
//
//   * **VRAM cleared.** `$80:9F9D` queues a vblank job that clears 1,024
//     words a frame, and holds until the job's address has reached `$8000`.
//   * **An upload gone.** `$80:9F29` queues a job that sends a tile map to
//     VRAM, and holds until the bytes left are none.
//   * **The VRAM queue flushed.** `$80:A4D9` queues a screen of tiles and
//     holds until the queue's count is zero.
//   * **The card held.** The level's card stays up for 120 frames by the
//     NMI's frame counter, twice: once after the level's music is asked for
//     and once after its sounds are sent.
//   * **A fade.** A vblank job counts the brightness up to 15, or down past
//     zero and then sets bit 7, and the screen that asked holds until it has.
//   * **The pause.** With Start held, the pause check holds until it is let
//     go, then until it is pressed again, then until it is let go again.
//
// ## Its contract with the ROM
//
// A turn writes nothing. It leaves the registers as the loop's instructions
// do: A as loaded, where the loop loads it, and the flags of its last test.
// The accumulator is 16 bits wide in all of them. All but one read their
// word by its address. The upload's reads `$C6` through the direct page,
// which is zero there, so the word is the same one.
//
// Port code: libc only.

#ifndef PORT_HOLD_H
#define PORT_HOLD_H

#include <stdbool.h>
#include <stdint.h>

#include "port/cpu.h"
#include "port/wram.h"

// What a turn reads, and what ends the wait. The instructions are in the
// order the ROM has them.
typedef enum {
  HOLD_TOP_BIT,   // `BIT at : BPL`                    its top bit is set
  HOLD_ZERO,      // `LDA at : BNE`                    it is zero
  HOLD_AT_LEAST,  // `LDA at : CMP #n : BCC`           it is `n` or more
  HOLD_EQUAL,     // `LDA at : CMP #n : BNE`           it is `n`
  HOLD_ANY_BIT,   // `LDA at : AND #n : BEQ`           it has a bit of `n`
  HOLD_PADS_OFF,  // `LDA at : ORA at+2 : BIT #n : BNE`  neither pad has
  HOLD_PADS_ON,   // ...and with `BEQ`                 either pad has
  HOLD_SHAPE_COUNT
} HoldUntil;

typedef struct {
  uint32_t pc;       // the loop's first instruction
  uint32_t over_pc;  // the one after its branch
  uint8_t until;
  uint16_t at;       // the word read, in WRAM's first 8 KB
  uint16_t n;
} Hold;

#define HOLD_PAD_ONE 0x006eu  // pad two's word is the next
#define HOLD_PAD_START 0x1000u
#define HOLD_CARD_FRAMES 0x0078u
#define HOLD_FADED_IN 0x000fu
#define HOLD_FADED_OUT 0x0080u

#define HOLD_VRAM_CLEAR_AT 0x00c8u   // where the clearing job has got to
#define HOLD_UPLOAD_LEFT 0x00c6u     // the bytes an upload has still to send

//  name, where, the instruction after its branch, until, the word, n
#define HOLDS(X) \
  X(vram_cleared,      "$80:9FAA", 0x809faau, 0x809fafu, HOLD_TOP_BIT,  HOLD_VRAM_CLEAR_AT,  0) \
  X(upload_gone,       "$80:9F5C", 0x809f5cu, 0x809f60u, HOLD_ZERO,     HOLD_UPLOAD_LEFT,    0) \
  X(queue_flushed,     "$80:A545", 0x80a545u, 0x80a54au, HOLD_ZERO,     W_VRAM_QUEUE_COUNT,  0) \
  X(card_music,        "$82:AC65", 0x82ac65u, 0x82ac6du, HOLD_AT_LEAST, W_NMI_FRAME_COUNTER, HOLD_CARD_FRAMES) \
  X(card_sounds,       "$82:AC92", 0x82ac92u, 0x82ac9au, HOLD_AT_LEAST, W_NMI_FRAME_COUNTER, HOLD_CARD_FRAMES) \
  X(screen_faded_in,   "$80:923A", 0x80923au, 0x809242u, HOLD_EQUAL,    W_BRIGHTNESS_SHADOW, HOLD_FADED_IN) \
  X(screen_faded_out,  "$80:924C", 0x80924cu, 0x809254u, HOLD_ANY_BIT,  W_BRIGHTNESS_SHADOW, HOLD_FADED_OUT) \
  X(level_faded_out,   "$80:9B94", 0x809b94u, 0x809b9cu, HOLD_ANY_BIT,  W_BRIGHTNESS_SHADOW, HOLD_FADED_OUT) \
  X(opening_faded_out, "$80:933A", 0x80933au, 0x809342u, HOLD_ANY_BIT,  W_BRIGHTNESS_SHADOW, HOLD_FADED_OUT) \
  X(pause_let_go,      "$80:89C8", 0x8089c8u, 0x8089d3u, HOLD_PADS_OFF, HOLD_PAD_ONE,        HOLD_PAD_START) \
  X(pause_pressed,     "$80:89D3", 0x8089d3u, 0x8089deu, HOLD_PADS_ON,  HOLD_PAD_ONE,        HOLD_PAD_START) \
  X(pause_ended,       "$80:89DE", 0x8089deu, 0x8089e9u, HOLD_PADS_OFF, HOLD_PAD_ONE,        HOLD_PAD_START)

enum {
#define X(name, sym, pc, over_pc, until, at, n) HOLD_##name,
  HOLDS(X)
#undef X
  HOLD_COUNT
};
extern const Hold HOLDS_BY[HOLD_COUNT];

// One turn of the loop. True when the wait is over, and then it leaves at
// `over_pc`. False leaves it at `pc`, to be asked again.
bool hold_turn(const Wram* w, PortCpu* c, const Hold* hold);

#endif
