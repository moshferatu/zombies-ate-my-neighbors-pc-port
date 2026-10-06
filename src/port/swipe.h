// A swipe at what is in front of someone, and the tiles it cuts.
//
// `$81:E8A8` is a thread in levels 13, 37 and 41. It makes a display record
// in front of the record at `$08` on its page, the way `$04` says, looks at
// the tiles there, sleeps two frames and ends. Two calls of it are here.
//
// `$81:E8F1` makes the record: so far across and down from its owner for
// each of eight ways, a picture of two for the way by the frame's parity,
// turned over for three of the ways, and the collide id of the player at
// `$06`. It keeps the owner's tile at `$3C` and `$3E`.
//
// `$81:E979` looks at a list of tiles for the way, each so many across and
// down from the owner's. A tile with bit 14 of its attributes becomes tile
// `$0097`, and one with bit 15 becomes `$01DB`: both by `map_tile_put`. It
// adds how many it cut to the player's word at `$1FC8` and plays sound
// `$0F`.
//
// It looks like a tool cutting what grows, by what the code does. I have
// not seen it on a screen.
//
// Port code: libc only.

#ifndef PORT_SWIPE_H
#define PORT_SWIPE_H

#include <stdbool.h>
#include <stdint.h>

#include "assets/rom.h"
#include "port/apu.h"
#include "port/cpu.h"
#include "port/oam.h"
#include "port/tile_put.h"
#include "port/wram.h"

#define SWIPE_BEGIN_PC 0x81e8f1u
#define SWIPE_BEGIN_RTS_PC 0x81e978u
#define SWIPE_CUT_PC 0x81e979u
#define SWIPE_CUT_RTS_PC 0x81ea03u

#define SWIPE_BANK 0x81
#define SWIPE_FLAG_OPS 0xea24u     // for each way: a mask or bits, and a picture
#define SWIPE_PICTURES 0xea10u
#define SWIPE_REACH 0x81ea48u      // across and down, four bytes a way
#define SWIPE_COLLIDE_IDS 0x81ea0cu
#define SWIPE_TILE_LISTS 0x81ea6cu  // a list's address for each way
#define SWIPE_CUT_TILES 0x81ea04u   // what a cut tile becomes, by the bit...
#define SWIPE_CUT_BITS 0x81ea08u    // ...and what is set on it
#define SWIPE_WAY_END 0x0012        // `$04` is twice a way of one to eight
#define SWIPE_META 0xa26bu
#define SWIPE_META_BANK 0x0090
#define SWIPE_ATTR_FIRST 0x4000u
#define SWIPE_ATTR_SECOND 0x8000u
#define SWIPE_ATTR_MASK 0xc080u
#define SWIPE_SFX 0x000f

#define SWIPE_DP_OWNER_X 0x00
#define SWIPE_DP_OWNER_Y 0x02
#define SWIPE_DP_WAY 0x04
#define SWIPE_DP_PLAYER 0x06
#define SWIPE_DP_OWNER 0x08
#define SWIPE_DP_RECORD 0x0a
#define SWIPE_DP_X 0x0c
#define SWIPE_DP_Y 0x10
#define SWIPE_DP_PARITY 0x12
#define SWIPE_DP_FLAG_OPS 0x14
#define SWIPE_DP_PICTURES 0x16
#define SWIPE_DP_TILE_X 0x3c
#define SWIPE_DP_TILE_Y 0x3e
#define SWIPE_DP_AT_X 0x44
#define SWIPE_DP_AT_Y 0x46
#define SWIPE_DP_CUT 0x48
#define SWIPE_DP_LIST 0x4a
#define SWIPE_DP_LEFT 0x4c
#define W_SWIPE_THREAD 0x0008u
#define W_SWIPE_CUT_COUNTS 0x1fc8u  // a word for each player
#define W_SWIPE_FIRST_PLAYER 0x1e84u

typedef struct {
  bool declined;  // no record free, which is the ROM's
  uint16_t record;
  bool turned_over;
} SwipeBeginWork;

#define SWIPE_CUTS_MAX 16

typedef struct {
  int tiles;     // looked at
  int seconds;   // ...of which this many got as far as the second bit
  int cuts;
  TilePutRegs put[SWIPE_CUTS_MAX];
  bool declined;  // a list longer than the port has room for
  bool other_player;
  bool played;
  // Overflow is a tile lookup's or the sound's, which the port does not
  // follow.
  bool overflow_unknown;
} SwipeCutWork;

void swipe_begin(Wram* w, const Rom* rom, PortCpu* c, SwipeBeginWork* k);
void swipe_cut(Wram* w, const Rom* rom, PortCpu* c, SwipeCutWork* k);

#endif
