// ZAMN's level layout — the format that turns a level number into a BG tilemap.
//
// A level is stored in three pieces, all named by a 54-byte record in bank
// `$9F`:
//
//   * a **block library** — up to 256 "blocks", each an 8x8 array of 16-bit BG
//     tilemap entries (128 bytes), shared by every level that uses the same
//     tileset and shipped as one LZSS stream;
//   * a **block map** — `cols * rows` 16-bit block indices, the level's actual
//     shape;
//   * a **tile attribute table** — 512 words, one per BG tile, whose bit 0 is
//     "solid". This is how the game does collision: it never looks at the
//     block map, only at the expanded tilemap and this table.
//
// At load time the game *expands* the block map into a complete 16-bit tilemap
// in WRAM bank `$7F` (`$80:AD2B`), one entry per 8x8 tile, and from then on
// only reads that. The camera streams rows and columns straight out of it into
// the PPU (`$80:A462` / `$80:A5E5` / `$80:A61D`). `level_expand()` reproduces
// that buffer byte for byte.
//
// Everything here was recovered from traced execution, and
// `zamn_assets verify-level` checks it by replaying a movie under the reference
// core and diffing the whole expanded map — plus the derived scalars, the row
// tables and the palettes — against what the ROM built. See
// `docs/asset-formats.md`.
//
// Port code: libc only.

#ifndef ASSETS_LEVEL_H
#define ASSETS_LEVEL_H

#include <stdbool.h>
#include <stdint.h>

#include "assets/lzss.h"
#include "assets/rom.h"

// The record table at `$9F:8000`: one 16-bit bank-$9F address per level, and
// levels are numbered from 1. Entry 0 is not a record (it holds `$0032`).
#define LEVEL_TABLE_ADDR 0x9f8000u
#define LEVEL_FIRST 1
#define LEVEL_COUNT 56

#define LEVEL_RECORD_BYTES 0x36

// A block is 8x8 tiles, stored as 64 little-endian tilemap entries.
#define LEVEL_BLOCK_TILES 8
#define LEVEL_BLOCK_BYTES (LEVEL_BLOCK_TILES * LEVEL_BLOCK_TILES * 2)

// The block library decompresses to `$7E:8000`, which leaves room for exactly
// 256 blocks before WRAM bank $7E ends.
#define LEVEL_MAX_BLOCKS 256
#define LEVEL_BLOCK_LIB_BYTES (LEVEL_MAX_BLOCKS * LEVEL_BLOCK_BYTES)
#define LEVEL_BLOCK_LIB_BASE 0x8000u

// 512 BG tiles: 16 KB of 4bpp characters, and one attribute word each.
#define LEVEL_BG_TILES 512
#define LEVEL_BG_TILES_BYTES 16384
#define LEVEL_TILE_ATTR_BYTES (LEVEL_BG_TILES * 2)

// One 256-byte (128-colour) palette for the background and one for sprites.
#define LEVEL_PALETTE_BYTES 256

// Bit 0 of a tile attribute word blocks movement (`$80:AE43` and friends test
// it with `LSR A / BCS`). The rest of the word is not yet identified.
#define LEVEL_ATTR_SOLID 0x0001

// The 54-byte level record. Offsets are the ones `$80:86A2` indexes.
typedef struct {
  uint32_t record_addr;  // where this came from, for reporting

  uint32_t block_defs;      // +$00/$02  LZSS stream -> the block library
  uint32_t block_map;       // +$04/$06  cols*rows 16-bit block indices
  uint32_t tile_attrs;      // +$08/$0A  LEVEL_TILE_ATTR_BYTES, -> $7E:611A
  uint32_t bg_tiles;        // +$0C/$0E  16 KB of 4bpp BG characters
  uint32_t bg_palette;      // +$10/$12  256 B -> $7E:5428 and $7E:5628
  uint32_t sprite_palette;  // +$14/$16  256 B -> $7E:5528

  // Not yet identified. `list_*` are addresses in bank $9F itself, pointing at
  // per-level data that sits between the records — the likely home of the
  // actor placements.
  uint16_t unknown_18, unknown_1a;
  uint16_t list_1c, list_1e, list_20;

  uint16_t cols;  // +$22  block columns
  uint16_t rows;  // +$24  block rows

  // +$26  tiles with an index below this get BG priority forced on as they are
  // streamed to the PPU (`$80:A47B`). It is a draw-time flag, not part of the
  // expanded map.
  uint16_t priority_below;

  uint16_t unknown_28;  // +$28  passed to $80:9F29 as X

  // +$2A..$30  the two players' start positions. The camera starts centred
  // between them: x = (start_x1 + start_x2) / 2 - $80.
  uint16_t start_x1, start_y1, start_x2, start_y2;

  uint16_t unknown_32, unknown_34;  // +$32/$34  arguments to the intro screen
} LevelHeader;

typedef enum {
  LEVEL_OK = 0,
  LEVEL_ERR_RANGE = -1,      // level number outside 1..LEVEL_COUNT
  LEVEL_ERR_ADDRESS = -2,    // a record field does not point at cartridge ROM
  LEVEL_ERR_SIZE = -3,       // caller's buffer is too small
  LEVEL_ERR_BLOCK = -4,      // a block index addresses outside the block library
  LEVEL_ERR_DECOMPRESS = -5, // the block library stream is malformed
} LevelStatus;

// Address of the record for `level` (1..LEVEL_COUNT), read from the table.
bool level_record_addr(const Rom* rom, int level, uint32_t* out_addr);

// Parse the 54-byte record at `addr`. Returns LEVEL_ERR_ADDRESS if the record
// itself is unreadable; the pointers inside it are only checked when used.
int level_header_read(const Rom* rom, uint32_t addr, LevelHeader* out);

// Geometry. The expanded map is `tile_cols * tile_rows` 16-bit entries, laid
// out row-major with a stride of `tile_cols` — the same buffer the ROM builds
// in bank $7F, where the stride is `$B2` bytes.
static inline uint32_t level_tile_cols(const LevelHeader* h) {
  return (uint32_t)h->cols * LEVEL_BLOCK_TILES;
}
static inline uint32_t level_tile_rows(const LevelHeader* h) {
  return (uint32_t)h->rows * LEVEL_BLOCK_TILES;
}
static inline uint32_t level_map_entries(const LevelHeader* h) {
  return level_tile_cols(h) * level_tile_rows(h);
}
static inline uint32_t level_width_px(const LevelHeader* h) {
  return level_tile_cols(h) * 8;
}
static inline uint32_t level_height_px(const LevelHeader* h) {
  return level_tile_rows(h) * 8;
}

// Decompress the block library into `out` (LEVEL_BLOCK_LIB_BYTES). `ring` is
// the decompressor's window; pass the same one the rest of the game uses.
// `out_bytes` receives how much the stream actually produced — the ROM's own
// buffer is whatever is left over from the previous level.
int level_load_blocks(const Rom* rom, const LevelHeader* h, uint8_t* out,
                      uint32_t* out_bytes, LzssRing* ring);

// Expand the block map into a full tilemap. `blocks` is the decompressed block
// library, `blocks_len` its length; `out` takes `level_map_entries(h)` 16-bit
// entries and `out_entries` is its capacity.
//
// Faithful to `$80:AD2B`, including its 16-bit arithmetic: a block index is
// turned into an address as `$8000 + index * 128` truncated to 16 bits, so an
// index above 255 wraps out of the library rather than reading past it. That
// is reported as LEVEL_ERR_BLOCK instead of being silently reproduced.
int level_expand(const LevelHeader* h, const uint8_t* blocks, uint32_t blocks_len,
                 const Rom* rom, uint16_t* out, uint32_t out_entries);

// The scalars `$80:ACA2` derives and the rest of the engine scrolls by. Kept
// here because they are part of the format, not of the renderer.
static inline uint16_t level_row_stride_bytes(const LevelHeader* h) {
  return (uint16_t)(h->cols * 16);  // $B2
}
static inline uint16_t level_max_scroll_x(const LevelHeader* h) {
  return (uint16_t)(level_row_stride_bytes(h) * 4 - 0x100);  // $B8
}
static inline uint16_t level_max_scroll_y(const LevelHeader* h) {
  return (uint16_t)(level_tile_rows(h) * 8 - 0xf0);  // $B6
}

#endif
