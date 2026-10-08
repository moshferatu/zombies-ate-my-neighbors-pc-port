// The neighbour who jumps on the spot.
//
// `$83:9EBE` is one of the threads a level's list gives a neighbour. This
// one stands for sixty frames, then goes up a pixel a frame for twenty and
// comes down a pixel a frame for twenty, and stands again. While it is in
// the air its display record has no collision id, so nothing can touch it.
//
// These are the two loops, a frame each from the wake to the next yield.
// Either stops at `$83:9F41` when the neighbour's event word has gone
// negative, which is the ROM's to deal with, and the second stops at
// `$83:9EDB` when it has landed.
//
// The rest of the round is here too. `jumper_stand` is `$83:9EDB`: on the
// ground, the record under it set to be drawn, the standing picture, and
// the sixty frames asked for. `jumper_rested` is where those sixty end,
// `$83:9EED`: the picture for the air, no collision id, and the first pixel
// up. An event of any kind in the sixty frames stops it at `$83:9F41`
// instead.
//
// The neighbour has a second record, at `$0A` on its page, which stays on
// the ground at the place it started. I have not looked at its picture, so
// I do not say what it is.
//
// Port code: libc only.

#ifndef PORT_JUMPER_H
#define PORT_JUMPER_H

#include <stdbool.h>
#include <stdint.h>

#include "port/cpu.h"
#include "port/wram.h"

#define JUMPER_UP_PC 0x839f11u          // after the first loop's yield
#define JUMPER_UP_YIELD_PC 0x839f0du    // `JSL thread_yield`, A already 1
#define JUMPER_DOWN_PC 0x839f2eu
#define JUMPER_DOWN_YIELD_PC 0x839f2au
#define JUMPER_ENDED_PC 0x839f41u
#define JUMPER_LANDED_PC 0x839edbu
#define JUMPER_STAND_PC JUMPER_LANDED_PC
#define JUMPER_STAND_YIELD_PC 0x839ee9u  // `JSL thread_yield`, A already 60
#define JUMPER_RESTED_PC 0x839eedu

// On the neighbour's page. The record and the event are `VICTIM_DP_RECORD`
// and `VICTIM_DP_EVENT` in `port/collide.h`.
#define JUMPER_DP_RECORD 0x08
#define JUMPER_DP_FRAMES 0x10  // left of this way's twenty
#define JUMPER_DP_EVENT 0x1e
#define JUMPER_FRAMES 20
#define JUMPER_LANDED_ID 0x0001
#define JUMPER_DP_UNDER 0x0a   // the record that stays on the ground
#define JUMPER_STAND_PICTURE 0xdf8fu
#define JUMPER_AIR_PICTURE 0xdfb0u
#define JUMPER_STAND_FRAMES 0x003c

enum {
  JP_EVENT,  // LDA $1E : BMI
  JP_COUNT,  // DEC $10 : BNE
  JP_STEP,   // $9F01-$9F0C, and the same at $9F1E-$9F29
  JP_TURN,   // $9F19-$9F1D
  JP_LAND,   // $9F36-$9F40
  JP_TAKEN,  // a branch taken
  JP_SHOW,   // JSR $9FCA, and $9FCA-$9FD5
  JP_STAND,  // $9EDE-$9EE8
  JP_ANY,    // LDA $1E : BNE
  JP_BEGIN,  // $9EF1-$9F00
  JP_BLOCK_COUNT
};

typedef struct {
  uint16_t blocks[JP_BLOCK_COUNT];
} JumperWork;

// `down` is which loop woke.
void jumper_frame(Wram* w, PortCpu* c, bool down, JumperWork* k);

void jumper_stand(Wram* w, PortCpu* c, JumperWork* k);
void jumper_rested(Wram* w, PortCpu* c, JumperWork* k);

#endif
