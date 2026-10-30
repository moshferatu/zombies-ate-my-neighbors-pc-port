// Frankenstein's monster, awake -- see port/frankenstein.h.

#include "port/frankenstein.h"

#include "port/coverage.h"
#include "port/rng.h"
#include "port/thread.h"

typedef struct {
  Wram* w;
  const Rom* rom;
  PortCpu* c;
  FrankensteinWork* k;
} Frankenstein;

// --- The machine, as these stretches use it ---------------------------------

static uint16_t field(const Frankenstein* s, uint16_t at) {
  return wram_r16(s->w, (uint16_t)(s->c->d + at));
}

static void set_field(Frankenstein* s, uint16_t at, uint16_t v) {
  wram_w16(s->w, (uint16_t)(s->c->d + at), v);
}

// A word read through the data bank, which is its own: a table of its own,
// a word of a record, or low memory.
static uint16_t data(const Frankenstein* s, uint16_t at) {
  return bus_r16(s->w, s->rom, (uint32_t)FRANKENSTEIN_CODE_BANK << 16 | at);
}

// ...and one written, which is always to a record or to low memory.
static void set_data(Frankenstein* s, uint16_t at, uint16_t v) {
  wram_w16(s->w, at, v);
}

static void ran(Frankenstein* s, FrankensteinRun run) { s->k->runs[run]++; }

// A branch: counted if it is taken.
static bool took(Frankenstein* s, bool taken) {
  if (taken) s->k->taken++;
  return taken;
}

static bool negative(uint16_t v) { return (v & 0x8000u) != 0; }
static bool zero(const Frankenstein* s) { return flag(s->c, PORT_P_Z); }
static bool carry(const Frankenstein* s) { return flag(s->c, PORT_P_C); }

static void lda(Frankenstein* s, uint16_t v) {
  s->c->a = v;
  set_nz16(s->c, v);
}

static void ldx(Frankenstein* s, uint16_t v) {
  s->c->x = v;
  set_nz16(s->c, v);
}

static void ldy(Frankenstein* s, uint16_t v) {
  s->c->y = v;
  set_nz16(s->c, v);
}

static void doubled(Frankenstein* s) { s->c->a = asl16(s->c, s->c->a); }
static void one_more(Frankenstein* s) { lda(s, (uint16_t)(s->c->a + 1)); }
static void one_less(Frankenstein* s) { lda(s, (uint16_t)(s->c->a - 1)); }

// `AND #`.
static void masked(Frankenstein* s, uint16_t bits) { lda(s, s->c->a & bits); }

// `CLC : ADC` and `SEC : SBC`, to what is in A; and `ADC` with the carry as
// it stands.
static void add(Frankenstein* s, uint16_t v) {
  set_c(s->c, false);
  s->k->v_known = true;
  s->c->a = adc16(s->c, s->c->a, v);
}

static void sub(Frankenstein* s, uint16_t v) {
  set_c(s->c, true);
  s->k->v_known = true;
  s->c->a = sbc16(s->c, s->c->a, v);
}

static void cmp(Frankenstein* s, uint16_t v) { cmp16(s->c, s->c->a, v); }

// `DEC` and `INC` of a word of the direct page.
static uint16_t count_down(Frankenstein* s, uint16_t at) {
  const uint16_t v = (uint16_t)(field(s, at) - 1);
  set_field(s, at, v);
  set_nz16(s->c, v);
  return v;
}

static uint16_t count_up(Frankenstein* s, uint16_t at) {
  const uint16_t v = (uint16_t)(field(s, at) + 1);
  set_field(s, at, v);
  set_nz16(s->c, v);
  return v;
}

// `INC` of a word anywhere in low memory.
static void count_up_at(Frankenstein* s, uint16_t at) {
  const uint16_t v = (uint16_t)(data(s, at) + 1);
  set_data(s, at, v);
  set_nz16(s->c, v);
}

// A `JSR` or a `JSL` at `at`, and coming back from one.
static void jsr(Frankenstein* s, uint16_t at) {
  push16(s->w, s->c, (uint16_t)(at + 2));
}

static void rts(Frankenstein* s) {
  (void)pull16(s->w, s->c);
  ran(s, FRANKENSTEIN_RUN_RTS);
}

// An `RTS` that is its run's last instruction.
static void back(Frankenstein* s) { (void)pull16(s->w, s->c); }

static void jsl(Frankenstein* s, uint16_t at) {
  push8(s->w, s->c, FRANKENSTEIN_CODE_BANK);
  push16(s->w, s->c, (uint16_t)(at + 3));
}

static void rtl(Frankenstein* s) {
  (void)pull16(s->w, s->c);
  (void)pull8(s->w, s->c);
}

static void set_flags(Frankenstein* s, bool n, bool z, bool c) {
  PortCpu* cpu = s->c;
  cpu->p = (uint8_t)(cpu->p & ~(PORT_P_N | PORT_P_Z));
  if (n) cpu->p |= PORT_P_N;
  if (z) cpu->p |= PORT_P_Z;
  set_c(cpu, c);
}

// What a routine called with `PHD` leaves in N and Z when it pulls it.
static void set_flags_pld(Frankenstein* s, bool c) {
  set_flags(s, negative(s->c->d), s->c->d == 0, c);
}

// --- What it calls ----------------------------------------------------------

// `JSL rng_next` at `at`.
static void draw(Frankenstein* s, uint16_t at) {
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

// `JSL actor_nearest` at `at`: the nearest record to the point in X and Y,
// in X, and how far in A.
static void asks_nearest(Frankenstein* s, uint16_t at) {
  PortCpu* c = s->c;
  FrankensteinWork* k = s->k;
  if (k->nearest_count == FRANKENSTEIN_MAX_CALLS) k->nearest_count--;
  ActorNearestWork* work = &k->nearest[k->nearest_count++];
  jsl(s, at);
  uint16_t gap = 0;
  c->x = actor_nearest_counted(s->w, c->x, c->y, &gap, work);
  rtl(s);
  k->v_known = false;
  c->a = gap;
  set_flags_pld(s, false);
}

// `JSL actor_bearing` at `at`: the way from the record in X to the one in Y.
static void asks_way(Frankenstein* s, uint16_t at) {
  PortCpu* c = s->c;
  FrankensteinWork* k = s->k;
  if (k->bearing_count == FRANKENSTEIN_MAX_CALLS) k->bearing_count--;
  ActorBearingRegs* r = &k->bearings[k->bearing_count++];
  jsl(s, at);
  actor_bearing(s->w, s->rom, c->x, c->y, r);
  rtl(s);
  k->v_known = false;
  c->a = r->a;
  c->x = r->x;
  set_flags_pld(s, r->c);
}

// `JSL player_bearing` at `at`: the way to the nearer player within A of
// the point in X and Y.
static void asks_player_way(Frankenstein* s, uint16_t at) {
  PortCpu* c = s->c;
  FrankensteinWork* k = s->k;
  if (k->player_count == FRANKENSTEIN_MAX_CALLS) k->player_count--;
  PlayerPickRegs* r = &k->players[k->player_count++];
  jsl(s, at);
  player_bearing(s->w, s->rom, c->a, c->x, c->y, r);
  rtl(s);
  k->v_known = false;
  c->a = r->a;
  c->x = r->x;
  c->y = r->y;
  set_flags_pld(s, r->c);
}

// `JSL actor_snap_to` at `at`: the record in X onto the one in Y, where
// they are within a pixel.
static void snaps(Frankenstein* s, uint16_t at) {
  PortCpu* c = s->c;
  FrankensteinWork* k = s->k;
  if (k->snap_count == FRANKENSTEIN_MAX_CALLS) k->snap_count--;
  ActorSnapRegs* r = &k->snaps[k->snap_count++];
  jsl(s, at);
  actor_snap_to(s->w, c->x, c->y, r);
  rtl(s);
  c->a = r->a;
  set_flags(s, r->n, r->z, r->c);
}

// `JSL terrain_blocked_enemy` at `at`: is the ground at X and Y closed to it?
static bool blocked(Frankenstein* s, uint16_t at) {
  PortCpu* c = s->c;
  FrankensteinWork* k = s->k;
  if (k->ground_count == FRANKENSTEIN_MAX_CALLS) k->ground_count--;
  TerrainRegs* r = &k->grounds[k->ground_count++];
  jsl(s, at);
  terrain_blocked_enemy(s->w, c->x, c->y, r);
  rtl(s);
  c->a = r->a;
  c->x = r->x;
  c->y = r->y;
  set_flags_pld(s, r->blocked);
  set_v(c, r->v);
  k->v_known = true;
  return r->blocked;
}

// `JSL terrain_point_bit2` at `at`: likewise, for a thing it threw.
static bool thing_blocked(Frankenstein* s, uint16_t at) {
  PortCpu* c = s->c;
  FrankensteinWork* k = s->k;
  if (k->thing_ground_count == FRANKENSTEIN_MAX_CALLS) k->thing_ground_count--;
  TerrainRegs* r = &k->thing_grounds[k->thing_ground_count++];
  jsl(s, at);
  terrain_point_bit2(s->w, c->x, c->y, r);
  rtl(s);
  c->a = r->a;
  c->x = r->x;
  c->y = r->y;
  set_flags_pld(s, r->blocked);
  k->v_known = false;
  return r->blocked;
}

// `JSL actor_at_point` at `at`: is anybody but the record in A at X and Y?
static bool somebody_at(Frankenstein* s, uint16_t at) {
  PortCpu* c = s->c;
  FrankensteinWork* k = s->k;
  if (k->at_point_count == FRANKENSTEIN_MAX_CALLS) k->at_point_count--;
  AtPointWork* work = &k->at_points[k->at_point_count++];
  AtPointRegs r;
  jsl(s, at);
  actor_at_point_counted(s->w, c->a, c->x, c->y, &r, work);
  rtl(s);
  c->a = r.a;
  c->x = r.x;
  c->y = r.y;
  set_flags_pld(s, r.found);
  if (r.v_set) set_v(c, r.v);
  return r.found;
}

// --- Where it goes ----------------------------------------------------------

// `$81:A267`: it decides next, and back from the look round or the state.
static void to_deciding(Frankenstein* s) {
  lda(s, FRANKENSTEIN_DECIDING);
  set_field(s, FRANKENSTEIN_DP_STATE, s->c->a);
  ran(s, FRANKENSTEIN_RUN_A267);
  back(s);
}

// `$81:A362`: anybody within 320 and it decides, with the caller's state
// over. Walking off a hundred stuck steps, it does not look.
static void looks_round(Frankenstein* s) {
  PortCpu* c = s->c;
  lda(s, field(s, FRANKENSTEIN_DP_STUCK));
  ran(s, FRANKENSTEIN_RUN_A362);
  if (took(s, negative(c->a))) {
    PORT_COVER(frankenstein_walks_off);
    count_up(s, FRANKENSTEIN_DP_STUCK);
    ran(s, FRANKENSTEIN_RUN_A385);
    back(s);
    return;
  }
  ldx(s, field(s, FRANKENSTEIN_DP_X));
  ldy(s, field(s, FRANKENSTEIN_DP_Y));
  asks_nearest(s, 0xa36a);
  cmp(s, FRANKENSTEIN_LOOKS);
  ran(s, FRANKENSTEIN_RUN_A366);
  if (!took(s, carry(s))) {
    PORT_COVER(frankenstein_sees);
    ran(s, FRANKENSTEIN_RUN_A373);
    to_deciding(s);
    return;
  }
  // A question whose answer it does not keep.
  ldx(s, field(s, FRANKENSTEIN_DP_X));
  ldy(s, field(s, FRANKENSTEIN_DP_Y));
  lda(s, FRANKENSTEIN_LOOKS_PLAYER);
  asks_player_way(s, 0xa37d);
  ldx(s, c->a);
  ran(s, FRANKENSTEIN_RUN_A376);
  took(s, !zero(s));
  rts(s);
}

// The place a way's step from where it is, by the index in X into its
// steps. `x_first` is the order the sum is written in.
static void step_from(Frankenstein* s, bool x_first) {
  PortCpu* c = s->c;
  if (x_first) {
    lda(s, field(s, FRANKENSTEIN_DP_X));
    add(s, data(s, (uint16_t)(FRANKENSTEIN_STEPS + c->x)));
  } else {
    lda(s, data(s, (uint16_t)(FRANKENSTEIN_STEPS + c->x)));
    add(s, field(s, FRANKENSTEIN_DP_X));
  }
  set_field(s, FRANKENSTEIN_DP_TRY_X, c->a);
  if (x_first) {
    lda(s, field(s, FRANKENSTEIN_DP_Y));
    add(s, data(s, (uint16_t)(FRANKENSTEIN_STEPS + 2 + c->x)));
  } else {
    lda(s, data(s, (uint16_t)(FRANKENSTEIN_STEPS + 2 + c->x)));
    add(s, field(s, FRANKENSTEIN_DP_Y));
  }
  set_field(s, FRANKENSTEIN_DP_TRY_Y, c->a);
}

// It is where it tried, and its record too.
static void goes_there(Frankenstein* s) {
  PortCpu* c = s->c;
  ldy(s, field(s, FRANKENSTEIN_DP_RECORD));
  lda(s, field(s, FRANKENSTEIN_DP_TRY_X));
  set_field(s, FRANKENSTEIN_DP_X, c->a);
  set_data(s, (uint16_t)(c->y + ACTOR_X), c->a);
  lda(s, field(s, FRANKENSTEIN_DP_TRY_Y));
  set_field(s, FRANKENSTEIN_DP_Y, c->a);
  set_data(s, (uint16_t)(c->y + ACTOR_Y), c->a);
}

// `$81:A1F5`: a quarter turn, and it turns next.
static void quarter_turn(Frankenstein* s) {
  PortCpu* c = s->c;
  lda(s, field(s, FRANKENSTEIN_DP_WAY));
  one_less(s);
  one_less(s);
  add(s, 4);
  masked(s, 0x000f);
  one_more(s);
  one_more(s);
  set_field(s, FRANKENSTEIN_DP_WAY, c->a);
  ran(s, FRANKENSTEIN_RUN_A1F5);
  lda(s, FRANKENSTEIN_TURNING);
  set_field(s, FRANKENSTEIN_DP_STATE, c->a);
  ran(s, FRANKENSTEIN_RUN_A207);
  back(s);
}

// `$81:A1B4`: walking. A look round, and a step its way if the ground and
// nobody is there; the ground there, and it turns.
static void walking(Frankenstein* s) {
  PortCpu* c = s->c;
  ran(s, FRANKENSTEIN_RUN_A1B4);
  jsr(s, 0xa1b4);
  looks_round(s);
  lda(s, field(s, FRANKENSTEIN_DP_WAY));
  doubled(s);
  ldx(s, c->a);
  step_from(s, true);
  ldx(s, field(s, FRANKENSTEIN_DP_TRY_X));
  ldy(s, field(s, FRANKENSTEIN_DP_TRY_Y));
  const bool closed = blocked(s, 0xa1cf);
  ran(s, FRANKENSTEIN_RUN_A1B7);
  if (!took(s, !closed)) {
    PORT_COVER(frankenstein_walked_into_ground);
    ran(s, FRANKENSTEIN_RUN_A1D5);
    quarter_turn(s);
    return;
  }
  lda(s, field(s, FRANKENSTEIN_DP_RECORD));
  ldx(s, field(s, FRANKENSTEIN_DP_TRY_X));
  ldy(s, field(s, FRANKENSTEIN_DP_TRY_Y));
  const bool met = somebody_at(s, 0xa1de);
  ran(s, FRANKENSTEIN_RUN_A1D8);
  if (took(s, met)) {
    PORT_COVER(frankenstein_walked_into_somebody);
    rts(s);
    return;
  }
  PORT_COVER(frankenstein_walked);
  goes_there(s);
  ran(s, FRANKENSTEIN_RUN_A1E4);
  back(s);
}

// `$81:A1AF`: it walks next, and walks now.
static void starts_walking(Frankenstein* s) {
  lda(s, FRANKENSTEIN_WALKING);
  set_field(s, FRANKENSTEIN_DP_STATE, s->c->a);
  ran(s, FRANKENSTEIN_RUN_A1AF);
  walking(s);
}

// `$81:A19F`: a way at random, and it walks.
static void wanders(Frankenstein* s) {
  PortCpu* c = s->c;
  draw(s, 0xa19f);
  masked(s, 0x0003);
  doubled(s);
  doubled(s);
  one_more(s);
  one_more(s);
  set_field(s, FRANKENSTEIN_DP_WAY, c->a);
  ran(s, FRANKENSTEIN_RUN_A19F);
  starts_walking(s);
}

// `$81:9F33`: is the place it tried open, and nobody there? Carry is no.
static bool closed_at(Frankenstein* s) {
  ldx(s, field(s, FRANKENSTEIN_DP_TRY_X));
  ldy(s, field(s, FRANKENSTEIN_DP_TRY_Y));
  const bool closed = blocked(s, 0x9f37);
  ran(s, FRANKENSTEIN_RUN_9F33);
  if (took(s, closed)) {
    rts(s);
    return true;
  }
  lda(s, field(s, FRANKENSTEIN_DP_RECORD));
  ldx(s, field(s, FRANKENSTEIN_DP_TRY_X));
  ldy(s, field(s, FRANKENSTEIN_DP_TRY_Y));
  const bool met = somebody_at(s, 0x9f43);
  ran(s, FRANKENSTEIN_RUN_9F3D);
  back(s);
  return met;
}

// `$81:A20D`: turning. A look round, and the way an eighth back and then
// its own, each tried; the first open is its way and it steps. Neither, and
// another quarter turn.
static void turning(Frankenstein* s) {
  PortCpu* c = s->c;
  ran(s, FRANKENSTEIN_RUN_A20D);
  jsr(s, 0xa20d);
  looks_round(s);
  lda(s, field(s, FRANKENSTEIN_DP_WAY));
  one_less(s);
  one_less(s);
  sub(s, 4);
  masked(s, 0x000f);
  one_more(s);
  one_more(s);
  set_field(s, FRANKENSTEIN_DP_TURN, c->a);
  doubled(s);
  ldx(s, c->a);
  step_from(s, false);
  ran(s, FRANKENSTEIN_RUN_A210);
  jsr(s, 0xa231);
  const bool back_closed = closed_at(s);
  ran(s, FRANKENSTEIN_RUN_A234);
  if (!took(s, back_closed)) {
    PORT_COVER(frankenstein_turned_back);
    lda(s, field(s, FRANKENSTEIN_DP_TURN));
    set_field(s, FRANKENSTEIN_DP_WAY, c->a);
    ran(s, FRANKENSTEIN_RUN_A236);
  }
  lda(s, field(s, FRANKENSTEIN_DP_WAY));
  doubled(s);
  ldx(s, c->a);
  step_from(s, false);
  ran(s, FRANKENSTEIN_RUN_A23A);
  jsr(s, 0xa24e);
  const bool closed = closed_at(s);
  ran(s, FRANKENSTEIN_RUN_A251);
  if (took(s, closed)) {
    PORT_COVER(frankenstein_turned_again);
    ran(s, FRANKENSTEIN_RUN_A264);
    quarter_turn(s);
    return;
  }
  PORT_COVER(frankenstein_turned);
  goes_there(s);
  ran(s, FRANKENSTEIN_RUN_A253);
  back(s);
}

// `$81:9F48`: a step to the place it tried, an axis at a time, each where
// the ground lets it and nobody is. Neither, and it is a step stuck.
static void steps_towards(Frankenstein* s) {
  PortCpu* c = s->c;
  set_field(s, FRANKENSTEIN_DP_MOVED, 0);
  lda(s, field(s, FRANKENSTEIN_DP_TRY_X));
  cmp(s, field(s, FRANKENSTEIN_DP_X));
  ran(s, FRANKENSTEIN_RUN_9F48);
  if (!took(s, zero(s))) {
    ldx(s, c->a);
    ldy(s, field(s, FRANKENSTEIN_DP_Y));
    const bool closed = blocked(s, 0x9f53);
    ran(s, FRANKENSTEIN_RUN_9F50);
    if (!took(s, closed)) {
      lda(s, field(s, FRANKENSTEIN_DP_RECORD));
      ldx(s, field(s, FRANKENSTEIN_DP_TRY_X));
      ldy(s, field(s, FRANKENSTEIN_DP_Y));
      const bool met = somebody_at(s, 0x9f5f);
      ran(s, FRANKENSTEIN_RUN_9F59);
      if (!took(s, met)) {
        count_up(s, FRANKENSTEIN_DP_MOVED);
        ldy(s, field(s, FRANKENSTEIN_DP_RECORD));
        lda(s, field(s, FRANKENSTEIN_DP_TRY_X));
        set_field(s, FRANKENSTEIN_DP_X, c->a);
        set_data(s, (uint16_t)(c->y + ACTOR_X), c->a);
        ran(s, FRANKENSTEIN_RUN_9F65);
      }
    }
  }
  lda(s, field(s, FRANKENSTEIN_DP_TRY_Y));
  cmp(s, field(s, FRANKENSTEIN_DP_Y));
  ran(s, FRANKENSTEIN_RUN_9F70);
  if (!took(s, zero(s))) {
    ldx(s, field(s, FRANKENSTEIN_DP_X));
    ldy(s, c->a);
    const bool closed = blocked(s, 0x9f79);
    ran(s, FRANKENSTEIN_RUN_9F76);
    if (!took(s, closed)) {
      lda(s, field(s, FRANKENSTEIN_DP_RECORD));
      ldx(s, field(s, FRANKENSTEIN_DP_X));
      ldy(s, field(s, FRANKENSTEIN_DP_TRY_Y));
      const bool met = somebody_at(s, 0x9f85);
      ran(s, FRANKENSTEIN_RUN_9F7F);
      if (!took(s, met)) {
        count_up(s, FRANKENSTEIN_DP_MOVED);
        ldy(s, field(s, FRANKENSTEIN_DP_RECORD));
        lda(s, field(s, FRANKENSTEIN_DP_TRY_Y));
        set_field(s, FRANKENSTEIN_DP_Y, c->a);
        set_data(s, (uint16_t)(c->y + ACTOR_Y), c->a);
        ran(s, FRANKENSTEIN_RUN_9F8B);
      }
    }
  }
  lda(s, field(s, FRANKENSTEIN_DP_MOVED));
  ran(s, FRANKENSTEIN_RUN_9F96);
  if (took(s, !zero(s))) {
    rts(s);
    return;
  }
  PORT_COVER(frankenstein_stuck);
  count_up(s, FRANKENSTEIN_DP_STUCK);
  ran(s, FRANKENSTEIN_RUN_9F9A);
  back(s);
}

// `$81:A2D1`: nobody near enough. It can be hit as ever, and wanders off.
static void gives_up(Frankenstein* s) {
  PortCpu* c = s->c;
  ldy(s, field(s, FRANKENSTEIN_DP_RECORD));
  lda(s, FRANKENSTEIN_COLLIDE_AWAKE);
  set_data(s, (uint16_t)(c->y + ACTOR_COLLIDE_ID), c->a);
  ran(s, FRANKENSTEIN_RUN_A2D1);
  wanders(s);
}

// `$81:A2DC`: it attacks next, by the pictures for its way.
static void starts_attacking(Frankenstein* s) {
  PortCpu* c = s->c;
  lda(s, FRANKENSTEIN_ATTACKING);
  set_field(s, FRANKENSTEIN_DP_STATE, c->a);
  count_up(s, FRANKENSTEIN_DP_FRAMES);
  count_up(s, FRANKENSTEIN_DP_FRAMES);
  ldx(s, field(s, FRANKENSTEIN_DP_WAY));
  lda(s, data(s, (uint16_t)(FRANKENSTEIN_ATTACKS_BY_WAY + c->x)));
  set_field(s, FRANKENSTEIN_DP_ATTACK, c->a);
  set_field(s, FRANKENSTEIN_DP_ATTACK_TICKS, 0);
  set_field(s, FRANKENSTEIN_DP_PICTURE, 0);
  ran(s, FRANKENSTEIN_RUN_A2DC);
  back(s);
}

// `$81:A26D`: deciding. Whoever is nearest, and how far: under 80 it
// attacks, from 300 it gives up, and between it makes a step their way, an
// axis at a time. A hundred stuck, and it walks.
static void deciding(Frankenstein* s) {
  PortCpu* c = s->c;
  ldx(s, field(s, FRANKENSTEIN_DP_X));
  ldy(s, field(s, FRANKENSTEIN_DP_Y));
  asks_nearest(s, 0xa271);
  cmp(s, FRANKENSTEIN_GIVES_UP);
  ran(s, FRANKENSTEIN_RUN_A26D);
  if (took(s, carry(s))) {
    PORT_COVER(frankenstein_gives_up);
    gives_up(s);
    return;
  }
  cmp(s, FRANKENSTEIN_ATTACKS);
  ran(s, FRANKENSTEIN_RUN_A27A);
  if (!took(s, carry(s))) {
    PORT_COVER(frankenstein_attacks);
    ran(s, FRANKENSTEIN_RUN_A27F);
    starts_attacking(s);
    return;
  }
  ldy(s, c->x);
  ldx(s, field(s, FRANKENSTEIN_DP_RECORD));
  snaps(s, 0xa287);
  asks_way(s, 0xa28b);
  ldx(s, c->a);
  ran(s, FRANKENSTEIN_RUN_A284);
  if (took(s, zero(s))) {
    PORT_COVER(frankenstein_on_top);
    gives_up(s);
    return;
  }
  PORT_COVER(frankenstein_makes_for);
  doubled(s);
  set_field(s, FRANKENSTEIN_DP_WAY, c->a);
  doubled(s);
  set_field(s, FRANKENSTEIN_DP_TURN, c->a);
  draw(s, 0xa298);
  masked(s, 0x0002);
  ran(s, FRANKENSTEIN_RUN_A292);
  if (!took(s, !zero(s))) {
    lda(s, 0x0020);
    add(s, field(s, FRANKENSTEIN_DP_TURN));
    set_field(s, FRANKENSTEIN_DP_TURN, c->a);
    ran(s, FRANKENSTEIN_RUN_A2A1);
  }
  ldx(s, field(s, FRANKENSTEIN_DP_TURN));
  ldy(s, field(s, FRANKENSTEIN_DP_RECORD));
  step_from(s, false);
  ran(s, FRANKENSTEIN_RUN_A2A9);
  jsr(s, 0xa2bd);
  steps_towards(s);
  lda(s, field(s, FRANKENSTEIN_DP_STUCK));
  cmp(s, FRANKENSTEIN_STUCK_LIMIT);
  ran(s, FRANKENSTEIN_RUN_A2C0);
  if (took(s, !carry(s))) {
    rts(s);
    return;
  }
  PORT_COVER(frankenstein_stuck_too_long);
  lda(s, (uint16_t)~c->a);
  one_more(s);
  set_field(s, FRANKENSTEIN_DP_STUCK, c->a);
  ran(s, FRANKENSTEIN_RUN_A2C7);
  starts_walking(s);
}

// `$81:A2F1`: attacking. The next of its pictures every third tick, and
// from 96 away it decides again.
static void attacking(Frankenstein* s) {
  PortCpu* c = s->c;
  count_up(s, FRANKENSTEIN_DP_FRAMES);
  count_up(s, FRANKENSTEIN_DP_FRAMES);
  const uint16_t ticks = count_down(s, FRANKENSTEIN_DP_ATTACK_TICKS);
  ran(s, FRANKENSTEIN_RUN_A2F1);
  if (!took(s, !negative(ticks))) {
    lda(s, FRANKENSTEIN_ATTACK_FRAMES);
    set_field(s, FRANKENSTEIN_DP_ATTACK_TICKS, c->a);
    count_up(s, FRANKENSTEIN_DP_PICTURE);
    count_up(s, FRANKENSTEIN_DP_PICTURE);
    ldy(s, field(s, FRANKENSTEIN_DP_PICTURE));
    const uint16_t list = field(s, FRANKENSTEIN_DP_ATTACK);
    lda(s, data(s, (uint16_t)(list + c->y)));
    ran(s, FRANKENSTEIN_RUN_A2F9);
    if (!took(s, !zero(s))) {
      PORT_COVER(frankenstein_attack_again);
      set_field(s, FRANKENSTEIN_DP_PICTURE, 0);
      lda(s, data(s, list));
      ran(s, FRANKENSTEIN_RUN_A308);
    }
    ldx(s, field(s, FRANKENSTEIN_DP_RECORD));
    set_data(s, (uint16_t)(c->x + ACTOR_META), c->a);
    ran(s, FRANKENSTEIN_RUN_A30C);
  }
  ldx(s, field(s, FRANKENSTEIN_DP_X));
  ldy(s, field(s, FRANKENSTEIN_DP_Y));
  asks_nearest(s, 0xa315);
  cmp(s, FRANKENSTEIN_ATTACK_ENDS);
  ran(s, FRANKENSTEIN_RUN_A311);
  if (took(s, !carry(s))) {
    rts(s);
    return;
  }
  PORT_COVER(frankenstein_attack_over);
  set_field(s, FRANKENSTEIN_DP_PICTURE, 0);
  set_field(s, FRANKENSTEIN_DP_FRAMES, 0);
  ran(s, FRANKENSTEIN_RUN_A31E);
  to_deciding(s);
}

// --- Its frame --------------------------------------------------------------

// `$81:9FA9`: back from a throw, and a sound if it is on the screen. The
// stretch stops at the sound, or comes back.
static bool heard_if_seen(Frankenstein* s) {
  PortCpu* c = s->c;
  lda(s, data(s, W_FRANKENSTEIN_SCREEN_X));
  cmp(s, field(s, FRANKENSTEIN_DP_X));
  ran(s, FRANKENSTEIN_RUN_9FA9);
  if (took(s, carry(s))) goto unseen;
  add(s, FRANKENSTEIN_SCREEN_W);
  cmp(s, field(s, FRANKENSTEIN_DP_X));
  ran(s, FRANKENSTEIN_RUN_9FB0);
  if (took(s, !carry(s))) goto unseen;
  lda(s, data(s, W_FRANKENSTEIN_SCREEN_Y));
  cmp(s, field(s, FRANKENSTEIN_DP_Y));
  ran(s, FRANKENSTEIN_RUN_9FB8);
  if (took(s, carry(s))) goto unseen;
  add(s, FRANKENSTEIN_SCREEN_H);
  cmp(s, field(s, FRANKENSTEIN_DP_Y));
  ran(s, FRANKENSTEIN_RUN_9FBF);
  if (took(s, !carry(s))) goto unseen;
  PORT_COVER(frankenstein_throw_heard);
  lda(s, FRANKENSTEIN_THROW_SOUND);
  ran(s, FRANKENSTEIN_RUN_9FC7);
  c->pc = FRANKENSTEIN_THROW_SOUND_PC;
  return true;
unseen:
  PORT_COVER(frankenstein_throw_unseen);
  rts(s);
  return false;
}

static uint32_t rest_of_frame(Frankenstein* s);

// `$81:9F9D`: a draw of 12 in 256, and it throws, if one of its four things
// is free; the stretch stops for its record, or for the sound.
static bool throws(Frankenstein* s) {
  PortCpu* c = s->c;
  draw(s, 0x9f9d);
  cmp(s, FRANKENSTEIN_THROWS_UNDER);
  ran(s, FRANKENSTEIN_RUN_9F9D);
  if (took(s, carry(s))) {
    rts(s);
    return false;
  }
  ran(s, FRANKENSTEIN_RUN_9FA6);
  jsr(s, 0x9fa6);
  lda(s, FRANKENSTEIN_THING_LAST);
  ran(s, FRANKENSTEIN_RUN_9FCF);
  for (;;) {
    ldx(s, c->a);
    lda(s, field(s, (uint16_t)(FRANKENSTEIN_DP_THINGS + c->x)));
    cmp(s, FRANKENSTEIN_NO_THING);
    ran(s, FRANKENSTEIN_RUN_9FD2);
    if (took(s, zero(s))) break;
    lda(s, c->x);
    sub(s, FRANKENSTEIN_THING_STRIDE);
    ran(s, FRANKENSTEIN_RUN_9FDA);
    if (!took(s, !negative(c->a))) {
      PORT_COVER(frankenstein_throws_none_free);
      rts(s);
      return heard_if_seen(s);
    }
  }
  PORT_COVER(frankenstein_throws);
  set_field(s, FRANKENSTEIN_DP_THING_AT, c->x);
  count_up(s, FRANKENSTEIN_DP_THINGS_OUT);
  ran(s, FRANKENSTEIN_RUN_9FE2);
  c->pc = FRANKENSTEIN_RECORD_PC;
  return true;
}

// `$81:A388`: the next picture of its walk every ninth frame, by its way;
// past the middle of the table, mirrored.
static void shows_walk(Frankenstein* s) {
  PortCpu* c = s->c;
  const uint16_t frames = count_down(s, FRANKENSTEIN_DP_FRAMES);
  ran(s, FRANKENSTEIN_RUN_A388);
  if (took(s, !negative(frames))) {
    rts(s);
    return;
  }
  lda(s, FRANKENSTEIN_PICTURE_FRAMES);
  set_field(s, FRANKENSTEIN_DP_FRAMES, c->a);
  lda(s, field(s, FRANKENSTEIN_DP_PICTURE));
  one_more(s);
  one_more(s);
  masked(s, 0x0007);
  set_field(s, FRANKENSTEIN_DP_PICTURE, c->a);
  lda(s, field(s, FRANKENSTEIN_DP_WAY));
  doubled(s);
  doubled(s);
  lda(s, c->a | field(s, FRANKENSTEIN_DP_PICTURE));
  ldx(s, c->a);
  ldy(s, field(s, FRANKENSTEIN_DP_RECORD));
  lda(s, data(s, (uint16_t)(FRANKENSTEIN_PICTURES + c->x)));
  set_data(s, (uint16_t)(c->y + ACTOR_META), c->a);
  lda(s, FRANKENSTEIN_PICTURE_BANK_AWAKE);
  set_data(s, (uint16_t)(c->y + ACTOR_META_BANK), c->a);
  cmp16(c, c->x, FRANKENSTEIN_MIRRORED_FROM);
  ran(s, FRANKENSTEIN_RUN_A38C);
  const uint16_t flags = data(s, (uint16_t)(c->y + ACTOR_FLAGS));
  if (took(s, carry(s))) {
    lda(s, flags | FRANKENSTEIN_MIRROR);
    set_data(s, (uint16_t)(c->y + ACTOR_FLAGS), c->a);
    ran(s, FRANKENSTEIN_RUN_A3BE);
  } else {
    lda(s, flags & (uint16_t)~FRANKENSTEIN_MIRROR);
    set_data(s, (uint16_t)(c->y + ACTOR_FLAGS), c->a);
    ran(s, FRANKENSTEIN_RUN_A3B4);
  }
  back(s);
}

// `$81:A115`: the thing in hand is freed; the stretch stops for it.
static uint32_t frees_thing(Frankenstein* s) {
  lda(s, field(s, FRANKENSTEIN_DP_THING));
  ran(s, FRANKENSTEIN_RUN_A115);
  return FRANKENSTEIN_FREE_PC;
}

// `$81:A084`: the thing at `$4E` moves, if there is one. Its turns run out
// and it turns towards whoever is nearest it, and with nobody it is freed.
// Its life runs out, and it is freed; it moves, and the ground stops it, and
// it is freed. Zero is that it moved, or there was none.
static uint32_t moves_thing(Frankenstein* s) {
  PortCpu* c = s->c;
  ldx(s, field(s, FRANKENSTEIN_DP_THING_AT));
  lda(s, field(s, (uint16_t)(FRANKENSTEIN_DP_THINGS + c->x)));
  cmp(s, FRANKENSTEIN_NO_THING);
  ran(s, FRANKENSTEIN_RUN_A084);
  if (!took(s, !zero(s))) {
    ran(s, FRANKENSTEIN_RUN_A08D);
    rts(s);
    return 0;
  }
  const uint16_t at = c->x;
  set_field(s, FRANKENSTEIN_DP_THING, c->a);
  const uint16_t turns =
      count_down(s, (uint16_t)(FRANKENSTEIN_DP_THING_TURNS + at));
  ran(s, FRANKENSTEIN_RUN_A090);
  if (!took(s, !negative(turns))) {
    draw(s, 0xa096);
    masked(s, 0x0007);
    one_more(s);
    set_field(s, (uint16_t)(FRANKENSTEIN_DP_THING_TURNS + at), c->a);
    const uint16_t thing = field(s, FRANKENSTEIN_DP_THING);
    ldy(s, thing);
    lda(s, data(s, (uint16_t)(thing + ACTOR_X)));
    ldx(s, c->a);
    lda(s, data(s, (uint16_t)(thing + ACTOR_Y)));
    ldy(s, c->a);
    asks_nearest(s, 0xa0aa);
    ldy(s, c->x);
    ldx(s, thing);
    asks_way(s, 0xa0b1);
    ldx(s, c->a);
    ran(s, FRANKENSTEIN_RUN_A096);
    if (took(s, zero(s))) {
      PORT_COVER(frankenstein_thing_lost);
      return frees_thing(s);
    }
    PORT_COVER(frankenstein_thing_turns);
    doubled(s);
    doubled(s);
    ldx(s, field(s, FRANKENSTEIN_DP_THING_AT));
    set_field(s, (uint16_t)(FRANKENSTEIN_DP_THING_WAY + c->x), c->a);
    ldx(s, c->a);
    ldy(s, thing);
    lda(s, data(s, (uint16_t)(thing + ACTOR_FLAGS)));
    masked(s, FRANKENSTEIN_THING_FLIPS);
    lda(s, c->a | data(s, (uint16_t)(FRANKENSTEIN_THINGS_LOOK + 2 + c->x)));
    set_data(s, (uint16_t)(thing + ACTOR_FLAGS), c->a);
    ran(s, FRANKENSTEIN_RUN_A0B8);
  }
  ldx(s, field(s, FRANKENSTEIN_DP_THING_AT));
  lda(s, field(s, (uint16_t)(FRANKENSTEIN_DP_THING_LIFE + c->x)));
  one_less(s);
  ran(s, FRANKENSTEIN_RUN_A0CD);
  if (took(s, negative(c->a))) {
    PORT_COVER(frankenstein_thing_spent);
    return frees_thing(s);
  }
  set_field(s, (uint16_t)(FRANKENSTEIN_DP_THING_LIFE + c->x), c->a);
  masked(s, 0x0003);
  doubled(s);
  set_field(s, FRANKENSTEIN_DP_MOVED, c->a);
  const uint16_t thing = field(s, FRANKENSTEIN_DP_THING);
  ldy(s, thing);
  lda(s, field(s, (uint16_t)(FRANKENSTEIN_DP_THING_WAY + c->x)));
  ldx(s, c->a);
  lda(s, data(s, (uint16_t)(FRANKENSTEIN_THING_STEPS + c->x)));
  add(s, data(s, (uint16_t)(thing + ACTOR_X)));
  set_field(s, FRANKENSTEIN_DP_TRY_X, c->a);
  lda(s, data(s, (uint16_t)(FRANKENSTEIN_THING_STEPS + 2 + c->x)));
  add(s, data(s, (uint16_t)(thing + ACTOR_Y)));
  set_field(s, FRANKENSTEIN_DP_TRY_Y, c->a);
  lda(s, c->x);
  doubled(s);
  lda(s, c->a | field(s, FRANKENSTEIN_DP_MOVED));
  ldx(s, c->a);
  lda(s, data(s, (uint16_t)(FRANKENSTEIN_THING_PICTURES + c->x)));
  set_data(s, (uint16_t)(thing + ACTOR_META), c->a);
  ldx(s, field(s, FRANKENSTEIN_DP_TRY_X));
  ldy(s, field(s, FRANKENSTEIN_DP_TRY_Y));
  const bool closed = thing_blocked(s, 0xa102);
  ran(s, FRANKENSTEIN_RUN_A0D4);
  if (took(s, closed)) {
    PORT_COVER(frankenstein_thing_landed);
    return frees_thing(s);
  }
  PORT_COVER(frankenstein_thing_moved);
  ldy(s, thing);
  lda(s, field(s, FRANKENSTEIN_DP_TRY_X));
  set_data(s, (uint16_t)(thing + ACTOR_X), c->a);
  lda(s, field(s, FRANKENSTEIN_DP_TRY_Y));
  set_data(s, (uint16_t)(thing + ACTOR_Y), c->a);
  ran(s, FRANKENSTEIN_RUN_A108);
  back(s);
  return 0;
}

// `$81:A4D3`: alive, and to sleep a frame; or dying, and its things' lives
// ended first.
static uint32_t frame_ends(Frankenstein* s) {
  lda(s, field(s, FRANKENSTEIN_DP_DEAD));
  ran(s, FRANKENSTEIN_RUN_A4D3);
  if (took(s, zero(s))) {
    set_field(s, FRANKENSTEIN_DP_HIT_ID, 0);
    lda(s, 1);
    ran(s, FRANKENSTEIN_RUN_A4B1);
    return FRANKENSTEIN_SLEEP_PC;
  }
  PORT_COVER(frankenstein_dies);
  for (uint16_t i = 0; i <= FRANKENSTEIN_THING_LAST; i += FRANKENSTEIN_THING_STRIDE)
    set_field(s, (uint16_t)(FRANKENSTEIN_DP_THING_LIFE + i), 0);
  lda(s, 1);
  ran(s, FRANKENSTEIN_RUN_A4D7);
  return FRANKENSTEIN_DIES_SLEEP_PC;
}

// `$81:A4D0`: one of its things moves, by the frame, and the frame ends.
static uint32_t moves_one(Frankenstein* s) {
  PortCpu* c = s->c;
  ran(s, FRANKENSTEIN_RUN_A4D0);
  jsr(s, 0xa4d0);
  lda(s, data(s, W_SCHED_TICK));
  masked(s, 0x0003);
  doubled(s);
  ldy(s, c->a);
  ldx(s, data(s, (uint16_t)(FRANKENSTEIN_THING_ORDER + c->y)));
  set_field(s, FRANKENSTEIN_DP_THING_AT, c->x);
  ran(s, FRANKENSTEIN_RUN_A06B);
  jsr(s, 0xa078);
  const uint32_t stop = moves_thing(s);
  if (stop) return stop;
  rts(s);
  return frame_ends(s);
}

// `$81:A4BD`: every other frame its state, and every frame its walk's
// picture and a thing moved.
static uint32_t rest_of_frame(Frankenstein* s) {
  PortCpu* c = s->c;
  lda(s, data(s, W_SCHED_TICK));
  masked(s, 0x0001);
  ran(s, FRANKENSTEIN_RUN_A4BD);
  if (!took(s, !zero(s))) {
    push16(s->w, c, 0xa4cc);
    const uint16_t state = field(s, FRANKENSTEIN_DP_STATE);
    lda(s, state);
    one_less(s);
    push16(s->w, c, c->a);
    ran(s, FRANKENSTEIN_RUN_A4C5);
    (void)pull16(s->w, c);
    switch (state) {
      case FRANKENSTEIN_WALKING:
        walking(s);
        break;
      case FRANKENSTEIN_TURNING:
        turning(s);
        break;
      case FRANKENSTEIN_DECIDING:
        deciding(s);
        break;
      case FRANKENSTEIN_ATTACKING:
        attacking(s);
        break;
    }
  }
  ran(s, FRANKENSTEIN_RUN_A4CD);
  jsr(s, 0xa4cd);
  shows_walk(s);
  return moves_one(s);
}

// --- The stretches ----------------------------------------------------------

// `$81:A484`: a player came near. Its handler for being hit, and its entry
// in the list is marked; the stretch stops for that.
static uint32_t found(Frankenstein* s) {
  PortCpu* c = s->c;
  c->a = FRANKENSTEIN_HANDLER_AWAKE;
  ldy(s, FRANKENSTEIN_CODE_BANK);
  thread_set_handler(s->w, c);
  s->k->handlers_set++;
  set_field(s, FRANKENSTEIN_DP_DEAD, 0);
  lda(s, field(s, FRANKENSTEIN_DP_LISTED));
  ran(s, FRANKENSTEIN_RUN_A484);
  return FRANKENSTEIN_MARK_PC;
}

// `$81:A496`: a way at random and a step, its health, and it can be hit and
// is drawn among the rest. Then a frame's sleep.
static uint32_t marked(Frankenstein* s) {
  PortCpu* c = s->c;
  ran(s, FRANKENSTEIN_RUN_A496);
  jsr(s, 0xa496);
  wanders(s);
  lda(s, FRANKENSTEIN_HEALTH);
  set_field(s, FRANKENSTEIN_DP_HEALTH, c->a);
  const uint16_t record = field(s, FRANKENSTEIN_DP_RECORD);
  ldy(s, record);
  lda(s, FRANKENSTEIN_COLLIDE_AWAKE);
  set_data(s, (uint16_t)(record + ACTOR_COLLIDE_ID), c->a);
  set_field(s, FRANKENSTEIN_DP_FRAMES, 0);
  set_field(s, FRANKENSTEIN_DP_PICTURE, 0);
  lda(s, FRANKENSTEIN_NOT_ON_TOP);
  lda(s, c->a & data(s, record));
  set_data(s, record, c->a);
  ran(s, FRANKENSTEIN_RUN_A499);
  set_field(s, FRANKENSTEIN_DP_HIT_ID, 0);
  lda(s, 1);
  ran(s, FRANKENSTEIN_RUN_A4B1);
  return FRANKENSTEIN_SLEEP_PC;
}

// `$81:A4BA`: a frame.
static uint32_t woke(Frankenstein* s) {
  ran(s, FRANKENSTEIN_RUN_A4BA);
  jsr(s, 0xa4ba);
  if (throws(s)) return s->c->pc;
  return rest_of_frame(s);
}

// `$81:9FEA`: a record for a thing, in A. At one of eight places about it,
// by a way drawn, with its turns drawn, and drawn itself. Then the sound, if
// it is seen.
static uint32_t made(Frankenstein* s) {
  PortCpu* c = s->c;
  const uint16_t thing = c->a;
  ldx(s, field(s, FRANKENSTEIN_DP_THING_AT));
  const uint16_t at = c->x;
  set_field(s, (uint16_t)(FRANKENSTEIN_DP_THINGS + at), thing);
  ldy(s, thing);
  lda(s, FRANKENSTEIN_THING_LIVES);
  set_field(s, (uint16_t)(FRANKENSTEIN_DP_THING_LIFE + at), c->a);
  lda(s, data(s, W_FRANKENSTEIN_OWNER));
  set_data(s, (uint16_t)(thing + ACTOR_THREAD), c->a);
  lda(s, FRANKENSTEIN_THING_COLLIDE);
  set_data(s, (uint16_t)(thing + ACTOR_COLLIDE_ID), c->a);
  lda(s, FRANKENSTEIN_THING_BANK);
  set_data(s, (uint16_t)(thing + ACTOR_META_BANK), c->a);
  draw(s, 0xa006);
  masked(s, 0x0003);
  set_field(s, (uint16_t)(FRANKENSTEIN_DP_THING_TURNS + at), c->a);
  draw(s, 0xa00f);
  masked(s, 0x0007);
  one_more(s);
  doubled(s);
  doubled(s);
  set_field(s, (uint16_t)(FRANKENSTEIN_DP_THING_WAY + at), c->a);
  ldx(s, c->a);
  lda(s, data(s, (uint16_t)(FRANKENSTEIN_THINGS_LOOK + c->x)));
  set_data(s, (uint16_t)(thing + ACTOR_META), c->a);
  lda(s, data(s, (uint16_t)(FRANKENSTEIN_THINGS_LOOK + 2 + c->x)));
  lda(s, c->a | data(s, (uint16_t)(thing + ACTOR_FLAGS)));
  lda(s, c->a | FRANKENSTEIN_THING_DRAWN);
  set_data(s, (uint16_t)(thing + ACTOR_FLAGS), c->a);
  lda(s, field(s, FRANKENSTEIN_DP_X));
  add(s, data(s, (uint16_t)(FRANKENSTEIN_THINGS_FROM + c->x)));
  set_data(s, (uint16_t)(thing + ACTOR_X), c->a);
  lda(s, 0);
  set_data(s, (uint16_t)(thing + ACTOR_Z), c->a);
  lda(s, field(s, FRANKENSTEIN_DP_Y));
  add(s, data(s, (uint16_t)(FRANKENSTEIN_THINGS_FROM + 2 + c->x)));
  set_data(s, (uint16_t)(thing + ACTOR_Y), c->a);
  ran(s, FRANKENSTEIN_RUN_9FEA);
  back(s);
  if (heard_if_seen(s)) return c->pc;
  return rest_of_frame(s);
}

// `$81:9FCE`: after the sound of a throw, the rest of the frame.
static uint32_t heard(Frankenstein* s) {
  rts(s);
  return rest_of_frame(s);
}

// `$81:A11B`: a thing freed. Its place is free, one fewer is out, and the
// frame ends.
static uint32_t freed(Frankenstein* s) {
  PortCpu* c = s->c;
  ldx(s, field(s, FRANKENSTEIN_DP_THING_AT));
  lda(s, FRANKENSTEIN_NO_THING);
  set_field(s, (uint16_t)(FRANKENSTEIN_DP_THINGS + c->x), c->a);
  count_down(s, FRANKENSTEIN_DP_THINGS_OUT);
  ran(s, FRANKENSTEIN_RUN_A11B);
  back(s);
  rts(s);
  return frame_ends(s);
}

// `$81:A4E6`: a frame of its death. Its things move until none is out, and
// then it is paid for.
static uint32_t dies_woke(Frankenstein* s) {
  lda(s, field(s, FRANKENSTEIN_DP_THINGS_OUT));
  ran(s, FRANKENSTEIN_RUN_A4E6);
  if (took(s, !zero(s))) return moves_one(s);
  PORT_COVER(frankenstein_died);
  return FRANKENSTEIN_DIES_PC;
}

// `$81:A4F5`: paid for. Its kind's count and its killer's tally, it can no
// longer be touched, and it is heard.
static uint32_t scored(Frankenstein* s) {
  PortCpu* c = s->c;
  count_up_at(s, FRANKENSTEIN_KILLS);
  lda(s, field(s, FRANKENSTEIN_DP_HIT_ID));
  masked(s, 0x8000);
  // `ASL : ROL : ROL`: bit 15 to bit 1.
  doubled(s);
  {
    const bool in = carry(s);
    set_c(c, (c->a & 0x8000u) != 0);
    lda(s, (uint16_t)(c->a << 1 | (in ? 1 : 0)));
  }
  {
    const bool in = carry(s);
    set_c(c, (c->a & 0x8000u) != 0);
    lda(s, (uint16_t)(c->a << 1 | (in ? 1 : 0)));
  }
  ran(s, FRANKENSTEIN_RUN_A4F5);
  // `JSL $80:9D6A`: which score slot that side is.
  jsl(s, 0xa500);
  ldx(s, 0);
  cmp(s, data(s, W_SCORE_SLOT_SIDE));
  ran(s, FRANKENSTEIN_RUN_9D6A);
  if (!took(s, zero(s))) {
    ldx(s, 2);
    ran(s, FRANKENSTEIN_RUN_9D72);
  }
  ran(s, FRANKENSTEIN_RUN_9D74);
  rtl(s);
  count_up_at(s, (uint16_t)(W_FRANKENSTEIN_TALLY + c->x));
  const uint16_t record = field(s, FRANKENSTEIN_DP_RECORD);
  ldy(s, record);
  lda(s, 0);
  set_data(s, (uint16_t)(record + ACTOR_COLLIDE_ID), c->a);
  lda(s, FRANKENSTEIN_DEAD_BANK);
  set_data(s, (uint16_t)(record + ACTOR_META_BANK), c->a);
  lda(s, FRANKENSTEIN_CRY);
  ran(s, FRANKENSTEIN_RUN_A504);
  return FRANKENSTEIN_CRY_PC;
}

// `$81:A51C`: its last pictures.
static uint32_t cried(Frankenstein* s) {
  lda(s, FRANKENSTEIN_LAST_PICTURES);
  ran(s, FRANKENSTEIN_RUN_A51C);
  return FRANKENSTEIN_LAST_PC;
}

static const struct {
  uint32_t pc;
  uint32_t (*run)(Frankenstein* s);
} STRETCHES[] = {
    {FRANKENSTEIN_FOUND_PC, found},         {FRANKENSTEIN_MARKED_PC, marked},
    {FRANKENSTEIN_WOKE_PC, woke},           {FRANKENSTEIN_MADE_PC, made},
    {FRANKENSTEIN_HEARD_PC, heard},         {FRANKENSTEIN_FREED_PC, freed},
    {FRANKENSTEIN_DIES_WOKE_PC, dies_woke}, {FRANKENSTEIN_SCORED_PC, scored},
    {FRANKENSTEIN_CRIED_PC, cried},
};

bool frankenstein_state_known(uint16_t state) {
  return state == FRANKENSTEIN_WALKING || state == FRANKENSTEIN_TURNING ||
         state == FRANKENSTEIN_DECIDING || state == FRANKENSTEIN_ATTACKING;
}

// The word `at` bytes under the top of the stack.
static uint16_t stacked(const Wram* w, uint16_t s, int at) {
  return wram_r16(w, (uint16_t)(s + 1 + at));
}

bool frankenstein_stack_known(const Wram* w, uint32_t pc, uint16_t s) {
  switch (pc) {
    case FRANKENSTEIN_MADE_PC:
      return stacked(w, s, 0) == FRANKENSTEIN_RET_MAKES &&
             stacked(w, s, 2) == FRANKENSTEIN_RET_THROWS;
    case FRANKENSTEIN_HEARD_PC:
      return stacked(w, s, 0) == FRANKENSTEIN_RET_THROWS;
    case FRANKENSTEIN_FREED_PC:
      return stacked(w, s, 0) == FRANKENSTEIN_RET_MOVE &&
             stacked(w, s, 2) == FRANKENSTEIN_RET_MOVES;
  }
  return true;
}

void frankenstein_run(Wram* w, const Rom* rom, PortCpu* c, FrankensteinWork* k) {
  Frankenstein s = {w, rom, c, k};
  k->v_known = true;
  for (unsigned i = 0; i < sizeof STRETCHES / sizeof STRETCHES[0]; i++) {
    if (STRETCHES[i].pc != c->pc) continue;
    c->pc = STRETCHES[i].run(&s);
    return;
  }
}
