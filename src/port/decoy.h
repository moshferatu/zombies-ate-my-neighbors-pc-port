// A thing put down that the monsters go for.
//
// `$81:F159` is a thread. It makes a display record where it was started,
// with collide id `$38`, and counts itself at `$1F90`. Every ten frames it
// looks at the tile under it, shows the next of four pictures, and plays
// sound 2 each time the four come round. It ends when the tile's attributes
// are `$0400`, when the word at `$1C` on its page has gone negative, or
// after a hundred turns.
//
// This is a turn of that loop, from the return of its sleep at `$81:F16D` to
// the next sleep, or to `$81:F1DB` where the ROM ends it.
//
// A tile with bit 3 moves it two pixels, the way the tile says. No movie
// has that, and a turn on such a tile is the ROM's.
//
// Collide id `$38` is one of the four `actor_nearest` looks for, with the
// two players' and the neighbours': it is something monsters make for. By
// that, and by how long it lasts, it is the decoy. I have not seen it on a
// screen.
//
// A turn with the sound in it is checked and not priced.
//
// Port code: libc only.

#ifndef PORT_DECOY_H
#define PORT_DECOY_H

#include <stdbool.h>
#include <stdint.h>

#include "assets/rom.h"
#include "port/apu.h"
#include "port/cpu.h"
#include "port/oam.h"
#include "port/terrain.h"
#include "port/wram.h"

#define DECOY_PC 0x81f16du
#define DECOY_SLEEP_PC 0x81f169u  // `JSL`, A already the frames
#define DECOY_ENDED_PC 0x81f1dbu

#define DECOY_BANK 0x81
#define DECOY_PICTURES 0x81f2aau
#define DECOY_PICTURE_COUNT 4
#define DECOY_FRAMES 0x000a
#define DECOY_SFX 0x0002
#define DECOY_TILE_ENDS 0x0400u
#define DECOY_TILE_MOVES 0x0008u

#define DECOY_DP_X 0x00
#define DECOY_DP_Y 0x02
#define DECOY_DP_RECORD 0x0a
#define DECOY_DP_HIT 0x1c
#define DECOY_DP_PICTURE 0x3c
#define DECOY_DP_TURNS_LEFT 0x3e

typedef enum {
  DECOY_ON,
  DECOY_ON_AN_ENDING_TILE,
  DECOY_HIT,
  DECOY_RAN_OUT,
} DecoyEnd;

typedef struct {
  bool declined;  // a tile that moves it
  DecoyEnd end;
  bool played;    // the four came round, and the sound with them
} DecoyWork;

void decoy_frame(Wram* w, const Rom* rom, PortCpu* c, DecoyWork* k);

#endif
