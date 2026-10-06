// $81:D2A5  the weeds' frame -- see port/weeds.h.

#include "port/weeds.h"

#include "port/coverage.h"
#include "port/rng.h"

// How near a player has to be to its root for it to stop growing, and to a
// step for the step to be no good.
#define WEED_GUARD_WITHIN 0x0048
#define WEED_STEP_CLEAR_OF 0x0018
// The bit of a tile's attributes that says a weed can take it.
#define WEED_GROUND 0x0080u
// A good step plants a tile on a draw of this or more, and a clump under it.
#define WEED_PLANT_ODDS 0x87
#define WEED_TILE_FIRST 0x01fcu  // four small tiles, by a draw
#define WEED_TILE_DRAW 0x0003
// The kind that may mark the spot, how far from the root, and on what draw.
#define WEED_KIND_MARKS 0x1400u
#define WEED_MARK_BEYOND 0x0020
#define WEED_MARK_ODDS 0x5f
// A clump needs this much more of the level beyond the step.
#define WEED_CLUMP_MARGIN 8
// On its guard, it snaps on a draw under this.
#define WEED_SNAP_ODDS 0x19

// `$81:CFC4`: for each arm, the four ways it may step, as offsets into the
// steps. `$81:CFE4`: a step across and down for each of the eight ways.
#define WEED_ARM_WAYS 0xcfc4u
#define WEED_STEPS 0xcfe4u
#define WEED_WAY_DRAW 0x0006
// `$81:D107`: a clump's four tiles, last first: a step in tiles from the one
// before, and the tile.
#define WEED_CLUMP 0xd107u
#define WEED_CLUMP_FIRST 0x0018
#define WEED_CLUMP_ENTRY 8

typedef struct {
  Wram* w;
  const Rom* rom;
  uint16_t page;
  WeedWork* k;
  bool carry;
} Weed;

static uint16_t field(const Weed* m, uint16_t at) {
  return wram_r16(m->w, (uint16_t)(m->page + at));
}

static void set_field(Weed* m, uint16_t at, uint16_t v) {
  wram_w16(m->w, (uint16_t)(m->page + at), v);
}

// A word of a table in the thread's bank. The index is the ROM's own, and
// one past `$FFFF` carries into the next bank as the 65816's does: the
// arms past the fourth read their steps from there.
static uint16_t table_word(const Weed* m, uint16_t table, uint16_t index) {
  return rom_word(m->rom, ((uint32_t)WEED_BANK << 16) + table + index);
}

// A draw takes the carry before it, and leaves its own.
static uint16_t draw(Weed* m, bool* overflowed) {
  RngResult r;
  rng_next(m->w, m->carry, &r);
  m->carry = r.c;
  *overflowed = r.v;
  return r.a;
}

static bool player_within(Weed* m, uint16_t reach, uint16_t x, uint16_t y,
                          PlayerPickRegs* r) {
  player_in_range(m->w, reach, x, y, r);
  m->carry = r->c;
  return r->a != 0;
}

// `$81:CF9F`: a step from the arm's tip, one of its four ways by a draw.
static void pick_step(Weed* m, uint16_t arm) {
  const uint16_t which = draw(m, &m->k->pick_overflow) & WEED_WAY_DRAW;
  set_field(m, WEED_DP_SCRATCH, which);
  const uint16_t way =
      table_word(m, WEED_ARM_WAYS, (uint16_t)((arm << 1) + which));
  set_field(m, WEED_DP_TO_X, (uint16_t)(field(m, WEED_DP_FROM_X) +
                                        table_word(m, WEED_STEPS, way)));
  set_field(m, WEED_DP_TO_Y, (uint16_t)(field(m, WEED_DP_FROM_Y) +
                                        table_word(m, WEED_STEPS + 2, way)));
}

typedef enum {
  STEP_GOOD,
  STEP_BARE,   // ground a weed cannot take
  STEP_NO,     // a player near it, or off the level
} Step;

// `$81:CF4D`: may it grow where the step is?
static Step try_step(Weed* m) {
  WeedWork* k = m->k;
  const uint16_t x = field(m, WEED_DP_TO_X), y = field(m, WEED_DP_TO_Y);
  if (player_within(m, WEED_STEP_CLEAR_OF, x, y, &k->step_range)) {
    k->step_near = true;
    return STEP_NO;
  }
  TileAttrsRegs tile;
  tile_attrs_at_pixel(m->w, x, y, &tile);
  k->step_ground_asked = true;
  if (!(tile.a & WEED_GROUND)) {
    k->step_bare = true;
    return STEP_BARE;
  }
  BoundsRegs edge;
  terrain_out_of_bounds(m->w, x, y, &edge);
  k->step_edge_asked = true;
  k->step_edge = edge.exit;
  if (edge.c) {
    k->step_off = true;
    return STEP_NO;
  }
  m->carry = false;
  return STEP_GOOD;
}

static void move_tip(Weed* m, uint16_t arm) {
  set_field(m, (uint16_t)(WEED_DP_TIPS + arm), field(m, WEED_DP_TO_X));
  set_field(m, (uint16_t)(WEED_DP_TIPS + 2 + arm), field(m, WEED_DP_TO_Y));
}

// `$81:D008`: how far the step is from the root's record, across and down
// together.
static uint16_t gap_from_root(Weed* m) {
  const uint16_t record = field(m, WEED_DP_RECORD);
  uint16_t dx = (uint16_t)(wram_r16(m->w, (uint16_t)(record + ACTOR_X)) -
                           field(m, WEED_DP_TO_X));
  if (dx & 0x8000u) {
    m->k->gap_x_negative = true;
    dx = (uint16_t)(0u - dx);
  }
  set_field(m, WEED_DP_SCRATCH, dx);
  uint16_t dy = (uint16_t)(wram_r16(m->w, (uint16_t)(record + ACTOR_Y)) -
                           field(m, WEED_DP_TO_Y));
  if (dy & 0x8000u) {
    m->k->gap_y_negative = true;
    dy = (uint16_t)(0u - dy);
  }
  return (uint16_t)(dy + dx);
}

// `$81:D07C`: the spot marked, if it is the kind that marks, far enough out,
// drawn, and clear.
static void maybe_mark(Weed* m) {
  WeedWork* k = m->k;
  if (field(m, WEED_DP_TILE_BITS) != WEED_KIND_MARKS) return;
  k->kind_marks = true;
  if (gap_from_root(m) < WEED_MARK_BEYOND) {
    k->near_root = true;
    return;
  }
  m->carry = true;  // the compare's
  k->mark_drawn = true;
  if (draw(m, &k->mark_overflow) < WEED_MARK_ODDS) return;
  k->mark_wanted = true;
  terrain_blocked_enemy(m->w, field(m, WEED_DP_TO_X), field(m, WEED_DP_TO_Y),
                        &k->mark_ground);
  if (k->mark_ground.blocked) return;
  PORT_COVER(weed_marked);
  k->marked = true;
  set_field(m, WEED_DP_MARK_X, field(m, WEED_DP_TO_X));
  set_field(m, WEED_DP_MARK_Y, field(m, WEED_DP_TO_Y));
}

// `$81:CF79`: is there room for a clump?
static bool room_for_clump(Weed* m) {
  WeedWork* k = m->k;
  const uint16_t x = field(m, WEED_DP_TO_X), y = field(m, WEED_DP_TO_Y);
  if (player_within(m, WEED_STEP_CLEAR_OF, x, y, &k->room_range)) {
    k->room_near = true;
    return false;
  }
  BoundsRegs edge;
  terrain_out_of_bounds(m->w, (uint16_t)(x + WEED_CLUMP_MARGIN),
                        (uint16_t)(y + WEED_CLUMP_MARGIN), &edge);
  k->room_edge_asked = true;
  k->room_edge = edge.exit;
  k->room_off = edge.c;
  return !edge.c;
}

// `$81:D0AB`: four tiles round the step, each only where the ground can
// take one.
static void plant_clump(Weed* m) {
  WeedWork* k = m->k;
  PORT_COVER(weed_clump);
  k->clump = true;
  set_field(m, WEED_DP_TO_X, (uint16_t)(field(m, WEED_DP_TO_X) >> 3));
  set_field(m, WEED_DP_TO_Y, (uint16_t)(field(m, WEED_DP_TO_Y) >> 3));
  uint16_t at = WEED_CLUMP_FIRST;
  for (int i = 0; i < WEED_CLUMP_TILES; i++) {
    set_field(m, WEED_DP_CLUMP_AT, at);
    const uint16_t col = (uint16_t)(table_word(m, WEED_CLUMP, at) +
                                    field(m, WEED_DP_TO_X));
    set_field(m, WEED_DP_TO_X, col);
    const uint16_t row = (uint16_t)(table_word(m, WEED_CLUMP + 2, at) +
                                    field(m, WEED_DP_TO_Y));
    set_field(m, WEED_DP_TO_Y, row);
    TileAttrsRegs tile;
    tile_attrs_at_tile(m->w, col, row, &tile);
    if (tile.a & WEED_GROUND) {
      PORT_COVER(weed_clump_tile);
      k->clump_put[i] = true;
      map_tile_put(m->w, m->rom,
                   (uint16_t)(table_word(m, WEED_CLUMP + 4, at) |
                              field(m, WEED_DP_TILE_BITS)),
                   col, row, &k->clump_tile[i]);
    }
    at = (uint16_t)(at - WEED_CLUMP_ENTRY);
  }
  set_field(m, WEED_DP_CLUMP_AT, at);
}

// `$81:D028`: an arm's turn.
static void grow(Weed* m) {
  WeedWork* k = m->k;
  k->grew = true;
  const uint16_t arm = field(m, WEED_DP_ARM);
  set_field(m, WEED_DP_FROM_X, field(m, (uint16_t)(WEED_DP_TIPS + arm)));
  set_field(m, WEED_DP_FROM_Y, field(m, (uint16_t)(WEED_DP_TIPS + 2 + arm)));
  pick_step(m, arm);

  switch (try_step(m)) {
    case STEP_NO:
      PORT_COVER(weed_step_refused);
      break;
    case STEP_BARE:
      PORT_COVER(weed_tip_moved);
      k->tip_moved = true;
      move_tip(m, arm);
      break;
    case STEP_GOOD:
      if (draw(m, &k->plant_overflow) >= WEED_PLANT_ODDS) {
        PORT_COVER(weed_planted);
        k->planted = true;
        move_tip(m, arm);
        const uint16_t x = field(m, WEED_DP_TO_X), y = field(m, WEED_DP_TO_Y);
        m->carry = (y & 0x0004u) != 0;  // the last bit shifted out of it
        const uint16_t tile =
            (uint16_t)(((draw(m, &k->tile_overflow) & WEED_TILE_DRAW) +
                        WEED_TILE_FIRST) |
                       field(m, WEED_DP_TILE_BITS));
        map_tile_put(m->w, m->rom, tile, (uint16_t)(x >> 3),
                     (uint16_t)(y >> 3), &k->tile);
      } else {
        maybe_mark(m);
        if (room_for_clump(m)) plant_clump(m);
      }
      break;
  }

  // The next arm's turn. This sum is the last thing to write carry and
  // overflow.
  uint16_t next = (uint16_t)(arm + 4);
  m->carry = next >= WEED_ARM_END;
  if (next >= WEED_ARM_END) {
    k->went_round = true;
    next = 0;
  }
  set_field(m, WEED_DP_ARM, next);
  k->v = false;
  k->v_known = true;
}

bool weed_frame_supported(const Wram* w, uint16_t page) {
  const uint16_t state = wram_r16(w, (uint16_t)(page + WEED_DP_STATE));
  const uint16_t arm = wram_r16(w, (uint16_t)(page + WEED_DP_ARM));
  if (arm >= WEED_ARM_END || (arm & 3) != 0) return false;
  return state == WEED_STATE_GROW || state == WEED_STATE_GUARD ||
         state == WEED_STATE_REST;
}

bool weed_frame(Wram* w, const Rom* rom, uint16_t page, bool carry,
                bool overflow, WeedWork* k) {
  Weed m = {w, rom, page, k, carry};
  k->state = field(&m, WEED_DP_STATE);
  k->v = overflow;
  k->v_known = true;
  switch (k->state) {
    case WEED_STATE_GROW:
      if (player_within(&m, WEED_GUARD_WITHIN, field(&m, WEED_DP_ROOT_X),
                        field(&m, WEED_DP_ROOT_Y), &k->seek)) {
        PORT_COVER(weed_on_guard);
        k->player_near = true;
        k->v_known = false;  // the test's, which the port does not follow
        set_field(&m, WEED_DP_STATE, WEED_STATE_GUARD);
      } else {
        PORT_COVER(weed_grew);
        grow(&m);
      }
      break;
    case WEED_STATE_GUARD:
      if (draw(&m, &k->guard_overflow) < WEED_SNAP_ODDS) {
        k->declined = true;
        return true;
      }
      PORT_COVER(weed_stood_down);
      m.carry = true;  // the compare's
      k->v = k->guard_overflow;
      set_field(&m, WEED_DP_STATE, WEED_STATE_GROW);
      break;
    default: {
      const uint16_t left = (uint16_t)(field(&m, WEED_DP_REST_LEFT) - 1);
      set_field(&m, WEED_DP_REST_LEFT, left);
      if (left & 0x8000u) {
        PORT_COVER(weed_rested);
        k->rest_over = true;
        set_field(&m, WEED_DP_STATE, WEED_STATE_GROW);
      } else {
        PORT_COVER(weed_resting);
      }
      break;
    }
  }

  k->c = m.carry;
  if (field(&m, WEED_DP_FATE) != 0) return false;
  set_field(&m, WEED_DP_CLEARED, 0);
  return true;
}

void weed_seed_frame(Wram* w, PortCpu* c, WeedSeedWork* k) {
  line_step(w, c, &k->line);

  // `$81:D3EB`: up by the rise, to the even number below it, and the rise
  // one less.
  const uint16_t page = c->d;
  const uint16_t record = wram_r16(w, (uint16_t)(page + LINE_DP_RECORD));
  const uint16_t rise = wram_r16(w, (uint16_t)(page + WEED_SEED_DP_RISE));
  k->falling = (rise & 0x8000u) != 0;
  set_c(c, false);
  const uint16_t height =
      adc16(c, (uint16_t)(rise & 0xfffeu),
            wram_r16(w, (uint16_t)(record + ACTOR_Z)));
  wram_w16(w, (uint16_t)(record + ACTOR_Z), height);
  wram_w16(w, (uint16_t)(page + WEED_SEED_DP_RISE), (uint16_t)(rise - 1));

  c->y = record;
  c->a = height;
  set_nz16(c, c->a);
  if (height & 0x8000u) {
    PORT_COVER(weed_seed_landed);
    k->landed = true;
    c->pc = WEED_SEED_LANDED_PC;
    return;
  }
  PORT_COVER(weed_seed_flew);
  c->a = WEED_SEED_TICKS;
  set_nz16(c, c->a);
  c->pc = WEED_SEED_SLEEP_PC;
}
