// A thing kept beside another.
//
// `$82:F40B` is called every frame by a thread in level 21. It puts the
// thread's display record at a place beside the point at `$1E62`: so far
// across and so far down, from a table, by the word at `$0C` on its page.
// A negative word at `$0A` puts it the same distance the other side. It
// keeps the place, and the tile that is in, on its page.
//
// A table entry of no distance across is the end: it counts `$14` down.
// Every sixteenth frame of the game it goes on to look at the tile, and
// that is the ROM's, from the sleep at `$82:F446`.
//
// I have not seen it on a screen.
//
// Port code: libc only.

#ifndef PORT_FOLLOWER_H
#define PORT_FOLLOWER_H

#include <stdbool.h>
#include <stdint.h>

#include "assets/rom.h"
#include "port/cpu.h"
#include "port/wram.h"

#define FOLLOWER_PC 0x82f40bu
#define FOLLOWER_RTS_PC 0x82f476u
#define FOLLOWER_SLEEP_PC 0x82f446u  // `JSL`, A already 1
#define FOLLOWER_ENDED_RTS_PC 0x82f479u

#define FOLLOWER_BANK 0x82
#define FOLLOWER_PLACES 0x82f47au  // across and down, four bytes a place
#define FOLLOWER_PLACES_END 0x0020
#define FOLLOWER_DP_RECORD 0x08
#define FOLLOWER_DP_SIDE 0x0a
#define FOLLOWER_DP_PLACE 0x0c
#define FOLLOWER_DP_LEFT 0x14
#define FOLLOWER_DP_TILE_X 0x1a
#define FOLLOWER_DP_TILE_Y 0x1c
#define FOLLOWER_DP_X 0x1e
#define FOLLOWER_DP_Y 0x20
#define W_FOLLOWED_X 0x1e62u
#define W_FOLLOWED_Y 0x1e64u
#define FOLLOWER_LOOK_FRAMES 0x000f  // a look when the frame count has none

enum {
  FW_HEAD,   // LDX $0C : LDA $F47A,X : BEQ
  FW_SIDE,   // LDY $0A : BPL
  FW_OTHER,  // EOR #$FFFF : INC
  FW_PLACE,  // $F41A-$F442
  FW_LOOK,   // LDA #$0001
  FW_ENDED,  // DEC $14
  FW_TAKEN,
  FW_BLOCK_COUNT
};

typedef struct {
  uint16_t blocks[FW_BLOCK_COUNT];
} FollowerWork;

void follower_place(Wram* w, const Rom* rom, PortCpu* c, FollowerWork* k);

#endif
