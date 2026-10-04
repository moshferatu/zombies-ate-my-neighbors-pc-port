// $82:DD5C  the bystander's frame -- see port/bystander.h.

#include "port/bystander.h"

#include <stddef.h>

#include "port/coverage.h"
#include "port/oam.h"

#define DRAWN_MAX_BYTES 0x40  // 32 records

static uint16_t field(const Wram* w, uint16_t page, uint16_t at) {
  return wram_r16(w, (uint16_t)(page + at));
}

static uint16_t drawn_record(const Wram* w, int at) {
  return wram_r16(w, (uint16_t)(W_VISIBLE_ACTORS + at));
}

bool bystander_frame_supported(const Wram* w) {
  const uint16_t bytes = wram_r16(w, W_VISIBLE_ACTOR_COUNT);
  if (bytes > DRAWN_MAX_BYTES || bytes % 2 != 0) return false;
  for (int at = 0; at < bytes; at += 2) {
    if (drawn_record(w, at) >= 0x1f00) return false;
  }
  return true;
}

// Is this record a player, and in the box? The log keeps which test said no.
static bool is_player_in_box(const Wram* w, uint16_t page, uint16_t record,
                             BystanderLog* log) {
  const uint16_t id = wram_r16(w, (uint16_t)(record + ACTOR_COLLIDE_ID));
  log->a = id;
  if (id == 0) {
    log->not_colliding++;
    return false;
  }
  if (id == ALIGNED_ID_PLAYER_A) {
    log->first_players++;
  } else if (id == ALIGNED_ID_PLAYER_B) {
    log->second_players++;
  } else {
    log->others++;
    return false;
  }

  const uint16_t x = wram_r16(w, (uint16_t)(record + ACTOR_X));
  log->a = x;
  if (x < field(w, page, BYSTANDER_DP_LEFT)) {
    log->left_of++;
    return false;
  }
  if (x >= field(w, page, BYSTANDER_DP_RIGHT)) {
    log->right_of++;
    return false;
  }
  const uint16_t y = wram_r16(w, (uint16_t)(record + ACTOR_Y));
  log->a = y;
  if (y < field(w, page, BYSTANDER_DP_TOP)) {
    log->above++;
    return false;
  }
  if (y >= field(w, page, BYSTANDER_DP_BOTTOM)) {
    log->below++;
    return false;
  }
  return true;
}

// `$82:DD6E`: down the list of what is being drawn, from its end.
static bool player_in_box(const Wram* w, uint16_t page, BystanderLog* log) {
  const uint16_t bytes = wram_r16(w, W_VISIBLE_ACTOR_COUNT);
  if (bytes == 0) {
    log->none_drawn = true;
    return false;
  }
  if (bytes == 2) {
    PORT_COVER(bystander_one_drawn);
    log->one_drawn = true;
    return false;
  }
  for (int at = bytes - 2; at >= 0; at -= 2) {
    log->looked++;
    if (is_player_in_box(w, page, drawn_record(w, at), log)) return true;
  }
  return false;
}

BystanderFate bystander_frame(const Wram* w, uint16_t page, BystanderLog* log) {
  BystanderLog scratch;
  if (log == NULL) log = &scratch;
  *log = (BystanderLog){0};

  if (player_in_box(w, page, log)) {
    PORT_COVER(bystander_met);
    // The last compare was the player's y against the bottom of the box.
    const uint16_t gap =
        (uint16_t)(log->a - field(w, page, BYSTANDER_DP_BOTTOM));
    log->n = (gap & 0x8000u) != 0;
    log->z = false;
    return BYSTANDER_MET;
  }

  const uint16_t end = field(w, page, BYSTANDER_DP_END);
  log->a = end;
  log->n = (end & 0x8000u) != 0;
  log->z = end == 0;
  if (end != 0) {
    PORT_COVER(bystander_ended);
    return BYSTANDER_ENDS;
  }
  PORT_COVER(bystander_slept);
  log->a = BYSTANDER_YIELD_TICKS;
  log->z = false;
  return BYSTANDER_SLEEPS;
}
