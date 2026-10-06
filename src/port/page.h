// A player's page, begun.
//
// `$80:D13A` is called once by a player's thread, before its loop. It sets
// the page up: who the player is, their two records, where they stand, the
// way they face. This is its first stretch, as far as the `JSL` at
// `$80:D16A` that asks for the first record:
//
//   * a player's weight, `$1C`, onto the census at `$7E:00DE`;
//   * the page cleared from `$0A` up, which is nearly all the stretch costs;
//   * `$0C`, the side that has the slot at `$04`, and that slot's half of
//     the status panel turned on;
//   * `$0E`, the word at `$06` doubled, and Y the same.
//
// The rest is a dozen calls with a few instructions between, and is the
// ROM's.
//
// Port code: libc only.

#ifndef PORT_PAGE_H
#define PORT_PAGE_H

#include <stdint.h>

#include "port/cpu.h"
#include "port/wram.h"

#define PAGE_BEGIN_PC 0x80d13au
#define PAGE_BEGIN_ALLOC_PC 0x80d16au  // `JSL $80BE0C`

#define PAGE_PLAYER_WEIGHT 0x001c
#define PAGE_CLEAR_FROM 0x0a
#define PAGE_CLEAR_TO 0x7e  // the last word cleared
#define PAGE_DP_SLOT 0x04
#define PAGE_DP_PLAYER_IN 0x06
#define PAGE_DP_SIDE 0x0c
#define PAGE_DP_PLAYER 0x0e
#define PAGE_SLOT_COUNT 2

enum {
  PG_HEAD,   // $D13A-$D146
  PG_CLEAR,  // STZ $00,X : DEX : DEX : CPX #$0009 : BCS, once a word
  PG_TAIL,   // $D150-$D169
  PG_TAKEN,
  PG_BLOCK_COUNT
};

typedef struct {
  uint16_t blocks[PG_BLOCK_COUNT];
} PageWork;

void player_page_begin(Wram* w, PortCpu* c, PageWork* k);

#endif
