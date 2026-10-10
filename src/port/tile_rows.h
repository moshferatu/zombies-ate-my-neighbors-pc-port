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
// That is why the port is not the whole of it. `tile_block_rows` is the
// stretch the bit is set for, from `$80:AB8F` to the `JSR` at `$80:ABCE`,
// and its last act is to clear the bit. An NMI that lands in the stretch
// finds the bit set whether the rows have been done yet or not, and reads
// nothing the rows write.
//
// `tile_block_begin` is what comes before, from `$80:AB5A` to `$80:AB8F`:
// where the block's tiles are in the library, where its first row is in
// the map, and eight rows to do. Its last act is to set the bit. It writes
// nothing else but the routine's own scratch on page zero. The bit is set
// before the rows' time begins, as it is in the ROM.
//
// ## A block swapped for its pair
//
// A door and a wall that can come down are two blocks numbered next to each
// other, shut and open: the callers that open one read the block at a place
// and put the same number with its low bit turned over. Three of them do it
// with the same instructions, their own places apart. `tile_block_swap_ask`
// is those: the place in pixels to a column and a row of blocks, the block
// there, and its pair in A for the `JSL` to `$80:AB5A` that follows.
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
#define TILE_BLOCK_BEGIN_PC 0x80ab5au
#define TILE_BLOCK_ROWS_PC 0x80ab8fu      // `STZ $4C`, the camera just held
#define TILE_BLOCK_ROWS_END_PC 0x80abceu  // `JSR`, the camera let go

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
#define W_BLOCK_LIBRARY 0x00aa       // the blocks' tiles, and their bank
#define W_BLOCK_LIBRARY_BANK 0x00ac
#define BLOCK_NUMBER_MASK 0x01ffu
#define BLOCK_LIBRARY_SHIFT 7        // 128 bytes a block
#define BLOCK_PIXEL_SHIFT 6          // sixty-four pixels a block
#define BLOCK_PAIR_BIT 0x0001u
#define BLOCK_SWAP_COLUMN 0x0038     // the swap's column and row, page zero
#define BLOCK_SWAP_ROW 0x003a
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
  TR_RELEASE,    // LDA #$4000 : TRB $26
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

// `$80:AB5A`: block A put at column X and row Y of the map of blocks. `v`
// is the overflow its last sum left.
void tile_block_begin(Wram* w, PortCpu* c, bool* v);

// What the swap read, for the harness.
typedef struct {
  bool v;            // overflow, from the sum that found the cell
  bool cell_in_rom;  // the map of blocks is in the cartridge
} BlockSwapWork;

// False if the cell for that place is not where a word can be read.
bool tile_block_swap_supported(const Wram* w, const Rom* rom, uint16_t x,
                               uint16_t y);
// `PHD`, the column and row to `$0038` and `$003A`, page zero, `$80:ACF6`,
// and the block's pair. It leaves the caller's page on the stack and the
// column and row in X and Y. The caller says where it stops.
void tile_block_swap_ask(Wram* w, const Rom* rom, PortCpu* c, uint16_t x,
                         uint16_t y, BlockSwapWork* k);

// False unless the two pointers are in the banks the ROM puts them in and
// every row stays inside them.
bool tile_block_rows_supported(const Wram* w);
void tile_block_rows(Wram* w, const Rom* rom, PortCpu* c, TileRowsWork* k);

#endif
