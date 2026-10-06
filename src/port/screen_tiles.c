// $80:A4D9  a whole screen of the level's tiles -- see port/screen_tiles.h.

#include "port/screen_tiles.h"

#include "port/camera.h"
#include "port/coverage.h"
#include "port/terrain.h"

// A word of the level's map, which is in bank `$7F`.
static uint16_t map_word(const Wram* w, uint32_t at) {
  return wram_r16(w, 0x10000u + at);
}

// Where the map has the tile at a column and a row.
static uint16_t map_at(const Wram* w, uint16_t column, uint16_t row) {
  TilemapAddrRegs r;
  tilemap_tile_addr(w, column, row, &r);
  return r.a;
}

bool screen_tiles_supported(const Wram* w) {
  if (!tilemap_buffer_alloc_supported(w, SCREEN_TILES_BUFFER)) return false;
  const uint32_t buffer = wram_r16(w, W_TILEMAP_ARENA_NEXT);
  if (buffer + SCREEN_TILES_BUFFER > 0x10000u) return false;
  // Both copies have to stay inside the map's bank.
  const uint16_t column = wram_r16(w, W_CAMERA_X) >> 3;
  const uint16_t row = wram_r16(w, W_CAMERA_Y) >> 3;
  const uint32_t row_bytes = wram_r16(w, W_TILEMAP_ROW_BYTES);
  const uint32_t rows = map_at(w, column, row);
  const uint32_t extra =
      map_at(w, (uint16_t)(column + SCREEN_TILES_ACROSS), row);
  const uint32_t rows_end =
      rows + SCREEN_TILES_DOWN * row_bytes + 2 * SCREEN_TILES_ACROSS;
  const uint32_t extra_end = extra + SCREEN_TILES_COLUMN * row_bytes + 2;
  return rows_end <= 0x10000u && extra_end <= 0x10000u;
}

// One word of the map into the buffer, with its priority. True when it got
// the bit.
static bool copy_tile(Wram* w, uint32_t from, uint16_t to) {
  const uint16_t tile = map_word(w, from);
  const bool over =
      (tile & SCREEN_TILE_NUMBER) < wram_r16(w, W_TILE_PRIORITY_BELOW);
  wram_w16(w, to, over ? (uint16_t)(tile | SCREEN_TILE_PRIORITY) : tile);
  return over;
}

void screen_tiles_fill(Wram* w, PortCpu* c, ScreenTilesWork* k) {
  PORT_COVER(screen_tiles_fill);
  // The entry's pushes, and its `PLB`, which takes back one byte of the two
  // the `PEA` pushed.
  push8(w, c, c->db);
  push16(w, c, c->d);
  push16(w, c, 0x007e);
  c->s = (uint16_t)(c->s + 1);
  c->db = 0x7e;
  c->d = 0;

  // `$80:A439`: the camera in tiles, the pixels left over, and the tilemap's
  // cursor back at its corner.
  const uint16_t camera_x = wram_r16(w, W_CAMERA_X);
  const uint16_t camera_y = wram_r16(w, W_CAMERA_Y);
  const uint16_t column = camera_x >> 3;
  const uint16_t row = camera_y >> 3;
  wram_w16(w, W_TILEMAP_CURSOR_X, 0);
  wram_w16(w, W_TILEMAP_CURSOR_Y, 0);
  wram_w16(w, W_CAMERA_SUB_X, camera_x & 7u);
  wram_w16(w, W_CAMERA_SUB_Y, camera_y & 7u);
  wram_w16(w, SCREEN_DP_COLUMN, column);
  wram_w16(w, SCREEN_DP_ROW, row);

  TilemapAllocRegs buffer;
  tilemap_buffer_alloc(w, SCREEN_TILES_BUFFER, &buffer);
  wram_w16(w, SCREEN_DP_BUFFER, buffer.a);
  uint16_t to = buffer.a;

  // `$80:A462`: 31 rows, each from its last tile to its first.
  const uint16_t row_bytes = wram_r16(w, W_TILEMAP_ROW_BYTES);
  uint16_t map = map_at(w, column, row);
  for (int down = 0; down < SCREEN_TILES_DOWN; down++) {
    for (int at = 2 * (SCREEN_TILES_ACROSS - 1); at >= 0; at -= 2)
      if (copy_tile(w, (uint32_t)map + (uint32_t)at, (uint16_t)(to + at)))
        k->rows_over++;
    map = (uint16_t)(map + row_bytes);
    to = (uint16_t)(to + 2 * SCREEN_TILES_ACROSS);
  }
  wram_w16(w, SCREEN_DP_BUFFER_COLUMN, to);

  // `$80:A4A0`: the column to the right, from the top down. Y is how far
  // down the map it has come, in bytes.
  map = map_at(w, (uint16_t)(column + SCREEN_TILES_ACROSS), row);
  wram_w16(w, SCREEN_DP_MAP, map);
  wram_w16(w, SCREEN_DP_MAP + 2, SCREEN_MAP_BANK);
  uint16_t along = 0;
  for (int down = 0; down < SCREEN_TILES_COLUMN; down++) {
    if (copy_tile(w, (uint32_t)map + along, to)) k->column_over++;
    set_c(c, false);
    along = adc16(c, along, row_bytes);
    to = (uint16_t)(to + 2);
  }
  wram_w16(w, SCREEN_DP_TO, to);

  c->a = along;
  c->y = along;
  c->x = 0;
  set_nz16(c, 0);  // the `DEX` that ended the loop
  c->pc = SCREEN_TILES_QUEUE_PC;
}
