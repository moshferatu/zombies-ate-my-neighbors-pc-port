// $80:DEC5  a player knocked back fifteen frames -- see port/lunge.h.

#include "port/lunge.h"

#include "port/coverage.h"

static uint16_t field(const Wram* w, const PortCpu* c, uint16_t at) {
  return wram_r16(w, (uint16_t)(c->d + at));
}

static void set_field(Wram* w, const PortCpu* c, uint16_t at, uint16_t v) {
  wram_w16(w, (uint16_t)(c->d + at), v);
}

// May they be at `x`, `y`? Each test leaves its own registers.
static bool stopped(Wram* w, PortCpu* c, LungeWork* k, uint16_t x,
                    uint16_t y) {
  terrain_blocked(w, x, y, &k->ground);
  k->tried = true;
  c->a = k->ground.a;
  c->x = k->ground.x;
  c->y = k->ground.y;
  set_c(c, k->ground.blocked);
  k->blocks[LU_TRY]++;
  if (k->ground.blocked) {
    c->a = (uint16_t)(asl16(c, c->a) & LUNGE_GROUND_MASK);
    cmp16(c, c->a, LUNGE_GROUND_ON);
    k->blocks[LU_WALL]++;
    if (c->a == LUNGE_GROUND_ON) {
      k->declined = true;
    } else {
      PORT_COVER(lunge_met_ground);
    }
    return true;
  }
  k->blocks[LU_TAKEN]++;

  step_tether_blocked(w, x, y, &k->leash);
  k->asked_leash = true;
  c->a = k->leash.a;
  c->x = k->leash.x;
  c->y = k->leash.y;
  set_c(c, k->leash.blocked);
  k->blocks[LU_TEST]++;
  if (k->leash.blocked) {
    PORT_COVER(lunge_past_the_leash);
    k->nz_unknown = true;
    return true;
  }

  for (int i = 0; i < 2; i++) {
    terrain_out_of_bounds(w, x, y, &k->edge);
    k->edges++;
    c->a = k->edge.a;
    c->x = x;
    c->y = y;
    c->p = (uint8_t)((c->p & ~(PORT_P_N | PORT_P_Z | PORT_P_C)) |
                     (k->edge.n ? PORT_P_N : 0) | (k->edge.z ? PORT_P_Z : 0) |
                     (k->edge.c ? PORT_P_C : 0));
    k->blocks[LU_TEST]++;
    if (k->edge.c) {
      PORT_COVER(lunge_off_the_level);
      return true;
    }
  }

  ObstacleRegs thing;
  actor_obstacle_at_point_counted(w, field(w, c, LUNGE_DP_RECORD), x, y,
                                  &thing, &k->thing);
  k->asked_thing = true;
  c->a = thing.a;
  c->x = thing.x;
  c->y = thing.y;
  set_c(c, thing.blocked);
  k->blocks[LU_THING]++;
  if (thing.blocked) {
    PORT_COVER(lunge_met_a_thing);
    k->nz_unknown = true;
    return true;
  }
  return false;
}

void lunge_frame(Wram* w, PortCpu* c, LungeWork* k) {
  const uint16_t left = (uint16_t)(field(w, c, LUNGE_DP_FRAMES_LEFT) - 1);
  set_field(w, c, LUNGE_DP_FRAMES_LEFT, left);
  set_nz16(c, left);
  k->blocks[LU_COUNT]++;
  c->pc = LUNGE_ENDED_PC;
  if (left == 0) {
    PORT_COVER(lunge_ran_out);
    k->blocks[LU_TAKEN]++;
    return;
  }

  const uint16_t x = (uint16_t)(field(w, c, LUNGE_DP_X) +
                                field(w, c, LUNGE_DP_STEP_X));
  set_field(w, c, LUNGE_DP_TRY_X, x);
  const uint16_t y = (uint16_t)(field(w, c, LUNGE_DP_Y) +
                                field(w, c, LUNGE_DP_STEP_Y));
  set_field(w, c, LUNGE_DP_TRY_Y, y);
  if (stopped(w, c, k, x, y)) {
    k->blocks[LU_TAKEN]++;
    return;
  }

  PORT_COVER(lunge_on);
  c->y = field(w, c, LUNGE_DP_RECORD);
  set_field(w, c, LUNGE_DP_X, x);
  wram_w16(w, (uint16_t)(c->y + ACTOR_X), x);
  set_field(w, c, LUNGE_DP_Y, y);
  wram_w16(w, (uint16_t)(c->y + ACTOR_Y), y);
  k->blocks[LU_PUT]++;
  c->a = 1;
  set_nz16(c, c->a);
  k->blocks[LU_AGAIN]++;
  c->pc = LUNGE_SLEEP_PC;
}
