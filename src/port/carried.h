// A player carried along until something stops them.
//
// `$80:E33E` is a pose of a player's that sleeps inside itself: once it has
// set them going it sleeps a frame at `$80:E3D2`, steps them with
// `$80:F791`, and goes round while that leaves carry clear. A frame of that
// is here, from the sleep's return at `$80:E3D6`, and so is the start.
//
// ## The start
//
// The player's other record is hidden, and they are sent away from the
// record at `$58`: the way opposite the one `actor_bearing_point` gives.
// That way is tried five steps out, by the steps of `$80:F76D`, and a step
// into the ground, past the leash or off the level rules it out. Then the
// next way round is tried, eight in all.
//
// * **The ways go round from one to eight, and not to zero.** The next way
//   is the last masked to three bits and one added. Eight is a ninth way,
//   whose step and speed are the bytes after the tables' ends.
// * **A player boxed in on all eight goes the last way tried anyway.**
//
// The way found sets their facing, their speed, a nudge of where they are
// and a picture, and the start sleeps where the frame does.
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

#include "assets/rom.h"
#include "port/cpu.h"
#include "port/oam.h"
#include "port/step.h"
#include "port/terrain.h"
#include "port/wram.h"

#define CARRIED_BEGIN_PC 0x80e33eu
#define CARRIED_PC 0x80e3d6u
#define CARRIED_SLEEP_PC 0x80e3d2u  // `JSL`, A already 1
#define CARRIED_STOPPED_PC 0x80e3dbu

#define CARRIED_BANK 0x80  // the data bank the picture is read through
#define CARRIED_DP_RECORD 0x08
#define CARRIED_DP_OTHER_RECORD 0x0a
#define CARRIED_DP_PICTURES 0x10
#define CARRIED_DP_TRIES 0x18
#define CARRIED_DP_STEP_AT 0x2e   // the probe's step, as an index
#define CARRIED_DP_STEPS_LEFT 0x44
#define CARRIED_DP_CARRIER 0x58   // the record they are sent away from
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

// By the way times four: a step out for the probe, x then y...
#define CARRIED_PROBE_STEPS 0x80f76du
#define CARRIED_PROBE_LENGTH 5
#define CARRIED_WAYS 8
// ...and the speed, the nudge, and the picture's number with the mask that
// shows or hides the record.
#define CARRIED_SPEEDS 0x80e3e4u
#define CARRIED_NUDGES 0x80e408u
#define CARRIED_PICTURES 0x80e42cu

enum {
  CB_HEAD,     // $E33E-$E35F, to the `JSL actor_bearing_point`
  CB_TURN,     // $E363-$E374
  CB_CALL,     // JSR $F71E, for each way tried
  CB_PROBE,    // $F71E-$F72F
  CB_STEP,     // $F731-$F749, to the `JSL terrain_blocked`
  CB_LEASH,    // $F74D-$F753
  CB_EDGE,     // $F757-$F75D
  CB_COUNT,    // $F761-$F765
  CB_CLEAR,    // PLA : CLC : RTS
  CB_BLOCKED,  // PLA : SEC : RTS
  CB_TRIED,    // BCC, after each probe
  CB_NEXT,     // $E37A-$E380
  CB_WAY,      // $E382-$E3C2
  CB_SHOW,     // ORA : BRA
  CB_HIDE,     // AND
  CB_TAIL,     // STA : LDA #$0001
  CB_TAKEN,
  CB_BLOCK_COUNT
};

#define CARRIED_MAX_STEPS (CARRIED_WAYS * CARRIED_PROBE_LENGTH)

typedef struct {
  uint16_t blocks[CB_BLOCK_COUNT];
  bool same_row, same_column;  // of the carrier, for `actor_bearing_point`
  int steps;                   // how many steps the probes took...
  TerrainRegs ground[CARRIED_MAX_STEPS];  // ...and what each was told
  bool asked_leash[CARRIED_MAX_STEPS];
  TetherRegs leash[CARRIED_MAX_STEPS];
  bool asked_edge[CARRIED_MAX_STEPS];
  BoundsRegs edge[CARRIED_MAX_STEPS];
} CarriedBeginWork;

void carried_begin(Wram* w, const Rom* rom, PortCpu* c, CarriedBeginWork* k);

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
