// The zombies' state bodies -- see port/zombie.h.

#include "port/zombie.h"

#include <stddef.h>

#include "port/coverage.h"
#include "port/cpu.h"  // add16_overflows
#include "port/rng.h"
#include "port/terrain.h"

typedef struct {
  uint16_t x, y;
} Point;

// What tells the two kinds apart.
typedef struct {
  uint16_t steps;        // the step table, a pixel a step or two
  uint16_t walk;         // the three state bodies, as `$14` names them
  uint16_t follow_wall;
  uint16_t chase;
  uint16_t notice;       // decide: chase what comes nearer than this
  uint16_t lose;         // chase: give up on what is this far or further
  uint32_t reach_at;     // `LDA #$00D0`'s operand: how far a player may be
} Kind;

static const Kind KINDS[ZOMBIE_KINDS] = {
    [ZOMBIE_SLOW] = {.steps = 0x858b,
                     .walk = 0x8600,
                     .follow_wall = 0x8656,
                     .chase = 0x86b3,
                     .notice = 0x0041,
                     .lose = 0x0046,
                     .reach_at = 0x818717},
    [ZOMBIE_FAST] = {.steps = 0x85af,
                     .walk = 0x89bf,
                     .follow_wall = 0x8a72,
                     .chase = 0x8ad8,
                     .notice = 0x00a0,
                     .lose = 0x00b4,
                     .reach_at = 0x818b47},
};

// A slow zombie turns a quarter clockwise at a wall.
#define SLOW_TURN 4

// One zombie's frame.
typedef struct {
  Wram* w;
  const Rom* rom;
  uint16_t page;
  uint16_t record;
  ZombieKind kind;
  ZombieLog* log;  // may be NULL
} Zombie;

static uint16_t field(const Zombie* z, uint16_t at) {
  return wram_r16(z->w, (uint16_t)(z->page + at));
}

static void set_field(Zombie* z, uint16_t at, uint16_t v) {
  wram_w16(z->w, (uint16_t)(z->page + at), v);
}

static Point position(const Zombie* z) {
  return (Point){field(z, ZOMBIE_DP_X), field(z, ZOMBIE_DP_Y)};
}

// Where it is, on its page and on its record together.
static void move_to(Zombie* z, Point p) {
  set_field(z, ZOMBIE_DP_X, p.x);
  wram_w16(z->w, (uint16_t)(z->record + ACTOR_X), p.x);
  set_field(z, ZOMBIE_DP_Y, p.y);
  wram_w16(z->w, (uint16_t)(z->record + ACTOR_Y), p.y);
}

static uint16_t heading(const Zombie* z) { return field(z, ZOMBIE_DP_HEADING); }

static void set_state(Zombie* z, uint16_t body) {
  set_field(z, ZOMBIE_DP_STATE, body);
}

uint16_t zombie_step_at(ZombieKind kind, uint16_t heading) {
  return (uint16_t)(KINDS[kind].steps + heading * 2);
}

// One step along `h` from `from`. The point tried is left in `$1A`/`$1C`.
static Point step_from(Zombie* z, Point from, uint16_t h) {
  const uint32_t at = ((uint32_t)ZOMBIE_BANK << 16) | zombie_step_at(z->kind, h);
  const Point to = {(uint16_t)(from.x + rom_word(z->rom, at)),
                    (uint16_t)(from.y + rom_word(z->rom, at + 2))};
  set_field(z, ZOMBIE_DP_NEXT_X, to.x);
  set_field(z, ZOMBIE_DP_NEXT_Y, to.y);
  return to;
}

static uint16_t turn_amount(const Zombie* z) {
  return z->kind == ZOMBIE_SLOW ? SLOW_TURN : field(z, ZOMBIE_DP_TURN);
}

// A heading turned clockwise by `by`, wrapping round the eight.
static uint16_t turned(uint16_t h, uint16_t by) {
  return (uint16_t)((((uint16_t)(h - 2) + by) & 15) + 2);
}

// ---------------------------------------------------------------------------
// What it asks the rest of the game
// ---------------------------------------------------------------------------

// Each question is a probe in the log, and leaves carry and overflow behind.
static ZombieProbe* new_probe(Zombie* z) {
  if (!z->log || z->log->probes == ZOMBIE_MAX_PROBES) return NULL;
  ZombieProbe* p = &z->log->probe[z->log->probes++];
  *p = (ZombieProbe){false, 0, false, false};
  return p;
}

static void flags_left(Zombie* z, bool c, bool v) {
  if (z->log) {
    z->log->carry = c;
    z->log->overflow = v;
  }
}

static bool ground_is_solid(Zombie* z, Point p, ZombieProbe* probe) {
  TerrainRegs r;
  terrain_blocked_enemy(z->w, p.x, p.y, &r);
  flags_left(z, r.blocked, r.v);
  if (probe) {
    probe->ground = r.blocked;
    probe->tiles = r.probes;
  }
  return r.blocked;
}

static bool someone_at(Zombie* z, Point p, ZombieProbe* probe) {
  AtPointRegs r;
  AtPointWork work;
  actor_at_point_counted(z->w, z->record, p.x, p.y, &r, &work);
  if (z->log) {
    z->log->carry = r.found;
    if (r.v_set) z->log->overflow = r.v;
    for (int i = 0; i < AT_POINT_BLOCK_COUNT; i++)
      z->log->at_point.blocks[i] += work.blocks[i];
  }
  if (probe) {
    probe->asked = true;
    probe->someone = r.found;
  }
  return r.found;
}

// May it stand at `p`? Solid ground or anyone standing there says no.
static bool can_stand_at(Zombie* z, Point p) {
  ZombieProbe* probe = new_probe(z);
  return !ground_is_solid(z, p, probe) && !someone_at(z, p, probe);
}

static uint16_t nearest_actor(Zombie* z, uint16_t* dist) {
  const Point me = position(z);
  ActorNearestWork work;
  const uint16_t found = actor_nearest_counted(z->w, me.x, me.y, dist, &work);
  if (z->log) {
    z->log->nearest = work;
    z->log->asked_nearest = true;
  }
  return found;
}

// Is either player near enough for it to stay?
static bool player_about(Zombie* z) {
  const Point me = position(z);
  PlayerPickRegs r;
  player_bearing(z->w, z->rom, rom_word(z->rom, KINDS[z->kind].reach_at), me.x,
                 me.y, &r);
  if (z->log) z->log->players = r;
  return r.a != 0;
}

// ---------------------------------------------------------------------------
// Walking
// ---------------------------------------------------------------------------

// At a wall: turn, and follow it from now on.
static void turn(Zombie* z) {
  PORT_COVER(zombie_turned);
  const uint16_t from = heading(z);
  const uint16_t by = turn_amount(z);
  set_field(z, ZOMBIE_DP_HEADING, turned(from, by));
  set_state(z, KINDS[z->kind].follow_wall);
  // `DEC : DEC : CLC : ADC` is the last to write carry and overflow.
  const uint16_t less_two = (uint16_t)(from - 2);
  flags_left(z, (uint32_t)less_two + by > 0xffffu,
             add16_overflows(less_two, by));
  if (z->log) z->log->turned = true;
}

static void walk(Zombie* z) {
  ZombieProbe* probe = new_probe(z);
  const Point to = step_from(z, position(z), heading(z));
  if (ground_is_solid(z, to, probe)) {
    turn(z);
    return;
  }
  // Someone in the way is waited for, not walked around.
  if (someone_at(z, to, probe)) {
    PORT_COVER(zombie_waited);
    return;
  }
  PORT_COVER(zombie_walked);
  move_to(z, to);
}

static void follow_wall(Zombie* z) {
  if (z->kind == ZOMBIE_FAST)
    set_field(z, ZOMBIE_DP_QUIET, (uint16_t)(field(z, ZOMBIE_DP_QUIET) - 1));

  // Back towards the wall, if it has given way.
  const uint16_t back = turned(heading(z), (uint16_t)-turn_amount(z));
  set_field(z, ZOMBIE_DP_TRYING, back);
  if (can_stand_at(z, step_from(z, position(z), back))) {
    PORT_COVER(zombie_rounded_corner);
    set_field(z, ZOMBIE_DP_HEADING, back);
  }

  const Point to = step_from(z, position(z), heading(z));
  if (can_stand_at(z, to)) {
    PORT_COVER(zombie_followed_wall);
    move_to(z, to);
    return;
  }
  turn(z);
}

// A random straight heading, and a walk along it this frame.
static void wander(Zombie* z, bool carry) {
  PORT_COVER(zombie_wandered);
  RngResult r;
  rng_next(z->w, carry, &r);
  set_field(z, ZOMBIE_DP_HEADING, (uint16_t)((r.a & 3) * 4 + 2));
  set_state(z, KINDS[z->kind].walk);
  if (z->log) {
    z->log->wandered = true;
    z->log->drew_overflow = r.v;
  }
  walk(z);
}

// ---------------------------------------------------------------------------
// Chasing
// ---------------------------------------------------------------------------

// The slow kind: two one-pixel steps along the bearing, each refused outright.
static void plod_towards(Zombie* z) {
  for (int i = 0; i < 2; i++) {
    const Point to = step_from(z, position(z), heading(z));
    if (can_stand_at(z, to)) move_to(z, to);
  }
}

// The fast kind's step, `$81:89FD`: across and then up or down, each on its
// own, so a step blocked one way still slides the other. False when it went
// nowhere.
static bool slide_to(Zombie* z, Point to) {
  const Point was = position(z);
  set_field(z, ZOMBIE_DP_WAS_X, was.x);
  set_field(z, ZOMBIE_DP_WAS_Y, was.y);

  ZombieProbe* across = new_probe(z);
  const Point side = {to.x, was.y};
  if (!ground_is_solid(z, side, across) && !someone_at(z, side, across))
    set_field(z, ZOMBIE_DP_X, to.x);

  ZombieProbe* up_down = new_probe(z);
  const Point ahead = {field(z, ZOMBIE_DP_X), to.y};
  if (!ground_is_solid(z, ahead, up_down) && !someone_at(z, ahead, up_down))
    set_field(z, ZOMBIE_DP_Y, to.y);

  const Point now = position(z);
  const bool moved = now.x != was.x || now.y != was.y;
  if (moved) set_field(z, ZOMBIE_DP_QUIET, 0);
  if (z->log) {
    z->log->carry = !moved;
    z->log->moved_across = now.x != was.x;
  }
  return moved;
}

// The fast kind: one two-pixel step along the bearing, sliding if it must. A
// step that goes nowhere ends the chase.
static void stride_towards(Zombie* z) {
  set_field(z, ZOMBIE_DP_STEPS, 1);
  do {
    const uint16_t index = field(z, ZOMBIE_DP_CHASE_INDEX);
    if (!slide_to(z, step_from(z, position(z), (uint16_t)(index >> 1)))) {
      PORT_COVER(zombie_chase_stuck);
      if (z->log) z->log->stuck = true;
      set_field(z, ZOMBIE_DP_QUIET, 0);
      wander(z, true);
      return;
    }
    const Point now = position(z);
    wram_w16(z->w, (uint16_t)(z->record + ACTOR_X), now.x);
    wram_w16(z->w, (uint16_t)(z->record + ACTOR_Y), now.y);
    set_field(z, ZOMBIE_DP_STEPS, (uint16_t)(field(z, ZOMBIE_DP_STEPS) - 1));
  } while (field(z, ZOMBIE_DP_STEPS) != 0);
}

static void chase(Zombie* z) {
  uint16_t dist;
  const uint16_t target = nearest_actor(z, &dist);
  if (z->kind == ZOMBIE_SLOW) set_field(z, ZOMBIE_DP_TARGET, target);
  if (dist >= KINDS[z->kind].lose) {
    PORT_COVER(zombie_lost_target);
    if (z->log) z->log->lost = true;
    wander(z, true);  // the range test's carry
    return;
  }

  // Within a pixel of lining up, it snaps into line. The snap moves only the
  // record: the page keeps where it was, and the step is from there.
  ActorSnapRegs snapped;
  actor_snap_to(z->w, z->record, target, &snapped);
  ActorBearingRegs bearing;
  actor_bearing(z->w, z->rom, z->record, target, &bearing);
  if (z->log) {
    z->log->snap = snapped;
    z->log->bearing = bearing;
  }
  if (bearing.a == 0) {
    PORT_COVER(zombie_on_top);
    if (z->log) z->log->lost = z->log->on_top = true;
    wander(z, bearing.c);
    return;
  }
  set_field(z, ZOMBIE_DP_HEADING, (uint16_t)(bearing.a * 2));
  set_field(z, ZOMBIE_DP_CHASE_INDEX, (uint16_t)(bearing.a * 4));

  PORT_COVER(zombie_chased);
  if (z->kind == ZOMBIE_SLOW) plod_towards(z);
  else stride_towards(z);
}

// ---------------------------------------------------------------------------
// Deciding, and the walk cycle
// ---------------------------------------------------------------------------

static void decide(Zombie* z) {
  if (z->kind == ZOMBIE_FAST) {
    const uint16_t quiet = (uint16_t)(field(z, ZOMBIE_DP_QUIET) - 1);
    set_field(z, ZOMBIE_DP_QUIET, quiet);
    if (!(quiet & 0x8000u)) {
      PORT_COVER(zombie_quiet);
      if (z->log) z->log->quiet = true;
      return;
    }
  }

  uint16_t dist;
  nearest_actor(z, &dist);
  if (dist < KINDS[z->kind].notice) {
    PORT_COVER(zombie_noticed);
    set_state(z, KINDS[z->kind].chase);
    return;
  }
  if (z->log) z->log->far = true;
  if (!player_about(z)) {
    PORT_COVER(zombie_left);
    if (z->log) z->log->nobody = true;
    set_field(z, ZOMBIE_DP_LEAVE, (uint16_t)(field(z, ZOMBIE_DP_LEAVE) - 1));
  }
}

// Four frames a leg, four legs, and the frames for a heading facing left are
// the ones facing right, drawn flipped.
#define ANIMATE_PERIOD 4
#define ANIMATE_META_BANK 0x0090
#define ANIMATE_FLIP_FROM 0x0030
#define ANIMATE_FLIP_X 0x0002

static void animate(Zombie* z, uint16_t frames) {
  const uint16_t timer = (uint16_t)(field(z, ZOMBIE_DP_TIMER) - 1);
  set_field(z, ZOMBIE_DP_TIMER, timer);
  if (timer != 0) return;

  set_field(z, ZOMBIE_DP_TIMER, ANIMATE_PERIOD);
  const uint16_t leg = (uint16_t)((field(z, ZOMBIE_DP_LEG) + 2) & 7);
  set_field(z, ZOMBIE_DP_LEG, leg);
  const uint16_t frame = (uint16_t)((heading(z) << 2) | leg);
  const uint32_t at = ((uint32_t)ZOMBIE_BANK << 16) | (uint16_t)(frames + frame);
  wram_w16(z->w, (uint16_t)(z->record + ACTOR_META), rom_word(z->rom, at));
  wram_w16(z->w, (uint16_t)(z->record + ACTOR_META_BANK), ANIMATE_META_BANK);

  const bool mirrored = frame >= ANIMATE_FLIP_FROM;
  const uint16_t flags = wram_r16(z->w, (uint16_t)(z->record + ACTOR_FLAGS));
  wram_w16(z->w, (uint16_t)(z->record + ACTOR_FLAGS),
           mirrored ? (uint16_t)(flags | ANIMATE_FLIP_X)
                    : (uint16_t)(flags & ~ANIMATE_FLIP_X));
  if (z->log) {
    z->log->new_leg = true;
    z->log->mirrored = mirrored;
    z->log->carry = mirrored;
  }
}

// ---------------------------------------------------------------------------

static Zombie zombie(Wram* w, const Rom* rom, uint16_t page, ZombieKind kind,
                     ZombieLog* log) {
  return (Zombie){w, rom, page,
                  wram_r16(w, (uint16_t)(page + ZOMBIE_DP_RECORD)), kind, log};
}

void zombie_walk(Wram* w, const Rom* rom, uint16_t page, ZombieKind kind,
                 ZombieLog* log) {
  Zombie z = zombie(w, rom, page, kind, log);
  walk(&z);
}

void zombie_follow_wall(Wram* w, const Rom* rom, uint16_t page,
                        ZombieKind kind, ZombieLog* log) {
  Zombie z = zombie(w, rom, page, kind, log);
  follow_wall(&z);
}

void zombie_chase(Wram* w, const Rom* rom, uint16_t page, ZombieKind kind,
                  ZombieLog* log) {
  Zombie z = zombie(w, rom, page, kind, log);
  chase(&z);
}

void zombie_decide(Wram* w, const Rom* rom, uint16_t page, ZombieKind kind,
                   ZombieLog* log) {
  Zombie z = zombie(w, rom, page, kind, log);
  decide(&z);
}

void zombie_animate(Wram* w, const Rom* rom, uint16_t page, uint16_t frames,
                    ZombieLog* log) {
  Zombie z = zombie(w, rom, page, ZOMBIE_SLOW, log);
  animate(&z, frames);
}

// ---------------------------------------------------------------------------
// A whole frame
// ---------------------------------------------------------------------------

// Which of the three state bodies `$14` names, or false for any other.
static bool state_of(const Zombie* z, ZombieState* state) {
  const Kind* k = &KINDS[z->kind];
  const uint16_t body = field(z, ZOMBIE_DP_STATE);
  if (body == k->walk) *state = ZOMBIE_WALKING;
  else if (body == k->follow_wall) *state = ZOMBIE_FOLLOWING;
  else if (body == k->chase) *state = ZOMBIE_CHASING;
  else return false;
  return true;
}

// What tells the three threads' loops apart.
typedef struct {
  ZombieKind kind;
  uint16_t frames;     // the animation's frame table
  bool checks_unmark;  // looks at `$22` before deciding
} Thread;

static const Thread THREADS[ZOMBIE_THREADS] = {
    [ZOMBIE_THREAD_87F8] = {ZOMBIE_SLOW, ZOMBIE_FRAMES, false},
    [ZOMBIE_THREAD_88CA] = {ZOMBIE_FAST, ZOMBIE_FRAMES, true},
    [ZOMBIE_THREAD_8C17] = {ZOMBIE_FAST, ZOMBIE_FRAMES_8C17, false},
};

ZombieKind zombie_thread_kind(ZombieThread thread) {
  return THREADS[thread].kind;
}

bool zombie_frame_supported(const Wram* w, uint16_t page, ZombieThread thread) {
  // Reading only, through a struct that could write.
  const Zombie z = zombie((Wram*)w, NULL, page, THREADS[thread].kind, NULL);
  ZombieState state;
  return state_of(&z, &state);
}

bool zombie_frame(Wram* w, const Rom* rom, uint16_t page, ZombieThread thread,
                  ZombieFrameLog* log) {
  const Thread* t = &THREADS[thread];
  ZombieFrameLog scratch = {0};
  if (!log) log = &scratch;
  Zombie z = zombie(w, rom, page, t->kind, &log->decide);

  log->unmarked = t->checks_unmark && field(&z, ZOMBIE_DP_UNMARK) != 0;
  if (log->unmarked) {
    const uint16_t flags = wram_r16(w, (uint16_t)(z.record + ACTOR_FLAGS));
    wram_w16(w, (uint16_t)(z.record + ACTOR_FLAGS),
             (uint16_t)(flags & ~ZOMBIE_RECORD_MARK));
    set_field(&z, ZOMBIE_DP_UNMARK, 0);
  } else {
    decide(&z);
  }

  // The decision may have just made it a chase.
  z.log = &log->act;
  log->state = ZOMBIE_WALKING;
  state_of(&z, &log->state);
  switch (log->state) {
    case ZOMBIE_WALKING: walk(&z); break;
    case ZOMBIE_FOLLOWING: follow_wall(&z); break;
    case ZOMBIE_CHASING: chase(&z); break;
  }

  z.log = &log->animate;
  animate(&z, t->frames);
  return field(&z, ZOMBIE_DP_LEAVE) == 0;
}
