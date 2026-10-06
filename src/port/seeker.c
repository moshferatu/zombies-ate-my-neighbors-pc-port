// $82:E7C7 and $82:E807  the thing that comes at a player -- see
// port/seeker.h.

#include "port/seeker.h"

#include "port/coverage.h"
#include "port/oam.h"  // the display record's fields

bool seeker_step_supported(uint16_t way) { return way < SEEKER_WAYS; }

void seeker_step(Wram* w, const Rom* rom, PortCpu* c) {
  PORT_COVER(seeker_stepped);
  c->x = (uint16_t)(c->a << 2);
  c->y = wram_r16(w, (uint16_t)(c->d + SEEKER_DP_RECORD));

  set_c(c, false);
  const uint16_t x = adc16(c, rom_word(rom, SEEKER_STEPS + c->x),
                           wram_r16(w, (uint16_t)(c->d + SEEKER_DP_X)));
  wram_w16(w, (uint16_t)(c->d + SEEKER_DP_X), x);
  wram_w16(w, (uint16_t)(c->y + ACTOR_X), x);

  set_c(c, false);
  c->a = adc16(c, rom_word(rom, SEEKER_STEPS + c->x + 2),
               wram_r16(w, (uint16_t)(c->d + SEEKER_DP_Y)));
  wram_w16(w, (uint16_t)(c->d + SEEKER_DP_Y), c->a);
  wram_w16(w, (uint16_t)(c->y + ACTOR_Y), c->a);
  c->pc = SEEKER_STEP_RTS_PC;
}

bool seeker_flap(Wram* w, const Rom* rom, PortCpu* c) {
  const uint16_t left =
      (uint16_t)(wram_r16(w, (uint16_t)(c->d + SEEKER_DP_FRAMES)) - 1);
  wram_w16(w, (uint16_t)(c->d + SEEKER_DP_FRAMES), left);
  set_nz16(c, left);
  c->pc = SEEKER_FLAP_RTS_PC;
  if ((left & 0x8000u) == 0) {
    PORT_COVER(seeker_flap_waited);
    return false;
  }

  PORT_COVER(seeker_flapped);
  wram_w16(w, (uint16_t)(c->d + SEEKER_DP_FRAMES), SEEKER_FLAP_FRAMES);
  const uint16_t picture =
      (uint16_t)(wram_r16(w, (uint16_t)(c->d + SEEKER_DP_PICTURE)) + 1);
  wram_w16(w, (uint16_t)(c->d + SEEKER_DP_PICTURE), picture);
  c->x = (uint16_t)((picture & 1) << 1);
  set_c(c, false);  // the `ASL`
  c->y = wram_r16(w, (uint16_t)(c->d + SEEKER_DP_RECORD));
  c->a = rom_word(rom, SEEKER_PICTURES + c->x);
  set_nz16(c, c->a);
  wram_w16(w, (uint16_t)(c->y + ACTOR_META), c->a);
  return true;
}
