// The Snakeoid's thread -- see port/snakeoid.h.

#include "port/snakeoid.h"

#include "port/begin.h"  // where its thread's last `RTL` goes
#include "port/bossbg.h"  // the upload's four words, and the picture's
#include "port/coverage.h"
#include "port/frontend.h"  // W_FRAME_COUNT
#include "port/rng.h"
#include "port/thread.h"
#include "port/vblank.h"  // the jobs its bite queues

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
static uint32_t bites_under(Snakeoid* s);
static uint32_t bites_beside(Snakeoid* s);

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
    return bites_under(s);
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
    return bites_beside(s);
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

// --- Its bite ----------------------------------------------------------------
//
// `$82:A538` and `$82:A56E`, from a chase. Stack and all, see the header.

static uint32_t plays(Snakeoid* s);
static uint32_t lunges(Snakeoid* s);
static uint32_t goes_under(Snakeoid* s);
static uint32_t clears_track(Snakeoid* s);

static void tax(Snakeoid* s) { ldx(s, s->c->a); }
static void tay(Snakeoid* s) { ldy(s, s->c->a); }
static void txa(Snakeoid* s) { lda(s, s->c->x); }
static void dex(Snakeoid* s) { ldx(s, (uint16_t)(s->c->x - 1)); }
static void iny(Snakeoid* s) { ldy(s, (uint16_t)(s->c->y + 1)); }

// `BIT` of a word of the direct page: N and V from its top bits, Z from what
// it shares with A.
static void bit_field(Snakeoid* s, uint16_t at) {
  PortCpu* c = s->c;
  const uint16_t v = field(s, at);
  c->p = (uint8_t)(c->p & ~(PORT_P_N | PORT_P_Z));
  if (negative(v)) c->p |= PORT_P_N;
  if ((c->a & v) == 0) c->p |= PORT_P_Z;
  set_v(c, (v & 0x4000u) != 0);
  s->k->v_known = true;
}

// A word written through the data bank where a record of its body would be,
// which is `$FFFF` when it bit from under them: the cartridge takes the low
// byte, and the high byte crosses into the next bank's low WRAM.
static void set_bank_word(Snakeoid* s, uint16_t at, uint16_t v) {
  for (int i = 0; i < 2; i++) {
    const uint32_t a = (((uint32_t)SNAKEOID_BANK << 16) + at + (uint32_t)i);
    if ((a & 0xffffu) < 0x2000u)
      wram_w8(s->w, a & 0xffffu, (uint8_t)(i ? v >> 8 : v));
    else if ((a & 0xffffu) >= 0x8000u)
      s->k->rom_bytes++;
  }
}

// ...and read.
static uint16_t bank_word(Snakeoid* s, uint16_t at) {
  for (int i = 0; i < 2; i++)
    if ((uint16_t)(at + i) >= 0x8000u) s->k->rom_bytes++;
  return data(s, at);
}

// `LDA ($01,S),Y`: a word of the script whose address is on top of the
// stack, in its own bank.
static uint16_t script_word(const Snakeoid* s) {
  const uint16_t list = wram_r16(s->w, (uint16_t)(s->c->s + 1));
  return data(s, (uint16_t)(list + s->c->y));
}

// `JSL vbl_queue_a_remove` at `at`: the job whose slot is in A taken out. X
// is the slot, A nought, and N and Z the count's.
static void unqueues(Snakeoid* s, uint16_t at) {
  PortCpu* c = s->c;
  jsl(s, at);
  vbl_queue_a_remove(s->w, c->a);
  rtl(s);
  s->k->unqueued++;
  c->x = c->a;
  c->a = 0;
  set_nz16(c, wram_r16(s->w, W_VBL_QUEUE_A_COUNT));
}

// `$82:A214`, by `JSR` at `at`: could it come up where it is?
static bool ground_blocked(Snakeoid* s, uint16_t at) {
  PortCpu* c = s->c;
  TerrainRegs r;
  jsr(s, at);
  terrain_blocked_wide_low_counted(s->w, c->x, c->y, &r, &s->k->wide);
  back(s);
  c->a = r.a;
  c->x = r.x;
  c->y = r.y;
  set_flags_pld(s, r.blocked);
  s->k->v_known = false;
  return r.blocked;
}

// `$82:A3BE`: where its plane is drawn, from where it came up.
static void places_plane(Snakeoid* s) {
  PortCpu* c = s->c;
  lda(s, data(s, W_SNAKEOID_PICTURE_X));
  add(s, data(s, W_SNAKEOID_UP_X));
  sub(s, data(s, W_SNAKEOID_SPAN_X));
  set_data(s, W_SNAKEOID_PLANE_X, c->a);
  lda(s, data(s, W_SNAKEOID_PICTURE_Y));
  add(s, data(s, W_SNAKEOID_UP_Y));
  sub(s, data(s, W_SNAKEOID_SPAN_Y));
  set_data(s, W_SNAKEOID_PLANE_Y, c->a);
  ran(s, SNAKEOID_RUN_A3BE);
  back(s);
}

// --- Its pictures --------------------------------------------------------------

// `$82:9AD2` and `$82:9AE3`: the plane's place off the picture's, by the way
// it faces, and the picture queued, or queued mirrored.
static uint32_t queues_picture(Snakeoid* s, bool mirrored) {
  lda(s, mirrored ? SNAKEOID_SPAN_MIRRORED_X : SNAKEOID_SPAN_PLAIN_X);
  set_data(s, W_SNAKEOID_SPAN_X, s->c->a);
  lda(s, SNAKEOID_SPAN_Y);
  set_data(s, W_SNAKEOID_SPAN_Y, s->c->a);
  if (mirrored) {
    PORT_COVER(snakeoid_picture_mirrored);
    ran(s, SNAKEOID_RUN_9AE3);
    return SNAKEOID_PICTURE_FLIP_PC;
  }
  PORT_COVER(snakeoid_picture_up);
  ran(s, SNAKEOID_RUN_9AD2);
  return SNAKEOID_PICTURE_PC;
}

// `$82:9AF4`: the picture in A. Its far address for the upload, in page
// nought's `$28`, and its offset on the plane, mirrored with it.
static uint32_t puts_picture(Snakeoid* s) {
  PortCpu* c = s->c;
  doubled(s);
  doubled(s);
  tax(s);
  lda(s, data(s, (uint16_t)(SNAKEOID_PICTURES + c->x)));
  set_data(s, BOSS_BG_DP_SRC, c->a);
  lda(s, data(s, (uint16_t)(SNAKEOID_PICTURES + 2 + c->x)));
  set_data(s, BOSS_BG_DP_SRC_BANK, c->a);
  lda(s, data(s, (uint16_t)(SNAKEOID_PICTURE_AT + c->x)));
  bit_field(s, SNAKEOID_DP_FACING);
  ran(s, SNAKEOID_RUN_9AF4);
  if (!took(s, !flag(c, PORT_P_N))) {
    negated(s);
    ran(s, SNAKEOID_RUN_9B0A);
  }
  set_data(s, W_SNAKEOID_PICTURE_X, c->a);
  lda(s, data(s, (uint16_t)(SNAKEOID_PICTURE_AT + 2 + c->x)));
  set_data(s, W_SNAKEOID_PICTURE_Y, c->a);
  lda(s, field(s, SNAKEOID_DP_FACING));
  ran(s, SNAKEOID_RUN_9B0E);
  if (!took(s, c->a != 0)) {
    jsr(s, 0x9b1b);
    ran(s, SNAKEOID_RUN_9B1B);
    return queues_picture(s, false);
  }
  jsr(s, 0x9b20);
  ran(s, SNAKEOID_RUN_9B20);
  return queues_picture(s, true);
}

// `$82:8113`, by the `JSL` at `at`: the layer blanked, by a job of its own.
static uint32_t blanks(Snakeoid* s, uint16_t at, SnakeoidRun call) {
  PORT_COVER(snakeoid_blanks);
  jsl(s, at);
  ran(s, call);
  lda(s, SNAKEOID_BLANK_SOURCE);
  set_data(s, W_BOSS_BG_ONE_SRC, s->c->a);
  lda(s, SNAKEOID_BLANK_BANK);
  set_data(s, W_BOSS_BG_ONE_BANK, s->c->a);
  lda(s, SNAKEOID_BLANK_VRAM);
  set_data(s, W_BOSS_BG_ONE_VRAM, s->c->a);
  lda(s, SNAKEOID_BLANK_BYTES);
  set_data(s, W_BOSS_BG_ONE_BYTES, s->c->a);
  lda(s, BOSS_BG_CLEAR_JOB_PC & 0xffffu);
  ldy(s, SNAKEOID_BANK);
  ran(s, SNAKEOID_RUN_8113);
  return SNAKEOID_BLANK_QUEUE_PC;
}

// `$82:A392`: the picture it wants put up, if it is not up and the uploads
// are through.
static uint32_t picks_picture(Snakeoid* s) {
  PortCpu* c = s->c;
  jsl(s, 0xa392);
  lda(s, data(s, W_BG_DMA_CURSOR));
  ran(s, SNAKEOID_RUN_80E0);
  rtl(s);
  ran(s, SNAKEOID_RUN_A392);
  if (took(s, c->a != 0)) {
    PORT_COVER(snakeoid_uploads_busy);
    return returns(s);
  }
  lda(s, field(s, SNAKEOID_DP_PICTURE));
  cmp16(c, c->a, field(s, SNAKEOID_DP_SHOWN));
  ran(s, SNAKEOID_RUN_A398);
  if (took(s, zero(s))) return returns(s);
  set_field(s, SNAKEOID_DP_SHOWN, c->a);
  doubled(s);
  tax(s);
  ran(s, SNAKEOID_RUN_A39E);
  if (took(s, c->x == 0)) return blanks(s, 0xa3ab, SNAKEOID_RUN_A3AB);
  lda(s, data(s, (uint16_t)(SNAKEOID_PICTURE_OF + c->x)));
  jsr(s, 0xa3a7);
  ran(s, SNAKEOID_RUN_A3A4);
  return puts_picture(s);
}

// `$82:A033`: its flash. Two frames after a hit its colours go back, and a
// hit since the last turns them, and both records, to the flash's.
static uint32_t flashes(Snakeoid* s) {
  PortCpu* c = s->c;
  lda(s, field(s, SNAKEOID_DP_FLASH));
  ran(s, SNAKEOID_RUN_A033);
  if (took(s, zero(s))) {
    bit_field(s, SNAKEOID_DP_HIT);
    ran(s, SNAKEOID_RUN_A05C);
    if (took(s, !flag(c, PORT_P_N))) return returns(s);
    PORT_COVER(snakeoid_flashes);
    set_field(s, SNAKEOID_DP_HIT, 0);
    lda(s, SNAKEOID_FLASH_FRAMES);
    set_field(s, SNAKEOID_DP_FLASH, c->a);
    ldy(s, field(s, SNAKEOID_DP_RECORD));
    lda(s, (uint16_t)(data(s, c->y) | ACTOR_ATTR_SET));
    set_data(s, c->y, c->a);
    ldy(s, field(s, SNAKEOID_DP_BODY));
    lda(s, (uint16_t)(bank_word(s, c->y) | ACTOR_ATTR_SET));
    set_bank_word(s, c->y, c->a);
    lda(s, SNAKEOID_FLASH_COLOURS);
    ldy(s, SNAKEOID_COLOURS_BANK);
    ran(s, SNAKEOID_RUN_A060);
    return SNAKEOID_FLASH_PC;
  }
  count_down(s, SNAKEOID_DP_FLASH);
  ran(s, SNAKEOID_RUN_A037);
  if (took(s, !zero(s))) return returns(s);
  PORT_COVER(snakeoid_unflashes);
  ldy(s, field(s, SNAKEOID_DP_RECORD));
  lda(s, (uint16_t)(data(s, c->y) & ~ACTOR_ATTR_SET));
  set_data(s, c->y, c->a);
  ldy(s, field(s, SNAKEOID_DP_BODY));
  lda(s, (uint16_t)(bank_word(s, c->y) & ~ACTOR_ATTR_SET));
  set_bank_word(s, c->y, c->a);
  lda(s, SNAKEOID_COLOURS);
  ldy(s, SNAKEOID_COLOURS_BANK);
  ran(s, SNAKEOID_RUN_A03B);
  return SNAKEOID_UNFLASH_PC;
}

// `$82:A3DB`, by `JSR`: a frame of its picture.
static uint32_t shows(Snakeoid* s) {
  jsr(s, 0xa3db);
  ran(s, SNAKEOID_RUN_A3DB);
  return picks_picture(s);
}

// ...after the picture, its flash.
static uint32_t shows_flash(Snakeoid* s) {
  jsr(s, 0xa3de);
  ran(s, SNAKEOID_RUN_A3DE);
  return flashes(s);
}

// ...and the plane's place, with the `RTS`.
static uint32_t shown(Snakeoid* s) {
  jsr(s, 0xa3e1);
  places_plane(s);
  ran(s, SNAKEOID_RUN_A3E1);
  return goes_back(s);
}

// `$82:A321`, by `JSR`: the box about the point in X and Y, to be told.
static uint32_t tells_box(Snakeoid* s) {
  PortCpu* c = s->c;
  txa(s);
  sub(s, 0x0018);
  set_data(s, W_SNAKEOID_BOX, c->a);
  add(s, 0x0030);
  set_data(s, (uint16_t)(W_SNAKEOID_BOX + 2), c->a);
  lda(s, c->y);
  sub(s, 0x0018);
  set_data(s, (uint16_t)(W_SNAKEOID_BOX + 4), c->a);
  add(s, 0x0038);
  set_data(s, (uint16_t)(W_SNAKEOID_BOX + 6), c->a);
  lda(s, SNAKEOID_BOX_KIND);
  set_data(s, (uint16_t)(W_SNAKEOID_BOX + 8), c->a);
  ran(s, SNAKEOID_RUN_A321);
  return SNAKEOID_TELL_PC;
}

// --- The two scripts ------------------------------------------------------------

// `$82:9B57`: the next picture of the script, for how many frames, or the
// script is over.
static uint32_t plays_next(Snakeoid* s) {
  PortCpu* c = s->c;
  ldy(s, field(s, SNAKEOID_DP_COUNT));
  lda(s, script_word(s));
  ran(s, SNAKEOID_RUN_9B57);
  if (took(s, c->a == 0)) {
    pla(s);
    ran(s, SNAKEOID_RUN_9B81);
    return goes_back(s);
  }
  ran(s, SNAKEOID_RUN_9B5D);
  if (!took(s, !negative(c->a))) {
    one_more(s);
    ran(s, SNAKEOID_RUN_9B5F);
  }
  set_field(s, SNAKEOID_DP_PICTURE, c->a);
  iny(s);
  iny(s);
  lda(s, script_word(s));
  iny(s);
  iny(s);
  set_field(s, SNAKEOID_DP_COUNT, c->y);
  pha(s);
  jsr(s, 0x9b6b);
  ran(s, SNAKEOID_RUN_9B60);
  return shows(s);
}

// `$82:9B6E`: a frame less of this picture, and sleep, or the next.
static uint32_t plays_on(Snakeoid* s) {
  pla(s);
  one_less(s);
  ran(s, SNAKEOID_RUN_9B6E);
  if (took(s, negative(s->c->a))) {
    took(s, true);
    ran(s, SNAKEOID_RUN_9B7F);
    return plays_next(s);
  }
  pha(s);
  lda(s, 0x0001);
  ran(s, SNAKEOID_RUN_9B72);
  return SNAKEOID_PLAY_WAIT_PC;
}

// `$82:9B54`, by `JSR`: the script in A.
static uint32_t plays(Snakeoid* s) {
  pha(s);
  set_field(s, SNAKEOID_DP_COUNT, 0);
  ran(s, SNAKEOID_RUN_9B54);
  return plays_next(s);
}

// `$82:9B7A`: a frame of a picture is over.
static uint32_t played(Snakeoid* s) {
  jsr(s, 0x9b7a);
  places_plane(s);
  took(s, true);
  ran(s, SNAKEOID_RUN_9B7A);
  return plays_on(s);
}

// `$82:9B8E`: the next of its lunge's pictures, on its body's first record,
// or the lunge is over and that record shows nothing.
static uint32_t lunges_next(Snakeoid* s) {
  PortCpu* c = s->c;
  ldy(s, field(s, SNAKEOID_DP_COUNT));
  lda(s, script_word(s));
  ran(s, SNAKEOID_RUN_9B8E);
  if (took(s, c->a == 0)) {
    PORT_COVER(snakeoid_lunge_ends);
    pla(s);
    ldx(s, field(s, SNAKEOID_DP_BODY));
    set_data(s, (uint16_t)(c->x + ACTOR_META), 0);
    set_data(s, (uint16_t)(c->x + ACTOR_META_BANK), 0);
    ran(s, SNAKEOID_RUN_9BD8);
    return goes_back(s);
  }
  ldx(s, field(s, SNAKEOID_DP_BODY));
  set_data(s, (uint16_t)(c->x + ACTOR_META), c->a);
  iny(s);
  iny(s);
  lda(s, script_word(s));
  iny(s);
  iny(s);
  set_field(s, SNAKEOID_DP_COUNT, c->y);
  pha(s);
  jsr(s, 0x9ba2);
  ran(s, SNAKEOID_RUN_9B94);
  return shows(s);
}

// `$82:9BA5`: a frame less of this one.
static uint32_t lunges_on(Snakeoid* s) {
  pla(s);
  one_less(s);
  ran(s, SNAKEOID_RUN_9BA5);
  if (took(s, negative(s->c->a))) {
    took(s, true);
    ran(s, SNAKEOID_RUN_9BD6);
    return lunges_next(s);
  }
  pha(s);
  lda(s, 0x0001);
  ran(s, SNAKEOID_RUN_9BA9);
  return SNAKEOID_LUNGE_WAIT_PC;
}

// `$82:9B83`, by `JSR`: the lunge in A.
static uint32_t lunges(Snakeoid* s) {
  PortCpu* c = s->c;
  pha(s);
  ldy(s, field(s, SNAKEOID_DP_BODY));
  lda(s, SNAKEOID_META_BANK);
  set_data(s, (uint16_t)(c->y + ACTOR_META_BANK), c->a);
  set_field(s, SNAKEOID_DP_COUNT, 0);
  ran(s, SNAKEOID_RUN_9B83);
  return lunges_next(s);
}

// `$82:9BB1`: a frame of its lunge is over. On a frame whose count has no
// bit of `$2F`, the box at its mouth is told.
static uint32_t lunged(Snakeoid* s) {
  PortCpu* c = s->c;
  jsr(s, 0x9bb1);
  places_plane(s);
  lda(s, (uint16_t)(data(s, W_FRAME_COUNT) & SNAKEOID_LUNGE_FRAMES));
  ran(s, SNAKEOID_RUN_9BB1);
  if (took(s, c->a != 0)) return lunges_on(s);
  PORT_COVER(snakeoid_lunge_tells);
  ldx(s, field(s, SNAKEOID_DP_SIDE));
  ldy(s, field(s, SNAKEOID_DP_BODY));
  lda(s, data(s, (uint16_t)(c->y + ACTOR_X)));
  add(s, data(s, (uint16_t)(SNAKEOID_LUNGE_BOX + c->x)));
  pha(s);
  lda(s, data(s, (uint16_t)(c->y + ACTOR_Y)));
  add(s, data(s, (uint16_t)(SNAKEOID_LUNGE_BOX + 2 + c->x)));
  plx(s);
  tay(s);
  jsr(s, 0x9bd1);
  ran(s, SNAKEOID_RUN_9BBC);
  return tells_box(s);
}

// --- Coming up ---------------------------------------------------------------------

// `$82:9C56`, by `JSR`: unless it is dying, four records under its mouth
// that hurt, the last first.
static uint32_t hurts(Snakeoid* s) {
  PortCpu* c = s->c;
  lda(s, field(s, SNAKEOID_DP_DYING));
  ran(s, SNAKEOID_RUN_9C56);
  if (took(s, c->a != 0)) {
    PORT_COVER(snakeoid_rises_dying);
    return returns(s);
  }
  lda(s, SNAKEOID_HURTS_DOWN);
  add(s, field(s, SNAKEOID_DP_Y));
  set_field(s, SNAKEOID_DP_GAP, c->a);
  ldx(s, SNAKEOID_HURT_LAST);
  set_field(s, SNAKEOID_DP_COUNT, c->x);
  ran(s, SNAKEOID_RUN_9C5A);
  return SNAKEOID_HURT_RECORD_PC;
}

// `$82:9C6B`: the record in A is one that hurts.
static uint32_t hurt_made(Snakeoid* s) {
  PortCpu* c = s->c;
  ldx(s, field(s, SNAKEOID_DP_COUNT));
  set_field(s, (uint16_t)(SNAKEOID_DP_HURTS + c->x), c->a);
  ldy(s, c->x);
  tax(s);
  lda(s, data(s, (uint16_t)(SNAKEOID_HURTS_X + c->y)));
  add(s, field(s, SNAKEOID_DP_X));
  set_data(s, (uint16_t)(c->x + ACTOR_X), c->a);
  set_data(s, (uint16_t)(c->x + ACTOR_Z), 0);
  lda(s, field(s, SNAKEOID_DP_GAP));
  set_data(s, (uint16_t)(c->x + ACTOR_Y), c->a);
  set_data(s, (uint16_t)(c->x + ACTOR_META), 0);
  set_data(s, (uint16_t)(c->x + ACTOR_META_BANK), 0);
  lda(s, SNAKEOID_HURT_KIND);
  set_data(s, (uint16_t)(c->x + ACTOR_COLLIDE_ID), c->a);
  lda(s, data(s, W_SCHED_CUR_TASK));
  set_data(s, (uint16_t)(c->x + ACTOR_THREAD), c->a);
  lda(s, data(s, (uint16_t)(c->x + ACTOR_FLAGS)));
  lda(s, (uint16_t)(c->a | ACTOR_DRAW));
  set_data(s, (uint16_t)(c->x + ACTOR_FLAGS), c->a);
  ldx(s, field(s, SNAKEOID_DP_COUNT));
  dex(s);
  dex(s);
  set_field(s, SNAKEOID_DP_COUNT, c->x);
  ran(s, SNAKEOID_RUN_9C6B);
  if (took(s, !negative(c->x))) return SNAKEOID_HURT_RECORD_PC;
  return returns(s);
}

// `$82:9F0D`: the piece of track before, if there is one.
static bool clears_before(Snakeoid* s) {
  txa(s);
  sub(s, SNAKEOID_TRACK_STRIDE);
  tax(s);
  ran(s, SNAKEOID_RUN_9F0D);
  return took(s, !negative(s->c->x));
}

// `$82:9EF9`: each piece of its track from the one in X down is freed.
static uint32_t clears_pieces(Snakeoid* s) {
  PortCpu* c = s->c;
  do {
    lda(s, field(s, (uint16_t)(SNAKEOID_DP_TRACK + c->x)));
    cmp16(c, c->a, SNAKEOID_NO_PIECE);
    ran(s, SNAKEOID_RUN_9EF9);
    if (!took(s, zero(s))) {
      PORT_COVER(snakeoid_clears_piece);
      phx(s);
      ran(s, SNAKEOID_RUN_9F00);
      return SNAKEOID_CLEAR_FREE_PC;
    }
  } while (clears_before(s));
  return returns(s);
}

// `$82:9EF1`, by `JSR`: its track, all four pieces, and a piece due on its
// next step.
static uint32_t clears_track(Snakeoid* s) {
  lda(s, 0x0001);
  set_field(s, SNAKEOID_DP_DROP_IN, s->c->a);
  ldx(s, (SNAKEOID_TRACK_PIECES - 1) * SNAKEOID_TRACK_STRIDE);
  ran(s, SNAKEOID_RUN_9EF1);
  return clears_pieces(s);
}

// `$82:9F05`: a piece is freed, and its place is empty.
static uint32_t cleared(Snakeoid* s) {
  PortCpu* c = s->c;
  plx(s);
  lda(s, SNAKEOID_NO_PIECE);
  set_field(s, (uint16_t)(SNAKEOID_DP_TRACK + c->x), c->a);
  set_field(s, (uint16_t)(SNAKEOID_DP_TRACK_AGE + c->x), 0);
  ran(s, SNAKEOID_RUN_9F05);
  if (clears_before(s)) return clears_pieces(s);
  return returns(s);
}

// `$82:9C20`: it waits for room in the queue of jobs, then queues the shake.
static uint32_t rise_waits(Snakeoid* s) {
  PortCpu* c = s->c;
  lda(s, data(s, W_VBL_QUEUE_A_COUNT));
  cmp16(c, c->a, SNAKEOID_QUEUE_ROOM);
  ran(s, SNAKEOID_RUN_9C20);
  if (took(s, !carry(s))) {
    lda(s, SNAKEOID_SHAKE_JOB_PC & 0xffffu);
    ldy(s, SNAKEOID_BANK);
    ran(s, SNAKEOID_RUN_9C31);
    return SNAKEOID_RISE_QUEUE_PC;
  }
  PORT_COVER(snakeoid_queue_full);
  lda(s, 0x0001);
  ran(s, SNAKEOID_RUN_9C28);
  return SNAKEOID_RISE_WAIT_PC;
}

// `$82:9C16`: the four records made. The uploads' queue is emptied, the
// plane placed, and its track cleared.
static uint32_t rise_hurts(Snakeoid* s) {
  jsl(s, 0x9c16);
  set_data(s, W_BG_DMA_CURSOR, 0);
  ran(s, SNAKEOID_RUN_80E4);
  rtl(s);
  jsr(s, 0x9c1a);
  places_plane(s);
  jsr(s, 0x9c1d);
  ran(s, SNAKEOID_RUN_9C16);
  return clears_track(s);
}

// `$82:9C02`, by `JSR`: it comes up.
static uint32_t comes_up(Snakeoid* s) {
  PortCpu* c = s->c;
  PORT_COVER(snakeoid_comes_up);
  lda(s, 0xffff);
  wram_w16(s->w, W_SNAKEOID_UP, c->a);
  lda(s, field(s, SNAKEOID_DP_X));
  set_data(s, W_SNAKEOID_UP_X, c->a);
  lda(s, field(s, SNAKEOID_DP_Y));
  set_data(s, W_SNAKEOID_UP_Y, c->a);
  jsr(s, 0x9c13);
  ran(s, SNAKEOID_RUN_9C02);
  return hurts(s);
}

// `$82:9C49`: it has come up. The shake is taken out, and the box about it
// told.
static uint32_t has_come_up(Snakeoid* s) {
  pla(s);
  unqueues(s, 0x9c4a);
  ldx(s, field(s, SNAKEOID_DP_X));
  ldy(s, field(s, SNAKEOID_DP_Y));
  jsr(s, 0x9c52);
  ran(s, SNAKEOID_RUN_9C49);
  return tells_box(s);
}

// --- Going under ---------------------------------------------------------------------

// `$82:9D3C`: the record that hurts at `$34`.
static uint32_t frees_hurt(Snakeoid* s) {
  ldx(s, field(s, SNAKEOID_DP_COUNT));
  lda(s, field(s, (uint16_t)(SNAKEOID_DP_HURTS + s->c->x)));
  ran(s, SNAKEOID_RUN_9D3C);
  return SNAKEOID_HURT_FREE_PC;
}

// `$82:9D44`: one freed, and the one before.
static uint32_t hurt_freed(Snakeoid* s) {
  ldx(s, field(s, SNAKEOID_DP_COUNT));
  dex(s);
  dex(s);
  set_field(s, SNAKEOID_DP_COUNT, s->c->x);
  ran(s, SNAKEOID_RUN_9D44);
  if (took(s, !negative(s->c->x))) return frees_hurt(s);
  return returns(s);
}

// `$82:9CEC`, by `JSR`: it waits for room in the queue, as it did coming up,
// and queues the shake's other copy.
static uint32_t goes_under(Snakeoid* s) {
  PortCpu* c = s->c;
  lda(s, data(s, W_VBL_QUEUE_A_COUNT));
  cmp16(c, c->a, SNAKEOID_QUEUE_ROOM);
  ran(s, SNAKEOID_RUN_9CEC);
  if (took(s, !carry(s))) {
    PORT_COVER(snakeoid_goes_under);
    lda(s, SNAKEOID_SINK_JOB_PC & 0xffffu);
    ldy(s, SNAKEOID_BANK);
    ran(s, SNAKEOID_RUN_9CFD);
    return SNAKEOID_SINK_QUEUE_PC;
  }
  lda(s, 0x0001);
  ran(s, SNAKEOID_RUN_9CF4);
  return SNAKEOID_SINK_WAIT_PC;
}

// `$82:9D0E`: its record no longer flashing, it sinks.
static uint32_t sinks(Snakeoid* s) {
  PortCpu* c = s->c;
  ldy(s, field(s, SNAKEOID_DP_RECORD));
  lda(s, (uint16_t)(data(s, c->y) & ~ACTOR_ATTR_SET));
  set_data(s, c->y, c->a);
  lda(s, SNAKEOID_SINKS);
  jsr(s, 0x9d1c);
  ran(s, SNAKEOID_RUN_9D0E);
  return plays(s);
}

// `$82:9D1F`: sunk. The shake is taken out, and a vblank waited for.
static uint32_t has_sunk(Snakeoid* s) {
  pla(s);
  unqueues(s, 0x9d20);
  ran(s, SNAKEOID_RUN_9D1F);
  return SNAKEOID_SINK_WAI_PC;
}

// `$82:9D25`: it is where it came up, and no Snakeoid is up.
static uint32_t sunk(Snakeoid* s) {
  PORT_COVER(snakeoid_sunk);
  lda(s, data(s, W_SNAKEOID_UP_X));
  set_field(s, SNAKEOID_DP_X, s->c->a);
  lda(s, data(s, W_SNAKEOID_UP_Y));
  set_field(s, SNAKEOID_DP_Y, s->c->a);
  lda(s, 0);
  wram_w16(s->w, W_SNAKEOID_UP, s->c->a);
  ran(s, SNAKEOID_RUN_9D25);
  return goes_back(s);
}

// --- Beside them ------------------------------------------------------------------

// `$82:A6A8`, by `JSR`: four records of its body, the last first.
static uint32_t grows_body(Snakeoid* s) {
  PortCpu* c = s->c;
  lda(s, SNAKEOID_BODY_LAST);
  set_field(s, SNAKEOID_DP_COUNT, c->a);
  lda(s, field(s, SNAKEOID_DP_SIDE));
  add(s, SNAKEOID_BODY_LAST_ROW);
  set_field(s, SNAKEOID_DP_GAP, c->a);
  ran(s, SNAKEOID_RUN_A6A8);
  return SNAKEOID_BODY_RECORD_PC;
}

// `$82:A6B9`: the record in A is a piece of its body, by its row of the
// table for the side.
static uint32_t body_made(Snakeoid* s) {
  PortCpu* c = s->c;
  ldx(s, field(s, SNAKEOID_DP_COUNT));
  set_field(s, (uint16_t)(SNAKEOID_DP_BODY + c->x), c->a);
  tax(s);
  set_data(s, (uint16_t)(c->x + ACTOR_Z), 0);
  set_data(s, (uint16_t)(c->x + ACTOR_META), 0);
  set_data(s, (uint16_t)(c->x + ACTOR_META_BANK), 0);
  ldy(s, field(s, SNAKEOID_DP_GAP));
  lda(s, data(s, (uint16_t)(SNAKEOID_BODY_AT + c->y)));
  add(s, field(s, SNAKEOID_DP_X));
  set_data(s, (uint16_t)(c->x + ACTOR_X), c->a);
  lda(s, data(s, (uint16_t)(SNAKEOID_BODY_AT + 2 + c->y)));
  add(s, field(s, SNAKEOID_DP_Y));
  set_data(s, (uint16_t)(c->x + ACTOR_Y), c->a);
  lda(s, SNAKEOID_BODY_KIND);
  set_data(s, (uint16_t)(c->x + ACTOR_COLLIDE_ID), c->a);
  lda(s, data(s, W_SCHED_CUR_TASK));
  set_data(s, (uint16_t)(c->x + ACTOR_THREAD), c->a);
  lda(s, SNAKEOID_ATTR);
  set_data(s, (uint16_t)(c->x + ACTOR_ATTR), c->a);
  lda(s, ACTOR_DRAW);
  lda(s, (uint16_t)(c->a | data(s, (uint16_t)(c->x + ACTOR_FLAGS))));
  set_data(s, (uint16_t)(c->x + ACTOR_FLAGS), c->a);
  lda(s, field(s, SNAKEOID_DP_GAP));
  sub(s, SNAKEOID_BODY_ROW);
  set_field(s, SNAKEOID_DP_GAP, c->a);
  lda(s, field(s, SNAKEOID_DP_COUNT));
  sub(s, SNAKEOID_TRACK_STRIDE);
  set_field(s, SNAKEOID_DP_COUNT, c->a);
  ran(s, SNAKEOID_RUN_A6B9);
  if (took(s, !negative(c->a))) return SNAKEOID_BODY_RECORD_PC;
  return returns(s);
}

// `$82:A660`: its body made. Its own record and the body's first take the
// side's place and flags, and its own the side's picture.
static uint32_t body_placed(Snakeoid* s) {
  PortCpu* c = s->c;
  PORT_COVER(snakeoid_body_made);
  lda(s, 0x0001);
  set_field(s, SNAKEOID_DP_COUNT, c->a);
  ldx(s, field(s, SNAKEOID_DP_SIDE));
  ldy(s, field(s, SNAKEOID_DP_RECORD));
  lda(s, data(s, (uint16_t)(SNAKEOID_BODY_FLAGS + 2 + c->x)));
  set_data(s, (uint16_t)(c->y + ACTOR_META), c->a);
  lda(s, SNAKEOID_META_BANK);
  set_data(s, (uint16_t)(c->y + ACTOR_META_BANK), c->a);
  ran(s, SNAKEOID_RUN_A660);
  do {
    ldx(s, field(s, SNAKEOID_DP_SIDE));
    lda(s, SNAKEOID_BODY_KIND);
    set_data(s, (uint16_t)(c->y + ACTOR_COLLIDE_ID), c->a);
    lda(s, data(s, (uint16_t)(SNAKEOID_BODY_AT + c->x)));
    add(s, field(s, SNAKEOID_DP_X));
    set_data(s, (uint16_t)(c->y + ACTOR_X), c->a);
    lda(s, data(s, (uint16_t)(SNAKEOID_BODY_AT + 2 + c->x)));
    add(s, field(s, SNAKEOID_DP_Y));
    set_data(s, (uint16_t)(c->y + ACTOR_Y), c->a);
    lda(s, (uint16_t)(data(s, (uint16_t)(c->y + ACTOR_FLAGS)) &
                      SNAKEOID_BODY_CLEARS));
    set_data(s, (uint16_t)(c->y + ACTOR_FLAGS), c->a);
    lda(s, data(s, (uint16_t)(SNAKEOID_BODY_FLAGS + c->x)));
    lda(s, (uint16_t)(c->a | data(s, (uint16_t)(c->y + ACTOR_FLAGS))));
    set_data(s, (uint16_t)(c->y + ACTOR_FLAGS), c->a);
    ldy(s, field(s, SNAKEOID_DP_BODY));
    ran(s, SNAKEOID_RUN_A675);
  } while (took(s, !negative(count_down(s, SNAKEOID_DP_COUNT))));
  return returns(s);
}

// A frame of its mouth working: every eighth, the other of its two
// pictures. `low` is from below, where `$3E` follows the picture.
static uint32_t strikes(Snakeoid* s, bool low) {
  PortCpu* c = s->c;
  lda(s, field(s, SNAKEOID_DP_COUNT));
  lda(s, c->a & 0x0007u);
  ran(s, low ? SNAKEOID_RUN_A5BC : SNAKEOID_RUN_A615);
  if (!took(s, c->a != 0)) {
    const uint16_t first = low ? SNAKEOID_STRIKE_LOW_A : SNAKEOID_STRIKE_HIGH_A;
    ldy(s, field(s, SNAKEOID_DP_RECORD));
    lda(s, data(s, (uint16_t)(c->y + ACTOR_META)));
    cmp16(c, c->a, first);
    ran(s, low ? SNAKEOID_RUN_A5C3 : SNAKEOID_RUN_A61C);
    if (!took(s, zero(s))) {
      if (low) {
        lda(s, SNAKEOID_STRIKE_LOW_PICTURE);
        set_field(s, SNAKEOID_DP_PICTURE, c->a);
        ldy(s, field(s, SNAKEOID_DP_RECORD));
      }
      lda(s, first);
      took(s, true);
      ran(s, low ? SNAKEOID_RUN_A5CD : SNAKEOID_RUN_A626);
    } else if (low) {
      lda(s, SNAKEOID_STRIKE_LOW_PICTURE + 1);
      set_field(s, SNAKEOID_DP_PICTURE, c->a);
      lda(s, SNAKEOID_STRIKE_LOW_B);
      ran(s, SNAKEOID_RUN_A5D9);
    } else {
      ldy(s, field(s, SNAKEOID_DP_RECORD));
      lda(s, SNAKEOID_STRIKE_HIGH_B);
      ran(s, SNAKEOID_RUN_A62B);
    }
    set_data(s, (uint16_t)(c->y + ACTOR_META), c->a);
    ran(s, low ? SNAKEOID_RUN_A5E1 : SNAKEOID_RUN_A630);
  }
  jsr(s, low ? 0xa5e4 : 0xa633);
  ran(s, low ? SNAKEOID_RUN_A5E4 : SNAKEOID_RUN_A633);
  return shows(s);
}

// `$82:A5B2` and `$82:A60B`, by `JSR`: 80 frames of its mouth working.
static uint32_t starts_strike(Snakeoid* s, bool low) {
  PORT_COVER_IF(low, snakeoid_strikes_low, snakeoid_strikes_high);
  lda(s, low ? SNAKEOID_STRIKE_LOW_PICTURE : SNAKEOID_STRIKE_HIGH_PICTURE);
  set_field(s, SNAKEOID_DP_PICTURE, s->c->a);
  lda(s, SNAKEOID_STRIKE_FRAMES);
  set_field(s, SNAKEOID_DP_COUNT, s->c->a);
  ran(s, low ? SNAKEOID_RUN_A5B2 : SNAKEOID_RUN_A60B);
  return strikes(s, low);
}

// `$82:A5E7` and `$82:A636`: the picture is up. By a draw, on the second
// of its two pictures, its mouth stops working early. Then sleep a frame.
static uint32_t struck(Snakeoid* s, bool low) {
  PortCpu* c = s->c;
  draw(s, low ? 0xa5e7 : 0xa636);
  cmp16(c, c->a, SNAKEOID_STRIKE_ENDS_UNDER);
  ran(s, low ? SNAKEOID_RUN_A5E7 : SNAKEOID_RUN_A636);
  if (!took(s, carry(s))) {
    if (low) {
      lda(s, field(s, SNAKEOID_DP_PICTURE));
      cmp16(c, c->a, SNAKEOID_STRIKE_LOW_PICTURE);
    } else {
      ldy(s, field(s, SNAKEOID_DP_RECORD));
      lda(s, data(s, (uint16_t)(c->y + ACTOR_META)));
      cmp16(c, c->a, SNAKEOID_STRIKE_HIGH_B);
    }
    ran(s, low ? SNAKEOID_RUN_A5F0 : SNAKEOID_RUN_A63F);
    // From below it stops on the first picture; from above, on the second.
    if (!took(s, !zero(s))) {
      PORT_COVER(snakeoid_strike_ends);
      set_field(s, SNAKEOID_DP_COUNT, 0);
      ran(s, low ? SNAKEOID_RUN_A5F7 : SNAKEOID_RUN_A649);
    }
  }
  lda(s, 0x0001);
  ran(s, low ? SNAKEOID_RUN_A5F9 : SNAKEOID_RUN_A64B);
  return low ? SNAKEOID_STRIKE_LOW_WAIT_PC : SNAKEOID_STRIKE_HIGH_WAIT_PC;
}

// `$82:A600` and `$82:A652`: a frame is over. Another, or its lunge.
static uint32_t strike_ticked(Snakeoid* s, bool low) {
  const uint16_t left = count_down(s, SNAKEOID_DP_COUNT);
  ran(s, low ? SNAKEOID_RUN_A600 : SNAKEOID_RUN_A652);
  if (took(s, !negative(left))) return strikes(s, low);
  lda(s, low ? SNAKEOID_LUNGES_LOW : SNAKEOID_LUNGES_HIGH);
  jsr(s, low ? 0xa607 : 0xa659);
  ran(s, low ? SNAKEOID_RUN_A604 : SNAKEOID_RUN_A656);
  return lunges(s);
}

static uint32_t struck_low(Snakeoid* s) { return strike_ticked(s, true); }
static uint32_t struck_high(Snakeoid* s) { return strike_ticked(s, false); }

// `$82:A709`, by `JSR`: a mound again, where it is.
static void settles(Snakeoid* s) {
  PortCpu* c = s->c;
  ldy(s, field(s, SNAKEOID_DP_RECORD));
  lda(s, SNAKEOID_MOUND_PICTURE);
  set_data(s, (uint16_t)(c->y + ACTOR_META), c->a);
  lda(s, SNAKEOID_META_BANK);
  set_data(s, (uint16_t)(c->y + ACTOR_META_BANK), c->a);
  lda(s, 0);
  set_data(s, (uint16_t)(c->y + ACTOR_COLLIDE_ID), c->a);
  lda(s, field(s, SNAKEOID_DP_X));
  set_data(s, (uint16_t)(c->y + ACTOR_X), c->a);
  lda(s, field(s, SNAKEOID_DP_Y));
  set_data(s, (uint16_t)(c->y + ACTOR_Y), c->a);
  lda(s, (uint16_t)(data(s, (uint16_t)(c->y + ACTOR_FLAGS)) &
                    ~ACTOR_SORT_FIRST));
  set_data(s, (uint16_t)(c->y + ACTOR_FLAGS), c->a);
  ran(s, SNAKEOID_RUN_A709);
  back(s);
}

// `$82:A59F`: its mouth has done. A mound again, and it goes under.
static uint32_t has_struck(Snakeoid* s) {
  jsr(s, 0xa59f);
  settles(s);
  jsr(s, 0xa5a2);
  ran(s, SNAKEOID_RUN_A59F);
  return goes_under(s);
}

// `$82:A590`: its body made, its mouth works by the side it bites from.
static uint32_t strikes_by_side(Snakeoid* s) {
  PortCpu* c = s->c;
  lda(s, field(s, SNAKEOID_DP_SIDE));
  cmp16(c, c->a, SNAKEOID_SIDE_LOW);
  ran(s, SNAKEOID_RUN_A590);
  if (!took(s, !carry(s))) {
    jsr(s, 0xa597);
    ran(s, SNAKEOID_RUN_A597);
    return starts_strike(s, true);
  }
  jsr(s, 0xa59c);
  ran(s, SNAKEOID_RUN_A59C);
  return starts_strike(s, false);
}

// --- The bite --------------------------------------------------------------------

// Where it may not come up: a draw, and a leap at a player under 35.
static uint32_t cannot_bite(Snakeoid* s, uint16_t at, SnakeoidRun run,
                            SnakeoidRun jump) {
  PORT_COVER(snakeoid_cannot_bite);
  draw(s, at);
  cmp16(s->c, s->c->a, SNAKEOID_BITE_LEAPS_UNDER);
  ran(s, run);
  if (took(s, carry(s))) return returns(s);
  ran(s, jump);
  return leaps_at_a_player(s);
}

// It turns to face who it bites: `$44` negative if they are not to its
// left, and then its pictures are mirrored.
static void faces_them(Snakeoid* s, SnakeoidRun run, SnakeoidRun turned) {
  PortCpu* c = s->c;
  set_field(s, SNAKEOID_DP_FACING, 0);
  ldx(s, field(s, SNAKEOID_DP_TARGET));
  lda(s, data(s, (uint16_t)(c->x + ACTOR_X)));
  cmp16(c, c->a, field(s, SNAKEOID_DP_X));
  ran(s, run);
  if (!took(s, !carry(s))) {
    count_down(s, SNAKEOID_DP_FACING);
    ran(s, turned);
  }
}

// `$82:A538`: under them.
static uint32_t bites_under(Snakeoid* s) {
  PortCpu* c = s->c;
  ldx(s, field(s, SNAKEOID_DP_X));
  ldy(s, field(s, SNAKEOID_DP_Y));
  const bool blocked = ground_blocked(s, 0xa53c);
  ran(s, SNAKEOID_RUN_A538);
  if (took(s, blocked))
    return cannot_bite(s, 0xa562, SNAKEOID_RUN_A562, SNAKEOID_RUN_A56B);
  lda(s, wram_r16(s->w, W_SNAKEOID_UP));
  ran(s, SNAKEOID_RUN_A541);
  if (took(s, c->a != 0))
    return cannot_bite(s, 0xa562, SNAKEOID_RUN_A562, SNAKEOID_RUN_A56B);
  faces_them(s, SNAKEOID_RUN_A547, SNAKEOID_RUN_A552);
  jsr(s, 0xa554);
  ran(s, SNAKEOID_RUN_A554);
  return comes_up(s);
}

// `$82:A56E`: beside them.
static uint32_t bites_beside(Snakeoid* s) {
  PortCpu* c = s->c;
  lda(s, wram_r16(s->w, W_SNAKEOID_UP));
  ran(s, SNAKEOID_RUN_A56E);
  if (took(s, c->a != 0))
    return cannot_bite(s, 0xa5a6, SNAKEOID_RUN_A5A6, SNAKEOID_RUN_A5AF);
  ldx(s, field(s, SNAKEOID_DP_X));
  ldy(s, field(s, SNAKEOID_DP_Y));
  const bool blocked = ground_blocked(s, 0xa578);
  ran(s, SNAKEOID_RUN_A574);
  if (took(s, blocked))
    return cannot_bite(s, 0xa5a6, SNAKEOID_RUN_A5A6, SNAKEOID_RUN_A5AF);
  faces_them(s, SNAKEOID_RUN_A57D, SNAKEOID_RUN_A588);
  jsr(s, 0xa58a);
  ran(s, SNAKEOID_RUN_A58A);
  return comes_up(s);
}

// What an `RTS` of its bite goes on with, by the address it pulls, or 0 for
// one that is not its bite's.
static uint32_t bite_goes_back(Snakeoid* s, uint16_t pulled) {
  switch (pulled) {
    case SNAKEOID_RET_UNDER_RISES:
      lda(s, SNAKEOID_BITE_WAIT);
      ran(s, SNAKEOID_RUN_A557);
      return SNAKEOID_BITE_WAIT_PC;
    case SNAKEOID_RET_UNDER_SINKS:
    case SNAKEOID_RET_BESIDE_SINKS:
    case SNAKEOID_RET_RISE_TELLS:
    case SNAKEOID_RET_PUTS:
    case SNAKEOID_RET_PICTURE_FLIP:
    case SNAKEOID_RET_LOW_LUNGES:
    case SNAKEOID_RET_HIGH_LUNGES:
      return returns(s);
    case SNAKEOID_RET_BESIDE_RISES:
      jsr(s, 0xa58d);
      jsr(s, 0xa65d);
      ran(s, SNAKEOID_RUN_A58D);
      ran(s, SNAKEOID_RUN_A65D);
      return grows_body(s);
    case SNAKEOID_RET_BODY_MADE:
      return body_placed(s);
    case SNAKEOID_RET_BODY:
      return strikes_by_side(s);
    case SNAKEOID_RET_STRIKE_LOW:
      took(s, true);
      ran(s, SNAKEOID_RUN_A59A);
      return has_struck(s);
    case SNAKEOID_RET_STRIKE_HIGH:
      return has_struck(s);
    case SNAKEOID_RET_HURTS:
      return rise_hurts(s);
    case SNAKEOID_RET_RISE_CLEARS:
      return rise_waits(s);
    case SNAKEOID_RET_RISE_PLAYS:
      return has_come_up(s);
    case SNAKEOID_RET_SINK_FREES:
      jsr(s, 0x9d0b);
      ran(s, SNAKEOID_RUN_9D0B);
      return clears_track(s);
    case SNAKEOID_RET_SINK_CLEARS:
      return sinks(s);
    case SNAKEOID_RET_SINK_PLAYS:
      return has_sunk(s);
    case SNAKEOID_RET_LOW_SHOWS:
      return struck(s, true);
    case SNAKEOID_RET_HIGH_SHOWS:
      return struck(s, false);
    case SNAKEOID_RET_PLAY_SHOWS:
      return plays_on(s);
    case SNAKEOID_RET_LUNGE_SHOWS:
      return lunges_on(s);
    case SNAKEOID_RET_LUNGE_TELLS:
      took(s, true);
      ran(s, SNAKEOID_RUN_9BD4);
      return lunges_on(s);
    case SNAKEOID_RET_PICKS:
      return shows_flash(s);
    case SNAKEOID_RET_FLASHES:
      return shown(s);
    case SNAKEOID_RET_PICTURE:
      took(s, true);
      ran(s, SNAKEOID_RUN_9B1E);
      return returns(s);
  }
  return 0;
}

static uint32_t dies_waits(Snakeoid* s);
static uint32_t dies_up(Snakeoid* s);

// ...and of its death.
static uint32_t death_goes_back(Snakeoid* s, uint16_t pulled) {
  switch (pulled) {
    case SNAKEOID_RET_DIES_CLEARS:
      return dies_waits(s);
    case SNAKEOID_RET_DIES_RISES:
      return dies_up(s);
  }
  return 0;
}

// --- Where a stretch of its bite begins ----------------------------------------------

// `$82:9C2F` and `$82:9CFB`: a tick of waiting for room in the queue.
static uint32_t rise_waited(Snakeoid* s) {
  took(s, true);
  ran(s, SNAKEOID_RUN_9C2F);
  return rise_waits(s);
}

static uint32_t sink_waited(Snakeoid* s) {
  took(s, true);
  ran(s, SNAKEOID_RUN_9CFB);
  return goes_under(s);
}

// `$82:9C3B`: the shake queued, its slot in X kept, and its cry.
static uint32_t rise_queued(Snakeoid* s) {
  phx(s);
  lda(s, SNAKEOID_CRY_SFX);
  ran(s, SNAKEOID_RUN_9C3B);
  return SNAKEOID_CRY_PC;
}

// `$82:9C43`: the pictures it comes up by.
static uint32_t cried(Snakeoid* s) {
  lda(s, SNAKEOID_RISES);
  jsr(s, 0x9c46);
  ran(s, SNAKEOID_RUN_9C43);
  return plays(s);
}

// `$82:9D07`: the shake queued for its going under, and its four records
// freed.
static uint32_t sink_queued(Snakeoid* s) {
  phx(s);
  jsr(s, 0x9d08);
  ran(s, SNAKEOID_RUN_9D07);
  ldx(s, SNAKEOID_HURT_LAST);
  set_field(s, SNAKEOID_DP_COUNT, s->c->x);
  ran(s, SNAKEOID_RUN_9D37);
  return frees_hurt(s);
}

// `$82:A55E`: its six frames under them are over.
static uint32_t bitten(Snakeoid* s) {
  jsr(s, 0xa55e);
  ran(s, SNAKEOID_RUN_A55E);
  return goes_under(s);
}

static uint32_t dies_blanked(Snakeoid* s);

// `$82:8135`: the blanking job queued, and back by `RTL`: to its picture's
// `RTS`, or on in its death.
static uint32_t blank_queued(Snakeoid* s) {
  const uint16_t pulled = pull16(s->w, s->c);
  (void)pull8(s->w, s->c);
  ran(s, SNAKEOID_RUN_8135);
  if (pulled == SNAKEOID_RET_DIES_BLANKS) return dies_blanked(s);
  return returns(s);
}

// --- Its death --------------------------------------------------------------

// `$82:A901`: a frame's sleep.
static uint32_t dies_waits(Snakeoid* s) {
  lda(s, 0x0001);
  ran(s, SNAKEOID_RUN_A901);
  return SNAKEOID_DIES_WAIT_PC;
}

// `$82:A8FE`: its worth paid, and its track cleared.
static uint32_t dies_scored(Snakeoid* s) {
  jsr(s, 0xa8fe);
  ran(s, SNAKEOID_RUN_A8FE);
  return clears_track(s);
}

// `$82:A908`: it comes up when no other Snakeoid is.
static uint32_t dies_waited(Snakeoid* s) {
  lda(s, wram_r16(s->w, W_SNAKEOID_UP));
  ran(s, SNAKEOID_RUN_A908);
  if (took(s, s->c->a != 0)) {
    PORT_COVER(snakeoid_dies_waits);
    return dies_waits(s);
  }
  jsr(s, 0xa90e);
  ran(s, SNAKEOID_RUN_A90E);
  return comes_up(s);
}

// `$82:A911`: up, and four ticks' sleep.
static uint32_t dies_up(Snakeoid* s) {
  lda(s, SNAKEOID_DEATH_TICK);
  ran(s, SNAKEOID_RUN_A911);
  return SNAKEOID_DIES_UP_WAIT_PC;
}

// `$82:A924`: a tick of its death.
static uint32_t dies_ticks(Snakeoid* s) {
  lda(s, SNAKEOID_DEATH_TICK);
  ran(s, SNAKEOID_RUN_A924);
  return SNAKEOID_DIES_TICK_PC;
}

// `$82:A918`: no mosaic yet, and twenty ticks to go.
static uint32_t dies_counted(Snakeoid* s) {
  PORT_COVER(snakeoid_dies_up);
  lda(s, 0);
  wram_w16(s->w, W_MOSAIC_SIZE, s->c->a);
  lda(s, SNAKEOID_DEATH_TICKS);
  set_field(s, SNAKEOID_DP_COUNT, s->c->a);
  ran(s, SNAKEOID_RUN_A918);
  return dies_ticks(s);
}

// `$82:A98B`: its own colours again.
static uint32_t dies_unflashes(Snakeoid* s) {
  lda(s, SNAKEOID_COLOURS);
  ldy(s, SNAKEOID_COLOURS_BANK);
  ran(s, SNAKEOID_RUN_A98B);
  return SNAKEOID_DIES_UNFLASH_PC;
}

// `$82:A956`: on an even tick a piece of it bursts off, from the place the
// table has for that tick; on an odd one its colours come back.
static uint32_t dies_bursts(Snakeoid* s) {
  PortCpu* c = s->c;
  lda(s, (uint16_t)(field(s, SNAKEOID_DP_COUNT) & 0x000fu));
  shares_bit(s, 0x0001);
  ran(s, SNAKEOID_RUN_A956);
  if (took(s, !zero(s))) {
    PORT_COVER(snakeoid_dies_unflashes);
    return dies_unflashes(s);
  }
  PORT_COVER(snakeoid_dies_bursts);
  doubled(s);
  doubled(s);
  tax(s);
  lda(s, data(s, (uint16_t)(SNAKEOID_BURSTS + c->x)));
  add(s, field(s, SNAKEOID_DP_X));
  set_field(s, SNAKEOID_DP_PLACE_X, c->a);
  lda(s, data(s, (uint16_t)(SNAKEOID_BURSTS + 2 + c->x)));
  add(s, field(s, SNAKEOID_DP_Y));
  set_field(s, SNAKEOID_DP_PLACE_Y, c->a);
  lda(s, SNAKEOID_PIECE);
  ldy(s, SNAKEOID_PIECE_BANK);
  ran(s, SNAKEOID_RUN_A960);
  return SNAKEOID_DIES_SPAWN_PC;
}

// `$82:A932`: the mosaic's job queued when the queue has room, or a frame's
// sleep.
static uint32_t dies_makes_room(Snakeoid* s) {
  PortCpu* c = s->c;
  lda(s, data(s, W_VBL_QUEUE_A_COUNT));
  cmp16(c, c->a, SNAKEOID_QUEUE_ROOM);
  ran(s, SNAKEOID_RUN_A932);
  if (took(s, !carry(s))) {
    lda(s, SNAKEOID_MOSAIC_JOB_PC & 0xffffu);
    ldy(s, SNAKEOID_BANK);
    ran(s, SNAKEOID_RUN_A943);
    return SNAKEOID_DIES_MOSAIC_QUEUE_PC;
  }
  PORT_COVER(snakeoid_dies_queue_full);
  lda(s, 0x0001);
  ran(s, SNAKEOID_RUN_A93A);
  return SNAKEOID_DIES_ROOM_WAIT_PC;
}

// `$82:A92B`: a tick over. From the fifth on, the mosaic grows first.
static uint32_t dies_ticked(Snakeoid* s) {
  PortCpu* c = s->c;
  lda(s, field(s, SNAKEOID_DP_COUNT));
  cmp16(c, c->a, SNAKEOID_DEATH_MOSAIC_UNDER);
  ran(s, SNAKEOID_RUN_A92B);
  if (took(s, carry(s))) return dies_bursts(s);
  PORT_COVER(snakeoid_dies_mosaic);
  return dies_makes_room(s);
}

// `$82:A941`.
static uint32_t dies_room_waited(Snakeoid* s) {
  took(s, true);
  ran(s, SNAKEOID_RUN_A941);
  return dies_makes_room(s);
}

// `$82:A94D`: the mosaic a step coarser.
static uint32_t dies_mosaic_queued(Snakeoid* s) {
  lda(s, (uint16_t)(wram_r16(s->w, W_MOSAIC_SIZE) + 1));
  wram_w16(s->w, W_MOSAIC_SIZE, s->c->a);
  ran(s, SNAKEOID_RUN_A94D);
  return dies_bursts(s);
}

// `$82:A97D`: a piece begun, and its colours flash.
static uint32_t dies_burst(Snakeoid* s) {
  lda(s, SNAKEOID_DEATH_COLOURS);
  ldy(s, SNAKEOID_DEATH_COLOURS_BANK);
  ran(s, SNAKEOID_RUN_A97D);
  return SNAKEOID_DIES_FLASH_PC;
}

// `$82:A987`: a tick less, which from an even one is never the last.
static uint32_t dies_flashed(Snakeoid* s) {
  count_down(s, SNAKEOID_DP_COUNT);
  ran(s, SNAKEOID_RUN_A987);
  if (took(s, !zero(s))) return dies_ticks(s);
  return dies_unflashes(s);
}

// `$82:A995`: a tick less, and after the last its layer blanked.
static uint32_t dies_unflashed(Snakeoid* s) {
  count_down(s, SNAKEOID_DP_COUNT);
  ran(s, SNAKEOID_RUN_A995);
  if (took(s, !zero(s))) return dies_ticks(s);
  return blanks(s, 0xa999, SNAKEOID_RUN_A999);
}

// `$82:A99D`: the mosaic's end queued.
static uint32_t dies_blanked(Snakeoid* s) {
  lda(s, SNAKEOID_MOSAIC_OFF_JOB_PC & 0xffffu);
  ldy(s, SNAKEOID_BANK);
  ran(s, SNAKEOID_RUN_A99D);
  return SNAKEOID_DIES_END_QUEUE_PC;
}

// `$82:A9A7`: its record freed.
static uint32_t dies_ended(Snakeoid* s) {
  lda(s, field(s, SNAKEOID_DP_RECORD));
  ran(s, SNAKEOID_RUN_A9A7);
  return SNAKEOID_DIES_FREE_PC;
}

// `$82:A9AD`: what it leaves, put down a little above where it was.
static uint32_t dies_freed(Snakeoid* s) {
  ldx(s, field(s, SNAKEOID_DP_X));
  lda(s, field(s, SNAKEOID_DP_Y));
  sub(s, SNAKEOID_LEAVES_UP);
  tay(s);
  lda(s, SNAKEOID_LEAVES);
  ran(s, SNAKEOID_RUN_A9AD);
  return SNAKEOID_DIES_LEAVE_PC;
}

// `$82:A9BD`: none up, its weight off the level's load, and its thread's
// `RTL`, to `thread_exit`. A load that would go below nothing stops the ROM
// on a branch to itself, which the guard leaves to the ROM.
static uint32_t dies_left(Snakeoid* s) {
  PortCpu* c = s->c;
  PORT_COVER(snakeoid_died);
  lda(s, 0);
  wram_w16(s->w, W_SNAKEOID_UP, c->a);
  lda(s, data(s, W_SPAWN_LOAD));
  sub(s, SNAKEOID_LOAD);
  set_data(s, W_SPAWN_LOAD, c->a);
  ran(s, SNAKEOID_RUN_A9BD);
  rtl(s);
  ran(s, SNAKEOID_RUN_A9D0);
  return RECORD_END_EXITED_PC;
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
  const uint32_t bite = bite_goes_back(s, pulled);
  if (bite != 0) return bite;
  const uint32_t death = death_goes_back(s, pulled);
  if (death != 0) return death;
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

// --- Its start ---------------------------------------------------------------

// `$82:A87E`: where it was given, nobody to go for and no side to bite from,
// and its record asked for.
static uint32_t takes_its_place(Snakeoid* s) {
  PORT_COVER(snakeoid_placed);
  set_data(s, W_SNAKEOID_SPAN_X, SNAKEOID_SPAN_X);
  set_data(s, W_SNAKEOID_SPAN_Y, SNAKEOID_SPAN_Y);
  set_field(s, SNAKEOID_DP_X, field(s, 0x00));
  set_field(s, SNAKEOID_DP_Y, field(s, 0x02));
  set_field(s, SNAKEOID_DP_COUNT, 0);
  set_field(s, 0x44, 0);
  set_field(s, SNAKEOID_DP_DYING, 0);
  set_field(s, 0x4a, 0);
  set_field(s, 0x4c, 0);
  set_field(s, SNAKEOID_DP_PAUSE, 0);
  lda(s, 0xffff);
  set_field(s, SNAKEOID_DP_SIDE, s->c->a);
  set_field(s, 0x52, s->c->a);
  jsr(s, 0xa8a5);
  ran(s, SNAKEOID_RUN_A87E);
  return SNAKEOID_START_RECORD_PC;
}

// `$82:A82F`, by `JSR` from its start: the first of a level sets the
// figure's colours.
static uint32_t starts(Snakeoid* s) {
  lda(s, data(s, W_SNAKEOID_COLOURED));
  ran(s, SNAKEOID_RUN_A82F);
  if (took(s, s->c->a != 0)) return takes_its_place(s);
  PORT_COVER(snakeoid_coloured);
  lda(s, 0xffff);
  set_data(s, W_SNAKEOID_COLOURED, s->c->a);
  lda(s, 0);
  wram_w16(s->w, W_SNAKEOID_UP, s->c->a);
  lda(s, SNAKEOID_COLOURS);
  ldy(s, SNAKEOID_COLOURS_BANK);
  ran(s, SNAKEOID_RUN_A834);
  return SNAKEOID_COLOURS_PC;
}

// `$82:A8BB` and `$82:A8C3`: its thread, by the ground it churns.
static uint32_t enters(Snakeoid* s, uint16_t ground) {
  PORT_COVER(snakeoid_entered);
  lda(s, ground);
  set_field(s, SNAKEOID_DP_GROUND, ground);
  ran(s, ground == 0 ? SNAKEOID_RUN_A8BB : SNAKEOID_RUN_A8C3);
  lda(s, data(s, W_SPAWN_LOAD));
  add(s, SNAKEOID_LOAD);
  set_data(s, W_SPAWN_LOAD, s->c->a);
  jsr(s, 0xa8d5);
  ran(s, SNAKEOID_RUN_A8CB);
  return starts(s);
}

static uint32_t enters_a(Snakeoid* s) { return enters(s, 0); }
static uint32_t enters_b(Snakeoid* s) { return enters(s, SNAKEOID_GROUND_B); }

// `$82:A84B`: the sprites' row too.
static uint32_t coloured(Snakeoid* s) {
  lda(s, SNAKEOID_COLOURS);
  ldy(s, SNAKEOID_COLOURS_BANK);
  ran(s, SNAKEOID_RUN_A84B);
  return SNAKEOID_SPRITE_COLOURS_PC;
}

// `$82:A855`: the background's next upload, by `boss_bg_queue_one`.
static uint32_t sprite_coloured(Snakeoid* s) {
  set_data(s, W_BOSS_BG_ONE_SRC, SNAKEOID_UPLOAD_SOURCE);
  set_data(s, W_BOSS_BG_ONE_BANK, SNAKEOID_UPLOAD_BANK);
  set_data(s, W_BOSS_BG_ONE_BYTES, SNAKEOID_UPLOAD_BYTES);
  lda(s, SNAKEOID_UPLOAD_VRAM);
  set_data(s, W_BOSS_BG_ONE_VRAM, s->c->a);
  ran(s, SNAKEOID_RUN_A855);
  return SNAKEOID_UPLOAD_PC;
}

// `$82:A871`: a tick. Where the upload comes back to, this is a thread
// argument (`port/thread_stretches.h`); it is here for the wait's loop.
static uint32_t queued(Snakeoid* s) {
  lda(s, 0x0001);
  ran(s, SNAKEOID_RUN_A871);
  return SNAKEOID_QUEUE_WAIT_PC;
}

// `$82:A878`: `JSL $82:80E0`, which loads how far the background's uploads
// have got. Another tick while there are any.
static uint32_t queue_woke(Snakeoid* s) {
  jsl(s, 0xa878);
  lda(s, data(s, W_BG_DMA_CURSOR));
  ran(s, SNAKEOID_RUN_80E0);
  rtl(s);
  ran(s, SNAKEOID_RUN_A878);
  ran(s, SNAKEOID_RUN_A87C);
  if (took(s, s->c->a != 0)) {
    PORT_COVER(snakeoid_queue_waited);
    return queued(s);
  }
  return takes_its_place(s);
}

// `$82:A7F7`: its record, at its place and drawn, then its track cleared
// (`$82:9D8F`), its count, and its handler.
static uint32_t made(Snakeoid* s) {
  PORT_COVER(snakeoid_made);
  PortCpu* c = s->c;
  const uint16_t record = c->a;
  set_field(s, SNAKEOID_DP_RECORD, record);
  ldx(s, record);
  set_data(s, (uint16_t)(record + ACTOR_X), field(s, 0x00));
  set_data(s, (uint16_t)(record + ACTOR_Z), 0);
  set_data(s, (uint16_t)(record + ACTOR_Y), field(s, 0x02));
  set_data(s, (uint16_t)(record + ACTOR_META), SNAKEOID_MOUND_PICTURE);
  set_data(s, (uint16_t)(record + ACTOR_META_BANK), SNAKEOID_META_BANK);
  set_data(s, (uint16_t)(record + ACTOR_COLLIDE_ID), 0);
  set_data(s, (uint16_t)(record + ACTOR_THREAD), data(s, W_SCHED_CUR_TASK));
  set_data(s, (uint16_t)(record + ACTOR_ATTR), SNAKEOID_ATTR);
  lda(s, (uint16_t)(data(s, (uint16_t)(record + ACTOR_FLAGS)) | ACTOR_DRAW));
  set_data(s, (uint16_t)(record + ACTOR_FLAGS), c->a);
  back(s);
  ran(s, SNAKEOID_RUN_A7F7);

  jsr(s, 0xa8a8);
  ran(s, SNAKEOID_RUN_A8A8);
  lda(s, 0x0001);
  set_field(s, SNAKEOID_DP_DROP_IN, c->a);
  ldx(s, (SNAKEOID_TRACK_PIECES - 1) * SNAKEOID_TRACK_STRIDE);
  ldy(s, SNAKEOID_NO_PIECE);
  ran(s, SNAKEOID_RUN_9D8F);
  do {
    set_field(s, (uint16_t)(SNAKEOID_DP_TRACK + c->x), c->y);
    set_field(s, (uint16_t)(SNAKEOID_DP_TRACK_AGE + c->x), 0);
    lda(s, c->x);
    sub(s, SNAKEOID_TRACK_STRIDE);
    ldx(s, c->a);
    ran(s, SNAKEOID_RUN_9D9A);
  } while (took(s, !negative(c->x)));
  rts(s);

  lda(s, SNAKEOID_COUNT);
  set_field(s, SNAKEOID_DP_COUNTED, c->a);
  c->a = SNAKEOID_HANDLER;
  ldy(s, SNAKEOID_HANDLER_BANK);
  thread_set_handler(s->w, c);
  s->k->handlers_set++;
  ran(s, SNAKEOID_RUN_A8AB);
  return returns(s);
}

// --- The stretches ----------------------------------------------------------

static const struct {
  uint32_t pc;
  uint32_t (*run)(Snakeoid* s);
} STRETCHES[] = {
    {SNAKEOID_READY_PC, returns},   {SNAKEOID_WAITED_PC, waited},
    {SNAKEOID_WAKES_PC, wakes},     {SNAKEOID_FREED_PC, freed},
    {SNAKEOID_DROPPED_PC, dropped}, {SNAKEOID_ROSE_PC, rose},
    {SNAKEOID_ENTER_PC, enters_a},  {SNAKEOID_ENTER_B_PC, enters_b},
    {SNAKEOID_COLOURED_PC, coloured},
    {SNAKEOID_SPRITE_COLOURED_PC, sprite_coloured},
    {SNAKEOID_QUEUE_WOKE_PC, queue_woke},
    {SNAKEOID_MADE_PC, made},
    {SNAKEOID_HURT_MADE_PC, hurt_made},
    {SNAKEOID_BODY_MADE_PC, body_made},
    {SNAKEOID_CLEARED_PC, cleared},
    {SNAKEOID_HURT_FREED_PC, hurt_freed},
    {SNAKEOID_RISE_WAITED_PC, rise_waited},
    {SNAKEOID_RISE_QUEUED_PC, rise_queued},
    {SNAKEOID_CRIED_PC, cried},
    {SNAKEOID_PLAYED_PC, played},
    {SNAKEOID_LUNGED_PC, lunged},
    {SNAKEOID_PICTURE_UP_PC, returns},
    {SNAKEOID_PICTURE_FLIPPED_PC, returns},
    {SNAKEOID_BLANKED_PC, blank_queued},
    {SNAKEOID_UNFLASHED_PC, returns},
    {SNAKEOID_FLASHED_PC, returns},
    {SNAKEOID_TOLD_PC, returns},
    {SNAKEOID_BITTEN_PC, bitten},
    {SNAKEOID_STRUCK_LOW_PC, struck_low},
    {SNAKEOID_STRUCK_HIGH_PC, struck_high},
    {SNAKEOID_SINK_WAITED_PC, sink_waited},
    {SNAKEOID_SINK_QUEUED_PC, sink_queued},
    {SNAKEOID_SUNK_PC, sunk},
    {SNAKEOID_DIES_SCORED_PC, dies_scored},
    {SNAKEOID_DIES_WAITED_PC, dies_waited},
    {SNAKEOID_DIES_UP_PC, dies_counted},
    {SNAKEOID_DIES_TICKED_PC, dies_ticked},
    {SNAKEOID_DIES_ROOM_PC, dies_room_waited},
    {SNAKEOID_DIES_MOSAIC_PC, dies_mosaic_queued},
    {SNAKEOID_DIES_BURST_PC, dies_burst},
    {SNAKEOID_DIES_FLASHED_PC, dies_flashed},
    {SNAKEOID_DIES_UNFLASHED_PC, dies_unflashed},
    {SNAKEOID_DIES_ENDED_PC, dies_ended},
    {SNAKEOID_DIES_FREED_PC, dies_freed},
    {SNAKEOID_DIES_LEFT_PC, dies_left},
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

// --- The stack in its bite -------------------------------------------------------
//
// Each return address of its bite is under the state's own, or under one of
// a few others with a word or two between: a grammar, read from the top.

static bool one_of(uint16_t v, uint16_t a, uint16_t b) { return v == a || v == b; }

static bool state_at(const Wram* w, uint16_t s, int at) {
  return stacked(w, s, at) == SNAKEOID_RET_STATE;
}

// The slot of the shake it queued, kept for taking it out.
static bool slot_at(const Wram* w, uint16_t s, int at) {
  return vbl_queue_a_remove_supported(stacked(w, s, at));
}

// Its thread's own end, `thread_exit`'s return, which is under its death.
static bool exit_at(const Wram* w, uint16_t s, int at) {
  return stacked(w, s, at) == (RECORD_END_EXITED_PC & 0xffffu) - 1 &&
         wram_r8(w, (uint16_t)(s + 3 + at)) == (RECORD_END_EXITED_PC >> 16);
}

static bool rising_at(const Wram* w, uint16_t s, int at) {
  const uint16_t ret = stacked(w, s, at);
  if (ret == SNAKEOID_RET_DIES_RISES) return exit_at(w, s, at + 2);
  return one_of(ret, SNAKEOID_RET_UNDER_RISES, SNAKEOID_RET_BESIDE_RISES) &&
         state_at(w, s, at + 2);
}

static bool sinking_at(const Wram* w, uint16_t s, int at) {
  return one_of(stacked(w, s, at), SNAKEOID_RET_UNDER_SINKS,
                SNAKEOID_RET_BESIDE_SINKS) &&
         state_at(w, s, at + 2);
}

// A script's frame: the frames left of a picture, the script's address, and
// the return of the `JSR` that began it. `lunge` is `$82:9B83`'s.
static bool script_at(const Wram* w, uint16_t s, int at, bool lunge) {
  const uint16_t list = stacked(w, s, at + 2);
  switch (stacked(w, s, at + 4)) {
    case SNAKEOID_RET_RISE_PLAYS:
      return !lunge && list == SNAKEOID_RISES && slot_at(w, s, at + 6) &&
             rising_at(w, s, at + 8);
    case SNAKEOID_RET_SINK_PLAYS:
      return !lunge && list == SNAKEOID_SINKS && slot_at(w, s, at + 6) &&
             sinking_at(w, s, at + 8);
    case SNAKEOID_RET_LOW_LUNGES:
      return lunge && list == SNAKEOID_LUNGES_LOW &&
             stacked(w, s, at + 6) == SNAKEOID_RET_STRIKE_LOW &&
             state_at(w, s, at + 8);
    case SNAKEOID_RET_HIGH_LUNGES:
      return lunge && list == SNAKEOID_LUNGES_HIGH &&
             stacked(w, s, at + 6) == SNAKEOID_RET_STRIKE_HIGH &&
             state_at(w, s, at + 8);
  }
  return false;
}

static bool bite_known(const Wram* w, uint16_t s, int at);

// The return of a `JSR $A3DB`, a frame of its picture.
static bool shows_at(const Wram* w, uint16_t s, int at) {
  switch (stacked(w, s, at)) {
    case SNAKEOID_RET_PLAY_SHOWS:
    case SNAKEOID_RET_LUNGE_SHOWS:
    case SNAKEOID_RET_LOW_SHOWS:
    case SNAKEOID_RET_HIGH_SHOWS:
      return bite_known(w, s, at);
  }
  return false;
}

static bool bite_known(const Wram* w, uint16_t s, int at) {
  switch (stacked(w, s, at)) {
    case SNAKEOID_RET_UNDER_RISES:
    case SNAKEOID_RET_UNDER_SINKS:
    case SNAKEOID_RET_BESIDE_RISES:
    case SNAKEOID_RET_BODY:
    case SNAKEOID_RET_STRIKE_LOW:
    case SNAKEOID_RET_STRIKE_HIGH:
    case SNAKEOID_RET_BESIDE_SINKS:
      return state_at(w, s, at + 2);
    case SNAKEOID_RET_HURTS:
    case SNAKEOID_RET_RISE_CLEARS:
    case SNAKEOID_RET_RISE_TELLS:
      return rising_at(w, s, at + 2);
    case SNAKEOID_RET_RISE_PLAYS:
      return slot_at(w, s, at + 2) && rising_at(w, s, at + 4);
    case SNAKEOID_RET_SINK_FREES:
    case SNAKEOID_RET_SINK_CLEARS:
    case SNAKEOID_RET_SINK_PLAYS:
      return slot_at(w, s, at + 2) && sinking_at(w, s, at + 4);
    case SNAKEOID_RET_BODY_MADE:
      return stacked(w, s, at + 2) == SNAKEOID_RET_BODY &&
             state_at(w, s, at + 4);
    case SNAKEOID_RET_LOW_SHOWS:
    case SNAKEOID_RET_LOW_LUNGES:
      return stacked(w, s, at + 2) == SNAKEOID_RET_STRIKE_LOW &&
             state_at(w, s, at + 4);
    case SNAKEOID_RET_HIGH_SHOWS:
    case SNAKEOID_RET_HIGH_LUNGES:
      return stacked(w, s, at + 2) == SNAKEOID_RET_STRIKE_HIGH &&
             state_at(w, s, at + 4);
    case SNAKEOID_RET_PLAY_SHOWS:
      return script_at(w, s, at + 2, false);
    case SNAKEOID_RET_LUNGE_SHOWS:
    case SNAKEOID_RET_LUNGE_TELLS:
      return script_at(w, s, at + 2, true);
    case SNAKEOID_RET_PICKS:
    case SNAKEOID_RET_FLASHES:
      return shows_at(w, s, at + 2);
    case SNAKEOID_RET_PUTS:
      return stacked(w, s, at + 2) == SNAKEOID_RET_PICKS &&
             shows_at(w, s, at + 4);
    case SNAKEOID_RET_PICTURE:
    case SNAKEOID_RET_PICTURE_FLIP:
      return stacked(w, s, at + 2) == SNAKEOID_RET_PUTS &&
             stacked(w, s, at + 4) == SNAKEOID_RET_PICKS &&
             shows_at(w, s, at + 6);
    case SNAKEOID_RET_DIES_CLEARS:
    case SNAKEOID_RET_DIES_RISES:
      return exit_at(w, s, at + 2);
  }
  return false;
}

// Is the stack of a stretch of its bite that begins at `pc` one it knows?
static bool bite_stack_known(const Wram* w, uint32_t pc, uint16_t s) {
  const uint16_t top = stacked(w, s, 0);
  switch (pc) {
    case SNAKEOID_HURT_MADE_PC:
      return top == SNAKEOID_RET_HURTS && bite_known(w, s, 0);
    case SNAKEOID_BODY_MADE_PC:
      return top == SNAKEOID_RET_BODY_MADE && bite_known(w, s, 0);
    case SNAKEOID_CLEARED_PC:
      // The piece it pushed, and under it the return of the clearing.
      return piece_known(top) &&
             (one_of(stacked(w, s, 2), SNAKEOID_RET_RISE_CLEARS,
                     SNAKEOID_RET_SINK_CLEARS) ||
              stacked(w, s, 2) == SNAKEOID_RET_DIES_CLEARS) &&
             bite_known(w, s, 2);
    case SNAKEOID_HURT_FREED_PC:
      return top == SNAKEOID_RET_SINK_FREES && bite_known(w, s, 0);
    case SNAKEOID_RISE_WAITED_PC:
    case SNAKEOID_RISE_QUEUED_PC:
      return rising_at(w, s, 0);
    case SNAKEOID_CRIED_PC:
      return slot_at(w, s, 0) && rising_at(w, s, 2);
    case SNAKEOID_PLAYED_PC:
      return script_at(w, s, 0, false);
    case SNAKEOID_LUNGED_PC:
      return script_at(w, s, 0, true);
    case SNAKEOID_PICTURE_UP_PC:
      return top == SNAKEOID_RET_PICTURE && bite_known(w, s, 0);
    case SNAKEOID_PICTURE_FLIPPED_PC:
      return top == SNAKEOID_RET_PICTURE_FLIP && bite_known(w, s, 0);
    case SNAKEOID_BLANKED_PC:
      // A `JSL`'s three bytes, and under them `$82:A392`'s return, or its
      // thread's end.
      if (wram_r8(w, (uint16_t)(s + 3)) != SNAKEOID_BANK) return false;
      if (top == SNAKEOID_RET_DIES_BLANKS) return exit_at(w, s, 3);
      return top == SNAKEOID_RET_BLANKS &&
             stacked(w, s, 3) == SNAKEOID_RET_PICKS && shows_at(w, s, 5);
    case SNAKEOID_UNFLASHED_PC:
    case SNAKEOID_FLASHED_PC:
      return top == SNAKEOID_RET_FLASHES && bite_known(w, s, 0);
    case SNAKEOID_TOLD_PC:
      return one_of(top, SNAKEOID_RET_RISE_TELLS, SNAKEOID_RET_LUNGE_TELLS) &&
             bite_known(w, s, 0);
    case SNAKEOID_BITTEN_PC:
      return state_at(w, s, 0);
    case SNAKEOID_STRUCK_LOW_PC:
      return top == SNAKEOID_RET_STRIKE_LOW && state_at(w, s, 2);
    case SNAKEOID_STRUCK_HIGH_PC:
      return top == SNAKEOID_RET_STRIKE_HIGH && state_at(w, s, 2);
    case SNAKEOID_SINK_WAITED_PC:
    case SNAKEOID_SINK_QUEUED_PC:
    case SNAKEOID_SUNK_PC:
      return sinking_at(w, s, 0);
    case SNAKEOID_DIES_SCORED_PC:
    case SNAKEOID_DIES_WAITED_PC:
    case SNAKEOID_DIES_UP_PC:
    case SNAKEOID_DIES_TICKED_PC:
    case SNAKEOID_DIES_ROOM_PC:
    case SNAKEOID_DIES_MOSAIC_PC:
    case SNAKEOID_DIES_BURST_PC:
    case SNAKEOID_DIES_FLASHED_PC:
    case SNAKEOID_DIES_UNFLASHED_PC:
    case SNAKEOID_DIES_ENDED_PC:
    case SNAKEOID_DIES_FREED_PC:
      return exit_at(w, s, 0);
    case SNAKEOID_DIES_LEFT_PC: {
      // ...and the load must not go below nothing.
      const uint16_t load = wram_r16(w, W_SPAWN_LOAD);
      return exit_at(w, s, 0) && load >= SNAKEOID_LOAD &&
             ((load - SNAKEOID_LOAD) & 0x8000u) == 0;
    }
  }
  return false;
}

bool snakeoid_regs_known(uint32_t pc, uint16_t a, uint16_t x) {
  switch (pc) {
    case SNAKEOID_HURT_MADE_PC:
    case SNAKEOID_BODY_MADE_PC:
      return a < 0x1f00;
    case SNAKEOID_RISE_QUEUED_PC:
    case SNAKEOID_SINK_QUEUED_PC:
      return vbl_queue_a_remove_supported(x);
  }
  return true;
}

bool snakeoid_stack_known(const Wram* w, uint32_t pc, uint16_t s) {
  switch (pc) {
    case SNAKEOID_WAKES_PC:
    case SNAKEOID_ENTER_PC:
    case SNAKEOID_ENTER_B_PC:
      return true;
    case SNAKEOID_COLOURED_PC:
    case SNAKEOID_SPRITE_COLOURED_PC:
    case SNAKEOID_QUEUE_WOKE_PC:
      return stacked(w, s, 0) == SNAKEOID_RET_READY;
    case SNAKEOID_MADE_PC:
      return stacked(w, s, 0) == SNAKEOID_RET_RECORD &&
             stacked(w, s, 2) == SNAKEOID_RET_READY;
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
  return bite_stack_known(w, pc, s);
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
