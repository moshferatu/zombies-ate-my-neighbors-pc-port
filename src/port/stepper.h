// A thing that steps round four places.
//
// `$83:9D00` is a thread. Every twenty frames it moves its thing by the next
// of four steps, and round again. It shows one picture and then another,
// each for the frames a table gives. It goes on until something sets the
// word at `$1E` on its page, and what it does then is by what was set.
//
// This is a turn of that loop, from the return of its sleep at `$83:9D2C`
// to the next sleep, or to `$83:9D7C` with the word in A.
//
// I have not seen it on a screen.
//
// Port code: libc only.

#ifndef PORT_STEPPER_H
#define PORT_STEPPER_H

#include <stdbool.h>
#include <stdint.h>

#include "assets/rom.h"
#include "port/cpu.h"
#include "port/wram.h"

#define STEPPER_PC 0x839d2cu
#define STEPPER_SLEEP_PC 0x839d28u  // `JSL`, A already 1
#define STEPPER_TOLD_PC 0x839d7cu

#define STEPPER_BANK 0x83
#define STEPPER_STEPS 0x839e05u     // across and down, four of them
#define STEPPER_PICTURES 0x839ddbu  // a picture and its frames, twice
#define STEPPER_PLACES 4
#define STEPPER_STEP_FRAMES 0x0014
#define STEPPER_DP_RECORD 0x08
#define STEPPER_DP_PICTURE_LEFT 0x0c  // frames until the other picture
#define STEPPER_DP_STEP_LEFT 0x0e     // ...and until the next step
#define STEPPER_DP_WHICH 0x10
#define STEPPER_DP_PLACE 0x12
#define STEPPER_DP_TOLD 0x1e
#define STEPPER_DP_X 0x20
#define STEPPER_DP_Y 0x24

enum {
  ST_HEAD,     // DEC $0E : BNE
  ST_PLACE,    // LDA $12 : INC : CMP #$0004 : BNE
  ST_ROUND,    // LDA #$0000
  ST_STEP,     // $9D3B-$9D5C
  ST_PICTURE_TEST,  // DEC $0C : BNE
  ST_PICTURE,  // $9D61-$9D77
  ST_TOLD,     // LDA $1E : BEQ
  ST_AGAIN,    // LDA #$0001
  ST_TAKEN,
  ST_BLOCK_COUNT
};

typedef struct {
  uint16_t blocks[ST_BLOCK_COUNT];
} StepperWork;

void stepper_frame(Wram* w, const Rom* rom, PortCpu* c, StepperWork* k);

#endif
