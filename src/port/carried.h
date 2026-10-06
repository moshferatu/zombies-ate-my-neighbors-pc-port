// A player carried along until something stops them.
//
// `$80:E33E` is a pose of a player's that sleeps inside itself: once it has
// set them going it sleeps a frame at `$80:E3D2`, steps them with
// `$80:F791`, and goes round while that leaves carry clear. A frame of that
// is here, from the sleep's return at `$80:E3D6`.
//
// The step adds the speed at `$3C` and `$3E` to where they are. Ground, a
// thing in the way, the leash to the other player and the level's edge each
// stop it, and so does the count at `$16` running out. Stopped, they are
// turned to face the other way, and the pad's direction is theirs again.
//
// Level 21 has it. I have not seen what carries them.
//
// Port code: libc only.

#ifndef PORT_CARRIED_H
#define PORT_CARRIED_H

#include <stdbool.h>
#include <stdint.h>

#include "port/cpu.h"
#include "port/oam.h"
#include "port/step.h"
#include "port/terrain.h"
#include "port/wram.h"

#define CARRIED_PC 0x80e3d6u
#define CARRIED_SLEEP_PC 0x80e3d2u  // `JSL`, A already 1
#define CARRIED_STOPPED_PC 0x80e3dbu

#define CARRIED_DP_RECORD 0x08
#define CARRIED_DP_PLAYER 0x0e  // 0 or 2
#define CARRIED_DP_FRAMES_LEFT 0x16
#define CARRIED_DP_DIR 0x24
#define CARRIED_DP_FACING 0x26
#define CARRIED_DP_X 0x30
#define CARRIED_DP_Y 0x32
#define CARRIED_DP_TRY_X 0x34
#define CARRIED_DP_TRY_Y 0x36
#define CARRIED_DP_SPEED_X 0x3c
#define CARRIED_DP_SPEED_Y 0x3e
#define W_CARRIED_PAD_DIR 0x0072u  // a word for each player

enum {
  CA_TRY,     // JSR $F791, $F791-$F7A8
  CA_THING,   // $F7A9-$F7B4
  CA_LEASH,   // $F7B5-$F7BE
  CA_EDGE,    // $F7BF-$F7C8
  CA_COUNT,   // DEC $16 : BMI
  CA_PUT,     // $F7CD-$F7DE
  CA_STOP,    // $F7DF-$F7F6
  CA_TAIL,    // BCC
  CA_AGAIN,   // LDA #$0001
  CA_TAKEN,
  CA_BLOCK_COUNT
};

typedef struct {
  uint16_t blocks[CA_BLOCK_COUNT];
  TerrainRegs ground;
  bool asked_thing;  // `actor_obstacle_at_point` was asked...
  ObstacleWork thing;
  bool asked_leash;  // ...and `step_tether_blocked`
  TetherRegs leash;
  bool asked_edge;   // ...and `terrain_out_of_bounds`
  BoundsRegs edge;
  // Overflow is the tests', which the port does not follow.
  bool overflow_unknown;
} CarriedWork;

void carried_frame(Wram* w, PortCpu* c, CarriedWork* k);

#endif
