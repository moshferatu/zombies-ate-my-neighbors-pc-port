// A player knocked back fifteen frames, against the way they face.
//
// `$80:DE96` is a pose of a player's that sleeps inside itself. It waits
// five frames, shows the picture for the way they face, and then for
// fourteen frames sleeps one and steps them by what a table has for that
// way: five pixels straight, three and three on a diagonal. A frame of that
// is here, from the sleep's return at `$80:DEC5`.
//
// **The step is backward.** The table at `$80:DF6C` has five pixels down
// for a player facing up, and five right for one facing left. This file
// said forward until the table was read against one whose ways are known.
// Firing weapon 5 is the one thing that starts it (`$80:ED82`), so it is
// that weapon's kick. `port/pose.h` has the pose's first two stretches.
//
// A step is taken if the ground is clear, the leash to the other player
// allows it, it is on the level, and no thing is in the way. Anything else
// ends the pose at `$80:DF45`, and so does the count at `$68` running out.
//
// **Water is not an end.** Where what the test hands back, doubled and
// masked with `$8B38`, is `$0100`, the kick looks further on: the way the
// player faces, a row of `$80:DF48` from the step's point, read through the
// data bank. That point goes to `$60` and `$62`, and if its tile has bit 8
// (`terrain_point_bit8`) the player leaps into the water there: the pose
// ends at `$80:DF05`, the `JMP` to the leap (`pose_leap`). Without bit 8 it
// ends as the others do.
//
// It sits among the trampoline's poses in the ROM, and no corpus movie's own
// player does it: the demos have it. I have not seen it on a screen.
//
// Port code: libc only.

#ifndef PORT_LUNGE_H
#define PORT_LUNGE_H

#include <stdbool.h>
#include <stdint.h>

#include "port/cpu.h"
#include "port/oam.h"
#include "port/step.h"
#include "port/terrain.h"
#include "port/wram.h"

#define LUNGE_PC 0x80dec5u
#define LUNGE_SLEEP_PC 0x80dec1u  // `JSL`, A already 1
#define LUNGE_ENDED_PC 0x80df45u
#define LUNGE_LEAP_PC 0x80df05u   // `JMP $DD0D`, into the water

// Where a kick into water looks, by the way the player faces: a word of x
// and one of y, from the step's point.
#define LUNGE_AHEAD 0xdf48u
#define LUNGE_DP_FACING 0x26

#define LUNGE_DP_RECORD 0x08
#define LUNGE_DP_X 0x30
#define LUNGE_DP_Y 0x32
#define LUNGE_DP_TRY_X 0x34
#define LUNGE_DP_TRY_Y 0x36
#define LUNGE_DP_STEP_X 0x60
#define LUNGE_DP_STEP_Y 0x62
#define LUNGE_DP_FRAMES_LEFT 0x68
#define LUNGE_GROUND_MASK 0x8b38u  // of the attributes, doubled
#define LUNGE_GROUND_ON 0x0100u    // ...and what sends them on

enum {
  LU_COUNT,  // DEC $68 : BEQ
  LU_TRY,    // $DEC9-$DEDF
  LU_WALL,   // $DEE0-$DEE8
  LU_TEST,   // LDX $34 : LDY $36 : JSL : BCS, three times
  LU_THING,  // $DF26-$DF31
  LU_PUT,    // $DF32-$DF44
  LU_WATER,  // $DEE9-$DF04: the look further on, to its `BCC`
  LU_AGAIN,  // LDA #$0001
  LU_TAKEN,
  LU_BLOCK_COUNT
};

typedef struct {
  uint16_t blocks[LU_BLOCK_COUNT];
  bool water;     // ground that sends them on...
  TerrainRegs ahead;  // ...the tile further on, asked about bit 8...
  bool leaps;     // ......which it has: into the water
  int slow_reads; // ...the two words of the look, if not through FastROM
  bool tried;
  TerrainRegs ground;
  bool asked_leash;
  TetherRegs leash;
  int edges;  // the level's edge is asked twice over
  BoundsRegs edge;
  bool asked_thing;
  ObstacleWork thing;
  // N and Z are a test's that does not hand them back.
  bool nz_unknown;
} LungeWork;

void lunge_frame(Wram* w, const Rom* rom, PortCpu* c, LungeWork* k);

#endif
