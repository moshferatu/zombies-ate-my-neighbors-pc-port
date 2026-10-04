// The squirt gun's water: a shot in flight.
//
// A shot is a thread, `$81:FCB2`. It is the starting weapon's: the movies that
// never change weapon fire it. It flies a fixed step a frame, for two frames
// and then up to twenty more, and ends early off the level or at anything a
// tile's attribute bit 2 marks. Either way it finishes with two
// pictures of a splash. Hitting an enemy is not decided here: the overlap pass
// does that, through the collision id on its display record and the handler
// it installs at `$81:FE0E`.
//
// This is one frame of the flight, from where `thread_yield` returns:
//
//     $81:FD0A  LDA #$0001 : JSL thread_yield
//     $81:FD11  JSR $FDF7             move, and move the picture
//               LDX $0C : LDY $10 : JSL terrain_point_bit2 : BCS $FD22
//               DEC $42 : BNE $FD0A
//     $81:FD22  the splash
//
// The first two frames of a shot run the same three steps from two other
// places, before the loop, and are the ROM's.
//
// Port code: libc only.

#ifndef PORT_SQUIRT_H
#define PORT_SQUIRT_H

#include <stdint.h>

#include "port/terrain.h"
#include "port/wram.h"

#define SQUIRT_FLIGHT_PC 0x81fd11u
#define SQUIRT_YIELD_PC 0x81fd0du  // `JSL thread_yield`, A already 1
#define SQUIRT_END_PC 0x81fd22u
#define SQUIRT_YIELD_TICKS 1

// Fields on the shot's page.
#define SQUIRT_DP_RECORD 0x0a
#define SQUIRT_DP_X 0x0c
#define SQUIRT_DP_Y 0x10
#define SQUIRT_DP_FRAMES_LEFT 0x42
#define SQUIRT_DP_STEP_X 0x44
#define SQUIRT_DP_STEP_Y 0x46

typedef enum {
  SQUIRT_FLIES,       // on to the next frame
  SQUIRT_HIT_GROUND,  // stopped by a tile
  SQUIRT_SPENT,       // out of frames
} SquirtNext;

// One frame of flight, for the shot whose page is `page`. `ground` is what
// the tile test answered, which the harness prices the call by.
SquirtNext squirt_flight_frame(Wram* w, uint16_t page, TerrainRegs* ground);

#endif
