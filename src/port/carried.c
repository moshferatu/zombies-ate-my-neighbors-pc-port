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

// `$80:F71E`: is the way clear for five steps out? Each step is tried as
// the frame tries one, but for things in the way and the count.
static bool way_clear(Wram* w, const Rom* rom, PortCpu* c,
                      CarriedBeginWork* k, uint16_t way) {
  const uint16_t at = (uint16_t)(way << 2);
  k->blocks[CB_CALL]++;
  set_field(w, c, CARRIED_DP_TRY_X, field(w, c, CARRIED_DP_X));
  set_field(w, c, CARRIED_DP_TRY_Y, field(w, c, CARRIED_DP_Y));
  set_field(w, c, CARRIED_DP_STEPS_LEFT, CARRIED_PROBE_LENGTH);
  set_field(w, c, CARRIED_DP_STEP_AT, at);
  k->blocks[CB_PROBE]++;

  for (;;) {
    const int i = k->steps++;
    const uint16_t x =
        (uint16_t)(field(w, c, CARRIED_DP_TRY_X) +
                   rom_word(rom, CARRIED_PROBE_STEPS + at));
    set_field(w, c, CARRIED_DP_TRY_X, x);
    const uint16_t y =
        (uint16_t)(field(w, c, CARRIED_DP_TRY_Y) +
                   rom_word(rom, CARRIED_PROBE_STEPS + at + 2));
    set_field(w, c, CARRIED_DP_TRY_Y, y);
    terrain_blocked(w, x, y, &k->ground[i]);
    k->blocks[CB_STEP]++;
    if (k->ground[i].blocked) break;
    step_tether_blocked(w, x, y, &k->leash[i]);
    k->asked_leash[i] = true;
    k->blocks[CB_LEASH]++;
    if (k->leash[i].blocked) break;
    terrain_out_of_bounds(w, x, y, &k->edge[i]);
    k->asked_edge[i] = true;
    k->blocks[CB_EDGE]++;
    if (k->edge[i].c) break;
    const uint16_t left =
        (uint16_t)(field(w, c, CARRIED_DP_STEPS_LEFT) - 1);
    set_field(w, c, CARRIED_DP_STEPS_LEFT, left);
    k->blocks[CB_COUNT]++;
    if (left == 0) {
      k->blocks[CB_CLEAR]++;
      return true;
    }
    k->blocks[CB_TAKEN]++;
  }
  k->blocks[CB_TAKEN]++;
  k->blocks[CB_BLOCKED]++;
  return false;
}

void carried_begin(Wram* w, const Rom* rom, PortCpu* c, CarriedBeginWork* k) {
  const uint16_t other = field(w, c, CARRIED_DP_OTHER_RECORD);
  wram_w16(w, (uint16_t)(other + ACTOR_FLAGS),
           (uint16_t)(wram_r16(w, (uint16_t)(other + ACTOR_FLAGS)) &
                      ~ACTOR_DRAW));

  // Away from the carrier. The position is kept round the question, which
  // is asked with the carrier's.
  const uint16_t x = field(w, c, CARRIED_DP_X);
  const uint16_t y = field(w, c, CARRIED_DP_Y);
  const uint16_t carrier = field(w, c, CARRIED_DP_CARRIER);
  const uint16_t record = field(w, c, CARRIED_DP_RECORD);
  const uint16_t carrier_x = wram_r16(w, (uint16_t)(carrier + ACTOR_X));
  const uint16_t carrier_y = wram_r16(w, (uint16_t)(carrier + ACTOR_Y));
  set_field(w, c, CARRIED_DP_X, carrier_x);
  set_field(w, c, CARRIED_DP_Y, carrier_y);
  k->same_row = wram_r16(w, (uint16_t)(record + ACTOR_Y)) == carrier_y;
  k->same_column = wram_r16(w, (uint16_t)(record + ACTOR_X)) == carrier_x;
  ActorBearingRegs bearing;
  actor_bearing_point(w, rom, record, carrier_x, carrier_y, &bearing);
  k->blocks[CB_HEAD]++;
  uint16_t way = (uint16_t)((bearing.a + CARRIED_WAYS / 2) &
                            (CARRIED_WAYS - 1));
  set_field(w, c, CARRIED_DP_Y, y);
  set_field(w, c, CARRIED_DP_X, x);
  set_field(w, c, CARRIED_DP_TRIES, CARRIED_WAYS);
  k->blocks[CB_TURN]++;

  for (;;) {
    const bool clear = way_clear(w, rom, c, k, way);
    k->blocks[CB_TRIED]++;
    if (clear) {
      PORT_COVER(carried_begin_way_clear);
      k->blocks[CB_TAKEN]++;
      break;
    }
    way = (uint16_t)((way & (CARRIED_WAYS - 1)) + 1);
    const uint16_t tries = (uint16_t)(field(w, c, CARRIED_DP_TRIES) - 1);
    set_field(w, c, CARRIED_DP_TRIES, tries);
    k->blocks[CB_NEXT]++;
    if (tries == 0) {
      PORT_COVER(carried_begin_boxed_in);
      break;
    }
    PORT_COVER(carried_begin_turned);
    k->blocks[CB_TAKEN]++;
  }

  // The way found, or the last tried.
  const uint16_t facing = (uint16_t)(way << 1);
  set_field(w, c, CARRIED_DP_DIR, facing);
  set_field(w, c, CARRIED_DP_FACING, facing);
  c->x = (uint16_t)(facing << 1);
  set_field(w, c, CARRIED_DP_SPEED_X, rom_word(rom, CARRIED_SPEEDS + c->x));
  set_field(w, c, CARRIED_DP_SPEED_Y,
            rom_word(rom, CARRIED_SPEEDS + c->x + 2));
  c->y = record;
  set_c(c, false);
  const uint16_t nudged_x =
      adc16(c, rom_word(rom, CARRIED_NUDGES + c->x), x);
  set_field(w, c, CARRIED_DP_X, nudged_x);
  wram_w16(w, (uint16_t)(record + ACTOR_X), nudged_x);
  set_c(c, false);
  const uint16_t nudged_y =
      adc16(c, rom_word(rom, CARRIED_NUDGES + c->x + 2), y);
  set_field(w, c, CARRIED_DP_Y, nudged_y);
  wram_w16(w, (uint16_t)(record + ACTOR_Y), nudged_y);
  set_field(w, c, CARRIED_DP_TRIES, 0);

  // The picture, and whether the record is drawn. Carry is the top bit of
  // the picture's number, which the doubling shifts out.
  const uint16_t picture =
      asl16(c, rom_word(rom, CARRIED_PICTURES + c->x));
  const uint32_t from = (((uint32_t)c->db << 16) +
                         field(w, c, CARRIED_DP_PICTURES) + picture) &
                        0xffffffu;
  wram_w16(w, (uint16_t)(record + ACTOR_META), bus_r16(w, rom, from));
  const uint16_t mask = rom_word(rom, CARRIED_PICTURES + c->x + 2);
  const uint16_t flags = wram_r16(w, (uint16_t)(record + ACTOR_FLAGS));
  k->blocks[CB_WAY]++;
  if (mask & 0x8000u) {
    PORT_COVER(carried_begin_masked);
    k->blocks[CB_TAKEN]++;
    k->blocks[CB_HIDE]++;
    wram_w16(w, (uint16_t)(record + ACTOR_FLAGS), (uint16_t)(flags & mask));
  } else {
    PORT_COVER(carried_begin_shown);
    k->blocks[CB_SHOW]++;
    wram_w16(w, (uint16_t)(record + ACTOR_FLAGS), (uint16_t)(flags | mask));
  }
  c->a = 1;
  set_nz16(c, c->a);
  k->blocks[CB_TAIL]++;
  c->pc = CARRIED_SLEEP_PC;
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
