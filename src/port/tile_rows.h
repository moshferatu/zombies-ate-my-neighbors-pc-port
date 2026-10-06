// Where the rows of a level's map are, and a block put into it.
//
// `$80:ACA2` is called once a level, with the map's size in blocks in X and
// Y. A block is eight tiles a side. It makes the two tables everything else
// finds a row with: the offset of each row of tiles at `$7E:4328`, and the
// address of each row of blocks at `$7E:4228`. It also leaves the map's size
// in four words of page zero, and how far the camera may go.
//
// `$80:AB5A` changes one block of the map while a level is running: a door
// opened, a wall knocked down. It copies the block's eight rows of eight
// tiles from the library into the map, and for each row that is on the
// screen it queues a transfer of the tiles that are. A row that crosses the
// seam between the tilemap's two screens is two transfers.
//
// The routine holds the camera and the queue still while it works, with bit
// 14 of the render flags: see `RENDER_FLAG_CAMERA_HELD` in `port/camera.h`.
// That is why the port is not the whole of it. It is the stretch the bit is
// set for, from `$80:AB8F` to `$80:ABC9`. The ROM sets the bit before and
// clears it after, so an NMI that lands in the stretch finds it set whether
// the rows have been done yet or not, and reads nothing the rows write.
//
// Port code: libc only.

#ifndef PORT_TILE_ROWS_H
#define PORT_TILE_ROWS_H

#include <stdbool.h>
#include <stdint.h>

#include "assets/rom.h"
#include "port/cpu.h"
#include "port/wram.h"

#define TILEMAP_ROW_TABLES_PC 0x80aca2u
#define TILEMAP_ROW_TABLES_RTL_PC 0x80acf5u
#define TILE_BLOCK_ROWS_PC 0x80ab8fu      // `STZ $4C`, the bit just set
#define TILE_BLOCK_ROWS_END_PC 0x80abc9u  // `LDA #$4000`, to clear it

#define BLOCK_TILES 8
#define MAP_BLOCK_ROWS_MAX 128  // what the two tables have room for

// Page zero, which both routines make their direct page.
#define TB_DP_FROM 0x28       // the library's row, and its bank at `$2A`
#define TB_DP_TO 0x2c         // the map's row, and its bank at `$2E`
#define TB_DP_SCRATCH 0x38
#define TB_DP_COLUMN 0x3a     // of the row's first tile to send
#define TB_DP_ROW 0x3c
#define TB_DP_ROWS_LEFT 0x3e
#define TB_DP_LENGTH 0x40     // bytes of the row to send
#define TB_DP_SEND_FROM 0x42  // where in the map they are
#define TB_DP_SEEN 0x44       // a bit a test, shifted in
#define TB_DP_BLOCK_COLUMN 0x46
#define TB_DP_VRAM 0x48
#define TB_DP_CURSOR 0x4a     // the column, in the tilemap's sixty-four
#define TB_DP_FIRST_RUN 0x4c  // bytes this side of the seam
#define W_MAP_FIRST_ROW 0x00a6       // the map of blocks, in its bank
#define W_MAP_BLOCK_ROW_BYTES 0x00ae
#define W_MAP_BLOCK_ROWS 0x00b0
#define SCREEN_WIDTH 0x0100
#define SCREEN_HEIGHT 0x00f0
#define TILE_BLOCK_BANK_FROM 0x7e
#define TILE_BLOCK_BANK_TO 0x7f
#define VRAM_QUEUE_FULL 0x0030

enum {
  TR_HEAD,       // STZ $4C
  TR_ROW,        // $AB91-$AB97, $AAF7-$AB13 less the two calls' bodies
  TR_TEST_1,     // CPX $1B6E : BCC
  TR_TEST_2,     // CPX $1B70 : BCS
  TR_TEST_3,     // CPY $1B72 : BCC
  TR_TEST_4,     // CPY $1B74 : BEQ
  TR_TEST_5,     // BCS
  TR_TEST_END,   // SEC : RTS, or CLC : RTS
  TR_NONE,       // $AB1C
  TR_RIGHT_END,  // $AB1D-$AB3C
  TR_LEFT_END,   // $AB3D-$AB4D
  TR_WHOLE,      // $AB4E-$AB59
  TR_PLACE,      // $AAA1-$AAE0
  TR_ONE,        // $AAE1, $AA39-$AA5A
  TR_TWO,        // $AAE4-$AAF1, $AAF4, $AA5B-$AAA0
  TR_TWO_STORE,  // STA $4C
  TR_COPY,       // LDY #$000E, $ABB1-$ABC5
  TR_WORD,       // $AB9B-$ABA5, $ABAD-$ABB0
  TR_BELOW,      // $ABA6-$ABAC
  TR_TAIL,       // JSR $9E37 and its three instructions
  TR_TAKEN,
  TR_BLOCK_COUNT
};

typedef struct {
  uint16_t blocks[TR_BLOCK_COUNT];
} TileRowsWork;

// X and Y are the map's size in blocks. False for a height the tables have
// no room for, or none.
bool tilemap_row_tables_supported(uint16_t blocks_down);
void tilemap_row_tables(Wram* w, PortCpu* c);

// False unless the two pointers are in the banks the ROM puts them in and
// every row stays inside them.
bool tile_block_rows_supported(const Wram* w);
void tile_block_rows(Wram* w, const Rom* rom, PortCpu* c, TileRowsWork* k);

#endif
