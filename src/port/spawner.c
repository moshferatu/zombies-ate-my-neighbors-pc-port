// $81:807E  something started from a list -- see port/spawner.h.

#include "port/spawner.h"

#include "port/coverage.h"
#include "port/thread.h"

static uint16_t entry_of(const Wram* w, uint16_t page) {
  return (uint16_t)(wram_r16(w, (uint16_t)(page + SPAWN_DP_INDEX)) *
                        SPAWN_ENTRY_BYTES +
                    wram_r16(w, (uint16_t)(page + SPAWN_DP_LIST)));
}

// The list is read through the data bank: in the cartridge, or in the WRAM
// every bank shows the bottom of.
bool spawn_entry_supported(const Wram* w, const PortCpu* c) {
  const uint16_t entry = entry_of(w, c->d);
  if (wram_r16(w, (uint16_t)(c->d + SPAWN_DP_INDEX)) >= 0x0400) return false;
  if (entry >= 0x8000u)
    return c->db >= 0x80 && entry <= 0xffffu - SPAWN_ENTRY_BYTES;
  return (c->db & 0x40) == 0 && entry <= 0x2000 - SPAWN_ENTRY_BYTES;
}

static uint16_t entry_word(const Wram* w, const Rom* rom, const PortCpu* c,
                           uint16_t entry, uint16_t at) {
  return bus_r16(w, rom, ((uint32_t)c->db << 16) | (uint16_t)(entry + at));
}

// A random byte under the mask, less half of it, added to `place`. The
// carry the draw takes is whatever was left.
static uint16_t scatter(Wram* w, PortCpu* c, bool* overflow, uint16_t spread,
                        uint16_t half, uint16_t place) {
  RngResult r;
  rng_next(w, flag(c, PORT_P_C), &r);
  *overflow = r.v;
  set_c(c, true);
  const uint16_t off = sbc16(c, (uint16_t)(r.a & spread), half);
  set_c(c, false);
  return adc16(c, off, place);
}

void spawn_entry(Wram* w, const Rom* rom, PortCpu* c, SpawnWork* k) {
  const uint16_t d = c->d;
  c->pc = SPAWN_ENTRY_RTS_PC;
  const uint16_t twice =
      asl16(c, wram_r16(w, (uint16_t)(d + SPAWN_DP_INDEX)));
  wram_w16(w, (uint16_t)(d + SPAWN_DP_TWICE), twice);
  set_c(c, false);
  const uint16_t ten = adc16(c, asl16(c, asl16(c, twice)), twice);
  set_c(c, false);
  const uint16_t entry =
      adc16(c, ten, wram_r16(w, (uint16_t)(d + SPAWN_DP_LIST)));
  wram_w16(w, (uint16_t)(d + SPAWN_DP_ENTRY), entry);
  c->x = entry;

  const uint16_t spread =
      entry_word(w, rom, c, entry, SPAWN_ENTRY_SPREAD) & 0x00ffu;
  if (spread == 0) {
    PORT_COVER(spawn_at_place);
    k->outcome = SPAWN_AT_PLACE;
    wram_w16(w, (uint16_t)(d + SPAWN_DP_X),
             entry_word(w, rom, c, entry, SPAWN_ENTRY_X));
    wram_w16(w, (uint16_t)(d + SPAWN_DP_Y),
             entry_word(w, rom, c, entry, SPAWN_ENTRY_Y));
  } else {
    wram_w16(w, (uint16_t)(d + SPAWN_DP_X), spread);
    wram_w16(w, (uint16_t)(d + SPAWN_DP_SPREAD), spread);
    const uint16_t half = (uint16_t)(spread >> 1);
    set_c(c, (spread & 1) != 0);  // the `LSR`
    wram_w16(w, (uint16_t)(d + SPAWN_DP_HALF), half);
    const uint16_t x =
        scatter(w, c, &k->drew_overflow[0], spread, half,
                entry_word(w, rom, c, entry, SPAWN_ENTRY_X));
    wram_w16(w, (uint16_t)(d + SPAWN_DP_X), x);
    const uint16_t y =
        scatter(w, c, &k->drew_overflow[1], spread, half,
                entry_word(w, rom, c, entry, SPAWN_ENTRY_Y));
    wram_w16(w, (uint16_t)(d + SPAWN_DP_Y), y);

    terrain_blocked_enemy(w, x, y, &k->ground);
    c->a = k->ground.a;
    c->x = k->ground.x;
    c->y = k->ground.y;
    set_c(c, k->ground.blocked);
    set_v(c, k->ground.v);
    set_nz16(c, d);  // its `PLD`
    if (k->ground.blocked) {
      PORT_COVER(spawn_ground_in_the_way);
      k->outcome = SPAWN_GROUND_IN_THE_WAY;
      return;
    }
    terrain_out_of_bounds(w, x, y, &k->bounds);
    c->a = k->bounds.a;
    c->x = x;
    c->y = y;
    set_c(c, k->bounds.c);
    c->p = (uint8_t)(c->p & ~(PORT_P_N | PORT_P_Z));
    if (k->bounds.n) c->p |= PORT_P_N;
    if (k->bounds.z) c->p |= PORT_P_Z;
    if (k->bounds.c) {
      PORT_COVER(spawn_off_the_level);
      k->outcome = SPAWN_OFF_THE_LEVEL;
      return;
    }
    PORT_COVER(spawn_scattered);
    k->outcome = SPAWN_SCATTERED;
  }

  wram_w16(w, (uint16_t)(d + SPAWN_DP_ARG_2), 0);
  wram_w16(w, (uint16_t)(d + SPAWN_DP_ARG_3), 0);
  const uint16_t bank = entry_word(w, rom, c, entry, SPAWN_ENTRY_BANK);
  k->slot = thread_spawn(w, rom, entry_word(w, rom, c, entry,
                                            SPAWN_ENTRY_THREAD),
                         bank, d);
  // `thread_spawn` comes back with the slot in A and X, or nothing and
  // `$FFFE` with none free, and Y the last of the page it copied.
  c->a = k->slot < 0 ? 0 : (uint16_t)k->slot;
  c->x = k->slot < 0 ? 0xfffe : (uint16_t)k->slot;
  c->y = k->slot < 0 ? bank : (THREAD_SPAWN_ARGS - 1) * 2;
  set_nz16(c, c->a);
}
