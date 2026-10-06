// $80:D13A  a player's page, begun -- see port/page.h.

#include "port/page.h"

#include "port/coverage.h"

static uint16_t field(const Wram* w, const PortCpu* c, uint16_t at) {
  return wram_r16(w, (uint16_t)(c->d + at));
}

static void set_field(Wram* w, const PortCpu* c, uint16_t at, uint16_t v) {
  wram_w16(w, (uint16_t)(c->d + at), v);
}

void player_page_begin(Wram* w, PortCpu* c, PageWork* k) {
  PORT_COVER(player_page_begun);
  set_c(c, false);
  wram_w16(w, W_SPAWN_LOAD,
           adc16(c, wram_r16(w, W_SPAWN_LOAD), PAGE_PLAYER_WEIGHT));
  k->blocks[PG_HEAD]++;

  for (uint16_t at = PAGE_CLEAR_TO; at >= PAGE_CLEAR_FROM; at -= 2) {
    set_field(w, c, at, 0);
    k->blocks[PG_CLEAR]++;
    if (at != PAGE_CLEAR_FROM) k->blocks[PG_TAKEN]++;
  }

  const uint16_t slot = (uint16_t)(field(w, c, PAGE_DP_SLOT) << 1);
  c->x = slot;
  set_field(w, c, PAGE_DP_SIDE,
            wram_r16(w, (uint16_t)(W_SCORE_SLOT_SIDE + slot)));
  wram_w16(w, (uint16_t)(W_HUD_PANEL_ON + slot), 1);
  c->a = asl16(c, field(w, c, PAGE_DP_PLAYER_IN));
  set_field(w, c, PAGE_DP_PLAYER, c->a);
  c->y = c->a;
  k->blocks[PG_TAIL]++;
  c->pc = PAGE_BEGIN_ALLOC_PC;
}
