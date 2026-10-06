// $83:9D2C  a thing that steps round four places -- see port/stepper.h.

#include "port/stepper.h"

#include "port/coverage.h"
#include "port/oam.h"  // the display record's fields

static uint16_t field(const Wram* w, const PortCpu* c, uint16_t at) {
  return wram_r16(w, (uint16_t)(c->d + at));
}

static void set_field(Wram* w, const PortCpu* c, uint16_t at, uint16_t v) {
  wram_w16(w, (uint16_t)(c->d + at), v);
}

// `$83:9D30`: on to the next of the four places, by its step from here.
static void step(Wram* w, const Rom* rom, PortCpu* c, StepperWork* k) {
  uint16_t place = (uint16_t)(field(w, c, STEPPER_DP_PLACE) + 1);
  cmp16(c, place, STEPPER_PLACES);
  k->blocks[ST_PLACE]++;
  if (place == STEPPER_PLACES) {
    PORT_COVER(stepper_went_round);
    place = 0;
    k->blocks[ST_ROUND]++;
  } else {
    k->blocks[ST_TAKEN]++;
  }
  set_field(w, c, STEPPER_DP_PLACE, place);
  c->x = asl16(c, asl16(c, place));
  c->y = field(w, c, STEPPER_DP_RECORD);
  set_c(c, false);
  const uint16_t x =
      adc16(c, field(w, c, STEPPER_DP_X), rom_word(rom, STEPPER_STEPS + c->x));
  set_field(w, c, STEPPER_DP_X, x);
  wram_w16(w, (uint16_t)(c->y + ACTOR_X), x);
  set_c(c, false);
  const uint16_t y = adc16(c, field(w, c, STEPPER_DP_Y),
                           rom_word(rom, STEPPER_STEPS + 2 + c->x));
  set_field(w, c, STEPPER_DP_Y, y);
  wram_w16(w, (uint16_t)(c->y + ACTOR_Y), y);
  set_field(w, c, STEPPER_DP_STEP_LEFT, STEPPER_STEP_FRAMES);
  k->blocks[ST_STEP]++;
}

// `$83:9D61`: the other picture, and how long it is up.
static void turn_picture(Wram* w, const Rom* rom, PortCpu* c, StepperWork* k) {
  const uint16_t which = (uint16_t)(field(w, c, STEPPER_DP_WHICH) ^ 1);
  set_field(w, c, STEPPER_DP_WHICH, which);
  c->x = asl16(c, asl16(c, which));
  c->y = ACTOR_META;
  wram_w16(w, (uint16_t)(field(w, c, STEPPER_DP_RECORD) + ACTOR_META),
           rom_word(rom, STEPPER_PICTURES + c->x));
  set_field(w, c, STEPPER_DP_PICTURE_LEFT,
            rom_word(rom, STEPPER_PICTURES + 2 + c->x));
  k->blocks[ST_PICTURE]++;
}

void stepper_frame(Wram* w, const Rom* rom, PortCpu* c, StepperWork* k) {
  const uint16_t step_left =
      (uint16_t)(field(w, c, STEPPER_DP_STEP_LEFT) - 1);
  set_field(w, c, STEPPER_DP_STEP_LEFT, step_left);
  k->blocks[ST_HEAD]++;
  if (step_left != 0) {
    k->blocks[ST_TAKEN]++;
  } else {
    PORT_COVER(stepper_stepped);
    step(w, rom, c, k);
  }

  const uint16_t picture_left =
      (uint16_t)(field(w, c, STEPPER_DP_PICTURE_LEFT) - 1);
  set_field(w, c, STEPPER_DP_PICTURE_LEFT, picture_left);
  k->blocks[ST_PICTURE_TEST]++;
  if (picture_left != 0) {
    k->blocks[ST_TAKEN]++;
  } else {
    PORT_COVER(stepper_turned_picture);
    turn_picture(w, rom, c, k);
  }

  c->a = field(w, c, STEPPER_DP_TOLD);
  set_nz16(c, c->a);
  k->blocks[ST_TOLD]++;
  if (c->a != 0) {
    PORT_COVER(stepper_told);
    c->pc = STEPPER_TOLD_PC;
    return;
  }
  k->blocks[ST_TAKEN]++;
  c->a = 1;
  set_nz16(c, c->a);
  k->blocks[ST_AGAIN]++;
  c->pc = STEPPER_SLEEP_PC;
}
