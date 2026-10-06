// A step along a line.
//
// `$81:D443` moves a thing a frame's worth along a straight line that is
// not one of the eight ways. Each axis has a part it adds to a sum every
// frame, and for every `$12` in the sum the thing moves that axis's step:
// the same sums a leaping player is moved by, in `port/pose.h`. Then where
// it is goes to its display record.
//
// Port code: libc only.

#ifndef PORT_LINE_H
#define PORT_LINE_H

#include <stdbool.h>
#include <stdint.h>

#include "port/cpu.h"
#include "port/wram.h"

#define LINE_STEP_PC 0x81d443u
#define LINE_STEP_RTS_PC 0x81d481u

#define LINE_DP_RECORD 0x08
#define LINE_DP_X 0x0a
#define LINE_DP_Y 0x0c
#define LINE_DP_WHOLE 0x12   // how much of a sum is a step
#define LINE_DP_STEP_X 0x14  // what a step moves it
#define LINE_DP_STEP_Y 0x16
#define LINE_DP_PART_X 0x18  // what a frame adds to the sum
#define LINE_DP_PART_Y 0x1a
#define LINE_DP_SUM_X 0x1e
#define LINE_DP_SUM_Y 0x20

enum {
  LN_HEAD,   // LDA $1E : CLC : ADC $18
  LN_TEST,   // CMP $12 : BCC, either axis
  LN_STEP,   // $D44C-$D459, either axis
  LN_MID,    // STA $1E : LDA $20 : CLC : ADC $1A
  LN_TAIL,   // $D473-$D481
  LN_TAKEN,
  LN_BLOCK_COUNT
};

typedef struct {
  uint16_t blocks[LN_BLOCK_COUNT];
} LineWork;

// False with nothing for a sum to be a step of: the ROM would not come back.
bool line_step_supported(const Wram* w, uint16_t page);
void line_step(Wram* w, PortCpu* c, LineWork* k);

#endif
