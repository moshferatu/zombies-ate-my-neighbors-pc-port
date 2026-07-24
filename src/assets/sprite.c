#include "assets/sprite.h"

#include <string.h>

#include "assets/gfx.h"
#include "port/coverage.h"

// ---------------------------------------------------------------------------
// Frames
// ---------------------------------------------------------------------------

uint32_t sprite_frame_addr(uint16_t frame) {
  frame &= SPRITE_FRAME_COUNT - 1;
  uint32_t bank = (SPRITE_FRAME_BASE >> 16) + (frame >> 8);
  uint32_t off = (SPRITE_FRAME_BASE & 0xffff) +
                 (uint32_t)(frame & 0xff) * SPRITE_FRAME_BYTES;
  return (bank << 16) | (off & 0xffff);
}

bool sprite_frame_read(const Rom* rom, uint16_t frame,
                       uint8_t out[SPRITE_FRAME_BYTES]) {
  uint32_t avail = 0;
  const uint8_t* p = rom_ptr(rom, sprite_frame_addr(frame), &avail);
  if (!p || avail < SPRITE_FRAME_BYTES) return false;
  memcpy(out, p, SPRITE_FRAME_BYTES);
  return true;
}

void sprite_frame_pixels(const uint8_t raw[SPRITE_FRAME_BYTES],
                         uint8_t out[SPRITE_FRAME_PIXELS]) {
  for (int tile = 0; tile < SPRITE_FRAME_TILES; tile++) {
    uint8_t px[GFX_TILE_PIXELS];
    gfx_decode_tile(raw + tile * 32, 4, px);
    int ox = (tile & 1) * GFX_TILE_W;
    int oy = (tile >> 1) * GFX_TILE_H;
    for (int y = 0; y < GFX_TILE_H; y++)
      for (int x = 0; x < GFX_TILE_W; x++)
        out[(oy + y) * SPRITE_W + ox + x] = px[y * GFX_TILE_W + x];
  }
}

uint16_t sprite_slot_tile(int slot) {
  return (uint16_t)((slot / SPRITE_SLOTS_PER_ROW) * 32 +
                    (slot % SPRITE_SLOTS_PER_ROW) * 2);
}

uint16_t sprite_slot_vram(int slot) {
  return (uint16_t)((slot / SPRITE_SLOTS_PER_ROW) * 0x200 +
                    (slot % SPRITE_SLOTS_PER_ROW) * 0x20);
}

// ---------------------------------------------------------------------------
// Metasprites
// ---------------------------------------------------------------------------

bool sprite_meta_addr_valid(uint32_t addr) {
  uint32_t bank = addr >> 16;
  return bank >= SPRITE_META_BANK_LO && bank <= SPRITE_META_BANK_HI &&
         (addr & 0xffff) >= 0x8000;
}

int sprite_meta_read(const Rom* rom, uint32_t addr, SpriteMeta* out) {
  memset(out, 0, sizeof *out);
  out->addr = addr;
  if (!sprite_meta_addr_valid(addr)) return SPRITE_ERR_BANK;

  uint32_t avail = 0;
  const uint8_t* p = rom_ptr(rom, addr, &avail);
  if (!p || avail < 1) return SPRITE_ERR_ADDRESS;

  int count = p[0];
  out->count = count;
  out->bytes = 1 + (uint32_t)count * SPRITE_PIECE_BYTES;
  if (avail < out->bytes) return SPRITE_ERR_ADDRESS;

  const uint8_t* q = p + 1;
  for (int i = 0; i < count; i++, q += SPRITE_PIECE_BYTES) {
    out->pieces[i].x = (int16_t)(q[0] | (q[1] << 8));
    out->pieces[i].y = (int16_t)(q[2] | (q[3] << 8));
    out->pieces[i].attr = (uint16_t)(q[4] | (q[5] << 8));
    out->pieces[i].frame = (uint16_t)(q[6] | (q[7] << 8));
  }
  return SPRITE_OK;
}

// ---------------------------------------------------------------------------
// OAM composition — the port of $80:BA51 / $BABA / $BB30 / $BBA6
// ---------------------------------------------------------------------------

// `SEC : SBC #$000F` after `EOR #$FFFF`, i.e. -v - 16. This is how the flipped
// emitters mirror a piece offset: a 16x16 frame placed at `v` occupies
// `v..v+15`, so its mirror image starts at `-v-16`.
static inline uint16_t mirror(uint16_t v) {
  return (uint16_t)(((uint16_t)~v) - 0x000f);
}

static inline void store16(SpriteOam* oam, uint32_t off, uint16_t v) {
  if (off < SPRITE_OAM_BYTES) oam->bytes[off] = (uint8_t)v;
  if (off + 1 < SPRITE_OAM_BYTES) oam->bytes[off + 1] = (uint8_t)(v >> 8);
}

int sprite_emit(SpriteOam* oam, const SpriteMeta* meta, SpriteFlip flip,
                int16_t ox, int16_t oy, uint16_t attr_or, uint16_t attr_and,
                SpriteTileFn tile_of, void* ctx, SpriteEmitTrace* trace) {
  if (trace) {
    trace->walked = 0;
    trace->attr = 0;
    trace->attr_valid = false;
  }
  bool flip_x = (flip & SPRITE_FLIP_X) != 0;
  bool flip_y = (flip & SPRITE_FLIP_Y) != 0;
  // The EOR the emitter applies to every finished OAM word: the hardware's own
  // flip bits, toggled rather than set, so a piece authored flipped comes out
  // unflipped when the actor is.
  uint16_t flip_eor = (uint16_t)((flip_x ? 0x4000 : 0) | (flip_y ? 0x8000 : 0));

  uint16_t x_index = oam->index;
  int written = 0;

  for (int i = 0; i < meta->count; i++) {
    const SpritePiece* p = &meta->pieces[i];

    uint16_t sy = (uint16_t)p->y;
    if (flip_y) { PORT_COVER(emit_flip_y); sy = mirror(sy); }
    sy = (uint16_t)(sy + (uint16_t)oy);
    // 16-bit store, as the ROM does it: this also lands the high byte in the
    // tile slot, which the attribute store below overwrites.
    store16(oam, (uint32_t)x_index + 1, sy);
    // Keep only rows the 224-line display can show, allowing the 15 pixels a
    // sprite may hang off the top ($FFF1..$FFFF).
    if (sy < 0xfff1 && sy >= 0x00e0) {
      PORT_COVER(emit_drop_y);
      if (trace) trace->walked++;
      continue;
    }

    uint16_t sx = (uint16_t)p->x;
    if (flip_x) { PORT_COVER(emit_flip_x); sx = mirror(sx); }
    sx = (uint16_t)(sx + (uint16_t)ox);
    if (x_index < SPRITE_OAM_LOW_BYTES) oam->bytes[x_index] = (uint8_t)sx;
    if (sx >= 0x0100) {
      if (sx < 0xfff1) {
        PORT_COVER(emit_drop_x);
        if (trace) trace->walked++;
        continue;
      }
      PORT_COVER(emit_wrap_x);
      // Off the left edge: set this sprite's x bit 8 in the high table. The
      // ROM reads it out of the table at $80:B747, which holds exactly this
      // address and mask for all 128 sprites.
      uint32_t n = x_index / 4u;
      uint32_t hi = SPRITE_OAM_LOW_BYTES + (n >> 2);
      if (hi < SPRITE_OAM_BYTES) oam->bytes[hi] |= (uint8_t)(1u << ((n & 3) * 2));
    }

    uint16_t attr = (uint16_t)(p->attr & attr_and);
    if (trace) {
      trace->attr = attr;
      trace->attr_valid = true;
    }
    uint16_t word = tile_of ? tile_of(p->frame, ctx) : 0;
    word = (uint16_t)((word | attr | attr_or) ^ flip_eor);
    store16(oam, (uint32_t)x_index + 2, word);

    x_index += 4;
    written++;
    // `$80:BAA9` leaves without stepping the walk past this piece, which is why
    // the break is here rather than after the increment below.
    if (x_index == SPRITE_OAM_LOW_BYTES) { PORT_COVER(emit_oam_full); break; }
    if (trace) trace->walked++;
  }

  oam->index = x_index;
  return written;
}

void sprite_oam_terminate(SpriteOam* oam) {
  store16(oam, oam->index, 0xe000);
}
