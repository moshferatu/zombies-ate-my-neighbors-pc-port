// $83:B299  the spiders' frame -- see port/spider.h.

#include "port/spider.h"

#include <stddef.h>

#include "port/coverage.h"
#include "port/flags.h"
#include "port/rng.h"

typedef struct {
  uint16_t x, y;
} Point;

// How near someone has to be for it to take aim, and how far before it
// thinks of leaving.
#define SPIDER_AIM_WITHIN 0x00b4
#define SPIDER_ALONE_BEYOND 0x00f0
// Taking aim, how far its target may be before it gives up.
#define SPIDER_GIVE_UP_BEYOND 0x00dc
// The draw that sets how long it runs, and whether it runs a turn wide.
#define SPIDER_RUN_PASSES_MASK 0x0007
#define SPIDER_RUN_WIDE_MASK 0x0001
// The bit of a running pass's draw that makes it one step and not two.
#define SPIDER_ONE_STEP_MASK 0x0002
// Passes a picture lasts, less one.
#define SPIDER_PICTURE_PASSES 2
#define SPIDER_MIRROR 0x0002
#define SPIDER_QUARTER_TURN 4
#define SPIDER_DIRECTION_MAX 16

// `$83:B253`: the step for each direction, across and down, at twice the
// doubled direction.
#define SPIDER_STEPS 0xb253u
// `$83:B30E`: the two pictures for each direction, at twice the doubled
// direction plus the cycle. Those from `SPIDER_FIRST_MIRRORED` on face left.
#define SPIDER_PICTURES 0xb30eu
#define SPIDER_FIRST_MIRRORED 0x0018
// `$83:B431`: how far a player may be before it leaves, the operand of
// `LDA #$00F0`. Read from the cartridge because a wider picture widens it.
#define SPIDER_REACH 0xb431u

// One spider's frame, and the carry and overflow the ROM would leave.
typedef struct {
  Wram* w;
  const Rom* rom;
  uint16_t page;
  uint16_t record;
  SpiderLog* log;  // never NULL here
  PortFlags flags;
} Spider;

static uint16_t field(const Spider* s, uint16_t at) {
  return wram_r16(s->w, (uint16_t)(s->page + at));
}

static void set_field(Spider* s, uint16_t at, uint16_t v) {
  wram_w16(s->w, (uint16_t)(s->page + at), v);
}

static uint16_t record_field(const Spider* s, uint16_t at) {
  return wram_r16(s->w, (uint16_t)(s->record + at));
}

static void set_record_field(Spider* s, uint16_t at, uint16_t v) {
  wram_w16(s->w, (uint16_t)(s->record + at), v);
}

static uint16_t table_word(const Spider* s, uint16_t table, uint16_t index) {
  return rom_word(s->rom, ((uint32_t)SPIDER_BANK << 16) +
                              (uint16_t)(table + index));
}

static Point position(const Spider* s) {
  return (Point){field(s, SPIDER_DP_X), field(s, SPIDER_DP_Y)};
}

static void move_to(Spider* s, Point p) {
  set_field(s, SPIDER_DP_X, p.x);
  set_field(s, SPIDER_DP_Y, p.y);
}

static uint16_t random_byte(Spider* s, bool* overflowed) {
  RngResult draw;
  rng_next(s->w, s->flags.c, &draw);
  flags_carry(&s->flags, draw.c);
  flags_overflow(&s->flags, draw.v);
  *overflowed = draw.v;
  return draw.a;
}

// ---------------------------------------------------------------------------
// Stepping
// ---------------------------------------------------------------------------

// Where a step along `direction` would end. It is written down as the step
// being tried.
static Point step_end(Spider* s, uint16_t direction) {
  const uint16_t at = flags_double(&s->flags, direction);
  const Point from = position(s);
  const Point to = {
      flags_add(&s->flags, table_word(s, SPIDER_STEPS, at), from.x),
      flags_add(&s->flags, table_word(s, SPIDER_STEPS + 2, at), from.y)};
  set_field(s, SPIDER_DP_TRY_X, to.x);
  set_field(s, SPIDER_DP_TRY_Y, to.y);
  return to;
}

// May it stand at `p`? Solid ground or anyone standing there says no. Both
// tests leave carry, and whichever ran last an overflow.
static bool can_stand_at(Spider* s, Point p, SpiderProbe* probe) {
  probe->asked = true;
  terrain_blocked_enemy(s->w, p.x, p.y, &probe->ground);
  flags_carry(&s->flags, probe->ground.blocked);
  flags_overflow(&s->flags, probe->ground.v);
  if (probe->ground.blocked) return false;

  AtPointRegs r;
  AtPointWork work;
  actor_at_point_counted(s->w, s->record, p.x, p.y, &r, &work);
  for (int i = 0; i < AT_POINT_BLOCK_COUNT; i++)
    s->log->at_point.blocks[i] += work.blocks[i];
  probe->someone = r.found;
  flags_carry(&s->flags, r.found);
  if (r.v_set) flags_overflow(&s->flags, r.v);
  return !r.found;
}

static uint16_t turned(Spider* s, uint16_t direction, int quarters) {
  const uint16_t from_up = (uint16_t)(direction - 2);
  const uint16_t round =
      quarters > 0 ? flags_add(&s->flags, from_up, SPIDER_QUARTER_TURN)
                   : flags_sub(&s->flags, from_up, SPIDER_QUARTER_TURN);
  return (uint16_t)((round & 0x000f) + 2);
}

// `$83:B445`: a quarter clockwise, and feel along from there.
static void turn(Spider* s) {
  PORT_COVER(spider_turned);
  set_field(s, SPIDER_DP_DIRECTION,
            turned(s, field(s, SPIDER_DP_DIRECTION), 1));
  set_field(s, SPIDER_DP_STATE, SPIDER_STATE_FEEL);
}

// The step wandering and feeling end on: all of it, or a turn.
static void step_or_turn(Spider* s) {
  const Point to = step_end(s, field(s, SPIDER_DP_DIRECTION));
  if (can_stand_at(s, to, &s->log->ahead))
    move_to(s, to);
  else
    turn(s);
}

// `$83:B50D`: a running step. Across if it may, and then down from wherever
// that left it.
static void run_step(Spider* s, SpiderProbe* probes) {
  const Point to = step_end(s, field(s, SPIDER_DP_DIRECTION));
  Point me = position(s);
  if (can_stand_at(s, (Point){to.x, me.y}, &probes[0])) {
    me.x = to.x;
    set_field(s, SPIDER_DP_X, me.x);
  }
  if (can_stand_at(s, (Point){me.x, to.y}, &probes[1])) {
    me.y = to.y;
    set_field(s, SPIDER_DP_Y, me.y);
  }
}

// ---------------------------------------------------------------------------
// Looking and taking aim
// ---------------------------------------------------------------------------

// Whoever `actor_nearest` knows, and how far. It leaves carry and overflow
// clear.
static uint16_t nearest_actor(Spider* s, uint16_t* dist, ActorNearestWork* work) {
  const Point me = position(s);
  const uint16_t found = actor_nearest_counted(s->w, me.x, me.y, dist, work);
  flags_carry(&s->flags, false);
  flags_overflow(&s->flags, false);
  return found;
}

// `$83:B4C4`: run at whoever is nearest, or a turn of eight wide of them, for
// up to seven passes. With nobody near enough it wanders.
static void take_aim(Spider* s) {
  SpiderLog* log = s->log;
  log->aimed = true;
  set_field(s, SPIDER_DP_STATE, SPIDER_STATE_RUN);
  uint16_t dist = 0;
  const uint16_t target = nearest_actor(s, &dist, &log->aim_nearest);
  set_field(s, SPIDER_DP_TARGET, target);
  if (flags_at_least(&s->flags, dist, SPIDER_GIVE_UP_BEYOND)) {
    PORT_COVER(spider_gave_up);
    log->gave_up = true;
    set_field(s, SPIDER_DP_STATE, SPIDER_STATE_WANDER);
    return;
  }
  PORT_COVER(spider_took_aim);

  actor_bearing(s->w, s->rom, s->record, target, &log->bearing);
  flags_carry(&s->flags, log->bearing.c);
  const uint16_t bearing = (uint16_t)(log->bearing.a - 1);
  set_field(s, SPIDER_DP_BEARING, bearing);

  const uint16_t draw =
      random_byte(s, &log->aim_overflow) & SPIDER_RUN_PASSES_MASK;
  set_field(s, SPIDER_DP_RUN_LEFT, draw);
  const uint16_t wide =
      flags_add(&s->flags, draw & SPIDER_RUN_WIDE_MASK, bearing);
  set_field(s, SPIDER_DP_DIRECTION,
            flags_double(&s->flags, (uint16_t)((wide & 7) + 1)));
}

// `$83:B41E`: take aim at anyone close, and leave if there is nobody at all.
static void look(Spider* s) {
  SpiderLog* log = s->log;
  uint16_t dist = 0;
  nearest_actor(s, &dist, &log->nearest);
  if (dist < SPIDER_AIM_WITHIN) {
    log->look = SPIDER_LOOK_NEAR;
    take_aim(s);
  } else if (dist < SPIDER_ALONE_BEYOND) {
    log->look = SPIDER_LOOK_MIDDLE;
  } else {
    const Point me = position(s);
    player_bearing(s->w, s->rom, table_word(s, SPIDER_REACH, 0), me.x, me.y,
                   &log->players);
    log->look = SPIDER_LOOK_FAR;
    if (log->players.a == 0) {
      PORT_COVER(spider_left);
      log->look = SPIDER_LOOK_LEAVE;
      set_field(s, SPIDER_DP_FATE, (uint16_t)(field(s, SPIDER_DP_FATE) - 1));
    }
  }
}

// ---------------------------------------------------------------------------
// The state bodies
// ---------------------------------------------------------------------------

// `$83:B46B`: straight on.
static void wander(Spider* s) {
  PORT_COVER(spider_wandered);
  look(s);
  step_or_turn(s);
}

// `$83:B499`: along whatever turned it. The turn back is taken if that way
// is clear, and then a step the way it faces, or another turn.
static void feel(Spider* s) {
  PORT_COVER(spider_felt);
  look(s);
  const uint16_t other = turned(s, field(s, SPIDER_DP_DIRECTION), -1);
  set_field(s, SPIDER_DP_OTHER_WAY, other);
  if (can_stand_at(s, step_end(s, other), &s->log->back)) {
    PORT_COVER(spider_turned_back);
    s->log->turned_back = true;
    set_field(s, SPIDER_DP_DIRECTION, other);
  }
  step_or_turn(s);
}

// `$83:B4FD`: at its target, a step or two a pass, and aim again when the
// passes run out.
static void run(Spider* s) {
  SpiderLog* log = s->log;
  const uint16_t left = (uint16_t)(field(s, SPIDER_DP_RUN_LEFT) - 1);
  set_field(s, SPIDER_DP_RUN_LEFT, left);
  if (left & 0x8000u) {
    take_aim(s);
    return;
  }
  PORT_COVER(spider_ran);
  log->ran = true;
  const bool once =
      (random_byte(s, &log->run_overflow) & SPIDER_ONE_STEP_MASK) != 0;
  log->steps = once ? 1 : 2;
  if (!once) PORT_COVER(spider_ran_twice);
  for (int i = 0; i < log->steps; i++) run_step(s, log->step[i]);
}

// `$83:B2D0`: the next of the direction's two pictures when the last has had
// its passes, and the record moved to where the thread is.
static void show(Spider* s) {
  const uint16_t left = (uint16_t)(field(s, SPIDER_DP_PICTURE_TIMER) - 1);
  set_field(s, SPIDER_DP_PICTURE_TIMER, left);
  if (left & 0x8000u) {
    s->log->new_picture = true;
    set_field(s, SPIDER_DP_PICTURE_TIMER, SPIDER_PICTURE_PASSES);
    const uint16_t cycle = (uint16_t)((field(s, SPIDER_DP_CYCLE) + 1) & 1);
    set_field(s, SPIDER_DP_CYCLE, cycle);
    const uint16_t at = flags_double(
        &s->flags, flags_add(&s->flags, cycle, field(s, SPIDER_DP_DIRECTION)));
    set_record_field(s, ACTOR_META, table_word(s, SPIDER_PICTURES, at));
    const uint16_t flags = record_field(s, ACTOR_FLAGS);
    s->log->mirrored = flags_at_least(&s->flags, at, SPIDER_FIRST_MIRRORED);
    set_record_field(s, ACTOR_FLAGS,
                     s->log->mirrored ? (uint16_t)(flags | SPIDER_MIRROR)
                                      : (uint16_t)(flags & ~SPIDER_MIRROR));
  }
  const Point me = position(s);
  set_record_field(s, ACTOR_X, me.x);
  set_record_field(s, ACTOR_Y, me.y);
}

// ---------------------------------------------------------------------------
// The frame
// ---------------------------------------------------------------------------

bool spider_frame_supported(const Wram* w, uint16_t page) {
  const uint16_t state = wram_r16(w, (uint16_t)(page + SPIDER_DP_STATE));
  const uint16_t direction =
      wram_r16(w, (uint16_t)(page + SPIDER_DP_DIRECTION));
  if (state != SPIDER_STATE_WANDER && state != SPIDER_STATE_FEEL &&
      state != SPIDER_STATE_RUN)
    return false;
  return direction <= SPIDER_DIRECTION_MAX &&
         wram_r16(w, (uint16_t)(page + SPIDER_DP_CYCLE)) <= 1;
}

bool spider_frame(Wram* w, const Rom* rom, uint16_t page, bool carry,
                  SpiderLog* log) {
  SpiderLog scratch = {0};
  Spider s = {w, rom, page, wram_r16(w, (uint16_t)(page + SPIDER_DP_RECORD)),
              log ? log : &scratch, {carry, false, false, false}};
  s.log->state = field(&s, SPIDER_DP_STATE);
  switch (s.log->state) {
    case SPIDER_STATE_WANDER: wander(&s); break;
    case SPIDER_STATE_FEEL: feel(&s); break;
    default: run(&s); break;
  }
  show(&s);

  s.log->c = s.flags.c;
  s.log->v = s.flags.v;
  s.log->c_set = s.flags.c_set;
  s.log->v_set = s.flags.v_set;
  if (field(&s, SPIDER_DP_FATE) != 0) return false;
  set_field(&s, SPIDER_DP_HIT_BY, 0);
  return true;
}
