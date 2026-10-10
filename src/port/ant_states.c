// The giant ant's states -- see port/ant_states.h.

#include "port/ant_states.h"

#include "port/chase.h"
#include "port/coverage.h"
#include "port/ant.h"
#include "port/collide.h"  // ANT_DP_HIT_ID
#include "port/rng.h"

typedef struct {
  Wram* w;
  const Rom* rom;
  PortCpu* c;
  AntStatesWork* k;
} Ant;

// --- The machine, as these stretches use it ---------------------------------

static uint16_t field(const Ant* m, uint16_t at) {
  return wram_r16(m->w, (uint16_t)(m->c->d + at));
}

static void set_field(Ant* m, uint16_t at, uint16_t v) {
  wram_w16(m->w, (uint16_t)(m->c->d + at), v);
}

// A word of its record.
static uint16_t word_at(const Ant* m, uint16_t record, uint16_t at) {
  return wram_r16(m->w, (uint16_t)(record + at));
}

static void set_word_at(Ant* m, uint16_t record, uint16_t at, uint16_t v) {
  wram_w16(m->w, (uint16_t)(record + at), v);
}

// A word of one of its tables, which are in its own bank.
static uint16_t table(const Ant* m, uint16_t at) {
  return rom_word(m->rom, (uint32_t)ANT_STATES_BANK << 16 | at);
}

static void ran(Ant* m, AntRun run) { m->k->runs[run]++; }

// A branch: counted if it is taken.
static bool took(Ant* m, bool taken) {
  if (taken) m->k->taken++;
  return taken;
}

static bool negative(uint16_t v) { return (v & 0x8000u) != 0; }

static void lda(Ant* m, uint16_t v) {
  m->c->a = v;
  set_nz16(m->c, v);
}

static void ldx(Ant* m, uint16_t v) {
  m->c->x = v;
  set_nz16(m->c, v);
}

static void ldy(Ant* m, uint16_t v) {
  m->c->y = v;
  set_nz16(m->c, v);
}

// `ASL`, of what is in A.
static void doubled(Ant* m) { m->c->a = asl16(m->c, m->c->a); }

// `CLC : ADC`, and `SEC : SBC`.
static uint16_t add(Ant* m, uint16_t to, uint16_t v) {
  set_c(m->c, false);
  m->k->v_known = true;
  return m->c->a = adc16(m->c, to, v);
}

static uint16_t sub(Ant* m, uint16_t from, uint16_t v) {
  set_c(m->c, true);
  m->k->v_known = true;
  return m->c->a = sbc16(m->c, from, v);
}

// `INC` of a word of the direct page.
static void count_up(Ant* m, uint16_t at) {
  const uint16_t v = (uint16_t)(field(m, at) + 1);
  set_field(m, at, v);
  set_nz16(m->c, v);
}

// A `JSR` or a `JSL` at `at`, and coming back from one.
static void jsr(Ant* m, uint16_t at) {
  push16(m->w, m->c, (uint16_t)(at + 2));
}

static void rts(Ant* m) { (void)pull16(m->w, m->c); }

static void jsl(Ant* m, uint16_t at) {
  push8(m->w, m->c, ANT_STATES_BANK);
  push16(m->w, m->c, (uint16_t)(at + 3));
}

static void rtl(Ant* m) {
  (void)pull16(m->w, m->c);
  (void)pull8(m->w, m->c);
}

static void set_flags(Ant* m, bool n, bool z, bool carry) {
  PortCpu* c = m->c;
  c->p = (uint8_t)(c->p & ~(PORT_P_N | PORT_P_Z));
  if (n) c->p |= PORT_P_N;
  if (z) c->p |= PORT_P_Z;
  set_c(c, carry);
}

// What a routine called with `PHD` leaves in N and Z when it pulls it.
static void set_flags_pld(Ant* m, bool carry) {
  set_flags(m, negative(m->c->d), m->c->d == 0, carry);
}

// --- What it calls ----------------------------------------------------------

// `JSL rng_next` at `at`.
static uint16_t draw(Ant* m, uint16_t at) {
  RngResult r;
  jsl(m, at);
  rng_next(m->w, flag(m->c, PORT_P_C), &r);
  rtl(m);
  m->k->drew = true;
  m->k->draw_twice = r.v;
  m->c->a = r.a;
  set_flags(m, r.n, r.z, r.c);
  set_v(m->c, r.v);
  m->k->v_known = true;
  return r.a;
}

// `JSL terrain_blocked_enemy` at `at`: is the ground solid at the point in X
// and Y?
static bool ground_is_solid(Ant* m, uint16_t at) {
  PortCpu* c = m->c;
  TerrainRegs r;
  jsl(m, at);
  terrain_blocked_enemy(m->w, c->x, c->y, &r);
  rtl(m);
  m->k->grounds[r.blocked][r.probes]++;
  c->a = r.a;
  c->x = r.x;
  c->y = r.y;
  set_flags_pld(m, r.blocked);
  set_v(c, r.v);
  m->k->v_known = true;
  return r.blocked;
}

// `JSL actor_at_point` at `at`: is anybody but the record in A standing
// there?
static bool someone_is_at(Ant* m, uint16_t at) {
  PortCpu* c = m->c;
  AtPointRegs r;
  jsl(m, at);
  actor_at_point_counted(m->w, c->a, c->x, c->y, &r,
                         &m->k->at_points[m->k->at_point_count++]);
  rtl(m);
  c->a = r.a;
  c->x = r.x;
  c->y = r.y;
  set_flags_pld(m, r.found);
  if (r.v_set) set_v(c, r.v);
  return r.found;
}

// `JSL terrain_blocked` at `at`: the same question, as a player's step
// would ask it.
static bool ground_is_shut(Ant* m, uint16_t at) {
  PortCpu* c = m->c;
  TerrainRegs* r = &m->k->room;
  jsl(m, at);
  terrain_blocked(m->w, c->x, c->y, r);
  rtl(m);
  m->k->asked_room = true;
  c->a = r->a;
  c->x = r->x;
  c->y = r->y;
  set_flags_pld(m, r->blocked);
  set_v(c, r->v);
  m->k->v_known = true;
  return r->blocked;
}

// `JSL terrain_out_of_bounds` at `at`: is the point past the level's edge?
static bool off_the_level(Ant* m, uint16_t at) {
  PortCpu* c = m->c;
  BoundsRegs r;
  jsl(m, at);
  terrain_out_of_bounds(m->w, c->x, c->y, &r);
  rtl(m);
  m->k->asked_edge = true;
  m->k->edge = r.exit;
  c->a = r.a;
  set_flags(m, r.n, r.z, r.c);
  return r.c;
}

// `JSL tile_attrs_at_pixel` at `at`. N and Z are its `PLB`'s, and what it
// leaves in overflow it does not say.
static uint16_t tile_attrs(Ant* m, uint16_t at) {
  PortCpu* c = m->c;
  TileAttrsRegs r;
  jsl(m, at);
  tile_attrs_at_pixel(m->w, c->x, c->y, &r);
  rtl(m);
  m->k->tiles++;
  m->k->v_known = false;
  c->a = r.a;
  set_flags(m, (c->db & 0x80u) != 0, c->db == 0, r.c);
  return r.a;
}

// --- What the states share --------------------------------------------------

// The point a step from where it is, into `$0E` and `$10`. Y is the step
// table's row: a way times two.
static void aim_a_step(Ant* m) {
  PortCpu* c = m->c;
  const uint16_t steps = field(m, CHASE_DP_STEPS);
  set_field(m, CHASE_DP_NEXT_X,
            add(m, field(m, ANT_DP_X), table(m, (uint16_t)(steps + c->y))));
  ldy(m, (uint16_t)(c->y + 2));
  set_field(m, CHASE_DP_NEXT_Y,
            add(m, field(m, ANT_DP_Y), table(m, (uint16_t)(steps + c->y))));
}

// It goes to the point: its page, and its record.
static void step_there(Ant* m) {
  PortCpu* c = m->c;
  ldy(m, field(m, ANT_DP_RECORD));
  lda(m, field(m, CHASE_DP_NEXT_X));
  set_field(m, ANT_DP_X, c->a);
  set_word_at(m, c->y, ACTOR_X, c->a);
  lda(m, field(m, CHASE_DP_NEXT_Y));
  set_field(m, ANT_DP_Y, c->a);
  set_word_at(m, c->y, ACTOR_Y, c->a);
}

// `$81:BC05`: is the step refused? By the ground, or by somebody standing
// there, and `$32` says which: 0 for the ground.
static bool step_is_refused(Ant* m) {
  lda(m, 0);
  set_field(m, CHASE_DP_BLOCKER, 0);
  ldx(m, field(m, CHASE_DP_NEXT_X));
  ldy(m, field(m, CHASE_DP_NEXT_Y));
  bool refused = ground_is_solid(m, 0xbc0e);
  ran(m, ANT_RUN_BC05);
  if (!took(m, refused)) {
    count_up(m, CHASE_DP_BLOCKER);
    lda(m, field(m, ANT_DP_RECORD));
    ldx(m, field(m, CHASE_DP_NEXT_X));
    ldy(m, field(m, CHASE_DP_NEXT_Y));
    refused = someone_is_at(m, 0xbc1c);
    ran(m, ANT_RUN_BC14);
    took(m, refused);  // a `BCS` to the next instruction
  }
  ran(m, ANT_RUN_RTS);
  rts(m);
  return refused;
}

// `$81:BC23`: the same for one that marches, which minds the level's edge
// and not who is standing there. `$32` is 1 when it was the edge.
static bool march_is_refused(Ant* m) {
  lda(m, 0);
  set_field(m, CHASE_DP_BLOCKER, 0);
  ldx(m, field(m, CHASE_DP_NEXT_X));
  ldy(m, field(m, CHASE_DP_NEXT_Y));
  bool refused = ground_is_solid(m, 0xbc2c);
  ran(m, ANT_RUN_BC23);
  if (!took(m, refused)) {
    count_up(m, CHASE_DP_BLOCKER);
    ldx(m, field(m, CHASE_DP_NEXT_X));
    ldy(m, field(m, CHASE_DP_NEXT_Y));
    refused = off_the_level(m, 0xbc38);
    ran(m, ANT_RUN_BC32);
  }
  ran(m, ANT_RUN_RTS);
  rts(m);
  return refused;
}

// Does the tile `tiles` tiles ahead have the bit that says it can be leapt?
// The point is left in `$0E` and `$10`.
static bool tile_ahead_is_leapable(Ant* m, int tiles, uint16_t call) {
  PortCpu* c = m->c;
  lda(m, field(m, ANT_DP_FACING));
  doubled(m);
  ldy(m, c->a);
  lda(m, table(m, (uint16_t)(CHASE_LEAP_PROBE + c->y)));
  if (tiles == 2) doubled(m);
  set_field(m, CHASE_DP_NEXT_X, add(m, c->a, field(m, ANT_DP_X)));
  ldx(m, c->a);
  lda(m, table(m, (uint16_t)(CHASE_LEAP_PROBE + 2 + c->y)));
  if (tiles == 2) doubled(m);
  set_field(m, CHASE_DP_NEXT_Y, add(m, c->a, field(m, ANT_DP_Y)));
  ldy(m, c->a);
  lda(m, tile_attrs(m, call) & CHASE_LEAPABLE);
  return c->a != 0;
}

// `$81:BC3D`: solid ground ahead. True unless there is a tile to leap, one
// ahead or two, and clear ground past it.
static bool nothing_to_leap(Ant* m) {
  PortCpu* c = m->c;
  bool leapable = tile_ahead_is_leapable(m, 1, 0xbc53);
  ran(m, ANT_RUN_BC3D);
  if (!took(m, leapable)) {
    leapable = tile_ahead_is_leapable(m, 2, 0xbc74);
    ran(m, ANT_RUN_BC5C);
    if (!took(m, leapable)) {
      PORT_COVER(ant_nothing_leapable);
      set_c(c, true);
      ran(m, ANT_RUN_BC7D);
      rts(m);
      return true;
    }
  }
  // Where it would land: that far past the tile.
  lda(m, field(m, ANT_DP_FACING));
  doubled(m);
  ldy(m, c->a);
  ldx(m, add(m, table(m, (uint16_t)(CHASE_LEAP_LANDING + c->y)),
             field(m, CHASE_DP_NEXT_X)));
  ldy(m, add(m, table(m, (uint16_t)(CHASE_LEAP_LANDING + 2 + c->y)),
             field(m, CHASE_DP_NEXT_Y)));
  const bool solid = ground_is_solid(m, 0xbc91);
  ran(m, ANT_RUN_BC7F);
  if (took(m, solid)) {
    PORT_COVER(ant_no_landing);
    set_c(c, true);
    ran(m, ANT_RUN_BC7D);
    rts(m);
    return true;
  }
  set_c(c, false);
  ran(m, ANT_RUN_BC97);
  rts(m);
  return false;
}

// `$81:BE0E`.
static uint32_t begin_walking(Ant* m) {
  lda(m, ANT_STATE_WANDER);
  set_field(m, ANT_DP_STATE, m->c->a);
  ran(m, ANT_RUN_BE0E);
  return ANT_WALK_BEGUN_RTS_PC;
}

// `$81:BCE1`: one of the four straight ways by a draw, and it walks.
static uint32_t wanders_off(Ant* m) {
  PORT_COVER(ant_wanders_off);
  lda(m, draw(m, 0xbce1) & 0x0003u);
  doubled(m);
  doubled(m);
  lda(m, (uint16_t)(m->c->a + 2));
  set_field(m, ANT_DP_FACING, m->c->a);
  ran(m, ANT_RUN_BCE1);
  return begin_walking(m);
}

// `$81:BE69`.
static uint32_t begin_going_round(Ant* m) {
  lda(m, ANT_STATE_GOING_ROUND);
  set_field(m, ANT_DP_STATE, m->c->a);
  set_field(m, ANT_DP_LEFT_TURNS, 0);
  ran(m, ANT_RUN_BE69);
  return ANT_ROUND_BEGUN_RTS_PC;
}

// `$81:BE41`: a quarter turn to its right, and it goes round from there.
static uint32_t turns_right(Ant* m) {
  PORT_COVER(ant_turns_right);
  set_field(m, ANT_DP_TIMER, 0);
  lda(m, (uint16_t)(field(m, ANT_DP_FACING) - 2));
  lda(m, add(m, m->c->a, ANT_QUARTER_TURN) & 0x000fu);
  lda(m, (uint16_t)(m->c->a + 2));
  set_field(m, ANT_DP_FACING, m->c->a);
  ran(m, ANT_RUN_BE41);
  return begin_going_round(m);
}

// `$81:BD1E`: where a leap comes down. Past the tile, and then on by eights
// until the ground is clear.
static uint32_t lands(Ant* m) {
  PortCpu* c = m->c;
  lda(m, field(m, ANT_DP_FACING));
  doubled(m);
  set_field(m, CHASE_DP_STEP_INDEX, c->a);
  ldx(m, c->a);
  set_field(m, CHASE_DP_NEXT_X,
            add(m, table(m, (uint16_t)(ANT_LEAP_LANDING + c->x)),
                field(m, ANT_DP_X)));
  set_field(m, CHASE_DP_NEXT_Y,
            add(m, table(m, (uint16_t)(ANT_LEAP_LANDING + 2 + c->x)),
                field(m, ANT_DP_Y)));
  ran(m, ANT_RUN_BD1E);
  for (;;) {
    ldx(m, field(m, CHASE_DP_NEXT_X));
    ldy(m, field(m, CHASE_DP_NEXT_Y));
    const bool solid = ground_is_solid(m, 0xbd38);
    ran(m, ANT_RUN_BD34);
    if (took(m, !solid)) break;
    PORT_COVER(ant_lands_further);
    ldx(m, field(m, CHASE_DP_STEP_INDEX));
    set_field(m, CHASE_DP_NEXT_X,
              add(m, table(m, (uint16_t)(ANT_LEAP_FURTHER + c->x)),
                  field(m, CHASE_DP_NEXT_X)));
    set_field(m, CHASE_DP_NEXT_Y,
              add(m, table(m, (uint16_t)(ANT_LEAP_FURTHER + 2 + c->x)),
                  field(m, CHASE_DP_NEXT_Y)));
    took(m, true);
    ran(m, ANT_RUN_BD3E);
  }
  ldy(m, field(m, ANT_DP_RECORD));
  lda(m, (uint16_t)~ACTOR_PRIORITY_TOP);
  lda(m, c->a & word_at(m, c->y, ACTOR_FLAGS));
  set_word_at(m, c->y, ACTOR_FLAGS, c->a);
  lda(m, field(m, CHASE_DP_NEXT_X));
  set_field(m, ANT_DP_X, c->a);
  set_word_at(m, c->y, ACTOR_X, c->a);
  lda(m, field(m, CHASE_DP_NEXT_Y));
  set_field(m, ANT_DP_Y, c->a);
  set_word_at(m, c->y, ACTOR_Y, c->a);
  set_field(m, ANT_DP_TIMER, 0);
  set_field(m, ANT_DP_PHASE, 0);
  ran(m, ANT_RUN_BD52);
  return begin_walking(m);
}

// `$81:BCF1`: it leaps. It is drawn over everything while it does, and
// turned to face the way it goes.
static uint32_t leaps(Ant* m) {
  PortCpu* c = m->c;
  PORT_COVER(ant_leaps);
  lda(m, ANT_STATE_LEAPING);
  set_field(m, ANT_DP_STATE, c->a);
  ldy(m, field(m, ANT_DP_RECORD));
  lda(m, ACTOR_PRIORITY_TOP);
  lda(m, c->a | word_at(m, c->y, ACTOR_FLAGS));
  set_word_at(m, c->y, ACTOR_FLAGS, c->a);
  ran(m, ANT_RUN_BCF1);

  lda(m, field(m, ANT_DP_FACING));
  doubled(m);
  ldx(m, c->a);
  lda(m, table(m, (uint16_t)(ANT_LEAP_FLAGS + c->x)));
  ran(m, ANT_RUN_BD01);
  if (took(m, negative(c->a))) {
    lda(m, c->a & word_at(m, c->y, ACTOR_FLAGS));
    ran(m, ANT_RUN_BD0F);
  } else {
    lda(m, c->a | word_at(m, c->y, ACTOR_FLAGS));
    took(m, true);
    ran(m, ANT_RUN_BD0A);
  }
  set_word_at(m, c->y, ACTOR_FLAGS, c->a);
  lda(m, table(m, (uint16_t)(ANT_LEAP_PICTURES + c->x)));
  ran(m, ANT_RUN_BD12);
  if (took(m, c->a == 0)) {
    PORT_COVER(ant_leaps_plain);
    return lands(m);
  }
  return ANT_LEAP_PICTURES_PC;
}

// --- How one is set up ------------------------------------------------------

// `$81:B9FD`: it stands where it was spawned, which is also its home, and
// its record shows its first picture. Then it has no handler.
static uint32_t set_up(Ant* m) {
  PortCpu* c = m->c;
  lda(m, field(m, ANT_DP_SPAWN_X));
  set_field(m, ANT_DP_X, c->a);
  set_field(m, ANT_DP_HOME_X, c->a);
  lda(m, field(m, ANT_DP_SPAWN_Y));
  set_field(m, ANT_DP_Y, c->a);
  set_field(m, ANT_DP_HOME_Y, c->a);
  ldy(m, field(m, ANT_DP_RECORD));
  set_word_at(m, c->y, ACTOR_META, ANT_FIRST_PICTURE);
  set_word_at(m, c->y, ACTOR_META_BANK, ANT_META_BANK);
  set_word_at(m, c->y, ACTOR_COLLIDE_ID, ANT_COLLIDE_ID);
  set_word_at(m, c->y, ACTOR_ATTR, 0);
  lda(m, ACTOR_DRAW | word_at(m, c->y, ACTOR_FLAGS));
  set_word_at(m, c->y, ACTOR_FLAGS, c->a);
  lda(m, 0);
  ldy(m, 0);
  ran(m, ANT_RUN_B9FD);
  return ANT_NO_HANDLER_PC;
}

// `$81:BA34`: its legs at rest, after nobody, holding nothing.
static uint32_t holds_nothing(Ant* m) {
  set_field(m, ANT_DP_PHASE, 0);
  set_field(m, ANT_DP_TIMER, 0);
  set_field(m, ANT_DP_LEAVE, 0);
  set_field(m, ANT_DP_HIT_ID, 0);
  set_field(m, ANT_DP_HITS, 0);
  set_field(m, ANT_DP_HELD_KIND, 0);
  lda(m, ANT_CARRY_NONE);
  set_field(m, ANT_DP_CARRIED, m->c->a);
  ran(m, ANT_RUN_BA34);
  return ANT_SET_UP_RTS_PC;
}

// What makes a kind: how much it takes, how far it steps, and its pictures,
// which are left in A for the call.
static void becomes(Ant* m, uint16_t health, uint16_t steps,
                    uint16_t pictures) {
  lda(m, health);
  set_field(m, ANT_DP_HEALTH, health);
  lda(m, steps);
  set_field(m, CHASE_DP_STEPS, steps);
  set_field(m, CHASE_DP_USUAL_STEPS, steps);
  lda(m, pictures);
}

// `$81:BA46`: the usual kind, in colours of its own.
static uint32_t usual_kind(Ant* m) {
  ldy(m, field(m, ANT_DP_RECORD));
  set_word_at(m, m->c->y, ACTOR_ATTR, ANT_USUAL_ATTR);
  becomes(m, ANT_USUAL_HEALTH, ANT_USUAL_STEPS,
          ANT_USUAL_PICTURES);
  ran(m, ANT_RUN_BA46);
  return ANT_USUAL_PICTURES_PC;
}

// `$81:BA78`: the fast kind, in the colours its record says.
static uint32_t fast_kind(Ant* m) {
  PortCpu* c = m->c;
  ldy(m, field(m, ANT_DP_RECORD));
  lda(m, ACTOR_ATTR_SET | word_at(m, c->y, ACTOR_FLAGS));
  set_word_at(m, c->y, ACTOR_FLAGS, c->a);
  set_word_at(m, c->y, ACTOR_ATTR, 0);
  becomes(m, ANT_FAST_HEALTH, CHASE_FAST_STEPS, ANT_FAST_PICTURES);
  ran(m, ANT_RUN_BA78);
  return ANT_FAST_PICTURES_PC;
}

// `$81:BFA8`: is there room for one at the top of the screen, above where
// it was spawned? Carry set is no. `$02` is left as that row.
static uint32_t asks_for_room(Ant* m) {
  PortCpu* c = m->c;
  ldx(m, field(m, ANT_DP_SPAWN_X));
  lda(m, wram_r16(m->w, W_CAMERA_Y));
  set_field(m, ANT_DP_SPAWN_Y, c->a);
  ldy(m, c->a);
  const bool shut = ground_is_shut(m, 0xbfb0);
  ran(m, ANT_RUN_BFA8);
  if (took(m, shut)) {
    PORT_COVER(ant_no_room_ground);
    return ANT_ROOM_RTS_PC;
  }
  ldx(m, field(m, ANT_DP_SPAWN_X));
  ldy(m, field(m, ANT_DP_SPAWN_Y));
  const bool outside = off_the_level(m, 0xbfba);
  ran(m, ANT_RUN_BFB6);
  if (took(m, outside)) {
    PORT_COVER(ant_no_room_edge);
    return ANT_ROOM_RTS_PC;
  }
  set_c(c, false);
  ran(m, ANT_RUN_BFC0);
  return ANT_ROOM_RTS_PC;
}

// `$81:BFC2`: it will march, down.
static uint32_t march_begins(Ant* m) {
  lda(m, ANT_STATE_MARCHING);
  set_field(m, ANT_DP_STATE, m->c->a);
  lda(m, ANT_WAY_DOWN);
  set_field(m, ANT_DP_FACING, m->c->a);
  ran(m, ANT_RUN_BFC2);
  return ANT_MARCH_BEGUN_RTS_PC;
}

// --- What it holds ----------------------------------------------------------

// The row of a table by kind for what it holds.
static uint16_t held_kind_row(uint16_t kind) {
  return (uint16_t)((uint16_t)(kind - ANT_HELD_FIRST_KIND) << 1);
}

bool ant_state_supported(const Wram* w, const Rom* rom, uint16_t page,
                         uint32_t pc) {
  const uint16_t row =
      held_kind_row(wram_r16(w, (uint16_t)(page + ANT_DP_HELD_KIND)));
  const uint32_t bank = (uint32_t)ANT_STATES_BANK << 16;
  if (pc == ANT_PICKS_UP_PC)
    return rom_word(rom, bank | (uint16_t)(ANT_HELD_PICTURES + row)) != 0;
  if (pc == ANT_PUTS_DOWN_PC)
    return rom_word(rom, bank | (uint16_t)(ANT_PUT_BACK_AS + row)) !=
           0xffffu;
  return true;
}

// `$81:C054`: the record in A is who it has caught. It is put where the
// ant is, shown by the picture for its kind, and owned by this thread.
// Then the ant wanders off with it.
static uint32_t picks_up(Ant* m) {
  PortCpu* c = m->c;
  PORT_COVER(ant_picks_up);
  set_field(m, ANT_DP_CARRIED, c->a);
  ldy(m, c->a);
  lda(m, field(m, ANT_DP_X));
  set_word_at(m, c->y, ACTOR_X, c->a);
  set_word_at(m, c->y, ACTOR_Z, ANT_HELD_HEIGHT);
  lda(m, field(m, ANT_DP_Y));
  set_word_at(m, c->y, ACTOR_Y, c->a);
  set_word_at(m, c->y, ACTOR_COLLIDE_ID, 0);
  sub(m, field(m, ANT_DP_HELD_KIND), ANT_HELD_FIRST_KIND);
  doubled(m);
  ldx(m, c->a);
  lda(m, table(m, (uint16_t)(ANT_HELD_PICTURES + c->x)));
  ran(m, ANT_RUN_C054);

  set_word_at(m, c->y, ACTOR_META, c->a);
  set_word_at(m, c->y, ACTOR_META_BANK, ANT_HELD_META_BANK);
  set_word_at(m, c->y, ACTOR_THREAD, wram_r16(m->w, W_SCHED_CUR_TASK));
  lda(m, ACTOR_DRAW | word_at(m, c->y, ACTOR_FLAGS));
  set_word_at(m, c->y, ACTOR_FLAGS, c->a);
  ran(m, ANT_RUN_C07A);
  return wanders_off(m);
}

// `$81:C0E5`: a killed one puts down who it holds. They are put back where
// it stands, as what its kind says, by the call this stops on.
static uint32_t puts_down(Ant* m) {
  PortCpu* c = m->c;
  PORT_COVER(ant_puts_down);
  set_field(m, ANT_DP_SPAWN_X, field(m, ANT_DP_X));
  set_field(m, ANT_DP_SPAWN_Y, field(m, ANT_DP_Y));
  sub(m, field(m, ANT_DP_HELD_KIND), ANT_HELD_FIRST_KIND);
  doubled(m);
  ldx(m, c->a);
  lda(m, table(m, (uint16_t)(ANT_PUT_BACK_AS + c->x)));
  cmp16(c, c->a, 0xffff);
  ran(m, ANT_RUN_C0E5);

  ldx(m, field(m, ANT_DP_SPAWN_X));
  ldy(m, field(m, ANT_DP_SPAWN_Y));
  ran(m, ANT_RUN_C0FD);
  return ANT_PUT_BACK_PC;
}

// `$81:C105`: ...and the record it held them as is freed.
static uint32_t put_down(Ant* m) {
  lda(m, field(m, ANT_DP_CARRIED));
  ran(m, ANT_RUN_C105);
  return ANT_FREE_HELD_PC;
}

// `$81:C10B`: it holds nobody, and can be touched again.
static uint32_t holds_none(Ant* m) {
  PortCpu* c = m->c;
  lda(m, ANT_CARRY_NONE);
  set_field(m, ANT_DP_CARRIED, c->a);
  set_field(m, ANT_DP_HELD_KIND, 0);
  ldy(m, field(m, ANT_DP_RECORD));
  lda(m, ANT_COLLIDE_ID);
  set_word_at(m, c->y, ACTOR_COLLIDE_ID, c->a);
  ran(m, ANT_RUN_C10B);
  return ANT_PUT_DOWN_RTS_PC;
}

// --- The states -------------------------------------------------------------

// `$81:BE14`.
static uint32_t walking(Ant* m) {
  PortCpu* c = m->c;
  lda(m, field(m, ANT_DP_FACING));
  doubled(m);
  ldy(m, c->a);
  aim_a_step(m);
  jsr(m, 0xbe28);
  const bool refused = step_is_refused(m);
  ran(m, ANT_RUN_BE14);
  ran(m, ANT_RUN_BE2B);
  if (took(m, refused)) {
    PORT_COVER(ant_walk_refused);
    ran(m, ANT_RUN_BE3E);
    return begin_going_round(m);
  }
  PORT_COVER(ant_walks);
  step_there(m);
  ran(m, ANT_RUN_BE2D);
  return ANT_WALKED_RTS_PC;
}

// `$81:BE71`.
static uint32_t going_round(Ant* m) {
  PortCpu* c = m->c;
  // Could it step to its left?
  lda(m, (uint16_t)(field(m, ANT_DP_FACING) - 2));
  lda(m, sub(m, c->a, ANT_QUARTER_TURN) & 0x000fu);
  lda(m, (uint16_t)(c->a + 2));
  set_field(m, ANT_DP_LEFT_WAY, c->a);
  doubled(m);
  ldy(m, c->a);
  aim_a_step(m);
  jsr(m, 0xbe92);
  bool refused = step_is_refused(m);
  ran(m, ANT_RUN_BE71);
  ran(m, ANT_RUN_BE95);
  if (took(m, refused)) {
    set_field(m, ANT_DP_LEFT_TURNS, 0);
    ran(m, ANT_RUN_BEAB);
  } else {
    count_up(m, ANT_DP_LEFT_TURNS);
    lda(m, field(m, ANT_DP_LEFT_TURNS));
    cmp16(c, c->a, ANT_LEFT_TURNS_MOST);
    ran(m, ANT_RUN_BE97);
    if (!took(m, c->a < ANT_LEFT_TURNS_MOST)) {
      PORT_COVER(ant_circling);
      ran(m, ANT_RUN_BEA0);
      return wanders_off(m);
    }
    PORT_COVER(ant_turns_left);
    lda(m, field(m, ANT_DP_LEFT_WAY));
    set_field(m, ANT_DP_FACING, c->a);
    set_field(m, ANT_DP_TIMER, 0);
    took(m, true);
    ran(m, ANT_RUN_BEA3);
  }

  // Then a step the way it faces.
  lda(m, field(m, ANT_DP_FACING));
  doubled(m);
  ldy(m, c->a);
  aim_a_step(m);
  jsr(m, 0xbec1);
  refused = step_is_refused(m);
  ran(m, ANT_RUN_BEAD);
  ran(m, ANT_RUN_BEC4);
  if (took(m, refused)) {
    ran(m, ANT_RUN_BED7);
    return turns_right(m);
  }
  PORT_COVER(ant_goes_round);
  step_there(m);
  ran(m, ANT_RUN_BEC6);
  return ANT_WENT_ROUND_RTS_PC;
}

// `$81:BFCD`.
static uint32_t marching(Ant* m) {
  PortCpu* c = m->c;
  lda(m, field(m, ANT_DP_FACING));
  doubled(m);
  set_field(m, CHASE_DP_STEP_INDEX, c->a);
  ldy(m, c->a);
  ldx(m, field(m, ANT_DP_RECORD));
  aim_a_step(m);
  jsr(m, 0xbfe6);
  const bool refused = march_is_refused(m);
  ran(m, ANT_RUN_BFCD);
  ran(m, ANT_RUN_BFE9);
  if (!took(m, refused)) {
    PORT_COVER(ant_marches);
    step_there(m);
    ran(m, ANT_RUN_BFEB);
    return ANT_MARCHED_RTS_PC;
  }
  lda(m, field(m, CHASE_DP_BLOCKER));
  ran(m, ANT_RUN_BFFF);
  if (took(m, c->a != 0)) {
    PORT_COVER(ant_march_met_edge);
    ran(m, ANT_RUN_BFFC);
    return wanders_off(m);
  }
  jsr(m, 0xc003);
  const bool waits = nothing_to_leap(m);
  ran(m, ANT_RUN_C003);
  ran(m, ANT_RUN_C006);
  if (took(m, waits)) {
    PORT_COVER(ant_march_waits);
    return ANT_MARCHED_RTS_PC;
  }
  ran(m, ANT_RUN_C008);
  return leaps(m);
}

// --- The stretches ----------------------------------------------------------

static const struct {
  uint32_t pc;
  uint32_t (*run)(Ant* m);
} STRETCHES[] = {
    {ANT_SET_UP_PC, set_up},
    {ANT_HOLDS_NOTHING_PC, holds_nothing},
    {ANT_USUAL_KIND_PC, usual_kind},
    {ANT_FAST_KIND_PC, fast_kind},
    {ANT_ROOM_PC, asks_for_room},
    {ANT_MARCH_BEGINS_PC, march_begins},
    {ANT_PICKS_UP_PC, picks_up},
    {ANT_PUTS_DOWN_PC, puts_down},
    {ANT_PUT_DOWN_PC, put_down},
    {ANT_HOLDS_NONE_PC, holds_none},
    {ANT_WANDERS_OFF_PC, wanders_off},
    {ANT_LEAPS_PC, leaps},
    {ANT_LANDS_PC, lands},
    {ANT_WALKING_PC, walking},
    {ANT_GOING_ROUND_PC, going_round},
    {ANT_MARCHING_PC, marching},
};

bool ant_state_begins_at(uint32_t pc) {
  for (unsigned i = 0; i < sizeof STRETCHES / sizeof STRETCHES[0]; i++)
    if (STRETCHES[i].pc == pc) return true;
  return false;
}

void ant_state_run(Wram* w, const Rom* rom, PortCpu* c,
                   AntStatesWork* k) {
  Ant m = {w, rom, c, k};
  k->v_known = true;
  for (unsigned i = 0; i < sizeof STRETCHES / sizeof STRETCHES[0]; i++) {
    if (STRETCHES[i].pc != c->pc) continue;
    c->pc = STRETCHES[i].run(&m);
    return;
  }
}
