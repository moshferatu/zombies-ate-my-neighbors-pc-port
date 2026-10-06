// A thing sent off one way, in a straight line.
//
// `$81:F808` is a thread. It is started with a place and a way to go, makes
// sound `$15`, and from then on moves its thing a step every frame. Every
// ninth frame it shows the other of its two pictures. It ends after sixty
// frames, or when the ground is in the way, or when it would go further from
// a player than a thing may, or when something sets the word at `$1C` on its
// page.
//
// This is a frame of that, from the return of its sleep at `$81:F823` to
// the next sleep, or through its end to the `JML` at `$81:F899` that frees
// its record.
//
// Level 21 is the only one of the twelve movies that has it.
//
// Port code: libc only.

#ifndef PORT_BOLT_H
#define PORT_BOLT_H

#include <stdbool.h>
#include <stdint.h>

#include "assets/rom.h"
#include "port/cpu.h"
#include "port/step.h"     // TetherRegs
#include "port/terrain.h"  // TerrainRegs
#include "port/wram.h"

#define BOLT_PC 0x81f823u
#define BOLT_SLEEP_PC 0x81f81fu  // `JSL`, A already 1
#define BOLT_FREE_PC 0x81f899u   // `JML`, A already the record

#define BOLT_BANK 0x81
#define BOLT_STEPS 0x81f91cu     // across and down, at twice the way
#define BOLT_PICTURES 0x81f940u  // two for each way
#define BOLT_WAY_MAX 0x0010
#define BOLT_PICTURE_FRAMES 0x0008
// What a bolt adds to `W_SPAWN_LOAD`, and takes off as it ends.
#define BOLT_BUDGET 2

#define BOLT_DP_WAY 0x04
#define BOLT_DP_RECORD 0x0a
#define BOLT_DP_PICTURE_LEFT 0x18  // frames until the other picture
#define BOLT_DP_WHICH 0x1a
#define BOLT_DP_TOLD 0x1c
#define BOLT_DP_FRAMES_LEFT 0x26

enum {
  BO_HEAD,     // LDA $1C : BNE
  BO_COUNT,    // DEC $26 : BMI
  BO_LEASH,    // $F82B-$F83A
  BO_GROUND,   // $F83B-$F84A
  BO_MOVE,     // $F84B-$F868
  BO_PICTURE,  // $F869-$F882
  BO_AGAIN,    // LDA #$0001
  BO_END,      // $F883-$F898
  BO_TAKEN,
  BO_BLOCK_COUNT
};

typedef struct {
  uint16_t blocks[BO_BLOCK_COUNT];
  bool leashed;  // `step_tether_blocked` was asked...
  TetherRegs leash;
  bool tested;   // ...and `terrain_point_bit2`
  TerrainRegs ground;
} BoltWork;

void bolt_frame(Wram* w, const Rom* rom, PortCpu* c, BoltWork* k);

#endif
