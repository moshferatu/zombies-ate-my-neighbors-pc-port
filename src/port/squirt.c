// $81:FD11  the squirt gun's water in flight -- see port/squirt.h.

#include "port/squirt.h"

#include "port/oam.h"  // ACTOR_X, ACTOR_Y

static uint16_t field(const Wram* w, uint16_t page, uint16_t at) {
  return wram_r16(w, (uint16_t)(page + at));
}

static void set_field(Wram* w, uint16_t page, uint16_t at, uint16_t v) {
  wram_w16(w, (uint16_t)(page + at), v);
}

SquirtNext squirt_flight_frame(Wram* w, uint16_t page, TerrainRegs* ground) {
  // `$81:FDF7`: a step along its line, and the picture with it.
  const uint16_t record = field(w, page, SQUIRT_DP_RECORD);
  const uint16_t x = (uint16_t)(field(w, page, SQUIRT_DP_X) +
                                field(w, page, SQUIRT_DP_STEP_X));
  set_field(w, page, SQUIRT_DP_X, x);
  wram_w16(w, (uint16_t)(record + ACTOR_X), x);
  const uint16_t y = (uint16_t)(field(w, page, SQUIRT_DP_Y) +
                                field(w, page, SQUIRT_DP_STEP_Y));
  set_field(w, page, SQUIRT_DP_Y, y);
  wram_w16(w, (uint16_t)(record + ACTOR_Y), y);

  terrain_point_bit2(w, x, y, ground);
  if (ground->blocked) return SQUIRT_HIT_GROUND;

  const uint16_t left = (uint16_t)(field(w, page, SQUIRT_DP_FRAMES_LEFT) - 1);
  set_field(w, page, SQUIRT_DP_FRAMES_LEFT, left);
  return left == 0 ? SQUIRT_SPENT : SQUIRT_FLIES;
}
