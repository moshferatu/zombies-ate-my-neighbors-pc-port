// A whole screen of the level's tiles, made ready to send.
//
// `$80:A4D9` is what puts a level on the screen before the camera has moved:
// the 32 tiles across and 31 down that the camera is over, and the column to
// the right of them, copied out of the level's map into a buffer. The scroll
// routines of `port/camera.h` do the same for one row or one column at a time
// from then on. Then it queues the two for VRAM and waits for them to go.
//
// This is the copying, from the routine's entry to `$80:A4FA`:
//
//     PHB : PHD : PEA $007E : PLB : LDA #$0000 : TCD
//     JSR $A439      where the camera is, in tiles and the pixels left over
//     LDA #$083E : JSR $A401 : STA $3E : STA $2C      a buffer
//     JSR $A462      31 rows of 32 tiles
//     LDA $2C : STA $40
//     JSR $A4A0      32 tiles of the next column
//
// The queueing is three stretches more. `screen_tiles_queue`, from
// `$80:A4FA`, writes two entries past the VRAM queue's end: the rows to the
// level's map, and the column to the page after it, a tile a row. Nothing
// reads them there. `screen_tiles_queued` is the one instruction that counts
// them in, `STA $CE`, kept on its own so that a vblank finds the queue
// longer exactly when the ROM's would. The wait after it is `port/hold.h`'s,
// and `screen_tiles_end` takes the caller's direct page and data bank back.
//
// ## A tile's priority
//
// A word of the map is a tile's number in its low nine bits. One whose
// number is under the level's `W_TILE_PRIORITY_BELOW` gets bit 13 set in the
// copy, which is the bit that draws a tile over the sprites.
//
// ## Its contract with the ROM
//
// WRAM as the ROM leaves it: the buffer, the camera's four words, the
// allocator's two, and the scratch on page zero. It leaves on page zero with
// the data bank `$7E`, and with what the entry pushed still on the stack:
// the caller's data bank and direct page, and a spare byte of the `PEA`.
// X is zero, and A and Y are both 32 rows' worth of the map's row length.
//
// Port code: libc only.

#ifndef PORT_SCREEN_TILES_H
#define PORT_SCREEN_TILES_H

#include <stdbool.h>
#include <stdint.h>

#include "port/cpu.h"
#include "port/wram.h"

#define SCREEN_TILES_PC 0x80a4d9u
#define SCREEN_TILES_QUEUE_PC 0x80a4fau  // the `PLB` before the queue is filled
#define SCREEN_TILES_QUEUED_PC 0x80a540u  // `STA $CE`
#define SCREEN_TILES_REQUEST_PC 0x80a542u  // `JSR $9E6D`
#define SCREEN_TILES_END_PC 0x80a54au
#define SCREEN_TILES_RTL_PC 0x80a54cu
#define SCREEN_ROWS_BYTES 0x0740u    // 29 rows of 32 tiles
#define SCREEN_COLUMN_BYTES 0x003au  // ...and 29 of the column
#define SCREEN_COLUMN_VRAM 0x0400u   // the page after the map's first

#define SCREEN_TILES_ACROSS 32
#define SCREEN_TILES_DOWN 31
#define SCREEN_TILES_COLUMN 32       // the extra column is a tile longer
#define SCREEN_TILES_BUFFER 0x083eu  // what it asks the allocator for
#define SCREEN_TILE_NUMBER 0x01ffu
#define SCREEN_TILE_PRIORITY 0x2000u
#define SCREEN_MAP_BANK 0x7fu

// Scratch on page zero.
#define SCREEN_DP_MAP 0x28      // a far pointer into the level's map
#define SCREEN_DP_TO 0x2c       // where in the buffer
#define SCREEN_DP_COLUMN 0x38   // the camera's tile
#define SCREEN_DP_ROW 0x3a
#define SCREEN_DP_BUFFER 0x3e   // the buffer's start
#define SCREEN_DP_BUFFER_COLUMN 0x40  // ...and where the extra column starts

// For the harness: how many tiles of each copy got the priority bit.
typedef struct {
  int rows_over, column_over;
} ScreenTilesWork;

// False when the allocator would have to wait for room: see
// `tilemap_buffer_alloc` in `port/camera.h`.
bool screen_tiles_supported(const Wram* w);

void screen_tiles_fill(Wram* w, PortCpu* c, ScreenTilesWork* k);

// The queue has to have room for two entries, and the byte the stack holds
// has to be a data bank that sees low memory.
bool screen_tiles_queue_supported(const Wram* w, uint16_t s);
void screen_tiles_queue(Wram* w, PortCpu* c);
void screen_tiles_queued(Wram* w, PortCpu* c);
void screen_tiles_end(const Wram* w, PortCpu* c);

#endif
