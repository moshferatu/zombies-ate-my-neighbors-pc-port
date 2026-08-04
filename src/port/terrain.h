// Terrain collision: can something stand here?
//
// Three routines, all reached from the same place and all asking a question
// about a point rather than about an actor:
//
//   $80:AE14  terrain_blocked        the 3x2 tile footprint, attribute bit 0
//   $80:AE97  terrain_blocked_enemy  ...the same footprint, attribute bit 1
//   $80:B422  terrain_out_of_bounds  is the point off the edge of the level
//
// ...and, below, the three leaves the rest of the ROM reaches this table
// through:
//
//   $80:AD1C  tilemap_tile_addr      column and row to a tilemap address
//   $80:ADC8  tile_attrs_at_pixel    0.4%  one point's attribute word
//   $80:ADF3  tile_attrs_at_tile     --    ...for a caller that has tiles
//
// The first and third are two of the four tests `$80:E4C1` puts a proposed step
// through -- `actor_obstacle_at_point` in `port/oam.h` is another -- and the
// second is the same footprint test with a different mask, called only by enemy
// bodies. All three answer in the carry, set meaning **no**.
//
// ## What a footprint test actually reads
//
// The game never looks at the block map at load time or the level record at
// run time. It expands the level into a 16-bit tilemap in WRAM bank `$7F`
// (`src/assets/level.h` reproduces that expansion byte for byte) and from then
// on collision is two indirections:
//
//   1. the tilemap entry for a tile, masked to ten bits, is a BG tile number;
//   2. that number indexes a **512-word attribute table** at `[$BA]`, and the
//      low bits of the word are what blocks movement.
//
// Neither pointer is a constant. `W_TILE_ROW_BASE` holds one word per tile row
// -- row times `W_TILEMAP_ROW_BYTES` -- so a row lookup is a table read rather
// than a multiply, and `W_TILE_ATTRS` is a 24-bit pointer the level loader
// fills in. On `movies/level1.zmv` it holds `$7E:611A`, `W_TILEMAP_ROW_BYTES`
// is 352 and `W_TILEMAP_ROWS` is 104, which is level 1's 22x13 blocks expanded
// to 176x104 tiles at two bytes each.
//
// Port code: libc only.

#ifndef PORT_TERRAIN_H
#define PORT_TERRAIN_H

#include <stdbool.h>
#include <stdint.h>

#include "port/wram.h"

// --- The footprint ----------------------------------------------------------

// A point is turned into a tile by subtracting the actor's origin from it and
// shifting: `TXA : SEC : SBC #$0009` and `TYA : SEC : SBC #$0008`. Nine and
// eight, not eight and eight, and the routine does not say why.
#define TERRAIN_ORIGIN_X 0x0009
#define TERRAIN_ORIGIN_Y 0x0008

// `LSR A : LSR A : AND #$FFFE` on both axes. Dividing by four and then clearing
// the low bit is dividing by eight and doubling, which is what a table of
// 16-bit entries wants -- so the same two instructions produce a byte offset
// along a row and a word index into the row table.
#define TERRAIN_TILE_SHIFT 2
#define TERRAIN_TILE_MASK 0xfffeu

// Six probes: three tiles across, two rows down, tested left to right and then
// top to bottom. That is a 24x16 pixel box, which is wider than it is tall.
#define TERRAIN_PROBE_COLS 3
#define TERRAIN_PROBE_ROWS 2
#define TERRAIN_PROBE_COUNT (TERRAIN_PROBE_COLS * TERRAIN_PROBE_ROWS)

// `AND #$03FF` -- ten bits of tile number, and the top six of a tilemap entry
// are the PPU's own palette/priority/flip bits, which collision ignores.
#define TILEMAP_INDEX_MASK 0x03ffu

// The two masks, and they are **not** a hierarchy. Across all 55 levels 216
// tiles carry bit 0 without bit 1 and 519 carry bit 1 without bit 0, so these
// are two independent classes of blocking terrain rather than a strict and a
// loose version of one. See `LEVEL_ATTR_SOLID` in `src/assets/level.h`.
#define TERRAIN_MASK_SOLID 0x0001
#define TERRAIN_MASK_ENEMY 0x0002

// The long pointer both routines build in direct page zero on the way in, and
// leave behind: `STA $28` then `LDA #$007F : STA $2A`. Two 16-bit stores, so
// four bytes are written and `$2B` is zeroed along with the bank byte.
#define TERRAIN_DP_MAP 0x28       // offset into the expanded tilemap
#define TERRAIN_DP_MAP_BANK 0x2a  // ...and $007F, stored as a word
#define TERRAIN_MAP_BANK 0x007fu

// What the ROM leaves in A, X and Y. As everywhere else in this port none of it
// is tidy, because the routine falls out of whichever probe decided and never
// tidies up after itself.
typedef struct {
  uint16_t a;  // the attribute word -- shifted right one for `terrain_blocked`
  uint16_t x;  // the row index, from the `TAX` on the way in
  uint16_t y;  // the doubled tile number of the probe that decided
  bool blocked;
} TerrainRegs;

// `$80:AE14`. Carry set means at least one of the six tiles has attribute bit 0.
void terrain_blocked(Wram* w, uint16_t x, uint16_t y, TerrainRegs* out);

// `$80:AE97`. The same, for bit 1. Its three callers -- `$81:80CB`, `$81:85D7`
// and `$81:8618` -- are all enemy bodies, and the last two are the same
// routines that call `actor_nearest` and `actor_at_point` twelve and fifteen
// bytes further on. That is where the name comes from; the bit's meaning to the
// game's designers is not recorded anywhere this port can read.
void terrain_blocked_enemy(Wram* w, uint16_t x, uint16_t y, TerrainRegs* out);

// --- $82:90F7 ---------------------------------------------------------------

// **The same test again, wider, with a second rule.** Everything above is here:
// the same two coordinates, the same shift, the same row table, the same
// pointer built in `$28`, the same attribute table, and the same bit 1. Four
// things differ, and the last of them is the interesting one.
//
//   1. **Ten probes, not six** -- five tiles across instead of three, so a
//      40x16 box, and the origin moves left to match: `SBC #$0011` where the
//      others use `#$0009`. The two rows are still one `W_TILEMAP_ROW_BYTES`
//      apart.
//   2. **The loop is unrolled**, all ten of it, which is why this routine is
//      406 bytes for what `$80:AE14` says in 130 and why its profile is flat:
//      every byte executes exactly once per call.
//   3. `AND #$01FF`, **nine bits of tile number** rather than ten. There are
//      512 BG tiles and 512 attribute words, so this is the mask that matches
//      the data; `TILEMAP_INDEX_MASK`'s tenth bit is the odd one out.
//   4. **A tile can block on its number alone.** Before the attribute word is
//      even fetched, `CMP $00DC : BCC` refuses any tile whose index is below
//      `W_TILE_PRIORITY_BELOW` -- and that is the level record's `+$26`, which
//      `src/assets/level.h` has been describing since Phase 2 as a *draw-time*
//      flag: tiles below it get BG priority forced on as the camera streams
//      them. So the tiles the game draws **in front of** the player are exactly
//      the tiles this routine will not let something stand on, and one field
//      does both jobs.
#define TERRAIN_WIDE_ENTRY 0x8290f7u

#define TERRAIN_WIDE_ORIGIN_X 0x0011
#define TERRAIN_WIDE_ORIGIN_Y 0x0008
#define TERRAIN_WIDE_COLS 5
#define TERRAIN_WIDE_ROWS 2
#define TERRAIN_WIDE_PROBE_COUNT (TERRAIN_WIDE_COLS * TERRAIN_WIDE_ROWS)

// `AND #$01FF`, and see (3) above.
#define TILEMAP_INDEX_MASK_9 0x01ffu

// Carry set means blocked, the same convention as everything else here: one of
// the ten tiles is below the priority threshold, or one of them carries bit 1.
// Carry clear needs all ten to pass both tests, and is the only exit the ROM
// reaches by falling off the end of the last `LSR A`.
//
// `out->y` is worth one caution. Nine of the ten probes load through `Y`, so a
// rejection leaves the probe's own offset there -- but **the first probe does
// not**: `$82:911F  LDA [$28]` has no index, so a tile rejected by the very
// first test returns with `Y` still holding the caller's own argument.
void terrain_blocked_wide(Wram* w, uint16_t x, uint16_t y, TerrainRegs* out);

// --- $80:B422 ---------------------------------------------------------------

// **Is the point off the edge of the level?** Carry set means yes.
//
// Four tests, and each rejects on its own: a negative coordinate on either
// axis, a point too close to the left or top edge, or one past the right or
// bottom. The near edges are constants -- `x >> 2 < 4` and `y >> 3 < 2` -- and
// the far ones are compared against the same two scalars the footprint test
// uses for its strides, which is what makes them the level's extents rather
// than the screen's.
//
// It is the only one of the three with **no `PHD`**, and that changes what a
// shim has to do. There is no `PLD` to take N and Z from, so both come from
// whichever comparison the routine happened to stop at -- six exits, six
// different answers -- and getting them from the last instruction of the
// routine rather than the last one executed would be wrong on five of the six.
#define TERRAIN_BOUNDS_MIN_X 0x0004  // in x >> 2 units
#define TERRAIN_BOUNDS_MIN_Y 0x0002  // in y >> 3 units
#define TERRAIN_BOUNDS_PAD_X 3       // `INC A` three times before the compare
#define TERRAIN_BOUNDS_PAD_Y 2

// N, Z and C all have to be published here, because none of them is a constant
// and none survives from anywhere but the exit that produced it.
typedef struct {
  uint16_t a;
  bool n, z, c;  // c set means the point is outside
} BoundsRegs;

void terrain_out_of_bounds(Wram* w, uint16_t x, uint16_t y, BoundsRegs* out);

// --- $80:AD1C  tilemap_tile_addr — X = column, Y = row; A = the address ------
//
// Fifteen bytes, no calls, and the third routine in this file to reach for
// `W_TILE_ROW_BASE`. Everything above turns a *pixel* into a tile and then
// looks the row up; this is the lookup on its own, for callers that already
// have tile coordinates:
//
//   TXA : ASL A : PHA          ; column x 2
//   TYA : ASL A : TAX
//   LDA $7E4328,X              ; the row's byte offset, straight out of the table
//   CLC : ADC $01,S            ; ...plus the column
//   PLX : RTL
//
// It lives here rather than in a file of its own because the row table is the
// thing it knows about, and this header is where that table is explained.
//
// **X comes back doubled, and that is not a restore.** `PHA` saves the column
// *already shifted*, and `PLX` puts that back — so a caller passing column 5
// gets X = 10 on the way out. `$80:A5E5` relies on it. And because `PLX` is the
// last flag-setting instruction, N and Z describe that doubled column rather
// than the address in A; carry is the `ADC`'s, and survives the `PLX`
// untouched. Three registers, three different sources, no two of them obvious.
#define TILEMAP_TILE_ADDR_ENTRY 0x80ad1cu

typedef struct {
  uint16_t a, x;
  bool n, z, c;
} TilemapAddrRegs;

void tilemap_tile_addr(const Wram* w, uint16_t x, uint16_t y,
                       TilemapAddrRegs* out);

// --- $80:ADC8 / $80:ADF3  one point's attribute word ------------------------
//
// The other leaf under this table, and the one the rest of the cartridge
// actually uses. `terrain_blocked` and its two neighbours read six probes and
// answer a yes/no; **these two read one tile and hand back the raw attribute
// word**, and 22 call sites across four banks pick their own bits out of it.
// 32,169 calls over the profile corpus, 31 instructions each, every byte
// exactly once -- the arithmetic above already covers all of it:
//
//   TXA : LSR A x3 : TAX       ; pixel to tile, both axes
//   TYA : LSR A x3 : TAY
//   PEA $007F : PLB
//   JSL $80AD1C : TAX          ; the row table, again
//   LDA $0000,X                ; the tilemap entry, in bank $7F
//   AND #$03FF : ASL A : TAY   ; ten bits, doubled
//   LDA [$BA],Y                ; the attribute table, wherever the loader put it
//
// `$80:ADF3` is the same twenty-one instructions **without the six `LSR`s**,
// for the four call sites that already hold tile coordinates. It is 76 calls to
// `$80:ADC8`'s 32,169 and it is here because it is free, not because it is hot.
//
// ## The shift is not the one the footprint tests use
//
// `TERRAIN_TILE_SHIFT` is 2, because `terrain_blocked` wants a *byte offset*
// into a row of 16-bit entries and gets it by shifting twice and clearing the
// low bit. These two want a tile *number*, because `tilemap_tile_addr` does the
// doubling itself -- so they shift three times and mask nothing. Same
// conversion, two representations, and mixing them up costs a factor of two in
// one direction and an off-by-one tile in the other.
//
// Note also that there is **no `TERRAIN_ORIGIN_X`/`_Y` subtraction here**. The
// footprint tests bias the point by (9, 8) before dividing; these do not touch
// it. So the two families do not agree about which tile a pixel is in, and that
// is the ROM's arrangement rather than an oversight in the port: a caller
// asking "what am I standing on" and a caller asking "can this actor fit"
// are asking about different rectangles.
//
// ## `PEA $007F : PLB` leaves a byte behind
//
// `PLB` pulls one byte and `PEA` pushed two, so the high `$00` stays on the
// stack until the `PLB` at `$80:ADEE` takes it -- which sets the data bank to
// zero for four instructions that do not use it, and only the *second* `PLB`
// restores the caller's. The stack balances, and the routine is nine bytes deep
// of its own before `tilemap_tile_addr`'s `JSL` and `PHA` go under it.
//
// That second `PLB` is also the last flag-setting instruction, so **N and Z
// describe the caller's data bank byte** and have nothing to do with the
// attribute word in A. `$80:8480` is the other routine in this registry that
// ends that way, and `CosimRegs::db` exists for the pair of them.
#define TILE_ATTRS_AT_PIXEL_ENTRY 0x80adc8u
#define TILE_ATTRS_AT_TILE_ENTRY 0x80adf3u

// `LSR A` three times, on both axes. Eight pixels to the tile.
#define TILE_ATTRS_PIXEL_SHIFT 3

// A is the attribute word. X and Y are the caller's own, put back by `PLX` and
// `PLY` -- the shifted copies never leave the routine. Carry is the `ASL`'s,
// and it is always clear, because `AND #$03FF` has already taken bit 15 out.
typedef struct {
  uint16_t a;
  bool c;
} TileAttrsRegs;

void tile_attrs_at_tile(const Wram* w, uint16_t col, uint16_t row,
                        TileAttrsRegs* out);
void tile_attrs_at_pixel(const Wram* w, uint16_t x, uint16_t y,
                         TileAttrsRegs* out);

#endif  // PORT_TERRAIN_H
