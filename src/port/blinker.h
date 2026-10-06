// A thing of two pictures, turn about.
//
// `$83:9C6D` is a thread. It shows one picture and then the
// other, twenty frames each, until something sets the word at `$1E` on its
// page. What it does then is by what was set.
//
// This is a turn of that loop, from the return of its sleep at `$83:9C94`
// to the next sleep, or to `$83:9CAF` with the word in A.
//
// I have not seen it on a screen.
//
// Port code: libc only.

#ifndef PORT_BLINKER_H
#define PORT_BLINKER_H

#include <stdbool.h>
#include <stdint.h>

#include "assets/rom.h"
#include "port/cpu.h"
#include "port/wram.h"

#define BLINKER_PC 0x839c94u
#define BLINKER_SLEEP_PC 0x839c90u  // `JSL`, A already the frames
#define BLINKER_TOLD_PC 0x839cafu

#define BLINKER_BANK 0x83
#define BLINKER_PICTURES 0x839cf8u  // a picture and its frames, twice
#define BLINKER_DP_RECORD 0x08
#define BLINKER_DP_WHICH 0x10
#define BLINKER_DP_FRAMES 0x1a
#define BLINKER_DP_TOLD 0x1e

enum {
  BK_TURN,   // $9C94-$9CAE
  BK_AGAIN,  // LDA $1A
  BK_TAKEN,
  BK_BLOCK_COUNT
};

typedef struct {
  uint16_t blocks[BK_BLOCK_COUNT];
} BlinkerWork;

void blinker_frame(Wram* w, const Rom* rom, PortCpu* c, BlinkerWork* k);

#endif
