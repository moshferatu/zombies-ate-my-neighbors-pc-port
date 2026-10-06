// $81:B664  what a destroyed doll can leave behind -- see port/flame.h.

#include "port/flame.h"

#include <stddef.h>

#include "port/coverage.h"
#include "port/rng.h"
#include "port/squirt.h"  // W_SQUIRTS_LIVE, the budget it shares

typedef struct {
  uint16_t x, y;
} Point;

// One flame's stretch.
typedef struct {
  Wram* w;
  const Rom* rom;
  PortCpu* c;
  FlameWork* k;
  uint16_t page;
  uint16_t record;
} Flame;

static uint16_t field(const Flame* f, uint16_t at) {
  return wram_r16(f->w, (uint16_t)(f->page + at));
}

static void set_field(Flame* f, uint16_t at, uint16_t v) {
  wram_w16(f->w, (uint16_t)(f->page + at), v);
}

static uint16_t record_field(const Flame* f, uint16_t record, uint16_t at) {
  return wram_r16(f->w, (uint16_t)(record + at));
}

static void set_record_field(Flame* f, uint16_t record, uint16_t at,
                             uint16_t v) {
  wram_w16(f->w, (uint16_t)(record + at), v);
}

static Point position(const Flame* f) {
  return (Point){field(f, FLAME_DP_X), field(f, FLAME_DP_Y)};
}

static void move_to(Flame* f, Point p) {
  set_field(f, FLAME_DP_X, p.x);
  set_field(f, FLAME_DP_Y, p.y);
}

static bool negative(uint16_t v) { return (v & 0x8000u) != 0; }

// The harness prices a stretch by the runs of the ROM it stands for.
static void ran(Flame* f, int block) { f->k->blocks[block]++; }

static void lda(PortCpu* c, uint16_t v) {
  c->a = v;
  set_nz16(c, v);
}

// A word of one of the tables, read through the data bank.
static uint16_t table(const Flame* f, uint32_t at) {
  return bus_r16(f->w, f->rom, ((uint32_t)FLAME_BANK << 16) + at);
}

// `count` less one, and whether that took it below zero.
static bool count_down(Flame* f, uint16_t at) {
  const uint16_t left = (uint16_t)(field(f, at) - 1);
  set_field(f, at, left);
  return negative(left);
}

// ---------------------------------------------------------------------------
// Aiming
// ---------------------------------------------------------------------------

// `$81:B24D`, which is the doll's: from here to `$1E`/`$20`, how far and which
// way on each axis. X is left holding the way down.
static void aim(Flame* f) {
  const Point me = position(f);
  const uint16_t from[2] = {me.x, me.y};
  const uint16_t to[2] = {field(f, FLAME_DP_TO_X), field(f, FLAME_DP_TO_Y)};
  const uint16_t dist[2] = {FLAME_DP_AIM_DX, FLAME_DP_AIM_DY};
  const uint16_t step[2] = {FLAME_DP_AIM_STEP_X, FLAME_DP_AIM_STEP_Y};
  ran(f, FL_AIM);
  for (int i = 0; i < 2; i++) {
    const uint16_t gap = (uint16_t)(to[i] - from[i]);
    uint16_t way = 1;
    if (gap == 0) {
      way = 0;
      ran(f, FL_AIM_DEX);
      ran(f, FL_TAKEN);  // the `BPL`
    } else if (negative(gap)) {
      way = 0xffff;
      ran(f, FL_TAKEN);  // the `BNE`
      ran(f, FL_AIM_NEG);
    } else {
      ran(f, FL_TAKEN);
      ran(f, FL_TAKEN);
    }
    set_field(f, dist[i], negative(gap) ? (uint16_t)-gap : gap);
    set_field(f, step[i], way);
    f->c->x = way;
  }
}

// `$81:B83E`: keep the longer leg and drop the other, and face along it. The
// facings are the doll's: 0 up, 4 right, 8 down, 12 left.
static void keep_longer_leg(Flame* f) {
  uint16_t facing;
  ran(f, FL_AXIS_HEAD);
  if (field(f, FLAME_DP_AIM_DX) >= field(f, FLAME_DP_AIM_DY)) {
    PORT_COVER(flame_goes_across);
    set_field(f, FLAME_DP_AIM_STEP_Y, 0);
    ran(f, FL_TAKEN);
    ran(f, FL_AXIS_ACROSS);
  } else {
    PORT_COVER(flame_goes_down);
    set_field(f, FLAME_DP_AIM_STEP_X, 0);
    ran(f, FL_AXIS_DOWN);
    ran(f, FL_TAKEN);  // its `BRA`
  }

  const uint16_t down = field(f, FLAME_DP_AIM_STEP_Y);
  const uint16_t across = field(f, FLAME_DP_AIM_STEP_X);
  ran(f, FL_FACE_HEAD);
  if (down != 0) {
    facing = 0;
    ran(f, FL_FACE_BMI);
    if (negative(down)) {
      ran(f, FL_TAKEN);
    } else {
      facing = 8;
      ran(f, FL_FACE_LDY);
    }
    ran(f, FL_FACE_STY_BRA);
    ran(f, FL_TAKEN);
    ran(f, FL_RTS);
  } else {
    ran(f, FL_TAKEN);
    ran(f, FL_FACE_HEAD);
    if (across == 0) {
      // Going nowhere, which a wander's legs never are.
      PORT_COVER(flame_goes_nowhere);
      facing = 8;
      ran(f, FL_TAKEN);
      ran(f, FL_FACE_NONE);
    } else {
      facing = 12;
      ran(f, FL_FACE_BMI);
      if (negative(across)) {
        ran(f, FL_TAKEN);
      } else {
        facing = 4;
        ran(f, FL_FACE_LDY);
      }
      ran(f, FL_FACE_STY);
      ran(f, FL_RTS);
    }
  }
  set_field(f, FLAME_DP_FACING, facing);
  f->c->y = facing;
}

// The count of steps for the legs just aimed: half their sum, and one. The
// sum's overflow and the bit the halving drops are what the ROM leaves in V
// and C.
static void count_steps(Flame* f) {
  set_c(f->c, false);
  const uint16_t sum = adc16(f->c, field(f, FLAME_DP_AIM_DX),
                             field(f, FLAME_DP_AIM_DY));
  set_c(f->c, (sum & 1u) != 0);
  lda(f->c, (uint16_t)((sum >> 1) + 1));
  set_field(f, FLAME_DP_STEPS_LEFT, f->c->a);
}

// One leg of a wander: 8 to 23 pixels one way or 9 to 24 the other, from a
// random number that the caller's carry goes into. `*carry` comes back as the
// carry of the sum that made the point.
static void pick_leg(Flame* f, uint16_t from, uint16_t to_at, uint16_t leg_at,
                     bool* carry) {
  RngResult r;
  rng_next(f->w, *carry, &r);
  if (f->k->draws < 2) f->k->draw_v[f->k->draws] = r.v;
  f->k->draws++;
  const uint16_t leg = (uint16_t)((r.a & 0x002fu) + 0xffe8u);
  const uint32_t to = (uint32_t)leg + from;
  *carry = to > 0xffffu;
  set_field(f, to_at, (uint16_t)to);
  ran(f, FL_PICK);
  if (negative(leg)) {
    set_field(f, leg_at, (uint16_t)-leg);
    ran(f, FL_PICK_NEG);
  } else {
    set_field(f, leg_at, leg);
    ran(f, FL_TAKEN);
  }
}

// `$81:B72D`: somewhere new to wander to.
static void wander_off(Flame* f, bool carry) {
  const Point me = position(f);
  PORT_COVER(flame_wandered_off);
  pick_leg(f, me.x, FLAME_DP_TO_X, FLAME_DP_LEG_X, &carry);
  pick_leg(f, me.y, FLAME_DP_TO_Y, FLAME_DP_LEG_Y, &carry);
  aim(f);
  keep_longer_leg(f);
  set_field(f, FLAME_DP_STATE, FLAME_STATE_WANDER);
  count_steps(f);
  ran(f, FL_PICK_TAIL);
}

// ---------------------------------------------------------------------------
// The two states
// ---------------------------------------------------------------------------

// A step along the leg it kept, tried on the ground. `$1E`/`$20` hold it
// either way.
static bool try_step(Flame* f, Point* to) {
  const Point me = position(f);
  *to = (Point){(uint16_t)(me.x + field(f, FLAME_DP_AIM_STEP_X)),
                (uint16_t)(me.y + field(f, FLAME_DP_AIM_STEP_Y))};
  set_field(f, FLAME_DP_TO_X, to->x);
  set_field(f, FLAME_DP_TO_Y, to->y);
  TerrainRegs r;
  terrain_blocked_enemy(f->w, to->x, to->y, &r);
  if (f->k->grounds < 2) f->k->ground[f->k->grounds] = r;
  f->k->grounds++;
  ran(f, FL_STEP);
  return !r.blocked;
}

// `$81:B7CD`: after `target`, for as long as the way there is.
static void chase_begin(Flame* f, uint16_t target) {
  PORT_COVER(flame_chase_began);
  set_field(f, FLAME_DP_STATE, FLAME_STATE_CHASE);
  set_field(f, FLAME_DP_TO_X, record_field(f, target, ACTOR_X));
  set_field(f, FLAME_DP_TO_Y, record_field(f, target, ACTOR_Y));
  ran(f, FL_C_START);
  aim(f);
  keep_longer_leg(f);
  count_steps(f);
  ran(f, FL_C_START_TAIL);
}

// `$81:B77D`.
static void wander(Flame* f) {
  ran(f, FL_W_HEAD);
  if (!(wram_r16(f->w, W_SCHED_TICK) & 1u)) {
    PORT_COVER(flame_wander_rested);
    ran(f, FL_TAKEN);
    ran(f, FL_RTS);
    return;
  }

  const Point me = position(f);
  uint16_t dist;
  const uint16_t found =
      actor_nearest_counted(f->w, me.x, me.y, &dist, &f->k->nearest);
  f->k->looked = true;
  set_field(f, FLAME_DP_TARGET, found);
  ran(f, FL_W_LOOK);
  if (dist < 0x0030) {
    ran(f, FL_TAKEN);
    ran(f, FL_JMP);
    chase_begin(f, found);
    return;
  }

  ran(f, FL_COUNT);
  if (count_down(f, FLAME_DP_STEPS_LEFT)) {
    ran(f, FL_TAKEN);
    ran(f, FL_JMP);
    wander_off(f, true);  // the compare with `$30` left carry set
    return;
  }

  Point to;
  if (try_step(f, &to)) {
    PORT_COVER(flame_wander_stepped);
    move_to(f, to);
    ran(f, FL_TAKE);
  } else {
    PORT_COVER(flame_wander_blocked);
    ran(f, FL_TAKEN);
  }

  const Point now = position(f);
  player_in_range(f->w, 0x00d0, now.x, now.y, &f->k->players);
  f->k->asked_players = true;
  ran(f, FL_W_PLAYERS);
  if (f->k->players.a != 0) {
    ran(f, FL_TAKEN);
  } else {
    PORT_COVER(flame_gave_up);
    count_down(f, FLAME_DP_LEAVE);
    ran(f, FL_DEC);
  }
  ran(f, FL_RTS);
}

// One of a chase's two steps a frame: taken unless the ground is solid or it
// would end within 9 pixels of the target, summed over both axes.
static void chase_step(Flame* f) {
  Point to;
  if (!try_step(f, &to)) {
    PORT_COVER(flame_chase_blocked);
    ran(f, FL_TAKEN);
  } else {
    const uint16_t target = field(f, FLAME_DP_TARGET);
    const uint16_t at[2] = {record_field(f, target, ACTOR_X),
                            record_field(f, target, ACTOR_Y)};
    const uint16_t tried[2] = {to.x, to.y};
    uint16_t gap[2];
    for (int i = 0; i < 2; i++) {
      gap[i] = (uint16_t)(at[i] - tried[i]);
      ran(f, FL_C_GAP);
      if (negative(gap[i])) {
        gap[i] = (uint16_t)-gap[i];
        ran(f, FL_NEG);
      } else {
        ran(f, FL_TAKEN);
      }
      if (i == 0) set_field(f, FLAME_DP_SCRATCH, gap[0]);
    }
    ran(f, FL_C_SUM);
    if ((uint16_t)(gap[0] + gap[1]) < 9) {
      PORT_COVER(flame_chase_held_off);
      ran(f, FL_TAKEN);
    } else {
      PORT_COVER(flame_chase_stepped);
      move_to(f, to);
      ran(f, FL_TAKE);
    }
  }
  // Each step hurries the picture on, too.
  count_down(f, FLAME_DP_TURN_WAIT);
  ran(f, FL_C_STEP_TAIL);
}

// `$81:B7EE`. `carry` is the thread's own, which a wander picked here takes.
static void chase(Flame* f, bool carry) {
  ran(f, FL_COUNT);
  if (count_down(f, FLAME_DP_STEPS_LEFT)) {
    ran(f, FL_TAKEN);
    ran(f, FL_JMP);
    wander_off(f, carry);
    return;
  }
  ran(f, FL_JSR);
  chase_step(f);
  chase_step(f);
}

// ---------------------------------------------------------------------------
// Its picture, and the trail
// ---------------------------------------------------------------------------

// `$81:B1A1`, the doll's: show frame `i` of the animation table `$16` names.
static void show_frame(Flame* f, uint16_t i) {
  const uint32_t at = (uint32_t)field(f, FLAME_DP_FRAMES) + (uint16_t)(i << 2);
  const uint16_t mask = table(f, at);
  const uint16_t picture = table(f, at + 2);
  const uint16_t flags = record_field(f, f->record, ACTOR_FLAGS);
  ran(f, FL_SHOW_HEAD);
  ran(f, FL_TAKEN);  // the `BMI`, or the `BRA` after the `ORA`
  if (negative(mask)) {
    PORT_COVER(flame_shown_masked);
    set_record_field(f, f->record, ACTOR_FLAGS, (uint16_t)(flags & mask));
    ran(f, FL_SHOW_AND);
  } else {
    PORT_COVER(flame_shown_ored);
    set_record_field(f, f->record, ACTOR_FLAGS, (uint16_t)(flags | mask));
    ran(f, FL_SHOW_ORA);
  }
  set_record_field(f, f->record, ACTOR_META,
                   table(f, FLAME_PICTURES + (uint16_t)(picture << 1)));
  set_record_field(f, f->record, ACTOR_META_BANK, FLAME_PICTURE_BANK);
  ran(f, FL_SHOW_TAIL);
}

static uint16_t trail_at(int place) {
  return (uint16_t)(FLAME_DP_TRAIL + place * FLAME_TRAIL_STRIDE);
}

// Where in the display list a record is: the ROM's unlink walks that far.
// -1 for one that is not this thread's, -2 for one not in use, -3 for one
// the list does not hold.
static int place_in_list(const Wram* w, uint16_t record) {
  if (wram_r16(w, W_SCHED_CUR_TASK) !=
      wram_r16(w, (uint16_t)(record + ACTOR_THREAD)))
    return -1;
  if (!(wram_r16(w, (uint16_t)(record + ACTOR_FLAGS)) & ACTOR_ACTIVE))
    return -2;
  int place = 0;
  for (uint16_t at = wram_r16(w, W_ACTOR_LIST_HEAD); at != record;
       at = wram_r16(w, (uint16_t)(at + ACTOR_NEXT))) {
    if (at < W_ACTOR_SLOTS || at > ACTOR_SLOT_LAST ||
        ++place > ACTOR_SLOT_COUNT)
      return -3;
  }
  return place;
}

// `$81:B8E4`: a picture two steps behind it, in the first of its two places
// that is free, counting down from the second.
static void leave_trail(Flame* f) {
  int place = FLAME_TRAIL_PLACES - 1;
  ran(f, FL_T_HEAD);
  for (;; place--) {
    ran(f, FL_T_TEST);
    if (field(f, trail_at(place)) == FLAME_NO_TRAIL) break;
    ran(f, FL_T_NEXT);
    if (place == 0) {
      PORT_COVER(flame_trail_full);
      ran(f, FL_RTS);
      return;
    }
    ran(f, FL_TAKEN);
  }
  PORT_COVER(flame_trailed);
  ran(f, FL_TAKEN);
  ran(f, FL_T_FOUND);

  SlotAllocRegs r;
  actor_slot_alloc(f->w, FLAME_BANK, &r);
  f->k->trailed = true;
  f->k->trail_record = r.a;
  const uint16_t trail = r.a;
  const Point me = position(f);
  set_record_field(f, trail, ACTOR_X,
                   (uint16_t)(me.x - 2 * field(f, FLAME_DP_AIM_STEP_X)));
  set_record_field(f, trail, ACTOR_Z, 0);
  set_record_field(f, trail, ACTOR_Y,
                   (uint16_t)(me.y - 2 * field(f, FLAME_DP_AIM_STEP_Y) + 1));
  set_record_field(f, trail, ACTOR_META, FLAME_TRAIL_PICTURE);
  set_record_field(f, trail, ACTOR_META_BANK, FLAME_PICTURE_BANK);
  set_record_field(f, trail, ACTOR_THREAD, wram_r16(f->w, W_SCHED_CUR_TASK));
  set_record_field(f, trail, ACTOR_COLLIDE_ID, FLAME_COLLIDE_ID);
  set_record_field(
      f, trail, ACTOR_FLAGS,
      (uint16_t)(ACTOR_DRAW | record_field(f, trail, ACTOR_FLAGS)));
  set_field(f, FLAME_DP_SCRATCH, (uint16_t)(place * FLAME_TRAIL_STRIDE));
  set_field(f, (uint16_t)(trail_at(place) + 2), 0x0018);
  set_field(f, trail_at(place), trail);
  set_field(f, FLAME_DP_TRAILS, (uint16_t)(field(f, FLAME_DP_TRAILS) + 1));
  ran(f, FL_T_DRESS);
}

// `$81:B8AE`: a frame off each trail. On every fourth it changes picture,
// and after its last it is given back. Y is left holding the first place.
static void age_trails(Flame* f) {
  ran(f, FL_T_HEAD);
  for (int place = FLAME_TRAIL_PLACES - 1; place >= 0; place--) {
    const uint16_t trail = field(f, trail_at(place));
    f->c->y = trail;
    ran(f, FL_T_TEST);
    if (trail == FLAME_NO_TRAIL) {
      ran(f, FL_TAKEN);
    } else {
      ran(f, FL_G_COUNT);
      if (count_down(f, (uint16_t)(trail_at(place) + 2))) {
        PORT_COVER(flame_trail_gone);
        set_field(f, FLAME_DP_SCRATCH, (uint16_t)(place * FLAME_TRAIL_STRIDE));
        if (f->k->frees < FLAME_TRAIL_PLACES)
          f->k->free_place[f->k->frees] = place_in_list(f->w, trail);
        f->k->frees++;
        SlotFreeRegs r;
        actor_slot_free(f->w, trail, f->page, 0, 0, &r);
        set_field(f, trail_at(place), FLAME_NO_TRAIL);
        set_field(f, FLAME_DP_TRAILS,
                  (uint16_t)(field(f, FLAME_DP_TRAILS) - 1));
        ran(f, FL_TAKEN);
        ran(f, FL_G_FREE);
        ran(f, FL_TAKEN);  // its `BRA`
      } else {
        const uint16_t left = field(f, (uint16_t)(trail_at(place) + 2));
        set_field(f, FLAME_DP_SCRATCH, left);
        ran(f, FL_G_PHASE);
        if (left & 3u) {
          ran(f, FL_TAKEN);
        } else {
          PORT_COVER(flame_trail_changed);
          set_record_field(
              f, trail, ACTOR_META,
              table(f, FLAME_PICTURES + FLAME_TRAIL_PICTURES + (left >> 1)));
          ran(f, FL_G_SHOW);
        }
      }
    }
    ran(f, FL_T_NEXT);
    if (place != 0) ran(f, FL_TAKEN);
  }
  ran(f, FL_RTS);
  // The loop's last `SBC #$0004`, from 0.
  f->c->x = (uint16_t)-FLAME_TRAIL_STRIDE;
  set_c(f->c, false);
  set_v(f->c, false);
}

// `$81:B86F`: the picture, a trail on every sixteenth frame of the game, and
// one frame fewer to live.
static void animate(Flame* f) {
  ran(f, FL_A_HEAD);
  if (!count_down(f, FLAME_DP_TURN_WAIT)) {
    ran(f, FL_TAKEN);
  } else {
    PORT_COVER(flame_turned);
    set_field(f, FLAME_DP_TURN_WAIT, 0x000c);
    const uint16_t stride = (uint16_t)((field(f, FLAME_DP_STRIDE) + 1) & 3u);
    set_field(f, FLAME_DP_STRIDE, stride);
    ran(f, FL_A_TURN);
    show_frame(f, (uint16_t)(stride + field(f, FLAME_DP_FACING)));
  }

  ran(f, FL_A_TRAIL_TEST);
  if (wram_r16(f->w, W_SCHED_TICK) & 0x000fu) {
    ran(f, FL_TAKEN);
  } else {
    ran(f, FL_JSR);
    leave_trail(f);
  }

  const Point me = position(f);
  set_record_field(f, f->record, ACTOR_X, me.x);
  set_record_field(f, f->record, ACTOR_Y, me.y);
  ran(f, FL_A_TAIL);
  if (!count_down(f, FLAME_DP_FRAMES_LEFT)) {
    ran(f, FL_TAKEN);
  } else {
    PORT_COVER(flame_burned_out);
    count_down(f, FLAME_DP_LEAVE);
    ran(f, FL_DEC);
  }
  ran(f, FL_RTS);
}

// ---------------------------------------------------------------------------
// The stretches
// ---------------------------------------------------------------------------

static Flame flame_of(Wram* w, const Rom* rom, PortCpu* c, FlameWork* k) {
  return (Flame){w, rom, c, k, c->d,
                 wram_r16(w, (uint16_t)(c->d + FLAME_DP_RECORD))};
}

void flame_launch(Wram* w, PortCpu* c, FlameWork* k) {
  PORT_COVER(flame_launched);
  set_c(c, false);
  lda(c, adc16(c, wram_r16(w, W_SQUIRTS_LIVE), FLAME_BUDGET));
  wram_w16(w, W_SQUIRTS_LIVE, c->a);
  // `JSR $B6BD`, which begins with the call for a record and comes back in
  // `flame_dress`.
  push16(w, c, 0xb670);
  k->blocks[FL_LAUNCH]++;
  c->pc = FLAME_ALLOC_CALL_PC;
}

void flame_dress(Wram* w, PortCpu* c, FlameWork* k) {
  Flame f = flame_of(w, NULL, c, k);
  const uint16_t record = c->a;
  PORT_COVER(flame_dressed);
  f.record = record;
  set_field(&f, FLAME_DP_RECORD, record);

  const Point at = {field(&f, FLAME_DP_FROM_X), field(&f, FLAME_DP_FROM_Y)};
  move_to(&f, at);
  set_record_field(&f, record, ACTOR_X, at.x);
  set_record_field(&f, record, ACTOR_Z, 0);
  set_record_field(&f, record, ACTOR_Y, at.y);
  set_record_field(&f, record, ACTOR_META, FLAME_PICTURE);
  set_record_field(&f, record, ACTOR_META_BANK, FLAME_PICTURE_BANK);
  set_record_field(&f, record, ACTOR_THREAD, wram_r16(w, W_SCHED_CUR_TASK));
  set_record_field(
      &f, record, ACTOR_FLAGS,
      (uint16_t)(ACTOR_DRAW | record_field(&f, record, ACTOR_FLAGS)));
  set_record_field(&f, record, ACTOR_ATTR, FLAME_ATTR);
  set_record_field(&f, record, ACTOR_COLLIDE_ID, FLAME_COLLIDE_ID);

  set_field(&f, FLAME_DP_HEALTH, FLAME_HEALTH);
  set_field(&f, FLAME_DP_FRAMES_LEFT, FLAME_FRAMES_LEFT);
  set_field(&f, FLAME_DP_TURN_WAIT, 0);
  set_field(&f, FLAME_DP_LEAVE, 0);
  set_field(&f, 0x4c, 0);
  set_field(&f, 0x7e, 0);
  set_field(&f, FLAME_DP_HIT_ID, 0);
  set_field(&f, FLAME_DP_FACING, 0);
  set_field(&f, FLAME_DP_STRIDE, 0);
  set_field(&f, FLAME_DP_FRAMES, FLAME_ANIMATION);
  ran(&f, FL_DRESS);

  // What hits it is told to `$81:B95F`.
  c->x = wram_r16(w, W_SCHED_CUR_TASK);
  wram_w16(w, (uint16_t)(W_THREAD_HANDLER + c->x), FLAME_HANDLER);
  wram_w16(w, (uint16_t)(W_THREAD_HANDLER_BANK + c->x), FLAME_BANK);
  c->y = FLAME_BANK;
  ran(&f, FL_HANDLER);

  c->s = (uint16_t)(c->s + 2);  // the `RTS`, back to the launch
  // `JSR $B8A4` from there, whose return address lies over the launch's.
  push16(w, c, 0xb673);
  c->s = (uint16_t)(c->s + 2);
  for (int place = 0; place < FLAME_TRAIL_PLACES; place++)
    set_field(&f, trail_at(place), FLAME_NO_TRAIL);
  set_field(&f, FLAME_DP_TRAILS, 0);
  lda(c, FLAME_FIRST_TICKS);
  ran(&f, FL_DRESS_TAIL);
  c->pc = FLAME_FIRST_YIELD_PC;
}

// Back to the yield for another frame.
static void again(Flame* f) {
  set_field(f, FLAME_DP_HIT_ID, 0);
  lda(f->c, FLAME_YIELD_TICKS);
  ran(f, FL_AGAIN);
  f->c->pc = FLAME_YIELD_PC;
}

void flame_begin(Wram* w, PortCpu* c, FlameWork* k) {
  Flame f = flame_of(w, NULL, c, k);
  PORT_COVER(flame_began);
  ran(&f, FL_JSR);
  wander_off(&f, flag(c, PORT_P_C));
  again(&f);
}

// One flame fewer, and its record on the way to being freed.
static void end(Flame* f) {
  set_c(f->c, true);
  wram_w16(f->w, W_SQUIRTS_LIVE,
           sbc16(f->c, wram_r16(f->w, W_SQUIRTS_LIVE), FLAME_BUDGET));
  lda(f->c, field(f, FLAME_DP_RECORD));
  ran(f, FL_L_END);
  f->c->pc = FLAME_FREE_JML_PC;
}

void flame_frame(Wram* w, const Rom* rom, PortCpu* c, FlameWork* k) {
  Flame f = flame_of(w, rom, c, k);
  // `PEA $B68E : LDA $0E : DEC : PHA : RTS`, and the state body's own `RTS`.
  const uint16_t s = c->s;
  const uint16_t state = field(&f, FLAME_DP_STATE);
  push16(w, c, 0xb68e);
  push16(w, c, (uint16_t)(state - 1));
  c->s = s;
  ran(&f, FL_DISPATCH);

  if (state == FLAME_STATE_WANDER)
    wander(&f);
  else
    chase(&f, flag(c, PORT_P_C));
  ran(&f, FL_JSR);
  animate(&f);

  // A flame that is leaving runs its trail out first, here and now.
  for (;;) {
    ran(&f, FL_JSR);
    age_trails(&f);
    ran(&f, FL_L_TEST);
    if (field(&f, FLAME_DP_LEAVE) == 0) {
      PORT_COVER(flame_stayed);
      ran(&f, FL_TAKEN);
      again(&f);
      return;
    }
    ran(&f, FL_L_TEST);  // `LDA $50 : BNE`
    if (field(&f, FLAME_DP_TRAILS) == 0) break;
    PORT_COVER(flame_trail_run_out);
    ran(&f, FL_TAKEN);
  }

  c->x = FLAME_POINTS;
  lda(c, field(&f, FLAME_DP_HIT_ID));
  ran(&f, FL_L_SCORE_TEST);
  if (c->a != 0) {
    PORT_COVER(flame_destroyed);
    c->pc = FLAME_SCORE_CALL_PC;
    return;
  }
  PORT_COVER(flame_left);
  ran(&f, FL_TAKEN);
  end(&f);
}

void flame_scored(Wram* w, PortCpu* c, FlameWork* k) {
  Flame f = flame_of(w, NULL, c, k);
  PORT_COVER(flame_scored);
  wram_w16(w, W_FLAMES_DESTROYED,
           (uint16_t)(wram_r16(w, W_FLAMES_DESTROYED) + 1));
  ran(&f, FL_S_INC);
  end(&f);
}

bool flame_frame_supported(const Wram* w, uint16_t page) {
  const uint16_t state = wram_r16(w, (uint16_t)(page + FLAME_DP_STATE));
  if (state != FLAME_STATE_WANDER && state != FLAME_STATE_CHASE) return false;

  // Its two places for a trail, and the count of them, have to agree with
  // each other and with the display list.
  int trails = 0;
  for (int place = 0; place < FLAME_TRAIL_PLACES; place++) {
    const uint16_t trail = wram_r16(w, (uint16_t)(page + trail_at(place)));
    if (trail == FLAME_NO_TRAIL) continue;
    if (trail < W_ACTOR_SLOTS || trail > ACTOR_SLOT_LAST) return false;
    if (place_in_list(w, trail) < 0) return false;
    trails++;
  }
  if (wram_r16(w, (uint16_t)(page + FLAME_DP_TRAILS)) != trails) return false;

  // A record for a trail it might leave.
  for (uint16_t at = W_ACTOR_SLOTS; at <= ACTOR_SLOT_LAST;
       at = (uint16_t)(at + ACTOR_SLOT_STRIDE))
    if (!(wram_r16(w, (uint16_t)(at + ACTOR_FLAGS)) & ACTOR_ACTIVE))
      return true;
  return false;
}
