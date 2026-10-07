// A fishman on land looking for water to dive back into.
//
// `$81:D948` is the first thing a fishman's pass on land does: see
// `port/fishman.h`, whose state `$81:DA1D` calls it. When the word at `$0C`
// on its page is not the one at `$0E`, it draws a spot within ninety-six
// pixels of itself, up to three times. A spot will do when it is on the
// level and the tile there, and the tile eight pixels below, both have bit 8
// of their attributes, which is the bit the fishman's own test of water
// reads. With one found the two words are made the same and the routine goes
// on to `$81:DB04`, where the dive begins. Otherwise it returns.
//
// The spot is left at `$2A` and `$2C` on the page, which is where a leap is
// to come down.
//
// Port code: libc only.

#ifndef PORT_WANDER_H
#define PORT_WANDER_H

#include <stdbool.h>
#include <stdint.h>

#include "port/cpu.h"
#include "port/terrain.h"
#include "port/wram.h"

#define WANDER_PC 0x81d948u
#define WANDER_RTS_PC 0x81d94eu    // the `RTS`, which is the ROM's
#define WANDER_FOUND_PC 0x81db04u

#define WANDER_TRIES 3
#define WANDER_DRAW_MASK 0x002f
#define WANDER_DRAW_HALF 0x0018
#define WANDER_BELOW 8
#define WANDER_TILE_BIT 0x0100
#define WANDER_DP_RECORD 0x08
#define WANDER_DP_WANTED 0x0c
#define WANDER_DP_HAD 0x0e
#define WANDER_DP_TRIES_LEFT 0x26
#define WANDER_DP_X 0x2a
#define WANDER_DP_Y 0x2c

enum {
  WA_HEAD,     // LDA $0C : CMP $0E : BNE
  WA_TRIES,    // LDA #$0003 : STA $26
  WA_DRAW,     // $D954-$D985
  WA_TILE,     // $D986-$D992
  WA_BELOW,    // $D993-$D9A4
  WA_NEXT,     // DEC $26 : BNE
  WA_GIVE_UP,  // BRA
  WA_FOUND,    // LDA $0C : STA $0E : JMP
  WA_TAKEN,
  WA_BLOCK_COUNT
};

typedef struct {
  uint16_t blocks[WA_BLOCK_COUNT];
  int draws_once, draws_twice;  // by what a draw cost: see `rng_next`
  int bounds_exits[BOUNDS_LAST_COMPARE + 1];
  int tiles;
  // A tile was looked up since the last sum, and what that leaves in
  // overflow the port does not know.
  bool overflow_unknown;
} WanderWork;

void wander_pick(Wram* w, PortCpu* c, WanderWork* k);

#endif
