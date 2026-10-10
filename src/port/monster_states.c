// The big monster's states -- see port/monster_states.h.

#include "port/monster_states.h"

#include "port/chase.h"
#include "port/coverage.h"
#include "port/monster.h"
#include "port/collide.h"  // MONSTER_DP_HIT_ID
#include "port/rng.h"

typedef struct {
  Wram* w;
  const Rom* rom;
  PortCpu* c;
  MonsterStatesWork* k;
} Monster;

// --- The machine, as these stretches use it ---------------------------------

static uint16_t field(const Monster* m, uint16_t at) {
  return wram_r16(m->w, (uint16_t)(m->c->d + at));
}

static void set_field(Monster* m, uint16_t at, uint16_t v) {
  wram_w16(m->w, (uint16_t)(m->c->d + at), v);
}

// A word of its record.
static uint16_t word_at(const Monster* m, uint16_t record, uint16_t at) {
  return wram_r16(m->w, (uint16_t)(record + at));
}

static void set_word_at(Monster* m, uint16_t record, uint16_t at, uint16_t v) {
  wram_w16(m->w, (uint16_t)(record + at), v);
}

// A word of one of its tables, which are in its own bank.
static uint16_t table(const Monster* m, uint16_t at) {
  return rom_word(m->rom, (uint32_t)MONSTER_STATES_BANK << 16 | at);
}

static void ran(Monster* m, MonsterRun run) { m->k->runs[run]++; }

// A branch: counted if it is taken.
static bool took(Monster* m, bool taken) {
  if (taken) m->k->taken++;
  return taken;
}

static bool negative(uint16_t v) { return (v & 0x8000u) != 0; }

static void lda(Monster* m, uint16_t v) {
  m->c->a = v;
  set_nz16(m->c, v);
}

static void ldx(Monster* m, uint16_t v) {
  m->c->x = v;
  set_nz16(m->c, v);
}

static void ldy(Monster* m, uint16_t v) {
  m->c->y = v;
  set_nz16(m->c, v);
}

// `ASL`, of what is in A.
static void doubled(Monster* m) { m->c->a = asl16(m->c, m->c->a); }

// `CLC : ADC`, and `SEC : SBC`.
static uint16_t add(Monster* m, uint16_t to, uint16_t v) {
  set_c(m->c, false);
  m->k->v_known = true;
  return m->c->a = adc16(m->c, to, v);
}

static uint16_t sub(Monster* m, uint16_t from, uint16_t v) {
  set_c(m->c, true);
  m->k->v_known = true;
  return m->c->a = sbc16(m->c, from, v);
}

// `INC` of a word of the direct page.
static void count_up(Monster* m, uint16_t at) {
  const uint16_t v = (uint16_t)(field(m, at) + 1);
  set_field(m, at, v);
  set_nz16(m->c, v);
}

// A `JSR` or a `JSL` at `at`, and coming back from one.
static void jsr(Monster* m, uint16_t at) {
  push16(m->w, m->c, (uint16_t)(at + 2));
}

static void rts(Monster* m) { (void)pull16(m->w, m->c); }

static void jsl(Monster* m, uint16_t at) {
  push8(m->w, m->c, MONSTER_STATES_BANK);
  push16(m->w, m->c, (uint16_t)(at + 3));
}

static void rtl(Monster* m) {
  (void)pull16(m->w, m->c);
  (void)pull8(m->w, m->c);
}

static void set_flags(Monster* m, bool n, bool z, bool carry) {
  PortCpu* c = m->c;
  c->p = (uint8_t)(c->p & ~(PORT_P_N | PORT_P_Z));
  if (n) c->p |= PORT_P_N;
  if (z) c->p |= PORT_P_Z;
  set_c(c, carry);
}

// What a routine called with `PHD` leaves in N and Z when it pulls it.
static void set_flags_pld(Monster* m, bool carry) {
  set_flags(m, negative(m->c->d), m->c->d == 0, carry);
}

// --- What it calls ----------------------------------------------------------

// `JSL rng_next` at `at`.
static uint16_t draw(Monster* m, uint16_t at) {
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
static bool ground_is_solid(Monster* m, uint16_t at) {
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
static bool someone_is_at(Monster* m, uint16_t at) {
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
static bool ground_is_shut(Monster* m, uint16_t at) {
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
static bool off_the_level(Monster* m, uint16_t at) {
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
static uint16_t tile_attrs(Monster* m, uint16_t at) {
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
static void aim_a_step(Monster* m) {
  PortCpu* c = m->c;
  const uint16_t steps = field(m, CHASE_DP_STEPS);
  set_field(m, CHASE_DP_NEXT_X,
            add(m, field(m, MONSTER_DP_X), table(m, (uint16_t)(steps + c->y))));
  ldy(m, (uint16_t)(c->y + 2));
  set_field(m, CHASE_DP_NEXT_Y,
            add(m, field(m, MONSTER_DP_Y), table(m, (uint16_t)(steps + c->y))));
}

// It goes to the point: its page, and its record.
static void step_there(Monster* m) {
  PortCpu* c = m->c;
  ldy(m, field(m, MONSTER_DP_RECORD));
  lda(m, field(m, CHASE_DP_NEXT_X));
  set_field(m, MONSTER_DP_X, c->a);
  set_word_at(m, c->y, ACTOR_X, c->a);
  lda(m, field(m, CHASE_DP_NEXT_Y));
  set_field(m, MONSTER_DP_Y, c->a);
  set_word_at(m, c->y, ACTOR_Y, c->a);
}

// `$81:BC05`: is the step refused? By the ground, or by somebody standing
// there, and `$32` says which: 0 for the ground.
static bool step_is_refused(Monster* m) {
  lda(m, 0);
  set_field(m, CHASE_DP_BLOCKER, 0);
  ldx(m, field(m, CHASE_DP_NEXT_X));
  ldy(m, field(m, CHASE_DP_NEXT_Y));
  bool refused = ground_is_solid(m, 0xbc0e);
  ran(m, MONSTER_RUN_BC05);
  if (!took(m, refused)) {
    count_up(m, CHASE_DP_BLOCKER);
    lda(m, field(m, MONSTER_DP_RECORD));
    ldx(m, field(m, CHASE_DP_NEXT_X));
    ldy(m, field(m, CHASE_DP_NEXT_Y));
    refused = someone_is_at(m, 0xbc1c);
    ran(m, MONSTER_RUN_BC14);
    took(m, refused);  // a `BCS` to the next instruction
  }
  ran(m, MONSTER_RUN_RTS);
  rts(m);
  return refused;
}

// `$81:BC23`: the same for one that marches, which minds the level's edge
// and not who is standing there. `$32` is 1 when it was the edge.
static bool march_is_refused(Monster* m) {
  lda(m, 0);
  set_field(m, CHASE_DP_BLOCKER, 0);
  ldx(m, field(m, CHASE_DP_NEXT_X));
  ldy(m, field(m, CHASE_DP_NEXT_Y));
  bool refused = ground_is_solid(m, 0xbc2c);
  ran(m, MONSTER_RUN_BC23);
  if (!took(m, refused)) {
    count_up(m, CHASE_DP_BLOCKER);
    ldx(m, field(m, CHASE_DP_NEXT_X));
    ldy(m, field(m, CHASE_DP_NEXT_Y));
    refused = off_the_level(m, 0xbc38);
    ran(m, MONSTER_RUN_BC32);
  }
  ran(m, MONSTER_RUN_RTS);
  rts(m);
  return refused;
}

// Does the tile `tiles` tiles ahead have the bit that says it can be leapt?
// The point is left in `$0E` and `$10`.
static bool tile_ahead_is_leapable(Monster* m, int tiles, uint16_t call) {
  PortCpu* c = m->c;
  lda(m, field(m, MONSTER_DP_FACING));
  doubled(m);
  ldy(m, c->a);
  lda(m, table(m, (uint16_t)(CHASE_LEAP_PROBE + c->y)));
  if (tiles == 2) doubled(m);
  set_field(m, CHASE_DP_NEXT_X, add(m, c->a, field(m, MONSTER_DP_X)));
  ldx(m, c->a);
  lda(m, table(m, (uint16_t)(CHASE_LEAP_PROBE + 2 + c->y)));
  if (tiles == 2) doubled(m);
  set_field(m, CHASE_DP_NEXT_Y, add(m, c->a, field(m, MONSTER_DP_Y)));
  ldy(m, c->a);
  lda(m, tile_attrs(m, call) & CHASE_LEAPABLE);
  return c->a != 0;
}

// `$81:BC3D`: solid ground ahead. True unless there is a tile to leap, one
// ahead or two, and clear ground past it.
static bool nothing_to_leap(Monster* m) {
  PortCpu* c = m->c;
  bool leapable = tile_ahead_is_leapable(m, 1, 0xbc53);
  ran(m, MONSTER_RUN_BC3D);
  if (!took(m, leapable)) {
    leapable = tile_ahead_is_leapable(m, 2, 0xbc74);
    ran(m, MONSTER_RUN_BC5C);
    if (!took(m, leapable)) {
      PORT_COVER(monster_nothing_leapable);
      set_c(c, true);
      ran(m, MONSTER_RUN_BC7D);
      rts(m);
      return true;
    }
  }
  // Where it would land: that far past the tile.
  lda(m, field(m, MONSTER_DP_FACING));
  doubled(m);
  ldy(m, c->a);
  ldx(m, add(m, table(m, (uint16_t)(CHASE_LEAP_LANDING + c->y)),
             field(m, CHASE_DP_NEXT_X)));
  ldy(m, add(m, table(m, (uint16_t)(CHASE_LEAP_LANDING + 2 + c->y)),
             field(m, CHASE_DP_NEXT_Y)));
  const bool solid = ground_is_solid(m, 0xbc91);
  ran(m, MONSTER_RUN_BC7F);
  if (took(m, solid)) {
    PORT_COVER(monster_no_landing);
    set_c(c, true);
    ran(m, MONSTER_RUN_BC7D);
    rts(m);
    return true;
  }
  set_c(c, false);
  ran(m, MONSTER_RUN_BC97);
  rts(m);
  return false;
}

// `$81:BE0E`.
static uint32_t begin_walking(Monster* m) {
  lda(m, MONSTER_STATE_WANDER);
  set_field(m, MONSTER_DP_STATE, m->c->a);
  ran(m, MONSTER_RUN_BE0E);
  return MONSTER_WALK_BEGUN_RTS_PC;
}

// `$81:BCE1`: one of the four straight ways by a draw, and it walks.
static uint32_t wanders_off(Monster* m) {
  PORT_COVER(monster_wanders_off);
  lda(m, draw(m, 0xbce1) & 0x0003u);
  doubled(m);
  doubled(m);
  lda(m, (uint16_t)(m->c->a + 2));
  set_field(m, MONSTER_DP_FACING, m->c->a);
  ran(m, MONSTER_RUN_BCE1);
  return begin_walking(m);
}

// `$81:BE69`.
static uint32_t begin_going_round(Monster* m) {
  lda(m, MONSTER_STATE_GOING_ROUND);
  set_field(m, MONSTER_DP_STATE, m->c->a);
  set_field(m, MONSTER_DP_LEFT_TURNS, 0);
  ran(m, MONSTER_RUN_BE69);
  return MONSTER_ROUND_BEGUN_RTS_PC;
}

// `$81:BE41`: a quarter turn to its right, and it goes round from there.
static uint32_t turns_right(Monster* m) {
  PORT_COVER(monster_turns_right);
  set_field(m, MONSTER_DP_TIMER, 0);
  lda(m, (uint16_t)(field(m, MONSTER_DP_FACING) - 2));
  lda(m, add(m, m->c->a, MONSTER_QUARTER_TURN) & 0x000fu);
  lda(m, (uint16_t)(m->c->a + 2));
  set_field(m, MONSTER_DP_FACING, m->c->a);
  ran(m, MONSTER_RUN_BE41);
  return begin_going_round(m);
}

// `$81:BD1E`: where a leap comes down. Past the tile, and then on by eights
// until the ground is clear.
static uint32_t lands(Monster* m) {
  PortCpu* c = m->c;
  lda(m, field(m, MONSTER_DP_FACING));
  doubled(m);
  set_field(m, CHASE_DP_STEP_INDEX, c->a);
  ldx(m, c->a);
  set_field(m, CHASE_DP_NEXT_X,
            add(m, table(m, (uint16_t)(MONSTER_LEAP_LANDING + c->x)),
                field(m, MONSTER_DP_X)));
  set_field(m, CHASE_DP_NEXT_Y,
            add(m, table(m, (uint16_t)(MONSTER_LEAP_LANDING + 2 + c->x)),
                field(m, MONSTER_DP_Y)));
  ran(m, MONSTER_RUN_BD1E);
  for (;;) {
    ldx(m, field(m, CHASE_DP_NEXT_X));
    ldy(m, field(m, CHASE_DP_NEXT_Y));
    const bool solid = ground_is_solid(m, 0xbd38);
    ran(m, MONSTER_RUN_BD34);
    if (took(m, !solid)) break;
    PORT_COVER(monster_lands_further);
    ldx(m, field(m, CHASE_DP_STEP_INDEX));
    set_field(m, CHASE_DP_NEXT_X,
              add(m, table(m, (uint16_t)(MONSTER_LEAP_FURTHER + c->x)),
                  field(m, CHASE_DP_NEXT_X)));
    set_field(m, CHASE_DP_NEXT_Y,
              add(m, table(m, (uint16_t)(MONSTER_LEAP_FURTHER + 2 + c->x)),
                  field(m, CHASE_DP_NEXT_Y)));
    took(m, true);
    ran(m, MONSTER_RUN_BD3E);
  }
  ldy(m, field(m, MONSTER_DP_RECORD));
  lda(m, (uint16_t)~ACTOR_PRIORITY_TOP);
  lda(m, c->a & word_at(m, c->y, ACTOR_FLAGS));
  set_word_at(m, c->y, ACTOR_FLAGS, c->a);
  lda(m, field(m, CHASE_DP_NEXT_X));
  set_field(m, MONSTER_DP_X, c->a);
  set_word_at(m, c->y, ACTOR_X, c->a);
  lda(m, field(m, CHASE_DP_NEXT_Y));
  set_field(m, MONSTER_DP_Y, c->a);
  set_word_at(m, c->y, ACTOR_Y, c->a);
  set_field(m, MONSTER_DP_TIMER, 0);
  set_field(m, MONSTER_DP_PHASE, 0);
  ran(m, MONSTER_RUN_BD52);
  return begin_walking(m);
}

// `$81:BCF1`: it leaps. It is drawn over everything while it does, and
// turned to face the way it goes.
static uint32_t leaps(Monster* m) {
  PortCpu* c = m->c;
  PORT_COVER(monster_leaps);
  lda(m, MONSTER_STATE_LEAPING);
  set_field(m, MONSTER_DP_STATE, c->a);
  ldy(m, field(m, MONSTER_DP_RECORD));
  lda(m, ACTOR_PRIORITY_TOP);
  lda(m, c->a | word_at(m, c->y, ACTOR_FLAGS));
  set_word_at(m, c->y, ACTOR_FLAGS, c->a);
  ran(m, MONSTER_RUN_BCF1);

  lda(m, field(m, MONSTER_DP_FACING));
  doubled(m);
  ldx(m, c->a);
  lda(m, table(m, (uint16_t)(MONSTER_LEAP_FLAGS + c->x)));
  ran(m, MONSTER_RUN_BD01);
  if (took(m, negative(c->a))) {
    lda(m, c->a & word_at(m, c->y, ACTOR_FLAGS));
    ran(m, MONSTER_RUN_BD0F);
  } else {
    lda(m, c->a | word_at(m, c->y, ACTOR_FLAGS));
    took(m, true);
    ran(m, MONSTER_RUN_BD0A);
  }
  set_word_at(m, c->y, ACTOR_FLAGS, c->a);
  lda(m, table(m, (uint16_t)(MONSTER_LEAP_PICTURES + c->x)));
  ran(m, MONSTER_RUN_BD12);
  if (took(m, c->a == 0)) {
    PORT_COVER(monster_leaps_plain);
    return lands(m);
  }
  return MONSTER_LEAP_PICTURES_PC;
}

// --- How one is set up ------------------------------------------------------

// `$81:B9FD`: it stands where it was spawned, which is also its home, and
// its record shows its first picture. Then it has no handler.
static uint32_t set_up(Monster* m) {
  PortCpu* c = m->c;
  lda(m, field(m, MONSTER_DP_SPAWN_X));
  set_field(m, MONSTER_DP_X, c->a);
  set_field(m, MONSTER_DP_HOME_X, c->a);
  lda(m, field(m, MONSTER_DP_SPAWN_Y));
  set_field(m, MONSTER_DP_Y, c->a);
  set_field(m, MONSTER_DP_HOME_Y, c->a);
  ldy(m, field(m, MONSTER_DP_RECORD));
  set_word_at(m, c->y, ACTOR_META, MONSTER_FIRST_PICTURE);
  set_word_at(m, c->y, ACTOR_META_BANK, MONSTER_META_BANK);
  set_word_at(m, c->y, ACTOR_COLLIDE_ID, MONSTER_COLLIDE_ID);
  set_word_at(m, c->y, ACTOR_ATTR, 0);
  lda(m, ACTOR_DRAW | word_at(m, c->y, ACTOR_FLAGS));
  set_word_at(m, c->y, ACTOR_FLAGS, c->a);
  lda(m, 0);
  ldy(m, 0);
  ran(m, MONSTER_RUN_B9FD);
  return MONSTER_NO_HANDLER_PC;
}

// `$81:BA34`: its legs at rest, after nobody, holding nothing.
static uint32_t holds_nothing(Monster* m) {
  set_field(m, MONSTER_DP_PHASE, 0);
  set_field(m, MONSTER_DP_TIMER, 0);
  set_field(m, MONSTER_DP_LEAVE, 0);
  set_field(m, MONSTER_DP_HIT_ID, 0);
  set_field(m, MONSTER_DP_HITS, 0);
  set_field(m, MONSTER_DP_HELD_KIND, 0);
  lda(m, MONSTER_CARRY_NONE);
  set_field(m, MONSTER_DP_CARRIED, m->c->a);
  ran(m, MONSTER_RUN_BA34);
  return MONSTER_SET_UP_RTS_PC;
}

// What makes a kind: how much it takes, how far it steps, and its pictures,
// which are left in A for the call.
static void becomes(Monster* m, uint16_t health, uint16_t steps,
                    uint16_t pictures) {
  lda(m, health);
  set_field(m, MONSTER_DP_HEALTH, health);
  lda(m, steps);
  set_field(m, CHASE_DP_STEPS, steps);
  set_field(m, CHASE_DP_USUAL_STEPS, steps);
  lda(m, pictures);
}

// `$81:BA46`: the usual kind, in colours of its own.
static uint32_t usual_kind(Monster* m) {
  ldy(m, field(m, MONSTER_DP_RECORD));
  set_word_at(m, m->c->y, ACTOR_ATTR, MONSTER_USUAL_ATTR);
  becomes(m, MONSTER_USUAL_HEALTH, MONSTER_USUAL_STEPS,
          MONSTER_USUAL_PICTURES);
  ran(m, MONSTER_RUN_BA46);
  return MONSTER_USUAL_PICTURES_PC;
}

// `$81:BA78`: the fast kind, in the colours its record says.
static uint32_t fast_kind(Monster* m) {
  PortCpu* c = m->c;
  ldy(m, field(m, MONSTER_DP_RECORD));
  lda(m, ACTOR_ATTR_SET | word_at(m, c->y, ACTOR_FLAGS));
  set_word_at(m, c->y, ACTOR_FLAGS, c->a);
  set_word_at(m, c->y, ACTOR_ATTR, 0);
  becomes(m, MONSTER_FAST_HEALTH, CHASE_FAST_STEPS, MONSTER_FAST_PICTURES);
  ran(m, MONSTER_RUN_BA78);
  return MONSTER_FAST_PICTURES_PC;
}

// `$81:BFA8`: is there room for one at the top of the screen, above where
// it was spawned? Carry set is no. `$02` is left as that row.
static uint32_t asks_for_room(Monster* m) {
  PortCpu* c = m->c;
  ldx(m, field(m, MONSTER_DP_SPAWN_X));
  lda(m, wram_r16(m->w, W_CAMERA_Y));
  set_field(m, MONSTER_DP_SPAWN_Y, c->a);
  ldy(m, c->a);
  const bool shut = ground_is_shut(m, 0xbfb0);
  ran(m, MONSTER_RUN_BFA8);
  if (took(m, shut)) {
    PORT_COVER(monster_no_room_ground);
    return MONSTER_ROOM_RTS_PC;
  }
  ldx(m, field(m, MONSTER_DP_SPAWN_X));
  ldy(m, field(m, MONSTER_DP_SPAWN_Y));
  const bool outside = off_the_level(m, 0xbfba);
  ran(m, MONSTER_RUN_BFB6);
  if (took(m, outside)) {
    PORT_COVER(monster_no_room_edge);
    return MONSTER_ROOM_RTS_PC;
  }
  set_c(c, false);
  ran(m, MONSTER_RUN_BFC0);
  return MONSTER_ROOM_RTS_PC;
}

// `$81:BFC2`: it will march, down.
static uint32_t march_begins(Monster* m) {
  lda(m, MONSTER_STATE_MARCHING);
  set_field(m, MONSTER_DP_STATE, m->c->a);
  lda(m, MONSTER_WAY_DOWN);
  set_field(m, MONSTER_DP_FACING, m->c->a);
  ran(m, MONSTER_RUN_BFC2);
  return MONSTER_MARCH_BEGUN_RTS_PC;
}

// --- What it holds ----------------------------------------------------------

// The row of a table by kind for what it holds.
static uint16_t held_kind_row(uint16_t kind) {
  return (uint16_t)((uint16_t)(kind - MONSTER_HELD_FIRST_KIND) << 1);
}

bool monster_state_supported(const Wram* w, const Rom* rom, uint16_t page,
                             uint32_t pc) {
  const uint16_t row =
      held_kind_row(wram_r16(w, (uint16_t)(page + MONSTER_DP_HELD_KIND)));
  const uint32_t bank = (uint32_t)MONSTER_STATES_BANK << 16;
  if (pc == MONSTER_PICKS_UP_PC)
    return rom_word(rom, bank | (uint16_t)(MONSTER_HELD_PICTURES + row)) != 0;
  if (pc == MONSTER_PUTS_DOWN_PC)
    return rom_word(rom, bank | (uint16_t)(MONSTER_PUT_BACK_AS + row)) !=
           0xffffu;
  return true;
}

// `$81:C054`: the record in A is who it has caught. It is put where the
// monster is, shown by the picture for its kind, and owned by this thread.
// Then the monster wanders off with it.
static uint32_t picks_up(Monster* m) {
  PortCpu* c = m->c;
  PORT_COVER(monster_picks_up);
  set_field(m, MONSTER_DP_CARRIED, c->a);
  ldy(m, c->a);
  lda(m, field(m, MONSTER_DP_X));
  set_word_at(m, c->y, ACTOR_X, c->a);
  set_word_at(m, c->y, ACTOR_Z, MONSTER_HELD_HEIGHT);
  lda(m, field(m, MONSTER_DP_Y));
  set_word_at(m, c->y, ACTOR_Y, c->a);
  set_word_at(m, c->y, ACTOR_COLLIDE_ID, 0);
  sub(m, field(m, MONSTER_DP_HELD_KIND), MONSTER_HELD_FIRST_KIND);
  doubled(m);
  ldx(m, c->a);
  lda(m, table(m, (uint16_t)(MONSTER_HELD_PICTURES + c->x)));
  ran(m, MONSTER_RUN_C054);

  set_word_at(m, c->y, ACTOR_META, c->a);
  set_word_at(m, c->y, ACTOR_META_BANK, MONSTER_HELD_META_BANK);
  set_word_at(m, c->y, ACTOR_THREAD, wram_r16(m->w, W_SCHED_CUR_TASK));
  lda(m, ACTOR_DRAW | word_at(m, c->y, ACTOR_FLAGS));
  set_word_at(m, c->y, ACTOR_FLAGS, c->a);
  ran(m, MONSTER_RUN_C07A);
  return wanders_off(m);
}

// `$81:C0E5`: a killed one puts down who it holds. They are put back where
// it stands, as what its kind says, by the call this stops on.
static uint32_t puts_down(Monster* m) {
  PortCpu* c = m->c;
  PORT_COVER(monster_puts_down);
  set_field(m, MONSTER_DP_SPAWN_X, field(m, MONSTER_DP_X));
  set_field(m, MONSTER_DP_SPAWN_Y, field(m, MONSTER_DP_Y));
  sub(m, field(m, MONSTER_DP_HELD_KIND), MONSTER_HELD_FIRST_KIND);
  doubled(m);
  ldx(m, c->a);
  lda(m, table(m, (uint16_t)(MONSTER_PUT_BACK_AS + c->x)));
  cmp16(c, c->a, 0xffff);
  ran(m, MONSTER_RUN_C0E5);

  ldx(m, field(m, MONSTER_DP_SPAWN_X));
  ldy(m, field(m, MONSTER_DP_SPAWN_Y));
  ran(m, MONSTER_RUN_C0FD);
  return MONSTER_PUT_BACK_PC;
}

// `$81:C105`: ...and the record it held them as is freed.
static uint32_t put_down(Monster* m) {
  lda(m, field(m, MONSTER_DP_CARRIED));
  ran(m, MONSTER_RUN_C105);
  return MONSTER_FREE_HELD_PC;
}

// `$81:C10B`: it holds nobody, and can be touched again.
static uint32_t holds_none(Monster* m) {
  PortCpu* c = m->c;
  lda(m, MONSTER_CARRY_NONE);
  set_field(m, MONSTER_DP_CARRIED, c->a);
  set_field(m, MONSTER_DP_HELD_KIND, 0);
  ldy(m, field(m, MONSTER_DP_RECORD));
  lda(m, MONSTER_COLLIDE_ID);
  set_word_at(m, c->y, ACTOR_COLLIDE_ID, c->a);
  ran(m, MONSTER_RUN_C10B);
  return MONSTER_PUT_DOWN_RTS_PC;
}

// --- The states -------------------------------------------------------------

// `$81:BE14`.
static uint32_t walking(Monster* m) {
  PortCpu* c = m->c;
  lda(m, field(m, MONSTER_DP_FACING));
  doubled(m);
  ldy(m, c->a);
  aim_a_step(m);
  jsr(m, 0xbe28);
  const bool refused = step_is_refused(m);
  ran(m, MONSTER_RUN_BE14);
  ran(m, MONSTER_RUN_BE2B);
  if (took(m, refused)) {
    PORT_COVER(monster_walk_refused);
    ran(m, MONSTER_RUN_BE3E);
    return begin_going_round(m);
  }
  PORT_COVER(monster_walks);
  step_there(m);
  ran(m, MONSTER_RUN_BE2D);
  return MONSTER_WALKED_RTS_PC;
}

// `$81:BE71`.
static uint32_t going_round(Monster* m) {
  PortCpu* c = m->c;
  // Could it step to its left?
  lda(m, (uint16_t)(field(m, MONSTER_DP_FACING) - 2));
  lda(m, sub(m, c->a, MONSTER_QUARTER_TURN) & 0x000fu);
  lda(m, (uint16_t)(c->a + 2));
  set_field(m, MONSTER_DP_LEFT_WAY, c->a);
  doubled(m);
  ldy(m, c->a);
  aim_a_step(m);
  jsr(m, 0xbe92);
  bool refused = step_is_refused(m);
  ran(m, MONSTER_RUN_BE71);
  ran(m, MONSTER_RUN_BE95);
  if (took(m, refused)) {
    set_field(m, MONSTER_DP_LEFT_TURNS, 0);
    ran(m, MONSTER_RUN_BEAB);
  } else {
    count_up(m, MONSTER_DP_LEFT_TURNS);
    lda(m, field(m, MONSTER_DP_LEFT_TURNS));
    cmp16(c, c->a, MONSTER_LEFT_TURNS_MOST);
    ran(m, MONSTER_RUN_BE97);
    if (!took(m, c->a < MONSTER_LEFT_TURNS_MOST)) {
      PORT_COVER(monster_circling);
      ran(m, MONSTER_RUN_BEA0);
      return wanders_off(m);
    }
    PORT_COVER(monster_turns_left);
    lda(m, field(m, MONSTER_DP_LEFT_WAY));
    set_field(m, MONSTER_DP_FACING, c->a);
    set_field(m, MONSTER_DP_TIMER, 0);
    took(m, true);
    ran(m, MONSTER_RUN_BEA3);
  }

  // Then a step the way it faces.
  lda(m, field(m, MONSTER_DP_FACING));
  doubled(m);
  ldy(m, c->a);
  aim_a_step(m);
  jsr(m, 0xbec1);
  refused = step_is_refused(m);
  ran(m, MONSTER_RUN_BEAD);
  ran(m, MONSTER_RUN_BEC4);
  if (took(m, refused)) {
    ran(m, MONSTER_RUN_BED7);
    return turns_right(m);
  }
  PORT_COVER(monster_goes_round);
  step_there(m);
  ran(m, MONSTER_RUN_BEC6);
  return MONSTER_WENT_ROUND_RTS_PC;
}

// `$81:BFCD`.
static uint32_t marching(Monster* m) {
  PortCpu* c = m->c;
  lda(m, field(m, MONSTER_DP_FACING));
  doubled(m);
  set_field(m, CHASE_DP_STEP_INDEX, c->a);
  ldy(m, c->a);
  ldx(m, field(m, MONSTER_DP_RECORD));
  aim_a_step(m);
  jsr(m, 0xbfe6);
  const bool refused = march_is_refused(m);
  ran(m, MONSTER_RUN_BFCD);
  ran(m, MONSTER_RUN_BFE9);
  if (!took(m, refused)) {
    PORT_COVER(monster_marches);
    step_there(m);
    ran(m, MONSTER_RUN_BFEB);
    return MONSTER_MARCHED_RTS_PC;
  }
  lda(m, field(m, CHASE_DP_BLOCKER));
  ran(m, MONSTER_RUN_BFFF);
  if (took(m, c->a != 0)) {
    PORT_COVER(monster_march_met_edge);
    ran(m, MONSTER_RUN_BFFC);
    return wanders_off(m);
  }
  jsr(m, 0xc003);
  const bool waits = nothing_to_leap(m);
  ran(m, MONSTER_RUN_C003);
  ran(m, MONSTER_RUN_C006);
  if (took(m, waits)) {
    PORT_COVER(monster_march_waits);
    return MONSTER_MARCHED_RTS_PC;
  }
  ran(m, MONSTER_RUN_C008);
  return leaps(m);
}

// --- The stretches ----------------------------------------------------------

static const struct {
  uint32_t pc;
  uint32_t (*run)(Monster* m);
} STRETCHES[] = {
    {MONSTER_SET_UP_PC, set_up},
    {MONSTER_HOLDS_NOTHING_PC, holds_nothing},
    {MONSTER_USUAL_KIND_PC, usual_kind},
    {MONSTER_FAST_KIND_PC, fast_kind},
    {MONSTER_ROOM_PC, asks_for_room},
    {MONSTER_MARCH_BEGINS_PC, march_begins},
    {MONSTER_PICKS_UP_PC, picks_up},
    {MONSTER_PUTS_DOWN_PC, puts_down},
    {MONSTER_PUT_DOWN_PC, put_down},
    {MONSTER_HOLDS_NONE_PC, holds_none},
    {MONSTER_WANDERS_OFF_PC, wanders_off},
    {MONSTER_LEAPS_PC, leaps},
    {MONSTER_LANDS_PC, lands},
    {MONSTER_WALKING_PC, walking},
    {MONSTER_GOING_ROUND_PC, going_round},
    {MONSTER_MARCHING_PC, marching},
};

bool monster_state_begins_at(uint32_t pc) {
  for (unsigned i = 0; i < sizeof STRETCHES / sizeof STRETCHES[0]; i++)
    if (STRETCHES[i].pc == pc) return true;
  return false;
}

void monster_state_run(Wram* w, const Rom* rom, PortCpu* c,
                       MonsterStatesWork* k) {
  Monster m = {w, rom, c, k};
  k->v_known = true;
  for (unsigned i = 0; i < sizeof STRETCHES / sizeof STRETCHES[0]; i++) {
    if (STRETCHES[i].pc != c->pc) continue;
    c->pc = STRETCHES[i].run(&m);
    return;
  }
}
