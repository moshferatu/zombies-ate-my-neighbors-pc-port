// $80:E4BA  player_walk -- see port/walk.h.

#include "port/walk.h"

#include <stddef.h>

#include "port/coverage.h"
#include "port/cpu.h"
#include "port/step.h"
#include "port/terrain.h"
#include "port/thread.h"

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

// A tile's lookup adds the column to its row's address, and leaves that
// add's overflow. Every lookup the walk makes shifts the point the same way.
static bool lookup_overflows(const Walk* k, uint16_t x, uint16_t y) {
  const uint16_t col = (uint16_t)((x >> 2) & 0xfffeu);
  const uint16_t row = (uint16_t)((y >> 2) & 0xfffeu);
  return add16_overflows(col, wram_r16(k->w, (uint32_t)(W_TILE_ROW_BASE + row)));
}

static WalkReaction* reaction_met(Walk* k, int kind) {
  static WalkReaction unlogged;
  WalkReaction* did = &unlogged;
  if (k->log && k->log->reactions < WALK_AXES)
    did = &k->log->reaction[k->log->reactions++];
  *did = (WalkReaction){.kind = (uint8_t)kind, .slot = -1};
  return did;
}

// `$80:E8D3`: is the player at a door, and with something to open it? A
// door they cannot open begins a thread. One they can is the ROM's: false.
static bool door_stays_shut(Walk* k) {
  WalkReaction* did = reaction_met(k, WALK_REACTION_DOOR);
  const uint16_t reach =
      rom_word(k->rom, WALK_DOOR_REACH + field(k, WALK_DP_FACING));
  if (reach == 0) {
    PORT_COVER(walk_door_no_reach);
    return true;
  }
  const uint16_t x = field(k, WALK_DP_X);
  const uint16_t y = (uint16_t)(field(k, WALK_DP_Y) + reach);
  set_field(k, WALK_DP_LOOK_Y, y);
  did->reached = true;
  TileAttrsRegs tile;
  tile_attrs_at_pixel(k->w, x, y, &tile);
  overflow_left(k, lookup_overflows(k, x, y));
  if ((tile.a & WALK_DOOR_BITS) != WALK_DOOR) {
    PORT_COVER(walk_door_not_there);
    return true;
  }
  // The items are in low WRAM. Anywhere else is not ours to read.
  const uint16_t items = field(k, WALK_DP_ITEMS);
  if (items >= 0x2000 || wram_r16(k->w, items) != 0) {
    PORT_COVER(walk_door_opened);
    return false;
  }
  PORT_COVER(walk_door_locked);
  did->locked = true;
  did->slot = thread_spawn(k->w, k->rom, WALK_DOOR_THREAD,
                           WALK_DOOR_THREAD_BANK, k->page);
  return true;
}

// `$82:F4FF`: the square of 64 pixels the player is in, looked for among
// five. A new one takes a free place, and a thread is begun for it.
static bool square_entered(Walk* k) {
  const uint16_t record = field(k, WALK_DP_RECORD);
  if (record >= 0x1f00) return false;
  WalkReaction* did = reaction_met(k, WALK_REACTION_SQUARE);
  const uint16_t x = wram_r16(k->w, (uint16_t)(record + ACTOR_X));
  const uint16_t y = wram_r16(k->w, (uint16_t)(record + ACTOR_Y));
  const uint16_t key = (uint16_t)(((x & 0xffc0u) << 2) | (y >> 6));
  wram_w16(k->w, W_WALK_SQUARE_X, x);
  wram_w16(k->w, W_WALK_SQUARE_Y, y);
  wram_w16(k->w, W_WALK_SQUARE_KEY, key);
  did->end = WALK_SQUARE_FULL;
  for (int at = WALK_SQUARES_LAST; at >= 0; at -= 2) {
    const uint16_t square = wram_r16(k->w, (uint32_t)(W_WALK_SQUARES + at));
    did->looked++;
    if (square & 0x8000u) {
      PORT_COVER(walk_square_begun);
      wram_w16(k->w, (uint32_t)(W_WALK_SQUARES + at), key);
      set_field(k, 0x00, x);
      set_field(k, 0x02, y);
      set_field(k, 0x04, (uint16_t)at);
      did->slot = thread_spawn(k->w, k->rom, WALK_SQUARE_THREAD,
                               WALK_SQUARE_THREAD_BANK, k->page);
      did->end = WALK_SQUARE_BEGUN;
      return true;
    }
    if (square == key) {
      PORT_COVER(walk_square_known);
      did->end = WALK_SQUARE_KNOWN;
      return true;
    }
  }
  PORT_COVER(walk_square_full);
  return true;
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
  switch (walk_tile_reaction(r.a)) {
    case 0:
      return answer(k, WALK_ASK_REACTION, true);
    case WALK_REACTION_DOOR:
      if (door_stays_shut(k)) {
        if (k->log) k->log->last_yes = true;
        return true;
      }
      break;
    case WALK_REACTION_SQUARE:
      // The one reaction the step goes on through.
      if (!square_entered(k)) break;
      if (k->log) k->log->last_yes = false;
      return false;
    default:
      break;
  }
  // Not ours to handle. `walk_supported` turns the frame down, so a walk
  // that gets here is only finding that out.
  PORT_COVER(walk_reaction);
  k->met_reaction = true;
  return true;
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

// ---------------------------------------------------------------------------
// The swim
// ---------------------------------------------------------------------------

// `$80:E7FE`: where it is not water, is that the bank, and can the player
// stand on it? True ends the swim.
static bool reaches_the_bank(Walk* k) {
  WalkLog* log = k->log;
  const uint16_t player = field(k, SWIM_DP_PLAYER);
  const uint16_t health = wram_r16(k->w, (uint16_t)(W_SWIM_HEALTH + player));
  if (health == 0 || (health & 0x8000u)) {
    if (log) log->bank[health == 0 ? SWIM_BANK_DEAD : SWIM_BANK_LESS]++;
    return false;
  }
  const uint16_t state = field(k, SWIM_DP_STATE);
  if (state != SWIM_STATE) {
    if (log) {
      log->bank[SWIM_BANK_NOT_SWIMMING]++;
      log->last_yes = state >= SWIM_STATE;
    }
    return false;
  }
  if (log) log->last_yes = true;
  const uint16_t facing = field(k, WALK_DP_FACING);
  if (facing == 0) {
    if (log) log->bank[SWIM_BANK_NO_WAY]++;
    return false;
  }
  const uint32_t step = SWIM_BANK_STEPS + (uint32_t)(facing << 1);
  const uint16_t y = (uint16_t)(rom_word(k->rom, step + 2) + field(k, WALK_DP_Y));
  const uint16_t x = (uint16_t)(rom_word(k->rom, step) + field(k, WALK_DP_X));
  set_field(k, WALK_DP_LOOK_X, x);
  set_field(k, WALK_DP_LOOK_Y, y);
  TerrainRegs r;
  terrain_blocked(k->w, x, y, &r);
  overflow_left(k, r.v);
  if (log) {
    for (int i = 0; i < r.probes; i++) log->probes[i]++;
    if (r.blocked && r.probes < TERRAIN_PROBE_COUNT) log->ground_cut_short++;
  }
  if (!r.blocked) {
    PORT_COVER(swim_reached_bank);
    return true;
  }
  PORT_COVER(swim_bank_solid);
  if (log) log->bank[SWIM_BANK_SOLID]++;
  return false;
}

static bool can_swim_to(Walk* k, Point to) {
  TerrainRegs r;
  terrain_point_bit8(k->w, to.x, to.y, &r);
  overflow_left(k, lookup_overflows(k, to.x, to.y));
  if (k->log) {
    k->log->water[r.blocked]++;
    k->log->last_yes = r.blocked;
  }
  if (!r.blocked) {
    PORT_COVER(swim_not_water);
    if (reaches_the_bank(k)) k->met_reaction = true;
    return false;
  }
  if (too_far_from_partner(k, to)) {
    PORT_COVER(swim_tethered);
    return false;
  }
  if (off_the_map(k, to)) {
    PORT_COVER(swim_off_map);
    return false;
  }
  return true;
}

static void swim(Walk* k) {
  StepProposeRegs proposed;
  step_propose(k->w, k->rom, k->page, &proposed);
  overflow_left(k, proposed.v);
  if (k->log) k->log->doubled = proposed.x != 0;

  Point here = position(k);
  if (can_swim_to(k, (Point){field(k, WALK_DP_WANT_X), here.y})) {
    set_field(k, WALK_DP_X, field(k, WALK_DP_WANT_X));
    if (k->log) k->log->taken++;
  }
  if (k->met_reaction) return;
  here = position(k);
  if (can_swim_to(k, (Point){here.x, field(k, WALK_DP_WANT_Y)})) {
    set_field(k, WALK_DP_Y, field(k, WALK_DP_WANT_Y));
    if (k->log) k->log->taken++;
  }
}

bool swim_walk_checked(Wram* w, const Rom* rom, uint16_t page, WalkLog* log) {
  Walk k = {w, rom, page, false, log, WALK_SWIM};
  PORT_COVER(swim_walked);
  swim(&k);
  return !k.met_reaction;
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
