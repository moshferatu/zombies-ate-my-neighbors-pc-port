// $82:DD5C  the bystander's frame -- see port/bystander.h.

#include "port/bystander.h"

#include <stddef.h>

#include "port/coverage.h"
#include "port/dma.h"  // W_HDMA_CHANNELS
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

// `SEP #$20 : LDA #colour : STA $2132`, and A's low byte left.
static void colour_added(PortCpu* c, HwTrace* t, uint8_t colour) {
  hw_run(t, BYS_SEP_IMM);
  hw_w8(t, REG_COLDATA, colour);
  c->a = (uint16_t)((c->a & 0xff00u) | colour);
  set_nz8(c, colour);
}

void bystander_dim(Wram* w, const Rom* rom, PortCpu* c, HwTrace* t) {
  PORT_COVER(bystander_dimmed);
  colour_added(c, t, BYSTANDER_DIM);
  hw_run(t, BYS_REP_LDX);
  for (int at = BYSTANDER_SHADE_LAST; at >= 0; at -= 2) {
    wram_w16(w, (uint16_t)(W_BYSTANDER_SHADE + at),
             rom_word(rom, BYSTANDER_SHADE_SOURCE + (uint16_t)at));
    hw_run(t, BYS_COPY);
    if (at != 0) hw_run(t, BYS_TAKEN);
  }
  c->x = 0xfffeu;
  c->a = BYSTANDER_HDMA;
  set_nz16(c, c->a);
  wram_w16(w, W_HDMA_CHANNELS, c->a);
  hw_run(t, BYS_HDMA);
  c->pc = BYSTANDER_DIMMED_PC;
}

void bystander_lit(Wram* w, PortCpu* c, HwTrace* t) {
  PORT_COVER(bystander_lit);
  wram_w16(w, W_HDMA_CHANNELS, 0);
  hw_run(t, BYS_STZ);
  colour_added(c, t, BYSTANDER_LIT);
  hw_run(t, BYS_REP);
  c->pc = BYSTANDER_LIT_CALL_PC;
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
    const uint16_t slot = field(w, page, BYSTANDER_DP_SLOT);
    log->a = slot;
    log->n = (slot & 0x8000u) != 0;
    log->z = slot == 0;
    return BYSTANDER_ENDS;
  }
  PORT_COVER(bystander_slept);
  log->a = BYSTANDER_YIELD_TICKS;
  log->z = false;
  return BYSTANDER_SLEEPS;
}
