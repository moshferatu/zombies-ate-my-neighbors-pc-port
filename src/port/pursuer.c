// $81:A74D  a thing going after the one it has chosen -- see
// port/pursuer.h.

#include "port/pursuer.h"

#include "port/coverage.h"

static uint16_t field(const Wram* w, const PortCpu* c, uint16_t at) {
  return wram_r16(w, (uint16_t)(c->d + at));
}

static void set_field(Wram* w, const PortCpu* c, uint16_t at, uint16_t v) {
  wram_w16(w, (uint16_t)(c->d + at), v);
}

// Is the way clear to `x`, `y`? Ground first, and then things.
static bool clear_to(Wram* w, PortCpu* c, PursuerWork* k, uint16_t x,
                     uint16_t y) {
  TerrainRegs* ground = &k->ground[k->grounds++];
  terrain_blocked_enemy(w, x, y, ground);
  c->a = ground->a;
  c->x = ground->x;
  c->y = ground->y;
  set_c(c, ground->blocked);
  k->overflow_unknown = true;
  if (ground->blocked) {
    PORT_COVER(pursuer_ground_in_the_way);
    k->blocks[PU_TAKEN]++;
    return false;
  }

  AtPointRegs body;
  actor_at_point_counted(w, field(w, c, PURSUER_DP_RECORD), x, y, &body,
                         &k->body[k->bodies++]);
  c->a = body.a;
  c->x = body.x;
  c->y = body.y;
  set_c(c, body.found);
  if (body.v_set) {
    set_v(c, body.v);
    k->overflow_unknown = false;
  }
  k->blocks[PU_BODY]++;
  if (body.found) {
    PORT_COVER(pursuer_thing_in_the_way);
    k->blocks[PU_TAKEN]++;
    return false;
  }
  return true;
}

void pursuer_step(Wram* w, const Rom* rom, PortCpu* c, PursuerWork* k) {
  c->a = field(w, c, PURSUER_DP_COUNT);
  cmp16(c, c->a, PURSUER_DONE);
  k->blocks[PU_HEAD]++;
  if (c->a == PURSUER_DONE) {
    PORT_COVER(pursuer_done);
    k->blocks[PU_TAKEN]++;
    set_field(w, c, PURSUER_DP_LEFT,
              (uint16_t)(field(w, c, PURSUER_DP_LEFT) - 1));
    set_nz16(c, field(w, c, PURSUER_DP_LEFT));
    set_field(w, c, PURSUER_DP_FLAG, 0);
    k->blocks[PU_DONE]++;
    c->pc = PURSUER_DONE_RTS_PC;
    return;
  }

  const uint16_t record = field(w, c, PURSUER_DP_RECORD);
  const uint16_t whom = field(w, c, PURSUER_DP_WHOM);
  ActorBearingRegs* bearing = &k->bearing;
  actor_snap_to(w, record, whom, &k->snap);
  actor_bearing(w, rom, record, whom, bearing);
  k->faced = true;
  k->overflow_unknown = true;
  c->a = bearing->a;
  c->x = c->a;
  c->y = whom;
  set_c(c, bearing->c);
  set_nz16(c, c->a);
  set_field(w, c, PURSUER_DP_WAY, c->a);
  k->blocks[PU_FACE]++;
  c->pc = PURSUER_RTS_PC;
  if (c->a == 0) {
    PORT_COVER(pursuer_on_top_of_them);
    k->blocks[PU_TAKEN]++;
    return;
  }

  c->x = asl16(c, asl16(c, c->a));
  set_c(c, false);
  const uint16_t x = adc16(c, wram_r16(w, (uint16_t)(record + ACTOR_X)),
                           rom_word(rom, PURSUER_STEPS + c->x));
  set_field(w, c, PURSUER_DP_TRY_X, x);
  set_c(c, false);
  const uint16_t y = adc16(c, wram_r16(w, (uint16_t)(record + ACTOR_Y)),
                           rom_word(rom, PURSUER_STEPS + 2 + c->x));
  set_field(w, c, PURSUER_DP_TRY_Y, y);
  k->blocks[PU_AIM]++;
  if (clear_to(w, c, k, x, field(w, c, PURSUER_DP_Y))) {
    PORT_COVER(pursuer_went_across);
    set_field(w, c, PURSUER_DP_X, x);
    k->blocks[PU_TAKE]++;
  }
  k->blocks[PU_GROUND]++;
  if (clear_to(w, c, k, field(w, c, PURSUER_DP_X), y)) {
    PORT_COVER(pursuer_went_down);
    set_field(w, c, PURSUER_DP_Y, y);
    k->blocks[PU_TAKE]++;
  }

  c->y = record;
  wram_w16(w, (uint16_t)(record + ACTOR_X), field(w, c, PURSUER_DP_X));
  c->a = field(w, c, PURSUER_DP_Y);
  set_nz16(c, c->a);
  wram_w16(w, (uint16_t)(record + ACTOR_Y), c->a);
  k->blocks[PU_PUT]++;
}
