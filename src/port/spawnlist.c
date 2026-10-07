// $81:810F  a frame of the level's spawn list -- see port/spawnlist.h.

#include "port/spawnlist.h"

#include <stddef.h>

#include "port/coverage.h"
#include "port/step.h"
#include "port/thread.h"

static uint16_t field(const Wram* w, uint16_t page, uint16_t at) {
  return wram_r16(w, (uint16_t)(page + at));
}

static void set_field(Wram* w, uint16_t page, uint16_t at, uint16_t v) {
  wram_w16(w, (uint16_t)(page + at), v);
}

static uint32_t place_at(const Wram* w, uint16_t page, uint8_t bank,
                         uint16_t place) {
  return ((uint32_t)bank << 16) + field(w, page, SPAWNLIST_DP_LIST) +
         (uint32_t)place * SPAWNLIST_PLACE_BYTES;
}

static bool player_record_ok(uint16_t record) { return record < 0x1f00u; }

// The last byte a frame reads of a place is its y's second.
static bool place_readable(const Wram* w, uint16_t page, uint16_t place) {
  const uint32_t at = (uint32_t)field(w, page, SPAWNLIST_DP_LIST) +
                      (uint32_t)place * SPAWNLIST_PLACE_BYTES;
  return place < SPAWNLIST_PLACES_MAX && at >= 0x8000u &&
         at + SPAWNLIST_PLACE_Y + 2 <= 0x10000u;
}

bool spawnlist_frame_supported(const Wram* w, uint16_t page, uint8_t bank) {
  if (bank < 0x80 || bank > 0x9f) return false;
  // The nearest place is read only once something has been measured near
  // enough, and until then the field is whatever the page held: the thread's
  // setup does not clear it.
  const bool picks = field(w, page, SPAWNLIST_DP_DISTANCE) < SPAWNLIST_NEAR;
  return place_readable(w, page, field(w, page, SPAWNLIST_DP_PLACE)) &&
         (!picks ||
          place_readable(w, page, field(w, page, SPAWNLIST_DP_NEAREST))) &&
         player_record_ok(wram_r16(w, W_PLAYER_A_RECORD)) &&
         player_record_ok(wram_r16(w, W_PLAYER_B_RECORD));
}

static void next_place(Wram* w, uint16_t page, uint16_t place) {
  set_field(w, page, SPAWNLIST_DP_PLACE, (uint16_t)(place + 1));
}

// How far the place is from the nearer player, and is that the nearest yet?
static void measure(Wram* w, const Rom* rom, uint16_t page, uint16_t place,
                    uint32_t at, SpawnlistLog* log) {
  set_field(w, page, NEAREST_PLAYER_DP_X, rom_word(rom, at + SPAWNLIST_PLACE_X));
  set_field(w, page, NEAREST_PLAYER_DP_Y, rom_word(rom, at + SPAWNLIST_PLACE_Y));
  NearestRegs nearest;
  nearest_player_dist(w, page, 0, &nearest);

  log->measured = true;
  log->c = nearest.a >= field(w, page, SPAWNLIST_DP_DISTANCE);
  if (!log->c) {
    PORT_COVER(spawnlist_nearer);
    log->nearer = true;
    set_field(w, page, SPAWNLIST_DP_DISTANCE, nearest.a);
    set_field(w, page, SPAWNLIST_DP_NEAREST, place);
  } else {
    PORT_COVER(spawnlist_farther);
  }
}

static void start_pass(Wram* w, uint16_t page) {
  set_field(w, page, SPAWNLIST_DP_DISTANCE, 0xffff);
  set_field(w, page, SPAWNLIST_DP_PLACE, 0);
}

void spawnlist_begin(Wram* w, PortCpu* c) {
  PORT_COVER(spawnlist_began);
  for (uint16_t at = 0; at < SPAWNLIST_PLACES_MAX; at += 2)
    wram_w16(w, (uint16_t)(W_SPAWNLIST_REST + at), 0);
  wram_w16(w, (uint16_t)(c->d + SPAWNLIST_DP_LIST),
           wram_r16(w, (uint16_t)(c->d + SPAWNLIST_DP_GIVEN_LIST)));
  // `PEI ($02) : PLB`: the word goes on the stack and its low byte comes off.
  push16(w, c, wram_r16(w, (uint16_t)(c->d + SPAWNLIST_DP_GIVEN_BANK)));
  c->db = pull8(w, c);
  wram_w16(w, (uint16_t)(c->d + SPAWNLIST_DP_DISTANCE), 0xffff);
  wram_w16(w, (uint16_t)(c->d + SPAWNLIST_DP_PLACE), 0);
  c->x = 0xfffe;  // where the clearing loop's count ends
  c->a = 1;
  set_nz16(c, c->a);
  c->pc = SPAWNLIST_YIELD_PC;
}

SpawnlistFate spawnlist_frame(Wram* w, const Rom* rom, uint16_t page,
                              uint8_t bank, SpawnlistLog* log) {
  SpawnlistLog scratch;
  if (log == NULL) log = &scratch;
  *log = (SpawnlistLog){0};

  SpawnRoomRegs room;
  spawn_has_room(w, &room);
  if (room.c) {
    PORT_COVER(spawnlist_held);
    log->held = true;
    log->c = true;
    return SPAWNLIST_SLEEPS;
  }

  const uint16_t place = field(w, page, SPAWNLIST_DP_PLACE);
  const uint8_t resting = wram_r8(w, W_SPAWNLIST_REST + place);
  if (resting != 0) {
    PORT_COVER(spawnlist_rested);
    log->rested = true;
    wram_w8(w, W_SPAWNLIST_REST + place, (uint8_t)(resting - 1));
    next_place(w, page, place);
    return SPAWNLIST_SLEEPS;
  }

  const uint32_t at = place_at(w, page, bank, place);
  set_field(w, page, SPAWNLIST_DP_DOUBLE, (uint16_t)(2 * place));
  if ((rom_word(rom, at) & 0x00ffu) != 0) {
    measure(w, rom, page, place, at, log);
    next_place(w, page, place);
    return SPAWNLIST_SLEEPS;
  }

  // The end of the list.
  if (field(w, page, SPAWNLIST_DP_DISTANCE) >= SPAWNLIST_NEAR) {
    PORT_COVER(spawnlist_none_near);
    log->none_near = true;
    log->c = true;
    start_pass(w, page);
    return SPAWNLIST_SLEEPS;
  }
  PORT_COVER(spawnlist_started);
  const uint16_t nearest = field(w, page, SPAWNLIST_DP_NEAREST);
  const uint16_t rest = rom_word(rom, place_at(w, page, bank, nearest)) & 0x00ffu;
  set_field(w, page, SPAWNLIST_DP_DOUBLE, (uint16_t)(2 * nearest));
  wram_w8(w, W_SPAWNLIST_REST + nearest, (uint8_t)rest);
  log->rest = rest;
  log->place = nearest;
  log->place_at = (uint16_t)(nearest * SPAWNLIST_PLACE_BYTES);
  return SPAWNLIST_STARTS;
}
