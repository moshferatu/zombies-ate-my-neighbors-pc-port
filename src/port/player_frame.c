// $80:CDFE  a frame of a player -- see port/player_frame.h.

#include "port/player_frame.h"

#include <stddef.h>

#include "port/bodies.h"  // W_NEIGHBOURS_LEFT, W_PLAYER_HEALTH
#include "port/coverage.h"
#include "port/step.h"

typedef struct {
  Wram* w;
  const Rom* rom;
  uint16_t page;  // the player's direct page
  PlayerFrameLog* log;
} Frame;

static uint16_t field(const Frame* f, uint16_t at) {
  return wram_r16(f->w, (uint16_t)(f->page + at));
}

static void set_field(Frame* f, uint16_t at, uint16_t v) {
  wram_w16(f->w, (uint16_t)(f->page + at), v);
}

// Each step answers whether it was the port's. A frame stops at the first
// that was not, and `player_frame_supported` turns that frame down.

// The ordinary state reads the pad. A press that changes the weapon or the
// item or uses the item, and a press of a shoulder button, are the ROM's.
static bool ordinary_state(Frame* f) {
  PlayerStateRegs* did = &f->log->normal;
  if (!player_state_normal_supported(f->w, f->page)) return false;
  player_state_normal(f->w, f->rom, f->page, did);
  for (int button = 0; button < PSN_PRESS_COUNT; button++)
    if (did->press[button] == PSN_PRESS_EDGE) return false;
  f->log->c = did->c;
  return true;
}

// A stuck player is still standing on something, and it acts on them first.
static bool stuck_state(Frame* f) {
  if (!stuck_supported(f->w, f->page)) return false;
  floor_effect(f->w, f->rom, f->page, &f->log->floor);
  stuck(f->w, f->rom, f->page, &f->log->stuck);
  f->log->c = f->log->stuck.c;
  return true;
}

static bool run_state(Frame* f) {
  f->log->state = field(f, PLAYER_DP_STATE);
  switch (f->log->state) {
    case PLAYER_STATE_NORMAL:
      PORT_COVER(player_frame_normal);
      return ordinary_state(f);
    case PLAYER_STATE_STUCK:
      PORT_COVER(player_frame_stuck);
      return stuck_state(f);
    default:
      return false;
  }
}

// The hurt timer runs down to minus one and stays there. Telling the player
// of a hit is the ROM's.
static bool run_hurt_timer(Frame* f) {
  const uint16_t event = field(f, PLAYER_DP_HIT_EVENT);
  if (event & 0x8000u) return false;
  f->log->v = (event & 0x4000u) != 0;

  if (field(f, PLAYER_DP_HURT_HELD) != 0) {
    f->log->hurt_timer = PLAYER_HURT_TIMER_HELD;
    return true;
  }
  const uint16_t left = (uint16_t)(field(f, PLAYER_DP_HURT_TIMER) - 1);
  if (left & 0x8000u) {
    PORT_COVER(player_frame_hurt_timer_out);
    f->log->hurt_timer = PLAYER_HURT_TIMER_OUT;
    set_field(f, PLAYER_DP_HURT_TIMER, 0xffff);
  } else {
    PORT_COVER(player_frame_hurt_timer_running);
    f->log->hurt_timer = PLAYER_HURT_TIMER_RUNNING;
    set_field(f, PLAYER_DP_HURT_TIMER, left);
  }
  return true;
}

static bool strike_pose(Frame* f) {
  PoseLog* did = &f->log->pose_log;
  f->log->pose = field(f, PLAYER_DP_POSE);
  switch (f->log->pose) {
    case POSE_HANDLER_STAND:
      pose_stand(f->w, f->rom, f->page, did);
      break;
    case POSE_HANDLER_WALK:
      pose_walk(f->w, f->rom, f->page, did);
      break;
    case POSE_HANDLER_WALK_FIRING:
      pose_walk_firing(f->w, f->rom, f->page, did);
      break;
    default:
      return false;
  }
  if (did->c_set) f->log->c = did->c;
  if (did->v_set) f->log->v = did->v;
  return !did->unported;
}

static bool move(Frame* f) {
  const uint16_t movement = field(f, PLAYER_DP_MOVEMENT);
  if (movement == 0) {
    PORT_COVER(player_frame_still);
    return true;
  }
  if (movement != PLAYER_MOVEMENT_WALK) return false;
  PORT_COVER(player_frame_walked);
  if (!player_walk_checked(f->w, f->rom, f->page, &f->log->walk)) return false;
  f->log->walked = true;
  f->log->c = f->log->walk.last_yes;
  f->log->v = f->log->walk.overflow;
  return true;
}

static void publish_position(Frame* f) {
  PublishRegs unused;
  f->log->two_part = field(f, PUBLISH_DP_TWO_PART) != 0;
  actor_publish_pos(f->w, f->page, 0, 0, &unused);
}

// Ending the level and dying are both the ROM's.
static bool level_goes_on(const Frame* f) {
  return wram_r16(f->w, W_NEIGHBOURS_LEFT) != 0;
}

static bool alive(const Frame* f) {
  const uint16_t player = field(f, PLAYER_DP_INDEX);
  return wram_r16(f->w, (uint16_t)(W_PLAYER_HEALTH + player)) != 0;
}

void player_frame(Wram* w, const Rom* rom, uint16_t page, PlayerFrameLog* log) {
  PlayerFrameLog scratch;
  if (log == NULL) log = &scratch;
  *log = (PlayerFrameLog){0};
  Frame f = {w, rom, page, log};

  log->unported = true;
  if (!run_state(&f)) return;
  if (!run_hurt_timer(&f)) return;
  if (!strike_pose(&f)) return;
  if (!move(&f)) return;
  publish_position(&f);
  set_field(&f, PLAYER_DP_LAST_BUTTONS, field(&f, PLAYER_DP_BUTTONS));
  if (!level_goes_on(&f) || !alive(&f)) return;
  log->unported = false;
}

bool player_frame_supported(Wram* w, const Rom* rom, uint16_t page) {
  // A frontend's request for the next weapon or item is taken by the ordinary
  // state, once. A frame with one waiting is left to the ROM's pieces, so
  // that finding out here does not take it.
  const uint16_t player = wram_r16(w, (uint16_t)(page + PLAYER_DP_INDEX));
  if (player_cycle_pending(player, PSN_CYCLE_WEAPON) != 0 ||
      player_cycle_pending(player, PSN_CYCLE_ITEM) != 0)
    return false;
  PlayerFrameLog log;
  player_frame(w, rom, page, &log);
  return !log.unported;
}
