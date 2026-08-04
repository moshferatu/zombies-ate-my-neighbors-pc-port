// Building the tile map a level is played on, out of the blocks it is made of.
//
//   $80:AD2B  blockmap_expand    0.5%   the whole map, once, at level load
//   $80:ACF6  blockmap_cell_ptr  --     where one cell of the block map lives
//
// A ZAMN level is not stored as tiles. It is stored as a **block map**: one
// 16-bit index per 8x8-tile block, and a library of those blocks at `$7E:8000`
// that `$80:86A2 level_load` points `$AA`/`$AC` at before calling. `$80:AD2B`
// walks the block map once and expands every cell into 64 tiles of the real map
// in bank `$7F` -- the same map `port/terrain.h` spends the rest of the level
// reading, and `port/camera.h` copies strips out of as the view scrolls.
//
// So this is where that map comes from, and it is the only thing in the ranking
// so far that builds rather than reads.
//
// ## Why it is 0.5% over ten calls
//
// It runs **once per level** and does 20,691 iterations of its innermost loop
// while it does. Every other routine in this registry is called thousands or
// millions of times and does a little each time; this is the opposite shape,
// and the "ten calls" in the ranking is one per movie in the profile set rather
// than anything about how hot it is.
//
// That shape is worth being careful about, because two rows above it on the
// same board turned out to be spin loops wearing the same disguise -- a large
// share next to a single-digit call count. This one is not: its hot bytes are
// `LDA [$28],Y : STA [$2C],Y : DEY DEY : BPL`, which is an eight-word copy and
// not a wait on anything.
//
// ## Three levels of loop, and a helper at each
//
// For every cell of the block map:
//
//   * `$80:AD1C tilemap_tile_addr` (in `port/terrain.h`) turns the block's
//     column and row -- multiplied by eight, so tile coordinates -- into the
//     address the tiles go to;
//   * `blockmap_cell_ptr` turns the same column and row, unmultiplied, into a
//     pointer at the cell itself. It is `tilemap_tile_addr`'s exact twin: the
//     same seven instructions against a different row table, `$7E:4228` instead
//     of `$7E:4328`, and it leaves its answer in `$28` rather than in A;
//   * `$80:AD0B` multiplies the index it finds there by 128 -- seven `ASL`s,
//     which is eight rows of sixteen bytes -- and adds the library base.
//
// Then eight rows of eight words are copied, the source stepping by 16 and the
// destination by `$B2`, the tile map's row stride.
//
// `blockmap_cell_ptr` is registered in its own right because **ten call sites
// in four banks reach it** and this routine is only one of them. `$80:AD0B` has
// two and is left inline.
//
// ## The block map's bank is level data
//
// `$AC` is always `$007E` -- `$80:86A2` writes it as a constant -- so the block
// library is always WRAM. `$A8` is not: it comes out of the level's own record
// at `$9F:0006,X`, so the map itself may be in WRAM or in the cartridge, and
// both are read through one accessor with a guard in front of it. Nothing else
// can appear there, and a call whose bank is neither is declined rather than
// guessed at.
//
// ## Both loops are do-whiles
//
// `DEC $38 : BNE` and `DEC $3A : BNE` are both tested at the bottom, so a map
// zero blocks wide or zero rows tall is walked 65,536 times rather than none.
// No level record is zero, and the port reproduces the wrap rather than
// guarding it, for the same reason `boss_bg_queue` does: a guard here would be
// a difference from the ROM that no input can distinguish from a fix.
//
// Port code: libc only.

#ifndef PORT_LEVELMAP_H
#define PORT_LEVELMAP_H

#include <stdbool.h>
#include <stdint.h>

#include "assets/rom.h"
#include "port/wram.h"

#define BLOCKMAP_EXPAND_ENTRY 0x80ad2bu
#define BLOCKMAP_CELL_PTR_ENTRY 0x80acf6u

// Direct page. `$80:AD2B` forces `D` to zero with `PHD : PEA $0000 : PLD`, so
// for it these are absolute WRAM; `blockmap_cell_ptr` has no `PHD` at all and
// takes its caller's, which is why its port function is handed one.
#define LM_DP_SRC 0x28u        // the cell pointer, then the block pointer...
#define LM_DP_SRC_BANK 0x2au   // ...and its bank
#define LM_DP_DEST 0x2cu       // where the tiles go...
#define LM_DP_DEST_BANK 0x2eu  // ...always $007F
#define LM_DP_COLS_LEFT 0x38u  // the inner countdown
#define LM_DP_ROWS_LEFT 0x3au  // ...and the outer one
#define LM_DP_COL 0xa2u        // the cell being expanded
#define LM_DP_ROW 0xa4u
#define LM_DP_MAP_BANK 0xa8u       // the block map's bank, from the level record
#define LM_DP_BLOCKS 0xaau         // the block library: $8000...
#define LM_DP_BLOCKS_BANK 0xacu    // ...in bank $7E, written as a constant
#define LM_DP_MAP_ROW_BYTES 0xaeu  // two per block, so `LSR` gives the count
#define LM_DP_MAP_ROWS 0xb0u
#define LM_DP_DEST_STRIDE 0xb2u  // the tile map's row stride, in bytes

#define LM_TILE_MAP_BANK 0x007fu     // where the expanded map lives
#define LM_BLOCK_BYTES 0x0080u       // 8 rows of 16 -- the seven `ASL`s
#define LM_BLOCK_ROW_BYTES 0x0010u   // ...one of those rows
#define LM_BLOCK_ROWS 8              // `LDX #$0008`
#define LM_BLOCK_LAST_WORD 0x000eu   // `LDY #$000E`, counted down by twos
#define LM_BLOCK_TILES_ACROSS 8      // the `ASL ASL ASL` on the way to $80:AD1C

// `$80:ACF6`. A is the bank it stored, not the pointer -- the pointer went to
// `$28` and the routine's last load was `$A8`. **X comes back doubled**, and
// that is not a restore: `PHA` saved the column already shifted and `PLX` puts
// that back, exactly as `tilemap_tile_addr` does. N and Z are that doubled
// column's, from the `PLX`; the carry is the `ADC`'s, two instructions earlier,
// and nothing between them touches it.
typedef struct {
  uint16_t a, x;
  bool n, z, c;
} BlockCellRegs;

bool blockmap_cell_ptr_supported(const Wram* w, uint16_t dp);
void blockmap_cell_ptr(Wram* w, uint16_t dp, uint16_t x, uint16_t y,
                       BlockCellRegs* out);

// `$80:AD2B`. A is the destination pointer as the last row left it, X is zero
// -- the `DEX` that ended the row loop -- and Y is `$FFFE`, the copy loop's
// index one step past the end. The carry is the last `ADC $B2`'s and survives
// every `DEC` after it. N and Z are the closing `PLD`'s, so the shim, not this,
// is where they come from.
typedef struct {
  uint16_t a, x, y;
  bool c;
} BlockExpandRegs;

bool blockmap_expand_supported(const Wram* w, const Rom* rom);
void blockmap_expand(Wram* w, const Rom* rom, BlockExpandRegs* out);

#endif
