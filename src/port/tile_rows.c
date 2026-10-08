// $80:ACA2 and $80:AB8F  the map's rows -- see port/tile_rows.h.

#include "port/tile_rows.h"

#include "port/camera.h"
#include "port/coverage.h"
#include "port/levelmap.h"
#include "port/terrain.h"

// --- $80:ACA2 ---------------------------------------------------------------

bool tilemap_row_tables_supported(uint16_t blocks_down) {
  return blocks_down >= 1 && blocks_down <= MAP_BLOCK_ROWS_MAX;
}

void tilemap_row_tables(Wram* w, PortCpu* c) {
  // `PHD : PEA $0000 : PLD`.
  const uint16_t page = c->d;
  push16(w, c, page);
  push16(w, c, 0);
  c->s = (uint16_t)(c->s + 2);

  const uint16_t block_row_bytes = (uint16_t)(c->x << 1);
  const uint16_t block_rows = c->y;
  const uint16_t tile_row_bytes = (uint16_t)(block_row_bytes * BLOCK_TILES);
  const uint16_t tile_rows = (uint16_t)(block_rows * BLOCK_TILES);
  wram_w16(w, W_MAP_BLOCK_ROW_BYTES, block_row_bytes);
  wram_w16(w, W_MAP_BLOCK_ROWS, block_rows);
  wram_w16(w, W_TILEMAP_ROW_BYTES, tile_row_bytes);
  wram_w16(w, W_TILEMAP_ROWS, tile_rows);

  uint16_t at = 0;
  for (uint16_t row = 0; row < tile_rows; row++) {
    wram_w16(w, (uint32_t)W_TILE_ROW_BASE + row * 2u, at);
    at = (uint16_t)(at + tile_row_bytes);
  }
  at = wram_r16(w, W_MAP_FIRST_ROW);
  for (uint16_t row = 0; row < block_rows; row++) {
    wram_w16(w, (uint32_t)W_BLOCK_ROW_BASE + row * 2u, at);
    at = (uint16_t)(at + block_row_bytes);
  }

  // The map in pixels, less a screen. The flags are the `PLD`'s but for
  // carry and overflow, which are the second subtraction's.
  wram_w16(w, W_CAMERA_MAX_X, (uint16_t)(tile_row_bytes * 4u - SCREEN_WIDTH));
  set_c(c, true);
  c->a = sbc16(c, (uint16_t)(tile_rows * 8u), SCREEN_HEIGHT);
  wram_w16(w, W_CAMERA_MAX_Y, c->a);
  c->x = (uint16_t)(block_rows * 2u);
  c->y = 0;
  c->d = pull16(w, c);
  set_nz16(c, c->d);
  c->pc = TILEMAP_ROW_TABLES_RTL_PC;
}

// --- $80:AB8F ---------------------------------------------------------------

static uint16_t zp(const Wram* w, uint16_t at) { return wram_r16(w, at); }

// --- $80:AB5A ---------------------------------------------------------------

void tile_block_begin(Wram* w, PortCpu* c, bool* v) {
  PORT_COVER(tile_block_begun);
  // `PHD : PEA $0000 : PLD`.
  push16(w, c, c->d);
  push16(w, c, 0);
  c->s = (uint16_t)(c->s + 2);
  c->d = 0;

  const uint16_t column = (uint16_t)(c->x * BLOCK_TILES);
  const uint16_t row = (uint16_t)(c->y * BLOCK_TILES);
  wram_w16(w, TB_DP_COLUMN, column);
  wram_w16(w, TB_DP_BLOCK_COLUMN, column);
  wram_w16(w, TB_DP_ROW, row);

  // `$80:AD0B`: where the block's tiles are.
  const uint16_t block = (uint16_t)(c->a & BLOCK_NUMBER_MASK);
  wram_w16(w, TB_DP_FROM, (uint16_t)((block << BLOCK_LIBRARY_SHIFT) +
                                     wram_r16(w, W_BLOCK_LIBRARY)));
  wram_w16(w, TB_DP_FROM + 2, wram_r16(w, W_BLOCK_LIBRARY_BANK));

  // `$80:AD1C`: where its first row goes.
  TilemapAddrRegs at;
  tilemap_tile_addr(w, column, row, &at);
  *v = add16_overflows((uint16_t)(at.a - at.x), at.x);
  wram_w16(w, TB_DP_TO, at.a);
  wram_w16(w, TB_DP_TO + 2, TILE_BLOCK_BANK_TO);
  wram_w16(w, TB_DP_ROWS_LEFT, BLOCK_TILES);

  c->a = BLOCK_TILES;
  set_nz16(c, c->a);
  c->x = at.x;
  c->y = row;
  set_c(c, at.c);
  c->pc = TILE_BLOCK_HOLD_PC;
}

// --- A block swapped for its pair ---------------------------------------------

static bool map_in_rom(uint8_t bank) { return (bank & 0xfeu) != 0x7eu; }

bool tile_block_swap_supported(const Wram* w, const Rom* rom, uint16_t x,
                               uint16_t y) {
  const uint16_t column = (uint16_t)(x >> BLOCK_PIXEL_SHIFT);
  const uint16_t row = (uint16_t)(y >> BLOCK_PIXEL_SHIFT);
  const uint16_t at = (uint16_t)(
      wram_r16(w, (uint32_t)W_BLOCK_ROW_BASE + (uint32_t)(row << 1)) +
      (uint16_t)(column << 1));
  const uint8_t bank = wram_r8(w, LM_DP_MAP_BANK);
  if (at == 0xffffu) return false;
  if (!map_in_rom(bank)) return true;
  return at >= 0x8000u && rom_has(rom, ((uint32_t)bank << 16) | at, 2);
}

void tile_block_swap_ask(Wram* w, const Rom* rom, PortCpu* c, uint16_t x,
                         uint16_t y, BlockSwapWork* k) {
  PORT_COVER(tile_block_swap_asked);
  push16(w, c, c->d);
  const uint16_t column = (uint16_t)(x >> BLOCK_PIXEL_SHIFT);
  const uint16_t row = (uint16_t)(y >> BLOCK_PIXEL_SHIFT);
  wram_w16(w, BLOCK_SWAP_COLUMN, column);
  wram_w16(w, BLOCK_SWAP_ROW, row);
  c->d = 0;

  BlockCellRegs cell;
  blockmap_cell_ptr(w, 0, column, row, &cell);
  const uint16_t at = wram_r16(w, LM_DP_SRC);
  k->v = add16_overflows((uint16_t)(at - cell.x), cell.x);
  const uint8_t bank = wram_r8(w, LM_DP_SRC_BANK);
  k->cell_in_rom = map_in_rom(bank);
  const uint16_t block =
      k->cell_in_rom
          ? rom_word(rom, ((uint32_t)bank << 16) | at)
          : wram_r16(w, ((uint32_t)(bank & 1u) << 16) | at);

  c->a = (uint16_t)(block ^ BLOCK_PAIR_BIT);
  c->x = column;
  c->y = row;
  set_nz16(c, row);
  set_c(c, cell.c);
  set_v(c, k->v);
}

bool tile_block_rows_supported(const Wram* w) {
  if (wram_r8(w, TB_DP_FROM + 2) != TILE_BLOCK_BANK_FROM) return false;
  if (wram_r8(w, TB_DP_TO + 2) != TILE_BLOCK_BANK_TO) return false;
  const uint16_t rows = zp(w, TB_DP_ROWS_LEFT);
  if (rows == 0 || rows > BLOCK_TILES) return false;
  const uint32_t from_end =
      (uint32_t)zp(w, TB_DP_FROM) + rows * (BLOCK_TILES * 2u);
  const uint32_t to_end = (uint32_t)zp(w, TB_DP_TO) +
                          (rows - 1u) * zp(w, W_TILEMAP_ROW_BYTES) +
                          BLOCK_TILES * 2u;
  return from_end <= 0x10000u && to_end <= 0x10000u;
}

// `$80:A9F3`: is this tile in what the camera shows? The right edge is one
// past the last column and the bottom edge is the last row.
static bool on_screen(const Wram* w, uint16_t column, uint16_t row,
                      TileRowsWork* k) {
  bool seen = false;
  k->blocks[TR_TEST_END]++;
  k->blocks[TR_TEST_1]++;
  if (column >= wram_r16(w, W_CAMERA_TILE_X)) {
    k->blocks[TR_TEST_2]++;
    if (column < wram_r16(w, W_CAMERA_TILE_X_END)) {
      k->blocks[TR_TEST_3]++;
      if (row >= wram_r16(w, W_CAMERA_TILE_Y)) {
        const uint16_t last = wram_r16(w, W_CAMERA_TILE_Y_END);
        k->blocks[TR_TEST_4]++;
        if (row == last) {
          k->blocks[TR_TAKEN]++;
          return true;
        }
        k->blocks[TR_TEST_5]++;
        seen = row < last;
      }
    }
  }
  if (!seen) k->blocks[TR_TAKEN]++;
  return seen;
}

static void queue_put(Wram* w, uint16_t at, uint16_t vram, uint16_t length,
                      uint16_t from) {
  wram_w16(w, (uint32_t)W_VRAM_QUEUE_DEST + at, vram);
  wram_w16(w, (uint32_t)W_VRAM_QUEUE_SIZE + at, length);
  wram_w16(w, (uint32_t)W_VRAM_QUEUE_SRC + at, from);
}

static void queue_kind(Wram* w, uint16_t at) {
  wram_w16(w, (uint32_t)W_VRAM_QUEUE_BANK + at, TILE_BLOCK_BANK_TO);
  wram_w16(w, (uint32_t)W_VRAM_QUEUE_VMAIN + at, VMAIN_STEP_ROW);
}

// `$80:AAA1`: queue `TB_DP_LENGTH` bytes of the row from `TB_DP_SEND_FROM`,
// to where column `TB_DP_COLUMN` of this row is in the tilemap.
static void queue_row(Wram* w, const Rom* rom, PortCpu* c, TileRowsWork* k) {
  const uint16_t cursor =
      (uint16_t)((zp(w, TB_DP_COLUMN) - wram_r16(w, W_CAMERA_TILE_X) +
                  wram_r16(w, W_TILEMAP_CURSOR_X)) & TILEMAP_CURSOR_MASK_X);
  const uint16_t base =
      (uint16_t)(rom_word(rom, TILEMAP_COLUMN_DEST_TABLE + cursor * 2u) +
                 wram_r16(w, W_TILEMAP_VRAM_BASE));
  const uint16_t down =
      (uint16_t)((zp(w, TB_DP_ROW) - wram_r16(w, W_CAMERA_TILE_Y) +
                  wram_r16(w, W_TILEMAP_CURSOR_Y)) & TILEMAP_CURSOR_MASK_Y);
  const uint16_t vram = (uint16_t)((down << 5) + base);
  const uint16_t length = zp(w, TB_DP_LENGTH);
  const uint16_t from = zp(w, TB_DP_SEND_FROM);
  const uint16_t at = wram_r16(w, W_VRAM_QUEUE_COUNT);
  wram_w16(w, TB_DP_CURSOR, cursor);
  wram_w16(w, TB_DP_SCRATCH, base);
  wram_w16(w, TB_DP_VRAM, vram);
  k->blocks[TR_PLACE]++;

  const uint16_t last = (uint16_t)((length >> 1) - 1 + cursor);
  if (((last ^ cursor) & CAMERA_SPLIT_COLUMNS) == 0) {
    PORT_COVER(tile_rows_one_run);
    queue_put(w, at, vram, length, from);
    queue_kind(w, at);
    wram_w16(w, W_VRAM_QUEUE_COUNT, (uint16_t)(at + 2));
    c->x = (uint16_t)(at + 2);
    k->blocks[TR_ONE]++;
    return;
  }

  // Across the seam. The first run is what is left of this screen, and the
  // word it is kept in is only ever raised: a block's later rows use the
  // longest any row of it has had.
  uint16_t first =
      (uint16_t)((((cursor & TILEMAP_SCREEN_MASK) ^ TILEMAP_SCREEN_MASK) + 1)
                 << 1);
  k->blocks[TR_TWO]++;
  k->blocks[TR_TAKEN]++;  // the `BNE` that came here
  if (first >= zp(w, TB_DP_FIRST_RUN)) {
    PORT_COVER(tile_rows_two_runs);
    wram_w16(w, TB_DP_FIRST_RUN, first);
    k->blocks[TR_TWO_STORE]++;
  } else {
    PORT_COVER(tile_rows_two_runs_kept);
    first = zp(w, TB_DP_FIRST_RUN);
    k->blocks[TR_TAKEN]++;
  }
  queue_put(w, at, vram, first, from);
  queue_put(w, (uint16_t)(at + 2),
            (uint16_t)((vram ^ TILEMAP_SECOND_SCREEN) & 0xffe0u),
            (uint16_t)(length - first), (uint16_t)(from + first));
  queue_kind(w, at);
  queue_kind(w, (uint16_t)(at + 2));
  wram_w16(w, W_VRAM_QUEUE_COUNT, (uint16_t)(at + 4));
  c->x = at;
}

// `$80:AAF7`: which of the row's eight tiles the camera shows, from its two
// ends, and a transfer queued for them.
static void send_row(Wram* w, const Rom* rom, PortCpu* c, TileRowsWork* k) {
  const uint16_t column = zp(w, TB_DP_COLUMN);
  const uint16_t row = zp(w, TB_DP_ROW);
  const uint16_t to = zp(w, TB_DP_TO);
  const bool left = on_screen(w, column, row, k);
  const bool right =
      on_screen(w, (uint16_t)(column + BLOCK_TILES - 1), row, k);
  const uint16_t seen = (uint16_t)((zp(w, TB_DP_SEEN) << 1) | (left ? 1 : 0));
  wram_w16(w, TB_DP_SEEN, seen);
  k->blocks[TR_ROW]++;

  if (!left && !right) {
    PORT_COVER(tile_rows_off_screen);
    c->x = 0;
    k->blocks[TR_NONE]++;
    return;
  }
  if (left && right) {
    PORT_COVER(tile_rows_whole);
    wram_w16(w, TB_DP_LENGTH, BLOCK_TILES * 2);
    wram_w16(w, TB_DP_SEND_FROM, to);
    k->blocks[TR_WHOLE]++;
  } else if (left) {
    // To the screen's right edge, and one tile past it.
    PORT_COVER(tile_rows_left_end);
    wram_w16(w, TB_DP_SEND_FROM, to);
    wram_w16(w, TB_DP_LENGTH,
             (uint16_t)((wram_r16(w, W_CAMERA_TILE_X_END) - column + 1) << 1));
    k->blocks[TR_LEFT_END]++;
  } else {
    // From the screen's left edge.
    PORT_COVER(tile_rows_right_end);
    const uint16_t edge = wram_r16(w, W_CAMERA_TILE_X);
    const uint16_t length = (uint16_t)((column + BLOCK_TILES - edge) << 1);
    wram_w16(w, TB_DP_LENGTH, length);
    wram_w16(w, TB_DP_SEND_FROM,
             (uint16_t)(to + BLOCK_TILES * 2 - length));
    wram_w16(w, TB_DP_COLUMN, edge);
    k->blocks[TR_RIGHT_END]++;
  }
  queue_row(w, rom, c, k);
}

void tile_block_rows(Wram* w, const Rom* rom, PortCpu* c, TileRowsWork* k) {
  const uint32_t from_bank = (uint32_t)(TILE_BLOCK_BANK_FROM & 1u) << 16;
  const uint32_t to_bank = (uint32_t)(TILE_BLOCK_BANK_TO & 1u) << 16;
  const uint16_t below = wram_r16(w, W_TILE_PRIORITY_BELOW);
  wram_w16(w, TB_DP_FIRST_RUN, 0);
  k->blocks[TR_HEAD]++;

  uint16_t left;
  do {
    wram_w16(w, TB_DP_COLUMN, zp(w, TB_DP_BLOCK_COLUMN));
    send_row(w, rom, c, k);

    // The row itself, last tile first. A tile below the level's line is
    // drawn in front of the figures.
    const uint16_t from = zp(w, TB_DP_FROM);
    const uint16_t to = zp(w, TB_DP_TO);
    for (int i = BLOCK_TILES - 1; i >= 0; i--) {
      uint16_t tile = wram_r16(w, from_bank | (uint16_t)(from + i * 2));
      k->blocks[TR_WORD]++;
      if ((tile & TILEMAP_COPY_MASK) < below) {
        tile |= TILEMAP_PRIORITY_BIT;
        k->blocks[TR_BELOW]++;
      } else {
        k->blocks[TR_TAKEN]++;
      }
      wram_w16(w, to_bank | (uint16_t)(to + i * 2), tile);
      if (i != 0) k->blocks[TR_TAKEN]++;
    }

    wram_w16(w, TB_DP_FROM, (uint16_t)(from + BLOCK_TILES * 2));
    set_c(c, false);
    wram_w16(w, TB_DP_TO, adc16(c, to, wram_r16(w, W_TILEMAP_ROW_BYTES)));
    wram_w16(w, TB_DP_ROW, (uint16_t)(zp(w, TB_DP_ROW) + 1));
    left = (uint16_t)(zp(w, TB_DP_ROWS_LEFT) - 1);
    wram_w16(w, TB_DP_ROWS_LEFT, left);
    k->blocks[TR_COPY]++;
    if (left != 0) k->blocks[TR_TAKEN]++;
  } while (left != 0);

  // `$80:9E37`: is the queue full? Nothing reads the answer.
  c->a = wram_r16(w, W_VRAM_QUEUE_COUNT);
  cmp16(c, c->a, VRAM_QUEUE_FULL);
  c->y = 0xfffeu;
  k->blocks[TR_TAIL]++;
  c->pc = TILE_BLOCK_ROWS_END_PC;
}
