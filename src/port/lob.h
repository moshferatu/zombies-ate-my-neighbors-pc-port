// The thing thrown in an arc.
//
// `$81:F976` is a thread. It takes a display record, puts it thirty pixels
// up at the place it was given, and then every frame moves it two pixels
// one of eight ways, adds its rise to its height, and every ninth frame
// shows the next of four pictures. The rise starts at 3 and goes down one
// every fourth frame, so the thing goes up, over and down. When its height
// goes below zero it has landed, and the ROM does the rest.
//
// This is a frame of that: from the return of the yield at `$81:F98A` to
// the next yield, or to `$81:F99C` when it has landed.
//
// Port code: libc only.

#ifndef PORT_LOB_H
#define PORT_LOB_H

#include <stdbool.h>
#include <stdint.h>

#include "assets/rom.h"
#include "port/cpu.h"
#include "port/wram.h"

#define LOB_PC 0x81f98au
#define LOB_YIELD_PC 0x81f986u   // `JSL thread_yield`, A already 1
#define LOB_LANDED_PC 0x81f99cu

#define LOB_BANK 0x81
#define LOB_STEPS 0xfad1u          // a step across and a step down, a way
#define LOB_WAY_MAX 16             // the way is kept doubled
#define LOB_PICTURES_LOW 0xfaa1u   // four pictures
#define LOB_PICTURES_HIGH 0xfaa9u  // ...and four for `$44` up and over
#define LOB_HIGH 0x0044
#define LOB_PICTURE_FRAMES 8
#define LOB_PICTURE_BANK 0x008f
#define LOB_DP_WAY 0x04
#define LOB_DP_RECORD 0x0a
#define LOB_DP_PICTURE_FRAMES 0x18
#define LOB_DP_PICTURE 0x1a
#define LOB_DP_RISE 0x1e
#define LOB_DP_FRAMES 0x20
#define LOB_DP_SLOW_MASK 0x22  // the rise drops when the count has none of it
#define LOB_DP_PICTURES 0x3c

enum {
  LB_ARC,        // JSR, $FEEF-$FF01, RTS
  LB_ARC_SLOW,   // DEC $1E
  LB_MOVE,       // JSR, $FA55-$FA6F
  LB_PIC_COUNT,  // JSR, DEC $18 : BPL, RTS
  LB_PIC,        // $FA26-$FA37, $FA3B-$FA53
  LB_PIC_HIGH,   // LDA #$FAA9
  LB_TEST,       // $F993-$F999
  LB_AGAIN,      // BRA, LDA #$0001
  LB_TAKEN,
  LB_BLOCK_COUNT
};

typedef struct {
  uint16_t blocks[LB_BLOCK_COUNT];
} LobWork;

// False for a way the table does not have.
bool lob_frame_supported(const Wram* w, uint16_t page);
void lob_frame(Wram* w, const Rom* rom, PortCpu* c, LobWork* k);

#endif
