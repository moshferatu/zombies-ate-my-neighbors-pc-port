// A thing that steers after whoever is nearest.
//
// `$82:F03E` is a thread in level 37. After each sleep of a frame it runs
// from `$82:F054`: the picture, then a step, and the loop goes round while
// `$14` on its page is zero.
//
// The step is `$82:F091`. It keeps a speed across and a speed down, at `$0E`
// and `$10`. Each frame both take one more towards whoever is nearest, and a
// speed that would come to six or over stays as it was. Then the place at
// `$0A` and `$0C` takes the speeds. Ground that stops it ends it, and so
// does the count at `$1A` running out: both count `$14` down.
//
// The picture is `$82:F11B`: every third frame the record gets the other of
// two.
//
// I have not seen it on a screen.
//
// Port code: libc only.

#ifndef PORT_TRACKER_H
#define PORT_TRACKER_H

#include <stdbool.h>
#include <stdint.h>

#include "assets/rom.h"
#include "port/cpu.h"
#include "port/oam.h"
#include "port/terrain.h"
#include "port/wram.h"

#define TRACKER_PC 0x82f054u
#define TRACKER_SLEEP_PC 0x82f050u  // `JSL`, A already 1
#define TRACKER_ENDED_PC 0x82f05eu

#define TRACKER_BANK 0x82
#define TRACKER_STEPS 0x82f0f7u     // across and down, four bytes a way
#define TRACKER_PICTURES 0x82f137u  // two
#define TRACKER_PICTURE_FRAMES 2    // counted down past zero
#define TRACKER_SPEED_LIMIT 6

#define TRACKER_DP_RECORD 0x08
#define TRACKER_DP_X 0x0a
#define TRACKER_DP_Y 0x0c
#define TRACKER_DP_SPEED_X 0x0e
#define TRACKER_DP_SPEED_Y 0x10
#define TRACKER_DP_ENDED 0x14  // not zero ends the loop
#define TRACKER_DP_PICTURE 0x16
#define TRACKER_DP_PICTURE_LEFT 0x18
#define TRACKER_DP_FRAMES_LEFT 0x1a

enum {
  TK_FLAP,     // JSR $F11B, DEC $18 : BPL
  TK_PICTURE,  // $F11F-$F135
  TK_LOOK,     // RTS, JSR $F091, $F091-$F0A2
  TK_STEER,    // $F0A3-$F0AB, and $F0B7-$F0BF the same
  TK_NEGATE,   // EOR #$FFFF : INC
  TK_LIMIT,    // CMP #$0006 : BCS
  TK_KEEP,     // STY $0E, or STY $10
  TK_MOVE,     // $F0CB-$F0E2
  TK_PUT,      // $F0E3-$F0F2
  TK_RTS,
  TK_STOP,     // DEC $14 : RTS
  TK_TAIL,     // LDA $14 : BEQ
  TK_AGAIN,    // LDA #$0001
  TK_TAKEN,
  TK_BLOCK_COUNT
};

typedef struct {
  uint16_t blocks[TK_BLOCK_COUNT];
  ActorNearestWork nearest;
  ActorBearingRegs bearing;
  TerrainRegs ground;
} TrackerWork;

void tracker_frame(Wram* w, const Rom* rom, PortCpu* c, TrackerWork* k);

#endif
