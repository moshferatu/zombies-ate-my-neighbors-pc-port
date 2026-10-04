// The evil dolls' state bodies -- see port/doll.h.

#include "port/doll.h"

#include <stddef.h>

#include "port/coverage.h"
#include "port/flags.h"
#include "port/rng.h"
#include "port/terrain.h"
#include "port/thread.h"

typedef struct {
  uint16_t x, y;
} Point;

// `$D0`: how far it looks for someone to go after.
#define DOLL_REACH 0x00d0
// Within this on both axes it swings rather than steps.
#define DOLL_SWING_REACH 0x0010
// A charge stops this far short.
#define DOLL_DASH_SHORT 0x0010
// Frames between throws, counted down to -1.
#define DOLL_THROW_COOLDOWN 0x0023
// Frames it waits to step after throwing, and after landing and throwing.
#define DOLL_THROW_PAUSE 0x000a
#define DOLL_LANDING_PAUSE 0x0005
// The step timer runs from this to zero: a step every fourth frame.
#define DOLL_STEP_EVERY 3
// In the air, the distance is covered a part at a time, in this many parts.
#define DOLL_FLIGHT_PARTS 0x0012
// How a flight starts: going up at 2, slowing every fourth frame.
#define DOLL_RISE 2
#define DOLL_RISE_26 3
#define DOLL_RISE_SLOWS 3  // `AND #$0003`

// Tables in `DOLL_BANK`, indexed by a facing.
#define DOLL_AXE_AT 0xb08du      // where the swung axe is shown, from the doll
#define DOLL_KNOCKED_TO 0xafa3u  // where a hit knocks it to
#define DOLL_FACINGS 0xb2a4u     // a byte per way of moving; see `face_towards`
#define DOLL_NO_FACING 0x0010

// The axe it throws, and the collision handler it installs on landing.
#define DOLL_AXE_THREAD 0xb4eau
#define DOLL_COLLIDE 0xb41cu

#define DOLL_PICTURE_BANK 0x0090

// One doll's frame, and the carry and overflow the ROM would leave.
typedef struct {
  Wram* w;
  const Rom* rom;
  uint16_t page;
  uint16_t record;
  DollLog* log;  // never NULL here
  PortFlags flags;
} Doll;

static uint16_t field(const Doll* d, uint16_t at) {
  return wram_r16(d->w, (uint16_t)(d->page + at));
}

static void set_field(Doll* d, uint16_t at, uint16_t v) {
  wram_w16(d->w, (uint16_t)(d->page + at), v);
}

static uint16_t record_field(const Doll* d, uint16_t record, uint16_t at) {
  return wram_r16(d->w, (uint16_t)(record + at));
}

static void set_record_field(Doll* d, uint16_t record, uint16_t at,
                             uint16_t v) {
  wram_w16(d->w, (uint16_t)(record + at), v);
}

static Point position(const Doll* d) {
  return (Point){field(d, DOLL_DP_X), field(d, DOLL_DP_Y)};
}

static Point record_position(const Doll* d, uint16_t record) {
  return (Point){record_field(d, record, ACTOR_X),
                 record_field(d, record, ACTOR_Y)};
}

static uint16_t height(const Doll* d) {
  return record_field(d, d->record, ACTOR_Z);
}

// Entry `index` of a table in `DOLL_BANK`. The ROM's indexed reads would carry
// past the end of the bank, so this does too.
static uint16_t table_word(const Doll* d, uint16_t table, uint16_t index) {
  return rom_word(d->rom, ((uint32_t)DOLL_BANK << 16) + table + index);
}

static void set_state(Doll* d, uint16_t body) {
  set_field(d, DOLL_DP_STATE, body);
}

static bool negative(uint16_t v) { return (v & 0x8000u) != 0; }

// A difference made positive, by `BPL : EOR #$FFFF : INC`, which leaves the
// flags alone. `*negated` counts the ones that had to be.
static uint16_t magnitude(uint16_t v, int* negated) {
  if (!negative(v)) return v;
  (*negated)++;
  return (uint16_t)-v;
}

// ---------------------------------------------------------------------------
// The ROM's arithmetic, which leaves carry and overflow for the thread's next
// `PHP`. See `port/flags.h`.
// ---------------------------------------------------------------------------

static void set_carry(Doll* d, bool c) { flags_carry(&d->flags, c); }

static void set_overflow(Doll* d, bool v) { flags_overflow(&d->flags, v); }

// `CLC : ADC`.
static uint16_t plus(Doll* d, uint16_t a, uint16_t b) {
  return flags_add(&d->flags, a, b);
}

// `SEC : SBC`.
static uint16_t minus(Doll* d, uint16_t a, uint16_t b) {
  return flags_sub(&d->flags, a, b);
}

// `CMP`: is `a` at least `b`?
static bool at_least(Doll* d, uint16_t a, uint16_t b) {
  return flags_at_least(&d->flags, a, b);
}

// `CMP : BEQ`.
static bool same(Doll* d, uint16_t a, uint16_t b) {
  return flags_same(&d->flags, a, b);
}

// ---------------------------------------------------------------------------
// What it asks the rest of the game
// ---------------------------------------------------------------------------

static bool ground_is_solid(Doll* d, Point p) {
  TerrainRegs r;
  terrain_blocked_enemy(d->w, p.x, p.y, &r);
  set_carry(d, r.blocked);
  set_overflow(d, r.v);
  if (d->log->probes < DOLL_MAX_PROBES)
    d->log->probe[d->log->probes++] = (DollProbe){r.blocked, r.probes};
  return r.blocked;
}

static bool off_the_level(Doll* d, Point p) {
  BoundsRegs r;
  terrain_out_of_bounds(d->w, p.x, p.y, &r);
  set_carry(d, r.c);
  d->log->knock_edge = r.exit;
  return r.c;
}

// Whoever `actor_nearest` finds, and how far. It leaves carry clear, and an
// overflow the port does not follow.
static uint16_t nearest_actor(Doll* d, uint16_t* dist) {
  const Point me = position(d);
  const uint16_t found =
      actor_nearest_counted(d->w, me.x, me.y, dist, &d->log->nearest);
  set_carry(d, false);
  flags_overflow_unknown(&d->flags);
  return found;
}

static uint16_t player_near(Doll* d) {
  const Point me = position(d);
  PlayerPickRegs* r = &d->log->players;
  player_in_range(d->w, DOLL_REACH, me.x, me.y, r);
  set_carry(d, r->c);
  flags_overflow_unknown(&d->flags);
  return r->a;
}

static uint16_t random_byte(Doll* d) {
  RngResult r;
  rng_next(d->w, d->flags.c, &r);
  set_carry(d, r.c);
  set_overflow(d, r.v);
  d->log->drew_overflow = r.v;
  return r.a;
}

// ---------------------------------------------------------------------------
// Pictures, facings and aims
// ---------------------------------------------------------------------------

// `$81:B1A1`: show frame `i` of the animation table `$16` names.
static void show_frame(Doll* d, uint16_t i) {
  const uint32_t at = ((uint32_t)DOLL_BANK << 16) + field(d, DOLL_DP_FRAMES) +
                      (uint16_t)(i << 2);
  const uint16_t mask = rom_word(d->rom, at);
  const uint16_t picture = rom_word(d->rom, at + 2);
  const uint16_t flags = record_field(d, d->record, ACTOR_FLAGS);
  set_record_field(d, d->record, ACTOR_FLAGS,
                   negative(mask) ? (uint16_t)(flags & mask)
                                  : (uint16_t)(flags | mask));
  set_record_field(d, d->record, ACTOR_META,
                   table_word(d, DOLL_PICTURES, (uint16_t)(picture << 1)));
  set_record_field(d, d->record, ACTOR_META_BANK, DOLL_PICTURE_BANK);
  set_carry(d, negative(picture));  // the `ASL` that doubles it
  d->log->frames++;
  if (negative(mask)) d->log->frames_masked++;
}

// The walk cycle's next step, 0 to 3.
static uint16_t stride_on(Doll* d) {
  uint16_t stride = (uint16_t)(field(d, DOLL_DP_STRIDE) + 1);
  d->log->strides++;
  if (at_least(d, stride, 4)) {
    stride = 0;
    d->log->wraps++;
  }
  set_field(d, DOLL_DP_STRIDE, stride);
  return stride;
}

// The next frame of the walk cycle, facing the way it faces.
static void walk_on(Doll* d) {
  const uint16_t stride = stride_on(d);
  show_frame(d, plus(d, stride, field(d, DOLL_DP_FACING)));
}

// `$81:B27E`: which way does it face, moving from here to `$1E`/`$20`? Only
// the four straight ways have a facing.
static uint16_t face_towards(Doll* d) {
  const Point me = position(d);
  const Point to = {field(d, DOLL_DP_TO_X), field(d, DOLL_DP_TO_Y)};
  d->log->faced++;
  // The index is built by `ADC #1` on the carry each `CMP` left: 4 for down
  // and 8 for up, plus 1 for right and 2 for left. None of it can overflow,
  // and the shifts and the sums are too small to carry.
  uint16_t index = 0;
  if (!same(d, me.y, to.y)) {
    index = (uint16_t)((1 + d->flags.c) << 2);
    set_overflow(d, false);
    d->log->faced_down++;
  }
  if (!same(d, me.x, to.x)) {
    index = (uint16_t)(index + 1 + d->flags.c);
    set_overflow(d, false);
    d->log->faced_across++;
  }
  const uint16_t facing =
      (uint16_t)((table_word(d, DOLL_FACINGS, index) & 0x00ffu) << 2);
  set_carry(d, false);
  set_field(d, DOLL_DP_FACING, facing);
  return facing;
}

// `$81:B24D`: from here to `$1E`/`$20`, how far and which way on each axis.
static void aim(Doll* d) {
  const Point me = position(d);
  const Point to = {field(d, DOLL_DP_TO_X), field(d, DOLL_DP_TO_Y)};
  const uint16_t from[2] = {me.x, me.y};
  const uint16_t at[2] = {to.x, to.y};
  const uint16_t dist[2] = {DOLL_DP_AIM_DX, DOLL_DP_AIM_DY};
  const uint16_t step[2] = {DOLL_DP_AIM_STEP_X, DOLL_DP_AIM_STEP_Y};
  d->log->aims++;
  for (int i = 0; i < 2; i++) {
    const uint16_t diff = minus(d, at[i], from[i]);
    if (diff == 0) d->log->aims_level++;
    set_field(d, dist[i], magnitude(diff, &d->log->aims_negated));
    set_field(d, step[i], diff == 0 ? 0 : negative(diff) ? 0xffff : 1);
  }
}

// `$1E`/`$20` to where a record is.
static void look_at(Doll* d, uint16_t record) {
  const Point at = record_position(d, record);
  set_field(d, DOLL_DP_TO_X, at.x);
  set_field(d, DOLL_DP_TO_Y, at.y);
}

static void aim_at(Doll* d, uint16_t record) {
  look_at(d, record);
  aim(d);
}

// ---------------------------------------------------------------------------
// In the air
// ---------------------------------------------------------------------------

// `$81:B470`: set up a flight to `to`, a distance and a 1 or -1 on each axis.
static void fly_to(Doll* d, Point to) {
  const Point me = position(d);
  const uint16_t dx = minus(d, to.x, me.x);
  set_field(d, DOLL_DP_FLIGHT_DX, magnitude(dx, &d->log->flights_negated));
  set_field(d, DOLL_DP_FLIGHT_STEP_X, negative(dx) ? 0xffff : 1);
  const uint16_t dy = minus(d, to.y, me.y);
  set_field(d, DOLL_DP_FLIGHT_DY, magnitude(dy, &d->log->flights_negated));
  set_field(d, DOLL_DP_FLIGHT_STEP_Y, negative(dy) ? 0xffff : 1);
  set_field(d, DOLL_DP_FLIGHT_ACC_X, 0);
  set_field(d, DOLL_DP_FLIGHT_ACC_Y, 0);
}

static void take_off(Doll* d) {
  set_field(d, DOLL_DP_RISE, DOLL_RISE);
  set_field(d, DOLL_DP_RISE_26, DOLL_RISE_26);
  set_field(d, DOLL_DP_RISE_TICK, 0);
}

// `$81:B4D3`: up, or down, by the rise, which slows every fourth frame.
static void rise(Doll* d) {
  set_record_field(d, d->record, ACTOR_Z,
                   plus(d, height(d), field(d, DOLL_DP_RISE)));
  const uint16_t tick = (uint16_t)(field(d, DOLL_DP_RISE_TICK) - 1);
  set_field(d, DOLL_DP_RISE_TICK, tick);
  if ((tick & DOLL_RISE_SLOWS) == 0) {
    set_field(d, DOLL_DP_RISE, (uint16_t)(field(d, DOLL_DP_RISE) - 1));
    d->log->slowed = true;
  }
}

// `$81:B49C`: a part further along the flight on each axis, and a pixel for
// every whole one.
static void fly(Doll* d) {
  const uint16_t pos[2] = {DOLL_DP_X, DOLL_DP_Y};
  const uint16_t step[2] = {DOLL_DP_FLIGHT_STEP_X, DOLL_DP_FLIGHT_STEP_Y};
  const uint16_t dist[2] = {DOLL_DP_FLIGHT_DX, DOLL_DP_FLIGHT_DY};
  const uint16_t acc[2] = {DOLL_DP_FLIGHT_ACC_X, DOLL_DP_FLIGHT_ACC_Y};
  int* moved[2] = {&d->log->flight_x, &d->log->flight_y};
  for (int i = 0; i < 2; i++) {
    uint16_t parts = plus(d, field(d, acc[i]), field(d, dist[i]));
    while (at_least(d, parts, DOLL_FLIGHT_PARTS)) {
      set_field(d, pos[i], plus(d, field(d, pos[i]), field(d, step[i])));
      parts = minus(d, parts, DOLL_FLIGHT_PARTS);
      (*moved[i])++;
    }
    set_field(d, acc[i], parts);
  }
}

// ---------------------------------------------------------------------------
// Attacking
// ---------------------------------------------------------------------------

static void hide_axe(Doll* d) {
  const uint16_t axe = field(d, DOLL_DP_AXE);
  set_record_field(d, axe, ACTOR_FLAGS,
                   (uint16_t)(record_field(d, axe, ACTOR_FLAGS) & ~ACTOR_DRAW));
}

// `$81:B04C`: swing the axe, shown beside it the way it faces. Only on the
// frames it could have stepped.
static void swing(Doll* d) {
  if (field(d, DOLL_DP_STEP_WAIT) != 0) {
    PORT_COVER(doll_swing_waited);
    d->log->swing_waited = true;
    set_field(d, DOLL_DP_FRAMES, DOLL_FRAMES_WALK);
    return;
  }
  PORT_COVER(doll_swung);
  set_field(d, DOLL_DP_FRAMES, DOLL_FRAMES_SWING);
  const uint16_t facing = field(d, DOLL_DP_FACING);
  const uint16_t axe = field(d, DOLL_DP_AXE);
  const Point me = position(d);
  set_record_field(d, axe, ACTOR_X,
                   plus(d, table_word(d, DOLL_AXE_AT, facing), me.x));
  set_record_field(d, axe, ACTOR_Y,
                   plus(d, table_word(d, DOLL_AXE_AT + 2, facing), me.y));
  set_record_field(d, axe, ACTOR_FLAGS,
                   (uint16_t)(record_field(d, axe, ACTOR_FLAGS) | ACTOR_DRAW));
  walk_on(d);
  set_field(d, DOLL_DP_FRAMES, DOLL_FRAMES_WALK);
}

// The axe starts where the doll is, and the doll aims at its target.
static void ready_axe(Doll* d) {
  const Point me = position(d);
  set_field(d, DOLL_DP_AXE_X, me.x);
  set_field(d, DOLL_DP_AXE_Y, me.y);
  aim_at(d, field(d, DOLL_DP_TARGET));
}

// Throw the axe along `dx`/`dy`, in the throwing pose, and wait to step.
static void let_fly(Doll* d, uint16_t dx, uint16_t dy, uint16_t pause) {
  set_field(d, DOLL_DP_AXE_DX, dx);
  set_field(d, DOLL_DP_AXE_DY, dy);
  set_field(d, DOLL_DP_FRAMES, DOLL_FRAMES_SWING);
  show_frame(d, plus(d, 1, field(d, DOLL_DP_FACING)));
  set_field(d, DOLL_DP_FRAMES, DOLL_FRAMES_WALK);
  set_field(d, DOLL_DP_STEP_WAIT, pause);
  d->log->axe_slot =
      thread_spawn(d->w, d->rom, DOLL_AXE_THREAD, DOLL_BANK, d->page);
}

// `$81:AFB3`: throw at the target, if the last throw was long enough ago.
static void throw_axe(Doll* d) {
  if (!negative(field(d, DOLL_DP_THROW_WAIT))) {
    PORT_COVER(doll_throw_waited);
    return;
  }
  PORT_COVER(doll_threw);
  d->log->threw = true;
  set_field(d, DOLL_DP_THROW_WAIT, DOLL_THROW_COOLDOWN);
  ready_axe(d);
  let_fly(d, field(d, DOLL_DP_AIM_STEP_X), field(d, DOLL_DP_AIM_STEP_Y),
          DOLL_THROW_PAUSE);
}

// `$81:AFFE`: on landing, throw along the wider of the gaps it last measured.
static void throw_on_landing(Doll* d) {
  PORT_COVER(doll_threw_on_landing);
  ready_axe(d);
  if (at_least(d, field(d, DOLL_DP_GAP_X), field(d, DOLL_DP_GAP_Y))) {
    d->log->landing_across = true;
    let_fly(d, field(d, DOLL_DP_AIM_STEP_X), 0, DOLL_LANDING_PAUSE);
  } else {
    let_fly(d, 0, field(d, DOLL_DP_AIM_STEP_Y), DOLL_LANDING_PAUSE);
  }
}

// ---------------------------------------------------------------------------
// Moving
// ---------------------------------------------------------------------------

static void seek_begin(Doll* d) {
  set_field(d, DOLL_DP_STEP_EVERY, DOLL_STEP_EVERY);
  set_state(d, DOLL_STATE_SEEK);
  set_field(d, DOLL_DP_FRAMES, DOLL_FRAMES_WALK);
  set_field(d, DOLL_DP_TO_Y, field(d, DOLL_DP_Y));
}

// May it step to `to`? Only solid ground says no. Taken, the step faces that
// way and moves the walk cycle on.
static bool try_step(Doll* d, Point to) {
  set_field(d, DOLL_DP_TO_X, to.x);
  set_field(d, DOLL_DP_TO_Y, to.y);
  if (ground_is_solid(d, to)) return false;
  if (same(d, face_towards(d), DOLL_NO_FACING)) {
    d->log->facing_none = true;
    return true;
  }
  set_field(d, DOLL_DP_Y, to.y);
  set_field(d, DOLL_DP_X, to.x);
  walk_on(d);
  return true;
}

// `$81:B09D`: a pixel towards the target, on the frames the step timer
// allows. The smaller gap first, which lines it up, and the other way when
// that is blocked.
static void step(Doll* d) {
  if (field(d, DOLL_DP_STEP_WAIT) != 0) {
    d->log->step_waited = true;
    return;
  }
  aim_at(d, field(d, DOLL_DP_TARGET));
  set_field(d, DOLL_DP_TRIED, 0);
  bool across = !at_least(d, field(d, DOLL_DP_GAP_X), field(d, DOLL_DP_GAP_Y));
  d->log->across_first = across;
  for (;;) {
    const Point me = position(d);
    if (across) {
      const Point to = {plus(d, me.x, field(d, DOLL_DP_AIM_STEP_X)), me.y};
      if (try_step(d, to)) {
        PORT_COVER(doll_stepped_across);
        return;
      }
      set_field(d, DOLL_DP_TRIED, (uint16_t)(field(d, DOLL_DP_TRIED) + 1));
    }
    const Point to = {me.x, plus(d, me.y, field(d, DOLL_DP_AIM_STEP_Y))};
    if (try_step(d, to)) {
      PORT_COVER(doll_stepped_down);
      return;
    }
    // Both ways blocked, the second time round it gives up for the frame.
    // Down first, that means it tries down twice.
    if (field(d, DOLL_DP_TRIED) != 0) {
      PORT_COVER(doll_step_blocked);
      return;
    }
    across = true;
  }
}

// One of a charge's steps, two a frame. A blocked step is not taken, and the
// walk cycle runs on regardless.
static void dash_step(Doll* d) {
  const Point me = position(d);
  const Point to = {plus(d, me.x, field(d, DOLL_DP_AIM_STEP_X)),
                    plus(d, me.y, field(d, DOLL_DP_AIM_STEP_Y))};
  set_field(d, DOLL_DP_TO_X, to.x);
  set_field(d, DOLL_DP_TO_Y, to.y);
  if (!ground_is_solid(d, to)) {
    set_field(d, DOLL_DP_X, to.x);
    set_field(d, DOLL_DP_Y, to.y);
  }
  walk_on(d);
}

static void dash(Doll* d) {
  const uint16_t left = (uint16_t)(field(d, DOLL_DP_DASH) - 1);
  set_field(d, DOLL_DP_DASH, left);
  if (negative(left)) {
    PORT_COVER(doll_dash_spent);
    d->log->dash_spent = true;
    seek_begin(d);
    return;
  }
  dash_step(d);
  dash_step(d);
}

// `$81:B108`: charge the target, which is straight ahead, until 16 short.
static void dash_start(Doll* d) {
  look_at(d, field(d, DOLL_DP_TARGET));
  face_towards(d);
  aim(d);
  const uint16_t run =
      minus(d, plus(d, field(d, DOLL_DP_AIM_DX), field(d, DOLL_DP_AIM_DY)),
            DOLL_DASH_SHORT);
  if (negative(run)) {
    PORT_COVER(doll_dash_short);
    d->log->dash_short = true;
    seek_begin(d);
    return;
  }
  PORT_COVER(doll_dashed);
  set_carry(d, run & 1);  // the `LSR`
  set_field(d, DOLL_DP_DASH, (uint16_t)((run >> 1) + 1));
  set_state(d, DOLL_STATE_DASH);
  dash(d);
}

// `$81:AF4E`: hit, so fly back the opposite way to its facing, unless solid
// ground or the edge of the level is there.
static DollKnock knock_back(Doll* d) {
  const uint16_t facing = field(d, DOLL_DP_FACING);
  if (same(d, facing, DOLL_NO_FACING)) {
    PORT_COVER(doll_knock_faceless);
    return DOLL_KNOCK_FACELESS;
  }
  const Point me = position(d);
  Point to;
  to.y = plus(d, me.y, table_word(d, DOLL_KNOCKED_TO + 2, facing));
  to.x = plus(d, me.x, table_word(d, DOLL_KNOCKED_TO, facing));
  if (ground_is_solid(d, to)) {
    PORT_COVER(doll_knock_ground);
    return DOLL_KNOCK_GROUND;
  }
  if (off_the_level(d, to)) {
    PORT_COVER(doll_knock_edge);
    return DOLL_KNOCK_EDGE;
  }
  PORT_COVER(doll_knocked_back);
  fly_to(d, to);
  set_state(d, DOLL_STATE_KNOCKED);
  take_off(d);
  return DOLL_KNOCK_FLEW;
}

// ---------------------------------------------------------------------------
// The state bodies
// ---------------------------------------------------------------------------

// Close in, and attack: the decision is made afresh every frame.
static DollSeek seek(Doll* d) {
  hide_axe(d);
  if (field(d, DOLL_DP_HIT) != 0) {
    set_field(d, DOLL_DP_HIT, 0);
    d->log->knock = knock_back(d);
    return DOLL_KNOCKED_BACK;
  }

  // Whoever is nearest, or failing that the nearer player.
  uint16_t dist;
  uint16_t target = nearest_actor(d, &dist);
  if (at_least(d, dist, DOLL_REACH)) {
    d->log->far = true;
    target = player_near(d);
    if (target == 0) {
      PORT_COVER(doll_left);
      set_field(d, DOLL_DP_LEAVE, (uint16_t)(field(d, DOLL_DP_LEAVE) - 1));
      return DOLL_LEFT;
    }
  }
  set_field(d, DOLL_DP_TARGET, target);

  const Point me = position(d);
  const Point there = record_position(d, target);
  const uint16_t gap_x =
      magnitude(minus(d, me.x, there.x), &d->log->gaps_negated);
  set_field(d, DOLL_DP_GAP_X, gap_x);
  const uint16_t gap_y =
      magnitude(minus(d, me.y, there.y), &d->log->gaps_negated);
  set_field(d, DOLL_DP_GAP_Y, gap_y);

  if (at_least(d, DOLL_SWING_REACH, gap_x)) {
    d->log->near_across = true;
    if (at_least(d, DOLL_SWING_REACH, gap_y)) {
      swing(d);
      return DOLL_SWUNG;
    }
  }

  const uint16_t sum = plus(d, gap_y, gap_x);
  if (sum == 0) {
    swing(d);
    return DOLL_SWUNG_ON_TOP;
  }
  // One gap is zero: lined up, so it charges.
  if (same(d, sum, gap_y)) {
    dash_start(d);
    return DOLL_DASHED_DOWN;
  }
  if (same(d, sum, gap_x)) {
    dash_start(d);
    return DOLL_DASHED_ACROSS;
  }
  if (same(d, gap_x, gap_y)) {
    PORT_COVER(doll_on_diagonal);
    throw_axe(d);
    step(d);
    return DOLL_THREW_DIAGONAL;
  }
  if (!at_least(d, random_byte(d), 1)) {
    throw_axe(d);
    return DOLL_THREW_AT_RANDOM;
  }
  step(d);
  return DOLL_STEPPED;
}

static void knocked(Doll* d) {
  rise(d);
  fly(d);
  if (height(d) != 0) return;
  PORT_COVER(doll_landed);
  d->log->landed = true;
  set_state(d, DOLL_STATE_SEEK_BEGIN);
  throw_on_landing(d);
}

// The jump out of the toy box. The doll changes picture on the frames its
// step timer runs out, and lands at or below the floor.
static void leap_out(Doll* d) {
  if (field(d, DOLL_DP_STEP_WAIT) == 0) {
    d->log->showed = true;
    const uint16_t stride = field(d, DOLL_DP_STRIDE) ^ 1;
    set_field(d, DOLL_DP_STRIDE, stride);
    show_frame(d, stride);
  }
  rise(d);
  fly(d);
  const uint16_t z = height(d);
  if (z != 0 && !negative(z)) return;
  PORT_COVER(doll_leapt_out);
  d->log->landed = true;
  d->log->landed_below = z != 0;
  set_state(d, DOLL_STATE_SEEK_BEGIN);
  set_record_field(d, d->record, ACTOR_Z, 0);
  // `$80:8475`: from now on a hit reaches it.
  const uint16_t slot = wram_r16(d->w, W_SCHED_CUR_TASK);
  wram_w16(d->w, (uint16_t)(W_THREAD_HANDLER + slot), DOLL_COLLIDE);
  wram_w16(d->w, (uint16_t)(W_THREAD_HANDLER_BANK + slot), DOLL_BANK);
}

// The throw cooldown counts down to -1 and stays there, the step timer goes
// round, and the picture goes where the doll is.
static void tick(Doll* d) {
  const uint16_t wait = field(d, DOLL_DP_THROW_WAIT);
  if (!negative(wait)) {
    set_field(d, DOLL_DP_THROW_WAIT, (uint16_t)(wait - 1));
    d->log->tick_threw = true;
  }
  const uint16_t step_wait = (uint16_t)(field(d, DOLL_DP_STEP_WAIT) - 1);
  set_field(d, DOLL_DP_STEP_WAIT, step_wait);
  if (negative(step_wait)) {
    set_field(d, DOLL_DP_STEP_WAIT, field(d, DOLL_DP_STEP_EVERY));
    d->log->tick_reset = true;
  }
  const Point me = position(d);
  set_record_field(d, d->record, ACTOR_X, me.x);
  set_record_field(d, d->record, ACTOR_Y, me.y);
}

// ---------------------------------------------------------------------------

typedef enum { SEEK_BEGIN, SEEK, DASH, KNOCKED, LEAP_OUT, TICK, FRAME } Body;

// Which body a state address is, or false for one that is not here.
static bool body_of(uint16_t state, Body* body) {
  switch (state) {
    case DOLL_STATE_SEEK_BEGIN: *body = SEEK_BEGIN; return true;
    case DOLL_STATE_SEEK: *body = SEEK; return true;
    case DOLL_STATE_DASH: *body = DASH; return true;
    case DOLL_STATE_KNOCKED: *body = KNOCKED; return true;
    case DOLL_STATE_LEAP_OUT: *body = LEAP_OUT; return true;
    default: return false;
  }
}

static void act(Doll* d, Body body) {
  switch (body) {
    case SEEK_BEGIN: seek_begin(d); break;
    case SEEK: d->log->seek = seek(d); break;
    case DASH: dash(d); break;
    case KNOCKED: knocked(d); break;
    case LEAP_OUT: leap_out(d); break;
    default: break;
  }
}

// The loop's frame: the state body, the tick, and unless it is leaving, the
// hit forgotten for the next.
static bool frame(Doll* d) {
  Body body = SEEK;
  d->log->state = field(d, DOLL_DP_STATE);
  body_of(d->log->state, &body);
  act(d, body);
  tick(d);
  if (field(d, DOLL_DP_LEAVE) != 0) return false;
  set_field(d, DOLL_DP_HIT_ID, 0);
  return true;
}

static bool run(Body body, Wram* w, const Rom* rom, uint16_t page,
                DollLog* log) {
  DollLog scratch = {0};
  Doll d = {w, rom, page, wram_r16(w, (uint16_t)(page + DOLL_DP_RECORD)),
            log ? log : &scratch, {false, false, false, false}};
  bool stays = true;
  if (body == FRAME) stays = frame(&d);
  else if (body == TICK) tick(&d);
  else act(&d, body);
  d.log->c = d.flags.c;
  d.log->v = d.flags.v;
  d.log->c_set = d.flags.c_set;
  d.log->v_set = d.flags.v_set;
  return stays;
}

bool doll_frame_supported(const Wram* w, uint16_t page) {
  Body body;
  return body_of(wram_r16(w, (uint16_t)(page + DOLL_DP_STATE)), &body);
}

bool doll_frame(Wram* w, const Rom* rom, uint16_t page, DollLog* log) {
  return run(FRAME, w, rom, page, log);
}

void doll_seek_begin(Wram* w, const Rom* rom, uint16_t page, DollLog* log) {
  run(SEEK_BEGIN, w, rom, page, log);
}

void doll_seek(Wram* w, const Rom* rom, uint16_t page, DollLog* log) {
  run(SEEK, w, rom, page, log);
}

void doll_dash(Wram* w, const Rom* rom, uint16_t page, DollLog* log) {
  run(DASH, w, rom, page, log);
}

void doll_knocked(Wram* w, const Rom* rom, uint16_t page, DollLog* log) {
  run(KNOCKED, w, rom, page, log);
}

void doll_leap_out(Wram* w, const Rom* rom, uint16_t page, DollLog* log) {
  run(LEAP_OUT, w, rom, page, log);
}

void doll_tick(Wram* w, uint16_t page, DollLog* log) {
  run(TICK, w, NULL, page, log);
}
