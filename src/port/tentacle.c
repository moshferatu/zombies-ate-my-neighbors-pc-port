// The purple tentacle's thread -- see port/tentacle.h.

#include "port/tentacle.h"

#include "port/coverage.h"
#include "port/rng.h"

typedef struct {
  Wram* w;
  const Rom* rom;
  PortCpu* c;
  TentacleWork* k;
} Tentacle;

// --- The machine, as these stretches use it ---------------------------------

static uint16_t field(const Tentacle* t, uint16_t at) {
  return wram_r16(t->w, (uint16_t)(t->c->d + at));
}

static void set_field(Tentacle* t, uint16_t at, uint16_t v) {
  wram_w16(t->w, (uint16_t)(t->c->d + at), v);
}

// A word of its record.
static uint16_t word_at(const Tentacle* t, uint16_t record, uint16_t at) {
  return wram_r16(t->w, (uint16_t)(record + at));
}

static void set_word_at(Tentacle* t, uint16_t record, uint16_t at,
                        uint16_t v) {
  wram_w16(t->w, (uint16_t)(record + at), v);
}

// A word of one of its tables, which are in its own bank.
static uint16_t table(const Tentacle* t, uint16_t at) {
  return rom_word(t->rom, (uint32_t)TENTACLE_BANK << 16 | at);
}

static void ran(Tentacle* t, TentacleRun run) { t->k->runs[run]++; }

// A branch: counted if it is taken.
static bool took(Tentacle* t, bool taken) {
  if (taken) t->k->taken++;
  return taken;
}

static bool negative(uint16_t v) { return (v & 0x8000u) != 0; }

static void lda(Tentacle* t, uint16_t v) {
  t->c->a = v;
  set_nz16(t->c, v);
}

static void ldx(Tentacle* t, uint16_t v) {
  t->c->x = v;
  set_nz16(t->c, v);
}

static void ldy(Tentacle* t, uint16_t v) {
  t->c->y = v;
  set_nz16(t->c, v);
}

// `ASL`, `INC` and `DEC`, of what is in A.
static void doubled(Tentacle* t) { t->c->a = asl16(t->c, t->c->a); }
static void one_more(Tentacle* t) { lda(t, (uint16_t)(t->c->a + 1)); }
static void one_less(Tentacle* t) { lda(t, (uint16_t)(t->c->a - 1)); }

// `CLC : ADC`, and `SEC : SBC`.
static uint16_t add(Tentacle* t, uint16_t to, uint16_t v) {
  set_c(t->c, false);
  t->k->v_known = true;
  return t->c->a = adc16(t->c, to, v);
}

static uint16_t sub(Tentacle* t, uint16_t from, uint16_t v) {
  set_c(t->c, true);
  t->k->v_known = true;
  return t->c->a = sbc16(t->c, from, v);
}

// `DEC` of a word of the direct page.
static uint16_t count_down(Tentacle* t, uint16_t at) {
  const uint16_t v = (uint16_t)(field(t, at) - 1);
  set_field(t, at, v);
  set_nz16(t->c, v);
  return v;
}

// A `JSR` or a `JSL` at `at`, and coming back from one.
static void jsr(Tentacle* t, uint16_t at) {
  push16(t->w, t->c, (uint16_t)(at + 2));
}

static void rts(Tentacle* t) {
  (void)pull16(t->w, t->c);
  ran(t, TENTACLE_RUN_RTS);
}

static void jsl(Tentacle* t, uint16_t at) {
  push8(t->w, t->c, TENTACLE_BANK);
  push16(t->w, t->c, (uint16_t)(at + 3));
}

static void rtl(Tentacle* t) {
  (void)pull16(t->w, t->c);
  (void)pull8(t->w, t->c);
}

static void set_flags(Tentacle* t, bool n, bool z, bool carry) {
  PortCpu* c = t->c;
  c->p = (uint8_t)(c->p & ~(PORT_P_N | PORT_P_Z));
  if (n) c->p |= PORT_P_N;
  if (z) c->p |= PORT_P_Z;
  set_c(c, carry);
}

// What a routine called with `PHD` leaves in N and Z when it pulls it.
static void set_flags_pld(Tentacle* t, bool carry) {
  set_flags(t, negative(t->c->d), t->c->d == 0, carry);
}

// --- What it calls ----------------------------------------------------------

// `JSL rng_next` at `at`.
static uint16_t draw(Tentacle* t, uint16_t at) {
  RngResult r;
  jsl(t, at);
  rng_next(t->w, flag(t->c, PORT_P_C), &r);
  rtl(t);
  t->k->drew = true;
  t->k->draw_twice = r.v;
  t->c->a = r.a;
  set_flags(t, r.n, r.z, r.c);
  set_v(t->c, r.v);
  t->k->v_known = true;
  return r.a;
}

// `JSL terrain_blocked_enemy` at `at`: is the ground solid at the point in X
// and Y?
static bool ground_is_solid(Tentacle* t, uint16_t at) {
  PortCpu* c = t->c;
  TerrainRegs r;
  jsl(t, at);
  terrain_blocked_enemy(t->w, c->x, c->y, &r);
  rtl(t);
  t->k->grounds[r.blocked][r.probes]++;
  c->a = r.a;
  c->x = r.x;
  c->y = r.y;
  set_flags_pld(t, r.blocked);
  set_v(c, r.v);
  t->k->v_known = true;
  return r.blocked;
}

// `JSL actor_at_point` at `at`: is anybody but the record in A standing
// there?
static bool someone_is_at(Tentacle* t, uint16_t at) {
  PortCpu* c = t->c;
  AtPointRegs r;
  jsl(t, at);
  actor_at_point_counted(t->w, c->a, c->x, c->y, &r,
                         &t->k->at_points[t->k->at_point_count++]);
  rtl(t);
  c->a = r.a;
  c->x = r.x;
  c->y = r.y;
  set_flags_pld(t, r.found);
  if (r.v_set) set_v(c, r.v);
  return r.found;
}

// `JSL terrain_blocked` at `at`: the same question of the ground, as a
// player's step would ask it.
static bool ground_is_shut(Tentacle* t, uint16_t at) {
  PortCpu* c = t->c;
  TerrainRegs* r = &t->k->room;
  jsl(t, at);
  terrain_blocked(t->w, c->x, c->y, r);
  rtl(t);
  t->k->asked_room = true;
  c->a = r->a;
  c->x = r->x;
  c->y = r->y;
  set_flags_pld(t, r->blocked);
  set_v(c, r->v);
  t->k->v_known = true;
  return r->blocked;
}

// `JSL terrain_out_of_bounds` at `at`: is the point past the level's edge?
static bool off_the_level(Tentacle* t, uint16_t at) {
  PortCpu* c = t->c;
  BoundsRegs r;
  jsl(t, at);
  terrain_out_of_bounds(t->w, c->x, c->y, &r);
  rtl(t);
  t->k->asked_edge = true;
  t->k->edge = r.exit;
  c->a = r.a;
  set_flags(t, r.n, r.z, r.c);
  return r.c;
}

// `LDX $0E : LDY $10 : JSL actor_nearest`, the `JSL` at `at`: how far the
// nearest thing is from where it stands. Who that is is left in X.
static uint16_t nearest_gap(Tentacle* t, uint16_t at) {
  PortCpu* c = t->c;
  ldx(t, field(t, TENTACLE_DP_X));
  ldy(t, field(t, TENTACLE_DP_Y));
  jsl(t, at);
  uint16_t gap = 0;
  c->x = actor_nearest_counted(t->w, c->x, c->y, &gap, &t->k->nearest);
  rtl(t);
  t->k->sought = true;
  t->k->v_known = false;
  c->a = gap;
  set_flags_pld(t, false);
  return gap;
}

// --- What the states share --------------------------------------------------

// The point a step from where it is, into `$12` and `$14`. X is the step
// table's row: a way times two.
static void aim_a_step(Tentacle* t) {
  const uint16_t row = t->c->x;
  set_field(t, TENTACLE_DP_NEXT_X,
            add(t, table(t, (uint16_t)(TENTACLE_STEPS + row)),
                field(t, TENTACLE_DP_X)));
  set_field(t, TENTACLE_DP_NEXT_Y,
            add(t, table(t, (uint16_t)(TENTACLE_STEPS + 2 + row)),
                field(t, TENTACLE_DP_Y)));
}

// `LDA $16 : ASL : TAX`, and the point a step that way.
static void aim_ahead(Tentacle* t) {
  lda(t, field(t, TENTACLE_DP_WAY));
  doubled(t);
  ldx(t, t->c->a);
  aim_a_step(t);
}

// It goes to the point. Its record is moved when its picture is seen to.
static void step_there(Tentacle* t) {
  lda(t, field(t, TENTACLE_DP_NEXT_X));
  set_field(t, TENTACLE_DP_X, t->c->a);
  lda(t, field(t, TENTACLE_DP_NEXT_Y));
  set_field(t, TENTACLE_DP_Y, t->c->a);
}

// `$82:97E0`.
static void begin_walking(Tentacle* t) {
  lda(t, TENTACLE_STATE_WALKING);
  set_field(t, TENTACLE_DP_STATE, t->c->a);
  ran(t, TENTACLE_RUN_97E0);
  (void)pull16(t->w, t->c);  // its `RTS` is the run's last instruction
}

// `$82:97D0`: one of the four straight ways by a draw, and it walks.
static void wanders_off(Tentacle* t) {
  lda(t, draw(t, 0x97d0) & 0x0003u);
  doubled(t);
  doubled(t);
  one_more(t);
  one_more(t);
  set_field(t, TENTACLE_DP_WAY, t->c->a);
  ran(t, TENTACLE_RUN_97D0);
  begin_walking(t);
}

// `$82:97BE`: a quarter turn to its right, and it follows from there.
static void turns_right(Tentacle* t) {
  PORT_COVER(tentacle_turns_right);
  lda(t, field(t, TENTACLE_DP_WAY));
  one_less(t);
  one_less(t);
  lda(t, add(t, t->c->a, TENTACLE_QUARTER_TURN) & 0x000fu);
  one_more(t);
  one_more(t);
  set_field(t, TENTACLE_DP_WAY, t->c->a);
  ran(t, TENTACLE_RUN_97BE);

  lda(t, TENTACLE_STATE_FOLLOWING);
  set_field(t, TENTACLE_DP_STATE, t->c->a);
  ran(t, TENTACLE_RUN_980E);
  (void)pull16(t->w, t->c);
}

// `$82:9786`: who is about? Somebody near and it will chase. Nobody for a
// long way, and it asks whether either player is in reach of where its
// record is, which is where it then stands. If not, it is to leave.
static void looks_for_someone(Tentacle* t) {
  PortCpu* c = t->c;
  const uint16_t gap = nearest_gap(t, 0x978a);
  cmp16(c, gap, TENTACLE_SEES);
  ran(t, TENTACLE_RUN_9786);
  if (took(t, gap < TENTACLE_SEES)) {
    PORT_COVER(tentacle_sees_someone);
    ran(t, TENTACLE_RUN_97BA);
    lda(t, TENTACLE_STATE_CHASING);
    set_field(t, TENTACLE_DP_STATE, c->a);
    ran(t, TENTACLE_RUN_983F);
    (void)pull16(t->w, c);
    return;
  }
  cmp16(c, gap, TENTACLE_FAR);
  ran(t, TENTACLE_RUN_9793);
  if (took(t, gap < TENTACLE_FAR)) {
    rts(t);
    return;
  }

  ldy(t, c->x);
  ldx(t, field(t, TENTACLE_DP_RECORD));
  jsl(t, 0x979b);
  actor_snap_to(t->w, c->x, c->y, &t->k->snap);
  rtl(t);
  lda(t, word_at(t, c->x, ACTOR_X));
  set_field(t, TENTACLE_DP_X, c->a);
  lda(t, word_at(t, c->x, ACTOR_Y));
  set_field(t, TENTACLE_DP_Y, c->a);
  lda(t, rom_word(t->rom, TENTACLE_REACH_AT));
  ldx(t, field(t, TENTACLE_DP_X));
  ldy(t, field(t, TENTACLE_DP_Y));
  jsl(t, 0x97b0);
  PlayerPickRegs* r = &t->k->players;
  player_bearing(t->w, t->rom, c->a, c->x, c->y, r);
  rtl(t);
  t->k->asked_players = true;
  t->k->v_known = false;
  c->y = r->y;
  ldx(t, r->a);
  c->a = r->a;
  set_c(c, r->c);
  ran(t, TENTACLE_RUN_9798);
  if (!took(t, c->a != 0)) {
    PORT_COVER(tentacle_nobody_about);
    count_down(t, TENTACLE_DP_LEAVE);
    ran(t, TENTACLE_RUN_97B7);
  }
  rts(t);
}

// `$82:96FD`: is the step refused? By the ground, or by somebody standing
// there.
static bool step_is_refused(Tentacle* t) {
  ldx(t, field(t, TENTACLE_DP_NEXT_X));
  ldy(t, field(t, TENTACLE_DP_NEXT_Y));
  bool refused = ground_is_solid(t, 0x9701);
  ran(t, TENTACLE_RUN_96FD);
  if (!took(t, refused)) {
    lda(t, field(t, TENTACLE_DP_RECORD));
    ldx(t, field(t, TENTACLE_DP_NEXT_X));
    ldy(t, field(t, TENTACLE_DP_NEXT_Y));
    refused = someone_is_at(t, 0x970d);
    ran(t, TENTACLE_RUN_9707);
    took(t, refused);  // a `BCS` over one `RTS` to another
  }
  rts(t);
  return refused;
}

// `$82:9715`: could it step to its left? Then it turns that way.
static void tries_left(Tentacle* t) {
  PortCpu* c = t->c;
  lda(t, field(t, TENTACLE_DP_WAY));
  one_less(t);
  one_less(t);
  lda(t, sub(t, c->a, TENTACLE_QUARTER_TURN) & 0x000fu);
  one_more(t);
  one_more(t);
  set_field(t, TENTACLE_DP_LEFT_WAY, c->a);
  doubled(t);
  ldx(t, c->a);
  aim_a_step(t);
  ldx(t, field(t, TENTACLE_DP_NEXT_X));
  ldy(t, field(t, TENTACLE_DP_NEXT_Y));
  const bool solid = ground_is_solid(t, 0x973a);
  ran(t, TENTACLE_RUN_9715);
  if (!took(t, solid)) {
    lda(t, field(t, TENTACLE_DP_RECORD));
    ldx(t, field(t, TENTACLE_DP_NEXT_X));
    ldy(t, field(t, TENTACLE_DP_NEXT_Y));
    const bool someone = someone_is_at(t, 0x9746);
    ran(t, TENTACLE_RUN_9740);
    if (!took(t, someone)) {
      PORT_COVER(tentacle_turns_left);
      lda(t, field(t, TENTACLE_DP_LEFT_WAY));
      set_field(t, TENTACLE_DP_WAY, c->a);
      ran(t, TENTACLE_RUN_974C);
    }
  }
  rts(t);
}

// One axis of `$82:9751`: may it stand at the point in X and Y? `ground` and
// `someone` are the two runs, and their calls are 4 and 10 bytes in.
static bool may_stand(Tentacle* t, uint16_t x, uint16_t y, uint16_t at,
                      TentacleRun ground, TentacleRun someone) {
  ldx(t, x);
  ldy(t, y);
  const bool solid = ground_is_solid(t, (uint16_t)(at + 4));
  ran(t, ground);
  if (took(t, solid)) return false;
  lda(t, field(t, TENTACLE_DP_RECORD));
  ldx(t, x);
  ldy(t, y);
  const bool taken = someone_is_at(t, (uint16_t)(at + 0x10));
  ran(t, someone);
  return !took(t, taken);
}

// `$82:9751`: the step an axis at a time. Across if it may, and then down
// from wherever that left it.
static void slides(Tentacle* t) {
  if (may_stand(t, field(t, TENTACLE_DP_NEXT_X), field(t, TENTACLE_DP_Y),
                0x9751, TENTACLE_RUN_9751, TENTACLE_RUN_975B)) {
    lda(t, field(t, TENTACLE_DP_NEXT_X));
    set_field(t, TENTACLE_DP_X, t->c->a);
    ran(t, TENTACLE_RUN_9767);
  }
  if (may_stand(t, field(t, TENTACLE_DP_X), field(t, TENTACLE_DP_NEXT_Y),
                0x976b, TENTACLE_RUN_976B, TENTACLE_RUN_9775)) {
    lda(t, field(t, TENTACLE_DP_NEXT_Y));
    set_field(t, TENTACLE_DP_Y, t->c->a);
    ran(t, TENTACLE_RUN_9781);
  }
  rts(t);
}

// `$82:9863`: a step towards who it chases. Which way they are is asked of
// its record.
static void steps_towards(Tentacle* t) {
  PortCpu* c = t->c;
  ldx(t, field(t, TENTACLE_DP_RECORD));
  ldy(t, field(t, TENTACLE_DP_TARGET));
  jsl(t, 0x9867);
  ActorBearingRegs* r = &t->k->bearings[t->k->bearing_count++];
  actor_bearing(t->w, t->rom, c->x, c->y, r);
  rtl(t);
  t->k->v_known = false;
  c->a = r->a;
  c->x = r->x;
  set_flags_pld(t, r->c);

  doubled(t);
  set_field(t, TENTACLE_DP_WAY, c->a);
  doubled(t);
  ldx(t, c->a);
  aim_a_step(t);
  jsr(t, 0x9880);
  ran(t, TENTACLE_RUN_9863);
  slides(t);
  rts(t);
}

// --- The states -------------------------------------------------------------

// `$82:97E6`.
static void walking(Tentacle* t) {
  jsr(t, 0x97e6);
  ran(t, TENTACLE_RUN_97E6);
  looks_for_someone(t);
  aim_ahead(t);
  jsr(t, 0x97fd);
  ran(t, TENTACLE_RUN_97E9);
  const bool refused = step_is_refused(t);
  ran(t, TENTACLE_RUN_9800);
  if (took(t, refused)) {
    PORT_COVER(tentacle_walk_refused);
    ran(t, TENTACLE_RUN_980B);
    turns_right(t);
    return;
  }
  PORT_COVER(tentacle_walks);
  step_there(t);
  ran(t, TENTACLE_RUN_9802);
  (void)pull16(t->w, t->c);
}

// `$82:9814`.
static void following(Tentacle* t) {
  jsr(t, 0x9814);
  ran(t, TENTACLE_RUN_9814);
  looks_for_someone(t);
  jsr(t, 0x9817);
  ran(t, TENTACLE_RUN_9817);
  tries_left(t);
  aim_ahead(t);
  jsr(t, 0x982e);
  ran(t, TENTACLE_RUN_981A);
  const bool refused = step_is_refused(t);
  ran(t, TENTACLE_RUN_9831);
  if (took(t, refused)) {
    ran(t, TENTACLE_RUN_983C);
    turns_right(t);
    return;
  }
  PORT_COVER(tentacle_follows);
  step_there(t);
  ran(t, TENTACLE_RUN_9833);
  (void)pull16(t->w, t->c);
}

// `$82:9845`.
static void chasing(Tentacle* t) {
  PortCpu* c = t->c;
  const uint16_t gap = nearest_gap(t, 0x9849);
  set_field(t, TENTACLE_DP_TARGET, c->x);
  cmp16(c, gap, TENTACLE_LOSES);
  ran(t, TENTACLE_RUN_9845);
  if (!took(t, gap < TENTACLE_LOSES)) {
    PORT_COVER(tentacle_loses_them);
    ran(t, TENTACLE_RUN_9854);
    begin_walking(t);
    return;
  }
  lda(t, draw(t, 0x9857) & 0x0002u);
  ran(t, TENTACLE_RUN_9857);
  if (!took(t, c->a != 0)) {
    // A `JSR` to the step, which comes back to the step.
    PORT_COVER(tentacle_chases_twice);
    jsr(t, 0x9860);
    ran(t, TENTACLE_RUN_9860);
    steps_towards(t);
  }
  PORT_COVER(tentacle_chases);
  steps_towards(t);
}

// --- Its thread -------------------------------------------------------------

// `$82:9927`: nothing has hit it this frame, and it sleeps two ticks.
static uint32_t sleeps(Tentacle* t) {
  set_field(t, TENTACLE_DP_HIT_ID, 0);
  lda(t, 0x0002);
  ran(t, TENTACLE_RUN_9927);
  return TENTACLE_SLEEP_PC;
}

// `$82:993B`: is it to end?
static uint32_t turn_ends(Tentacle* t) {
  lda(t, field(t, TENTACLE_DP_LEAVE));
  ran(t, TENTACLE_RUN_993B);
  if (took(t, t->c->a == 0)) return sleeps(t);
  PORT_COVER(tentacle_ends);
  return TENTACLE_DIES_PC;
}

// `$82:99CB`: its record goes to where it is.
static uint32_t shown(Tentacle* t) {
  PortCpu* c = t->c;
  ldy(t, field(t, TENTACLE_DP_RECORD));
  lda(t, field(t, TENTACLE_DP_X));
  set_word_at(t, c->y, ACTOR_X, c->a);
  lda(t, field(t, TENTACLE_DP_Y));
  set_word_at(t, c->y, ACTOR_Y, c->a);
  ran(t, TENTACLE_RUN_99CB);
  (void)pull16(t->w, c);
  return turn_ends(t);
}

// `$82:9994`: every third frame, the next of four pictures for the way it
// faces, mirrored for the ways to its left. After the fourth it rests.
static uint32_t pictured(Tentacle* t) {
  PortCpu* c = t->c;
  jsr(t, 0x9938);
  ran(t, TENTACLE_RUN_9938);
  const uint16_t left = count_down(t, TENTACLE_DP_TIMER);
  ran(t, TENTACLE_RUN_9994);
  if (took(t, !negative(left))) return shown(t);

  lda(t, TENTACLE_PICTURE_FRAMES);
  set_field(t, TENTACLE_DP_TIMER, c->a);
  lda(t, field(t, TENTACLE_DP_WAY));
  doubled(t);
  lda(t, c->a | field(t, TENTACLE_DP_PHASE));
  doubled(t);
  ldy(t, c->a);
  ldx(t, field(t, TENTACLE_DP_RECORD));
  lda(t, table(t, (uint16_t)(TENTACLE_PICTURES + c->y)));
  set_word_at(t, c->x, ACTOR_META, c->a);
  lda(t, word_at(t, c->x, ACTOR_FLAGS));
  cmp16(c, c->y, TENTACLE_FIRST_MIRRORED);
  ran(t, TENTACLE_RUN_9998);
  if (took(t, c->y >= TENTACLE_FIRST_MIRRORED)) {
    lda(t, c->a | 0x0002u);
    ran(t, TENTACLE_RUN_99B9);
  } else {
    lda(t, c->a & 0xfffdu);
    took(t, true);
    ran(t, TENTACLE_RUN_99B4);
  }
  set_word_at(t, c->x, ACTOR_FLAGS, c->a);
  lda(t, field(t, TENTACLE_DP_PHASE));
  one_more(t);
  lda(t, c->a & 0x0003u);
  set_field(t, TENTACLE_DP_PHASE, c->a);
  ran(t, TENTACLE_RUN_99BC);
  if (!took(t, c->a == 0)) return shown(t);

  PORT_COVER(tentacle_rests);
  lda(t, TENTACLE_REST_TICKS);
  ran(t, TENTACLE_RUN_99D8);
  return TENTACLE_REST_PC;
}

// `$82:9913`: was there room for one more? Then where it comes in: off one
// side of the screen or the other, on the row it was spawned at, unless the
// ground is solid there or that is past the level's edge.
static uint32_t comes_in(Tentacle* t) {
  PortCpu* c = t->c;
  ran(t, TENTACLE_RUN_9913);
  if (took(t, flag(c, PORT_P_C))) {
    PORT_COVER(tentacle_no_room);
    return TENTACLE_GONE_PC;
  }
  jsr(t, 0x9915);
  ran(t, TENTACLE_RUN_9915);
  jsr(t, 0x9884);
  ran(t, TENTACLE_RUN_9884);

  lda(t, draw(t, 0x98e6) & 0x0002u);
  ldx(t, c->a);
  add(t, table(t, (uint16_t)(TENTACLE_WINGS + c->x)),
      wram_r16(t->w, W_CAMERA_X));
  set_field(t, TENTACLE_DP_SPAWN_X, c->a);
  ldx(t, c->a);
  ldy(t, field(t, TENTACLE_DP_SPAWN_Y));
  bool refused = ground_is_shut(t, 0x98fa);
  ran(t, TENTACLE_RUN_98E6);
  if (took(t, refused)) {
    PORT_COVER(tentacle_no_ground);
  } else {
    ldx(t, field(t, TENTACLE_DP_SPAWN_X));
    ldy(t, field(t, TENTACLE_DP_SPAWN_Y));
    refused = off_the_level(t, 0x9904);
    ran(t, TENTACLE_RUN_9900);
    if (took(t, refused)) PORT_COVER(tentacle_past_edge);
  }
  rts(t);

  ran(t, TENTACLE_RUN_9887);
  if (took(t, refused)) {
    rts(t);
    ran(t, TENTACLE_RUN_9918);
    took(t, true);
    return TENTACLE_GONE_PC;
  }
  PORT_COVER(tentacle_comes_in);
  return TENTACLE_RECORD_PC;
}

// `$82:988D`: the record in A is its own. It stands where it comes in, in
// its first picture, with one hit of health. Then it wants its handler.
static uint32_t set_up(Tentacle* t) {
  PortCpu* c = t->c;
  set_field(t, TENTACLE_DP_RECORD, c->a);
  ldy(t, c->a);
  lda(t, field(t, TENTACLE_DP_SPAWN_X));
  set_field(t, TENTACLE_DP_X, c->a);
  set_word_at(t, c->y, ACTOR_X, c->a);
  set_word_at(t, c->y, ACTOR_Z, 0);
  lda(t, field(t, TENTACLE_DP_SPAWN_Y));
  set_field(t, TENTACLE_DP_Y, c->a);
  set_word_at(t, c->y, ACTOR_Y, c->a);
  set_word_at(t, c->y, ACTOR_META, TENTACLE_FIRST_PICTURE);
  set_word_at(t, c->y, ACTOR_META_BANK, TENTACLE_META_BANK);
  set_word_at(t, c->y, ACTOR_THREAD, wram_r16(t->w, W_SCHED_CUR_TASK));
  set_word_at(t, c->y, ACTOR_COLLIDE_ID, TENTACLE_COLLIDE_ID);
  set_word_at(t, c->y, ACTOR_FLAGS,
              ACTOR_DRAW | word_at(t, c->y, ACTOR_FLAGS));
  set_word_at(t, c->y, ACTOR_ATTR, TENTACLE_ATTR);
  set_field(t, TENTACLE_DP_HEALTH, TENTACLE_HEALTH);
  set_field(t, TENTACLE_DP_PHASE, 0);
  set_field(t, TENTACLE_DP_TIMER, 0);
  set_field(t, TENTACLE_DP_LEAVE, 0);
  set_field(t, TENTACLE_DP_HIT_ID, 0);
  set_field(t, TENTACLE_DP_HITS, 0);
  lda(t, TENTACLE_HANDLER);
  ldy(t, TENTACLE_BANK);
  ran(t, TENTACLE_RUN_988D);
  return TENTACLE_HANDLER_PC;
}

// `$82:98E4`: it is one more for the level to carry, it walks off a random
// way, and it sleeps.
static uint32_t settles(Tentacle* t) {
  set_c(t->c, false);
  ran(t, TENTACLE_RUN_98E4);
  (void)pull16(t->w, t->c);
  ran(t, TENTACLE_RUN_9918);
  wram_w16(t->w, W_SPAWN_LOAD,
           add(t, wram_r16(t->w, W_SPAWN_LOAD), TENTACLE_WEIGHT));
  jsr(t, 0x9924);
  ran(t, TENTACLE_RUN_991A);
  wanders_off(t);
  return sleeps(t);
}

// `$82:9930`: a frame. Its state's own routine, and then its picture.
static uint32_t wakes(Tentacle* t) {
  PortCpu* c = t->c;
  push16(t->w, c, 0x9937);
  lda(t, field(t, TENTACLE_DP_STATE));
  one_less(t);
  push16(t->w, c, c->a);
  ran(t, TENTACLE_RUN_9930);
  switch ((uint16_t)(pull16(t->w, c) + 1)) {
    case TENTACLE_STATE_WALKING: walking(t); break;
    case TENTACLE_STATE_FOLLOWING: following(t); break;
    default: chasing(t); break;
  }
  return pictured(t);
}

// `$82:994A`: one more killed. Nothing can touch it now, and it cries out.
static uint32_t scored(Tentacle* t) {
  PortCpu* c = t->c;
  wram_w16(t->w, W_TENTACLES_KILLED,
           (uint16_t)(wram_r16(t->w, W_TENTACLES_KILLED) + 1));
  ldy(t, field(t, TENTACLE_DP_RECORD));
  set_word_at(t, c->y, ACTOR_COLLIDE_ID, 0);
  set_word_at(t, c->y, ACTOR_META_BANK, TENTACLE_META_BANK);
  lda(t, TENTACLE_CRY);
  ran(t, TENTACLE_RUN_994A);
  return TENTACLE_CRY_PC;
}

// `$82:9962`: ...and its death is played.
static uint32_t cried(Tentacle* t) {
  lda(t, TENTACLE_DEATH_PICTURES);
  ran(t, TENTACLE_RUN_9962);
  return TENTACLE_DEATH_PC;
}

// `$82:99DF`: a rest is over. By a draw, it looks round first.
static uint32_t rested(Tentacle* t) {
  PortCpu* c = t->c;
  set_field(t, TENTACLE_DP_TIMER, 0);
  const uint16_t drawn = draw(t, 0x99e1);
  cmp16(c, drawn, TENTACLE_LOOKS_UNDER);
  ran(t, TENTACLE_RUN_99DF);
  if (took(t, drawn >= TENTACLE_LOOKS_UNDER)) return shown(t);
  PORT_COVER(tentacle_looks_round);
  lda(t, TENTACLE_LOOK_PICTURES);
  ran(t, TENTACLE_RUN_99EA);
  return TENTACLE_LOOK_PC;
}

// `$82:99F1`.
static uint32_t looked(Tentacle* t) {
  took(t, true);
  ran(t, TENTACLE_RUN_99F1);
  return shown(t);
}

// --- The stretches ----------------------------------------------------------

static const struct {
  uint32_t pc;
  uint32_t (*run)(Tentacle* t);
} STRETCHES[] = {
    {TENTACLE_COMES_IN_PC, comes_in}, {TENTACLE_SET_UP_PC, set_up},
    {TENTACLE_SETTLES_PC, settles},   {TENTACLE_WAKES_PC, wakes},
    {TENTACLE_SCORED_PC, scored},     {TENTACLE_CRIED_PC, cried},
    {TENTACLE_RESTED_PC, rested},     {TENTACLE_LOOKED_PC, looked},
};

bool tentacle_begins_at(uint32_t pc) {
  for (unsigned i = 0; i < sizeof STRETCHES / sizeof STRETCHES[0]; i++)
    if (STRETCHES[i].pc == pc) return true;
  return false;
}

bool tentacle_state_known(uint16_t state) {
  return state == TENTACLE_STATE_WALKING ||
         state == TENTACLE_STATE_FOLLOWING || state == TENTACLE_STATE_CHASING;
}

void tentacle_run(Wram* w, const Rom* rom, PortCpu* c, TentacleWork* k) {
  Tentacle t = {w, rom, c, k};
  k->v_known = true;
  for (unsigned i = 0; i < sizeof STRETCHES / sizeof STRETCHES[0]; i++) {
    if (STRETCHES[i].pc != c->pc) continue;
    c->pc = STRETCHES[i].run(&t);
    return;
  }
}
