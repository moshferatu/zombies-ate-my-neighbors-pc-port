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
  WalkKind kind;
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

// The monster's ground. Two kinds of wall break under it, which is not ours.
static bool ground_stops_monster(Walk* k, Point p) {
  TerrainRegs r;
  terrain_blocked_enemy(k->w, p.x, p.y, &r);
  overflow_left(k, r.v);
  if (k->log && k->log->enemy_grounds < WALK_AXES)
    k->log->enemy_ground[k->log->enemy_grounds++] = r;
  if (!answer(k, WALK_ASK_GROUND, r.blocked)) return false;
  const uint16_t wall = r.a & MONSTER_WALL_BITS;
  if (wall == MONSTER_WALL_BREAKS_A || wall == MONSTER_WALL_BREAKS_B) {
    PORT_COVER(monster_walk_broke_wall);
    k->met_reaction = true;
    return true;
  }
  answer(k, WALK_ASK_REACTION, true);
  // The carry this leaves is the compare with the second kind's.
  if (k->log) k->log->last_yes = wall >= MONSTER_WALL_BREAKS_B;
  return true;
}

static bool ground_is_solid(Walk* k, Point p) {
  if (k->kind == WALK_OF_MONSTER) return ground_stops_monster(k, p);
  TerrainRegs r;
  terrain_blocked(k->w, p.x, p.y, &r);
  overflow_left(k, r.v);
  if (k->log) {
    for (int i = 0; i < r.probes; i++) k->log->probes[i]++;
    if (r.blocked && r.probes < TERRAIN_PROBE_COUNT) k->log->ground_cut_short++;
  }
  if (!answer(k, WALK_ASK_GROUND, r.blocked)) return false;
  if (k->kind == WALK_STUCK) {
    // The word comes back shifted right one, and the ROM shifts it back.
    const bool crosses = ((r.a << 1) & STUCK_WALK_CROSSES) != 0;
    PORT_COVER_IF(crosses, stuck_walk_crossed, stuck_walk_solid);
    answer(k, WALK_ASK_REACTION, !crosses);
    // Stopped here, the carry left is the bit that shift pushed out.
    if (!crosses && k->log) k->log->last_yes = (r.a & 0x8000u) != 0;
    return !crosses;
  }
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
  if (k->log && k->log->tethers < WALK_AXES)
    k->log->tether[k->log->tethers++] = r;
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
  if (k->log) k->log->map_exits[r.exit]++;
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
    if (k->kind != WALK_ORDINARY) {
      PORT_COVER_IF(k->kind == WALK_STUCK, stuck_walk_obstructed,
                    monster_walk_obstructed);
      return false;
    }
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
  if (k->log) k->log->doubled = proposed.x != 0;

  // Across first, at the height the player stands at now.
  Point here = position(k);
  Point across = {field(k, WALK_DP_WANT_X), here.y};
  if (can_step(k, here, across)) {
    set_field(k, WALK_DP_X, across.x);
    if (k->kind == WALK_STUCK)
      wram_w16(k->w, (uint16_t)(field(k, STUCK_WALK_DP_COVER) + ACTOR_X),
               across.x);
    if (k->log) {
      k->log->taken++;
      k->log->took[0] = true;
    }
  }

  // Then up or down, from wherever that left the player.
  here = position(k);
  Point up_down = {here.x, field(k, WALK_DP_WANT_Y)};
  if (can_step(k, here, up_down)) {
    set_field(k, WALK_DP_Y, up_down.y);
    if (k->kind == WALK_STUCK)
      wram_w16(k->w, (uint16_t)(field(k, STUCK_WALK_DP_COVER) + ACTOR_Y),
               (uint16_t)(up_down.y + 1));
    if (k->log) {
      k->log->taken++;
      k->log->took[1] = true;
    }
  }
}

void player_walk(Wram* w, const Rom* rom, uint16_t page, WalkLog* log) {
  Walk k = {w, rom, page, false, log, WALK_ORDINARY};
  walk(&k);
}

void stuck_walk(Wram* w, const Rom* rom, uint16_t page, WalkLog* log) {
  Walk k = {w, rom, page, false, log, WALK_STUCK};
  PORT_COVER(stuck_walked);
  walk(&k);
}

bool monster_walk_checked(Wram* w, const Rom* rom, uint16_t page,
                          WalkLog* log) {
  Walk k = {w, rom, page, false, log, WALK_OF_MONSTER};
  PORT_COVER(monster_walked);
  walk(&k);
  return !k.met_reaction;
}

bool player_walk_checked(Wram* w, const Rom* rom, uint16_t page, WalkLog* log) {
  Walk k = {w, rom, page, false, log, WALK_ORDINARY};
  if (field(&k, WALK_DP_MODE) & WALK_TWICE) {
    PORT_COVER(walk_twice);
    return false;
  }
  walk(&k);
  return !k.met_reaction;
}

bool walk_supported(Wram* w, const Rom* rom, uint16_t page) {
  return player_walk_checked(w, rom, page, NULL);
}
