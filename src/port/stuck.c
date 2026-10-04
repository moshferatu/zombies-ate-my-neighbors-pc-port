// $80:D468  a player stuck fast -- see port/stuck.h.

#include "port/stuck.h"

#include <stddef.h>

#include "port/coverage.h"

// The pad as the frame read it, a word for each player.
#define PAD_BUTTONS 0x006eu
#define PAD_DIRECTION 0x0072u

// Directions are doubled: 2 is up, and round clockwise in twos.
#define DIRECTION_RIGHT 0x0006
#define DIRECTION_LEFT 0x000e

#define SHAKES_TO_GET_FREE 3
#define HIT_EVENT 0x8001u
#define HURT_EVERY 0x00b0

// A display record's flags and its picture.
#define RECORD_FLAGS 0x00
#define RECORD_PICTURE 0x08
#define RECORD_TOP_BIT 0x8000u  // set on the cover, whatever the player has

// `$80:D4E5`: the cover's two pictures, by the walk cycle's second bit.
#define STUCK_PICTURES 0xd4e5u
#define STUCK_PICTURE_BIT 0x0002

static uint16_t field(const Wram* w, uint16_t page, uint16_t at) {
  return wram_r16(w, (uint16_t)(page + at));
}

static void set_field(Wram* w, uint16_t page, uint16_t at, uint16_t v) {
  wram_w16(w, (uint16_t)(page + at), v);
}

static uint16_t direction_held(const Wram* w, uint16_t page) {
  return wram_r16(w, (uint16_t)(PAD_DIRECTION + field(w, page, STUCK_DP_PLAYER)));
}

// Right after left, or left after right.
static bool is_shake(uint16_t held, uint16_t held_before) {
  return (held == DIRECTION_RIGHT && held_before == DIRECTION_LEFT) ||
         (held == DIRECTION_LEFT && held_before == DIRECTION_RIGHT);
}

// How long is left once this frame's shake is counted.
static uint16_t frames_left_after_shaking(const Wram* w, uint16_t page) {
  uint16_t shakes = field(w, page, STUCK_DP_SHAKES);
  if (is_shake(direction_held(w, page), field(w, page, STUCK_DP_HELD))) {
    shakes++;
  }
  return shakes == SHAKES_TO_GET_FREE ? 1
                                      : field(w, page, STUCK_DP_FRAMES_LEFT);
}

bool stuck_supported(const Wram* w, uint16_t page) {
  return frames_left_after_shaking(w, page) != 1;
}

// The slime goes on hurting for as long as it holds.
static void hurt_again(Wram* w, uint16_t page, StuckLog* log) {
  if ((field(w, page, STUCK_DP_HURT_TIMER) & 0x8000u) == 0) return;
  PORT_COVER(stuck_hurt);
  log->hurt = true;
  set_field(w, page, STUCK_DP_EVENT, HIT_EVENT);
  set_field(w, page, STUCK_DP_HURT_TIMER, HURT_EVERY);
}

// What covers the player, drawn as they are and wobbling.
static void show_cover(Wram* w, const Rom* rom, uint16_t page) {
  const uint16_t cover = field(w, page, STUCK_DP_COVER);
  const uint16_t record = field(w, page, STUCK_DP_RECORD);
  const uint16_t which = field(w, page, STUCK_DP_CYCLE) & STUCK_PICTURE_BIT;
  wram_w16(w, (uint16_t)(cover + RECORD_FLAGS),
           wram_r16(w, (uint16_t)(record + RECORD_FLAGS)) | RECORD_TOP_BIT);
  wram_w16(w, (uint16_t)(cover + RECORD_PICTURE),
           rom_word(rom, ((uint32_t)STUCK_BANK << 16) +
                             (uint16_t)(STUCK_PICTURES + which)));
}

static void count_shake(Wram* w, uint16_t page, uint16_t held, StuckLog* log) {
  log->held_right = held == DIRECTION_RIGHT;
  log->held_left = held == DIRECTION_LEFT;
  if (is_shake(held, field(w, page, STUCK_DP_HELD))) {
    PORT_COVER(stuck_shook);
    log->shook = true;
    set_field(w, page, STUCK_DP_SHAKES,
              (uint16_t)(field(w, page, STUCK_DP_SHAKES) + 1));
  }
  const uint16_t shakes = field(w, page, STUCK_DP_SHAKES);
  log->c = shakes >= SHAKES_TO_GET_FREE;
  if (shakes == SHAKES_TO_GET_FREE) {
    log->third_shake = true;
    set_field(w, page, STUCK_DP_FRAMES_LEFT, 1);
  }
}

// The buttons do nothing, but the player still turns to face what is held.
static void turn(Wram* w, uint16_t page, uint16_t held, StuckLog* log) {
  set_field(w, page, STUCK_DP_BUTTONS,
            wram_r16(w, (uint16_t)(PAD_BUTTONS +
                                   field(w, page, STUCK_DP_PLAYER))));
  set_field(w, page, STUCK_DP_HELD, held);
  if (held == 0) return;
  PORT_COVER(stuck_turned);
  log->turned = true;
  set_field(w, page, STUCK_DP_FACING, held);
}

static void run_pose_timer(Wram* w, uint16_t page, StuckLog* log) {
  const uint16_t timer = field(w, page, STUCK_DP_POSE_TIMER);
  if (timer == 0) return;
  log->timer_ran = true;
  set_field(w, page, STUCK_DP_POSE_TIMER, (uint16_t)(timer - 1));
}

// A frame nearer the end. The frame that reaches it is the ROM's, so this
// one never does.
static void count_down(Wram* w, uint16_t page, StuckLog* log) {
  const uint16_t left = field(w, page, STUCK_DP_FRAMES_LEFT);
  log->a = left;
  if (left == 0) {
    log->z = true;
    return;
  }
  log->counting = true;
  set_field(w, page, STUCK_DP_FRAMES_LEFT, (uint16_t)(left - 1));
  log->n = ((left - 1) & 0x8000u) != 0;
  log->z = left == 1;
}

void stuck(Wram* w, const Rom* rom, uint16_t page, StuckLog* log) {
  StuckLog scratch;
  if (log == NULL) log = &scratch;
  *log = (StuckLog){0};
  PORT_COVER(stuck_frame);

  hurt_again(w, page, log);
  set_field(w, page, STUCK_DP_FIRE_A, 0);
  set_field(w, page, STUCK_DP_FIRE_B, 0);
  show_cover(w, rom, page);

  const uint16_t held = direction_held(w, page);
  count_shake(w, page, held, log);
  turn(w, page, held, log);
  run_pose_timer(w, page, log);
  count_down(w, page, log);

  log->x = field(w, page, STUCK_DP_PLAYER);
  log->y = field(w, page, STUCK_DP_COVER);
}
