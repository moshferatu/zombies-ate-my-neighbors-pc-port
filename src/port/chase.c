// $81:BEE3  monster_chase -- see port/chase.h.

#include "port/chase.h"

#include <stddef.h>

#include "port/coverage.h"
#include "port/monster.h"
#include "port/rng.h"
#include "port/terrain.h"

typedef struct {
  uint16_t x, y;
} Point;

// Directions, as the game numbers them. See the header.
enum { DIR_UP = 1, DIR_RIGHT = 3, DIR_DOWN = 5, DIR_LEFT = 7 };

// One monster's chase this frame.
typedef struct {
  Wram* w;
  const Rom* rom;
  uint16_t page;     // the monster's direct page
  uint16_t record;   // ...and its display record
  bool leapt;        // found something to leap, which is the ROM's to do
  ChaseLog* log;     // may be NULL
} Chase;

static uint16_t field(const Chase* k, uint16_t at) {
  return wram_r16(k->w, (uint16_t)(k->page + at));
}

static void set_field(Chase* k, uint16_t at, uint16_t v) {
  wram_w16(k->w, (uint16_t)(k->page + at), v);
}

static Point where_is(const Chase* k, uint16_t record) {
  return (Point){wram_r16(k->w, (uint16_t)(record + ACTOR_X)),
                 wram_r16(k->w, (uint16_t)(record + ACTOR_Y))};
}

// Where the monster is, as its page keeps it, and moving it there and on its
// record together.
static Point position(const Chase* k) {
  return (Point){field(k, MONSTER_DP_X), field(k, MONSTER_DP_Y)};
}

static void move_to(Chase* k, Point p) {
  set_field(k, MONSTER_DP_X, p.x);
  set_field(k, MONSTER_DP_Y, p.y);
  wram_w16(k->w, (uint16_t)(k->record + ACTOR_X), p.x);
  wram_w16(k->w, (uint16_t)(k->record + ACTOR_Y), p.y);
}

// A table of points in the chase's bank, one per direction.
static Point table_point(const Chase* k, uint16_t table, uint16_t dir) {
  const uint32_t at = ((uint32_t)CHASE_BANK << 16) + table + dir * 4u;
  return (Point){rom_word(k->rom, at), rom_word(k->rom, at + 2)};
}

static Point offset(Point p, Point by) {
  return (Point){(uint16_t)(p.x + by.x), (uint16_t)(p.y + by.y)};
}

static bool negative(uint16_t v) { return (v & 0x8000u) != 0; }

static uint16_t magnitude(uint16_t v) {
  return negative(v) ? (uint16_t)-v : v;
}

// ---------------------------------------------------------------------------
// What it asks the rest of the game
// ---------------------------------------------------------------------------

static uint16_t nearest_actor(Chase* k, Point from, uint16_t* dist) {
  ActorNearestWork work;
  const uint16_t found = actor_nearest_counted(k->w, from.x, from.y, dist, &work);
  if (k->log) k->log->nearest = work;
  return found;
}

static uint16_t random_byte(Chase* k, bool carry_in) {
  RngResult r;
  rng_next(k->w, carry_in, &r);
  if (k->log) k->log->overflow = r.v;
  return r.a;
}

static bool ground_is_solid(Chase* k, Point p) {
  TerrainRegs r;
  terrain_blocked_enemy(k->w, p.x, p.y, &r);
  if (k->log && k->log->grounds < CHASE_MAX_GROUNDS)
    k->log->ground_tiles[k->log->grounds++] = r.probes;
  return r.blocked;
}

static bool someone_at(Chase* k, Point p) {
  AtPointRegs r;
  AtPointWork work;
  actor_at_point_counted(k->w, k->record, p.x, p.y, &r, &work);
  if (k->log) {
    k->log->asked_actors = true;
    k->log->at_point = work;
  }
  return r.found;
}

// Does the tile at `p` have bit 13? The point is left in `$0E`/`$10`.
static bool leapable(Chase* k, Point p) {
  set_field(k, CHASE_DP_NEXT_X, p.x);
  set_field(k, CHASE_DP_NEXT_Y, p.y);
  if (k->log) k->log->leap_probes++;
  TileAttrsRegs r;
  tile_attrs_at_pixel(k->w, p.x, p.y, &r);
  return (r.a & CHASE_LEAPABLE) != 0;
}

// ---------------------------------------------------------------------------
// The chase
// ---------------------------------------------------------------------------

// Nothing in range: wander off in a random straight line. The carry the
// generator is handed is the range test's, which is set.
static void give_up(Chase* k) {
  PORT_COVER(chase_gave_up);
  if (k->log) k->log->gave_up = true;
  const uint16_t dir = (uint16_t)((random_byte(k, true) & 3) * 2 + DIR_UP);
  set_field(k, MONSTER_DP_FACING, (uint16_t)(dir * 2));
  set_field(k, MONSTER_DP_STATE, MONSTER_STATE_WANDER);
}

// The target is on a diagonal: close the smaller gap first. The halved gaps
// are what the ROM compares, and it leaves them in `$0E`/`$10`.
static uint16_t line_up(Chase* k, Point me, Point target) {
  const uint16_t gap_x = (uint16_t)(target.x - me.x);
  const uint16_t gap_y = (uint16_t)(target.y - me.y);
  set_field(k, CHASE_DP_GAP_X, gap_x);
  set_field(k, CHASE_DP_GAP_Y, gap_y);
  const uint16_t half_x = magnitude(gap_x) >> 1;
  const uint16_t half_y = magnitude(gap_y) >> 1;
  set_field(k, CHASE_DP_NEXT_X, half_x);
  set_field(k, CHASE_DP_NEXT_Y, half_y);

  const bool across = half_y >= half_x;
  if (k->log) {
    k->log->gap_x_negative = negative(gap_x);
    k->log->gap_y_negative = negative(gap_y);
    k->log->across = across;
  }
  if (across) return negative(gap_x) ? DIR_LEFT : DIR_RIGHT;
  return negative(gap_y) ? DIR_UP : DIR_DOWN;
}

// May the monster stand at `p`? `$32` says which test refused: 0 for the
// ground, 1 for an actor.
static bool can_stand_at(Chase* k, Point p) {
  set_field(k, CHASE_DP_BLOCKER, 0);
  if (ground_is_solid(k, p)) return false;
  set_field(k, CHASE_DP_BLOCKER, 1);
  return !someone_at(k, p);
}

// Solid ground ahead. Is there something to leap, and clear ground past it?
static bool can_leap(Chase* k, Point me, uint16_t dir) {
  const Point probe = table_point(k, CHASE_LEAP_PROBE, dir);
  Point tile = offset(me, probe);
  if (!leapable(k, tile)) {
    tile = offset(me, (Point){(uint16_t)(probe.x << 1), (uint16_t)(probe.y << 1)});
    if (!leapable(k, tile)) return false;
  }
  if (k->log) k->log->leap_found = true;
  const Point landing = offset(tile, table_point(k, CHASE_LEAP_LANDING, dir));
  return !ground_is_solid(k, landing);
}

static ChaseOutcome step(Chase* k, Point me, uint16_t dir) {
  const Point to = offset(me, table_point(k, field(k, CHASE_DP_STEPS), dir));
  set_field(k, CHASE_DP_NEXT_X, to.x);
  set_field(k, CHASE_DP_NEXT_Y, to.y);
  if (can_stand_at(k, to)) {
    PORT_COVER(chase_stepped);
    move_to(k, to);
    return CHASE_STEPPED;
  }
  if (field(k, CHASE_DP_BLOCKER) != 0) {
    PORT_COVER(chase_met_someone);
    return CHASE_MET_SOMEONE;
  }
  if (can_leap(k, me, dir)) {
    // Not ours. `chase_supported` turns the frame down, so a chase that gets
    // here is only finding that out.
    PORT_COVER(chase_leapt);
    k->leapt = true;
    return CHASE_LEAPT;
  }
  PORT_COVER(chase_met_ground);
  return CHASE_MET_GROUND;
}

static void chase(Chase* k) {
  uint16_t dist;
  const uint16_t target = nearest_actor(k, position(k), &dist);
  if (dist >= MONSTER_SEEK_NEAR) {
    give_up(k);
    return;
  }
  set_field(k, MONSTER_DP_TARGET, target);

  // Within a pixel of lining up with the target, the monster snaps into
  // line. Then it asks which way the target is, from wherever that left it.
  ActorSnapRegs snapped;
  actor_snap_to(k->w, k->record, target, &snapped);
  ActorBearingRegs bearing;
  actor_bearing(k->w, k->rom, k->record, target, &bearing);
  const Point me = where_is(k, k->record);
  set_field(k, MONSTER_DP_X, me.x);
  set_field(k, MONSTER_DP_Y, me.y);

  uint16_t dir = bearing.a;
  const bool straight = (dir & 1) != 0;
  if (k->log) k->log->straight = straight;
  if (straight) {
    PORT_COVER(chase_straight);
  } else {
    PORT_COVER(chase_diagonal);
    dir = line_up(k, me, where_is(k, target));
  }
  set_field(k, MONSTER_DP_FACING, (uint16_t)(dir * 2));
  set_field(k, CHASE_DP_STEP_INDEX, (uint16_t)(dir * 4));

  // Two pixels on the frames the generator's bit 1 is set. The carry it is
  // handed is the doubling's, which is clear.
  const bool fast = (random_byte(k, false) & 2) != 0;
  if (k->log) k->log->fast = fast;
  if (fast) set_field(k, CHASE_DP_STEPS, CHASE_FAST_STEPS);

  const ChaseOutcome outcome = step(k, me, dir);
  if (k->log) k->log->outcome = outcome;
  if (outcome != CHASE_LEAPT)
    set_field(k, CHASE_DP_STEPS, field(k, CHASE_DP_USUAL_STEPS));
}

void monster_chase(Wram* w, const Rom* rom, uint16_t page, ChaseLog* log) {
  Chase k = {w, rom, page, wram_r16(w, (uint16_t)(page + MONSTER_DP_RECORD)),
             false, log};
  chase(&k);
}

bool chase_supported(Wram* w, const Rom* rom, uint16_t page) {
  Chase k = {w, rom, page, wram_r16(w, (uint16_t)(page + MONSTER_DP_RECORD)),
             false, NULL};
  chase(&k);
  return !k.leapt;
}
