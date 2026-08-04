// $80:AD2B and $80:ACF6 — see port/levelmap.h.

#include "port/levelmap.h"

#include "port/terrain.h"  // tilemap_tile_addr, and the row table it shares

// A word through a 24-bit pointer the ROM built at run time. The block library
// is always WRAM, but the block map's bank comes out of the level record, so
// this has to answer for both. The guards decline anything that is neither, so
// there is no third branch and no fallback value.
static uint16_t map_word(const Wram* w, const Rom* rom, uint16_t bank,
                         uint16_t addr) {
  if ((bank & 0xfffeu) == 0x007eu) {
    return wram_r16(w, ((uint32_t)(bank & 1u) << 16) | addr);
  }
  return rom_word(rom, ((uint32_t)bank << 16) | addr);
}

static bool bank_readable(const Rom* rom, uint16_t bank, uint16_t addr,
                          uint32_t bytes) {
  if ((bank & 0xfffeu) == 0x007eu) return true;  // WRAM wraps inside its bank
  return rom_has(rom, ((uint32_t)bank << 16) | addr, bytes);
}

// --- $80:ACF6 ---------------------------------------------------------------

bool blockmap_cell_ptr_supported(const Wram* w, uint16_t dp) {
  (void)w;
  (void)dp;
  return true;  // seven instructions, one table, nothing to decline
}

void blockmap_cell_ptr(Wram* w, uint16_t dp, uint16_t x, uint16_t y,
                       BlockCellRegs* out) {
  // `TXA : ASL : PHA` — the column, doubled, and kept on the stack rather than
  // in a register, which is the only reason X comes back changed.
  uint16_t col = (uint16_t)(x << 1);

  // `TYA : ASL : TAX : LDA $7E4228,X` — the row's own word offset. A long read,
  // so the index does not wrap inside the bank the way an absolute one would.
  uint16_t row_off =
      wram_r16(w, (uint32_t)W_BLOCK_ROW_BASE + (uint32_t)(y << 1));

  uint32_t sum = (uint32_t)row_off + col;  // `CLC : ADC $01,S`
  wram_w16(w, dp + LM_DP_SRC, (uint16_t)sum);
  uint16_t bank = wram_r16(w, dp + LM_DP_MAP_BANK);
  wram_w16(w, dp + LM_DP_SRC_BANK, bank);

  out->a = bank;  // `LDA $A8` was the last thing loaded
  out->x = col;   // `PLX` — the doubled column, not the caller's
  out->n = (col & 0x8000u) != 0;
  out->z = col == 0;
  out->c = sum > 0xffffu;
}

// --- $80:AD2B ---------------------------------------------------------------

bool blockmap_expand_supported(const Wram* w, const Rom* rom) {
  // The block map: one word per cell, and the guard asks for the whole of it
  // rather than for one cell, because the walk will read every one.
  uint16_t map_bank = wram_r16(w, LM_DP_MAP_BANK);
  uint16_t rows = wram_r16(w, LM_DP_MAP_ROWS);
  uint16_t row_bytes = wram_r16(w, LM_DP_MAP_ROW_BYTES);
  uint16_t first = wram_r16(w, (uint32_t)W_BLOCK_ROW_BASE);
  if (!bank_readable(rom, map_bank, first, (uint32_t)row_bytes * rows)) {
    return false;
  }
  // The library. Its bank is a constant at every call site there is, but the
  // port checks rather than trusting the caller.
  uint16_t lib_bank = wram_r16(w, LM_DP_BLOCKS_BANK);
  return bank_readable(rom, lib_bank, wram_r16(w, LM_DP_BLOCKS), LM_BLOCK_BYTES);
}

// One cell: find where its tiles go, find the block it names, copy the block.
// `src`/`dest` are threaded back out because the ROM keeps them in `$28`/`$2C`
// and the caller's next iteration does not care, but the exit registers do.
static void expand_cell(Wram* w, const Rom* rom, uint16_t col, uint16_t row,
                        uint16_t* out_a, bool* out_c) {
  TilemapAddrRegs addr;
  tilemap_tile_addr(w, (uint16_t)(col * LM_BLOCK_TILES_ACROSS),
                    (uint16_t)(row * LM_BLOCK_TILES_ACROSS), &addr);
  wram_w16(w, LM_DP_DEST, addr.a);
  wram_w16(w, LM_DP_DEST_BANK, LM_TILE_MAP_BANK);

  // `$80:ACF6`, then the word it pointed at: the block's index.
  BlockCellRegs cell;
  blockmap_cell_ptr(w, 0, col, row, &cell);
  uint16_t index = map_word(w, rom, wram_r16(w, LM_DP_SRC_BANK),
                            wram_r16(w, LM_DP_SRC));

  // `$80:AD0B` — seven `ASL`s and the library base. The shifts set carry and
  // the `CLC` throws it away, so only the `ADC` here can be seen from outside.
  uint16_t src = (uint16_t)((uint16_t)(index * LM_BLOCK_BYTES) +
                            wram_r16(w, LM_DP_BLOCKS));
  wram_w16(w, LM_DP_SRC, src);
  uint16_t src_bank = wram_r16(w, LM_DP_BLOCKS_BANK);
  wram_w16(w, LM_DP_SRC_BANK, src_bank);

  uint16_t dest = wram_r16(w, LM_DP_DEST);
  uint16_t stride = wram_r16(w, LM_DP_DEST_STRIDE);
  uint16_t a = dest;
  bool c = false;

  for (int r = LM_BLOCK_ROWS; r > 0; --r) {
    // `LDA [$28],Y : STA [$2C],Y : DEY DEY : BPL` — eight words, backwards.
    for (int16_t y = (int16_t)LM_BLOCK_LAST_WORD; y >= 0; y -= 2) {
      uint16_t v = map_word(w, rom, src_bank, (uint16_t)(src + (uint16_t)y));
      wram_w16(w, ((uint32_t)(LM_TILE_MAP_BANK & 1u) << 16) |
                      (uint16_t)(dest + (uint16_t)y),
               v);
    }
    src = (uint16_t)(src + LM_BLOCK_ROW_BYTES);
    wram_w16(w, LM_DP_SRC, src);

    uint32_t sum = (uint32_t)dest + stride;  // `LDA $2C : CLC : ADC $B2`
    dest = (uint16_t)sum;
    a = dest;
    c = sum > 0xffffu;
    wram_w16(w, LM_DP_DEST, dest);
  }

  *out_a = a;
  *out_c = c;
}

void blockmap_expand(Wram* w, const Rom* rom, BlockExpandRegs* out) {
  wram_w16(w, LM_DP_COL, 0);
  wram_w16(w, LM_DP_ROW, 0);
  wram_w16(w, LM_DP_ROWS_LEFT, wram_r16(w, LM_DP_MAP_ROWS));

  uint16_t a = 0;
  bool c = false;
  uint16_t rows_left, cols_left;

  do {  // $AD38 — one row of the block map
    // `LDA $AE : LSR` — the row's byte count, two per block.
    wram_w16(w, LM_DP_COLS_LEFT,
             (uint16_t)(wram_r16(w, LM_DP_MAP_ROW_BYTES) >> 1));

    do {  // $AD3D — one cell
      uint16_t col = wram_r16(w, LM_DP_COL);
      uint16_t row = wram_r16(w, LM_DP_ROW);
      expand_cell(w, rom, col, row, &a, &c);

      wram_w16(w, LM_DP_COL, (uint16_t)(col + 1u));
      cols_left = (uint16_t)(wram_r16(w, LM_DP_COLS_LEFT) - 1u);
      wram_w16(w, LM_DP_COLS_LEFT, cols_left);
    } while (cols_left != 0);

    wram_w16(w, LM_DP_COL, 0);
    wram_w16(w, LM_DP_ROW, (uint16_t)(wram_r16(w, LM_DP_ROW) + 1u));
    rows_left = (uint16_t)(wram_r16(w, LM_DP_ROWS_LEFT) - 1u);
    wram_w16(w, LM_DP_ROWS_LEFT, rows_left);
  } while (rows_left != 0);

  out->a = a;
  out->x = 0;       // the `DEX` that ended the last row loop
  out->y = 0xfffeu; // `DEY DEY` one step past zero
  out->c = c;
}
