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
  // Overflow, from the last `ADC`: the tilemap address on the first five
  // probes, `$B2 + 4` on the sixth. It is not a constant. A tilemap row
  // based under `$8000` whose tile lies past it sets it, which happens on
  // the big maps of levels 19 and 25.
  bool v;
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

// --- What ten unrolled probes cost ------------------------------------------
//
// **The loop being written out ten times is what makes this priceable, and it
// is also the only reason it needs a table at all.** A rolled loop would have
// one body and one counter; this has ten bodies that agree about the two tests
// and disagree about how each one reaches the map. So the blocks split the way
// the ROM does: five ways of loading a probe, then the two tests, which are
// byte for byte the same all ten times.
//
// The five loads are the whole of the difference between the probes:
//
//     probe 0      `LDA [$28]`                        -- no index at all
//     probes 1-4   `LDY #$0002` .. `LDY #$0008`       -- an immediate
//     probe 5      `LDY $B2`                          -- the row stride
//     probe 6      `LDY $B2 : INY : INY`              -- ...and two increments
//     probes 7-9   `LDA $B2 : CLC : ADC #imm : TAY`   -- ...and an addition
//
// Nine of those cost more than the one before, and the sequence 52, 70, 80,
// 104, 122 is the price of the assembler never being asked to add a constant
// to a direct-page word twice the same way.
//
// Two exits and three routes to them. `SEC : PLD : RTS` is the priority
// rejection and `PLD : RTS` is everything else -- an attribute rejection,
// whose carry the second `LSR` already set, and the clear fall-off, whose
// carry the same `LSR` already cleared. **The tenth probe has no `BCS` after
// it**, because the instruction it would branch to is the one underneath it,
// so `WIDE_BLK_ATTR_LAST` is the ninth probe's block less a branch.
typedef enum {
  WIDE_BLK_PROLOGUE,      // $82:90F7-$82:911D: both shifts and the pointer
  WIDE_BLK_LOAD_FIRST,    // probe 0, the one that does not index
  WIDE_BLK_LOAD_IMM,      // probes 1-4
  WIDE_BLK_LOAD_ROW,      // probe 5
  WIDE_BLK_LOAD_ROW_INC,  // probe 6
  WIDE_BLK_LOAD_ROW_ADD,  // probes 7-9
  WIDE_BLK_PRIO_PASS,     // `AND : CMP $00DC : BCC` not taken
  WIDE_BLK_PRIO_FAIL,     // ...or taken, which is the tile number alone
  WIDE_BLK_ATTR_PASS,     // `ASL : TAY : LDA [$BA],Y : LSR : LSR : BCS` clear
  WIDE_BLK_ATTR_FAIL,     // ...or set
  WIDE_BLK_ATTR_LAST,     // the same without the branch: probe 9 only
  WIDE_BLK_ROW_BRA,       // $82:9185 BRA, stepping over row one's two exits
  WIDE_BLK_EXIT_SEC,      // $82:9189 / $82:9201, the priority rejection
  WIDE_BLK_EXIT_PLD,      // $82:9187 / $82:91FF, the other two
  WIDE_BLOCK_COUNT,
} TerrainWideBlock;

typedef struct {
  uint16_t blocks[WIDE_BLOCK_COUNT];
} TerrainWideWork;

// **This one adds to `work` instead of clearing it**, which is the opposite of
// every other `_counted` in this port. It is a leaf, and its caller worth
// pricing -- `$82:8F93 boss_step` -- calls it two, three or four times in one
// step and owes the sum. A caller that wants one call's figure clears the
// struct itself; `shim_terrain_blocked_wide` does exactly that.
//
// Every call adds, and the sums check:
//
//     PROLOGUE == EXIT_SEC + EXIT_PLD == calls
//     EXIT_SEC == PRIO_FAIL
//     PRIO_PASS + PRIO_FAIL == the probes reached
//     ATTR_PASS + ATTR_FAIL + ATTR_LAST == PRIO_PASS
//     LOAD_FIRST == PROLOGUE, and no LOAD_* may exceed its share of the ten
void terrain_blocked_wide_counted(Wram* w, uint16_t x, uint16_t y,
                                  TerrainRegs* out, TerrainWideWork* work);

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

// --- The rest of the attribute word ----------------------------------------
//
// Four more routines, out of the five that fill the 359 bytes between
// `terrain_blocked_enemy` and `actor_gap` (the fifth, `$80:AFFB`, is not about
// terrain at all and is in `port/step.h` with the other one like it). These
// four are the same question about four more bits:
//
//   $80:AF2C  terrain_point_bit2       0.17%  one tile, bit 2, bounds first
//   $80:AF66  terrain_footprint_bit12    --   the 3x2 footprint, bit 12, all six
//   $80:B03B  terrain_tile_bit3          --   one tile from tile coordinates, bit 3
//   $80:B05F  terrain_point_bit8       0.05%  one tile, bit 8
//
// `level.h` has said since Phase 2 that the rest of the attribute word is
// unidentified, and has named two more masks since: this is where the other two
// come from, and what four of the sixteen bits are worth measuring.
//
// ## What the four bits cost the level designer
//
// Counted straight out of the ROM, over all 55 levels' attribute tables (which
// are five distinct tilesets shared between them, so a tile is counted once per
// level that uses it — the same denominator the bit 0/bit 1 table in
// `docs/cosim.md` uses):
//
// | bit | mask | tiles | of 28,160 | also bit 0 | also bit 1 | neither |
// | --- | --- | --- | --- | --- | --- | --- |
// | 0 | `$0001` | 15,166 | 53.9% | — | 14,950 | — |
// | 1 | `$0002` | 15,469 | 54.9% | 14,950 | — | — |
// | **2** | `$0004` | **5,216** | 18.5% | 4,980 | 4,989 | **221** |
// | **3** | `$0008` | **410** | 1.5% | 223 | 217 | **187** |
// | **8** | `$0100` | **465** | 1.7% | 429 | 393 | **36** |
// | **12** | `$1000` | **2,098** | 7.5% | 48 | 39 | **2,050** |
//
// Three shapes fall out of that, and they match what the routines do with the
// answer.
//
// **Bits 2 and 8 mark terrain that is already blocking.** 95% of bit 2 and 92%
// of bit 8 also carry bit 0 — so a caller testing one of them is refining a
// *no* it would have got anyway, asking not "may I stand here" but "what kind
// of wall is this".
//
// **Bit 12 is the opposite.** 2,050 of its 2,098 tiles carry neither blocking
// bit, so it marks terrain that everything can walk on and that is nonetheless
// worth a mask of its own. `$80:AF66` is the routine that reads it, and it is
// the only test in this file whose answer is inverted: carry *clear* means all
// six probes carried the bit. That is a mover confined to a surface rather than
// kept off one, and `$82:A088` — a step validator built to the same plan as
// `$80:E4C1`, proposal and two axes and all — puts its candidate through this
// and `terrain_out_of_bounds` and nothing else.
//
// **Bit 3 is neither.** Its 410 tiles are barely correlated with blocking at
// all (54%), and its reader is not a movement test: `$80:E861` asks about the
// tile a mover has *already* stepped onto, and a set bit arms a state change.
// The bit says something happens here, not that something may or may not pass.
//
// All four bits appear in every one of the five tilesets (bit 12 in four of
// them, bit 8 in four), in tens rather than hundreds of tiles. Whatever they
// are, they are not a property of one level's theme.
#define TERRAIN_MASK_BIT2 0x0004
#define TERRAIN_MASK_BIT3 0x0008
#define TERRAIN_MASK_BIT8 0x0100
#define TERRAIN_MASK_BIT12 0x1000

#define TERRAIN_POINT_BIT2_ENTRY 0x80af2cu
#define TERRAIN_FOOTPRINT_BIT12_ENTRY 0x80af66u
#define TERRAIN_TILE_BIT3_ENTRY 0x80b03bu
#define TERRAIN_POINT_BIT8_ENTRY 0x80b05fu
//
// ## Three of them are `tile_attrs_at_pixel` written out longhand
//
// `$80:AF2C` and `$80:B05F` shift the point by two, mask the low bit off, add
// `W_TILE_ROW_BASE`, build the pointer in `$28`, take ten bits of the tilemap
// entry and index `[$BA]` — which is `tile_attrs_at_pixel` to the byte, and
// `$80:ADC8` is 356 bytes back up the same bank. `$80:B03B` gets halfway there:
// it calls `tilemap_tile_addr` like `$80:ADC8` does, and then inlines the rest.
//
// **They are not quite interchangeable, though**, and the difference is not in
// the answer. `tile_attrs_at_pixel` goes through `PEA $007F : PLB` and leaves no
// trace; these three write `$28`/`$2A` and leave the pointer behind, so a port
// that called the tidy leaf instead would agree about the attribute word and
// differ about four bytes of direct page. Two of them also skip the origin bias
// the footprint tests apply, so they and `terrain_blocked` do not agree about
// which tile a pixel is in — see the note under `$80:ADC8` above, which is the
// same distinction for the same reason.
//
// ## `BIT` keeps the word; `AND` does not
//
// `$80:AF2C` tests with `BIT #$0004`, which does not disturb A, so it returns
// the **whole attribute word** and a caller could pick more bits out of it. The
// other three test with `AND`, so A comes back as the mask or as zero and
// carries nothing the carry flag did not already say. Nobody uses either fact;
// the port reproduces both because a shim that publishes A has to.
//
// All four open `PHD` and close `PLD : RTL`, so N and Z are the caller's direct
// page — the trap `actor_nearest` cost four movies to learn, and by now the
// default assumption in this file.

// `$80:AF2C`. **Carry set means blocked**, and there are two ways to be: the
// point is off the map, or its tile carries bit 2. The bounds test comes first
// and is a real `JSL $80B422`, so on that exit A is whatever
// `terrain_out_of_bounds` left in it and X and Y are still the caller's — the
// routine has not reached its own `PHA` yet.
//
// Nineteen call sites, all of them in banks $81, $82 and $83, which is to say
// all of them actor bodies. 18,497 calls over the eleven profile movies, and it
// is the only one of the four that is hot.
void terrain_point_bit2(Wram* w, uint16_t x, uint16_t y, TerrainRegs* out);

// `$80:AF66`. **Carry clear means all six tiles carry bit 12** — the inverted
// one. The footprint is `terrain_blocked`'s exactly: the same `(9, 8)` origin,
// the same three-across two-down box, the same six offsets in the same order.
// A comes back as `$1000` on the clear path and `$0000` on the blocked one,
// because `AND` is the test.
//
// Six call sites: `$81:A832`, three in `$82:A088`'s two-axis step validator,
// and `$82:E963` / `$82:EC1C`. Twenty-six calls in the whole profile corpus,
// all of them on `level49-corner`.
void terrain_footprint_bit12(Wram* w, uint16_t x, uint16_t y, TerrainRegs* out);

// `$80:B03B`. **Carry set means the tile carries bit 3.** The only one of the
// four whose caller has already done the pixel-to-tile division: `$80:E861`
// passes `$30 >> 3` and `$32 >> 3`, and `$80:F41A` passes tile coordinates it
// built out of a table. X comes back **doubled**, because `tilemap_tile_addr`'s
// `PLX` hands back the shifted column rather than restoring the argument.
//
// `$80:E861` runs on the tile a mover has just stepped onto, and a set bit 3
// arms a ten-frame countdown that ends in a hundred-pixel jump — see
// `partner_near` in `port/step.h`, which is the other end of it. Three movies
// in the corpus reach this: `level5`, `level21` and `level21-spin`, 426 calls
// between them, of which three get as far as the jump.
void terrain_tile_bit3(Wram* w, uint16_t col, uint16_t row, TerrainRegs* out);

// `$80:B05F`. **Carry set means the tile carries bit 8.** Four call sites, all
// in bank $80's movement code, and one of them — `$80:E543` — is a step
// validator that treats carry *set* as permission to move and takes a different
// path entirely when the bit is absent. So bit 8, like bit 12, is a surface
// something is confined to; unlike bit 12 it is terrain that blocks everyone
// else, which makes it the more interesting of the two and the one this port
// can say least about.
void terrain_point_bit8(Wram* w, uint16_t x, uint16_t y, TerrainRegs* out);

#endif  // PORT_TERRAIN_H
