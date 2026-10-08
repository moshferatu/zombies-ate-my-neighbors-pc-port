// The figure that rises.
//
// `$81:8294` is the end of a thread. It shows five pictures for eight frames
// each, and then for sixty steps of two frames it lifts its display record
// a pixel and shows the next of five pictures, which change every fourth
// step and go round. A is which of two sets of five.
//
// All of it is here, a stretch at a time between its yields:
//
//   $81:8294  riser_begin  its record drawn in front of every layer, with
//                          nothing able to touch it, and the first picture
//   $81:82CB  riser_shown  the next of the five, or after the fifth the
//                          first step up
//   $81:8300  riser_frame  a step up, or the `RTL` after the sixtieth
//
// Both loops keep what they count on the stack across the yield, and the
// port reads and writes it there.
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
#define RISER_BEGIN_PC 0x818294u
#define RISER_SHOWN_PC 0x8182cbu       // `PLX`
#define RISER_SHOW_YIELD_PC 0x8182c7u  // `JSL thread_yield`, A already 8

#define RISER_DP_SET 0x06     // where this set's pictures begin
#define RISER_DP_RECORD 0x08
#define RISER_PICTURES 0x818314u
#define RISER_SET_BYTES 12    // five pictures and a zero
#define RISER_SETS 2
#define RISER_SET_STARTS 0x818310u  // a word for each set
#define RISER_FIRST_PICTURES 0x818304u  // five and a zero, for either set
#define RISER_FIRST_BYTES 12
#define RISER_PICTURE_BANK 0x008fu
#define RISER_SHOW_FRAMES 8
#define RISER_STEPS 0x003c
#define RISER_STEP_FRAMES 2

enum {
  RS_AGAIN,    // PLX : BRA, either loop's
  RS_STEP,     // $82D4-$82E2
  RS_AROUND,   // $82E3-$82E8
  RS_COUNT,    // $82E9-$82EF
  RS_FOURTH,   // PHA : AND #$0003 : BNE
  RS_NEXT,     // INX : INX
  RS_YIELD,    // PHX : LDA #$0002
  RS_TAKEN,
  RS_BEGIN,    // $8294-$82B5
  RS_PICTURE,  // LDA $818304,X : BEQ
  RS_SHOW,     // $82BC-$82C6
  RS_RISE,     // $82CE-$82D3
  RS_BLOCK_COUNT
};

typedef struct {
  uint16_t blocks[RS_BLOCK_COUNT];
} RiserWork;

// False unless the set and the place on the stack are inside the table.
bool riser_frame_supported(const Wram* w, const PortCpu* c);
void riser_frame(Wram* w, const Rom* rom, PortCpu* c, RiserWork* k);

// A is which set. False for one there is not.
bool riser_begin_supported(uint16_t a);
void riser_begin(Wram* w, const Rom* rom, PortCpu* c, RiserWork* k);

// False unless the place on the stack is in the first pictures, and the
// set is one there is.
bool riser_shown_supported(const Wram* w, const PortCpu* c);
void riser_shown(Wram* w, const Rom* rom, PortCpu* c, RiserWork* k);

#endif
