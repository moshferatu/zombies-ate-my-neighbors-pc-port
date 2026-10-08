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

// The seed's record: how high it begins, its picture, and what it is to
// whatever it comes down on.
#define WEED_SEED_HEIGHT 0x0018
#define WEED_SEED_PICTURE 0xc3bdu
#define WEED_SEED_PICTURE_BANK 0x0090
#define WEED_SEED_AIR_ID 0x0036
#define WEED_SEED_ATTR 0x0400
#define WEED_SEED_FLAGS 0x8018u
// Where it comes down, from whoever it is thrown at.
#define WEED_SEED_SCATTER_MASK 0x003f
#define WEED_SEED_SCATTER_BACK 0xffe0u
#define WEED_SEED_LEAST_GAP 4
// Landed: the box it tells, its id to them, and its last pictures.
#define WEED_SEED_BOX_LEFT 0x0018
#define WEED_SEED_BOX_WIDTH 0x0030
#define WEED_SEED_BOX_UP 0x0010
#define WEED_SEED_BOX_HEIGHT 0x0020
#define WEED_SEED_TELLS 0x0003
#define WEED_SEED_LAST_PICTURES 0xd433u
#define WEED_SEED_BOX_RETURN 0xd4ebu  // what the `JSR` to the box pushes
#define W_NOTIFY_BOX 0x0038u          // left, right, top, bottom, and the id

static uint16_t seed_field(const Wram* w, const PortCpu* c, uint16_t at) {
  return wram_r16(w, (uint16_t)(c->d + at));
}

static void set_seed_field(Wram* w, const PortCpu* c, uint16_t at,
                           uint16_t v) {
  wram_w16(w, (uint16_t)(c->d + at), v);
}

// A draw of up to 32 either way from `middle`. The second sum takes the
// carry of the first.
static uint16_t scatter(Wram* w, PortCpu* c, uint16_t middle, bool* overflow) {
  RngResult r;
  rng_next(w, flag(c, PORT_P_C), &r);
  *overflow = r.v;
  set_c(c, false);
  const uint16_t off =
      adc16(c, (uint16_t)(r.a & WEED_SEED_SCATTER_MASK), WEED_SEED_SCATTER_BACK);
  return adc16(c, off, middle);
}

// One gap of its line: which way a step goes, and how much a pass adds.
static uint16_t seed_gap(Wram* w, PortCpu* c, uint16_t to, uint16_t from,
                         uint16_t step_at, uint16_t part_at, bool* is_negative,
                         bool* floored) {
  set_c(c, true);
  uint16_t gap = sbc16(c, to, from);
  *is_negative = (gap & 0x8000u) != 0;
  if (*is_negative) gap = (uint16_t)(0u - gap);
  set_seed_field(w, c, step_at, *is_negative ? 0xffffu : 1);
  set_seed_field(w, c, part_at, gap);
  *floored = gap < WEED_SEED_LEAST_GAP;
  set_c(c, !*floored);
  if (*floored) {
    gap = WEED_SEED_LEAST_GAP;
    set_seed_field(w, c, part_at, gap);
  }
  return gap;
}

void weed_seed_begin(Wram* w, const Rom* rom, PortCpu* c, WeedSeedBegin* k) {
  *k = (WeedSeedBegin){0};
  SlotAllocRegs taken;
  actor_slot_alloc(w, c->db, &taken);
  if (taken.c) {
    k->declined = true;
    return;
  }
  PORT_COVER(weed_seed_began);
  const uint16_t record = taken.a;
  k->record = record;
  set_c(c, false);

  // `$81:D482`: its record, in the air beside the root.
  const uint16_t x = seed_field(w, c, WEED_SEED_DP_PLACED_X);
  const uint16_t y = seed_field(w, c, WEED_SEED_DP_PLACED_Y);
  const uint16_t at = seed_field(w, c, WEED_SEED_DP_PLACED_AT);
  set_seed_field(w, c, LINE_DP_RECORD, record);
  set_seed_field(w, c, LINE_DP_X, x);
  set_seed_field(w, c, LINE_DP_Y, y);
  set_seed_field(w, c, WEED_SEED_DP_AT, at);
  wram_w16(w, (uint16_t)(record + ACTOR_X), x);
  wram_w16(w, (uint16_t)(record + ACTOR_Z), WEED_SEED_HEIGHT);
  wram_w16(w, (uint16_t)(record + ACTOR_Y), y);
  wram_w16(w, (uint16_t)(record + ACTOR_META), WEED_SEED_PICTURE);
  wram_w16(w, (uint16_t)(record + ACTOR_META_BANK), WEED_SEED_PICTURE_BANK);
  wram_w16(w, (uint16_t)(record + ACTOR_THREAD), wram_r16(w, W_SCHED_CUR_TASK));
  wram_w16(w, (uint16_t)(record + ACTOR_COLLIDE_ID), WEED_SEED_AIR_ID);
  wram_w16(w, (uint16_t)(record + ACTOR_ATTR), WEED_SEED_ATTR);
  wram_w16(w, (uint16_t)(record + ACTOR_FLAGS),
           (uint16_t)(WEED_SEED_FLAGS |
                      wram_r16(w, (uint16_t)(record + ACTOR_FLAGS))));

  // `$81:D36E`: where it is to come down, and the line there.
  const uint16_t to_x =
      scatter(w, c, wram_r16(w, (uint16_t)(at + ACTOR_X)), &k->draw_overflow[0]);
  set_seed_field(w, c, WEED_SEED_DP_TO_X, to_x);
  const uint16_t to_y =
      scatter(w, c, wram_r16(w, (uint16_t)(at + ACTOR_Y)), &k->draw_overflow[1]);
  set_seed_field(w, c, WEED_SEED_DP_TO_Y, to_y);

  ActorBearingRegs way;
  k->same_row = y == to_y;
  k->same_column = x == to_x;
  actor_bearing_point(w, rom, record, to_x, to_y, &way);
  set_seed_field(w, c, WEED_SEED_DP_WAY, way.a);

  const uint16_t across = seed_gap(w, c, to_x, x, LINE_DP_STEP_X,
                                   LINE_DP_PART_X, &k->negative[0],
                                   &k->floored[0]);
  uint16_t longer = seed_gap(w, c, to_y, y, LINE_DP_STEP_Y, LINE_DP_PART_Y,
                             &k->negative[1], &k->floored[1]);
  k->across_longer = longer < across;
  if (k->across_longer) longer = across;
  const uint16_t whole = (uint16_t)(longer >> 1);
  set_seed_field(w, c, LINE_DP_WHOLE, whole);
  set_seed_field(w, c, WEED_SEED_DP_RISE, (uint16_t)(whole >> 1));
  set_c(c, (whole & 1) != 0);
  set_seed_field(w, c, LINE_DP_SUM_X, 0);
  set_seed_field(w, c, LINE_DP_SUM_Y, 0);

  c->a = WEED_SEED_TICKS;
  set_nz16(c, c->a);
  c->pc = WEED_SEED_SLEEP_PC;
}

void weed_seed_landed(Wram* w, PortCpu* c) {
  PORT_COVER(weed_seed_came_down);
  const uint16_t record = seed_field(w, c, LINE_DP_RECORD);
  wram_w16(w, (uint16_t)(c->y + ACTOR_Z), 0);
  push16(w, c, WEED_SEED_BOX_RETURN);

  // `$81:D3FF`: everything in a box round where it was to come down is
  // told.
  set_c(c, true);
  const uint16_t left =
      sbc16(c, seed_field(w, c, WEED_SEED_DP_TO_X), WEED_SEED_BOX_LEFT);
  wram_w16(w, W_NOTIFY_BOX, left);
  set_c(c, false);
  wram_w16(w, W_NOTIFY_BOX + 2, adc16(c, left, WEED_SEED_BOX_WIDTH));
  set_c(c, true);
  const uint16_t top =
      sbc16(c, seed_field(w, c, WEED_SEED_DP_TO_Y), WEED_SEED_BOX_UP);
  wram_w16(w, W_NOTIFY_BOX + 4, top);
  set_c(c, false);
  wram_w16(w, W_NOTIFY_BOX + 6, adc16(c, top, WEED_SEED_BOX_HEIGHT));
  wram_w16(w, W_NOTIFY_BOX + 8, WEED_SEED_TELLS);

  c->y = record;
  c->a = WEED_SEED_TELLS;
  set_nz16(c, c->a);
  c->pc = WEED_SEED_TELL_PC;
}

void weed_seed_told(PortCpu* c) {
  PORT_COVER(weed_seed_told);
  c->a = WEED_SEED_LAST_PICTURES;
  set_nz16(c, c->a);
  c->pc = WEED_SEED_PLAY_PC;
}

bool weed_seed_end_supported(const Wram* w, uint16_t page, uint16_t s) {
  const uint16_t record = wram_r16(w, (uint16_t)(page + LINE_DP_RECORD));
  return record >= W_ACTOR_SLOTS && record <= ACTOR_SLOT_LAST &&
         actor_list_place(w, record) != -3 &&
         wram_r16(w, (uint16_t)(s + 1)) == WEED_SEED_BOX_RETURN &&
         wram_r16(w, (uint16_t)(s + 3)) == (WEED_SEED_EXITED_PC & 0xffffu) - 1 &&
         wram_r8(w, (uint16_t)(s + 5)) == (WEED_SEED_EXITED_PC >> 16);
}

void weed_seed_end(Wram* w, PortCpu* c, int* place) {
  PORT_COVER(weed_seed_ended);
  // Back from the box, and a jump to `actor_slot_free`: its `RTL` is the
  // thread's.
  const uint16_t record = seed_field(w, c, LINE_DP_RECORD);
  *place = actor_list_place(w, record);
  SlotFreeRegs r;
  actor_slot_free(w, record, c->d, c->x, c->y, &r);
  // The free keeps the page on the stack as it works, where the box's
  // return was.
  wram_w16(w, (uint16_t)(c->s + 1), c->d);
  c->a = r.a;
  c->x = r.x;
  c->y = r.y;
  c->p = (uint8_t)(c->p & ~(PORT_P_N | PORT_P_Z));
  if (r.n) c->p |= PORT_P_N;
  if (r.z) c->p |= PORT_P_Z;
  set_c(c, r.c);
  c->s = (uint16_t)(c->s + 5);
  c->pc = WEED_SEED_EXITED_PC;
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
