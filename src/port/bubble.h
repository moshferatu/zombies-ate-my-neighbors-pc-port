// The bubble gun's bubble.
//
// A bubble is a thread, `$81:F380`. It is the fourth weapon in the table at
// `$80:ED8C`, and a martian fires the same thread from `$81:99B8`, which is
// where every one in the movies comes from. What it hits is told by the id on
// its record: `$5E` or `$805E` for a player's, the id `enemy_bubble_react`
// answers to (`port/collide.h`), and `$0B` for a martian's.
//
// It leaves the muzzle as the squirt gun's water does, four pixels a frame
// along one of eight ways, 24 pixels off the ground. For four frames it has
// the water's picture. Then it is a bubble for thirty more, and can be hit:
// its handler, `actor_f534_collide`, sets `$3E`, which ends it without a
// sound. A tile whose attribute bit 2 is set, or the edge of the level, or its
// thirty frames running out, bursts it: three pictures, four ticks each.
//
// The stretches, each from where control arrives to the call or yield it
// leaves by:
//
//     $81:F380  launch   is the firer's own tile one that stops it? Then
//                        nothing is fired. If not, two of the budget at
//                        `$00DE`, and on to ask for a record.
//     $81:F442  dress    the record: the muzzle for the way it faces, the
//                        picture, the id of the side that fired. Then on to
//                        the sound.
//     $81:F39F  first    four frames to come as water, and the first yield
//     $81:F3B5  rising   one of those frames. After the fourth it is a
//                        bubble, with a handler.
//     $81:F3F6  flying   one of the thirty
//     $81:F42C  gone     the budget back, and on to free the record
//
// ## The burst writes the picture over its bank
//
// `$81:F417` means to put the record back to the water's picture before the
// burst's list plays: `LDA #$0090 : STA $000A,Y : LDA #$A26B : STA $000A,Y`.
// Both stores are to the bank's word, so the bank is left `$A26B` and the
// picture is whatever the list shows next. The port writes what the ROM
// writes.
//
// The page as its firer filled it: where it is fired from at `$00` and
// `$02`, the way it faces at `$04`, doubled as the game has it, 2 to 16, and
// the side at `$06`: 0 or 2 for a player, and negative for anyone else.
//
// Port code: libc only.

#ifndef PORT_BUBBLE_H
#define PORT_BUBBLE_H

#include <stdbool.h>
#include <stdint.h>

#include "assets/rom.h"
#include "port/cpu.h"
#include "port/terrain.h"
#include "port/wram.h"

#define BUBBLE_BANK 0x81u

#define BUBBLE_LAUNCH_PC 0x81f380u
#define BUBBLE_DRESS_PC 0x81f442u    // after `JSL actor_slot_alloc`
#define BUBBLE_FIRST_PC 0x81f39fu    // after `JSL apu_play_sfx`
#define BUBBLE_RISING_PC 0x81f3b5u   // after a yield of the first four frames
#define BUBBLE_FLYING_PC 0x81f3f6u   // after a yield of the thirty
#define BUBBLE_GONE_PC 0x81f42cu     // after the burst's pictures
// Where they leave.
#define BUBBLE_UNFIRED_RTL_PC 0x81f38au
#define BUBBLE_ALLOC_CALL_PC 0x81f43eu
#define BUBBLE_SFX_CALL_PC 0x81f39bu
#define BUBBLE_RISING_YIELD_PC 0x81f3b1u
#define BUBBLE_FLYING_YIELD_PC 0x81f3f2u
#define BUBBLE_BURST_CALL_PC 0x81f428u  // `JSL $81:832C`, the list in A
#define BUBBLE_FREE_JML_PC 0x81f43au
#define BUBBLE_YIELD_TICKS 1

// Fields on its page.
#define BUBBLE_DP_FROM_X 0x00
#define BUBBLE_DP_FROM_Y 0x02
#define BUBBLE_DP_FACING 0x04
#define BUBBLE_DP_SIDE 0x06
#define BUBBLE_DP_RECORD_08 0x08  // the record, kept twice
#define BUBBLE_DP_RECORD 0x0a
#define BUBBLE_DP_X 0x0c
#define BUBBLE_DP_HEIGHT 0x0e
#define BUBBLE_DP_Y 0x10
#define BUBBLE_DP_FRAMES_LEFT 0x3c
#define BUBBLE_DP_HIT 0x3e        // the handler's

// How much of the budget at `$00DE` a bubble is.
#define BUBBLE_BUDGET 2
#define BUBBLE_SFX 0x0011
#define BUBBLE_HEIGHT 0x0018
#define BUBBLE_RISING_FRAMES 4
#define BUBBLE_FLYING_FRAMES 30
#define BUBBLE_FACING_MAX 16
#define BUBBLE_SIDE_OTHER 4  // what a negative side becomes
#define BUBBLE_HANDLER 0xf534u
// Its pictures, in bank `$90`: the water's, the bubble's, and the list the
// burst plays.
#define BUBBLE_WATER_PICTURE 0xa26bu
#define BUBBLE_PICTURE 0xc419u
#define BUBBLE_PICTURE_BANK 0x0090
#define BUBBLE_BURST_PICTURES 0xf4b2u
// The tables: a side's table of where the muzzle is for each facing, a side's
// collision id, and a facing's step, across and down.
#define BUBBLE_MUZZLES 0xf4c2u
#define BUBBLE_COLLIDE_IDS 0xf4c6u
#define BUBBLE_STEPS 0xf514u

enum {
  BUB_TEST,         // LDX : LDY : JSL : a branch not taken
  BUB_COUNT_UP,     // $F38B-$F397
  BUB_DRESS_HEAD,   // $F442-$F44A
  BUB_DRESS_OTHER,  // $F44B-$F452
  BUB_DRESS,        // $F453-$F49A
  BUB_SFX,          // $F398-$F39A
  BUB_FIRST,        // $F39F-$F3A3
  BUB_TICKS,        // LDA #$0001
  BUB_BURST,        // $F417-$F427
  BUB_MOVE,         // the `JSR`, and $F49B-$F4B1
  BUB_PLACE,        // $F3C2-$F3D1, and $F407-$F416 the same
  BUB_FLOAT,        // $F3D2-$F3EE, and `$80:8475` itself
  BUB_HIT_TEST,     // $F3F6-$F3F9
  BUB_END,          // $F42C-$F439
  BUB_TAKEN,        // a branch taken
  BUB_BLOCK_COUNT
};

#define BUBBLE_MAX_TESTS 2

// What a stretch did, for the harness to price. `overflow_known` is false
// when the last thing to write the overflow flag was the tile test.
typedef struct {
  uint16_t blocks[BUB_BLOCK_COUNT];
  int tests;
  TerrainRegs ground[BUBBLE_MAX_TESTS];
  bool overflow_known;
} BubbleWork;

void bubble_launch(Wram* w, PortCpu* c, BubbleWork* k);
void bubble_dress(Wram* w, const Rom* rom, PortCpu* c, BubbleWork* k);
void bubble_first(Wram* w, PortCpu* c, BubbleWork* k);
void bubble_rising(Wram* w, const Rom* rom, PortCpu* c, BubbleWork* k);
void bubble_flying(Wram* w, const Rom* rom, PortCpu* c, BubbleWork* k);
void bubble_gone(Wram* w, PortCpu* c, BubbleWork* k);

#endif
