// $80:D0BF  a player's pictures, a list of them -- see port/flinch.h.

#include "port/flinch.h"

#include "port/coverage.h"
#include "port/oam.h"  // the display record's fields

static uint16_t field(const Wram* w, uint16_t page, uint16_t at) {
  return wram_r16(w, (uint16_t)(page + at));
}

static uint16_t table(const Rom* rom, uint16_t at) {
  return rom_word(rom, ((uint32_t)FLINCH_BANK << 16) | at);
}

static uint16_t place_of(const Wram* w, const Rom* rom, uint16_t page,
                         uint16_t at) {
  uint16_t place = table(rom, (uint16_t)(field(w, page, FLINCH_DP_LIST) + at));
  if (field(w, page, FLINCH_DP_STATE) == FLINCH_STATE_OTHER)
    place = (uint16_t)(place + FLINCH_OTHER_PICTURES);
  return place;
}

bool flinch_frame_supported(const Wram* w, const Rom* rom, uint16_t page) {
  const uint16_t list = field(w, page, FLINCH_DP_LIST);
  const uint16_t pictures = field(w, page, FLINCH_DP_PICTURES);
  const uint16_t at =
      (uint16_t)(field(w, page, FLINCH_DP_AT) + FLINCH_ENTRY_BYTES);
  if (list < 0x8000u || pictures < 0x8000u ||
      at > 0x0100 || (uint32_t)list + at + 3 > 0xffffu)
    return false;
  return (uint32_t)pictures + place_of(w, rom, page, at) + 1 <= 0xffffu;
}

void flinch_frame(Wram* w, const Rom* rom, PortCpu* c, FlinchWork* k) {
  const uint16_t page = c->d;
  set_c(c, false);
  const uint16_t at =
      adc16(c, field(w, page, FLINCH_DP_AT), FLINCH_ENTRY_BYTES);
  wram_w16(w, (uint16_t)(page + FLINCH_DP_AT), at);
  k->blocks[FN_NEXT]++;

  const uint16_t list = field(w, page, FLINCH_DP_LIST);
  uint16_t place = table(rom, (uint16_t)(list + at));
  c->x = field(w, page, FLINCH_DP_STATE);
  cmp16(c, c->x, FLINCH_STATE_OTHER);
  k->blocks[FN_HEAD]++;
  if (c->x == FLINCH_STATE_OTHER) {
    PORT_COVER(flinch_other_state);
    set_c(c, false);
    place = adc16(c, place, FLINCH_OTHER_PICTURES);
    k->blocks[FN_OTHER]++;
  } else {
    k->blocks[FN_TAKEN]++;
  }

  c->x = field(w, page, FLINCH_DP_RECORD);
  wram_w16(w, (uint16_t)(c->x + ACTOR_META),
           table(rom, (uint16_t)(field(w, page, FLINCH_DP_PICTURES) + place)));
  c->y = (uint16_t)(at + 2);
  c->a = table(rom, (uint16_t)(list + c->y));
  set_nz16(c, c->a);
  k->blocks[FN_SHOW]++;
  if (c->a == 0) {
    PORT_COVER(flinch_done);
    k->blocks[FN_TAKEN]++;
    c->pc = FLINCH_DONE_PC;
    return;
  }
  PORT_COVER(flinch_next);
  c->pc = FLINCH_SLEEP_PC;
}
