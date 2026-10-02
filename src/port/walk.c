// $80:E4BA  player_walk -- see port/walk.h.

#include "port/walk.h"

#include <stddef.h>

#include "port/coverage.h"
#include "port/step.h"
#include "port/terrain.h"

typedef struct {
  uint16_t x, y;
} Point;

// One player's walk this frame.
typedef struct {
  Wram* w;
  const Rom* rom;
  uint16_t page;       // the player's direct page
  bool met_reaction;   // stopped at a tile with a reaction of its own
  WalkLog* log;        // may be NULL
} Walk;

static uint16_t field(const Walk* k, uint16_t at) {
  return wram_r16(k->w, (uint16_t)(k->page + at));
}

static void set_field(Walk* k, uint16_t at, uint16_t v) {
  wram_w16(k->w, (uint16_t)(k->page + at), v);
}

static Point position(const Walk* k) {
  return (Point){field(k, WALK_DP_X), field(k, WALK_DP_Y)};
}

// ---------------------------------------------------------------------------
// The four questions
// ---------------------------------------------------------------------------

// The tests that add or subtract leave overflow behind, and the log keeps the
// last one's.
static void overflow_left(Walk* k, bool v) {
  if (k->log) k->log->overflow = v;
}

// Every answer goes through here, so the log sees each one.
static bool answer(Walk* k, WalkQuestion q, bool yes) {
  if (k->log) {
    k->log->asked[q][yes]++;
    k->log->last_yes = yes;
  }
  return yes;
}

static const uint16_t REACTION_VALUE[WALK_REACTIONS] = {
    0x0100, 0x0200, 0x0010, 0x0020, 0x0800, 0x8008};

int walk_tile_reaction(uint16_t attrs) {
  const uint16_t bits = (uint16_t)((attrs << 1) & WALK_REACTION_BITS);
  for (int i = 0; i < WALK_REACTIONS; i++)
    if (bits == REACTION_VALUE[i]) return i + 1;
  return 0;
}

static bool ground_is_solid(Walk* k, Point p) {
  TerrainRegs r;
  terrain_blocked(k->w, p.x, p.y, &r);
  overflow_left(k, r.v);
  if (!answer(k, WALK_ASK_GROUND, r.blocked)) return false;
  if (walk_tile_reaction(r.a) != 0) {
    // Not ours to handle. `walk_supported` turns the frame down, so a walk
    // that gets here is only finding that out.
    PORT_COVER(walk_reaction);
    k->met_reaction = true;
    return true;
  }
  return answer(k, WALK_ASK_REACTION, true);
}

static bool too_far_from_partner(Walk* k, Point p) {
  TetherRegs r;
  step_tether_blocked(k->w, p.x, p.y, &r);
  if (r.v_set) overflow_left(k, r.v);
  return answer(k, WALK_ASK_TETHER, r.blocked);
}

// Is anyone but the player standing at `p`?
static bool someone_at(Walk* k, Point p, WalkQuestion q) {
  ObstacleRegs r;
  ObstacleWork work = {0};
  actor_obstacle_at_point_counted(k->w, field(k, WALK_DP_RECORD), p.x, p.y, &r,
                                  &work);
  if (r.v_set) overflow_left(k, r.v);
  if (k->log)
    for (int i = 0; i < OBSTACLE_BLOCK_COUNT; i++)
      k->log->obstacle.blocks[i] += work.blocks[i];
  return answer(k, q, r.blocked);
}

static bool off_the_map(Walk* k, Point p) {
  BoundsRegs r;
  terrain_out_of_bounds(k->w, p.x, p.y, &r);
  return answer(k, WALK_ASK_MAP, r.c);
}

// ---------------------------------------------------------------------------
// The walk
// ---------------------------------------------------------------------------

// May the player step from `from` to `to`, which differ along one axis?
static bool can_step(Walk* k, Point from, Point to) {
  if (ground_is_solid(k, to)) {
    PORT_COVER(walk_solid);
    return false;
  }
  if (too_far_from_partner(k, to)) {
    PORT_COVER(walk_tethered);
    return false;
  }
  if (someone_at(k, to, WALK_ASK_THERE)) {
    // Standing clear, the player cannot walk into someone. Already standing
    // in someone, the player can walk out.
    if (!someone_at(k, from, WALK_ASK_HERE)) {
      PORT_COVER(walk_obstructed);
      return false;
    }
    PORT_COVER(walk_overlapping);
  }
  if (off_the_map(k, to)) {
    PORT_COVER(walk_off_map);
    return false;
  }
  return true;
}

static void walk(Walk* k) {
  StepProposeRegs proposed;
  step_propose(k->w, k->rom, k->page, &proposed);
  overflow_left(k, proposed.v);

  // Across first, at the height the player stands at now.
  Point here = position(k);
  Point across = {field(k, WALK_DP_WANT_X), here.y};
  if (can_step(k, here, across)) {
    set_field(k, WALK_DP_X, across.x);
    if (k->log) k->log->taken++;
  }

  // Then up or down, from wherever that left the player.
  here = position(k);
  Point up_down = {here.x, field(k, WALK_DP_WANT_Y)};
  if (can_step(k, here, up_down)) {
    set_field(k, WALK_DP_Y, up_down.y);
    if (k->log) k->log->taken++;
  }
}

void player_walk(Wram* w, const Rom* rom, uint16_t page, WalkLog* log) {
  Walk k = {w, rom, page, false, log};
  walk(&k);
}

bool walk_supported(Wram* w, const Rom* rom, uint16_t page) {
  Walk k = {w, rom, page, false, NULL};
  if (field(&k, WALK_DP_MODE) & WALK_TWICE) {
    PORT_COVER(walk_twice);
    return false;
  }
  walk(&k);
  return !k.met_reaction;
}
