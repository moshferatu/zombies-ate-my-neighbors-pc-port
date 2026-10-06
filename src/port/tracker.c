// $82:F054  a thing that steers after whoever is nearest -- see
// port/tracker.h.

#include "port/tracker.h"

#include "port/coverage.h"

static uint16_t field(const Wram* w, const PortCpu* c, uint16_t at) {
  return wram_r16(w, (uint16_t)(c->d + at));
}

static void set_field(Wram* w, const PortCpu* c, uint16_t at, uint16_t v) {
  wram_w16(w, (uint16_t)(c->d + at), v);
}

// `$82:F11B`: every third frame the other picture.
static void flap(Wram* w, const Rom* rom, PortCpu* c, TrackerWork* k) {
  const uint16_t left = (uint16_t)(field(w, c, TRACKER_DP_PICTURE_LEFT) - 1);
  set_field(w, c, TRACKER_DP_PICTURE_LEFT, left);
  k->blocks[TK_FLAP]++;
  if (!(left & 0x8000u)) {
    k->blocks[TK_TAKEN]++;
    return;
  }
  PORT_COVER(tracker_flapped);
  set_field(w, c, TRACKER_DP_PICTURE_LEFT, TRACKER_PICTURE_FRAMES);
  const uint16_t picture = (uint16_t)(field(w, c, TRACKER_DP_PICTURE) + 1);
  set_field(w, c, TRACKER_DP_PICTURE, picture);
  wram_w16(w, (uint16_t)(field(w, c, TRACKER_DP_RECORD) + ACTOR_META),
           rom_word(rom, TRACKER_PICTURES + ((picture & 1u) << 1)));
  k->blocks[TK_PICTURE]++;
}

// One more towards them, unless that is too fast.
static void steer(Wram* w, const Rom* rom, const PortCpu* c, TrackerWork* k,
                  uint16_t speed_at, uint32_t step_at) {
  const uint16_t speed =
      (uint16_t)(field(w, c, speed_at) + rom_word(rom, step_at));
  uint16_t size = speed;
  k->blocks[TK_STEER]++;
  if (speed & 0x8000u) {
    size = (uint16_t)(0 - speed);
    k->blocks[TK_NEGATE]++;
  } else {
    k->blocks[TK_TAKEN]++;
  }
  k->blocks[TK_LIMIT]++;
  if (size >= TRACKER_SPEED_LIMIT) {
    PORT_COVER(tracker_fast_enough);
    k->blocks[TK_TAKEN]++;
    return;
  }
  PORT_COVER(tracker_steered);
  set_field(w, c, speed_at, speed);
  k->blocks[TK_KEEP]++;
}

// `$82:F091`. True when it goes on.
static bool step(Wram* w, const Rom* rom, PortCpu* c, TrackerWork* k) {
  const uint16_t record = field(w, c, TRACKER_DP_RECORD);
  uint16_t dist = 0;
  const uint16_t whom =
      actor_nearest_counted(w, field(w, c, TRACKER_DP_X),
                            field(w, c, TRACKER_DP_Y), &dist, &k->nearest);
  actor_bearing(w, rom, record, whom, &k->bearing);
  const uint32_t steps = TRACKER_STEPS + (uint16_t)(k->bearing.a << 2);
  k->blocks[TK_LOOK]++;
  steer(w, rom, c, k, TRACKER_DP_SPEED_X, steps);
  steer(w, rom, c, k, TRACKER_DP_SPEED_Y, steps + 2);

  const uint16_t x = (uint16_t)(field(w, c, TRACKER_DP_X) +
                                field(w, c, TRACKER_DP_SPEED_X));
  set_field(w, c, TRACKER_DP_X, x);
  const uint16_t y = (uint16_t)(field(w, c, TRACKER_DP_Y) +
                                field(w, c, TRACKER_DP_SPEED_Y));
  set_field(w, c, TRACKER_DP_Y, y);
  terrain_point_bit2(w, x, y, &k->ground);
  c->x = k->ground.x;
  c->y = k->ground.y;
  set_c(c, k->ground.blocked);
  k->blocks[TK_MOVE]++;
  if (k->ground.blocked) {
    PORT_COVER(tracker_met_ground);
    k->blocks[TK_TAKEN]++;
    return false;
  }

  c->y = record;
  wram_w16(w, (uint16_t)(record + ACTOR_X), x);
  wram_w16(w, (uint16_t)(record + ACTOR_Y), y);
  const uint16_t left = (uint16_t)(field(w, c, TRACKER_DP_FRAMES_LEFT) - 1);
  set_field(w, c, TRACKER_DP_FRAMES_LEFT, left);
  k->blocks[TK_PUT]++;
  if (left & 0x8000u) {
    PORT_COVER(tracker_ran_out);
    k->blocks[TK_TAKEN]++;
    return false;
  }
  PORT_COVER(tracker_flew);
  k->blocks[TK_RTS]++;
  return true;
}

void tracker_frame(Wram* w, const Rom* rom, PortCpu* c, TrackerWork* k) {
  flap(w, rom, c, k);
  if (!step(w, rom, c, k)) {
    set_field(w, c, TRACKER_DP_ENDED,
              (uint16_t)(field(w, c, TRACKER_DP_ENDED) - 1));
    k->blocks[TK_STOP]++;
  }
  c->a = field(w, c, TRACKER_DP_ENDED);
  k->blocks[TK_TAIL]++;
  if (c->a != 0) {
    PORT_COVER(tracker_ended);
    set_nz16(c, c->a);
    c->pc = TRACKER_ENDED_PC;
    return;
  }
  k->blocks[TK_TAKEN]++;
  c->a = 1;
  set_nz16(c, c->a);
  k->blocks[TK_AGAIN]++;
  c->pc = TRACKER_SLEEP_PC;
}
