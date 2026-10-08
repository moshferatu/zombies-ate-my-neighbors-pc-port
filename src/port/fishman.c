// The fishman's state bodies -- see port/fishman.h.

#include "port/fishman.h"

#include <stddef.h>

#include "port/coverage.h"
#include "port/flags.h"
#include "port/rng.h"
#include "port/thread.h"

typedef struct {
  uint16_t x, y;
} Point;

// How near whoever is nearest has to be for each thing it does.
#define FISHMAN_CLOSE_IN_WITHIN 0x0080
#define FISHMAN_NOTICE_WITHIN 0x00d0
#define FISHMAN_BITE_WITHIN 0x0018
#define FISHMAN_KEEP_CLOSING_WITHIN 0x00af
#define FISHMAN_STALK_NO_NEARER 0x001c
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
// Before a leap or a dive it waits one to eight ticks, by a draw.
#define FISHMAN_WAIT_MASK 0x0007
// A spot under this far across is no leap, and one straight above or below
// is no dive.
#define FISHMAN_LEAP_NO_SHORTER 0x0020
#define FISHMAN_DIVE_NO_SHORTER 0x0001
// In the air: its picture, what it is to hit, and how high a leap begins.
#define FISHMAN_AIR_PICTURE 0xf170u
#define FISHMAN_AIR_ID 0x0036
#define FISHMAN_LEAP_HEIGHT 0x0010
// The thread a leap leaves behind it where it left the water.
#define FISHMAN_LEAP_SPLASH 0xe72cu
// On land, somebody this far across is swept at, and the one that comes
// ashore sweeps on a draw under the second.
#define FISHMAN_SWEEP_ACROSS 0x0008
#define FISHMAN_SWEEP_ODDS 0x96
// The sweep faces the mirrored ways from here.
#define FISHMAN_SWEEP_MIRRORED_FROM 0x000c
#define FISHMAN_SWEEP_TICKS 5
// The lists of pictures: before a sweep, after it, and of a splash.
#define FISHMAN_SWEEP_PICTURES 0xe3a0u
#define FISHMAN_SWEPT_PICTURES 0xe3b0u
#define FISHMAN_SPLASH_PICTURES 0xdc0cu
// Where the show's call to ask after the players comes back.
#define FISHMAN_SHOW_ASKED_FROM 0xe61au

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

// It begins only where the tile has both of these: the water's bit, and the
// one `terrain_blocked` reads.
#define FISHMAN_DEEP_WATER 0x0101
// What the one that patrols counts for in the level's load.
#define FISHMAN_PATROL_LOAD 0x0015
// Its first picture, its handler and its first way.
#define FISHMAN_FIRST_PICTURE 0xf07au
#define FISHMAN_PICTURE_BANK 0x0090
#define FISHMAN_HANDLER 0xe6e4u
#define FISHMAN_ATTR 0x0c00
#define FISHMAN_PATROL_FIRST_WAY 0x000e
#define FISHMAN_WATER_WORD 3
#define FISHMAN_NO_BLOW 0xffffu
// What X holds through its end.
#define FISHMAN_END_X 0x0200

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
// By which way a leap is: a flag put on its record, or with the top bit a
// mask that takes one off.
#define FISHMAN_LEAP_FACES 0xe143u
#define FISHMAN_DIVE_PICTURES 0xdbb5u   // by way
// By way, doubled: where water is looked for, from where it was stopped.
#define FISHMAN_WATERSIDE 0xdae0u
// ...with up to 30 across put on by a draw, or taken off for the ways from
// here on.
#define FISHMAN_SEEK_MASK 0x000f
#define FISHMAN_SEEK_BACK_FROM 0x0018
#define FISHMAN_SEEK_BELOW 8
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
  const FishmanLoop* loop;
  uint16_t sp;  // the thread's stack, under the body's returns
} Fishman;

const FishmanLoop FISHMAN_LOOP = {FISHMAN_YIELD_PC, FISHMAN_FATE_PC, 0xe4b9u,
                                  0xe4bcu};
const FishmanLoop FISHMAN_PATROL_LOOP = {FISHMAN_PATROL_YIELD_PC,
                                         FISHMAN_PATROL_FATE_PC, 0xe55fu,
                                         0xe562u};

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

static void push_return(Fishman* s, uint16_t to) {
  s->sp = (uint16_t)(s->sp - 2);
  wram_w16(s->w, (uint16_t)(s->sp + 1), to);
}

// The body stops here, in the middle. `from` is what its `JSR` to where it
// stops pushed, or 0 for a body that stops in itself. The loop's return is
// under it.
static void stop_at(Fishman* s, FishmanStop stop, uint16_t a, uint16_t from) {
  push_return(s, s->loop->body_return);
  if (from != 0) push_return(s, from);
  s->log->stop = stop;
  s->log->a = a;
  s->log->s = s->sp;
}

// A few ticks where it is, before a leap or a dive.
static uint16_t wait_ticks(Fishman* s);

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

void fishman_landing(Wram* w, PortCpu* c, FishmanTiles* log) {
  TerrainFootprint under;
  terrain_footprint_read(w, c->x, c->y, TERRAIN_ORIGIN_X,
                         FISHMAN_LANDING_ORIGIN_Y, &under);
  int last = TERRAIN_PROBE_COUNT - 1;
  log->all = true;
  for (int i = 0; i < TERRAIN_PROBE_COUNT; i++) {
    if ((under.attrs[i] & (FISHMAN_LANDING | FISHMAN_NO_LANDING)) ==
        FISHMAN_LANDING)
      continue;
    log->all = false;
    last = i;
    break;
  }
  log->tiles = last + 1;
  if (log->all) {
    PORT_COVER(fishman_landing_good);
  } else {
    PORT_COVER(fishman_landing_bad);
  }
  c->a = (uint16_t)(under.attrs[last] & (FISHMAN_LANDING | FISHMAN_NO_LANDING));
  c->x = under.row;
  c->y = under.tiles[last];
  set_nz16(c, c->d);  // the `PLD`
  set_c(c, !log->all);
  set_v(c, last == TERRAIN_PROBE_COUNT - 1 ? under.v_last_row : under.v_map);
  c->pc = log->all ? FISHMAN_LANDING_YES_PC : FISHMAN_LANDING_NO_PC;
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

static uint16_t wait_ticks(Fishman* s) {
  return (uint16_t)((random_byte(s) & FISHMAN_WAIT_MASK) + 1);
}

// `$81:E007`: leap at somebody, on a draw, if there is somewhere near them
// to come down. It waits a few ticks first, and the pass stops there.
// `from` is what the body's `JSR` here pushed. True when the body goes on.
static bool maybe_leap(Fishman* s, uint16_t from) {
  FishmanLog* log = s->log;
  if (flags_at_least(&s->flags, random_byte(s), FISHMAN_LEAP_ODDS)) {
    log->leap = FISHMAN_LEAP_NO_DRAW;
    return true;
  }
  uint16_t dist;
  const uint16_t target = nearest_actor(s, &dist);
  if (flags_at_least(&s->flags, dist, FISHMAN_LEAP_WITHIN)) {
    log->leap = FISHMAN_LEAP_NOBODY;
    return true;
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
    return true;
  }
  if (!somewhere_to_land(s, spot, &log->landing)) {
    log->leap = FISHMAN_LEAP_NO_LANDING;
    return true;
  }
  if (someone_at(s, spot)) {
    PORT_COVER(fishman_leap_taken);
    log->leap = FISHMAN_LEAP_TAKEN;
    return true;
  }
  PORT_COVER(fishman_leapt);
  log->leap = FISHMAN_LEAP_LEAPS;
  stop_at(s, FISHMAN_STOP_LEAP, wait_ticks(s), from);
  return false;
}

// ---------------------------------------------------------------------------
// On land
// ---------------------------------------------------------------------------

// `$81:DA85`: stopped by the ground at `at`, is there water a little past
// it? With water there and under it, on the level, it dives, and that is
// the ROM's.
static void seek_water(Fishman* s, Point at, FishmanSeek* k) {
  uint16_t off = (uint16_t)((random_byte(s) & FISHMAN_SEEK_MASK) << 1);
  const uint16_t way = (uint16_t)(field(s, FISHMAN_DP_WAY) << 1);
  k->back = flags_at_least(&s->flags, way, FISHMAN_SEEK_BACK_FROM);
  if (k->back) off = (uint16_t)(0u - off);
  set_field(s, FISHMAN_DP_SCRATCH, off);
  // The second sum takes the carry of the first.
  const uint16_t part = flags_add(&s->flags, at.x, off);
  const uint16_t x = flags_adc(&s->flags, part,
                               table_word(s, FISHMAN_WATERSIDE, way),
                               s->flags.c);
  set_field(s, FISHMAN_DP_LAND_X, x);
  const uint16_t y = flags_add(&s->flags, at.y,
                               table_word(s, FISHMAN_WATERSIDE + 2, way));
  set_field(s, FISHMAN_DP_LAND_Y, y);

  // Every way back is by a `SEC`. A tile's lookup leaves an overflow the
  // port does not follow.
  flags_carry(&s->flags, true);
  flags_overflow_unknown(&s->flags);
  TileAttrsRegs tile;
  tile_attrs_at_pixel(s->w, x, y, &tile);
  k->tiles = 1;
  if ((tile.a & FISHMAN_WATER) == 0) return;
  tile_attrs_at_pixel(s->w, x, (uint16_t)(y + FISHMAN_SEEK_BELOW), &tile);
  k->tiles = 2;
  if ((tile.a & FISHMAN_WATER) == 0) return;
  BoundsRegs edge;
  terrain_out_of_bounds(s->w, x, y, &edge);
  k->edge_asked = true;
  k->edge = edge.exit;
  if (edge.c) return;
  PORT_COVER(fishman_found_water);
  s->log->declined = true;
}

// May it walk to `at`? Ground that may be walked on, and nobody there.
static bool can_walk_to(Fishman* s, Point at, FishmanLandProbe* k) {
  terrain_blocked_enemy(s->w, at.x, at.y, &k->ground);
  flags_carry(&s->flags, k->ground.blocked);
  flags_overflow(&s->flags, k->ground.v);
  if (k->ground.blocked) {
    PORT_COVER(fishman_walk_stopped);
    seek_water(s, at, &k->seek);
    return false;
  }
  k->someone = someone_at(s, at);
  return !k->someone;
}

// `$81:D8B6`: take the step being tried, an axis at a time, the second from
// wherever the first left it.
static void walk(Fishman* s) {
  FishmanLog* log = s->log;
  Point me = position(s);
  const Point to = {field(s, FISHMAN_DP_TRY_X), field(s, FISHMAN_DP_TRY_Y)};
  if (can_walk_to(s, (Point){to.x, me.y}, &log->land[0])) {
    me.x = to.x;
    set_field(s, FISHMAN_DP_X, me.x);
  }
  if (log->declined) return;
  if (can_walk_to(s, (Point){me.x, to.y}, &log->land[1]))
    set_field(s, FISHMAN_DP_Y, to.y);
}

// `$81:E309`: it sweeps from its next pass. The one that comes ashore does
// so on a draw, and otherwise walks on.
static void sweep_from_next_pass(Fishman* s) {
  FishmanLog* log = s->log;
  set_state(s, FISHMAN_STATE_LURK);
  if (field(s, FISHMAN_DP_STAYS) != 0) return;
  log->sweep_drawn = true;
  if (!flags_at_least(&s->flags, random_byte(s), FISHMAN_SWEEP_ODDS)) return;
  log->sweep_off = true;
  set_state(s, FISHMAN_STATE_STALK);
}

// `$81:DA20`: at whoever is nearest. Too near and to one side, it sweeps.
// With nobody near it leaves, or goes about as the one that comes ashore
// does.
static void stalk_on(Fishman* s) {
  FishmanLog* log = s->log;
  uint16_t dist;
  const uint16_t target = nearest_actor(s, &dist);
  if (!flags_at_least(&s->flags, dist, FISHMAN_STALK_NO_NEARER)) {
    set_field(s, FISHMAN_DP_TARGET, target);
    const uint16_t across =
        flags_sub(&s->flags, field(s, FISHMAN_DP_X), place_of(s, target).x);
    log->beside_negative = negative(across);
    if (flags_at_least(&s->flags, magnitude(across), FISHMAN_SWEEP_ACROSS)) {
      PORT_COVER(fishman_stalk_sweeps);
      log->stalk = FISHMAN_STALK_SWEEPS;
      sweep_from_next_pass(s);
      return;
    }
    PORT_COVER(fishman_stalk_beside);
    log->stalk = FISHMAN_STALK_BESIDE;
  } else if (flags_at_least(&s->flags, dist, FISHMAN_KEEP_CLOSING_WITHIN)) {
    if (nobody_about(s)) {
      PORT_COVER(fishman_stalk_left);
      log->stalk = FISHMAN_STALK_LEFT;
      leave(s);
    } else {
      PORT_COVER(fishman_stalk_goes_about);
      log->stalk = FISHMAN_STALK_GOES_ABOUT;
      set_state(s, FISHMAN_STATE_ASHORE);
    }
    return;
  }
  set_field(s, FISHMAN_DP_TARGET, target);
  const uint16_t way = (uint16_t)(bearing_to(s, target) << 1);
  set_field(s, FISHMAN_DP_WAY, way);
  flags_carry(&s->flags, false);  // the `ASL`s
  try_way(s, way);
  walk(s);
}

// `$81:DA1D`: on land. With water to go back to it waits a few ticks, and
// the pass stops there.
static void stalk(Fishman* s) {
  FishmanLog* log = s->log;
  PortCpu cpu = {0};
  cpu.d = s->page;
  log->wander_asked = true;
  wander_pick(s->w, &cpu, &log->wander);
  if (cpu.pc == WANDER_FOUND_PC) {
    PORT_COVER(fishman_stalk_found_water);
    log->stalk = FISHMAN_STALK_FOUND_WATER;
    flags_carry(&s->flags, flag(&cpu, PORT_P_C));
    stop_at(s, FISHMAN_STOP_DIVE, wait_ticks(s), FISHMAN_DIVE_FROM_STALK);
    return;
  }
  stalk_on(s);
}

// ---------------------------------------------------------------------------
// The state bodies
// ---------------------------------------------------------------------------

// `$81:DDFD`: the way it faces, until something stops it.
static void swim(Fishman* s) {
  look(s);
  maybe_look_about(s);
  if (s->log->declined) return;
  if (!maybe_leap(s, FISHMAN_LEAP_FROM_SWIM)) return;
  swim_on(s);
}

// `$81:DE36`: the same, turning at every opening.
static void swim_turned(Fishman* s) {
  maybe_look_about(s);
  if (s->log->declined) return;
  if (!maybe_leap(s, FISHMAN_LEAP_FROM_SWIM_TURNED)) return;
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
  if (!maybe_leap(s, FISHMAN_LEAP_FROM_CLOSE_IN)) return;

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
  if (!maybe_leap(s, FISHMAN_LEAP_FROM_PATROL)) return;

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

// `$81:E31C`: it turns to whoever it is after, and shows three pictures.
// The pass stops there, and the sweep is what follows them.
static void sweep_turn(Fishman* s) {
  FishmanLog* log = s->log;
  ActorBearingRegs scratch;
  ActorBearingRegs* r = log->bearings < FISHMAN_MAX_SLIDES
                            ? &log->bearing[log->bearings]
                            : &scratch;
  log->bearings++;
  actor_bearing(s->w, s->rom, s->record, field(s, FISHMAN_DP_TARGET), r);
  if (r->a != 0) flags_overflow(&s->flags, false);
  const uint16_t way = (uint16_t)(r->a << 1);
  set_field(s, FISHMAN_DP_WAY, way);
  const uint16_t flags = record_field(s, ACTOR_FLAGS);
  log->faces_back = flags_at_least(&s->flags, way, FISHMAN_SWEEP_MIRRORED_FROM);
  set_record_field(s, ACTOR_FLAGS,
                   log->faces_back ? (uint16_t)(flags | FISHMAN_MIRROR)
                                   : (uint16_t)(flags & ~FISHMAN_MIRROR));
  stop_at(s, FISHMAN_STOP_SWEEP, FISHMAN_SWEEP_PICTURES, 0);
}

// `$81:DB63`: off the ground, and the first pass of the dive.
static void dive_begin(Fishman* s) {
  set_record_field(s, ACTOR_META,
                   table_word(s, FISHMAN_DIVE_PICTURES,
                              field(s, FISHMAN_DP_WAY)));
  set_record_field(s, ACTOR_COLLIDE_ID, FISHMAN_AIR_ID);
  set_record_field(s, ACTOR_FLAGS,
                   record_field(s, ACTOR_FLAGS) | ACTOR_PRIORITY_TOP);
  set_field(s, FISHMAN_DP_FORM, 0xffffu);
  set_field(s, FISHMAN_DP_TICKS, FISHMAN_TICKS_FAST);
  set_state(s, FISHMAN_STATE_DIVE);
}

// `$81:DBCD`: back in the water. Two pictures of the splash, and the pass
// stops there.
static void splash(Fishman* s) {
  set_field(s, FISHMAN_DP_TICKS, FISHMAN_TICKS_SLOW);
  set_record_field(s, ACTOR_META_BANK, FISHMAN_PICTURE_BANK);
  stop_at(s, FISHMAN_STOP_SPLASH, FISHMAN_SPLASH_PICTURES, 0);
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

// ---------------------------------------------------------------------------
// The one that patrols: its beginning and its end
// ---------------------------------------------------------------------------

static void flags_to_cpu(PortCpu* c, const PortFlags* f) {
  set_c(c, f->c);
  set_v(c, f->v);
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

void fishman_patrol_begin(Wram* w, const Rom* rom, PortCpu* c,
                          FishmanBeginLog* log) {
  *log = (FishmanBeginLog){0};
  const uint16_t page = c->d;
  c->pc = FISHMAN_PATROL_RTL_PC;

  SpawnRoomRegs room;
  spawn_has_room(w, &room);
  if (room.c) {
    PORT_COVER(fishman_begin_no_room);
    log->how = FISHMAN_BEGIN_NO_ROOM;
    c->a = room.a;
    c->p = (uint8_t)(c->p & ~(PORT_P_N | PORT_P_Z));
    if (room.n) c->p |= PORT_P_N;
    if (room.z) c->p |= PORT_P_Z;
    set_c(c, true);
    return;
  }
  const Point at = {wram_r16(w, (uint16_t)(page + FISHMAN_DP_PLACED_X)),
                    wram_r16(w, (uint16_t)(page + FISHMAN_DP_PLACED_Y))};
  TileAttrsRegs tile;
  tile_attrs_at_pixel(w, at.x, at.y, &tile);
  c->a = (uint16_t)(tile.a & FISHMAN_DEEP_WATER);
  cmp16(c, c->a, FISHMAN_DEEP_WATER);
  if (c->a != FISHMAN_DEEP_WATER) {
    PORT_COVER(fishman_begin_not_water);
    log->how = FISHMAN_BEGIN_NOT_WATER;
    log->v_unknown = true;
    return;
  }

  FishmanLog pass = {0};
  Fishman s = {w, rom, page, 0, &pass, {0}};
  wram_w16(w, W_SPAWN_LOAD, flags_add(&s.flags, wram_r16(w, W_SPAWN_LOAD),
                                      FISHMAN_PATROL_LOAD));
  SlotAllocRegs slot;
  actor_slot_alloc(w, c->db, &slot);
  if (slot.c) {
    log->declined = true;
    return;
  }
  PORT_COVER(fishman_begun);
  log->how = FISHMAN_BEGIN_BEGAN;
  const uint16_t record = slot.a;
  log->record = record;
  s.record = record;
  // The search for a record takes twenty off as it goes.
  if (record != ACTOR_SLOT_LAST) flags_overflow(&s.flags, false);

  set_field(&s, FISHMAN_DP_RECORD, record);
  set_field(&s, FISHMAN_DP_X, at.x);
  set_field(&s, FISHMAN_DP_Y, at.y);
  set_record_field(&s, ACTOR_X, at.x);
  set_record_field(&s, ACTOR_Z, 0);
  set_record_field(&s, ACTOR_Y, at.y);
  set_record_field(&s, ACTOR_META, FISHMAN_FIRST_PICTURE);
  set_record_field(&s, ACTOR_META_BANK, FISHMAN_PICTURE_BANK);
  set_record_field(&s, ACTOR_THREAD, wram_r16(w, W_SCHED_CUR_TASK));
  set_record_field(&s, ACTOR_COLLIDE_ID, FISHMAN_LANDED_ID);
  set_record_field(&s, ACTOR_FLAGS,
                   record_field(&s, ACTOR_FLAGS) | ACTOR_DRAW);
  set_record_field(&s, ACTOR_ATTR, FISHMAN_ATTR);

  set_field(&s, FISHMAN_DP_WATER_WANTED, FISHMAN_WATER_WORD);
  set_field(&s, FISHMAN_DP_WATER_HAD, FISHMAN_WATER_WORD);
  set_field(&s, FISHMAN_DP_PICTURE, 0);
  set_field(&s, FISHMAN_DP_PICTURE_WAIT, 0);
  set_field(&s, FISHMAN_DP_FATE, 0);
  set_field(&s, FISHMAN_DP_HIT_BY, 0);
  set_field(&s, FISHMAN_DP_UNREAD, 0);
  set_field(&s, FISHMAN_DP_FORM, 0);
  set_field(&s, FISHMAN_DP_TICKS, FISHMAN_TICKS_SLOW);
  set_field(&s, FISHMAN_DP_BLOW, FISHMAN_NO_BLOW);
  set_field(&s, FISHMAN_DP_BLOW_AT, 0);

  c->a = FISHMAN_HANDLER;
  c->y = FISHMAN_BANK;
  thread_set_handler(w, c);

  set_field(&s, FISHMAN_DP_STAYS, 0xffffu);
  set_field(&s, FISHMAN_DP_WAY, FISHMAN_PATROL_FIRST_WAY);
  patrol_again(&s);
  log->players = pass.players[0];
  log->alone = field(&s, FISHMAN_DP_FATE) != 0;

  set_field(&s, FISHMAN_DP_HIT_BY, 0);
  c->a = field(&s, FISHMAN_DP_TICKS);
  set_nz16(c, c->a);
  flags_to_cpu(c, &s.flags);
  c->pc = FISHMAN_PATROL_YIELD_PC;
}

bool fishman_patrol_end_supported(const Wram* w, uint16_t page) {
  const uint16_t record = wram_r16(w, (uint16_t)(page + FISHMAN_DP_RECORD));
  return wram_r16(w, (uint16_t)(page + FISHMAN_DP_HIT_BY)) == 0 &&
         wram_r16(w, (uint16_t)(page + FISHMAN_DP_BLOW)) == FISHMAN_NO_BLOW &&
         wram_r16(w, W_SPAWN_LOAD) >= FISHMAN_PATROL_LOAD &&
         wram_r16(w, W_SPAWN_LOAD) < 0x8000u &&
         record >= W_ACTOR_SLOTS && record <= ACTOR_SLOT_LAST &&
         place_in_list(w, record) >= 0;
}

void fishman_patrol_end(Wram* w, PortCpu* c, FishmanEndLog* log) {
  PORT_COVER(fishman_ended);
  const uint16_t page = c->d;
  PortFlags f = {0};
  wram_w16(w, W_SPAWN_LOAD,
           flags_sub(&f, wram_r16(w, W_SPAWN_LOAD), FISHMAN_PATROL_LOAD));
  const uint16_t record = wram_r16(w, (uint16_t)(page + FISHMAN_DP_RECORD));
  log->place = place_in_list(w, record);
  SlotFreeRegs r;
  actor_slot_free(w, record, page, FISHMAN_END_X, c->y, &r);
  c->x = r.x;
  c->y = r.y;
  c->a = wram_r16(w, (uint16_t)(page + FISHMAN_DP_BLOW));
  cmp16(c, c->a, FISHMAN_NO_BLOW);
  set_v(c, f.v);
  c->pc = FISHMAN_PATROL_RTL_PC;
}

static bool state_known(uint16_t state) {
  return state == FISHMAN_STATE_SWIM || state == FISHMAN_STATE_SWIM_TURNED ||
         state == FISHMAN_STATE_CLOSE_IN || state == FISHMAN_STATE_PATROL ||
         state == FISHMAN_STATE_LINE_UP_DOWN ||
         state == FISHMAN_STATE_LINE_UP_ACROSS ||
         state == FISHMAN_STATE_FLIGHT || state == FISHMAN_STATE_LANDED ||
         state == FISHMAN_STATE_DIVE || state == FISHMAN_STATE_STALK ||
         state == FISHMAN_STATE_LURK || state == FISHMAN_STATE_DIVE_BEGIN ||
         state == FISHMAN_STATE_SPLASH;
}

// Its show reads two tables by these.
static bool shows(const Wram* w, uint16_t page) {
  const uint16_t way = wram_r16(w, (uint16_t)(page + FISHMAN_DP_WAY));
  return way <= FISHMAN_LAST_WAY && (way & 1) == 0 &&
         wram_r16(w, (uint16_t)(page + FISHMAN_DP_PICTURE)) <=
             FISHMAN_SWIM_PICTURES;
}

bool fishman_frame_supported(const Wram* w, uint16_t page) {
  const uint16_t state = wram_r16(w, (uint16_t)(page + FISHMAN_DP_STATE));
  if (!state_known(state) || !shows(w, page)) return false;
  // A leap over no distance would never finish its pass.
  const bool flying = state == FISHMAN_STATE_FLIGHT ||
                      state == FISHMAN_STATE_DIVE ||
                      state == FISHMAN_STATE_DIVE_BEGIN;
  return !flying || wram_r16(w, (uint16_t)(page + FISHMAN_DP_SPAN)) != 0;
}

// `$81:E560` and `$81:E4BA`: the rest of a pass, from its show.
static FishmanFate end_pass(Fishman* s) {
  FishmanLog* log = s->log;
  show(s);
  log->c = s->flags.c;
  log->v = s->flags.v;
  log->c_set = s->flags.c_set;
  log->v_set = s->flags.v_set;
  if (field(s, FISHMAN_DP_FATE) != 0) return FISHMAN_ENDS;
  set_field(s, FISHMAN_DP_HIT_BY, 0);
  log->ticks = field(s, FISHMAN_DP_TICKS);
  return FISHMAN_SLEEPS;
}

FishmanFate fishman_frame(Wram* w, const Rom* rom, const FishmanLoop* loop,
                          uint16_t page, uint16_t sp, bool carry,
                          FishmanLog* log) {
  FishmanLog scratch;
  if (log == NULL) log = &scratch;
  *log = (FishmanLog){0};
  Fishman s = {w, rom, page, wram_r16(w, (uint16_t)(page + FISHMAN_DP_RECORD)),
               log, {carry, false, false, false}, loop, sp};

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
    case FISHMAN_STATE_STALK:
      stalk(&s);
      if (!log->declined) PORT_COVER(fishman_stalked);
      break;
    case FISHMAN_STATE_LURK:
      PORT_COVER(fishman_sweep_turned);
      sweep_turn(&s);
      break;
    case FISHMAN_STATE_DIVE_BEGIN:
      PORT_COVER(fishman_dive_began);
      dive_begin(&s);
      fly(&s, FISHMAN_STATE_SPLASH);
      break;
    case FISHMAN_STATE_SPLASH:
      PORT_COVER(fishman_splashed);
      splash(&s);
      break;
    default:
      land(&s);
      break;
  }
  if (log->declined) return FISHMAN_SLEEPS;
  if (log->stop != FISHMAN_STOP_PASS) {
    log->c = s.flags.c;
    log->v = s.flags.v;
    log->c_set = s.flags.c_set;
    log->v_set = s.flags.v_set;
    return FISHMAN_SLEEPS;
  }
  return end_pass(&s);
}

// ---------------------------------------------------------------------------
// Where a body wakes
// ---------------------------------------------------------------------------

static const FishmanLoop* loop_of(uint16_t body_return) {
  if (body_return == FISHMAN_LOOP.body_return) return &FISHMAN_LOOP;
  if (body_return == FISHMAN_PATROL_LOOP.body_return)
    return &FISHMAN_PATROL_LOOP;
  return NULL;
}

// The rest of a pass, for a stretch that began with `returns` words of the
// body's on the stack. It leaves as the loop does. The show's own return
// goes where the loop's was, and under it is what its call to ask after
// the players pushed.
static FishmanFate finish(Fishman* s, PortCpu* c, int returns) {
  FishmanLog* log = s->log;
  const uint16_t top = (uint16_t)(c->s + 2 * returns);
  const FishmanFate fate = end_pass(s);
  wram_w16(s->w, (uint16_t)(top - 1), s->loop->shown_return);
  wram_w8(s->w, (uint16_t)(top - 2), FISHMAN_BANK);
  wram_w16(s->w, (uint16_t)(top - 4), FISHMAN_SHOW_ASKED_FROM);

  const bool sleeps = fate == FISHMAN_SLEEPS;
  c->s = top;
  c->pc = sleeps ? s->loop->yield_pc : s->loop->fate_pc;
  c->a = sleeps ? log->ticks : field(s, FISHMAN_DP_FATE);
  set_nz16(c, c->a);
  if (log->c_set) set_c(c, log->c);
  if (log->v_set) set_v(c, log->v);
  return fate;
}

static Fishman woken(Wram* w, const Rom* rom, PortCpu* c, FishmanLog* log,
                     int returns) {
  *log = (FishmanLog){0};
  const uint16_t loop_return =
      wram_r16(w, (uint16_t)(c->s + 2 * returns - 1));
  return (Fishman){w, rom, c->d,
                   wram_r16(w, (uint16_t)(c->d + FISHMAN_DP_RECORD)), log,
                   {flag(c, PORT_P_C), flag(c, PORT_P_V), false, false},
                   loop_of(loop_return), (uint16_t)(c->s + 2 * returns)};
}

// Which way the spot it is going to is, and how far across and down. The
// ways are `actor_bearing_point`'s, which knows only left and right.
static uint16_t gap_to(Fishman* s, uint16_t to, uint16_t from,
                       uint16_t sign_at, uint16_t gap_at, bool* is_negative) {
  uint16_t gap = flags_sub(&s->flags, to, from);
  *is_negative = negative(gap);
  set_field(s, sign_at, *is_negative ? 0xffffu : 1);
  gap = magnitude(gap);
  set_field(s, gap_at, gap);
  return gap;
}

static uint16_t way_to_spot(Fishman* s, Point spot, FishmanWakeLog* k) {
  ActorBearingRegs r;
  k->same_row = record_field(s, ACTOR_Y) == spot.y;
  k->same_column = record_field(s, ACTOR_X) == spot.x;
  actor_bearing_point(s->w, s->rom, s->record, spot.x, spot.y, &r);
  set_field(s, FISHMAN_DP_LEAP_WAY, r.a);
  return r.a;
}

// The longer of the two gaps.
static uint16_t longer_gap(Fishman* s, uint16_t across, Point spot,
                           FishmanWakeLog* k) {
  const uint16_t down = gap_to(s, spot.y, field(s, FISHMAN_DP_Y),
                               FISHMAN_DP_SIGN_Y, FISHMAN_DP_GAP_Y,
                               &k->negative[1]);
  k->across_longer = !flags_at_least(&s->flags, down, across);
  return k->across_longer ? across : down;
}

bool fishman_leap_wake_supported(const Wram* w, uint16_t page, uint16_t s) {
  const uint16_t from = wram_r16(w, (uint16_t)(s + 1));
  return (from == FISHMAN_LEAP_FROM_SWIM ||
          from == FISHMAN_LEAP_FROM_SWIM_TURNED ||
          from == FISHMAN_LEAP_FROM_CLOSE_IN ||
          from == FISHMAN_LEAP_FROM_PATROL) &&
         loop_of(wram_r16(w, (uint16_t)(s + 3))) != NULL && shows(w, page);
}

// `$81:E07C`: the leap, to the spot the pass before chose. Too little
// across and it does not leap after all.
FishmanFate fishman_leap_wake(Wram* w, const Rom* rom, PortCpu* c,
                              FishmanWakeLog* k) {
  *k = (FishmanWakeLog){0};
  k->splash_slot = -1;
  Fishman s = woken(w, rom, c, &k->pass, 2);
  const Point spot = {field(&s, FISHMAN_DP_LAND_X),
                      field(&s, FISHMAN_DP_LAND_Y)};
  const uint16_t way = way_to_spot(&s, spot, k);
  const uint16_t across = gap_to(&s, spot.x, field(&s, FISHMAN_DP_X),
                                 FISHMAN_DP_SIGN_X, FISHMAN_DP_GAP_X,
                                 &k->negative[0]);
  if (!flags_at_least(&s.flags, across, FISHMAN_LEAP_NO_SHORTER)) {
    PORT_COVER(fishman_leap_too_short);
    k->stays = true;
    return finish(&s, c, 2);
  }
  PORT_COVER(fishman_leap_began);
  const uint16_t span = (uint16_t)(longer_gap(&s, across, spot, k) >> 1);
  set_field(&s, FISHMAN_DP_SPAN, span);
  set_field(&s, FISHMAN_DP_RISE, (uint16_t)(span >> 1));
  flags_carry(&s.flags, (span & 1) != 0);
  set_field(&s, FISHMAN_DP_PART_X, 0);
  set_field(&s, FISHMAN_DP_PART_Y, 0);

  set_field(&s, FISHMAN_DP_FORM, 0xffffu);
  const uint16_t face = table_word(&s, FISHMAN_LEAP_FACES, (uint16_t)(way << 1));
  uint16_t flags = record_field(&s, ACTOR_FLAGS);
  k->faces_cleared = negative(face);
  flags = k->faces_cleared ? (uint16_t)(flags & face) : (uint16_t)(flags | face);
  set_record_field(&s, ACTOR_FLAGS, flags | ACTOR_PRIORITY_TOP);
  set_record_field(&s, ACTOR_META, FISHMAN_AIR_PICTURE);
  set_record_field(&s, ACTOR_COLLIDE_ID, FISHMAN_AIR_ID);
  set_state(&s, FISHMAN_STATE_FLIGHT);
  set_record_field(&s, ACTOR_Z, FISHMAN_LEAP_HEIGHT);

  // A splash where it left the water: a thread of its own, told where.
  set_field(&s, FISHMAN_DP_PLACED_X, record_field(&s, ACTOR_X));
  set_field(&s, FISHMAN_DP_PLACED_Y, record_field(&s, ACTOR_Y));
  k->splash_slot =
      thread_spawn(w, rom, FISHMAN_LEAP_SPLASH, FISHMAN_BANK, s.page);
  flags_overflow_unknown(&s.flags);
  set_field(&s, FISHMAN_DP_TICKS, FISHMAN_TICKS_FAST);
  return finish(&s, c, 2);
}

bool fishman_dive_wake_supported(const Wram* w, uint16_t page, uint16_t s) {
  return wram_r16(w, (uint16_t)(s + 1)) == FISHMAN_DIVE_FROM_STALK &&
         loop_of(wram_r16(w, (uint16_t)(s + 3))) != NULL && shows(w, page);
}

// `$81:DB10`: the dive, to the water `port/wander.h` found, from its next
// pass. Then the rest of the pass on land it was in the middle of.
FishmanFate fishman_dive_wake(Wram* w, const Rom* rom, PortCpu* c,
                              FishmanWakeLog* k) {
  *k = (FishmanWakeLog){0};
  k->splash_slot = -1;
  Fishman s = woken(w, rom, c, &k->pass, 2);
  const Point spot = {field(&s, FISHMAN_DP_LAND_X),
                      field(&s, FISHMAN_DP_LAND_Y)};
  way_to_spot(&s, spot, k);
  const uint16_t across = gap_to(&s, spot.x, field(&s, FISHMAN_DP_X),
                                 FISHMAN_DP_SIGN_X, FISHMAN_DP_GAP_X,
                                 &k->negative[0]);
  if (!flags_at_least(&s.flags, across, FISHMAN_DIVE_NO_SHORTER)) {
    PORT_COVER(fishman_dive_straight);
    k->stays = true;
    set_state(&s, FISHMAN_STATE_ASHORE_TURNED);
  } else {
    PORT_COVER(fishman_dive_set);
    const uint16_t span = longer_gap(&s, across, spot, k);
    set_field(&s, FISHMAN_DP_SPAN, span);
    set_field(&s, FISHMAN_DP_RISE, (uint16_t)(span >> 1));
    flags_carry(&s.flags, (span & 1) != 0);
    set_field(&s, FISHMAN_DP_PART_X, 0);
    set_field(&s, FISHMAN_DP_PART_Y, 0);
    set_state(&s, FISHMAN_STATE_DIVE_BEGIN);
  }
  stalk_on(&s);
  if (k->pass.declined) return FISHMAN_SLEEPS;
  return finish(&s, c, 2);
}

bool fishman_after_supported(const Wram* w, uint16_t page, uint16_t s) {
  return loop_of(wram_r16(w, (uint16_t)(s + 1))) != NULL && shows(w, page);
}

// `$81:E397`: the sweep is over, and it walks. The one that keeps to its
// pool now wants water to go back to.
FishmanFate fishman_sweep_after(Wram* w, const Rom* rom, PortCpu* c,
                                FishmanLog* log, bool* stays) {
  Fishman s = woken(w, rom, c, log, 1);
  *stays = field(&s, FISHMAN_DP_STAYS) != 0;
  if (*stays) {
    PORT_COVER(fishman_wants_water);
    set_field(&s, FISHMAN_DP_WATER_HAD,
              flags_double(&s.flags, field(&s, FISHMAN_DP_WATER_HAD)));
  } else {
    PORT_COVER(fishman_swept_ashore);
  }
  set_state(&s, FISHMAN_STATE_STALK);
  return finish(&s, c, 1);
}

// `$81:DBE1`: in the water again as it began, some way by a draw.
FishmanFate fishman_splash_after(Wram* w, const Rom* rom, PortCpu* c,
                                 FishmanLog* log) {
  PORT_COVER(fishman_swims_again);
  Fishman s = woken(w, rom, c, log, 1);
  set_record_field(&s, ACTOR_META, FISHMAN_FIRST_PICTURE);
  set_record_field(&s, ACTOR_META_BANK, FISHMAN_PICTURE_BANK);
  set_record_field(&s, ACTOR_COLLIDE_ID, FISHMAN_LANDED_ID);
  set_record_field(&s, ACTOR_FLAGS,
                   record_field(&s, ACTOR_FLAGS) &
                       (uint16_t)~ACTOR_PRIORITY_TOP);
  set_field(&s, FISHMAN_DP_FORM, 0);
  set_field(&s, FISHMAN_DP_PICTURE_WAIT, 0);
  set_field(&s, FISHMAN_DP_PICTURE, 0);
  set_field(&s, FISHMAN_DP_WAY,
            (uint16_t)(((random_byte(&s) & 3) << 2) + FISHMAN_FIRST_WAY));
  flags_carry(&s.flags, false);  // the two `ASL`s
  set_state(&s, FISHMAN_STATE_SWIM);
  return finish(&s, c, 1);
}

// ---------------------------------------------------------------------------
// The splash a leap leaves
// ---------------------------------------------------------------------------

// What it counts for in the level's load, and its pictures.
#define FISHMAN_LEAP_SPLASH_LOAD 0x0007
#define FISHMAN_LEAP_SPLASH_PICTURE 0x81aeu
#define FISHMAN_LEAP_SPLASH_PICTURES 0xe787u

bool fishman_splash_begin(Wram* w, PortCpu* c, uint16_t* record) {
  const uint16_t page = c->d;
  set_c(c, false);
  wram_w16(w, W_SPAWN_LOAD,
           adc16(c, wram_r16(w, W_SPAWN_LOAD), FISHMAN_LEAP_SPLASH_LOAD));
  SlotAllocRegs taken;
  actor_slot_alloc(w, c->db, &taken);
  if (taken.c) return false;
  PORT_COVER(fishman_splash_began);
  const uint16_t splash = taken.a;
  *record = splash;
  // The search for a record takes twenty off as it goes.
  if (splash != ACTOR_SLOT_LAST) set_v(c, false);
  set_c(c, false);

  wram_w16(w, (uint16_t)(page + FISHMAN_DP_RECORD), splash);
  wram_w16(w, (uint16_t)(splash + ACTOR_X),
           wram_r16(w, (uint16_t)(page + FISHMAN_DP_PLACED_X)));
  wram_w16(w, (uint16_t)(splash + ACTOR_Z), 0);
  wram_w16(w, (uint16_t)(splash + ACTOR_Y),
           wram_r16(w, (uint16_t)(page + FISHMAN_DP_PLACED_Y)));
  wram_w16(w, (uint16_t)(splash + ACTOR_META), FISHMAN_LEAP_SPLASH_PICTURE);
  wram_w16(w, (uint16_t)(splash + ACTOR_META_BANK), FISHMAN_PICTURE_BANK);
  wram_w16(w, (uint16_t)(splash + ACTOR_THREAD), wram_r16(w, W_SCHED_CUR_TASK));
  wram_w16(w, (uint16_t)(splash + ACTOR_COLLIDE_ID), 0);
  wram_w16(w, (uint16_t)(splash + ACTOR_FLAGS),
           (uint16_t)(ACTOR_DRAW |
                      wram_r16(w, (uint16_t)(splash + ACTOR_FLAGS))));
  c->x = taken.x;
  c->y = splash;
  c->a = FISHMAN_LEAP_SPLASH_PICTURES;
  set_nz16(c, c->a);
  c->pc = FISHMAN_SPLASH_BEGIN_LIST_PC;
  return true;
}

// ---------------------------------------------------------------------------
// The sweep
// ---------------------------------------------------------------------------

bool fishman_sweep_supported(const Wram* w, uint16_t page) {
  const uint16_t at = wram_r16(w, (uint16_t)(page + FISHMAN_DP_BLOW_AT));
  const uint16_t blow = wram_r16(w, (uint16_t)(page + FISHMAN_DP_BLOW));
  if (at >= FISHMAN_SWEEP_RING_SIZE || (at & 3) != 0 || blow >= 0x1f00u)
    return false;
  if (wram_r16(w, (uint16_t)(page + FISHMAN_DP_SCRATCH)) != 1) return true;
  return blow >= W_ACTOR_SLOTS && blow <= ACTOR_SLOT_LAST &&
         place_in_list(w, blow) >= 0;
}

static void sweep_place(Wram* w, const Rom* rom, PortCpu* c, bool* wrapped);

bool fishman_sweep_begin_supported(const Wram* w, uint16_t page, uint16_t s) {
  const uint16_t way = wram_r16(w, (uint16_t)(page + FISHMAN_DP_WAY));
  return way >= FISHMAN_FIRST_WAY && way <= FISHMAN_LAST_WAY &&
         (way & 1) == 0 && loop_of(wram_r16(w, (uint16_t)(s + 1))) != NULL;
}

// `$81:E346`: a second record where it stands, with no picture, to hit
// with. Then the ring from the way it faces, for five ticks.
bool fishman_sweep_begin(Wram* w, const Rom* rom, PortCpu* c, uint16_t* record,
                         bool* wrapped) {
  const uint16_t page = c->d;
  SlotAllocRegs taken;
  actor_slot_alloc(w, c->db, &taken);
  if (taken.c) return false;
  PORT_COVER(fishman_sweep_began);
  const uint16_t blow = taken.a;
  *record = blow;
  wram_w16(w, (uint16_t)(page + FISHMAN_DP_BLOW), blow);
  wram_w16(w, (uint16_t)(blow + ACTOR_X),
           wram_r16(w, (uint16_t)(page + FISHMAN_DP_X)));
  wram_w16(w, (uint16_t)(blow + ACTOR_Y),
           wram_r16(w, (uint16_t)(page + FISHMAN_DP_Y)));
  wram_w16(w, (uint16_t)(blow + ACTOR_Z), 0);
  wram_w16(w, (uint16_t)(blow + ACTOR_META), 0);
  wram_w16(w, (uint16_t)(blow + ACTOR_META_BANK), 0);
  wram_w16(w, (uint16_t)(blow + ACTOR_THREAD), wram_r16(w, W_SCHED_CUR_TASK));
  wram_w16(w, (uint16_t)(blow + ACTOR_COLLIDE_ID), FISHMAN_LANDED_ID);
  wram_w16(w, (uint16_t)(blow + ACTOR_FLAGS),
           (uint16_t)(ACTOR_DRAW | wram_r16(w, (uint16_t)(blow + ACTOR_FLAGS))));

  wram_w16(w, (uint16_t)(page + FISHMAN_DP_SCRATCH), FISHMAN_SWEEP_TICKS);
  const uint16_t way = wram_r16(w, (uint16_t)(page + FISHMAN_DP_WAY));
  wram_w16(w, (uint16_t)(page + FISHMAN_DP_BLOW_AT),
           (uint16_t)((way - FISHMAN_FIRST_WAY) << 1));
  sweep_place(w, rom, c, wrapped);
  return true;
}

bool fishman_sweep_tick(Wram* w, const Rom* rom, PortCpu* c, bool* wrapped,
                        int* place) {
  const uint16_t page = c->d;
  const uint16_t blow = wram_r16(w, (uint16_t)(page + FISHMAN_DP_BLOW));
  const uint16_t left =
      (uint16_t)(wram_r16(w, (uint16_t)(page + FISHMAN_DP_SCRATCH)) - 1);
  wram_w16(w, (uint16_t)(page + FISHMAN_DP_SCRATCH), left);
  if (left == 0) {
    // Its second record goes, and it shows three pictures more.
    PORT_COVER(fishman_sweep_over);
    *place = place_in_list(w, blow);
    SlotFreeRegs freed;
    actor_slot_free(w, blow, page, c->x, c->y, &freed);
    c->x = freed.x;
    c->y = freed.y;
    set_c(c, freed.c);
    wram_w16(w, (uint16_t)(page + FISHMAN_DP_BLOW), FISHMAN_NO_BLOW);
    c->a = FISHMAN_SWEPT_PICTURES;
    set_nz16(c, c->a);
    c->pc = FISHMAN_SWEEP_DONE_PC;
    return false;
  }

  PORT_COVER(fishman_swept);
  sweep_place(w, rom, c, wrapped);
  return true;
}

// `$81:E355`: the record at the next place in the ring, and a tick's sleep.
static void sweep_place(Wram* w, const Rom* rom, PortCpu* c, bool* wrapped) {
  const uint16_t page = c->d;
  const uint16_t blow = wram_r16(w, (uint16_t)(page + FISHMAN_DP_BLOW));
  const uint16_t at = wram_r16(w, (uint16_t)(page + FISHMAN_DP_BLOW_AT));
  const uint32_t ring = ((uint32_t)FISHMAN_BANK << 16) | FISHMAN_SWEEP_RING;
  set_c(c, false);
  wram_w16(w, (uint16_t)(blow + ACTOR_X),
           adc16(c, rom_word(rom, ring + at),
                 wram_r16(w, (uint16_t)(page + FISHMAN_DP_X))));
  set_c(c, false);
  wram_w16(w, (uint16_t)(blow + ACTOR_Y),
           adc16(c, rom_word(rom, ring + 2 + at),
                 wram_r16(w, (uint16_t)(page + FISHMAN_DP_Y))));
  set_c(c, false);
  uint16_t next = adc16(c, at, 4);
  *wrapped = next >= FISHMAN_SWEEP_RING_SIZE;
  set_c(c, *wrapped);
  if (*wrapped) next = 0;
  wram_w16(w, (uint16_t)(page + FISHMAN_DP_BLOW_AT), next);

  c->x = at;
  c->y = blow;
  c->a = 1;
  set_nz16(c, c->a);
  c->pc = FISHMAN_SWEEP_YIELD_PC;
}
