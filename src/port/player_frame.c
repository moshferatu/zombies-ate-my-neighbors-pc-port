// $80:CDFE  a frame of a player -- see port/player_frame.h.

#include "port/player_frame.h"

#include <stddef.h>

#include "port/bodies.h"  // W_NEIGHBOURS_LEFT, W_PLAYER_HEALTH
#include "port/coverage.h"
#include "port/frontend.h"  // W_FRAME_COUNT
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
  f->log->c_set = true;
  return true;
}

// `$80:D343`, which two states share. No weapon is out, and the pad only
// turns the player. The pose timer still counts down.
static bool turning_state(Frame* f) {
  set_field(f, PSN_DP_FIRE_A, 0);
  set_field(f, PSN_DP_FIRE_B, 0);
  const uint16_t player = field(f, PLAYER_DP_INDEX);
  set_field(f, PLAYER_DP_BUTTONS,
            wram_r16(f->w, (uint16_t)(W_JOY_RAW + player)));
  const uint16_t held = wram_r16(f->w, (uint16_t)(W_JOY_DIR + player));
  set_field(f, PSN_DP_DIR, held);
  if (held != 0) {
    f->log->turned = true;
    set_field(f, PSN_DP_DIR_HELD, held);
  }
  const uint16_t timer = field(f, POSE_DP_TIMER);
  if (timer != 0) {
    f->log->timer_ran = true;
    set_field(f, POSE_DP_TIMER, (uint16_t)(timer - 1));
  }
  return true;
}

// `$80:D2EA`, the monster the potion makes. The ground acts on it as on
// anyone. It has no weapon: the pad turns it, and is kept when a button to
// punch with is down, for the pose to see. The potion wearing off is the
// ROM's.
static bool monster_state(Frame* f) {
  floor_effect(f->w, f->rom, f->page, &f->log->floor);
  f->log->c = f->log->floor.c;
  f->log->c_set = true;

  const uint16_t player = field(f, PLAYER_DP_INDEX);
  const uint16_t pad = wram_r16(f->w, (uint16_t)(W_JOY_RAW + player));
  set_field(f, PLAYER_DP_BUTTONS, pad);
  f->log->punched = (pad & MONSTER_PUNCH_BUTTONS) != 0;
  set_field(f, MONSTER_DP_PUNCH, f->log->punched ? pad : 0);

  const uint16_t held = wram_r16(f->w, (uint16_t)(W_JOY_DIR + player));
  set_field(f, PSN_DP_DIR, held);
  if (held != 0) {
    f->log->turned = true;
    set_field(f, PSN_DP_DIR_HELD, held);
  }
  const uint16_t timer = field(f, POSE_DP_TIMER);
  if (timer != 0) {
    f->log->timer_ran = true;
    set_field(f, POSE_DP_TIMER, (uint16_t)(timer - 1));
  }
  const uint16_t left = field(f, MONSTER_DP_FRAMES_LEFT);
  if (left != 0) {
    f->log->potion_ran = true;
    set_field(f, MONSTER_DP_FRAMES_LEFT, (uint16_t)(left - 1));
    if (left == 1) return false;
  }
  return true;
}

// `$80:D404`: a player who flashes and cannot be hurt. Under it runs the
// ordinary state, or the turning one. Then everything within eight pixels is
// told the player is there, the hurt timer is held at two, and the picture
// is flipped on and off, a frame each. The frame it ends on is the ROM's.
static bool flashing_state(Frame* f) {
  if (field(f, FLASHING_DP_UNDER) == FLASHING_UNDER_TURNING) {
    PORT_COVER(player_frame_flashing_turning);
    f->log->under_turning = true;
    turning_state(f);
  } else {
    PORT_COVER(player_frame_flashing_normal);
    if (!ordinary_state(f)) return false;
  }

  const uint16_t left = (uint16_t)(field(f, STEP_DP_X) - FLASHING_REACH);
  const uint16_t top = (uint16_t)(field(f, STEP_DP_Y) - FLASHING_REACH);
  const uint16_t id =
      rom_word(f->rom, FLASHING_IDS + field(f, PLAYER_DP_INDEX));
  wram_w16(f->w, W_BOX_LEFT, left);
  wram_w16(f->w, W_BOX_RIGHT, (uint16_t)(left + 2 * FLASHING_REACH));
  wram_w16(f->w, W_BOX_TOP, top);
  wram_w16(f->w, W_BOX_BOTTOM, (uint16_t)(top + 2 * FLASHING_REACH));
  wram_w16(f->w, W_BOX_ID, id);
  // The carry it is called with is the last add's.
  const bool carry = top > 0xffffu - 2 * FLASHING_REACH;
  ThreadCallResult tail = {.c = carry};
  ActorNotifyRegs told;
  if (!actor_notify_box_counted(f->w, f->rom, id, carry, &tail, &told,
                                &f->log->told))
    return false;
  f->log->c = told.c;
  f->log->c_set = true;

  set_field(f, PLAYER_DP_HURT_TIMER, FLASHING_HURT_TIMER);
  if ((wram_r16(f->w, W_FRAME_COUNT) & 1) == 0) {
    f->log->flickered = true;
    const uint16_t record = field(f, POSE_DP_RECORD);
    wram_w16(f->w, (uint16_t)(record + ACTOR_FLAGS),
             (uint16_t)(wram_r16(f->w, (uint16_t)(record + ACTOR_FLAGS)) ^
                        0x8000u));
  }
  const uint16_t frames =
      (uint16_t)(field(f, FLASHING_DP_FRAMES_LEFT) - 1);
  set_field(f, FLASHING_DP_FRAMES_LEFT, frames);
  return (frames & 0x8000u) == 0;
}

// A stuck player is still standing on something, and it acts on them first.
static bool stuck_state(Frame* f) {
  floor_effect(f->w, f->rom, f->page, &f->log->floor);
  stuck(f->w, f->rom, f->page, &f->log->stuck);
  f->log->c = f->log->stuck.c;
  f->log->c_set = true;
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
    case PLAYER_STATE_TURNING:
    case PLAYER_STATE_TURNING_B:
      PORT_COVER(player_frame_turning);
      return turning_state(f);
    case PLAYER_STATE_MONSTER:
      PORT_COVER(player_frame_monster);
      return monster_state(f);
    case PLAYER_STATE_FLASHING:
      return flashing_state(f);
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
    case POSE_HANDLER_WALK_BAND_B:
      pose_walk_band_b(f->w, f->rom, f->page, did);
      break;
    case POSE_HANDLER_WALK_6C:
      pose_walk_6c(f->w, f->rom, f->page, did);
      break;
    case POSE_HANDLER_ARC:
    case POSE_HANDLER_ARC_B:
      pose_arc(f->w, f->rom, f->page, did);
      break;
    case POSE_HANDLER_ARC_READY:
      pose_arc_ready(f->w, f->rom, f->page, did);
      break;
    case POSE_HANDLER_BOUNCE:
      pose_bounce(f->w, f->rom, f->page, did);
      break;
    case POSE_HANDLER_BOUNCE_WAIT:
      pose_bounce_wait(f->w, f->rom, f->page, did);
      break;
    case POSE_HANDLER_BOUNCE_OFF:
      pose_bounce_off(f->w, f->rom, f->page, did);
      break;
    case POSE_HANDLER_SWIM:
      pose_swim(f->w, f->rom, f->page, did);
      break;
    default:
      return false;
  }
  if (did->c_set) {
    f->log->c = did->c;
    f->log->c_set = true;
  }
  if (did->v_set) f->log->v = did->v;
  return !did->unported;
}

static bool move(Frame* f) {
  const uint16_t movement = field(f, PLAYER_DP_MOVEMENT);
  if (movement == 0) {
    // A punch leaves overflow as those it told left it, or as the look at
    // the fist's tile did, which the walk then writes. With no walk to, the
    // frame is the ROM's.
    if (f->log->pose_log.punched || f->log->pose_log.reached) return false;
    PORT_COVER(player_frame_still);
    return true;
  }
  if (movement == PLAYER_MOVEMENT_MONSTER_WALK) {
    f->log->walk_kind = WALK_OF_MONSTER;
    if (!monster_walk_checked(f->w, f->rom, f->page, &f->log->walk))
      return false;
  } else if (movement == PLAYER_MOVEMENT_STUCK_WALK) {
    f->log->walk_kind = WALK_STUCK;
    stuck_walk(f->w, f->rom, f->page, &f->log->walk);
  } else if (movement == PLAYER_MOVEMENT_SWIM) {
    f->log->walk_kind = WALK_SWIM;
    if (!swim_walk_checked(f->w, f->rom, f->page, &f->log->walk))
      return false;
  } else if (movement == PLAYER_MOVEMENT_WALK) {
    PORT_COVER(player_frame_walked);
    if (!player_walk_checked(f->w, f->rom, f->page, &f->log->walk))
      return false;
  } else {
    return false;
  }
  f->log->walked = true;
  f->log->c_set = true;
  f->log->c = f->log->walk.last_yes;
  f->log->v = f->log->walk.overflow;
  return true;
}

static void publish_position(Frame* f) {
  PublishRegs unused;
  f->log->two_part = field(f, PUBLISH_DP_TWO_PART) != 0;
  actor_publish_pos(f->w, f->page, 0, 0, &unused);
}

// A level with nobody left to rescue goes on if somebody was rescued: there
// is a door to find. With nobody rescued the game is lost, and that is the
// ROM's. So is dying.
static bool level_goes_on(const Frame* f) {
  if (wram_r16(f->w, W_NEIGHBOURS_LEFT) != 0) return true;
  if ((wram_r16(f->w, W_RESCUED) | wram_r16(f->w, W_RESCUED + 2)) == 0)
    return false;
  PORT_COVER(player_frame_nobody_left);
  f->log->nobody_left = true;
  return true;
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

bool player_frame_tried(Wram* w, const Rom* rom, uint16_t page,
                        PlayerFrameLog* log) {
  // A frontend's request for the next weapon or item is taken by the ordinary
  // state, once. A frame with one waiting is left to the ROM's pieces, so
  // that finding out here does not take it.
  const uint16_t player = wram_r16(w, (uint16_t)(page + PLAYER_DP_INDEX));
  if (player_cycle_pending(player, PSN_CYCLE_WEAPON) != 0 ||
      player_cycle_pending(player, PSN_CYCLE_ITEM) != 0)
    return false;
  player_frame(w, rom, page, log);
  return !log->unported;
}

bool player_frame_supported(Wram* w, const Rom* rom, uint16_t page) {
  PlayerFrameLog log;
  return player_frame_tried(w, rom, page, &log);
}
