// $81:8300  the figure that rises -- see port/riser.h.

#include "port/riser.h"

#include "port/coverage.h"
#include "port/oam.h"  // the display record's fields

bool riser_frame_supported(const Wram* w, const PortCpu* c) {
  const uint16_t set = wram_r16(w, (uint16_t)(c->d + RISER_DP_SET));
  const uint16_t at = wram_r16(w, (uint16_t)(c->s + 1));
  return set % RISER_SET_BYTES == 0 && set < RISER_SETS * RISER_SET_BYTES &&
         at % 2 == 0 && at >= set && at < set + RISER_SET_BYTES;
}

void riser_frame(Wram* w, const Rom* rom, PortCpu* c, RiserWork* k) {
  c->x = pull16(w, c);
  c->y = wram_r16(w, (uint16_t)(c->d + RISER_DP_RECORD));
  const uint16_t z = wram_r16(w, (uint16_t)(c->y + ACTOR_Z));
  wram_w16(w, (uint16_t)(c->y + ACTOR_Z), (uint16_t)(z + 1));
  uint16_t picture = rom_word(rom, RISER_PICTURES + c->x);
  k->blocks[RS_HEAD]++;
  k->blocks[RS_TAKEN]++;  // the `BRA`
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
  c->a = 2;
  set_nz16(c, 2);
  k->blocks[RS_YIELD]++;
  c->pc = RISER_YIELD_PC;
}
