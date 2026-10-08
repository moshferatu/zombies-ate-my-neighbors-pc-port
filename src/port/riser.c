// $81:8294  the figure that rises -- see port/riser.h.

#include "port/riser.h"

#include "port/coverage.h"
#include "port/oam.h"  // the display record's fields

static bool set_ok(const Wram* w, const PortCpu* c) {
  const uint16_t set = wram_r16(w, (uint16_t)(c->d + RISER_DP_SET));
  return set % RISER_SET_BYTES == 0 && set < RISER_SETS * RISER_SET_BYTES;
}

bool riser_frame_supported(const Wram* w, const PortCpu* c) {
  const uint16_t set = wram_r16(w, (uint16_t)(c->d + RISER_DP_SET));
  const uint16_t at = wram_r16(w, (uint16_t)(c->s + 1));
  return set_ok(w, c) && at % 2 == 0 && at >= set &&
         at < set + RISER_SET_BYTES;
}

// `$81:82D4`: a pixel up and the picture X is at, with the count of steps
// on the stack. It ends at the yield, or at the `RTL` after the last.
static void rise(Wram* w, const Rom* rom, PortCpu* c, RiserWork* k) {
  c->y = wram_r16(w, (uint16_t)(c->d + RISER_DP_RECORD));
  const uint16_t z = wram_r16(w, (uint16_t)(c->y + ACTOR_Z));
  wram_w16(w, (uint16_t)(c->y + ACTOR_Z), (uint16_t)(z + 1));
  uint16_t picture = rom_word(rom, RISER_PICTURES + c->x);
  k->blocks[RS_STEP]++;
  if (picture == 0) {
    PORT_COVER(riser_went_round);
    c->x = wram_r16(w, (uint16_t)(c->d + RISER_DP_SET));
    picture = rom_word(rom, RISER_PICTURES + c->x);
    k->blocks[RS_AROUND]++;
  } else {
    k->blocks[RS_TAKEN]++;
  }
  wram_w16(w, (uint16_t)(c->y + ACTOR_META), picture);

  c->a = (uint16_t)(pull16(w, c) - 1);
  set_nz16(c, c->a);
  k->blocks[RS_COUNT]++;
  if (c->a == 0) {
    PORT_COVER(riser_gone);
    k->blocks[RS_TAKEN]++;
    c->pc = RISER_RTL_PC;
    return;
  }
  push16(w, c, c->a);
  k->blocks[RS_FOURTH]++;
  if ((c->a & 3) == 0) {
    PORT_COVER(riser_next_picture);
    c->x = (uint16_t)(c->x + 2);
    k->blocks[RS_NEXT]++;
  } else {
    PORT_COVER(riser_rose);
    k->blocks[RS_TAKEN]++;
  }
  push16(w, c, c->x);
  c->a = RISER_STEP_FRAMES;
  set_nz16(c, c->a);
  k->blocks[RS_YIELD]++;
  c->pc = RISER_YIELD_PC;
}

void riser_frame(Wram* w, const Rom* rom, PortCpu* c, RiserWork* k) {
  c->x = pull16(w, c);
  k->blocks[RS_AGAIN]++;
  k->blocks[RS_TAKEN]++;  // the `BRA`
  rise(w, rom, c, k);
}

// `$81:82B6`: the picture X is at of the first five, for eight frames, and
// X on to the next. False at the zero after the fifth.
static bool show(Wram* w, const Rom* rom, PortCpu* c, RiserWork* k) {
  c->a = rom_word(rom, RISER_FIRST_PICTURES + c->x);
  set_nz16(c, c->a);
  k->blocks[RS_PICTURE]++;
  if (c->a == 0) {
    k->blocks[RS_TAKEN]++;
    return false;
  }
  c->y = wram_r16(w, (uint16_t)(c->d + RISER_DP_RECORD));
  wram_w16(w, (uint16_t)(c->y + ACTOR_META), c->a);
  c->x = (uint16_t)(c->x + 2);
  push16(w, c, c->x);
  c->a = RISER_SHOW_FRAMES;
  set_nz16(c, c->a);
  k->blocks[RS_SHOW]++;
  c->pc = RISER_SHOW_YIELD_PC;
  return true;
}

bool riser_begin_supported(uint16_t a) { return a < RISER_SETS; }

void riser_begin(Wram* w, const Rom* rom, PortCpu* c, RiserWork* k) {
  PORT_COVER(riser_began);
  c->x = asl16(c, c->a);
  wram_w16(w, (uint16_t)(c->d + RISER_DP_SET),
           rom_word(rom, RISER_SET_STARTS + c->x));
  const uint16_t record = wram_r16(w, (uint16_t)(c->d + RISER_DP_RECORD));
  wram_w16(w, (uint16_t)(record + ACTOR_FLAGS),
           (uint16_t)(wram_r16(w, (uint16_t)(record + ACTOR_FLAGS)) |
                      ACTOR_PRIORITY_TOP));
  wram_w16(w, (uint16_t)(record + ACTOR_COLLIDE_ID), 0);
  wram_w16(w, (uint16_t)(record + ACTOR_META_BANK), RISER_PICTURE_BANK);
  c->x = 0;
  k->blocks[RS_BEGIN]++;
  // The first of the five is never the zero.
  show(w, rom, c, k);
}

bool riser_shown_supported(const Wram* w, const PortCpu* c) {
  const uint16_t at = wram_r16(w, (uint16_t)(c->s + 1));
  return set_ok(w, c) && at % 2 == 0 && at < RISER_FIRST_BYTES;
}

void riser_shown(Wram* w, const Rom* rom, PortCpu* c, RiserWork* k) {
  c->x = pull16(w, c);
  k->blocks[RS_AGAIN]++;
  k->blocks[RS_TAKEN]++;  // the `BRA`
  if (show(w, rom, c, k)) {
    PORT_COVER(riser_showed);
    return;
  }
  PORT_COVER(riser_lifted);
  push16(w, c, RISER_STEPS);
  c->x = wram_r16(w, (uint16_t)(c->d + RISER_DP_SET));
  k->blocks[RS_RISE]++;
  rise(w, rom, c, k);
}
