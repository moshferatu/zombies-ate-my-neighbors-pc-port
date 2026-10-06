// A thing going after the one it has chosen, a step or two a call.
//
// `$81:A741` is called by a thread in levels 13 and 41. A draw says whether
// it steps once or twice, and that is the ROM's: it calls the step and then
// falls into it. The step is `$81:A74D`, and it is here. It faces the record
// at `$42` on its page and tries the pixel that way, across and down each on
// its own: ground that stops an enemy, or a thing already there, and that
// half of the step is left out. Where it is, is kept at `$12` and `$14` and
// copied to its display record.
//
// Facing nowhere, because it is on top of them, it does nothing. With `$44`
// at `$0168` it counts `$10` down and clears `$3E` instead.
//
// I have not seen it on a screen.
//
// Port code: libc only.

#ifndef PORT_PURSUER_H
#define PORT_PURSUER_H

#include <stdbool.h>
#include <stdint.h>

#include "assets/rom.h"
#include "port/cpu.h"
#include "port/oam.h"
#include "port/terrain.h"
#include "port/wram.h"

#define PURSUER_PC 0x81a74du
#define PURSUER_RTS_PC 0x81a7bcu
#define PURSUER_DONE_RTS_PC 0x81a7c1u

#define PURSUER_BANK 0x81
#define PURSUER_STEPS 0x81a7c2u  // across and down, four bytes a way
#define PURSUER_DONE 0x0168

#define PURSUER_DP_RECORD 0x08
#define PURSUER_DP_LEFT 0x10
#define PURSUER_DP_X 0x12
#define PURSUER_DP_Y 0x14
#define PURSUER_DP_TRY_X 0x16
#define PURSUER_DP_TRY_Y 0x18
#define PURSUER_DP_WAY 0x1a
#define PURSUER_DP_FLAG 0x3e
#define PURSUER_DP_WHOM 0x42
#define PURSUER_DP_COUNT 0x44

enum {
  PU_HEAD,    // LDA $44 : CMP #$0168 : BEQ
  PU_FACE,    // $A754-$A764
  PU_AIM,     // $A765-$A785
  PU_BODY,    // LDA $08 : LDX : LDY : JSL actor_at_point : BCS
  PU_TAKE,    // LDA : STA
  PU_GROUND,  // LDX : LDY : JSL terrain_blocked_enemy : BCS
  PU_PUT,     // $A7B0-$A7BB
  PU_DONE,    // DEC $10 : STZ $3E
  PU_TAKEN,
  PU_BLOCK_COUNT
};

typedef struct {
  uint16_t blocks[PU_BLOCK_COUNT];
  bool faced;  // `actor_snap_to` and `actor_bearing` were asked
  ActorSnapRegs snap;
  ActorBearingRegs bearing;
  int grounds;  // `terrain_blocked_enemy`, as often as this
  TerrainRegs ground[2];
  int bodies;   // ...and `actor_at_point`
  AtPointWork body[2];
  // Overflow is something's the port does not follow.
  bool overflow_unknown;
} PursuerWork;

void pursuer_step(Wram* w, const Rom* rom, PortCpu* c, PursuerWork* k);

#endif
