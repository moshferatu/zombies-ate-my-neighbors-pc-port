// The Snakeoid's thread -- see port/snakeoid.h.

#include "port/snakeoid.h"

#include "port/coverage.h"
#include "port/rng.h"

typedef struct {
  Wram* w;
  const Rom* rom;
  PortCpu* c;
  SnakeoidWork* k;
} Snakeoid;

// --- The machine, as these stretches use it ---------------------------------

static uint16_t field(const Snakeoid* s, uint16_t at) {
  return wram_r16(s->w, (uint16_t)(s->c->d + at));
}

static void set_field(Snakeoid* s, uint16_t at, uint16_t v) {
  wram_w16(s->w, (uint16_t)(s->c->d + at), v);
}

// A word read through the data bank, which is its own: a table of its own,
// a word of a record, or low memory.
static uint16_t data(const Snakeoid* s, uint16_t at) {
  return bus_r16(s->w, s->rom, (uint32_t)SNAKEOID_BANK << 16 | at);
}

// ...and one written, which is always to a record or to low memory.
static void set_data(Snakeoid* s, uint16_t at, uint16_t v) {
  wram_w16(s->w, at, v);
}

static void ran(Snakeoid* s, SnakeoidRun run) { s->k->runs[run]++; }

// A branch: counted if it is taken.
static bool took(Snakeoid* s, bool taken) {
  if (taken) s->k->taken++;
  return taken;
}

static bool negative(uint16_t v) { return (v & 0x8000u) != 0; }
static bool zero(const Snakeoid* s) { return flag(s->c, PORT_P_Z); }
static bool carry(const Snakeoid* s) { return flag(s->c, PORT_P_C); }

static void lda(Snakeoid* s, uint16_t v) {
  s->c->a = v;
  set_nz16(s->c, v);
}

static void ldx(Snakeoid* s, uint16_t v) {
  s->c->x = v;
  set_nz16(s->c, v);
}

static void ldy(Snakeoid* s, uint16_t v) {
  s->c->y = v;
  set_nz16(s->c, v);
}

// `ASL`, `LSR`, `INC` and `DEC`, of what is in A.
static void doubled(Snakeoid* s) { s->c->a = asl16(s->c, s->c->a); }

static void halved(Snakeoid* s) {
  set_c(s->c, (s->c->a & 1) != 0);
  lda(s, (uint16_t)(s->c->a >> 1));
}

static void one_more(Snakeoid* s) { lda(s, (uint16_t)(s->c->a + 1)); }
static void one_less(Snakeoid* s) { lda(s, (uint16_t)(s->c->a - 1)); }

// `EOR #$FFFF : INC`.
static void negated(Snakeoid* s) {
  lda(s, (uint16_t)~s->c->a);
  one_more(s);
}

// `CLC : ADC`, and `SEC : SBC`, to what is in A.
static void add(Snakeoid* s, uint16_t v) {
  set_c(s->c, false);
  s->k->v_known = true;
  s->c->a = adc16(s->c, s->c->a, v);
}

static void sub(Snakeoid* s, uint16_t v) {
  set_c(s->c, true);
  s->k->v_known = true;
  s->c->a = sbc16(s->c, s->c->a, v);
}

// `BIT #`: only whether A shares a bit with it.
static void shares_bit(Snakeoid* s, uint16_t bits) {
  PortCpu* c = s->c;
  c->p = (uint8_t)((c->a & bits) == 0 ? c->p | PORT_P_Z : c->p & ~PORT_P_Z);
}

// `DEC` and `INC` of a word of the direct page.
static uint16_t count_down(Snakeoid* s, uint16_t at) {
  const uint16_t v = (uint16_t)(field(s, at) - 1);
  set_field(s, at, v);
  set_nz16(s->c, v);
  return v;
}

static void count_up(Snakeoid* s, uint16_t at) {
  const uint16_t v = (uint16_t)(field(s, at) + 1);
  set_field(s, at, v);
  set_nz16(s->c, v);
}

static void pha(Snakeoid* s) { push16(s->w, s->c, s->c->a); }
static void phx(Snakeoid* s) { push16(s->w, s->c, s->c->x); }
static void phy(Snakeoid* s) { push16(s->w, s->c, s->c->y); }
static void pla(Snakeoid* s) { lda(s, pull16(s->w, s->c)); }
static void plx(Snakeoid* s) { ldx(s, pull16(s->w, s->c)); }
static void ply(Snakeoid* s) { ldy(s, pull16(s->w, s->c)); }

// A `JSR` or a `JSL` at `at`, and coming back from one.
static void jsr(Snakeoid* s, uint16_t at) {
  push16(s->w, s->c, (uint16_t)(at + 2));
}

static void rts(Snakeoid* s) {
  (void)pull16(s->w, s->c);
  ran(s, SNAKEOID_RUN_RTS);
}

// An `RTS` that is its run's last instruction.
static void back(Snakeoid* s) { (void)pull16(s->w, s->c); }

static void jsl(Snakeoid* s, uint16_t at) {
  push8(s->w, s->c, SNAKEOID_BANK);
  push16(s->w, s->c, (uint16_t)(at + 3));
}

static void rtl(Snakeoid* s) {
  (void)pull16(s->w, s->c);
  (void)pull8(s->w, s->c);
}

static void set_flags(Snakeoid* s, bool n, bool z, bool c) {
  PortCpu* cpu = s->c;
  cpu->p = (uint8_t)(cpu->p & ~(PORT_P_N | PORT_P_Z));
  if (n) cpu->p |= PORT_P_N;
  if (z) cpu->p |= PORT_P_Z;
  set_c(cpu, c);
}

// What a routine called with `PHD` leaves in N and Z when it pulls it.
static void set_flags_pld(Snakeoid* s, bool c) {
  set_flags(s, negative(s->c->d), s->c->d == 0, c);
}

// --- What it calls ----------------------------------------------------------

// `JSL rng_next` at `at`.
static void draw(Snakeoid* s, uint16_t at) {
  RngResult r;
  jsl(s, at);
  rng_next(s->w, carry(s), &r);
  rtl(s);
  s->k->draws[r.v]++;
  s->c->a = r.a;
  set_flags(s, r.n, r.z, r.c);
  set_v(s->c, r.v);
  s->k->v_known = true;
}

// `JSL terrain_footprint_bit12` at `at`: is the point in X and Y off the
// ground it keeps to?
static bool off_its_ground(Snakeoid* s, uint16_t at) {
  PortCpu* c = s->c;
  TerrainRegs r;
  jsl(s, at);
  terrain_footprint_bit12(s->w, c->x, c->y, &r);
  rtl(s);
  s->k->grounds[r.blocked][r.probes]++;
  c->a = r.a;
  c->x = r.x;
  c->y = r.y;
  set_flags_pld(s, r.blocked);
  set_v(c, r.v);
  s->k->v_known = true;
  return r.blocked;
}

// `JSL terrain_out_of_bounds` at `at`: is the point past the level's edge?
static bool off_the_level(Snakeoid* s, uint16_t at) {
  PortCpu* c = s->c;
  BoundsRegs r;
  jsl(s, at);
  terrain_out_of_bounds(s->w, c->x, c->y, &r);
  rtl(s);
  s->k->edges[r.exit]++;
  c->a = r.a;
  set_flags(s, r.n, r.z, r.c);
  return r.c;
}

// `JSL tile_attrs_at_tile` at `at`: what the tile at X across and Y down is.
static void reads_tile(Snakeoid* s, uint16_t at) {
  PortCpu* c = s->c;
  TileAttrsRegs r;
  jsl(s, at);
  tile_attrs_at_tile(s->w, c->x, c->y, &r);
  rtl(s);
  s->k->tiles_read++;
  s->k->v_known = false;
  c->a = r.a;
  // Its last pull is of the data bank.
  set_flags(s, (c->db & 0x80u) != 0, c->db == 0, r.c);
}

// `JSL map_tile_put` at `at`: that tile becomes the one in A.
static void puts_tile(Snakeoid* s, uint16_t at) {
  PortCpu* c = s->c;
  SnakeoidWork* k = s->k;
  if (k->churn_count == SNAKEOID_CHURNS) k->churn_count--;
  TilePutRegs* r = &k->churned[k->churn_count++];
  jsl(s, at);
  map_tile_put(s->w, s->rom, c->a, c->x, c->y, r);
  rtl(s);
  k->v_known = false;
  c->a = r->a;
  c->x = r->x;
  c->y = r->y;
  set_flags_pld(s, r->c);
}

// `JSL player_in_range` at `at`: the nearer player within A of the point in
// X and Y, in A, or 0. How far is left in X.
static void asks_players(Snakeoid* s, uint16_t at) {
  PortCpu* c = s->c;
  SnakeoidWork* k = s->k;
  if (k->player_count == SNAKEOID_MAX_PLAYER_ASKS) k->player_count--;
  PlayerPickRegs* r = &k->players[k->player_count++];
  jsl(s, at);
  player_in_range(s->w, c->a, c->x, c->y, r);
  rtl(s);
  k->v_known = false;
  c->a = r->a;
  c->x = r->x;
  c->y = r->y;
  set_flags_pld(s, r->c);
}

// `JSL actor_nearest` at `at`: the nearest record to the point in X and Y,
// in X, and how far in A.
static void asks_nearest(Snakeoid* s, uint16_t at) {
  PortCpu* c = s->c;
  SnakeoidWork* k = s->k;
  if (k->nearest_count == SNAKEOID_MAX_NEAREST) k->nearest_count--;
  ActorNearestWork* work = &k->nearest[k->nearest_count++];
  jsl(s, at);
  uint16_t gap = 0;
  c->x = actor_nearest_counted(s->w, c->x, c->y, &gap, work);
  rtl(s);
  k->v_known = false;
  c->a = gap;
  set_flags_pld(s, false);
}

// --- Who it goes for --------------------------------------------------------

// `$82:9FFB`: the way from where it is to the point in X and Y, in A. 0 is
// no way: it is there.
static void way_to(Snakeoid* s) {
  PortCpu* c = s->c;
  set_data(s, W_SNAKEOID_TO_X, c->x);
  set_data(s, W_SNAKEOID_TO_Y, c->y);
  ldx(s, 0);
  lda(s, field(s, SNAKEOID_DP_Y));
  cmp16(c, c->a, data(s, W_SNAKEOID_TO_Y));
  ran(s, SNAKEOID_RUN_9FFB);
  if (!took(s, zero(s))) {
    // The compare's carry goes into the sum: a row for above, one for below.
    lda(s, c->x);
    c->a = adc16(c, c->a, 1);
    s->k->v_known = true;
    doubled(s);
    doubled(s);
    ldx(s, c->a);
    ran(s, SNAKEOID_RUN_A00B);
  }
  lda(s, field(s, SNAKEOID_DP_X));
  cmp16(c, c->a, data(s, W_SNAKEOID_TO_X));
  ran(s, SNAKEOID_RUN_A012);
  if (!took(s, zero(s))) {
    lda(s, c->x);
    c->a = adc16(c, c->a, 1);
    s->k->v_known = true;
    ldx(s, c->a);
    ran(s, SNAKEOID_RUN_A019);
  }
  lda(s, bus_r16(s->w, s->rom, SNAKEOID_WAYS + c->x) & 0x00ffu);
  ran(s, SNAKEOID_RUN_A01E);
  back(s);
}

// `$82:9F59`: nobody.
static void nobody(Snakeoid* s) {
  lda(s, 0);
  ldx(s, 0);
  ldy(s, 0);
  ran(s, SNAKEOID_RUN_9F59);
  back(s);
}

// `$82:9F36`: the record in X is who. It makes for a place beside them, by
// which way they are from it.
static void makes_for(Snakeoid* s) {
  PortCpu* c = s->c;
  phx(s);
  ldy(s, data(s, (uint16_t)(c->x + ACTOR_Y)));
  phy(s);
  lda(s, data(s, (uint16_t)(c->x + ACTOR_X)));
  ldx(s, c->a);
  phx(s);
  jsr(s, 0x9f40);
  way_to(s);
  doubled(s);
  doubled(s);
  ldy(s, c->a);
  pla(s);
  sub(s, data(s, (uint16_t)(SNAKEOID_BESIDE + c->y)));
  ldx(s, c->a);
  pla(s);
  sub(s, data(s, (uint16_t)(SNAKEOID_BESIDE + 2 + c->y)));
  ldy(s, c->a);
  jsr(s, 0x9f52);
  way_to(s);
  plx(s);
  ldy(s, field(s, SNAKEOID_DP_GAP));
  ran(s, SNAKEOID_RUN_9F36);
  back(s);
}

// `$82:9F1A`: neither word is set. The nearest record, if it is within the
// range and of a kind it eats.
static void looks_for_prey(Snakeoid* s) {
  PortCpu* c = s->c;
  pla(s);
  set_field(s, SNAKEOID_DP_GAP, c->a);
  asks_nearest(s, 0x9f1d);
  cmp16(c, c->a, field(s, SNAKEOID_DP_GAP));
  ran(s, SNAKEOID_RUN_9F1A);
  if (took(s, carry(s))) {
    nobody(s);
    return;
  }
  set_field(s, SNAKEOID_DP_GAP, c->a);
  ldy(s, data(s, (uint16_t)(c->x + ACTOR_COLLIDE_ID)));
  cmp16(c, c->y, SNAKEOID_EATS_A);
  ran(s, SNAKEOID_RUN_9F25);
  if (!took(s, zero(s))) {
    cmp16(c, c->y, SNAKEOID_EATS_B);
    ran(s, SNAKEOID_RUN_9F2F);
    if (!took(s, zero(s))) {
      PORT_COVER(snakeoid_not_prey);
      took(s, true);
      ran(s, SNAKEOID_RUN_9F34);
      nobody(s);
      return;
    }
  }
  PORT_COVER(snakeoid_finds_prey);
  makes_for(s);
}

// `$82:9F91`: one word is set, and the record in X is that player's. Within
// the range, by the sum of how far across and how far down?
static void looks_for_one_player(Snakeoid* s) {
  PortCpu* c = s->c;
  lda(s, field(s, SNAKEOID_DP_TRY_X));
  sub(s, data(s, (uint16_t)(c->x + ACTOR_X)));
  ran(s, SNAKEOID_RUN_9F91);
  if (!took(s, !negative(c->a))) {
    negated(s);
    ran(s, SNAKEOID_RUN_9F99);
  }
  set_field(s, SNAKEOID_DP_GAP, c->a);
  lda(s, field(s, SNAKEOID_DP_TRY_Y));
  sub(s, data(s, (uint16_t)(c->x + ACTOR_Y)));
  ran(s, SNAKEOID_RUN_9F9D);
  if (!took(s, !negative(c->a))) {
    negated(s);
    ran(s, SNAKEOID_RUN_9FA7);
  }
  add(s, field(s, SNAKEOID_DP_GAP));
  set_field(s, SNAKEOID_DP_GAP, c->a);
  pla(s);
  cmp16(c, c->a, field(s, SNAKEOID_DP_GAP));
  ran(s, SNAKEOID_RUN_9FAB);
  if (took(s, carry(s))) {
    PORT_COVER(snakeoid_finds_one_player);
    makes_for(s);
    return;
  }
  took(s, true);
  ran(s, SNAKEOID_RUN_9FB5);
  nobody(s);
}

// `$82:9F16`: who is there to go for, within A of the point in X and Y? It
// leaves the way to a place beside them in A, who in X and how far in Y, or
// 0 in all three.
static void looks_for(Snakeoid* s) {
  PortCpu* c = s->c;
  pha(s);
  ran(s, SNAKEOID_RUN_9F16);
  set_field(s, SNAKEOID_DP_TRY_X, c->x);
  set_field(s, SNAKEOID_DP_TRY_Y, c->y);
  lda(s, data(s, W_SNAKEOID_SIDE_A));
  add(s, data(s, W_SNAKEOID_SIDE_B));
  ran(s, SNAKEOID_RUN_9F5F);
  if (took(s, c->a == 0)) {
    looks_for_prey(s);
    return;
  }
  set_field(s, SNAKEOID_DP_GAP, c->a);
  sub(s, data(s, W_SNAKEOID_SIDE_A));
  ran(s, SNAKEOID_RUN_9F6C);
  if (took(s, c->a == 0)) {
    ldx(s, data(s, W_PLAYER_A_RECORD));
    took(s, true);
    ran(s, SNAKEOID_RUN_9F89);
    looks_for_one_player(s);
    return;
  }
  lda(s, field(s, SNAKEOID_DP_GAP));
  sub(s, data(s, W_SNAKEOID_SIDE_B));
  ran(s, SNAKEOID_RUN_9F74);
  if (took(s, c->a == 0)) {
    ldx(s, data(s, W_PLAYER_B_RECORD));
    ran(s, SNAKEOID_RUN_9F8E);
    looks_for_one_player(s);
    return;
  }

  pla(s);
  asks_players(s, 0x9f7d);
  ldy(s, c->a);
  ran(s, SNAKEOID_RUN_9F7C);
  if (took(s, c->y == 0)) {
    nobody(s);
    return;
  }
  PORT_COVER(snakeoid_finds_nearer_player);
  set_field(s, SNAKEOID_DP_GAP, c->x);
  ldx(s, c->a);
  took(s, true);
  ran(s, SNAKEOID_RUN_9F84);
  makes_for(s);
}

// `$82:A34A`: is anybody beside one of the four places it bites from? Which
// place is left in `$3A` and in A, or what is negative for none.
static void looks_beside(Snakeoid* s) {
  PortCpu* c = s->c;
  ldx(s, SNAKEOID_BITE_LAST);
  set_field(s, SNAKEOID_DP_COUNT, c->x);
  ran(s, SNAKEOID_RUN_A34A);
  for (;;) {
    ldx(s, field(s, SNAKEOID_DP_COUNT));
    lda(s, data(s, (uint16_t)(SNAKEOID_BITES_FROM + c->x)));
    add(s, field(s, SNAKEOID_DP_X));
    set_field(s, SNAKEOID_DP_TRY_X, c->a);
    lda(s, data(s, (uint16_t)(SNAKEOID_BITES_FROM + 2 + c->x)));
    add(s, field(s, SNAKEOID_DP_Y));
    set_field(s, SNAKEOID_DP_TRY_Y, c->a);
    lda(s, SNAKEOID_BESIDE_AT);
    ldx(s, field(s, SNAKEOID_DP_TRY_X));
    ldy(s, field(s, SNAKEOID_DP_TRY_Y));
    jsr(s, 0xa368);
    looks_for(s);
    ldy(s, c->a);
    ran(s, SNAKEOID_RUN_A34F);
    if (took(s, c->y != 0)) {
      set_field(s, SNAKEOID_DP_TARGET, c->x);
      lda(s, field(s, SNAKEOID_DP_COUNT));
      set_field(s, SNAKEOID_DP_SIDE, c->a);
      ran(s, SNAKEOID_RUN_A37B);
      back(s);
      return;
    }
    lda(s, field(s, SNAKEOID_DP_COUNT));
    sub(s, 0x0004);
    set_field(s, SNAKEOID_DP_COUNT, c->a);
    ran(s, SNAKEOID_RUN_A36E);
    if (!took(s, !negative(c->a))) break;
  }
  set_field(s, SNAKEOID_DP_SIDE, c->a);
  ran(s, SNAKEOID_RUN_A378);
  back(s);
}

// --- Where it can go --------------------------------------------------------

// One axis of `$82:A088`: may it be at the point in X and Y? `ground` and
// `edge` are the two runs, and their calls are 4 and 14 bytes in.
static bool may_be_at(Snakeoid* s, uint16_t x, uint16_t y, uint16_t at,
                      SnakeoidRun ground, SnakeoidRun edge) {
  ldx(s, x);
  ldy(s, y);
  const bool off = off_its_ground(s, (uint16_t)(at + 4));
  ran(s, ground);
  if (took(s, off)) return false;
  ldx(s, x);
  ldy(s, y);
  const bool out = off_the_level(s, (uint16_t)(at + 14));
  ran(s, edge);
  return !took(s, out);
}

// `$82:A088`: a step to the point in X and Y, an axis at a time: across if
// it may, and then down from wherever that left it.
static void steps_to(Snakeoid* s) {
  PortCpu* c = s->c;
  set_field(s, SNAKEOID_DP_TRY_X, c->x);
  set_field(s, SNAKEOID_DP_TRY_Y, c->y);
  if (may_be_at(s, field(s, SNAKEOID_DP_TRY_X), field(s, SNAKEOID_DP_Y),
                0xa08c, SNAKEOID_RUN_A088, SNAKEOID_RUN_A096)) {
    ldy(s, field(s, SNAKEOID_DP_RECORD));
    lda(s, field(s, SNAKEOID_DP_TRY_X));
    set_field(s, SNAKEOID_DP_X, c->a);
    set_data(s, (uint16_t)(c->y + ACTOR_X), c->a);
    ran(s, SNAKEOID_RUN_A0A0);
  }
  if (may_be_at(s, field(s, SNAKEOID_DP_X), field(s, SNAKEOID_DP_TRY_Y),
                0xa0a9, SNAKEOID_RUN_A0A9, SNAKEOID_RUN_A0B3)) {
    ldy(s, field(s, SNAKEOID_DP_RECORD));
    lda(s, field(s, SNAKEOID_DP_TRY_Y));
    set_field(s, SNAKEOID_DP_Y, c->a);
    set_data(s, (uint16_t)(c->y + ACTOR_Y), c->a);
    ran(s, SNAKEOID_RUN_A0BD);
  }
  rts(s);
}

// `$82:A1AF`: it goes to the point at `$30` and `$32`, if it may be there.
static bool lands(Snakeoid* s) {
  PortCpu* c = s->c;
  if (may_be_at(s, field(s, SNAKEOID_DP_TRY_X), field(s, SNAKEOID_DP_TRY_Y),
                0xa1af, SNAKEOID_RUN_A1AF, SNAKEOID_RUN_A1B9)) {
    ldy(s, field(s, SNAKEOID_DP_RECORD));
    lda(s, field(s, SNAKEOID_DP_TRY_X));
    set_field(s, SNAKEOID_DP_X, c->a);
    set_data(s, (uint16_t)(c->y + ACTOR_X), c->a);
    lda(s, field(s, SNAKEOID_DP_TRY_Y));
    set_field(s, SNAKEOID_DP_Y, c->a);
    set_data(s, (uint16_t)(c->y + ACTOR_Y), c->a);
    ran(s, SNAKEOID_RUN_A1C3);
  }
  rts(s);
  return !carry(s);
}

// The runs of a step, which each state has a copy of.
typedef struct {
  SnakeoidRun aims, missed_x, compares_y, missed_y, asks;
  uint16_t call;  // its `JSR $A088`
} StepRuns;

static const StepRuns WANDER_STEP = {SNAKEOID_RUN_A477, SNAKEOID_RUN_A498,
                                     SNAKEOID_RUN_A49A, SNAKEOID_RUN_A4A0,
                                     SNAKEOID_RUN_A4A2, 0xa48d};
static const StepRuns CHASE_STEP = {SNAKEOID_RUN_A4FA, SNAKEOID_RUN_A51B,
                                    SNAKEOID_RUN_A51D, SNAKEOID_RUN_A523,
                                    SNAKEOID_RUN_A525, 0xa510};

// A step its way. True if it was stopped, on either axis.
static bool step_is_stopped(Snakeoid* s, const StepRuns* r) {
  PortCpu* c = s->c;
  lda(s, field(s, SNAKEOID_DP_WAY));
  doubled(s);
  ldx(s, c->a);
  lda(s, field(s, SNAKEOID_DP_Y));
  add(s, data(s, (uint16_t)(SNAKEOID_STEPS + 2 + c->x)));
  ldy(s, c->a);
  set_field(s, SNAKEOID_DP_AIM_Y, c->a);
  lda(s, field(s, SNAKEOID_DP_X));
  add(s, data(s, (uint16_t)(SNAKEOID_STEPS + c->x)));
  set_field(s, SNAKEOID_DP_AIM_X, c->a);
  ldx(s, c->a);
  jsr(s, r->call);
  steps_to(s);

  set_field(s, SNAKEOID_DP_GAP, 0);
  lda(s, field(s, SNAKEOID_DP_X));
  cmp16(c, c->a, field(s, SNAKEOID_DP_AIM_X));
  ran(s, r->aims);
  if (!took(s, zero(s))) {
    count_up(s, SNAKEOID_DP_GAP);
    ran(s, r->missed_x);
  }
  lda(s, field(s, SNAKEOID_DP_Y));
  cmp16(c, c->a, field(s, SNAKEOID_DP_AIM_Y));
  ran(s, r->compares_y);
  if (!took(s, zero(s))) {
    count_up(s, SNAKEOID_DP_GAP);
    ran(s, r->missed_y);
  }
  lda(s, field(s, SNAKEOID_DP_GAP));
  ran(s, r->asks);
  return took(s, c->a != 0);
}

// `$82:9E57`: the four tiles about it that can be churned are, unless the
// list of tile changes is long already.
static void churns(Snakeoid* s) {
  PortCpu* c = s->c;
  lda(s, data(s, W_TILE_PUT_BYTES));
  cmp16(c, c->a, SNAKEOID_CHURN_LIST_FULL);
  ran(s, SNAKEOID_RUN_9E57);
  if (took(s, carry(s))) {
    PORT_COVER(snakeoid_list_full);
    rts(s);
    return;
  }
  lda(s, field(s, SNAKEOID_DP_X));
  for (int i = 0; i < TILE_ATTRS_PIXEL_SHIFT; i++) halved(s);
  set_field(s, SNAKEOID_DP_TRY_X, c->a);
  lda(s, field(s, SNAKEOID_DP_Y));
  for (int i = 0; i < TILE_ATTRS_PIXEL_SHIFT; i++) halved(s);
  set_field(s, SNAKEOID_DP_TRY_Y, c->a);
  ldx(s, SNAKEOID_CHURN_LAST_ROW);
  set_field(s, SNAKEOID_DP_COUNT, c->x);
  ran(s, SNAKEOID_RUN_9E5F);
  do {
    lda(s, field(s, SNAKEOID_DP_COUNT));
    add(s, field(s, SNAKEOID_DP_GROUND));
    ldx(s, c->a);
    lda(s, data(s, (uint16_t)(SNAKEOID_CHURN + c->x)));
    add(s, field(s, SNAKEOID_DP_TRY_X));
    set_field(s, SNAKEOID_DP_TRY_X, c->a);
    lda(s, data(s, (uint16_t)(SNAKEOID_CHURN + 2 + c->x)));
    add(s, field(s, SNAKEOID_DP_TRY_Y));
    set_field(s, SNAKEOID_DP_TRY_Y, c->a);
    ldx(s, field(s, SNAKEOID_DP_TRY_X));
    ldy(s, field(s, SNAKEOID_DP_TRY_Y));
    reads_tile(s, 0x9e8c);
    shares_bit(s, SNAKEOID_TILE_SOFT);
    ran(s, SNAKEOID_RUN_9E72);
    if (!took(s, zero(s))) {
      PORT_COVER(snakeoid_churns);
      lda(s, field(s, SNAKEOID_DP_COUNT));
      add(s, field(s, SNAKEOID_DP_GROUND));
      ldx(s, c->a);
      lda(s, data(s, (uint16_t)(SNAKEOID_CHURN + 4 + c->x)));
      ldx(s, field(s, SNAKEOID_DP_TRY_X));
      ldy(s, field(s, SNAKEOID_DP_TRY_Y));
      puts_tile(s, 0x9ea2);
      ran(s, SNAKEOID_RUN_9E95);
    }
    lda(s, field(s, SNAKEOID_DP_COUNT));
    sub(s, 0x0008);
    set_field(s, SNAKEOID_DP_COUNT, c->a);
    ran(s, SNAKEOID_RUN_9EA6);
  } while (took(s, !negative(c->a)));
  rts(s);
}

// --- What can stop ----------------------------------------------------------
//
// From here down a routine may stop on a call the harness makes, so each
// hands back the address it stopped on. One that ends in an `RTS` goes on
// with whatever the address it pulls is the return to.

static uint32_t goes_back(Snakeoid* s);
static uint32_t rises_on(Snakeoid* s);
static uint32_t shows_next(Snakeoid* s);

static uint32_t returns(Snakeoid* s) {
  ran(s, SNAKEOID_RUN_RTS);
  return goes_back(s);
}

// `$82:9DC0`: the piece before, if there is one.
static bool piece_before(Snakeoid* s) {
  lda(s, s->c->x);
  sub(s, SNAKEOID_TRACK_STRIDE);
  ldx(s, s->c->a);
  ran(s, SNAKEOID_RUN_9DC0);
  return took(s, !negative(s->c->x));
}

// `$82:9DAD`: the pieces of track from the one in X down grow a frame older.
// One in eight frames a piece is shown flatter, and at the end it is freed.
static uint32_t ages_pieces(Snakeoid* s) {
  PortCpu* c = s->c;
  do {
    lda(s, field(s, (uint16_t)(SNAKEOID_DP_TRACK + c->x)));
    cmp16(c, c->a, SNAKEOID_NO_PIECE);
    ran(s, SNAKEOID_RUN_9DAD);
    if (took(s, zero(s))) continue;

    lda(s, field(s, (uint16_t)(SNAKEOID_DP_TRACK_AGE + c->x)));
    one_less(s);
    set_field(s, (uint16_t)(SNAKEOID_DP_TRACK_AGE + c->x), c->a);
    ran(s, SNAKEOID_RUN_9DB4);
    if (took(s, c->a == 0)) {
      PORT_COVER(snakeoid_piece_goes);
      phx(s);
      ldy(s, SNAKEOID_NO_PIECE);
      lda(s, field(s, (uint16_t)(SNAKEOID_DP_TRACK + c->x)));
      set_field(s, (uint16_t)(SNAKEOID_DP_TRACK + c->x), c->y);
      ran(s, SNAKEOID_RUN_9DDB);
      return SNAKEOID_FREE_PC;
    }
    lda(s, c->a & 0x0007u);
    ran(s, SNAKEOID_RUN_9DBB);
    if (took(s, c->a == 0)) {
      PORT_COVER(snakeoid_piece_flattens);
      lda(s, field(s, (uint16_t)(SNAKEOID_DP_TRACK_AGE + c->x)));
      halved(s);
      halved(s);
      one_less(s);
      one_less(s);
      ldy(s, c->a);
      lda(s, data(s, (uint16_t)(SNAKEOID_TRACK_PICTURES + c->y)));
      ldy(s, field(s, (uint16_t)(SNAKEOID_DP_TRACK + c->x)));
      set_data(s, (uint16_t)(c->y + ACTOR_META), c->a);
      ran(s, SNAKEOID_RUN_9DC9);
    }
  } while (piece_before(s));
  return returns(s);
}

// `$82:9DA7`.
static uint32_t ages_track(Snakeoid* s) {
  ldx(s, (SNAKEOID_TRACK_PIECES - 1) * SNAKEOID_TRACK_STRIDE);
  ldy(s, SNAKEOID_NO_PIECE);
  ran(s, SNAKEOID_RUN_9DA7);
  return ages_pieces(s);
}

// `$82:9DE7`: the piece is freed, and the ones before it are seen to.
static uint32_t freed(Snakeoid* s) {
  plx(s);
  ran(s, SNAKEOID_RUN_9DE7);
  if (piece_before(s)) return ages_pieces(s);
  return returns(s);
}

// `$82:9DF7`: every other step, a piece of track where it is, if one of the
// four is not in use.
static uint32_t drops_track(Snakeoid* s) {
  PortCpu* c = s->c;
  const uint16_t left = count_down(s, SNAKEOID_DP_DROP_IN);
  ran(s, SNAKEOID_RUN_9DF7);
  if (took(s, !negative(left))) return returns(s);

  ldx(s, (SNAKEOID_TRACK_PIECES - 1) * SNAKEOID_TRACK_STRIDE);
  ran(s, SNAKEOID_RUN_9DFB);
  for (;;) {
    lda(s, field(s, (uint16_t)(SNAKEOID_DP_TRACK + c->x)));
    cmp16(c, c->a, SNAKEOID_NO_PIECE);
    ran(s, SNAKEOID_RUN_9DFE);
    if (took(s, zero(s))) break;
    lda(s, c->x);
    sub(s, SNAKEOID_TRACK_STRIDE);
    ldx(s, c->a);
    ran(s, SNAKEOID_RUN_9E05);
    if (!took(s, !negative(c->x))) {
      PORT_COVER(snakeoid_track_full);
      return returns(s);
    }
  }
  lda(s, 0x0001);
  set_field(s, SNAKEOID_DP_DROP_IN, c->a);
  phx(s);
  ran(s, SNAKEOID_RUN_9E0E);
  return SNAKEOID_RECORD_PC;
}

// `$82:9E18`: the record in A is the piece. It lies a pixel above where the
// mound is, for 32 frames, and the ground about it is churned.
static uint32_t dropped(Snakeoid* s) {
  PortCpu* c = s->c;
  PORT_COVER(snakeoid_drops_track);
  plx(s);
  set_field(s, (uint16_t)(SNAKEOID_DP_TRACK + c->x), c->a);
  ldy(s, c->a);
  lda(s, SNAKEOID_TRACK_FRAMES);
  set_field(s, (uint16_t)(SNAKEOID_DP_TRACK_AGE + c->x), c->a);
  lda(s, field(s, SNAKEOID_DP_X));
  set_data(s, (uint16_t)(c->y + ACTOR_X), c->a);
  lda(s, 0);
  set_data(s, (uint16_t)(c->y + ACTOR_Z), c->a);
  lda(s, field(s, SNAKEOID_DP_Y));
  one_less(s);
  set_data(s, (uint16_t)(c->y + ACTOR_Y), c->a);
  lda(s, SNAKEOID_MOUND_PICTURE);
  set_data(s, (uint16_t)(c->y + ACTOR_META), c->a);
  lda(s, SNAKEOID_META_BANK);
  set_data(s, (uint16_t)(c->y + ACTOR_META_BANK), c->a);
  lda(s, 0);
  set_data(s, (uint16_t)(c->y + ACTOR_COLLIDE_ID), c->a);
  lda(s, data(s, W_SCHED_CUR_TASK));
  set_data(s, (uint16_t)(c->y + ACTOR_THREAD), c->a);
  lda(s, ACTOR_DRAW);
  lda(s, c->a | data(s, (uint16_t)(c->y + ACTOR_FLAGS)));
  set_data(s, (uint16_t)(c->y + ACTOR_FLAGS), c->a);
  jsr(s, 0x9e53);
  ran(s, SNAKEOID_RUN_9E18);
  churns(s);
  return returns(s);
}

// `$82:A1EC`: a tick of the picture it is coming up in.
static uint32_t rises_on(Snakeoid* s) {
  pla(s);
  one_less(s);
  ran(s, SNAKEOID_RUN_A1EC);
  if (took(s, negative(s->c->a))) {
    took(s, true);
    ran(s, SNAKEOID_RUN_A1FD);
    return shows_next(s);
  }
  pha(s);
  lda(s, 0x0001);
  ran(s, SNAKEOID_RUN_A1F0);
  return SNAKEOID_RISE_PC;
}

// `$82:A1D8`: the next of the four pictures it comes up in, and for how
// long.
static uint32_t shows_next(Snakeoid* s) {
  PortCpu* c = s->c;
  ply(s);
  lda(s, data(s, (uint16_t)(SNAKEOID_RISE + c->y)));
  ran(s, SNAKEOID_RUN_A1D8);
  if (took(s, c->a == 0)) {
    PORT_COVER(snakeoid_is_up);
    return returns(s);
  }
  ldx(s, field(s, SNAKEOID_DP_RECORD));
  set_data(s, (uint16_t)(c->x + ACTOR_META), c->a);
  ldy(s, (uint16_t)(c->y + 2));
  lda(s, data(s, (uint16_t)(SNAKEOID_RISE + c->y)));
  ldy(s, (uint16_t)(c->y + 2));
  phy(s);
  pha(s);
  ran(s, SNAKEOID_RUN_A1DE);
  return rises_on(s);
}

// `$82:A1D4`: it comes up where it now is.
static uint32_t rises(Snakeoid* s) {
  ldy(s, 0);
  phy(s);
  ran(s, SNAKEOID_RUN_A1D4);
  return shows_next(s);
}

// `$82:A0C7`: a hop. The first place along its way it could be, 8 pixels at
// a time from 24 off, and it comes up there. Carry is left set if there is
// none.
static uint32_t hops(Snakeoid* s) {
  PortCpu* c = s->c;
  lda(s, field(s, SNAKEOID_DP_WAY));
  doubled(s);
  ldx(s, c->a);
  lda(s, data(s, (uint16_t)(SNAKEOID_HOP_STRIDE + c->x)));
  set_field(s, SNAKEOID_DP_AIM_X, c->a);
  lda(s, data(s, (uint16_t)(SNAKEOID_HOP_STRIDE + 2 + c->x)));
  set_field(s, SNAKEOID_DP_AIM_Y, c->a);
  lda(s, field(s, SNAKEOID_DP_X));
  add(s, data(s, (uint16_t)(SNAKEOID_HOP_FROM + c->x)));
  set_field(s, SNAKEOID_DP_TRY_X, c->a);
  lda(s, field(s, SNAKEOID_DP_Y));
  add(s, data(s, (uint16_t)(SNAKEOID_HOP_FROM + 2 + c->x)));
  set_field(s, SNAKEOID_DP_TRY_Y, c->a);
  lda(s, SNAKEOID_HOP_TRIES);
  set_field(s, SNAKEOID_DP_COUNT, c->a);
  ran(s, SNAKEOID_RUN_A0C7);
  for (;;) {
    lda(s, field(s, SNAKEOID_DP_TRY_X));
    add(s, field(s, SNAKEOID_DP_AIM_X));
    set_field(s, SNAKEOID_DP_LANDS_X, c->a);
    set_field(s, SNAKEOID_DP_TRY_X, c->a);
    ldx(s, c->a);
    lda(s, field(s, SNAKEOID_DP_TRY_Y));
    add(s, field(s, SNAKEOID_DP_AIM_Y));
    set_field(s, SNAKEOID_DP_LANDS_Y, c->a);
    set_field(s, SNAKEOID_DP_TRY_Y, c->a);
    ldy(s, c->a);
    jsr(s, 0xa0fe);
    const bool landed = lands(s);
    ran(s, SNAKEOID_RUN_A0EA);
    if (!took(s, !landed)) {
      PORT_COVER(snakeoid_hops);
      jsr(s, 0xa103);
      ran(s, SNAKEOID_RUN_A103);
      return rises(s);
    }
    const uint16_t left = count_down(s, SNAKEOID_DP_COUNT);
    ran(s, SNAKEOID_RUN_A108);
    if (!took(s, !negative(left))) break;
  }
  PORT_COVER(snakeoid_nowhere_to_hop);
  set_c(c, true);
  ran(s, SNAKEOID_RUN_A10C);
  return goes_back(s);
}

// A draw of 16 to 47, or of 24 to 55, and that far one side of them or the
// other by its low bit. `bias` is what is added and `at` the `JSL`.
static void draws_offset(Snakeoid* s, uint16_t at, uint16_t bias,
                         SnakeoidRun run, SnakeoidRun flipped) {
  draw(s, at);
  lda(s, s->c->a & 0x001fu);
  add(s, bias);
  shares_bit(s, 0x0001);
  ran(s, run);
  if (!took(s, zero(s))) {
    negated(s);
    ran(s, flipped);
  }
}

// `$82:A156`: it comes up beside a player, one or the other by a draw, if
// that player is about and it may be where the draws put it.
static uint32_t leaps_at_a_player(Snakeoid* s) {
  PortCpu* c = s->c;
  draw(s, 0xa156);
  lda(s, c->a & 0x0002u);
  ldx(s, c->a);
  lda(s, data(s, (uint16_t)(W_PLAYER_A_RECORD + c->x)));
  ran(s, SNAKEOID_RUN_A156);
  if (took(s, c->a == 0)) return returns(s);
  ldx(s, c->a);
  lda(s, data(s, (uint16_t)(c->x + ACTOR_COLLIDE_ID)));
  cmp16(c, c->a, SNAKEOID_PLAYER_KIND_A);
  ran(s, SNAKEOID_RUN_A163);
  if (!took(s, zero(s))) {
    cmp16(c, c->a, SNAKEOID_PLAYER_KIND_B);
    ran(s, SNAKEOID_RUN_A16C);
    if (!took(s, zero(s))) return returns(s);
  }

  draws_offset(s, 0xa172, 0x0010, SNAKEOID_RUN_A172, SNAKEOID_RUN_A182);
  add(s, data(s, (uint16_t)(c->x + ACTOR_X)));
  set_field(s, SNAKEOID_DP_TRY_X, c->a);
  draws_offset(s, 0xa18c, 0x0018, SNAKEOID_RUN_A186, SNAKEOID_RUN_A19C);
  add(s, data(s, (uint16_t)(c->x + ACTOR_Y)));
  set_field(s, SNAKEOID_DP_TRY_Y, c->a);
  jsr(s, 0xa1a6);
  const bool landed = lands(s);
  ran(s, SNAKEOID_RUN_A1A0);
  if (took(s, !landed)) return returns(s);
  PORT_COVER(snakeoid_leaps);
  jsr(s, 0xa1ab);
  ran(s, SNAKEOID_RUN_A1AB);
  return rises(s);
}

// `$82:A44B`: it will wander.
static uint32_t will_wander(Snakeoid* s) {
  lda(s, SNAKEOID_STATE_WANDERING);
  set_field(s, SNAKEOID_DP_STATE, s->c->a);
  ran(s, SNAKEOID_RUN_A44B);
  return goes_back(s);
}

// `$82:A43D`: one of the eight ways by a draw, and it wanders.
static uint32_t picks_a_way(Snakeoid* s) {
  draw(s, 0xa43d);
  lda(s, s->c->a & 0x0007u);
  one_more(s);
  doubled(s);
  set_field(s, SNAKEOID_DP_WAY, s->c->a);
  ran(s, SNAKEOID_RUN_A43D);
  return will_wander(s);
}

// `$82:A418`: a frame nearer its next step, and its track a frame older.
static uint32_t ticks(Snakeoid* s) {
  const uint16_t left = count_down(s, SNAKEOID_DP_PAUSE);
  ran(s, SNAKEOID_RUN_A418);
  if (!took(s, !negative(left))) {
    lda(s, SNAKEOID_STEP_FRAMES);
    set_field(s, SNAKEOID_DP_PAUSE, s->c->a);
    ran(s, SNAKEOID_RUN_A41C);
  }
  jsr(s, 0xa421);
  ran(s, SNAKEOID_RUN_A421);
  return ages_track(s);
}

// --- Wandering --------------------------------------------------------------

// `$82:A451`.
static uint32_t wanders(Snakeoid* s) {
  jsr(s, 0xa451);
  ran(s, SNAKEOID_RUN_A451);
  return ticks(s);
}

// `$82:A454`: anybody within `$B4` and it will chase. Else, on the frame it
// steps, a draw: a leap at a player, or a step.
static uint32_t wanders_on(Snakeoid* s) {
  PortCpu* c = s->c;
  lda(s, SNAKEOID_SEES);
  ldx(s, field(s, SNAKEOID_DP_X));
  ldy(s, field(s, SNAKEOID_DP_Y));
  jsr(s, 0xa45b);
  looks_for(s);
  ldy(s, c->a);
  ran(s, SNAKEOID_RUN_A454);
  if (took(s, c->y != 0)) {
    PORT_COVER(snakeoid_sees_someone);
    set_field(s, SNAKEOID_DP_TARGET, c->x);
    ran(s, SNAKEOID_RUN_A4B8);
    lda(s, SNAKEOID_STATE_CHASING);
    set_field(s, SNAKEOID_DP_STATE, c->a);
    ran(s, SNAKEOID_RUN_A4BD);
    return goes_back(s);
  }
  lda(s, field(s, SNAKEOID_DP_PAUSE));
  ran(s, SNAKEOID_RUN_A461);
  if (took(s, c->a != 0)) return returns(s);

  draw(s, 0xa465);
  cmp16(c, c->a, SNAKEOID_LEAPS_UNDER);
  ran(s, SNAKEOID_RUN_A465);
  if (!took(s, carry(s))) {
    jsr(s, 0xa46e);
    ran(s, SNAKEOID_RUN_A46E);
    return leaps_at_a_player(s);
  }
  jsr(s, 0xa474);
  ran(s, SNAKEOID_RUN_A474);
  return drops_track(s);
}

// `$82:A477`: the step. Stopped, a draw: a hop, or a new way.
static uint32_t wanders_a_step(Snakeoid* s) {
  PortCpu* c = s->c;
  if (!step_is_stopped(s, &WANDER_STEP)) {
    PORT_COVER(snakeoid_wanders);
    return returns(s);
  }
  PORT_COVER(snakeoid_wander_stopped);
  draw(s, 0xa4a7);
  cmp16(c, c->a, SNAKEOID_HOPS_UNDER);
  ran(s, SNAKEOID_RUN_A4A7);
  if (!took(s, carry(s))) {
    jsr(s, 0xa4b0);
    ran(s, SNAKEOID_RUN_A4B0);
    return hops(s);
  }
  ran(s, SNAKEOID_RUN_A4B5);
  return picks_a_way(s);
}

// `$82:A4B3`: the hop is over, or there was nowhere to.
static uint32_t wander_hopped(Snakeoid* s) {
  ran(s, SNAKEOID_RUN_A4B3);
  if (took(s, !carry(s))) return returns(s);
  ran(s, SNAKEOID_RUN_A4B5);
  return picks_a_way(s);
}

// --- Chasing ----------------------------------------------------------------

// `$82:A4C3`.
static uint32_t chases(Snakeoid* s) {
  jsr(s, 0xa4c3);
  ran(s, SNAKEOID_RUN_A4C3);
  return ticks(s);
}

// `$82:A4C6`: nobody within `$DC` and it wanders. Within 12 of them, or
// with somebody beside it, it comes up to bite. Else, on the frame it
// steps, a step, and it steps sooner for being near.
static uint32_t chases_on(Snakeoid* s) {
  PortCpu* c = s->c;
  lda(s, SNAKEOID_KEEPS);
  ldx(s, field(s, SNAKEOID_DP_X));
  ldy(s, field(s, SNAKEOID_DP_Y));
  jsr(s, 0xa4cd);
  looks_for(s);
  cmp16(c, c->a, 0);
  ran(s, SNAKEOID_RUN_A4C6);
  if (took(s, zero(s))) {
    PORT_COVER(snakeoid_loses_them);
    ran(s, SNAKEOID_RUN_A52F);
    return will_wander(s);
  }
  doubled(s);
  set_field(s, SNAKEOID_DP_WAY, c->a);
  set_field(s, SNAKEOID_DP_TARGET, c->x);
  cmp16(c, c->y, SNAKEOID_BITES_AT);
  ran(s, SNAKEOID_RUN_A4D5);
  if (took(s, !carry(s))) {
    PORT_COVER(snakeoid_bites);
    ran(s, SNAKEOID_RUN_A532);
    return SNAKEOID_BITES_PC;
  }
  phy(s);
  jsr(s, 0xa4e0);
  looks_beside(s);
  ply(s);
  ldx(s, c->a);
  ran(s, SNAKEOID_RUN_A4DF);
  if (took(s, !negative(c->x))) {
    PORT_COVER(snakeoid_bites_beside);
    ran(s, SNAKEOID_RUN_A535);
    return SNAKEOID_BITES_BESIDE_PC;
  }

  cmp16(c, c->y, SNAKEOID_HURRIES_AT);
  ran(s, SNAKEOID_RUN_A4E7);
  if (!took(s, carry(s))) {
    lda(s, field(s, SNAKEOID_DP_PAUSE));
    ran(s, SNAKEOID_RUN_A4EC);
    if (!took(s, c->a == 0)) {
      PORT_COVER(snakeoid_hurries);
      one_less(s);
      set_field(s, SNAKEOID_DP_PAUSE, c->a);
      ran(s, SNAKEOID_RUN_A4F0);
    }
  }
  lda(s, field(s, SNAKEOID_DP_PAUSE));
  ran(s, SNAKEOID_RUN_A4F3);
  if (took(s, c->a != 0)) return returns(s);
  jsr(s, 0xa4f7);
  ran(s, SNAKEOID_RUN_A4F7);
  return drops_track(s);
}

// `$82:A4FA`: the step. Stopped, it hops.
static uint32_t chases_a_step(Snakeoid* s) {
  if (!step_is_stopped(s, &CHASE_STEP)) {
    PORT_COVER(snakeoid_chases);
    return returns(s);
  }
  PORT_COVER(snakeoid_chase_stopped);
  jsr(s, 0xa52a);
  ran(s, SNAKEOID_RUN_A52A);
  return hops(s);
}

// `$82:A52D`: the hop is over. With nowhere to hop to, it wanders.
static uint32_t chase_hopped(Snakeoid* s) {
  ran(s, SNAKEOID_RUN_A52D);
  if (took(s, !carry(s))) return returns(s);
  ran(s, SNAKEOID_RUN_A52F);
  return will_wander(s);
}

// --- Its thread -------------------------------------------------------------

// `$82:A425`: it waits for a player within `$DC`, asking every four ticks.
static uint32_t waits(Snakeoid* s) {
  PortCpu* c = s->c;
  lda(s, SNAKEOID_WAKES_AT);
  ldx(s, field(s, SNAKEOID_DP_X));
  ldy(s, field(s, SNAKEOID_DP_Y));
  asks_players(s, 0xa42c);
  ldy(s, c->a);
  ran(s, SNAKEOID_RUN_A425);
  if (took(s, c->y != 0)) {
    PORT_COVER(snakeoid_wakes);
    return returns(s);
  }
  lda(s, 0x0004);
  ran(s, SNAKEOID_RUN_A433);
  return SNAKEOID_WAIT_PC;
}

// `$82:A8DE`: a frame. Nothing has hit it yet, and its state's own routine
// is gone to by an `RTS`.
static uint32_t takes_its_turn(Snakeoid* s) {
  PortCpu* c = s->c;
  set_field(s, SNAKEOID_DP_HIT_BY, 0);
  push16(s->w, c, SNAKEOID_RET_STATE);
  lda(s, field(s, SNAKEOID_DP_STATE));
  one_less(s);
  pha(s);
  ran(s, SNAKEOID_RUN_A8DE);
  if ((uint16_t)(pull16(s->w, c) + 1) == SNAKEOID_STATE_WANDERING)
    return wanders(s);
  return chases(s);
}

// `$82:A8EF`: is it dying?
static uint32_t wakes(Snakeoid* s) {
  lda(s, field(s, SNAKEOID_DP_DYING));
  ran(s, SNAKEOID_RUN_A8EF);
  if (took(s, s->c->a == 0)) return takes_its_turn(s);
  PORT_COVER(snakeoid_dies);
  return SNAKEOID_DIES_PC;
}

// `$82:A43A`: a sleep of its wait is over.
static uint32_t waited(Snakeoid* s) {
  took(s, true);
  ran(s, SNAKEOID_RUN_A43A);
  return waits(s);
}

// What an `RTS` goes on with, by the address it pulls.
static uint32_t goes_back(Snakeoid* s) {
  const uint16_t pulled = pull16(s->w, s->c);
  switch (pulled) {
    case SNAKEOID_RET_READY:
      jsr(s, 0xa8d8);
      ran(s, SNAKEOID_RUN_A8D8);
      return waits(s);
    case SNAKEOID_RET_WAIT:
      jsr(s, 0xa8db);
      ran(s, SNAKEOID_RUN_A8DB);
      return picks_a_way(s);
    case SNAKEOID_RET_WAY:
      return takes_its_turn(s);
    case SNAKEOID_RET_STATE:
      lda(s, 0x0001);
      ran(s, SNAKEOID_RUN_A8E8);
      return SNAKEOID_SLEEP_PC;
    case SNAKEOID_RET_AGED:
      return returns(s);
    case SNAKEOID_RET_AGED_RISING:
      took(s, true);
      ran(s, SNAKEOID_RUN_A1FB);
      return rises_on(s);
    case SNAKEOID_RET_TICK_WANDER:
      return wanders_on(s);
    case SNAKEOID_RET_TICK_CHASE:
      return chases_on(s);
    case SNAKEOID_RET_DROP_WANDER:
      return wanders_a_step(s);
    case SNAKEOID_RET_DROP_CHASE:
      return chases_a_step(s);
    case SNAKEOID_RET_RISE_HOP:
      set_c(s->c, false);
      ran(s, SNAKEOID_RUN_A106);
      return goes_back(s);
    case SNAKEOID_RET_RISE_LEAP:
      return returns(s);
    case SNAKEOID_RET_HOP_WANDER:
      return wander_hopped(s);
    case SNAKEOID_RET_HOP_CHASE:
      return chase_hopped(s);
    case SNAKEOID_RET_LEAP:
      ran(s, SNAKEOID_RUN_A471);
      return picks_a_way(s);
  }
  // Not one of its own, which the harness's guard has ruled out.
  return (uint32_t)SNAKEOID_BANK << 16 | (uint16_t)(pulled + 1);
}

// `$82:A1F8`: a tick of its coming up is over, and its track is a frame
// older.
static uint32_t rose(Snakeoid* s) {
  jsr(s, 0xa1f8);
  ran(s, SNAKEOID_RUN_A1F8);
  return ages_track(s);
}

// --- The stretches ----------------------------------------------------------

static const struct {
  uint32_t pc;
  uint32_t (*run)(Snakeoid* s);
} STRETCHES[] = {
    {SNAKEOID_READY_PC, returns},   {SNAKEOID_WAITED_PC, waited},
    {SNAKEOID_WAKES_PC, wakes},     {SNAKEOID_FREED_PC, freed},
    {SNAKEOID_DROPPED_PC, dropped}, {SNAKEOID_ROSE_PC, rose},
};

bool snakeoid_state_known(uint16_t state) {
  return state == SNAKEOID_STATE_WANDERING || state == SNAKEOID_STATE_CHASING;
}

// The word `at` bytes under the top of the stack.
static uint16_t stacked(const Wram* w, uint16_t s, int at) {
  return wram_r16(w, (uint16_t)(s + 1 + at));
}

// Is the return of its coming up, `at` bytes down, one this port follows
// back to its state's own?
static bool rise_returns_known(const Wram* w, uint16_t s, int at) {
  const uint16_t next = stacked(w, s, at + 2);
  switch (stacked(w, s, at)) {
    case SNAKEOID_RET_RISE_HOP:
      return (next == SNAKEOID_RET_HOP_WANDER ||
              next == SNAKEOID_RET_HOP_CHASE) &&
             stacked(w, s, at + 4) == SNAKEOID_RET_STATE;
    case SNAKEOID_RET_RISE_LEAP:
      // A leap the ROM's own biting jumped to comes straight back.
      return next == SNAKEOID_RET_STATE ||
             (next == SNAKEOID_RET_LEAP &&
              stacked(w, s, at + 4) == SNAKEOID_RET_STATE);
  }
  return false;
}

static bool piece_known(uint16_t at) {
  return at < SNAKEOID_TRACK_PIECES * SNAKEOID_TRACK_STRIDE &&
         at % SNAKEOID_TRACK_STRIDE == 0;
}

bool snakeoid_stack_known(const Wram* w, uint32_t pc, uint16_t s) {
  switch (pc) {
    case SNAKEOID_WAKES_PC:
      return true;
    case SNAKEOID_READY_PC:
      return stacked(w, s, 0) == SNAKEOID_RET_READY;
    case SNAKEOID_WAITED_PC:
      return stacked(w, s, 0) == SNAKEOID_RET_WAIT;
    case SNAKEOID_DROPPED_PC: {
      // The piece it pushed, and then the step's return.
      const uint16_t step = stacked(w, s, 2);
      return piece_known(stacked(w, s, 0)) &&
             (step == SNAKEOID_RET_DROP_WANDER ||
              step == SNAKEOID_RET_DROP_CHASE) &&
             stacked(w, s, 4) == SNAKEOID_RET_STATE;
    }
    case SNAKEOID_FREED_PC: {
      if (!piece_known(stacked(w, s, 0))) return false;
      const uint16_t tick = stacked(w, s, 4);
      switch (stacked(w, s, 2)) {
        case SNAKEOID_RET_AGED:
          return (tick == SNAKEOID_RET_TICK_WANDER ||
                  tick == SNAKEOID_RET_TICK_CHASE) &&
                 stacked(w, s, 6) == SNAKEOID_RET_STATE;
        case SNAKEOID_RET_AGED_RISING:
          // The ticks left and the place in the list are under it.
          return rise_returns_known(w, s, 8);
      }
      return false;
    }
    case SNAKEOID_ROSE_PC:
      return rise_returns_known(w, s, 4);
  }
  return false;
}

void snakeoid_run(Wram* w, const Rom* rom, PortCpu* c, SnakeoidWork* k) {
  Snakeoid s = {w, rom, c, k};
  k->v_known = true;
  for (unsigned i = 0; i < sizeof STRETCHES / sizeof STRETCHES[0]; i++) {
    if (STRETCHES[i].pc != c->pc) continue;
    c->pc = STRETCHES[i].run(&s);
    return;
  }
}
