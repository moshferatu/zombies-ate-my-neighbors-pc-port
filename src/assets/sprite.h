// ZAMN's sprite graphics — how an actor turns into OAM entries.
//
// Everything the game draws as a sprite is built from one fixed unit: a
// **16x16 frame**, 128 bytes of 4bpp planar tiles, living in a flat array that
// starts at `$84:8000`. Frames are numbered 0..4095 and frame `n` is simply
// `$84:8000 + n * 128` — the base is set once at boot by `$80:C05A`
// (`$80:85D3` and `$80:8632` both pass A=`$8000`, Y=`$0084`).
//
// An actor does not name a frame directly. It points at a **metasprite**: a
// count byte followed by that many 8-byte pieces, each of which places one
// 16x16 frame at a signed offset with its own OAM attributes. Metasprites live
// in banks `$8F` and `$90` — the drawing pass rejects any pointer outside them
// (`$80:BD97`).
//
// The renderer at `$80:BD1F` walks the visible-actor list and, for each one,
// calls one of four near-identical emitters chosen by the actor's flag bits 1-2:
//
//   flags & 6 == 0 -> `$80:BA51`  no flip
//   flags & 6 == 2 -> `$80:BABA`  horizontal
//   flags & 6 == 4 -> `$80:BB30`  vertical
//   flags & 6 == 6 -> `$80:BBA6`  both
//
// `sprite_emit()` is a port of all four. They differ only in how a piece offset
// is negated and which OAM flip bits get toggled, so they are one function here.
//
// The one thing that is *not* a static format is which VRAM tile a frame is
// currently loaded at: the game keeps a 128-slot LRU cache of 16x16 frames in
// VRAM and uploads on demand (`$80:B9D6`, drained by `$80:B947`). That cache is
// runtime state, so `sprite_emit()` takes a callback for it. The slot geometry
// itself is static and is exposed here.
//
// Port code: libc only.

#ifndef ASSETS_SPRITE_H
#define ASSETS_SPRITE_H

#include <stdbool.h>
#include <stdint.h>

#include "assets/rom.h"

// ---------------------------------------------------------------------------
// Frames — the 16x16 graphics unit
// ---------------------------------------------------------------------------

// Base of the frame array, as `$80:C05A` is called with it.
#define SPRITE_FRAME_BASE 0x848000u

#define SPRITE_W 16
#define SPRITE_H 16
#define SPRITE_FRAME_TILES 4
#define SPRITE_FRAME_BYTES 128
#define SPRITE_FRAME_PIXELS (SPRITE_W * SPRITE_H)

// `$80:C05A` clears a frame->slot map of `$2000` bytes, one word per frame, so
// the frame number is 12 bits.
#define SPRITE_FRAME_COUNT 4096

// ROM address of frame `n`'s 128 bytes. Frames tile the ROM from `$84:8000`
// upwards; 256 of them fill one LoROM bank exactly, which is why `$80:BA29`
// can build the address by putting `n >> 8` in the bank and `(n & $FF) * 128`
// in the offset.
uint32_t sprite_frame_addr(uint16_t frame);

// Copy frame `n`'s raw 128 bytes. False if they are not readable ROM.
bool sprite_frame_read(const Rom* rom, uint16_t frame,
                       uint8_t out[SPRITE_FRAME_BYTES]);

// Decode a frame into 256 palette indices, row-major, 16 per row.
//
// The 128 bytes are four 4bpp tiles in reading order — top-left, top-right,
// bottom-left, bottom-right. That is exactly how `$80:B960` uploads them: the
// first 64 bytes go to one VRAM row of two tiles and the second 64 to the row
// below (`ORA #$0100` on the VRAM address).
void sprite_frame_pixels(const uint8_t raw[SPRITE_FRAME_BYTES],
                         uint8_t out[SPRITE_FRAME_PIXELS]);

// ---------------------------------------------------------------------------
// The VRAM cache's geometry
// ---------------------------------------------------------------------------

// 128 slots of 16x16 = 512 tiles = the full 16 KB sprite character area, laid
// out 8 frames across (16 tiles) by 16 down.
#define SPRITE_SLOTS 128
#define SPRITE_SLOTS_PER_ROW 8

// OAM tile number for a loaded slot — the table at `$80:B647`, which is
// `(slot / 8) * 32 + (slot % 8) * 2` for every one of the 128 entries.
uint16_t sprite_slot_tile(int slot);

// VRAM word address of a slot — the table at `$80:B547`, likewise exactly
// `(slot / 8) * $200 + (slot % 8) * $20`.
uint16_t sprite_slot_vram(int slot);

// ---------------------------------------------------------------------------
// Metasprites
// ---------------------------------------------------------------------------

// The two banks `$80:BD97` accepts a metasprite pointer in.
#define SPRITE_META_BANK_LO 0x8f
#define SPRITE_META_BANK_HI 0x90

// The count is one byte, and `$80:BDAA` treats 0 as "draw nothing".
#define SPRITE_META_MAX_PIECES 255

#define SPRITE_PIECE_BYTES 8

// One 8-byte piece. `attr` is an OAM attribute word — the high byte is the
// hardware's byte 3 (vflip `$80`, hflip `$40`, priority `$30`, palette `$0E`,
// tile bit 8 `$01`), the low byte is unused by the shipped data.
typedef struct {
  int16_t x, y;    // +0/+2  signed offset from the actor's position
  uint16_t attr;   // +4  OR'd into the OAM word after masking
  uint16_t frame;  // +6  which 16x16 frame to draw
} SpritePiece;

typedef struct {
  uint32_t addr;   // where this was read from
  uint32_t bytes;  // 1 + count * 8
  int count;
  SpritePiece pieces[SPRITE_META_MAX_PIECES];
} SpriteMeta;

typedef enum {
  SPRITE_OK = 0,
  SPRITE_ERR_ADDRESS = -1,  // not readable ROM, or the record runs past the bank
  SPRITE_ERR_BANK = -2,     // not a bank the drawing pass would accept
} SpriteStatus;

// True if `addr` is somewhere `$80:BD1F` would agree to draw from: bank `$8F`
// or `$90`, offset `$8000` or above.
bool sprite_meta_addr_valid(uint32_t addr);

// Read the metasprite at `addr`. A count of 0 is a valid, empty metasprite.
int sprite_meta_read(const Rom* rom, uint32_t addr, SpriteMeta* out);

// ---------------------------------------------------------------------------
// OAM composition
// ---------------------------------------------------------------------------

// The buffer at `$7E:13BE`: 128 four-byte entries then the 32-byte high table,
// DMA'd to OAM whole (544 bytes, `$80:B9BA`).
#define SPRITE_OAM_SPRITES 128
#define SPRITE_OAM_LOW_BYTES (SPRITE_OAM_SPRITES * 4)
#define SPRITE_OAM_HIGH_BYTES (SPRITE_OAM_SPRITES / 4)
#define SPRITE_OAM_BYTES (SPRITE_OAM_LOW_BYTES + SPRITE_OAM_HIGH_BYTES)

typedef struct {
  uint8_t bytes[SPRITE_OAM_BYTES];
  uint16_t index;  // byte offset of the next free entry — the game's `$88`
} SpriteOam;

// Which of the four emitters to run. The values are the actor flag bits
// themselves, so `flags & 6` can be passed straight in.
typedef enum {
  SPRITE_FLIP_NONE = 0,
  SPRITE_FLIP_X = 2,
  SPRITE_FLIP_Y = 4,
  SPRITE_FLIP_XY = 6,
} SpriteFlip;

// Resolve a frame number to the OAM tile number it is currently loaded at.
// This is the VRAM cache lookup (`$80:B9D6`) and therefore runtime state, not
// a ROM format — see the header comment.
typedef uint16_t (*SpriteTileFn)(uint16_t frame, void* ctx);

// Emit one metasprite into `oam`, starting at `oam->index` and advancing it.
//
// A faithful port of `$80:BA51` and its three flipped twins, down to the
// details that are visible in the buffer afterwards:
//
//   * `ox`/`oy` are the game's `$8E`/`$90` — the actor's screen position, with
//     the camera already subtracted.
//   * `attr_and` (`$96`) masks the piece's attribute word and `attr_or`
//     (`$92`) is OR'd on top; together they are how an actor overrides the
//     palette and priority its frames were authored with.
//   * A piece whose screen y lands in `$00E0..$FFF0` or whose x lands in
//     `$0100..$FFF0` is dropped, and dropping it does not consume an OAM slot.
//   * The y store is 16-bit, exactly as the ROM does it, so it also writes the
//     byte above — a skipped piece leaves that byte behind. It is always either
//     overwritten by the next piece or made invisible by the terminator.
//
// What the ROM's emitter leaves in direct page when it returns.
//
// Phase 2 had no use for this — it diffed the OAM buffer, which is the whole
// point of the routine. Phase 3 does: `sprite_build_oam` is checked on all
// 128 KB of WRAM, and the emitter's walk lives in direct-page bytes ($86, $8A,
// $94) that survive the call. Reporting them from the one implementation of the
// walk is the only way the port can reproduce them without writing the walk a
// second time.
typedef struct {
  int walked;      // pieces stepped past — `$86` counts down by this much and
                   // `$8A` advances 8 bytes each. Not the same as the return
                   // value: a piece dropped for being off screen is still
                   // walked past, and the piece that *fills* OAM is not
                   // ($80:BAA9 leaves before the advance).
  uint16_t attr;   // the last emitted piece's attribute word after the AND
  bool attr_valid; // false if no piece was emitted, leaving `$94` alone
} SpriteEmitTrace;

// Returns the number of OAM entries written. Emission stops early if OAM fills
// up (`CPX #$0200`). `trace` may be NULL.
int sprite_emit(SpriteOam* oam, const SpriteMeta* meta, SpriteFlip flip,
                int16_t ox, int16_t oy, uint16_t attr_or, uint16_t attr_and,
                SpriteTileFn tile_of, void* ctx, SpriteEmitTrace* trace);

// Which of the emitter's straight-line runs this call went through, for
// `cosim_cost`. Same division of labour as everywhere else in the port: this
// counts branch outcomes, which is a fact about the metasprite and where it
// landed on screen, and `src/cosim/routines.c` multiplies them by cycles.
//
// The four emitters are one function here and they are one table there too,
// which is worth a sentence because it is not obvious that it can be. They
// differ in exactly four places and every one of them is a fixed insertion:
// `EOR #$FFFF : SEC : SBC #$000F` in front of the y offset for a vertical flip
// and in front of the x offset for a horizontal one, an `EOR #$4000`/`$8000`/
// `$C000` on the finished OAM word for any flip at all, and — because adding
// those pushed the loop-back past a byte's reach — `BEQ : JMP` where the
// unflipped one has a `BNE`. So the model is this table plus three deltas,
// which is the same claim `sprite_emit` makes by existing.
typedef enum {
  EMIT_BLK_PROLOGUE,    // $80:BA51  LDX $88, once a call
  EMIT_BLK_Y_HIGH,      // $80:BA61 BCS taken: y >= $FFF1, hanging off the top
  EMIT_BLK_Y_LOW,       // ...and $80:BA66 not taken: y < $00E0, on screen
  EMIT_BLK_Y_DROP,      // ...or taken: below the display, so skip the piece
  EMIT_BLK_X_NEAR,      // $80:BA77 BCC taken: x < $0100
  EMIT_BLK_X_DROP,      // $80:BA7C BCC taken: off the right, so skip the piece
  EMIT_BLK_X_WRAP,      // ...not taken: off the left, so set its high-table bit
  EMIT_BLK_PIECE,       // $80:BAA9 BEQ not taken: emitted, and OAM has room
  EMIT_BLK_PIECE_FULL,  // ...taken: that piece was the 128th
  EMIT_BLK_NEXT,        // $80:BAB5 BNE taken: another piece to walk
  EMIT_BLK_DONE,        // ...not taken: `$86` reached zero
  EMIT_BLK_EXIT,        // $80:BAB7  STX $88 : RTS
  EMIT_BLOCK_COUNT,
} SpriteEmitBlock;

// `flip` is carried so the harness knows which of the four emitters ran without
// being told twice; the block counts alone do not say.
typedef struct {
  uint16_t blocks[EMIT_BLOCK_COUNT];
  SpriteFlip flip;
} SpriteEmitWork;

int sprite_emit_counted(SpriteOam* oam, const SpriteMeta* meta, SpriteFlip flip,
                        int16_t ox, int16_t oy, uint16_t attr_or,
                        uint16_t attr_and, SpriteTileFn tile_of, void* ctx,
                        SpriteEmitTrace* trace, SpriteEmitWork* work);

// Park the remaining sprites off-screen, as `$80:BDC4` does with `$E000`.
void sprite_oam_terminate(SpriteOam* oam);

#endif
