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
// it with `LSR A / BCS`).
#define LEVEL_ATTR_SOLID 0x0001

// Bit 1 blocks it too, for a different set of movers. `$80:AE97` is `$80:AE14`
// byte for byte over the same six-tile footprint with `BIT #$0002 / BNE` in
// place of the `LSR A / BCS`, and its three callers — `$81:80CB`, `$81:85D7`,
// `$81:8618` — are all enemy bodies, two of them the same routines that pick a
// chase target twelve and fifteen bytes further on.
//
// **The two bits are not a hierarchy.** Across all 55 levels 216 tiles carry
// bit 0 without bit 1 and 519 carry bit 1 without bit 0, so neither set
// contains the other and terrain that stops one kind of mover need not stop the
// other. 9,903 of the 15,000-odd blocking tiles carry both.
#define LEVEL_ATTR_SOLID_ENEMY 0x0002

// Four more bits are read by four routines in one stretch of bank $80, and all
// four are now ported — see `port/terrain.h`, which has the counts and the call
// sites. They fall into three kinds.
//
// **Bits 2 and 8 refine a blocking tile.** 4,980 of bit 2's 5,216 tiles also
// carry bit 0, and so do 429 of bit 8's 465, so a caller testing one of them is
// mostly asking what kind of wall it has run into rather than whether it has.
#define LEVEL_ATTR_BIT2 0x0004
#define LEVEL_ATTR_BIT8 0x0100

// **Bit 8 on a blocking tile is water**, and the ROM says so in one comparison.
// When `$80:E4C1`'s walk is refused, `$80:DEDE` takes the attribute word
// `terrain_blocked` handed back, undoes that routine's `LSR` with an `ASL`, and
// asks
//
//     $80:DEE0  ASL A : AND #$8B38 : CMP #$0100 : BNE away
//
// — so the tile that stopped the walk has to carry bit 8 and *none* of bits 3,
// 4, 5, 9, 11 or 15. Bit 0 is already known set, and the `ASL` drops it, which
// is why the mask below carries it and the ROM's does not. Match, and the
// player stops walking and starts across: `$80:DEFF` re-reads a point one step
// ahead with `terrain_point_bit8` and `$80:DF05  JMP $DD0D` hands him to a
// scripted behaviour with its own byte script at `$80:DD41`.
//
// Measured on `movies/level1-keys.zmv`, which enters level 1's lake at x=1214:
// he walks north at two pixels a frame to y=687, stops dead for sixteen frames,
// and then travels **eight pixels every nine frames** to y=657 — the swimming
// sprite. `zamn_headless --at 3975` is the picture.
//
// That movie releases `Up` at frame 3960 and so ends there, which is why the
// round that found this bit first read y=657 as a destination the ROM had
// picked. It is not one. **Held down, he swims the pond and gets out the far
// side** — the thirty-two pixels to 657 are the entry, after which he is under
// his own input again: three pixels every two frames to y=616, a thirteen-frame
// stall at the far shore, one pixel every two while climbing out to y=601, and
// then walking at two again. Water is passable, and `--swim` in `zamn_assets
// route` is the search that knows it. What none of those speeds is, is two, so
// a wet route's *frame* counts are worth nothing until swimming is ported.
//
// **Thirty-eight tile indices in the cartridge carry bit 8 at all**, across the
// five attribute tables the 56 levels share between them, and they are two
// different things. Thirty-one of the thirty-two that also block are water by
// the test above — `$0101`, `$0103` and `$0107`, in the three tables that 43
// levels use. The other seven are all in one table, the one levels 4, 11, 14,
// 25, 26 and 35 read: six `$0108` and one `$010B`, which carry bit 3 as well
// and are the up-conveyor `port/floor.h` already documents. The `CMP` above
// refuses them by that bit, so those six levels have no water. Nor do the
// seven whose table has no bit-8 tile at all: 18, 31, 37, 50, 53, 54 and 56.
#define LEVEL_ATTR_WATER_MASK 0x8b39
#define LEVEL_ATTR_WATER 0x0101

// **Bit 3 is not terrain at all, it is a trigger.** Only 223 of its 410 tiles
// block anything, and its reader — `$80:B03B`, called from `$80:E861` on the
// tile a mover has just stepped onto — arms a ten-frame countdown ending in a
// hundred-pixel jump. `partner_near` in `port/step.h` is the other end of it.
#define LEVEL_ATTR_BIT3 0x0008

// **Bit 12 is the opposite of bits 2 and 8.** 2,050 of its 2,098 tiles carry
// neither blocking bit, and the routine that reads it — `$80:AF66` — is the
// only terrain test in the game whose answer is inverted: it refuses a position
// unless *all six* tiles of the footprint carry the bit. That is a surface
// something is confined to rather than kept off, and `$82:A088` is a step
// validator built around it the way `$80:E4C1` is built around bit 0.
#define LEVEL_ATTR_BIT12 0x1000

// **Bit 6 is a door**, and it is the one bit here that a route has to know
// about, because a tile carrying it blocks and is passable anyway.
//
// `$80:B0BB` is the reader, and it does not ask "is this tile a door" so much as
// *which way is one*. It takes a point in X/Y and reads four tiles around it
// through `tile_attrs_at_pixel`, in a fixed order, masking each with `#$0040`:
//
//     $80:B0D2  (x, y-$10)   up      $3C = 1
//     $80:B0E9  (x+$10, y)   right   $3C = 3
//     $80:B0FD  (x, y+1)     down    $3C = 5
//     $80:B112  (x-$10, y)   left    $3C = 7
//
// The first that matches leaves its code in `$3C`, and `$80:B11F  LDA $3C : PLD
// : RTL` hands it back. Two routines call it — `$81:9243` and `$81:92B2` — and
// nothing else in the cartridge reads bit 6 at all.
//
// 122 tiles carry it across the five attribute tables and 115 of those block, so
// it is a small deliberate set rather than a bit that fell out of the artwork.
// Level 23 is the level that makes the point: its spawn at (541,1095) is sealed
// into a grass yard by `$0053` tiles — bit 0 and bit 6 — with two `$08` objects
// in the yard with it, and `$80:CA30` says type `$08` is collision id `$21`,
// which is keys. Walk into the door holding one and `$7E:1D0C` goes 2 -> 1 and
// the wall is not there any more.
//
// **What opens is one 8x8-tile block**, and that is measured rather than read.
// Record 23's first doorway spans tile columns 54..58; walk through it at x=461
// and then push west, and the player moves four pixels and stops at x=457 —
// column 55, which is the last column of the block *next* to the one that
// opened. Columns 56..63 are gone and 48..55 are not, and 55/56 is a block
// boundary. So the unit is the block, and `route_doorable` marks whole blocks.
//
// `$81:92D6` is a routine that performs exactly that edit — `LSR` six times on
// each axis to get block coordinates, `$80:ACF6` for the block-map address, then
// `LDA [$28],Y : EOR #$0001`, so the open block is the closed one with bit 0 of
// its index flipped — and it is reached from both `$80:B0BB` callers through
// `$81:92C2`. **It is not the path the player takes**, which is worth saying
// because the shape is so persuasive: it also does `INC $1FC2` and spawns a
// thread bodied at `$81:990B`, and on a movie that opens two of record 23's
// doors `$1FC2` never leaves zero and no `$81:990B` ever appears in the display
// list. Something else edits the same block map the same way for the player.
// Finding it would settle where the key is actually spent — and `$81:92D6`'s
// own path is worth chasing for a different reason, since a door that spawns an
// `$81:990B` is the creature `$81:983A` is otherwise the only source of.
//
// So a door is scenery that a key spends, and a search that treats it as a wall
// calls half this cartridge impossible. `zamn_assets route --doors` is the grid
// that knows, and it is a separate predicate from `route_open` for the same
// reason `--swim` is: the legs are real, and they cost something the plan cannot
// see.
#define LEVEL_ATTR_DOOR 0x0040

// The remaining eight bits are still unidentified. All of them are set on
// somewhere between 55 and 1,429 tiles, so none of them is dead.

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
  // streamed to the PPU (`$80:A47B`), so it is not part of the expanded map.
  //
  // **It is not only a draw-time flag, which this comment used to say it was.**
  // `$80:86F9` copies it to `W_TILE_PRIORITY_BELOW`, and `$82:90F7` reads it as
  // a *collision* threshold: a tile whose index is below it is refused before
  // the attribute word is even fetched. So the tiles the game draws in front of
  // the player are exactly the tiles it will not let something stand on, and
  // one field does both jobs — which is also why the two uses agree about which
  // direction the comparison goes.
  uint16_t priority_below;

  uint16_t unknown_28;  // +$28  passed to $80:9F29 as X

  // +$2A..$30  the two players' start positions. The camera starts centred
  // between them: x = (start_x1 + start_x2) / 2 - $80.
  uint16_t start_x1, start_y1, start_x2, start_y2;

  // +$32/$34  what the level sounds like. `$82:AC56` reads both and passes
  // them to `$80:CBD9`: `song` selects an APU data set directly (2..11), and
  // `sample_set` is an index 0..3 that the ROM adds MUSIC_SET_SAMPLES to. See
  // `src/assets/music.h`.
  uint16_t song, sample_set;
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
