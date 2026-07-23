#include "assets/level.h"

#include <string.h>

bool level_record_addr(const Rom* rom, int level, uint32_t* out_addr) {
  if (level < LEVEL_FIRST || level >= LEVEL_FIRST + LEVEL_COUNT) return false;
  uint16_t addr = rom_word(rom, LEVEL_TABLE_ADDR + (uint32_t)level * 2);
  if (addr < 0x8000) return false;  // not a bank-$9F pointer
  if (out_addr) *out_addr = 0x9f0000u | addr;
  return true;
}

int level_header_read(const Rom* rom, uint32_t addr, LevelHeader* out) {
  if (!rom_has(rom, addr, LEVEL_RECORD_BYTES)) return LEVEL_ERR_ADDRESS;

  // Every pointer in the record is a (16-bit address, 16-bit bank) pair, which
  // is how `$80:86A2` loads them: two `LDA $9Fxxxx,X` in a row.
  #define PTR(off) ((uint32_t)rom_word(rom, addr + (off) + 2) << 16 | rom_word(rom, addr + (off)))
  #define VAL(off) rom_word(rom, addr + (off))

  memset(out, 0, sizeof *out);
  out->record_addr = addr;
  out->block_defs = PTR(0x00);
  out->block_map = PTR(0x04);
  out->tile_attrs = PTR(0x08);
  out->bg_tiles = PTR(0x0c);
  out->bg_palette = PTR(0x10);
  out->sprite_palette = PTR(0x14);
  out->unknown_18 = VAL(0x18);
  out->unknown_1a = VAL(0x1a);
  out->list_1c = VAL(0x1c);
  out->list_1e = VAL(0x1e);
  out->list_20 = VAL(0x20);
  out->cols = VAL(0x22);
  out->rows = VAL(0x24);
  out->priority_below = VAL(0x26);
  out->unknown_28 = VAL(0x28);
  out->start_x1 = VAL(0x2a);
  out->start_y1 = VAL(0x2c);
  out->start_x2 = VAL(0x2e);
  out->start_y2 = VAL(0x30);
  out->song = VAL(0x32);
  out->sample_set = VAL(0x34);

  #undef PTR
  #undef VAL

  if (out->cols == 0 || out->rows == 0) return LEVEL_ERR_ADDRESS;
  return LEVEL_OK;
}

int level_load_blocks(const Rom* rom, const LevelHeader* h, uint8_t* out,
                      uint32_t* out_bytes, LzssRing* ring) {
  uint32_t avail = 0;
  const uint8_t* src = rom_ptr(rom, h->block_defs, &avail);
  if (!src) return LEVEL_ERR_ADDRESS;

  LzssResult r = lzss_decompress(src, avail, out, LEVEL_BLOCK_LIB_BYTES, ring);
  if (r.status != LZSS_OK) return LEVEL_ERR_DECOMPRESS;
  if (out_bytes) *out_bytes = r.written;
  return LEVEL_OK;
}

int level_expand(const LevelHeader* h, const uint8_t* blocks, uint32_t blocks_len,
                 const Rom* rom, uint16_t* out, uint32_t out_entries) {
  uint32_t tile_cols = level_tile_cols(h);
  uint32_t entries = level_map_entries(h);
  if (out_entries < entries) return LEVEL_ERR_SIZE;

  // $80:ACF6 walks the block map as `base + row * (cols * 2) + col * 2`, all in
  // one bank, so the whole map has to be readable from its start address.
  uint32_t map_bytes = (uint32_t)h->cols * h->rows * 2;
  uint32_t map_avail = 0;
  const uint8_t* map = rom_ptr(rom, h->block_map, &map_avail);
  if (!map || map_avail < map_bytes) return LEVEL_ERR_ADDRESS;

  for (uint32_t brow = 0; brow < h->rows; brow++) {
    for (uint32_t bcol = 0; bcol < h->cols; bcol++) {
      uint32_t e = (brow * h->cols + bcol) * 2;
      uint16_t index = (uint16_t)(map[e] | ((uint16_t)map[e + 1] << 8));

      // $80:AD0B: `ASL A` seven times, then add the $7E:8000 base — 16-bit,
      // so an index of 256 or more lands below the library instead of past it.
      uint16_t block_addr = (uint16_t)(LEVEL_BLOCK_LIB_BASE + (uint16_t)(index << 7));
      if (block_addr < LEVEL_BLOCK_LIB_BASE) return LEVEL_ERR_BLOCK;
      uint32_t block_off = block_addr - LEVEL_BLOCK_LIB_BASE;
      if (block_off + LEVEL_BLOCK_BYTES > blocks_len) return LEVEL_ERR_BLOCK;

      // $80:AD65: eight rows of eight entries, the destination advancing by a
      // whole map row each time.
      const uint8_t* src = blocks + block_off;
      uint16_t* dst = out + (brow * LEVEL_BLOCK_TILES) * tile_cols
                          + bcol * LEVEL_BLOCK_TILES;
      for (uint32_t y = 0; y < LEVEL_BLOCK_TILES; y++) {
        for (uint32_t x = 0; x < LEVEL_BLOCK_TILES; x++) {
          dst[x] = (uint16_t)(src[x * 2] | ((uint16_t)src[x * 2 + 1] << 8));
        }
        src += LEVEL_BLOCK_TILES * 2;
        dst += tile_cols;
      }
    }
  }
  return LEVEL_OK;
}
