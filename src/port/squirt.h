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
// ## The rest of the thread
//
// Around that loop the thread is seven more stretches, each from where
// control arrives to the next call or yield it leaves by:
//
//     $81:FCB2  launch        is the player's own tile one that stops water?
//                             Then nothing is fired and the thread ends. If
//                             not, one more shot is in the air, and the sound.
//     $81:FD66  dress         the record `actor_slot_alloc` just gave it: the
//                             picture, the collision id of the side that
//                             fired, the step for the way it faces, and the
//                             place of the muzzle. Then the handler.
//     $81:FCE3  first frame   a step and the tile test, as the loop makes
//     $81:FCF7  second frame  ...and the picture of water in flight
//     $81:FD22  splash        the handler taken away, a picture for 5 ticks
//     $81:FD39  splash, 2     another for 4
//     $81:FD48  gone          one shot fewer, and the record freed
//
// The page as its player's code filled it: where it is fired from at `$00`
// and `$02`, the way it faces at `$04`, doubled as the game has it, 2 to 16,
// and the side at `$06`, 0 or 2. Four tables in bank `$81` are indexed by
// them.
//
// Port code: libc only.

#ifndef PORT_SQUIRT_H
#define PORT_SQUIRT_H

#include <stdint.h>

#include "assets/rom.h"
#include "port/cpu.h"
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

// --- The other seven stretches -------------------------------------------------

#define SQUIRT_LAUNCH_PC 0x81fcb2u
#define SQUIRT_DRESS_PC 0x81fd66u       // after `JSL actor_slot_alloc`
#define SQUIRT_FIRST_PC 0x81fce3u       // after the launch's yield
#define SQUIRT_SECOND_PC 0x81fcf7u      // after the first frame's
#define SQUIRT_SPLASH_PC SQUIRT_END_PC
#define SQUIRT_SPLASH_2_PC 0x81fd39u
#define SQUIRT_GONE_PC 0x81fd48u
// Where they leave.
#define SQUIRT_UNFIRED_RTL_PC 0x81fd5au    // the thread's end
#define SQUIRT_SFX_CALL_PC 0x81fd5eu       // `JSL apu_play_sfx`
#define SQUIRT_LAUNCH_YIELD_PC 0x81fcdfu
#define SQUIRT_FIRST_YIELD_PC 0x81fcf3u
#define SQUIRT_SPLASH_YIELD_PC 0x81fd35u
#define SQUIRT_SPLASH_2_YIELD_PC 0x81fd44u
#define SQUIRT_FREE_JML_PC 0x81fd56u       // `JML actor_slot_free`

// More of the page.
#define SQUIRT_DP_FROM_X 0x00
#define SQUIRT_DP_FROM_Y 0x02
#define SQUIRT_DP_FACING 0x04
#define SQUIRT_DP_SIDE 0x06
#define SQUIRT_DP_FACING_KEPT 0x3c
#define SQUIRT_DP_SIDE_KEPT 0x3e
#define SQUIRT_DP_FACING_AT 0x40  // the facing doubled again: a table offset

// How many of these shots are in the air.
#define W_SQUIRTS_LIVE 0x00deu

#define SQUIRT_SFX 0x000b
#define SQUIRT_FRAMES 20
#define SQUIRT_FACING_MAX 16
#define SQUIRT_SPLASH_TICKS 5
#define SQUIRT_SPLASH_2_TICKS 4
// Its pictures, in bank `$90`.
#define SQUIRT_PICTURE_BANK 0x0090
#define SQUIRT_PICTURE 0xa26b
#define SQUIRT_SPLASH_PICTURE 0xa2a2
#define SQUIRT_SPLASH_2_PICTURE 0xa2ab
// The tables. A side's height and collision id; a facing's step, across and
// down; a side's table of where the muzzle is for each facing; and two tables
// of a flags word and a picture for each facing.
#define SQUIRT_HEIGHTS 0x81fec7u
#define SQUIRT_COLLIDE_IDS 0x81fec3u
#define SQUIRT_STEPS 0x81fecbu
#define SQUIRT_MUZZLES 0x81fe2fu
#define SQUIRT_PICTURES_LAUNCH 0xfe7b
#define SQUIRT_PICTURES_FLIGHT 0xfe9f

enum {
  SQ_AIM,        // LDX $00 : LDY $02 : JSL : BCC
  SQ_UNFIRED,    // JMP $FD5A
  SQ_COUNT_UP,   // the branch taken, $FCBF-$FCCB and LDA #$000B
  SQ_DRESS,      // $FD66-$FDD6
  SQ_HANDLER,    // LDA # : LDY # : JSL, and `$80:8475` itself
  SQ_PICTURE,    // LDX # : JSR, and `$81:FDD7` itself
  SQ_TICKS,      // LDA #imm
  SQ_MOVE_TEST,  // JSR, `$81:FDF7`, LDX $0C : LDY $10 : JSL : BCS
  SQ_TAKEN,      // a branch taken
  SQ_SPLASH,     // $FD22-$FD34, and `$80:8475` itself
  SQ_SPLASH_2,   // $FD39-$FD43
  SQ_GONE,       // $FD48-$FD55
  SQ_BLOCK_COUNT
};

// What a stretch did, for the harness to price. `overflow_known` is false
// when the last thing to write the overflow flag was the tile test.
typedef struct {
  uint16_t blocks[SQ_BLOCK_COUNT];
  bool tested;
  TerrainRegs ground;
  bool overflow_known;
} SquirtWork;

void squirt_launch(Wram* w, PortCpu* c, SquirtWork* k);
void squirt_dress(Wram* w, const Rom* rom, PortCpu* c, SquirtWork* k);
void squirt_first_frame(Wram* w, PortCpu* c, SquirtWork* k);
void squirt_second_frame(Wram* w, const Rom* rom, PortCpu* c, SquirtWork* k);
void squirt_splash(Wram* w, PortCpu* c, SquirtWork* k);
void squirt_splash_2(Wram* w, PortCpu* c, SquirtWork* k);
void squirt_gone(Wram* w, PortCpu* c, SquirtWork* k);

#endif
