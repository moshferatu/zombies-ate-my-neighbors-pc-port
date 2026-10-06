// $81:F823  a thing sent off one way -- see port/bolt.h.

#include "port/bolt.h"

#include "port/coverage.h"
#include "port/oam.h"  // the display record's fields

static uint16_t field(const Wram* w, const PortCpu* c, uint16_t at) {
  return wram_r16(w, (uint16_t)(c->d + at));
}

static void set_field(Wram* w, const PortCpu* c, uint16_t at, uint16_t v) {
  wram_w16(w, (uint16_t)(c->d + at), v);
}

// `$81:F883`: the record stops touching things, the load it added comes
// off, and the record is the ROM's to free.
static void end(Wram* w, PortCpu* c, BoltWork* k) {
  c->y = field(w, c, BOLT_DP_RECORD);
  wram_w16(w, (uint16_t)(c->y + ACTOR_COLLIDE_ID), 0);
  set_c(c, true);
  wram_w16(w, W_SPAWN_LOAD,
           sbc16(c, wram_r16(w, W_SPAWN_LOAD), BOLT_BUDGET));
  c->a = c->y;
  set_nz16(c, c->a);
  k->blocks[BO_END]++;
  c->pc = BOLT_FREE_PC;
}

// May it be where it is? Not past the leash, and not on ground that stops
// it. Each test leaves its own registers.
static bool stopped(Wram* w, PortCpu* c, BoltWork* k) {
  const uint16_t record = field(w, c, BOLT_DP_RECORD);
  const uint16_t x = wram_r16(w, (uint16_t)(record + ACTOR_X));
  const uint16_t y = wram_r16(w, (uint16_t)(record + ACTOR_Y));

  step_tether_blocked(w, x, y, &k->leash);
  k->leashed = true;
  c->x = k->leash.x;
  k->blocks[BO_LEASH]++;
  if (k->leash.blocked) {
    PORT_COVER(bolt_past_the_leash);
    return true;
  }

  terrain_point_bit2(w, x, y, &k->ground);
  k->tested = true;
  c->x = k->ground.x;
  k->blocks[BO_GROUND]++;
  if (k->ground.blocked) {
    PORT_COVER(bolt_met_ground);
    return true;
  }
  return false;
}

void bolt_frame(Wram* w, const Rom* rom, PortCpu* c, BoltWork* k) {
  k->blocks[BO_HEAD]++;
  if (field(w, c, BOLT_DP_TOLD) != 0) {
    PORT_COVER(bolt_told);
    k->blocks[BO_TAKEN]++;
    end(w, c, k);
    return;
  }
  const uint16_t frames_left =
      (uint16_t)(field(w, c, BOLT_DP_FRAMES_LEFT) - 1);
  set_field(w, c, BOLT_DP_FRAMES_LEFT, frames_left);
  k->blocks[BO_COUNT]++;
  if (frames_left & 0x8000u) {
    PORT_COVER(bolt_ran_out);
    k->blocks[BO_TAKEN]++;
    end(w, c, k);
    return;
  }
  if (stopped(w, c, k)) {
    k->blocks[BO_TAKEN]++;
    end(w, c, k);
    return;
  }

  // A step the way it goes.
  c->y = field(w, c, BOLT_DP_RECORD);
  const uint16_t way = field(w, c, BOLT_DP_WAY);
  c->x = asl16(c, way);
  set_c(c, false);
  wram_w16(w, (uint16_t)(c->y + ACTOR_X),
           adc16(c, rom_word(rom, BOLT_STEPS + c->x),
                 wram_r16(w, (uint16_t)(c->y + ACTOR_X))));
  set_c(c, false);
  wram_w16(w, (uint16_t)(c->y + ACTOR_Y),
           adc16(c, rom_word(rom, BOLT_STEPS + 2 + c->x),
                 wram_r16(w, (uint16_t)(c->y + ACTOR_Y))));
  const uint16_t picture_left =
      (uint16_t)(field(w, c, BOLT_DP_PICTURE_LEFT) - 1);
  set_field(w, c, BOLT_DP_PICTURE_LEFT, picture_left);
  k->blocks[BO_MOVE]++;
  if (!(picture_left & 0x8000u)) {
    PORT_COVER(bolt_flew);
    k->blocks[BO_TAKEN]++;
  } else {
    PORT_COVER(bolt_turned_picture);
    set_field(w, c, BOLT_DP_PICTURE_LEFT, BOLT_PICTURE_FRAMES);
    const uint16_t which = (uint16_t)((field(w, c, BOLT_DP_WHICH) + 1) & 1);
    set_field(w, c, BOLT_DP_WHICH, which);
    set_c(c, false);
    c->x = asl16(c, adc16(c, which, way));
    wram_w16(w, (uint16_t)(c->y + ACTOR_META),
             rom_word(rom, BOLT_PICTURES + c->x));
    k->blocks[BO_PICTURE]++;
  }
  c->a = 1;
  set_nz16(c, c->a);
  k->blocks[BO_AGAIN]++;
  c->pc = BOLT_SLEEP_PC;
}
