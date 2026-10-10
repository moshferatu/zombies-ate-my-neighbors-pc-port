// $80:D02D  a hit taken from a player's health -- see port/hit.h.

#include "port/hit.h"

#include "port/coverage.h"
#include "port/oam.h"  // the display record's fields

static uint16_t field(const Wram* w, const PortCpu* c, uint16_t at) {
  return wram_r16(w, (uint16_t)(c->d + at));
}

static void set_field(Wram* w, const PortCpu* c, uint16_t at, uint16_t v) {
  wram_w16(w, (uint16_t)(c->d + at), v);
}

void player_hit(Wram* w, const Rom* rom, PortCpu* c, HitWork* k) {
  c->x = field(w, c, HIT_DP_WHO);
  c->a = rom_word(rom, ((uint32_t)HIT_BANK << 16) |
                           (uint16_t)(HIT_UNHURT_PICTURES + c->x));
  const uint16_t pictures = field(w, c, HIT_DP_PICTURES);
  cmp16(c, c->a, pictures);
  k->blocks[HT_WHO]++;
  c->pc = HIT_RTS_PC;
  if (c->a == pictures) {
    PORT_COVER(hit_shrugged);
    set_field(w, c, HIT_DP_REQUEST, 0);
    k->blocks[HT_NONE]++;
    k->end = HIT_SHRUGGED;
    return;
  }
  k->blocks[HT_TAKEN]++;

  c->x = field(w, c, HIT_DP_PLAYER);
  const uint16_t health_at = (uint16_t)(W_HIT_HEALTH + c->x);
  c->a = wram_r16(w, health_at);
  set_nz16(c, c->a);
  k->blocks[HT_HEALTH]++;
  if (c->a == 0) {
    PORT_COVER(hit_no_health);
    k->blocks[HT_TAKEN]++;
    k->end = HIT_NO_HEALTH;
    return;
  }

  wram_w16(w, health_at, (uint16_t)(c->a - 1));
  c->y = field(w, c, HIT_DP_OTHER_RECORD);
  c->a = (uint16_t)(wram_r16(w, (uint16_t)(c->y + ACTOR_FLAGS)) & ~ACTOR_DRAW);
  set_nz16(c, c->a);
  wram_w16(w, (uint16_t)(c->y + ACTOR_FLAGS), c->a);
  const uint16_t request = field(w, c, HIT_DP_REQUEST);
  bit16(c, request);
  set_field(w, c, HIT_DP_REQUEST, 0);
  k->blocks[HT_TAKE]++;
  if (request & HIT_REQUEST_OTHER) {
    PORT_COVER(hit_shown_the_other_way);
    k->end = HIT_OTHER;
    c->pc = HIT_OTHER_PC;
    return;
  }
  PORT_COVER(hit_flinched);
  k->blocks[HT_TAKEN]++;
  k->end = HIT_FLINCHED;
  c->a = HIT_SOUND;
  set_nz16(c, c->a);
  k->blocks[HT_SOUND]++;
  c->pc = HIT_FLINCH_PC;
}
