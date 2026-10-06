// The axe an evil doll throws.
//
// An axe is a thread, `$81:B4EA`, which a doll starts with where it stands
// at `$00` and `$02` and which way the axe goes on each axis, 1, 0 or -1, at
// `$04` and `$06` (`port/doll.h`). It flies along that line, four pixels a
// frame right or down and three left or up: it adds its doubled step twice,
// and the second sum takes the first's carry. It turns through four
// pictures, one every sixth frame.
//
// It ends at a tile whose attribute bit 2 is set, off the level, or past the
// leash that keeps two players together, which is measured from the first
// player. It also ends at the second thing it hits: its handler,
// `enemy_b592_collide` in `port/collide.h`, lets it through the first.
//
// The thread is three stretches, each from where control arrives to the call
// or yield it leaves by:
//
//     $81:B4EA  launch   two more of the budget at `$00DE` that the doll's
//                        own 24 came out of, and on to `actor_slot_alloc`
//     $81:B527  dress    the record that gave it: where it is, its picture,
//                        its step, its handler. Then the first yield.
//     $81:B500  frame    from each wake: a step, the two tests, the picture.
//                        Then the yield again, or the budget given back and
//                        on to `actor_slot_free`.
//
// ## The pictures it never shows
//
// `$81:B630` picks a set of four pictures by the way the axe is going, from
// a table of twelve bytes that names eight sets. It builds its index from
// two compares of a step with -1, adding in the carry of each. But the dress
// has doubled the steps by then, and -2 is not -1: neither compare ever sets
// carry. So the index is 0, 2, 4 or 6, and an axe has one of three sets.
// Going left looks as going right does, and up as down. The port asks the
// ROM's question.
//
// Port code: libc only.

#ifndef PORT_AXE_H
#define PORT_AXE_H

#include <stdbool.h>
#include <stdint.h>

#include "assets/rom.h"
#include "port/cpu.h"
#include "port/step.h"
#include "port/terrain.h"
#include "port/wram.h"

#define AXE_BANK 0x81u

#define AXE_LAUNCH_PC 0x81b4eau
#define AXE_DRESS_PC 0x81b527u   // after `JSL actor_slot_alloc`
#define AXE_FRAME_PC 0x81b500u   // after the yield
// Where they leave.
#define AXE_ALLOC_CALL_PC 0x81b523u
#define AXE_YIELD_PC 0x81b4fcu   // `JSL thread_yield`, A already 1
#define AXE_FREE_JML_PC 0x81b51fu
#define AXE_YIELD_TICKS 1

// Fields on the axe's page. The first four are what the doll passed.
#define AXE_DP_FROM_X 0x00
#define AXE_DP_FROM_Y 0x02
#define AXE_DP_WAY_X 0x04
#define AXE_DP_WAY_Y 0x06
#define AXE_DP_RECORD 0x08
#define AXE_DP_LEAVE 0x0a        // nonzero ends the thread
#define AXE_DP_HITS_LEFT 0x0c    // the handler's: one hit to spare
#define AXE_DP_STATE 0x0e        // the one state body, `$81:B5B0`
#define AXE_DP_TURN_WAIT 0x10    // frames until the picture turns
#define AXE_DP_TURN_EVERY 0x12   // ...and what that starts from
#define AXE_DP_TURN 0x14         // which of the four pictures, 0 to 3
#define AXE_DP_SET 0x16          // the set of four, as an offset in frames
#define AXE_DP_X 0x18
#define AXE_DP_Y 0x1c
#define AXE_DP_HIT_ID 0x1e       // the handler's, cleared every frame
#define AXE_DP_STEP_X 0x20       // the way it goes, doubled...
#define AXE_DP_STEP_Y 0x22       // ...and added twice a frame
#define AXE_DP_FRAMES 0x24       // the animation table

#define AXE_STATE_FLY 0xb5b0u
// How much of the budget at `$00DE` an axe is.
#define AXE_BUDGET 2
#define AXE_HITS 1
#define AXE_TURN_EVERY 5
#define AXE_TURNS 4
#define AXE_COLLIDE_ID 0x0003
#define AXE_HANDLER 0xb592u
// Its first picture, in bank `$90`, and the table of the rest: four bytes a
// frame, a mask for the record's flags and a picture number, as the doll's
// own tables are.
#define AXE_PICTURE 0xccecu
#define AXE_PICTURE_BANK 0x0090
#define AXE_FRAMES 0xaddeu
#define AXE_PICTURES 0xacd8u  // the doll's: picture numbers index it
// A byte for each way it can go: which set of four.
#define AXE_SETS 0xb658u

enum {
  AX_LAUNCH,       // $B4EA-$B4F6
  AX_DRESS,        // $B527-$B583
  AX_SET_HEAD,     // $B630-$B636
  AX_SET_X,        // $B637-$B640
  AX_SET_MID,      // $B641-$B644
  AX_SET_Y,        // $B645-$B64C
  AX_SET_TAIL,     // $B64D-$B657
  AX_HANDLER,      // $B584-$B58D, and `$80:8475` itself
  AX_DRESS_TAIL,   // $B58E-$B591, and `$81:B5AA` itself
  AX_AGAIN,        // $B4F7-$B4FB
  AX_DISPATCH,     // $B500-$B507
  AX_FLY_HEAD,     // $B5B0-$B5B3
  AX_TURN,         // $B5B4-$B5BC
  AX_TURN_WRAP,    // $B5BD-$B5BE
  AX_MOVE,         // $B5BF-$B5DA
  AX_LEASH,        // $B5DB-$B5E4
  AX_INC,          // $B5E5-$B5E6
  AX_FLY_TAIL,     // $B5E7-$B5E9
  AX_LOOP,         // $B508-$B50E
  AX_SHOW_HEAD,    // $B5EA-$B5ED
  AX_SHOW_RESET,   // $B5EE-$B5F1
  AX_SHOW_MID,     // $B5F2-$B60E
  AX_SHOW_ORA,     // $B60F-$B616
  AX_SHOW_AND,     // $B617-$B61C
  AX_SHOW_TAIL,    // $B61D-$B62F
  AX_LEAVE,        // $B50F-$B51E
  AX_TAKEN,        // a branch taken
  AX_BLOCK_COUNT
};

// What a stretch did, for the harness to price.
typedef struct {
  uint16_t blocks[AX_BLOCK_COUNT];
  bool tested;      // the tile test ran...
  TerrainRegs ground;
  bool leashed;     // ...and the leash's after it
  TetherRegs leash;
} AxeWork;

void axe_launch(Wram* w, PortCpu* c, AxeWork* k);
void axe_dress(Wram* w, const Rom* rom, PortCpu* c, AxeWork* k);
void axe_frame(Wram* w, const Rom* rom, PortCpu* c, AxeWork* k);

#endif
