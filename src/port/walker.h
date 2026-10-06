// A thing that walks until something is in its way, and then turns.
//
// `$81:D704` is a thread in levels 5 and 17. It sleeps two frames at
// `$81:D71F`, and from the sleep's return it runs the state `$1A` names,
// shows itself, and goes round while `$0A` on its page is zero. A frame of
// that is here, for the three states it has:
//
//   $81:D5EF  It walks the way it faces, a pixel a frame. Ground that stops
//             it, or a thing where it would be, turns it a quarter round
//             and has it go along what stopped it.
//   $81:D61D  The same walk, but first it tries the quarter turn back: when
//             that way is clear it faces it.
//   $81:D64E  It goes at whoever is nearest, one step or two by a draw,
//             across and down each on its own. From `$70` away or more it
//             walks again.
//
// The two walks look first (`$81:D5A0`). Anyone nearer than `$60` has it go
// at them from the next frame. With nobody nearer than `$D0`, and no player
// that near either, it counts `$0A` down and the loop ends.
//
// The way it faces is at `$16`, twice the usual one to eight. The table at
// `$81:D4F3` has a step across and a step down for each.
//
// `$81:D76E` shows it: every fifth frame the next of four pictures for the
// way it faces, turned over for the last three ways, and its place from
// `$0E` and `$10` every frame.
//
// I have not seen it on a screen.
//
// Port code: libc only.

#ifndef PORT_WALKER_H
#define PORT_WALKER_H

#include <stdbool.h>
#include <stdint.h>

#include "assets/rom.h"
#include "port/cpu.h"
#include "port/oam.h"
#include "port/terrain.h"
#include "port/wram.h"

#define WALKER_PC 0x81d723u
#define WALKER_SLEEP_PC 0x81d71fu  // `JSL`, A already 2
#define WALKER_ENDED_PC 0x81d732u
#define WALKER_STATE_RETURN 0xd72au  // what the `PEA` pushes

#define WALKER_BANK 0x81
#define WALKER_STEPS 0x81d4f3u     // across and down, at twice `$16`
#define WALKER_PICTURES 0x81d7aeu  // four for each way
#define WALKER_WAY_END 0x0020      // what `$16` is kept under here
#define WALKER_FLIPPED_FROM 0x0030
#define WALKER_PICTURE_FRAMES 4    // counted down past zero
#define WALKER_NEAR 0x0060
#define WALKER_FAR 0x00d0
#define WALKER_LOST 0x0070
#define WALKER_SLEEP_FRAMES 2

#define WALKER_STATE_WALK 0xd5efu
#define WALKER_STATE_ALONG 0xd61du
#define WALKER_STATE_AT 0xd64eu

#define WALKER_DP_RECORD 0x08
#define WALKER_DP_ENDED 0x0a
#define WALKER_DP_X 0x0e
#define WALKER_DP_Y 0x10
#define WALKER_DP_TRY_X 0x12
#define WALKER_DP_TRY_Y 0x14
#define WALKER_DP_WAY 0x16
#define WALKER_DP_SIDE 0x18
#define WALKER_DP_STATE 0x1a
#define WALKER_DP_PICTURE_LEFT 0x1c
#define WALKER_DP_PICTURE 0x1e
#define WALKER_DP_SCORED 0x20
#define WALKER_DP_WHOM 0x22

enum {
  WK_HEAD,         // PEA : LDA $1A : DEC : PHA : RTS
  WK_JSR,
  WK_JMP,
  WK_RTS,
  WK_LOOK,         // $D5A0-$D5AC
  WK_LOOK_FAR,     // CMP #$00D0 : BCC
  WK_LOOK_PLAYER,  // $D5B2-$D5BF
  WK_LOOK_NONE,    // DEC $0A
  WK_STATE,        // LDA #... : STA $1A : RTS
  WK_AIM,          // $D5F2-$D605, and $D623-$D636 the same
  WK_GROUND,       // LDX : LDY : JSL terrain_blocked_enemy : BCS
  WK_BODY,         // LDA $08 : LDX : LDY : JSL actor_at_point : BCS
  WK_BCS,
  WK_STEP,         // $D60B-$D613, and $D63C-$D644 the same
  WK_TURN,         // $D5C7-$D5D8
  WK_SIDE,         // $D52F-$D54F
  WK_TAKE,         // LDA : STA
  WK_AT,           // $D64E-$D65C
  WK_AT_DRAW,      // JSL rng : AND #$0002 : BNE
  WK_AT_GO,        // $D66C-$D688
  WK_SHOW,         // DEC $1C : BPL
  WK_PICTURE,      // $D772-$D78D
  WK_FACE_RIGHT,   // AND #$FFFD : BRA
  WK_FACE_LEFT,    // ORA #$0002
  WK_PICTURE_END,  // $D796-$D7A0
  WK_PLACE,        // $D7A1-$D7AD
  WK_TAIL,         // LDA $0A : BEQ
  WK_AGAIN,        // STZ $20 : LDA #$0002
  WK_TAKEN,
  WK_BLOCK_COUNT
};

#define WALKER_GROUNDS 4
#define WALKER_BODIES 4

typedef struct {
  uint16_t blocks[WK_BLOCK_COUNT];
  bool looked;  // `actor_nearest` asked
  ActorNearestWork nearest;
  bool asked_players;  // ...and `player_bearing`
  PlayerPickRegs players;
  int grounds;  // `terrain_blocked_enemy`, as often as this
  TerrainRegs ground[WALKER_GROUNDS];
  int bodies;   // ...and `actor_at_point`
  AtPointWork body[WALKER_BODIES];
  int bearings;  // ...and `actor_bearing`
  ActorBearingRegs bearing[2];
  bool drew;
  bool drew_overflow;
  // Overflow is something's the port does not follow.
  bool overflow_unknown;
} WalkerWork;

// A frame of the thread, from the sleep's return to the next sleep or to the
// end of the loop. False when `$1A` names no state of the three, having
// changed nothing.
bool walker_frame(Wram* w, const Rom* rom, PortCpu* c, WalkerWork* k);

#endif
