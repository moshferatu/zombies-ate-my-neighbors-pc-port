// The figure that rises.
//
// `$81:8294` is the end of a thread. It shows five pictures for eight frames
// each, and then for sixty steps of two frames it lifts its display record
// a pixel and shows the next of five pictures, which change every fourth
// step and go round. A is which of two sets of five.
//
// This is a step of that last loop, from the return of its yield at
// `$81:8300` to the next yield, or to the `RTL` after the sixtieth. The
// loop keeps its count and its place in the pictures on the stack across
// the yield, and the port reads and writes them there.
//
// Port code: libc only.

#ifndef PORT_RISER_H
#define PORT_RISER_H

#include <stdbool.h>
#include <stdint.h>

#include "assets/rom.h"
#include "port/cpu.h"
#include "port/wram.h"

#define RISER_PC 0x818300u        // `PLX`
#define RISER_YIELD_PC 0x8182fcu  // `JSL thread_yield`, A already 2
#define RISER_RTL_PC 0x818303u

#define RISER_DP_SET 0x06     // where this set's pictures begin
#define RISER_DP_RECORD 0x08
#define RISER_PICTURES 0x818314u
#define RISER_SET_BYTES 12    // five pictures and a zero
#define RISER_SETS 2

enum {
  RS_HEAD,     // PLX : BRA, $82D4-$82E2
  RS_AROUND,   // $82E3-$82E8
  RS_COUNT,    // $82E9-$82EF
  RS_FOURTH,   // PHA : AND #$0003 : BNE
  RS_NEXT,     // INX : INX
  RS_YIELD,    // PHX : LDA #$0002
  RS_TAKEN,
  RS_BLOCK_COUNT
};

typedef struct {
  uint16_t blocks[RS_BLOCK_COUNT];
} RiserWork;

// False unless the set and the place on the stack are inside the table.
bool riser_frame_supported(const Wram* w, const PortCpu* c);
void riser_frame(Wram* w, const Rom* rom, PortCpu* c, RiserWork* k);

#endif
