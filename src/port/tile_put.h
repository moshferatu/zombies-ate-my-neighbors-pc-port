// One tile changed in the level's map, and on the screen if it is there.
//
// `$80:ABD3` takes a tile's word in A and a column and a row in X and Y. The
// word goes into the map in bank `$7F`. A tile numbered below `$DC` has bit
// 13 set first, which draws it over the sprites.
//
// A tile the camera has in its window (`$1B6E` to `$1B74`) is on the screen
// as well, so its word and its VRAM address go on a list at `$1C74` and
// `$1C92`, counted in bytes at `$D0`. The first on an empty list queues the
// vblank job at `$80:AC55`, which sends the list.
//
// `$ED` is set while it runs and cleared after, and the job leaves the list
// alone while it is set.
//
// `tile_put_job` is that job. With `$ED` set it stays queued: carry set.
// Otherwise it sends each word to its address, last on the list first, and
// empties the list: carry clear. It records its writes as `port/hw.h` has
// them, and the harness makes them.
//
// ## Its contract with the ROM
//
// WRAM as the ROM leaves it, and the registers and carry: what they are
// depends on how far it got, and `TilePutRegs` says. N and Z are the caller's
// direct page's, from the `PLD` at the end.
//
// Port code: libc only.

#ifndef PORT_TILE_PUT_H
#define PORT_TILE_PUT_H

#include <stdbool.h>
#include <stdint.h>

#include "assets/rom.h"
#include "port/hw.h"
#include "port/wram.h"

#define MAP_TILE_PUT_PC 0x80abd3u
#define MAP_TILE_PUT_RTL_PC 0x80ac54u
#define MAP_TILE_JOB_PC 0x80ac55u  // the job that sends the list
#define MAP_TILE_JOB_RTL_PC 0x80ac7bu  // of two: the other is at `$AC7D`
#define TILE_PUT_LIST_BYTES 30  // fifteen words, and then the addresses

#define W_TILE_PUT_BUSY 0x00edu
#define W_TILE_PUT_BYTES 0x00d0u
#define W_TILE_PUT_WORDS 0x1c74u
#define W_TILE_PUT_VRAM 0x1c92u
#define W_TILE_OVER_SPRITES_BELOW 0x00dcu
#define TILE_OVER_SPRITES 0x2000u
#define TILE_NUMBER_MASK 0x01ffu
// The camera's window, in tiles: the first column and the one past the
// last, the first row and the last.
#define W_WINDOW_LEFT 0x1b6eu
#define W_WINDOW_RIGHT 0x1b70u
#define W_WINDOW_TOP 0x1b72u
#define W_WINDOW_BOTTOM 0x1b74u
// Where the window's first column and row are in the layer, and the
// layer's base in VRAM.
#define W_WINDOW_LAYER_X 0x1b76u
#define W_WINDOW_LAYER_Y 0x1b7au
#define W_WINDOW_VRAM 0x1b7eu
#define TILE_PUT_COLUMNS 0x809d77u  // a VRAM offset for each of 64 columns
#define TILE_PUT_ROWS 0x809df7u     // ...and for each of 32 rows

// Which compare of the window's test decided.
typedef enum {
  TILE_PUT_LEFT_OF,   // CPX $1B6E : BCC
  TILE_PUT_RIGHT_OF,  // CPX $1B70 : BCS
  TILE_PUT_ABOVE,     // CPY $1B72 : BCC
  TILE_PUT_LAST_ROW,  // CPY $1B74 : BEQ, and it is on the screen
  TILE_PUT_BELOW,     // BCS
  TILE_PUT_INSIDE,
} TilePutWindow;

typedef struct {
  uint16_t a, x, y;
  bool c;
  bool over_sprites;  // the tile's number was below `$DC`
  TilePutWindow window;
  bool asked;  // the list was empty, and the job was queued...
  int slot;    // ...in this slot of the vblank's queue, or -1 with it full
} TilePutRegs;

void map_tile_put(Wram* w, const Rom* rom, uint16_t tile, uint16_t col,
                  uint16_t row, TilePutRegs* out);

typedef enum {
  TILE_JOB_BUSY,   // `$ED` set: nothing sent, and A is that word
  TILE_JOB_EMPTY,  // nothing on the list: A is `$0080`, X is 0
  TILE_JOB_SENT,   // A is the first word of the list, X is `$FFFE`
} TileJobFate;

// False for a list longer than its room, or an odd count of bytes.
bool tile_put_job_supported(const Wram* w);
TileJobFate tile_put_job(Wram* w, HwTrace* t, uint16_t* a_out);

#endif
