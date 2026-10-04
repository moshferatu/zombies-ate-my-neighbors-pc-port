// $81:99F6, $81:9A5A  the martians' frame -- see port/martian.h.

#include "port/martian.h"

#include <stddef.h>

#include "port/coverage.h"
#include "port/flags.h"
#include "port/rng.h"

typedef struct {
  uint16_t x, y;
} Point;

// Walking: passes between looks, and the three distances it acts on.
#define MARTIAN_LOOK_PASSES 0x3c
#define MARTIAN_TOO_CLOSE 0x003c
#define MARTIAN_LINE_UP_WITHIN 0x0050
#define MARTIAN_APPROACH_WITHIN 0x00e0
// Arriving: passes between picking a side, how close the target has to be for
// it to pick the far one, and how far above the target it keeps.
#define MARTIAN_SIDE_PASSES 0x20
#define MARTIAN_SIDE_CLOSE 0x0020
#define MARTIAN_ABOVE_LEAST 0x0060
#define MARTIAN_ABOVE_MOST 0x0078
// ...and the draw that fires downwards has to come in under this.
#define MARTIAN_DRAW_FIRE 0x1e
// Passes a picture lasts, less one.
#define MARTIAN_PICTURE_PASSES 4
#define MARTIAN_MIRROR 0x0002
// A pass in four is a rest, by the game's own count of ticks.
#define MARTIAN_REST_MASK 0x0003

// Directions, doubled as the thread keeps them.
#define WAY_UP_RIGHT 0x04
#define WAY_RIGHT 0x06
#define WAY_DOWN_RIGHT 0x08
#define WAY_DOWN 0x0a
#define WAY_DOWN_LEFT 0x0c
#define WAY_LEFT 0x0e
#define WAY_UP_LEFT 0x10
// From here round to up and left it faces left, and is drawn mirrored.
#define WAY_FIRST_LEFT WAY_DOWN
#define WAY_MAX WAY_UP_LEFT
// The thread's setup does not clear its direction, so a martian that has not
// yet chosen one has whatever was on its page before. The ROM reads its
// tables with that all the same, far off their ends, and so does this, for as
// long as what it reads is still in the cartridge's bank.
#define WAY_READABLE_MAX 0x1800

// `actor_bearing`'s, which are not doubled.
#define BEARING_FIRST_LEFTWARD 5

// `$81:9C62`: the step for each direction, across and down, at twice the
// doubled direction.
#define MARTIAN_STEPS 0x9c62u
// `$81:9C86`: for each bearing, the one opposite. A byte each.
#define MARTIAN_OPPOSITES 0x9c86u
// `$81:9CD7`: walking, the picture's number for each direction and each of
// the four in its cycle.
#define MARTIAN_WALK_PICTURES 0x9cd7u
#define MARTIAN_WALK_PICTURE_COUNT 0x000c
#define MARTIAN_WALK_PICTURE_OTHERWISE 0x0008
// `$81:9EA7`: arriving, the four it cycles through whichever way it goes.
#define MARTIAN_ARRIVE_PICTURES 0x9ea7u
// `$81:9D6F` and `$81:9DBC`: how far a player may be before it leaves, the
// operand of each state's `LDA #$00D0`. Read from the cartridge because a
// wider picture widens it.
#define MARTIAN_WALK_REACH 0x9d6fu
#define MARTIAN_ARRIVE_REACH 0x9dbcu

// One martian's frame, and the carry and overflow the ROM would leave.
typedef struct {
  Wram* w;
  const Rom* rom;
  uint16_t page;
  uint16_t record;
  MartianLog* log;  // never NULL here
  PortFlags flags;
} Martian;

static uint16_t field(const Martian* m, uint16_t at) {
  return wram_r16(m->w, (uint16_t)(m->page + at));
}

static void set_field(Martian* m, uint16_t at, uint16_t v) {
  wram_w16(m->w, (uint16_t)(m->page + at), v);
}

static uint16_t record_field(const Martian* m, uint16_t at) {
  return wram_r16(m->w, (uint16_t)(m->record + at));
}

static void set_record_field(Martian* m, uint16_t at, uint16_t v) {
  wram_w16(m->w, (uint16_t)(m->record + at), v);
}

static uint16_t table_word(const Martian* m, uint16_t table, uint16_t index) {
  return rom_word(m->rom, ((uint32_t)MARTIAN_BANK << 16) +
                              (uint16_t)(table + index));
}

static Point position(const Martian* m) {
  return (Point){field(m, MARTIAN_DP_X), field(m, MARTIAN_DP_Y)};
}

static bool is_negative(uint16_t v) { return (v & 0x8000u) != 0; }

static uint16_t magnitude(uint16_t v) {
  return is_negative(v) ? (uint16_t)(0u - v) : v;
}

// A timer that counts down through zero. True on the pass it runs out.
static bool ran_out(Martian* m, uint16_t timer) {
  const uint16_t left = (uint16_t)(field(m, timer) - 1);
  set_field(m, timer, left);
  return is_negative(left);
}

static void show(Martian* m, uint16_t number) {
  set_record_field(m, ACTOR_META,
                   table_word(m, field(m, MARTIAN_DP_PICTURES),
                              (uint16_t)(number << 1)));
}

// The next of the cycle's four pictures, when the last has had its passes.
static bool cycle_moved_on(Martian* m) {
  if (!ran_out(m, MARTIAN_DP_PICTURE_TIMER)) return false;
  set_field(m, MARTIAN_DP_PICTURE_TIMER, MARTIAN_PICTURE_PASSES);
  set_field(m, MARTIAN_DP_CYCLE, (uint16_t)((field(m, MARTIAN_DP_CYCLE) + 1) & 3));
  return true;
}

// ---------------------------------------------------------------------------
// What it asks the rest of the game
// ---------------------------------------------------------------------------

// Whoever `actor_nearest` knows, written down with how far they are. It
// leaves carry and overflow clear.
static uint16_t find_target(Martian* m, uint16_t* dist) {
  const Point me = position(m);
  const uint16_t target =
      actor_nearest_counted(m->w, me.x, me.y, dist, &m->log->nearest);
  flags_carry(&m->flags, false);
  flags_overflow(&m->flags, false);
  set_field(m, MARTIAN_DP_TARGET_DIST, *dist);
  set_field(m, MARTIAN_DP_TARGET, target);
  return target;
}

// Which way `target` is, 1 to 8, or 0 on the same spot. A record within a
// pixel of the target's row or column is first put exactly on it, so that a
// martian closing in does not step past the line it was after.
//
// Overflow is clear when the two were apart, from the sums that made the
// answer. On the same spot it is still the snap's last difference's.
static uint16_t bearing_to(Martian* m, uint16_t target) {
  flags_sub(&m->flags, record_field(m, ACTOR_Y),
            wram_r16(m->w, (uint16_t)(target + ACTOR_Y)));
  actor_snap_to(m->w, m->record, target, &m->log->snap);
  ActorBearingRegs* r = &m->log->bearing;
  actor_bearing(m->w, m->rom, m->record, target, r);
  flags_carry(&m->flags, r->c);
  if (r->a != 0) flags_overflow(&m->flags, false);
  return r->a;
}

static uint16_t opposite(const Martian* m, uint16_t bearing) {
  return table_word(m, MARTIAN_OPPOSITES, bearing) & 0x00ff;
}

// Is either player within `reach`? `player_bearing` leaves an overflow the
// port does not follow.
static bool a_player_is_near(Martian* m, uint16_t reach_at) {
  const Point me = position(m);
  PlayerPickRegs* r = &m->log->players;
  player_bearing(m->w, m->rom, table_word(m, reach_at, 0), me.x, me.y, r);
  flags_carry(&m->flags, r->c);
  flags_overflow_unknown(&m->flags);
  return r->a != 0;
}

// ---------------------------------------------------------------------------
// Shooting
// ---------------------------------------------------------------------------

// `$81:9981`: fire along `way`, if it has not fired lately. The shot itself
// is the ROM's.
static void shoot(Martian* m, uint16_t way) {
  const Point me = position(m);
  set_field(m, MARTIAN_DP_SHOT_WAY, way);
  set_field(m, MARTIAN_DP_SHOT_X, me.x);
  set_field(m, MARTIAN_DP_SHOT_Y, me.y);
  set_field(m, MARTIAN_DP_SHOT_SLOT, 0xffff);
  const uint16_t cooldown = field(m, MARTIAN_DP_COOLDOWN);
  if (cooldown == 0) {
    PORT_COVER(martian_fired);
    m->log->declined = true;
    return;
  }
  set_field(m, MARTIAN_DP_COOLDOWN, (uint16_t)(cooldown - 1));
}

// Which way something is lined up with it, doubled, or 0 for nothing.
static uint16_t lined_up_way(Martian* m) {
  const Point me = position(m);
  ActorAlignedRegs* r = &m->log->aligned;
  m->log->aligned_asked = true;
  actor_aligned(m->w, me.x, me.y, r);
  flags_carry(&m->flags, r->c);
  flags_overflow(&m->flags, r->v);
  return r->a;
}

// ---------------------------------------------------------------------------
// Stepping
// ---------------------------------------------------------------------------

// May it stand at `p`? The ground, the level's edges and whoever is there
// each have a say, in that order. Carry is the answer and overflow the last
// test's that wrote one.
static bool can_stand_at(Martian* m, Point p, MartianProbe* probe) {
  terrain_blocked_enemy(m->w, p.x, p.y, &probe->ground);
  flags_carry(&m->flags, probe->ground.blocked);
  flags_overflow(&m->flags, probe->ground.v);
  if (probe->ground.blocked) return false;

  BoundsRegs edge;
  terrain_out_of_bounds(m->w, p.x, p.y, &edge);
  probe->edge_asked = true;
  probe->edge = edge.exit;
  probe->outside = edge.c;
  flags_carry(&m->flags, edge.c);
  if (edge.c) return false;

  AtPointRegs r;
  AtPointWork work;
  actor_at_point_counted(m->w, m->record, p.x, p.y, &r, &work);
  for (int i = 0; i < AT_POINT_BLOCK_COUNT; i++)
    m->log->at_point.blocks[i] += work.blocks[i];
  probe->someone_asked = true;
  probe->someone = r.found;
  flags_carry(&m->flags, r.found);
  if (r.v_set) flags_overflow(&m->flags, r.v);
  return !r.found;
}

// `$81:9BF3`: a step along `way`, each axis taken by itself if it may be.
// The record follows the thread at once.
static void step(Martian* m, uint16_t way, MartianStep* log) {
  // Doubling the direction is the last thing to write carry on a rest.
  flags_double(&m->flags, way);
  if ((wram_r16(m->w, W_SCHED_TICK) & MARTIAN_REST_MASK) == 0) {
    PORT_COVER(martian_rested);
    log->rested = true;
    return;
  }

  Point me = position(m);
  const uint16_t at = (uint16_t)(way << 1);
  const Point to = {(uint16_t)(me.x + table_word(m, MARTIAN_STEPS, at)),
                    (uint16_t)(me.y + table_word(m, MARTIAN_STEPS + 2, at))};
  set_field(m, MARTIAN_DP_TRY_X, to.x);
  set_field(m, MARTIAN_DP_TRY_Y, to.y);

  if (can_stand_at(m, (Point){to.x, me.y}, &log->axis[0])) {
    me.x = to.x;
    set_field(m, MARTIAN_DP_X, me.x);
  }
  if (can_stand_at(m, (Point){me.x, to.y}, &log->axis[1])) {
    me.y = to.y;
    set_field(m, MARTIAN_DP_Y, me.y);
  } else {
    PORT_COVER(martian_step_refused);
  }
  set_record_field(m, ACTOR_X, me.x);
  set_record_field(m, ACTOR_Y, me.y);
}

// ---------------------------------------------------------------------------
// Walking
// ---------------------------------------------------------------------------

// `$81:9BC0`: the way to go to have the target in its row or its column,
// which is along whichever axis the gap is smaller on. 0 when that gap is
// already nothing.
static uint16_t way_to_line_up(Martian* m) {
  MartianLog* log = m->log;
  const uint16_t gap_x = field(m, MARTIAN_DP_GAP_X);
  const uint16_t gap_y = field(m, MARTIAN_DP_GAP_Y);
  log->gap_x_negative = is_negative(gap_x);
  log->gap_y_negative = is_negative(gap_y);
  set_field(m, MARTIAN_DP_GAP_WIDTH, magnitude(gap_x));
  log->along_y = !flags_at_least(&m->flags, magnitude(gap_y), magnitude(gap_x));

  const uint16_t lesser = log->along_y ? gap_y : gap_x;
  log->lesser_sign = lesser == 0 ? 0 : is_negative(lesser) ? -1 : 1;
  if (lesser == 0) return 0;
  if (log->along_y)
    return is_negative(lesser) ? BEARING_UP : BEARING_DOWN;
  return is_negative(lesser) ? BEARING_LEFT : BEARING_RIGHT;
}

// `$81:9D42`: look for whoever is nearest and pick a way to go by how far
// they are. False when they are too far for it to move at all.
static bool look(Martian* m) {
  MartianLog* log = m->log;
  set_field(m, MARTIAN_DP_LOOK_TIMER, MARTIAN_LOOK_PASSES);
  uint16_t dist = 0;
  const uint16_t target = find_target(m, &dist);
  // The gaps are `actor_nearest`'s own scratch, and so those of the last
  // actor it measured, which need not be the nearest.
  set_field(m, MARTIAN_DP_GAP_X, wram_r16(m->w, NEAREST_DP_DX));
  set_field(m, MARTIAN_DP_GAP_Y, wram_r16(m->w, NEAREST_DP_DY));

  uint16_t bearing;
  if (!flags_at_least(&m->flags, dist, MARTIAN_TOO_CLOSE)) {
    PORT_COVER(martian_backed_off);
    log->look = MARTIAN_LOOK_BACK_OFF;
    bearing = opposite(m, bearing_to(m, target));
  } else if (!flags_at_least(&m->flags, dist, MARTIAN_LINE_UP_WITHIN)) {
    PORT_COVER(martian_lined_up);
    log->look = MARTIAN_LOOK_LINE_UP;
    bearing = way_to_line_up(m);
  } else if (!flags_at_least(&m->flags, dist, MARTIAN_APPROACH_WITHIN)) {
    PORT_COVER(martian_approached);
    log->look = MARTIAN_LOOK_APPROACH;
    bearing = bearing_to(m, target);
  } else {
    log->look = MARTIAN_LOOK_STAND;
    if (!a_player_is_near(m, MARTIAN_WALK_REACH)) {
      PORT_COVER(martian_left);
      log->look = MARTIAN_LOOK_LEAVE;
      set_field(m, MARTIAN_DP_FATE, (uint16_t)(field(m, MARTIAN_DP_FATE) - 1));
    }
    return false;
  }
  set_field(m, MARTIAN_DP_DIRECTION, flags_double(&m->flags, bearing));
  return true;
}

// `$81:9C99`: the picture for the way it faces, mirrored when that is to the
// left, and the cycle moved on when its time is up.
static void show_walking(Martian* m) {
  MartianLog* log = m->log;
  const uint16_t way = field(m, MARTIAN_DP_DIRECTION);
  const uint16_t at = (uint16_t)(((way << 1) | field(m, MARTIAN_DP_CYCLE)) << 1);
  uint16_t number = table_word(m, MARTIAN_WALK_PICTURES, at);
  if (number >= MARTIAN_WALK_PICTURE_COUNT) {
    log->capped = true;
    number = MARTIAN_WALK_PICTURE_OTHERWISE;
  }
  show(m, number);

  const uint16_t flags = record_field(m, ACTOR_FLAGS);
  log->mirrored = flags_at_least(&m->flags, way, WAY_FIRST_LEFT);
  set_record_field(m, ACTOR_FLAGS,
                   log->mirrored ? (uint16_t)(flags | MARTIAN_MIRROR)
                                 : (uint16_t)(flags & ~MARTIAN_MIRROR));
  log->new_picture = cycle_moved_on(m);
}

// `$81:9D30`: shoot at anything lined up, look now and then, and go the way
// the last look chose.
static void walk(Martian* m) {
  PORT_COVER(martian_walked);
  const uint16_t way = lined_up_way(m);
  if (way != 0) {
    PORT_COVER(martian_held_fire);
    m->log->held_fire = true;
    shoot(m, way);
    if (m->log->declined) return;
  }
  if (ran_out(m, MARTIAN_DP_LOOK_TIMER) && !look(m)) return;

  m->log->steps = 1;
  step(m, field(m, MARTIAN_DP_DIRECTION), &m->log->step[0]);
  show_walking(m);
}

// ---------------------------------------------------------------------------
// Arriving
// ---------------------------------------------------------------------------

// `$81:9EAF`: is either player higher up the level than it is?
static bool a_player_is_above(Martian* m) {
  static const uint16_t PLAYERS[2] = {W_PLAYER_A_RECORD, W_PLAYER_B_RECORD};
  const uint16_t my_y = field(m, MARTIAN_DP_Y);
  for (int i = 0; i < 2; i++) {
    const uint16_t player = wram_r16(m->w, PLAYERS[i]);
    if (player == 0) continue;
    m->log->player[i] = true;
    if (wram_r16(m->w, (uint16_t)(player + ACTOR_Y)) < my_y) {
      m->log->player_above[i] = true;
      flags_carry(&m->flags, true);
      return true;
    }
  }
  flags_carry(&m->flags, false);
  return false;
}

// `$81:9E8D`: its four pictures in turn, whichever way it goes.
static void show_arriving(Martian* m, MartianStep* log) {
  if (!cycle_moved_on(m)) return;
  log->new_picture = true;
  const uint16_t at = flags_double(&m->flags, field(m, MARTIAN_DP_CYCLE));
  show(m, table_word(m, MARTIAN_ARRIVE_PICTURES, at));
}

// `$81:9DF3`: the side of its target to go to. Towards them, or away when
// they are close.
static void pick_side(Martian* m) {
  MartianLog* log = m->log;
  log->picked_side = true;
  set_field(m, MARTIAN_DP_SIDE_TIMER, MARTIAN_SIDE_PASSES);
  uint16_t dist = 0;
  const uint16_t target = find_target(m, &dist);
  uint16_t bearing = bearing_to(m, target);
  if (dist < MARTIAN_SIDE_CLOSE) {
    log->target_close = true;
    bearing = opposite(m, bearing);
  }
  log->side_left = bearing >= BEARING_FIRST_LEFTWARD;
  set_field(m, MARTIAN_DP_SIDE, log->side_left ? WAY_LEFT : WAY_RIGHT);
}

// `$81:9DBB`: across the top, above its target, firing down now and then,
// until a player is above it.
static void arrive(Martian* m) {
  MartianLog* log = m->log;
  PORT_COVER(martian_arrived);
  if (!a_player_is_near(m, MARTIAN_ARRIVE_REACH)) {
    log->nobody = true;
    set_field(m, MARTIAN_DP_FATE, (uint16_t)(field(m, MARTIAN_DP_FATE) - 1));
  }
  if (a_player_is_above(m)) {
    PORT_COVER(martian_came_down);
    log->came_down = true;
    set_field(m, MARTIAN_DP_CYCLE, 0);
    set_field(m, MARTIAN_DP_PICTURE_TIMER, 0);
    set_field(m, MARTIAN_DP_STATE, MARTIAN_STATE_WALK);
    return;
  }

  const uint16_t way = lined_up_way(m);
  if (way != 0) {
    log->held_fire = true;
    shoot(m, way);
  } else {
    RngResult draw;
    rng_next(m->w, m->flags.c, &draw);
    log->drew = true;
    log->drew_overflow = draw.v;
    flags_overflow(&m->flags, draw.v);
    if (draw.a < MARTIAN_DRAW_FIRE) {
      log->drew_fire = true;
      shoot(m, WAY_DOWN);
    }
  }
  if (log->declined) return;

  if (ran_out(m, MARTIAN_DP_SIDE_TIMER)) pick_side(m);
  const uint16_t side = field(m, MARTIAN_DP_SIDE);
  const bool left = side >= WAY_FIRST_LEFT;
  uint16_t way_on = side;

  const uint16_t target = field(m, MARTIAN_DP_TARGET);
  const uint16_t below = flags_sub(&m->flags,
                                   wram_r16(m->w, (uint16_t)(target + ACTOR_Y)),
                                   field(m, MARTIAN_DP_Y));
  log->height_negative = is_negative(below);
  const uint16_t height = magnitude(below);
  log->steps = 2;
  if (height < MARTIAN_ABOVE_LEAST) {
    PORT_COVER(martian_climbed);
    log->height = MARTIAN_HEIGHT_CLIMB;
    way_on = left ? WAY_UP_LEFT : WAY_UP_RIGHT;
  } else if (height >= MARTIAN_ABOVE_MOST) {
    PORT_COVER(martian_dropped);
    log->height = MARTIAN_HEIGHT_DROP;
    way_on = left ? WAY_DOWN_LEFT : WAY_DOWN_RIGHT;
  } else {
    log->height = MARTIAN_HEIGHT_KEEP;
    log->steps = 1;
  }
  log->slant_left = left;
  set_field(m, MARTIAN_DP_DIRECTION, way_on);

  for (int i = 0; i < log->steps; i++) {
    step(m, way_on, &log->step[i]);
    show_arriving(m, &log->step[i]);
  }
}

// ---------------------------------------------------------------------------
// The frame
// ---------------------------------------------------------------------------

bool martian_frame_supported(const Wram* w, uint16_t page) {
  const uint16_t state = wram_r16(w, (uint16_t)(page + MARTIAN_DP_STATE));
  const uint16_t way = wram_r16(w, (uint16_t)(page + MARTIAN_DP_DIRECTION));
  const uint16_t side = wram_r16(w, (uint16_t)(page + MARTIAN_DP_SIDE));
  if (state != MARTIAN_STATE_WALK && state != MARTIAN_STATE_ARRIVE) return false;
  if (wram_r16(w, (uint16_t)(page + MARTIAN_DP_SHOOT)) != MARTIAN_SHOOT)
    return false;
  if (way > WAY_READABLE_MAX) return false;
  if (state == MARTIAN_STATE_ARRIVE && side > WAY_MAX) return false;
  return wram_r16(w, (uint16_t)(page + MARTIAN_DP_CYCLE)) <= 3;
}

bool martian_frame(Wram* w, const Rom* rom, uint16_t page, MartianLog* log) {
  MartianLog scratch = {0};
  Martian m = {w, rom, page, wram_r16(w, (uint16_t)(page + MARTIAN_DP_RECORD)),
               log ? log : &scratch, {false, false, false, false}};
  m.log->state = field(&m, MARTIAN_DP_STATE);
  if (m.log->state == MARTIAN_STATE_WALK)
    walk(&m);
  else
    arrive(&m);

  m.log->c = m.flags.c;
  m.log->v = m.flags.v;
  m.log->c_set = m.flags.c_set;
  m.log->v_set = m.flags.v_set;
  return field(&m, MARTIAN_DP_FATE) == 0;
}
