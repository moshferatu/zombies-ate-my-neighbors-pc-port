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
// ## The thread
//
// `$81:F2B2` puts a record 24 pixels below the place, in front of every
// layer, with nothing to hit it, and plays a sound and two pictures. Then
// the block there is swapped for its pair (`port/tile_rows.h`), two more
// pictures are played, and the record is freed. It counts itself nine in
// the level's load while it runs.
//
// The pictures sleep, so it is four stretches, each from where control
// arrives to the call it leaves by:
//
//   $81:F2B2  knock_thread_begin    to the `JSL apu_play_sfx`
//   $81:F315  knock_thread_swap     to the `JSL` that puts the block
//   $81:F348  knock_thread_swapped  to the `JSL pictures_play`
//   $81:F356  knock_thread_end      through `actor_slot_free`, whose `RTL`
//                                   is the thread's own, to `thread_exit`
//
// The swap is counted at `$7E:1FC2`. I have not read what reads that. The
// thread also keeps what its page had at `$08` in `$24`, and reads it
// nowhere.
//
// Port code: libc only.

#ifndef PORT_KNOCK_H
#define PORT_KNOCK_H

#include <stdbool.h>
#include <stdint.h>

#include "assets/rom.h"
#include "port/cpu.h"
#include "port/tile_rows.h"
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

// The thread's stretches, and where each stops.
#define KNOCK_THREAD_PC 0x81f2b2u
#define KNOCK_THREAD_SFX_PC 0x81f30au     // `JSL apu_play_sfx`, the sound in A
#define KNOCK_SWAP_PC 0x81f315u
#define KNOCK_SWAP_PUT_PC 0x81f344u       // `JSL $80:AB5A`
#define KNOCK_SWAPPED_PC 0x81f348u
#define KNOCK_SWAPPED_PLAY_PC 0x81f352u   // `JSL pictures_play`, the list in A
#define KNOCK_END_PC 0x81f356u
#define KNOCK_EXITED_PC 0x00833eu         // `thread_exit`, by the free's `RTL`

#define KNOCK_THREAD_DATA_BANK 0x81
#define KNOCK_LOAD 9
#define KNOCK_DP_RECORD 0x08
#define KNOCK_DP_KEPT 0x24       // what `$08` had before the record
#define KNOCK_BELOW 0x0018       // the record is this far under the place
#define KNOCK_PICTURE 0xb9e0u
#define KNOCK_PICTURE_BANK 0x0090
#define KNOCK_COLLIDE_ID 0x0007
#define KNOCK_SFX 0x0021
#define KNOCK_PICTURES_AFTER 0xf374u  // the list it plays once swapped
#define W_BLOCKS_SWAPPED 0x1fc2

typedef enum {
  KNOCK_BUSY,     // one is under way already
  KNOCK_NOTHING,  // the tile is not one to knock down
  KNOCK_BEGUN,
} KnockEnd;

// False for a picture or a way the tables do not have.
bool knock_supported(const Wram* w, const Rom* rom, uint16_t page);
KnockEnd knock_look(Wram* w, const Rom* rom, PortCpu* c);

// False with no record free, which is the ROM's: it only looks then.
bool knock_thread_begin(Wram* w, PortCpu* c, uint16_t* record);

bool knock_thread_swap_supported(const Wram* w, const Rom* rom, uint16_t page);
void knock_thread_swap(Wram* w, const Rom* rom, PortCpu* c, BlockSwapWork* k);

void knock_thread_swapped(Wram* w, PortCpu* c);

// False for a load that would go below nothing, where the ROM stops, a
// record the list does not hold, or a stack that is not a thread's.
bool knock_thread_end_supported(const Wram* w, uint16_t page, uint16_t s);
// `place` is where in the display list its record was.
void knock_thread_end(Wram* w, PortCpu* c, int* place);

#endif
