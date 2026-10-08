// $80:F0D7  the tile a punch lands on -- see port/knock.h.

#include "port/knock.h"

#include "port/coverage.h"
#include "port/oam.h"
#include "port/terrain.h"
#include "port/thread.h"

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

// --- $81:F2B2  the thread ----------------------------------------------------

bool knock_thread_begin(Wram* w, PortCpu* c, uint16_t* record_out) {
  wram_w16(w, W_SPAWN_LOAD,
           (uint16_t)(wram_r16(w, W_SPAWN_LOAD) + KNOCK_LOAD));
  set_field(w, c, KNOCK_DP_KEPT, field(w, c->d, KNOCK_DP_RECORD));

  SlotAllocRegs slot;
  actor_slot_alloc(w, c->db, &slot);
  if (slot.c) return false;
  PORT_COVER(knock_thread_began);
  const uint16_t record = slot.a;
  *record_out = record;
  set_field(w, c, KNOCK_DP_RECORD, record);
  wram_w16(w, (uint16_t)(record + ACTOR_X), field(w, c->d, KNOCK_DP_ARG_X));
  wram_w16(w, (uint16_t)(record + ACTOR_Z), 0);
  set_c(c, false);
  wram_w16(w, (uint16_t)(record + ACTOR_Y),
           adc16(c, field(w, c->d, KNOCK_DP_ARG_Y), KNOCK_BELOW));
  wram_w16(w, (uint16_t)(record + ACTOR_META_BANK), KNOCK_PICTURE_BANK);
  wram_w16(w, (uint16_t)(record + ACTOR_META), KNOCK_PICTURE);
  wram_w16(w, (uint16_t)(record + ACTOR_THREAD), wram_r16(w, W_SCHED_CUR_TASK));
  wram_w16(w, (uint16_t)(record + ACTOR_COLLIDE_ID), KNOCK_COLLIDE_ID);
  wram_w16(w, (uint16_t)(record + ACTOR_FLAGS),
           (uint16_t)(wram_r16(w, (uint16_t)(record + ACTOR_FLAGS)) |
                      ACTOR_DRAW | ACTOR_PRIORITY_TOP));

  // Nothing is told what touches it.
  c->a = 0;
  c->y = 0;
  thread_set_handler(w, c);
  c->a = KNOCK_SFX;
  set_nz16(c, c->a);
  c->pc = KNOCK_THREAD_SFX_PC;
  return true;
}

bool knock_thread_swap_supported(const Wram* w, const Rom* rom,
                                 uint16_t page) {
  return tile_block_swap_supported(w, rom, field(w, page, KNOCK_DP_ARG_X),
                                   field(w, page, KNOCK_DP_ARG_Y));
}

void knock_thread_swap(Wram* w, const Rom* rom, PortCpu* c, BlockSwapWork* k) {
  PORT_COVER(knock_thread_swapping);
  tile_block_swap_ask(w, rom, c, field(w, c->d, KNOCK_DP_ARG_X),
                      field(w, c->d, KNOCK_DP_ARG_Y), k);
  c->pc = KNOCK_SWAP_PUT_PC;
}

void knock_thread_swapped(Wram* w, PortCpu* c) {
  PORT_COVER(knock_thread_swapped);
  c->d = pull16(w, c);
  wram_w16(w, W_BLOCKS_SWAPPED, (uint16_t)(wram_r16(w, W_BLOCKS_SWAPPED) + 1));
  wram_w16(w, W_KNOCK_UNDER_WAY, 0);
  c->a = KNOCK_PICTURES_AFTER;
  set_nz16(c, c->a);
  c->pc = KNOCK_SWAPPED_PLAY_PC;
}

bool knock_thread_end_supported(const Wram* w, uint16_t page, uint16_t s) {
  const uint16_t load = wram_r16(w, W_SPAWN_LOAD);
  const uint16_t record = field(w, page, KNOCK_DP_RECORD);
  return load >= KNOCK_LOAD && load < 0x8000u && record >= W_ACTOR_SLOTS &&
         record <= ACTOR_SLOT_LAST && actor_list_place(w, record) != -3 &&
         wram_r16(w, (uint16_t)(s + 1)) == (KNOCK_EXITED_PC & 0xffffu) - 1 &&
         wram_r8(w, (uint16_t)(s + 3)) == (KNOCK_EXITED_PC >> 16);
}

void knock_thread_end(Wram* w, PortCpu* c, int* place) {
  PORT_COVER(knock_thread_ended);
  set_c(c, true);
  wram_w16(w, W_SPAWN_LOAD, sbc16(c, wram_r16(w, W_SPAWN_LOAD), KNOCK_LOAD));

  // `JML actor_slot_free`: its `RTL` is the thread's, to `thread_exit`.
  const uint16_t record = field(w, c->d, KNOCK_DP_RECORD);
  *place = actor_list_place(w, record);
  SlotFreeRegs r;
  actor_slot_free(w, record, c->d, c->x, c->y, &r);
  c->a = r.a;
  c->x = r.x;
  c->y = r.y;
  c->p = (uint8_t)(c->p & ~(PORT_P_N | PORT_P_Z));
  if (r.n) c->p |= PORT_P_N;
  if (r.z) c->p |= PORT_P_Z;
  set_c(c, r.c);
  c->s = (uint16_t)(c->s + 3);
  c->pc = KNOCK_EXITED_PC;
}
