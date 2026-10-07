// $80:F0D7  the tile a punch lands on -- see port/knock.h.

#include "port/knock.h"

#include "port/coverage.h"
#include "port/terrain.h"

static uint16_t field(const Wram* w, uint16_t page, uint16_t at) {
  return wram_r16(w, (uint16_t)(page + at));
}

static void set_field(Wram* w, const PortCpu* c, uint16_t at, uint16_t v) {
  wram_w16(w, (uint16_t)(c->d + at), v);
}

// The table for a picture of the swing.
static uint16_t table_for(const Rom* rom, uint16_t picture) {
  const uint16_t which =
      (uint16_t)(rom_word(rom, KNOCK_PICTURE_TABLES + picture) & 0x00ffu);
  return rom_word(rom, KNOCK_TABLES + which);
}

bool knock_supported(const Wram* w, const Rom* rom, uint16_t page) {
  const uint16_t way = field(w, page, KNOCK_DP_WAY);
  const uint16_t picture = field(w, page, KNOCK_DP_PICTURE);
  if (picture >= KNOCK_PICTURES || way > KNOCK_WAY_MAX) return false;
  const uint16_t table = table_for(rom, picture);
  return rom_has(rom, ((uint32_t)KNOCK_BANK << 16) | table,
                 (uint32_t)(2 * KNOCK_WAY_MAX) + 4);
}

KnockEnd knock_look(Wram* w, const Rom* rom, PortCpu* c) {
  c->pc = KNOCK_RTS_PC;
  c->a = wram_r16(w, W_KNOCK_UNDER_WAY);
  set_nz16(c, c->a);
  if (c->a != 0) {
    PORT_COVER(knock_busy);
    return KNOCK_BUSY;
  }

  const uint16_t way = field(w, c->d, KNOCK_DP_WAY);
  const uint16_t table = table_for(rom, field(w, c->d, KNOCK_DP_PICTURE));
  const uint32_t at = ((uint32_t)KNOCK_BANK << 16) |
                      (uint16_t)(table + (uint16_t)(way << 1));
  set_c(c, false);
  const uint16_t x = adc16(c, field(w, c->d, KNOCK_DP_X), rom_word(rom, at));
  set_field(w, c, KNOCK_DP_FIST_X, x);
  set_c(c, false);
  const uint16_t y =
      adc16(c, field(w, c->d, KNOCK_DP_Y), rom_word(rom, at + 2));
  set_field(w, c, KNOCK_DP_FIST_Y, y);
  set_field(w, c, KNOCK_DP_AT, (uint16_t)(at + 2));

  TileAttrsRegs tile;
  tile_attrs_at_pixel(w, x, y, &tile);
  c->a = tile.a;
  c->x = x;
  c->y = y;
  set_c(c, tile.c);
  // The lookup's own flags are its `PLB`'s, and the `BIT` leaves only zero.
  set_nz16(c, (uint16_t)((uint16_t)c->db << 8));
  if ((tile.a & KNOCK_CAN_BE) == 0) {
    PORT_COVER(knock_nothing);
    c->p |= PORT_P_Z;
    return KNOCK_NOTHING;
  }

  PORT_COVER(knock_begun);
  wram_w16(w, W_KNOCK_UNDER_WAY, 1);
  set_field(w, c, KNOCK_DP_ARG_TILE, tile.a);
  set_field(w, c, KNOCK_DP_ARG_X, x);
  set_field(w, c, KNOCK_DP_ARG_Y, y);
  set_field(w, c, KNOCK_DP_ARG_WAY, way);
  c->a = KNOCK_THREAD;
  c->y = KNOCK_THREAD_BANK;
  set_nz16(c, c->y);
  c->pc = KNOCK_SPAWN_PC;
  return KNOCK_BEGUN;
}
