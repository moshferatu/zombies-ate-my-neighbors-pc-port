// $80:E3D6  a player carried along -- see port/carried.h.

#include "port/carried.h"

#include "port/coverage.h"

static uint16_t field(const Wram* w, const PortCpu* c, uint16_t at) {
  return wram_r16(w, (uint16_t)(c->d + at));
}

static void set_field(Wram* w, const PortCpu* c, uint16_t at, uint16_t v) {
  wram_w16(w, (uint16_t)(c->d + at), v);
}

// May they be at `x`, `y`? Each test leaves its own registers.
static bool stopped(Wram* w, PortCpu* c, CarriedWork* k, uint16_t x,
                    uint16_t y) {
  terrain_blocked(w, x, y, &k->ground);
  c->x = k->ground.x;
  c->y = k->ground.y;
  k->blocks[CA_TRY]++;
  if (k->ground.blocked) {
    PORT_COVER(carried_met_ground);
    return true;
  }

  ObstacleRegs thing;
  actor_obstacle_at_point_counted(w, field(w, c, CARRIED_DP_RECORD), x, y,
                                  &thing, &k->thing);
  k->asked_thing = true;
  c->x = thing.x;
  c->y = thing.y;
  k->blocks[CA_THING]++;
  if (thing.blocked) {
    PORT_COVER(carried_met_a_thing);
    return true;
  }

  step_tether_blocked(w, x, y, &k->leash);
  k->asked_leash = true;
  c->x = k->leash.x;
  c->y = k->leash.y;
  k->blocks[CA_LEASH]++;
  if (k->leash.blocked) {
    PORT_COVER(carried_past_the_leash);
    return true;
  }

  terrain_out_of_bounds(w, x, y, &k->edge);
  k->asked_edge = true;
  c->x = x;
  c->y = y;
  k->blocks[CA_EDGE]++;
  if (k->edge.c) {
    PORT_COVER(carried_off_the_level);
    return true;
  }

  const uint16_t left = (uint16_t)(field(w, c, CARRIED_DP_FRAMES_LEFT) - 1);
  set_field(w, c, CARRIED_DP_FRAMES_LEFT, left);
  k->blocks[CA_COUNT]++;
  if (left & 0x8000u) {
    PORT_COVER(carried_ran_out);
    return true;
  }
  return false;
}

void carried_frame(Wram* w, PortCpu* c, CarriedWork* k) {
  const uint16_t x = (uint16_t)(field(w, c, CARRIED_DP_SPEED_X) +
                                field(w, c, CARRIED_DP_X));
  set_field(w, c, CARRIED_DP_TRY_X, x);
  const uint16_t y = (uint16_t)(field(w, c, CARRIED_DP_SPEED_Y) +
                                field(w, c, CARRIED_DP_Y));
  set_field(w, c, CARRIED_DP_TRY_Y, y);

  if (stopped(w, c, k, x, y)) {
    k->blocks[CA_TAKEN]++;
    // About face: the way opposite, of the sixteen kept at twice and two on.
    set_c(c, false);
    const uint16_t about =
        adc16(c, (uint16_t)(field(w, c, CARRIED_DP_FACING) - 2), 8);
    set_field(w, c, CARRIED_DP_FACING, (uint16_t)((about & 0x000fu) + 2));
    c->x = field(w, c, CARRIED_DP_PLAYER);
    c->a = wram_r16(w, (uint16_t)(W_CARRIED_PAD_DIR + c->x));
    set_nz16(c, c->a);
    set_field(w, c, CARRIED_DP_DIR, c->a);
    set_c(c, true);
    k->blocks[CA_STOP]++;
    k->blocks[CA_TAIL]++;
    c->pc = CARRIED_STOPPED_PC;
    return;
  }

  PORT_COVER(carried_on);
  c->y = field(w, c, CARRIED_DP_RECORD);
  set_field(w, c, CARRIED_DP_X, x);
  wram_w16(w, (uint16_t)(c->y + ACTOR_X), x);
  set_field(w, c, CARRIED_DP_Y, y);
  wram_w16(w, (uint16_t)(c->y + ACTOR_Y), y);
  set_c(c, false);
  k->overflow_unknown = true;
  k->blocks[CA_PUT]++;
  k->blocks[CA_TAIL]++;
  k->blocks[CA_TAKEN]++;
  c->a = 1;
  set_nz16(c, c->a);
  k->blocks[CA_AGAIN]++;
  c->pc = CARRIED_SLEEP_PC;
}
