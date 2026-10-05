// The fishman's state bodies -- see port/fishman.h.

#include "port/fishman.h"

#include <stddef.h>

#include "port/coverage.h"
#include "port/flags.h"
#include "port/rng.h"

typedef struct {
  uint16_t x, y;
} Point;

// How near whoever is nearest has to be for each thing it does.
#define FISHMAN_CLOSE_IN_WITHIN 0x0080
#define FISHMAN_NOTICE_WITHIN 0x00d0
#define FISHMAN_BITE_WITHIN 0x0018
#define FISHMAN_KEEP_CLOSING_WITHIN 0x00af
#define FISHMAN_LEAP_WITHIN 0x00c8
// ...and how near a player, for it to stay on the level at all.
#define FISHMAN_PLAYERS_WITHIN 0x00d0

// It stops to look about on a draw under the first, and leaps on one under
// the second.
#define FISHMAN_LOOK_ABOUT_ODDS 0x05
#define FISHMAN_LEAP_ODDS 0x7d

// Where a leap comes down, from whoever it is at: one of two places across,
// and up to 62 further on; and from 16 above to 14 below.
#define FISHMAN_LEAP_SIDE_BIT 0x0002
#define FISHMAN_LEAP_ACROSS_MASK 0x001f
#define FISHMAN_LEAP_DOWN_MASK 0x000f
#define FISHMAN_LEAP_DOWN_MIDDLE 0x0008

// A quarter turn and a half turn, in doubled bearings.
#define FISHMAN_QUARTER_TURN 0x0004
#define FISHMAN_HALF_TURN 0x0008
#define FISHMAN_WAYS_MASK 0x000f
#define FISHMAN_EVEN_WAYS_MASK 0x000e
#define FISHMAN_FIRST_WAY 0x0002
#define FISHMAN_LAST_WAY 0x0010

// Patrolling: how near its row or column somebody has to be for it to line
// up with them, and how near it then gets.
#define FISHMAN_LINE_UP_WITHIN 0x0018
#define FISHMAN_LINED_UP_WITHIN 0x0010
// It lines up twice a pass on frames without this bit.
#define FISHMAN_LINE_ONCE_BIT 0x0002

// The tiles under it. Water has the first bit. Somewhere to land has the
// second and not the third, and that test takes nine off both ways.
#define FISHMAN_WATER 0x0100
#define FISHMAN_LANDING 0x0080
#define FISHMAN_NO_LANDING 0x0002
#define FISHMAN_LANDING_ORIGIN_Y 0x0009

// How long it sleeps: swimming, and lined up or in the air.
#define FISHMAN_TICKS_SLOW 2
#define FISHMAN_TICKS_FAST 1

// Its pictures. A picture lasts five passes. Swimming there are seven in the
// cycle and standing four, and those for the ways from here on are mirrored.
#define FISHMAN_PICTURE_PASSES 4
#define FISHMAN_SWIM_PICTURES 7
#define FISHMAN_STAND_PICTURES 4
#define FISHMAN_FIRST_MIRRORED 0x0030
#define FISHMAN_MIRROR 0x0002
// On land it is something to hit, and is not drawn over everything.
#define FISHMAN_LANDED_ID 0x0003

// Tables in `FISHMAN_BANK`.
#define FISHMAN_STEPS 0xd837u  // by way, doubled: a step across and down
#define FISHMAN_LEAP_SIDES 0xe078u      // by the draw's bit: a place across
#define FISHMAN_STAND_PICTURE_AT 0xe68eu
#define FISHMAN_SWIM_PICTURE_AT 0xe6d6u

// The game's count of frames.
#define W_FRAMES 0x0020u

// One fishman's pass.
typedef struct {
  Wram* w;
  const Rom* rom;
  uint16_t page;
  uint16_t record;
  FishmanLog* log;  // never NULL here
  PortFlags flags;
} Fishman;

static uint16_t field(const Fishman* s, uint16_t at) {
  return wram_r16(s->w, (uint16_t)(s->page + at));
}

static void set_field(Fishman* s, uint16_t at, uint16_t v) {
  wram_w16(s->w, (uint16_t)(s->page + at), v);
}

static uint16_t record_field(const Fishman* s, uint16_t at) {
  return wram_r16(s->w, (uint16_t)(s->record + at));
}

static void set_record_field(Fishman* s, uint16_t at, uint16_t v) {
  wram_w16(s->w, (uint16_t)(s->record + at), v);
}

static uint16_t table_word(const Fishman* s, uint16_t table, uint16_t index) {
  return rom_word(s->rom, ((uint32_t)FISHMAN_BANK << 16) +
                              (uint16_t)(table + index));
}

static Point position(const Fishman* s) {
  return (Point){field(s, FISHMAN_DP_X), field(s, FISHMAN_DP_Y)};
}

static Point place_of(const Fishman* s, uint16_t record) {
  return (Point){wram_r16(s->w, (uint16_t)(record + ACTOR_X)),
                 wram_r16(s->w, (uint16_t)(record + ACTOR_Y))};
}

static void set_state(Fishman* s, uint16_t body) {
  set_field(s, FISHMAN_DP_STATE, body);
}

static bool negative(uint16_t v) { return (v & 0x8000u) != 0; }

static uint16_t magnitude(uint16_t v) {
  return negative(v) ? (uint16_t)(0u - v) : v;
}

static void leave(Fishman* s) {
  set_field(s, FISHMAN_DP_FATE, (uint16_t)(field(s, FISHMAN_DP_FATE) - 1));
}

// ---------------------------------------------------------------------------
// What it asks the rest of the game
// ---------------------------------------------------------------------------

// Whoever `actor_nearest` knows, and how far. It leaves carry and overflow
// clear.
static uint16_t nearest_actor(Fishman* s, uint16_t* dist) {
  const Point me = position(s);
  ActorNearestWork work;
  const uint16_t found = actor_nearest_counted(s->w, me.x, me.y, dist, &work);
  for (int i = 0; i < NEAREST_BLOCK_COUNT; i++)
    s->log->nearest.blocks[i] += work.blocks[i];
  flags_carry(&s->flags, false);
  flags_overflow(&s->flags, false);
  return found;
}

// Which way `target` is, 1 to 8, or 0 on the same spot. Its record is first
// put exactly on the target's row or column if it is within a pixel of it.
//
// Overflow is clear when the two were apart, from the sums that made the
// answer. On the same spot it is still the snap's last difference's.
static uint16_t bearing_to(Fishman* s, uint16_t target) {
  FishmanLog* log = s->log;
  ActorSnapRegs snap_scratch;
  ActorBearingRegs bearing_scratch;
  const bool kept = log->bearings < FISHMAN_MAX_SLIDES;
  ActorSnapRegs* snap = kept ? &log->snap[log->bearings] : &snap_scratch;
  ActorBearingRegs* r = kept ? &log->bearing[log->bearings] : &bearing_scratch;
  log->bearings++;

  flags_sub(&s->flags, record_field(s, ACTOR_Y), place_of(s, target).y);
  actor_snap_to(s->w, s->record, target, snap);
  actor_bearing(s->w, s->rom, s->record, target, r);
  flags_carry(&s->flags, r->c);
  if (r->a != 0) flags_overflow(&s->flags, false);
  return r->a;
}

// The overflow `player_bearing` leaves. With a player found off its row or
// its column it is clear, from the sums that made the answer. Otherwise it
// is that of the last gap measured: down, to the second player if there is
// one, or else to the first.
static void players_overflow(Fishman* s, Point from, const PlayerPickRegs* r) {
  static const uint16_t LAST_FIRST[2] = {W_PLAYER_B_RECORD, W_PLAYER_A_RECORD};
  if (r->a != 0 && !(r->same_x && r->same_y)) {
    flags_overflow(&s->flags, false);
    return;
  }
  for (int i = 0; i < 2; i++) {
    const uint16_t player = wram_r16(s->w, LAST_FIRST[i]);
    if (player == 0) continue;
    PortFlags gap = {0};
    flags_sub(&gap, place_of(s, player).y, from.y);
    flags_overflow(&s->flags, gap.v);
    return;
  }
}

// Is neither player near enough for it to stay?
static bool nobody_about(Fishman* s) {
  FishmanLog* log = s->log;
  PlayerPickRegs scratch;
  PlayerPickRegs* r = log->player_asks < FISHMAN_MAX_PLAYER_ASKS
                          ? &log->players[log->player_asks]
                          : &scratch;
  log->player_asks++;
  const Point me = position(s);
  player_bearing(s->w, s->rom, FISHMAN_PLAYERS_WITHIN, me.x, me.y, r);
  flags_carry(&s->flags, r->c);
  players_overflow(s, me, r);
  return r->a == 0;
}

static void leave_if_alone(Fishman* s) {
  if (!nobody_about(s)) return;
  PORT_COVER(fishman_left);
  leave(s);
}

// A draw begins from the carry before it.
static uint16_t random_byte(Fishman* s) {
  RngResult r;
  rng_next(s->w, s->flags.c, &r);
  flags_carry(&s->flags, r.c);
  flags_overflow(&s->flags, r.v);
  if (s->log->draws < FISHMAN_MAX_DRAWS)
    s->log->draw_overflow[s->log->draws] = r.v;
  s->log->draws++;
  return r.a;
}

// Does each of the six tiles under `p` have `want` of the bits in `mask`?
// Carry is set when one does not. Overflow is that of the sum that found the
// first tile, or the sixth when the test got that far.
static bool every_tile(Fishman* s, Point p, uint16_t origin_y, uint16_t mask,
                       uint16_t want, FishmanTiles* log) {
  TerrainFootprint under;
  terrain_footprint_read(s->w, p.x, p.y, TERRAIN_ORIGIN_X, origin_y, &under);
  log->all = true;
  log->tiles = TERRAIN_PROBE_COUNT;
  for (int i = 0; i < TERRAIN_PROBE_COUNT; i++) {
    if ((under.attrs[i] & mask) == want) continue;
    log->all = false;
    log->tiles = i + 1;
    break;
  }
  flags_carry(&s->flags, !log->all);
  flags_overflow(&s->flags, log->tiles == TERRAIN_PROBE_COUNT ? under.v_last_row
                                                              : under.v_map);
  return log->all;
}

// `$81:DC24`.
static bool all_water(Fishman* s, Point p, FishmanTiles* log) {
  return every_tile(s, p, TERRAIN_ORIGIN_Y, FISHMAN_WATER, FISHMAN_WATER, log);
}

// `$81:E1D6`.
static bool somewhere_to_land(Fishman* s, Point p, FishmanTiles* log) {
  return every_tile(s, p, FISHMAN_LANDING_ORIGIN_Y,
                    FISHMAN_LANDING | FISHMAN_NO_LANDING, FISHMAN_LANDING, log);
}

// Is somebody at `p`? Carry is the answer, and overflow the test's when it
// wrote one.
static bool someone_at(Fishman* s, Point p) {
  AtPointRegs r;
  AtPointWork work;
  actor_at_point_counted(s->w, s->record, p.x, p.y, &r, &work);
  for (int i = 0; i < AT_POINT_BLOCK_COUNT; i++)
    s->log->at_point.blocks[i] += work.blocks[i];
  flags_carry(&s->flags, r.found);
  if (r.v_set) flags_overflow(&s->flags, r.v);
  return r.found;
}

// `$81:DCB9`: may it swim at `p`? Water, and nobody there.
static bool can_swim_at(Fishman* s, Point p, FishmanProbe* probe) {
  if (!all_water(s, p, &probe->water)) return false;
  probe->someone_asked = true;
  probe->someone = someone_at(s, p);
  return !probe->someone;
}

// ---------------------------------------------------------------------------
// Stepping
// ---------------------------------------------------------------------------

// Write down a step along `way` as the one to try.
static Point try_way(Fishman* s, uint16_t way) {
  const uint16_t at = (uint16_t)(way << 1);
  const Point me = position(s);
  Point to;
  to.x = flags_add(&s->flags, table_word(s, FISHMAN_STEPS, at), me.x);
  set_field(s, FISHMAN_DP_TRY_X, to.x);
  to.y = flags_add(&s->flags, table_word(s, FISHMAN_STEPS + 2, at), me.y);
  set_field(s, FISHMAN_DP_TRY_Y, to.y);
  return to;
}

// `$81:DD0B`: take the step being tried, an axis at a time, the second from
// wherever the first left it. An axis the step does not change is not asked
// after. True when neither was taken.
static bool slide(Fishman* s) {
  FishmanLog* log = s->log;
  FishmanSlide scratch = {0};
  FishmanSlide* k =
      log->slides < FISHMAN_MAX_SLIDES ? &log->slide[log->slides] : &scratch;
  log->slides++;

  Point me = position(s);
  const Point to = {field(s, FISHMAN_DP_TRY_X), field(s, FISHMAN_DP_TRY_Y)};
  int taken = 0;
  if (!flags_same(&s->flags, to.x, me.x)) {
    k->asked[0] = true;
    if (can_swim_at(s, (Point){to.x, me.y}, &k->axis[0])) {
      taken++;
      me.x = to.x;
      set_field(s, FISHMAN_DP_X, me.x);
    }
  }
  if (!flags_same(&s->flags, to.y, me.y)) {
    k->asked[1] = true;
    if (can_swim_at(s, (Point){me.x, to.y}, &k->axis[1])) {
      taken++;
      me.y = to.y;
      set_field(s, FISHMAN_DP_Y, me.y);
    }
  }
  set_field(s, FISHMAN_DP_AXES_TAKEN, (uint16_t)taken);
  k->stuck = taken == 0;
  flags_carry(&s->flags, k->stuck);
  return k->stuck;
}

// A way is 2 to 16. `turned` is one with 2 taken off and a turn put on, and
// this brings it back round.
static uint16_t way_of(uint16_t turned) {
  return (uint16_t)((turned & FISHMAN_WAYS_MASK) + FISHMAN_FIRST_WAY);
}

// `$81:DD7F`: a quarter turn, and from the next pass it looks for openings.
static void turn_at_wall(Fishman* s) {
  PORT_COVER(fishman_turned_at_wall);
  const uint16_t way = (uint16_t)(field(s, FISHMAN_DP_WAY) - FISHMAN_FIRST_WAY);
  set_field(s, FISHMAN_DP_WAY,
            way_of(flags_add(&s->flags, way, FISHMAN_QUARTER_TURN)));
  set_state(s, FISHMAN_STATE_SWIM_TURNED);
}

// `$81:DCD0`: a quarter turn the other way, if it can swim that way.
static void turn_if_clear(Fishman* s) {
  FishmanLog* log = s->log;
  log->opening_asked = true;
  const uint16_t way = (uint16_t)(field(s, FISHMAN_DP_WAY) - FISHMAN_FIRST_WAY);
  const uint16_t other =
      way_of(flags_sub(&s->flags, way, FISHMAN_QUARTER_TURN));
  set_field(s, FISHMAN_DP_TRY_WAY, other);
  if (can_swim_at(s, try_way(s, other), &log->opening)) {
    PORT_COVER(fishman_turned_at_opening);
    set_field(s, FISHMAN_DP_WAY, other);
  }
}

// A step the way it faces, or a turn if it cannot. True when it moved.
static bool swim_on(Fishman* s) {
  FishmanLog* log = s->log;
  const Point to = try_way(s, field(s, FISHMAN_DP_WAY));
  log->ahead_asked = true;
  if (!can_swim_at(s, to, &log->ahead)) {
    turn_at_wall(s);
    return false;
  }
  set_field(s, FISHMAN_DP_X, to.x);
  set_field(s, FISHMAN_DP_Y, to.y);
  return true;
}

// ---------------------------------------------------------------------------
// What it may do on any pass in the water
// ---------------------------------------------------------------------------

// `$81:DD57`: close in on somebody near, and leave if nobody is.
static void look(Fishman* s) {
  FishmanLog* log = s->log;
  uint16_t dist;
  const uint16_t target = nearest_actor(s, &dist);
  if (!flags_at_least(&s->flags, dist, FISHMAN_CLOSE_IN_WITHIN)) {
    PORT_COVER(fishman_closed_in);
    log->look = FISHMAN_LOOK_NEAR;
    set_field(s, FISHMAN_DP_TARGET, target);
    set_state(s, FISHMAN_STATE_CLOSE_IN);
    return;
  }
  if (!flags_at_least(&s->flags, dist, FISHMAN_NOTICE_WITHIN)) {
    log->look = FISHMAN_LOOK_WITHIN;
    return;
  }
  log->look = FISHMAN_LOOK_FAR;
  leave_if_alone(s);
}

// `$81:DDA1`: stop and look about, on a draw. That sleeps, so it is the
// ROM's.
static void maybe_look_about(Fishman* s) {
  s->log->look_about_asked = true;
  if (flags_at_least(&s->flags, random_byte(s), FISHMAN_LOOK_ABOUT_ODDS))
    return;
  PORT_COVER(fishman_looked_about);
  s->log->declined = true;
}

// `$81:E007`: leap at somebody, on a draw, if there is somewhere near them
// to come down. The leap is the ROM's.
static void maybe_leap(Fishman* s) {
  FishmanLog* log = s->log;
  if (flags_at_least(&s->flags, random_byte(s), FISHMAN_LEAP_ODDS)) {
    log->leap = FISHMAN_LEAP_NO_DRAW;
    return;
  }
  uint16_t dist;
  const uint16_t target = nearest_actor(s, &dist);
  if (flags_at_least(&s->flags, dist, FISHMAN_LEAP_WITHIN)) {
    log->leap = FISHMAN_LEAP_NOBODY;
    return;
  }
  set_field(s, FISHMAN_DP_TARGET, target);
  const Point them = place_of(s, target);

  const uint16_t side = random_byte(s) & FISHMAN_LEAP_SIDE_BIT;
  const uint16_t across =
      (uint16_t)((random_byte(s) & FISHMAN_LEAP_ACROSS_MASK) << 1);
  Point spot;
  spot.x = flags_add(&s->flags, flags_add(&s->flags, across, them.x),
                     table_word(s, FISHMAN_LEAP_SIDES, side));
  set_field(s, FISHMAN_DP_LAND_X, spot.x);
  const uint16_t down = flags_double(
      &s->flags, flags_sub(&s->flags, random_byte(s) & FISHMAN_LEAP_DOWN_MASK,
                           FISHMAN_LEAP_DOWN_MIDDLE));
  spot.y = flags_add(&s->flags, down, them.y);
  set_field(s, FISHMAN_DP_LAND_Y, spot.y);

  BoundsRegs edge;
  terrain_out_of_bounds(s->w, spot.x, spot.y, &edge);
  flags_carry(&s->flags, edge.c);
  log->leap_edge = edge.exit;
  if (edge.c) {
    PORT_COVER(fishman_leap_off_level);
    log->leap = FISHMAN_LEAP_OFF_LEVEL;
    return;
  }
  if (!somewhere_to_land(s, spot, &log->landing)) {
    log->leap = FISHMAN_LEAP_NO_LANDING;
    return;
  }
  if (someone_at(s, spot)) {
    PORT_COVER(fishman_leap_taken);
    log->leap = FISHMAN_LEAP_TAKEN;
    return;
  }
  PORT_COVER(fishman_leapt);
  log->leap = FISHMAN_LEAP_LEAPS;
  log->declined = true;
}

// ---------------------------------------------------------------------------
// The state bodies
// ---------------------------------------------------------------------------

// `$81:DDFD`: the way it faces, until something stops it.
static void swim(Fishman* s) {
  look(s);
  maybe_look_about(s);
  if (s->log->declined) return;
  maybe_leap(s);
  if (s->log->declined) return;
  swim_on(s);
}

// `$81:DE36`: the same, turning at every opening.
static void swim_turned(Fishman* s) {
  maybe_look_about(s);
  if (s->log->declined) return;
  maybe_leap(s);
  if (s->log->declined) return;
  turn_if_clear(s);
  if (swim_on(s)) look(s);
}

// One step at `target`.
static void close_step(Fishman* s, uint16_t target) {
  const uint16_t way = (uint16_t)(bearing_to(s, target) << 1);
  set_field(s, FISHMAN_DP_WAY, way);
  flags_carry(&s->flags, false);  // the `ASL`
  try_way(s, way);
  slide(s);
}

// `$81:DE72`: at whoever is nearest, until it can bite them.
static void close_in(Fishman* s) {
  FishmanLog* log = s->log;
  maybe_leap(s);
  if (log->declined) return;

  uint16_t dist;
  const uint16_t target = nearest_actor(s, &dist);
  if (!flags_at_least(&s->flags, dist, FISHMAN_BITE_WITHIN)) {
    PORT_COVER(fishman_bit);
    log->range = FISHMAN_RANGE_TOUCHING;
    set_field(s, FISHMAN_DP_TARGET, target);
    set_state(s, FISHMAN_STATE_BITE);
    return;
  }
  if (flags_at_least(&s->flags, dist, FISHMAN_KEEP_CLOSING_WITHIN)) {
    PORT_COVER(fishman_lost_them);
    log->range = FISHMAN_RANGE_FAR;
    if (nobody_about(s))
      leave(s);
    else
      set_state(s, FISHMAN_STATE_SWIM);
    return;
  }
  log->range = FISHMAN_RANGE_NEAR;
  set_field(s, FISHMAN_DP_TARGET, target);
  log->close_steps = (random_byte(s) & 3) != 0 ? 2 : 1;
  for (int i = 0; i < log->close_steps; i++) close_step(s, target);
}

// `$81:DED8`: back to its patrol.
static void patrol_again(Fishman* s) {
  set_state(s, FISHMAN_STATE_PATROL);
  leave_if_alone(s);
}

static void line_up_from_next_pass(Fishman* s, uint16_t body) {
  set_state(s, body);
  set_field(s, FISHMAN_DP_TICKS, FISHMAN_TICKS_FAST);
}

// `$81:DEFD`: along its line, turning about when stopped, until somebody is
// near its row or its column.
static void patrol(Fishman* s) {
  FishmanLog* log = s->log;
  try_way(s, field(s, FISHMAN_DP_WAY));
  if (slide(s)) {
    PORT_COVER(fishman_turned_about);
    log->reversed = true;
    uint16_t way = flags_add(&s->flags, field(s, FISHMAN_DP_WAY),
                             FISHMAN_HALF_TURN) &
                   FISHMAN_EVEN_WAYS_MASK;
    if (way == 0) {
      log->reversed_wrapped = true;
      way = FISHMAN_LAST_WAY;
    }
    set_field(s, FISHMAN_DP_WAY, way);
  }
  maybe_leap(s);
  if (log->declined) return;

  uint16_t dist;
  const uint16_t target = nearest_actor(s, &dist);
  if (!flags_at_least(&s->flags, dist, FISHMAN_BITE_WITHIN)) {
    PORT_COVER(fishman_patrol_bit);
    log->patrol = FISHMAN_PATROL_TOUCHING;
    set_state(s, FISHMAN_STATE_BITE);
    return;
  }
  set_field(s, FISHMAN_DP_TARGET, target);
  const Point me = position(s);
  const Point them = place_of(s, target);

  const uint16_t across = flags_sub(&s->flags, them.x, me.x);
  log->gap_negative[0] = negative(across);
  if (!flags_at_least(&s->flags, magnitude(across), FISHMAN_LINE_UP_WITHIN)) {
    PORT_COVER(fishman_in_column);
    log->patrol = FISHMAN_PATROL_IN_COLUMN;
    line_up_from_next_pass(s, FISHMAN_STATE_LINE_UP_DOWN);
    return;
  }
  const uint16_t down = flags_sub(&s->flags, them.y, me.y);
  log->gap_negative[1] = negative(down);
  if (!flags_at_least(&s->flags, magnitude(down), FISHMAN_LINE_UP_WITHIN)) {
    PORT_COVER(fishman_in_row);
    log->patrol = FISHMAN_PATROL_IN_ROW;
    line_up_from_next_pass(s, FISHMAN_STATE_LINE_UP_ACROSS);
    return;
  }
  log->patrol = FISHMAN_PATROL_APART;
  leave_if_alone(s);
}

// `$81:DF8F` and `$81:DFD6`: a pixel towards its target along one axis. Near
// enough, or stopped, and it patrols again.
static void line_step(Fishman* s, bool down, FishmanLineStep* log) {
  const Point me = position(s);
  const Point them = place_of(s, field(s, FISHMAN_DP_TARGET));
  uint16_t gap = down ? flags_sub(&s->flags, them.y, me.y)
                      : flags_sub(&s->flags, them.x, me.x);
  uint16_t step = 1;
  if (negative(gap)) {
    log->negative = true;
    gap = (uint16_t)(0u - gap);
    step = 0xffffu;
  }
  if (flags_at_least(&s->flags, gap, FISHMAN_LINED_UP_WITHIN)) {
    if (down) {
      set_field(s, FISHMAN_DP_TRY_Y, flags_add(&s->flags, step, me.y));
      set_field(s, FISHMAN_DP_TRY_X, me.x);
    } else {
      set_field(s, FISHMAN_DP_TRY_X, flags_add(&s->flags, step, me.x));
      set_field(s, FISHMAN_DP_TRY_Y, me.y);
    }
    if (!slide(s)) return;
    log->stuck = true;
  } else {
    PORT_COVER(fishman_lined_up);
    log->arrived = true;
  }
  set_field(s, FISHMAN_DP_TICKS, FISHMAN_TICKS_SLOW);
  patrol_again(s);
}

// `$81:DF84` and `$81:DFCB`: twice a pass, on two frames in four.
static void line_up(Fishman* s, bool down) {
  FishmanLog* log = s->log;
  log->line_steps =
      (wram_r16(s->w, W_FRAMES) & FISHMAN_LINE_ONCE_BIT) != 0 ? 1 : 2;
  for (int i = 0; i < log->line_steps; i++) line_step(s, down, &log->line[i]);
}

// One axis of a leap: so many pixels this pass, by how its gap compares with
// the longer one.
static uint16_t glide(Fishman* s, uint16_t place_at, uint16_t part_at,
                      uint16_t gap_at, uint16_t sign_at, int* steps) {
  const uint16_t span = field(s, FISHMAN_DP_SPAN);
  uint16_t place = field(s, place_at);
  uint16_t part = flags_add(&s->flags, field(s, part_at), field(s, gap_at));
  while (flags_at_least(&s->flags, part, span)) {
    place = flags_add(&s->flags, place, field(s, sign_at));
    set_field(s, place_at, place);
    part = flags_sub(&s->flags, part, span);
    ++*steps;
  }
  set_field(s, part_at, part);
  return place;
}

// `$81:E120` and `$81:DB8B`: a pass of a leap out, or of the dive back in.
static void fly(Fishman* s, uint16_t when_down) {
  FishmanLog* log = s->log;
  const uint16_t count = field(s, FISHMAN_DP_RISE);
  log->falling = negative(count);
  const uint16_t rise =
      (uint16_t)((count >> 2) | (negative(count) ? 0xc000u : 0u));
  set_record_field(s, ACTOR_Z,
                   flags_add(&s->flags, rise, record_field(s, ACTOR_Z)));
  set_field(s, FISHMAN_DP_RISE, (uint16_t)(count - 1));

  Point me;
  me.x = glide(s, FISHMAN_DP_X, FISHMAN_DP_PART_X, FISHMAN_DP_GAP_X,
               FISHMAN_DP_SIGN_X, &log->glide_steps[0]);
  me.y = glide(s, FISHMAN_DP_Y, FISHMAN_DP_PART_Y, FISHMAN_DP_GAP_Y,
               FISHMAN_DP_SIGN_Y, &log->glide_steps[1]);
  set_record_field(s, ACTOR_X, me.x);
  set_record_field(s, ACTOR_Y, me.y);

  const uint16_t height = record_field(s, ACTOR_Z);
  if (height != 0 && !negative(height)) {
    log->flight = FISHMAN_FLIGHT_UP;
    return;
  }
  PORT_COVER(fishman_came_down);
  log->flight = FISHMAN_FLIGHT_DOWN;
  if (negative(height)) {
    log->flight = FISHMAN_FLIGHT_UNDER;
    set_record_field(s, ACTOR_Z, 0);
  }
  set_state(s, when_down);
}

// `$81:E15B`: down. One that comes ashore sets off some way, by a draw.
static void land(Fishman* s) {
  set_field(s, FISHMAN_DP_TICKS, FISHMAN_TICKS_SLOW);
  set_field(s, FISHMAN_DP_FORM, 1);
  set_field(s, FISHMAN_DP_PICTURE, 0);
  set_field(s, FISHMAN_DP_PICTURE_WAIT, 0);
  const uint16_t flags = record_field(s, ACTOR_FLAGS);
  set_record_field(s, ACTOR_FLAGS, flags & (uint16_t)~ACTOR_PRIORITY_TOP);
  set_record_field(s, ACTOR_COLLIDE_ID, FISHMAN_LANDED_ID);

  if (field(s, FISHMAN_DP_STAYS) != 0) {
    PORT_COVER(fishman_landed_lurking);
    set_state(s, FISHMAN_STATE_LURK);
    return;
  }
  PORT_COVER(fishman_landed_ashore);
  s->log->ashore = true;
  set_field(s, FISHMAN_DP_WAY,
            (uint16_t)(((random_byte(s) & 3) << 2) + FISHMAN_FIRST_WAY));
  flags_carry(&s->flags, false);  // the two `ASL`s
  set_state(s, FISHMAN_STATE_ASHORE);
}

// ---------------------------------------------------------------------------
// The pass
// ---------------------------------------------------------------------------

// The next of its swimming pictures.
static void next_swim_picture(Fishman* s) {
  FishmanLog* log = s->log;
  const uint16_t picture = field(s, FISHMAN_DP_PICTURE);
  set_record_field(s, ACTOR_META,
                   table_word(s, FISHMAN_SWIM_PICTURE_AT,
                              (uint16_t)(picture << 1)));
  uint16_t next = (uint16_t)((picture + 1) & 7);
  if (flags_same(&s->flags, next, FISHMAN_SWIM_PICTURES)) {
    log->picture_wrapped = true;
    next = 0;
  }
  set_field(s, FISHMAN_DP_PICTURE, next);
}

// ...and of its standing ones, for the way it faces.
static void next_stand_picture(Fishman* s) {
  FishmanLog* log = s->log;
  const uint16_t picture = field(s, FISHMAN_DP_PICTURE);
  const uint16_t at =
      (uint16_t)(((field(s, FISHMAN_DP_WAY) << 1) | picture) << 1);
  set_record_field(s, ACTOR_META, table_word(s, FISHMAN_STAND_PICTURE_AT, at));
  const uint16_t flags = record_field(s, ACTOR_FLAGS);
  log->mirrored = flags_at_least(&s->flags, at, FISHMAN_FIRST_MIRRORED);
  set_record_field(s, ACTOR_FLAGS,
                   log->mirrored ? (uint16_t)(flags | FISHMAN_MIRROR)
                                 : (uint16_t)(flags & ~FISHMAN_MIRROR));
  set_field(s, FISHMAN_DP_PICTURE,
            (uint16_t)((picture + 1) & (FISHMAN_STAND_PICTURES - 1)));
}

// `$81:E610`: leave, with neither player near. Otherwise its next picture
// when this one has had its passes, and its record put where it now is.
static void show(Fishman* s) {
  FishmanLog* log = s->log;
  if (nobody_about(s)) {
    PORT_COVER(fishman_left_unshown);
    log->show = FISHMAN_SHOW_GONE;
    leave(s);
    return;
  }
  const uint16_t form = field(s, FISHMAN_DP_FORM);
  if (negative(form)) {
    log->show = FISHMAN_SHOW_FLYING;
  } else {
    log->show = form == 0 ? FISHMAN_SHOW_SWIMMING : FISHMAN_SHOW_STANDING;
    const uint16_t wait = (uint16_t)(field(s, FISHMAN_DP_PICTURE_WAIT) - 1);
    set_field(s, FISHMAN_DP_PICTURE_WAIT, wait);
    if (negative(wait)) {
      log->new_picture = true;
      set_field(s, FISHMAN_DP_PICTURE_WAIT, FISHMAN_PICTURE_PASSES);
      if (form == 0)
        next_swim_picture(s);
      else
        next_stand_picture(s);
    }
  }
  const Point me = position(s);
  set_record_field(s, ACTOR_X, me.x);
  set_record_field(s, ACTOR_Y, me.y);
}

static bool state_known(uint16_t state) {
  return state == FISHMAN_STATE_SWIM || state == FISHMAN_STATE_SWIM_TURNED ||
         state == FISHMAN_STATE_CLOSE_IN || state == FISHMAN_STATE_PATROL ||
         state == FISHMAN_STATE_LINE_UP_DOWN ||
         state == FISHMAN_STATE_LINE_UP_ACROSS ||
         state == FISHMAN_STATE_FLIGHT || state == FISHMAN_STATE_LANDED ||
         state == FISHMAN_STATE_DIVE;
}

bool fishman_frame_supported(const Wram* w, uint16_t page) {
  const uint16_t state = wram_r16(w, (uint16_t)(page + FISHMAN_DP_STATE));
  const uint16_t way = wram_r16(w, (uint16_t)(page + FISHMAN_DP_WAY));
  if (!state_known(state)) return false;
  if (way > FISHMAN_LAST_WAY || (way & 1) != 0) return false;
  const uint16_t picture = wram_r16(w, (uint16_t)(page + FISHMAN_DP_PICTURE));
  if (picture > FISHMAN_SWIM_PICTURES) return false;
  // A leap over no distance would never finish its pass.
  const bool flying =
      state == FISHMAN_STATE_FLIGHT || state == FISHMAN_STATE_DIVE;
  return !flying || wram_r16(w, (uint16_t)(page + FISHMAN_DP_SPAN)) != 0;
}

FishmanFate fishman_frame(Wram* w, const Rom* rom, uint16_t page, bool carry,
                          FishmanLog* log) {
  FishmanLog scratch;
  if (log == NULL) log = &scratch;
  *log = (FishmanLog){0};
  Fishman s = {w, rom, page, wram_r16(w, (uint16_t)(page + FISHMAN_DP_RECORD)),
               log, {carry, false, false, false}};

  log->state = field(&s, FISHMAN_DP_STATE);
  switch (log->state) {
    case FISHMAN_STATE_SWIM:
      PORT_COVER(fishman_swam);
      swim(&s);
      break;
    case FISHMAN_STATE_SWIM_TURNED:
      PORT_COVER(fishman_swam_turned);
      swim_turned(&s);
      break;
    case FISHMAN_STATE_CLOSE_IN:
      PORT_COVER(fishman_closing_in);
      close_in(&s);
      break;
    case FISHMAN_STATE_PATROL:
      PORT_COVER(fishman_patrolled);
      patrol(&s);
      break;
    case FISHMAN_STATE_LINE_UP_DOWN:
      PORT_COVER(fishman_lining_up_down);
      line_up(&s, true);
      break;
    case FISHMAN_STATE_LINE_UP_ACROSS:
      PORT_COVER(fishman_lining_up_across);
      line_up(&s, false);
      break;
    case FISHMAN_STATE_FLIGHT:
      PORT_COVER(fishman_flew);
      fly(&s, FISHMAN_STATE_LANDED);
      break;
    case FISHMAN_STATE_DIVE:
      PORT_COVER(fishman_dived);
      fly(&s, FISHMAN_STATE_SPLASH);
      break;
    default:
      land(&s);
      break;
  }
  if (log->declined) return FISHMAN_SLEEPS;
  show(&s);

  log->c = s.flags.c;
  log->v = s.flags.v;
  log->c_set = s.flags.c_set;
  log->v_set = s.flags.v_set;
  if (field(&s, FISHMAN_DP_FATE) != 0) return FISHMAN_ENDS;
  set_field(&s, FISHMAN_DP_HIT_BY, 0);
  log->ticks = field(&s, FISHMAN_DP_TICKS);
  return FISHMAN_SLEEPS;
}
