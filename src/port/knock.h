// The tile a punch lands on.
//
// A player turned monster punches as it walks: see `port/pose.h`. At each of
// the last three pictures of the swing the pose calls `$80:F0D7`, which
// looks at the tile the fist is at. One with bit 6 of its attributes is one
// that can be knocked down, and a thread is begun that does it,
// `$81:F2B2`. That thread is handed the place, the way the player faces and
// the tile's attributes.
//
// Where the fist is depends on the picture and the way the player faces: a
// step across and a step down from the player, out of one of three tables
// of nine.
//
// One knocking at a time. The word at `$7E:1FF8` is set when a thread is
// begun, and while it is set the routine does nothing.
//
// Two ways out: the routine's `RTS`, with nothing begun, and the `JSL` that
// asks for the thread.
//
// Port code: libc only.

#ifndef PORT_KNOCK_H
#define PORT_KNOCK_H

#include <stdbool.h>
#include <stdint.h>

#include "assets/rom.h"
#include "port/cpu.h"
#include "port/wram.h"

#define KNOCK_PC 0x80f0d7u
#define KNOCK_RTS_PC 0x80f112u
#define KNOCK_SPAWN_PC 0x80f12au  // `JSL thread_spawn`, A and Y the thread

#define KNOCK_BANK 0x80
#define KNOCK_PICTURE_TABLES 0x80f12fu  // by picture: which table, a byte
#define KNOCK_TABLES 0x80f133u          // the tables' addresses
#define KNOCK_PICTURES 4
#define KNOCK_WAY_MAX 0x0010            // ways are doubled, and nine of them
#define KNOCK_CAN_BE 0x0040             // of a tile's attributes
#define KNOCK_THREAD 0xf2b2u
#define KNOCK_THREAD_BANK 0x0081
#define W_KNOCK_UNDER_WAY 0x1ff8
#define KNOCK_DP_ARG_X 0x00   // what the thread is handed
#define KNOCK_DP_ARG_Y 0x02
#define KNOCK_DP_ARG_WAY 0x04
#define KNOCK_DP_ARG_TILE 0x06
#define KNOCK_DP_PICTURE 0x18
#define KNOCK_DP_WAY 0x26
#define KNOCK_DP_AT 0x2c      // scratch: where in the table
#define KNOCK_DP_X 0x30       // the player's place
#define KNOCK_DP_Y 0x32
#define KNOCK_DP_FIST_X 0x34
#define KNOCK_DP_FIST_Y 0x36

typedef enum {
  KNOCK_BUSY,     // one is under way already
  KNOCK_NOTHING,  // the tile is not one to knock down
  KNOCK_BEGUN,
} KnockEnd;

// False for a picture or a way the tables do not have.
bool knock_supported(const Wram* w, const Rom* rom, uint16_t page);
KnockEnd knock_look(Wram* w, const Rom* rom, PortCpu* c);

#endif
