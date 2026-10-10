// The big figure's thread, between its calls -- see port/boss_thread.h.

#include "port/boss_thread.h"

#include "port/bossbg.h"  // where the upload finds the picture
#include "port/coverage.h"
#include "port/frontend.h"  // W_FRAME_COUNT
#include "port/rng.h"
#include "port/vblank.h"  // W_BOSS_PLANE_X: where its plane is scrolled to

typedef struct {
  Wram* w;
  const Rom* rom;
  PortCpu* c;
  BossWork* k;
} Boss;

// --- The machine, as these stretches use it ---------------------------------

static uint16_t field(const Boss* b, uint16_t at) {
  return wram_r16(b->w, (uint16_t)(b->c->d + at));
}

static void set_field(Boss* b, uint16_t at, uint16_t v) {
  wram_w16(b->w, (uint16_t)(b->c->d + at), v);
}

// A word of a thing's record, or of whatever else a register points at.
static uint16_t word_at(const Boss* b, uint16_t record, uint16_t at) {
  return wram_r16(b->w, (uint16_t)(record + at));
}

static void set_word_at(Boss* b, uint16_t record, uint16_t at, uint16_t v) {
  wram_w16(b->w, (uint16_t)(record + at), v);
}

// A word of one of the thread's tables, which are in its own bank.
static uint16_t table(const Boss* b, uint16_t at) {
  return rom_word(b->rom, (uint32_t)BOSS_THREAD_BANK << 16 | at);
}

static void ran(Boss* b, BossRun run) { b->k->runs[run]++; }

// A branch: counted if it is taken.
static bool took(Boss* b, bool taken) {
  if (taken) b->k->taken++;
  return taken;
}

static bool negative(uint16_t v) { return (v & 0x8000u) != 0; }

static void lda(Boss* b, uint16_t v) {
  b->c->a = v;
  set_nz16(b->c, v);
}

static void ldx(Boss* b, uint16_t v) {
  b->c->x = v;
  set_nz16(b->c, v);
}

static void ldy(Boss* b, uint16_t v) {
  b->c->y = v;
  set_nz16(b->c, v);
}

// `CLC : ADC`, and `SEC : SBC`.
static uint16_t add(Boss* b, uint16_t to, uint16_t v) {
  set_c(b->c, false);
  b->k->v_known = true;
  return b->c->a = adc16(b->c, to, v);
}

static uint16_t sub(Boss* b, uint16_t from, uint16_t v) {
  set_c(b->c, true);
  b->k->v_known = true;
  return b->c->a = sbc16(b->c, from, v);
}

// `EOR #$FFFF : INC`.
static uint16_t negate(Boss* b, uint16_t v) {
  lda(b, (uint16_t)(0u - v));
  return b->c->a;
}

// `BIT` of a word in memory.
static void test_bits(Boss* b, uint16_t v) {
  bit16(b->c, v);
  b->k->v_known = true;
}

// `DEC` of a word of the direct page.
static uint16_t count_down(Boss* b, uint16_t at) {
  const uint16_t v = (uint16_t)(field(b, at) - 1);
  set_field(b, at, v);
  set_nz16(b->c, v);
  return v;
}

// A `JSR` or a `JSL` at `at`, and coming back from one.
static void jsr(Boss* b, uint16_t at) {
  push16(b->w, b->c, (uint16_t)(at + 2));
}

static void rts(Boss* b) { (void)pull16(b->w, b->c); }

static void jsl(Boss* b, uint16_t at) {
  push8(b->w, b->c, BOSS_THREAD_BANK);
  push16(b->w, b->c, (uint16_t)(at + 3));
}

static void rtl(Boss* b) {
  (void)pull16(b->w, b->c);
  (void)pull8(b->w, b->c);
}

static void set_flags(Boss* b, bool n, bool z, bool carry) {
  PortCpu* c = b->c;
  c->p = (uint8_t)(c->p & ~(PORT_P_N | PORT_P_Z));
  if (n) c->p |= PORT_P_N;
  if (z) c->p |= PORT_P_Z;
  set_c(c, carry);
}

// What a routine called with `PHD` leaves in N and Z when it pulls it.
static void set_flags_pld(Boss* b, bool carry) {
  set_flags(b, negative(b->c->d), b->c->d == 0, carry);
  b->k->v_known = false;
}

static uint16_t boss_x(const Boss* b) { return wram_r16(b->w, W_BOSS_X); }
static uint16_t boss_y(const Boss* b) { return wram_r16(b->w, W_BOSS_Y); }

// --- What it calls ----------------------------------------------------------

// `JSL rng_next` at `at`.
static uint16_t draw(Boss* b, uint16_t at) {
  RngResult r;
  jsl(b, at);
  rng_next(b->w, flag(b->c, PORT_P_C), &r);
  rtl(b);
  b->k->draw_twice[b->k->draws++] = r.v;
  b->c->a = r.a;
  set_flags(b, r.n, r.z, r.c);
  set_v(b->c, r.v);
  b->k->v_known = true;
  return r.a;
}

// `JSR boss_step` at `at`. True if it got nowhere.
static bool step(Boss* b, uint16_t at, uint16_t how) {
  PortCpu* c = b->c;
  BossStepRegs r;
  jsr(b, at);
  boss_step_counted(b->w, b->rom, c->d, how, &r,
                    &b->k->steps[b->k->step_count++]);
  rts(b);
  c->a = r.a;
  c->x = r.x;
  c->y = r.y;
  set_flags(b, r.n, r.z, r.c);
  // Its last sum is a pass counter of 8 or 0 less 8, which cannot overflow.
  set_v(c, false);
  b->k->v_known = true;
  return r.c;
}

// `$82:897E`: the nearer player within A of the point in X and Y, or nothing
// -- and nothing too if what answered is not a player.
static uint16_t player_within(Boss* b) {
  PortCpu* c = b->c;
  PlayerPickRegs* r = &b->k->looks[b->k->look_count++];
  jsl(b, 0x897e);
  player_in_range(b->w, c->a, c->x, c->y, r);
  rtl(b);
  set_flags_pld(b, r->c);

  push16(b->w, c, r->a);
  c->y = r->a;
  c->x = word_at(b, c->y, ACTOR_COLLIDE_ID);
  lda(b, pull16(b->w, c));
  cmp16(c, c->x, BOSS_ID_PLAYER_A);
  ran(b, BOSS_RUN_897E);
  if (!took(b, c->x == BOSS_ID_PLAYER_A)) {
    cmp16(c, c->x, BOSS_ID_PLAYER_B);
    ran(b, BOSS_RUN_898D);
    if (!took(b, c->x == BOSS_ID_PLAYER_B)) {
      PORT_COVER(boss_saw_no_player);
      took(b, true);  // a `BRA` to the next instruction
      lda(b, 0);
      ran(b, BOSS_RUN_8992);
    }
  }
  ran(b, BOSS_RUN_RTS);
  rts(b);
  return c->a;
}

// `LDA #reach : LDX $1E62 : LDY $1E64 : JSR $897E`, the `JSR` at `at`.
static uint16_t player_near(Boss* b, uint16_t reach, uint16_t at) {
  lda(b, reach);
  ldx(b, boss_x(b));
  ldy(b, boss_y(b));
  jsr(b, at);
  return player_within(b);
}

// `$82:8998`: which of the eight ways the point in X and Y is from the
// figure, or 0 for where it stands.
static uint16_t way_to(Boss* b) {
  PortCpu* c = b->c;
  const uint16_t to_x = c->x;
  const uint16_t to_y = c->y;
  wram_w16(b->w, 0x00a2, to_x);
  wram_w16(b->w, 0x00a4, to_y);

  // The table's row is by Y: level, the point below, the point above.
  ldx(b, 0);
  lda(b, boss_y(b));
  cmp16(c, c->a, to_y);
  ran(b, BOSS_RUN_8998);
  if (!took(b, c->a == to_y)) {
    b->k->v_known = true;
    c->a = adc16(c, c->x, 1);  // the compare's carry makes it 1 or 2
    c->a = asl16(c, asl16(c, c->a));
    ldx(b, c->a);
    ran(b, BOSS_RUN_89A9);
  }
  // ...and its column by X, the same.
  lda(b, boss_x(b));
  cmp16(c, c->a, to_x);
  ran(b, BOSS_RUN_89B0);
  if (!took(b, c->a == to_x)) {
    b->k->v_known = true;
    c->a = adc16(c, c->x, 1);
    ldx(b, c->a);
    ran(b, BOSS_RUN_89B8);
  }
  lda(b, rom_word(b->rom, 0x8289c5u + c->x) & 0x00ffu);
  ran(b, BOSS_RUN_89BD);
  rts(b);
  return c->a;
}

// `$82:9204`: face the nearer player, and say which way that is.
static void face_player(Boss* b) {
  PortCpu* c = b->c;
  lda(b, BOSS_SIGHT);
  ldx(b, boss_x(b));
  ldy(b, boss_y(b));
  jsl(b, 0x920d);
  PlayerPickRegs* r = &b->k->bearings[b->k->bearing_count++];
  player_bearing(b->w, b->rom, c->a, c->x, c->y, r);
  rtl(b);
  c->a = r->a;
  c->x = r->x;
  set_flags_pld(b, r->c);

  // The first four ways are to its east, and it is drawn facing west.
  ldy(b, 0xffff);
  cmp16(c, c->a, 5);
  ran(b, BOSS_RUN_9204);
  if (!took(b, c->a < 5)) {
    ldy(b, 0);
    ran(b, BOSS_RUN_9219);
  }
  set_field(b, BOSS_DP_FACING, c->y);
  lda(b, c->y);
  ran(b, BOSS_RUN_921C);
  rts(b);
}

// `LDA $0002,X : STA $20 : LDA $0006,X : STA $22`: it is going to where the
// thing in X is.
static void aim_at_thing(Boss* b) {
  set_field(b, BOSS_DP_TO_X, word_at(b, b->c->x, ACTOR_X));
  lda(b, word_at(b, b->c->x, ACTOR_Y));
  set_field(b, BOSS_DP_TO_Y, b->c->a);
}

// --- What it does next ------------------------------------------------------
//
// Each of these names the state and stops on the `RTS` the state would
// return by, which is what it hands back.

static uint32_t begin_lining_up(Boss* b);

// `$82:8A34`: a rampage, for 48 frames and a draw more. Its way is turned to
// the next diagonal.
static uint32_t begin_rampage(Boss* b) {
  PORT_COVER(boss_rampages);
  set_field(b, BOSS_DP_STATE, BOSS_STATE_RAMPAGING);
  set_field(b, BOSS_DP_TEMPER, BOSS_TEMPER_RAMPAGE);
  set_field(b, BOSS_DP_TIMER, add(b, draw(b, 0x8a3e), 0x30));
  const uint16_t way = add(b, field(b, BOSS_DP_WAY), 8) & 0x0038u;
  set_field(b, BOSS_DP_WAY, add(b, way, 8));
  ran(b, BOSS_RUN_8A34);
  return BOSS_RAMPAGE_BEGUN_RTS_PC;
}

// `$82:8B06`: to the point at `$20`, which is first put on the figure's own
// four-pixel grid so that a step of two can land on it.
static uint32_t begin_going(Boss* b) {
  PORT_COVER(boss_goes_to_point);
  set_field(b, BOSS_DP_SCRATCH, boss_x(b) & 3);
  set_field(b, BOSS_DP_TO_X,
            (field(b, BOSS_DP_TO_X) & 0xfffcu) | field(b, BOSS_DP_SCRATCH));
  set_field(b, BOSS_DP_SCRATCH, boss_y(b) & 3);
  set_field(b, BOSS_DP_TO_Y,
            (field(b, BOSS_DP_TO_Y) & 0xfffcu) | field(b, BOSS_DP_SCRATCH));
  lda(b, BOSS_STATE_GOING);
  set_field(b, BOSS_DP_STATE, b->c->a);
  ran(b, BOSS_RUN_8B06);
  return BOSS_GOING_BEGUN_RTS_PC;
}

// `$82:8B85`.
static uint32_t begin_stamping(Boss* b) {
  PORT_COVER(boss_stamps);
  set_field(b, BOSS_DP_STATE, BOSS_STATE_STAMPING);
  lda(b, BOSS_STAMP_FRAMES);
  set_field(b, BOSS_DP_TIMER, b->c->a);
  ran(b, BOSS_RUN_8B85);
  return BOSS_STAMP_BEGUN_RTS_PC;
}

// `$82:8C5F`: north or south, to its row.
static uint32_t begin_home(Boss* b) {
  PORT_COVER(boss_goes_home);
  set_field(b, BOSS_DP_STATE, BOSS_STATE_HOME);
  ldy(b, 0x0008);
  lda(b, boss_y(b));
  cmp16(b->c, b->c->a, field(b, BOSS_DP_HOME_Y));
  ran(b, BOSS_RUN_8C5F);
  if (!took(b, b->c->a >= field(b, BOSS_DP_HOME_Y))) {
    ldy(b, 0x0028);
    ran(b, BOSS_RUN_8C6E);
  }
  set_field(b, BOSS_DP_WAY, b->c->y);
  lda(b, BOSS_HOME_FRAMES);
  set_field(b, BOSS_DP_TIMER, b->c->a);
  ran(b, BOSS_RUN_8C71);
  return BOSS_HOME_BEGUN_RTS_PC;
}

// `$82:8CBD`: it turns to the player, and will spit from the side it faces.
static uint32_t begin_lining_up(Boss* b) {
  PORT_COVER(boss_lines_up);
  set_field(b, BOSS_DP_STATE, BOSS_STATE_LINING_UP);
  ran(b, BOSS_RUN_8CBD);
  jsr(b, 0x8cc2);
  face_player(b);
  lda(b, (b->c->a & 0x8000u) | 0x000fu);
  set_field(b, BOSS_DP_SPIT_FACING, b->c->a);
  ran(b, BOSS_RUN_8CC5);
  return BOSS_LINE_UP_BEGUN_RTS_PC;
}

// `$82:89D2`: nothing in mind. One time in thirteen it rampages; else it
// paces for up to 63 frames, east or west, or with under 8 drawn goes to spit.
static uint32_t choose(Boss* b) {
  PortCpu* c = b->c;
  set_field(b, BOSS_DP_STATE, BOSS_STATE_PACING);
  uint16_t drawn = draw(b, 0x89d7);
  cmp16(c, drawn, 0x0014);
  ran(b, BOSS_RUN_89D2);
  if (took(b, drawn < 0x0014)) {
    ran(b, BOSS_RUN_8A31);
    return begin_rampage(b);
  }
  lda(b, drawn & 0x003fu);
  set_field(b, BOSS_DP_TIMER, c->a);
  cmp16(c, c->a, 0x0008);
  ran(b, BOSS_RUN_89E0);
  if (took(b, c->a < 0x0008)) {
    ran(b, BOSS_RUN_8A2E);
    return begin_lining_up(b);
  }
  PORT_COVER(boss_paces);
  drawn = draw(b, 0x89ea);
  set_field(b, BOSS_DP_WAY, add(b, drawn & 0x0020u, 0x0018));
  ran(b, BOSS_RUN_89EA);
  return BOSS_CHOSE_RTS_PC;
}

// --- The states -------------------------------------------------------------

// `$82:8A0E`: a step the way it paces.
static uint32_t pacing_step(Boss* b) {
  lda(b, BOSS_STEP_PLAIN);
  step(b, 0x8a11, BOSS_STEP_PLAIN);
  ran(b, BOSS_RUN_8A0E);
  return BOSS_PACED_RTS_PC;
}

// `$82:89F8`.
static uint32_t pacing(Boss* b) {
  PortCpu* c = b->c;
  ldx(b, player_near(b, BOSS_SIGHT, 0x8a01));
  ran(b, BOSS_RUN_89F8);
  if (took(b, c->x != 0)) {
    // A player in sight: three times in four it goes to them.
    lda(b, draw(b, 0x8a15) & 0x0003u);
    ran(b, BOSS_RUN_8A15);
    if (took(b, c->a != 0)) {
      aim_at_thing(b);
      ran(b, BOSS_RUN_8A21);
      return begin_going(b);
    }
    ran(b, BOSS_RUN_8A1E);
    return choose(b);
  }
  const uint16_t left = count_down(b, BOSS_DP_TIMER);
  ran(b, BOSS_RUN_8A07);
  if (!took(b, !negative(left))) return BOSS_CHOOSE_CALL_PC;
  return pacing_step(b);
}

// `$82:8A79`: stuck, so another way, by the one it had and a draw.
static uint32_t turn_away(Boss* b) {
  PORT_COVER(boss_turns_away);
  ldx(b, add(b, draw(b, 0x8a79) & 0x0006u, field(b, BOSS_DP_WAY)));
  lda(b, table(b, (uint16_t)(0x8abe + b->c->x)));
  set_field(b, BOSS_DP_WAY, b->c->a);
  ran(b, BOSS_RUN_8A79);
  return BOSS_TURNED_RTS_PC;
}

// `$82:8A58`.
static uint32_t rampaging(Boss* b) {
  PortCpu* c = b->c;
  lda(b, field(b, BOSS_DP_TEMPER));
  ran(b, BOSS_RUN_8A58);
  if (took(b, negative(c->a))) {
    // Stung: it goes for the nearer player.
    PORT_COVER(boss_stung_rampaging);
    set_field(b, BOSS_DP_TEMPER, BOSS_TEMPER_CALM);
    ran(b, BOSS_RUN_8A9D);
    set_field(b, BOSS_DP_PLAYER, player_near(b, BOSS_SIGHT, 0x8aab));
    ldx(b, c->a);
    aim_at_thing(b);
    ran(b, BOSS_RUN_8AAE);
    return begin_lining_up(b);
  }
  const uint16_t left = count_down(b, BOSS_DP_TIMER);
  ran(b, BOSS_RUN_8A5C);
  if (took(b, negative(left))) {
    // Run out: it stamps, or goes to spit, by the frame.
    set_field(b, BOSS_DP_TEMPER, BOSS_TEMPER_CALM);
    lda(b, wram_r16(b->w, W_FRAME_COUNT) & 0x0001u);
    ran(b, BOSS_RUN_8A8A);
    if (took(b, c->a != 0)) {
      ran(b, BOSS_RUN_8A9A);
      return begin_stamping(b);
    }
    ran(b, BOSS_RUN_8A97);
    return begin_lining_up(b);
  }
  static const struct {
    BossRun run;
    uint16_t call;
  } STEPS[] = {
      {BOSS_RUN_8A60, 0x8a63}, {BOSS_RUN_8A68, 0x8a6b}, {BOSS_RUN_8A70, 0x8a73}};
  for (int i = 0; i < 3; i++) {
    const bool stuck = step(b, STEPS[i].call, BOSS_STEP_FAST);
    ran(b, STEPS[i].run);
    if (took(b, stuck)) return turn_away(b);
  }
  return BOSS_RAMPAGED_RTS_PC;
}

// `$82:8B7F`: there, or as near as it gets.
static uint32_t arrived(Boss* b) {
  ran(b, BOSS_RUN_8B7F);
  return begin_stamping(b);
}

// `$82:8B2E`.
static uint32_t going(Boss* b) {
  PortCpu* c = b->c;
  ldx(b, field(b, BOSS_DP_TO_X));
  ldy(b, field(b, BOSS_DP_TO_Y));
  ran(b, BOSS_RUN_8B2E);
  jsr(b, 0x8b32);
  const uint16_t way = way_to(b);
  ldx(b, way);
  ran(b, BOSS_RUN_8B35);
  if (took(b, way == 0)) return arrived(b);

  set_field(b, BOSS_DP_WAY, c->a = asl16(c, asl16(c, asl16(c, way))));
  // In the same eight pixels both ways is there.
  set_field(b, BOSS_DP_SCRATCH, field(b, BOSS_DP_TO_X) & 0xfff8u);
  lda(b, boss_x(b) & 0xfff8u);
  cmp16(c, c->a, field(b, BOSS_DP_SCRATCH));
  ran(b, BOSS_RUN_8B38);
  if (!took(b, c->a != field(b, BOSS_DP_SCRATCH))) {
    set_field(b, BOSS_DP_SCRATCH, field(b, BOSS_DP_TO_Y) & 0xfff8u);
    lda(b, boss_y(b) & 0xfff8u);
    cmp16(c, c->a, field(b, BOSS_DP_SCRATCH));
    ran(b, BOSS_RUN_8B4E);
    if (took(b, c->a == field(b, BOSS_DP_SCRATCH))) return arrived(b);
  }

  bool stuck = step(b, 0x8b62, BOSS_STEP_FAST);
  ran(b, BOSS_RUN_8B5F);
  if (!took(b, stuck)) {
    stuck = step(b, 0x8b6a, BOSS_STEP_FAST);
    ran(b, BOSS_RUN_8B67);
    if (!took(b, stuck)) return BOSS_WENT_RTS_PC;
  }
  // Stuck. With a player right by it stamps, and else gives up for home.
  PORT_COVER(boss_stuck_going);
  ldx(b, player_near(b, 0x0018, 0x8b79));
  ran(b, BOSS_RUN_8B70);
  if (took(b, c->x == 0)) {
    ran(b, BOSS_RUN_8B82);
    return begin_home(b);
  }
  return arrived(b);
}

// `$82:8BCB`: nobody to mind. One time in four it goes home.
static uint32_t nobody_near(Boss* b) {
  lda(b, draw(b, 0x8bcb) & 0x0003u);
  ran(b, BOSS_RUN_8BCB);
  if (took(b, b->c->a != 0)) {
    ran(b, BOSS_RUN_8BE2);
    return begin_rampage(b);
  }
  ran(b, BOSS_RUN_8BD4);
  return begin_home(b);
}

// One offset of a hop: 32 to 95, and the other way when its bit 1 is set.
static void hop_offset(Boss* b, uint16_t draw_at, uint16_t at, BossRun drawn,
                       BossRun turned) {
  uint16_t off = add(b, draw(b, draw_at) & 0x003fu, 0x0020);
  set_field(b, at, off);
  lda(b, off & 0x0002u);
  ran(b, drawn);
  if (!took(b, b->c->a == 0)) {
    off = negate(b, off);
    set_field(b, at, off);
    ran(b, turned);
  }
}

// `$82:8C02`: somebody right under it, so it goes to a point a hop away.
static uint32_t hop(Boss* b) {
  PORT_COVER(boss_hops);
  hop_offset(b, 0x8c02, BOSS_DP_SCRATCH, BOSS_RUN_8C02, BOSS_RUN_8C14);
  hop_offset(b, 0x8c1c, BOSS_DP_SCRATCH + 2, BOSS_RUN_8C1C, BOSS_RUN_8C2E);
  set_field(b, BOSS_DP_TO_X, add(b, boss_x(b), field(b, BOSS_DP_SCRATCH)));
  set_field(b, BOSS_DP_TO_Y,
            add(b, boss_y(b), field(b, BOSS_DP_SCRATCH + 2)));
  ran(b, BOSS_RUN_8C36);
  return begin_going(b);
}

// `$82:8B90`.
static uint32_t stamping(Boss* b) {
  PortCpu* c = b->c;
  lda(b, field(b, BOSS_DP_TEMPER));
  ran(b, BOSS_RUN_8B90);
  if (took(b, negative(c->a))) {
    PORT_COVER(boss_stung_stamping);
    ran(b, BOSS_RUN_8BE2);
    return begin_rampage(b);
  }
  // Its stride runs three times as fast as it stamps.
  count_down(b, BOSS_DP_STRIDE_LEFT);
  count_down(b, BOSS_DP_STRIDE_LEFT);
  const uint16_t left = count_down(b, BOSS_DP_TIMER);
  ran(b, BOSS_RUN_8B94);
  if (took(b, !negative(left))) {
    // The job that shakes the screen, for the harness to queue.
    lda(b, 0x8c49);
    ldy(b, BOSS_THREAD_BANK);
    ran(b, BOSS_RUN_8BD7);
    return BOSS_SHAKE_PC;
  }

  // Done. What it does next is by what is nearest, and how near.
  ldx(b, boss_x(b));
  ldy(b, boss_y(b));
  jsl(b, 0x8ba2);
  uint16_t gap = 0;
  const uint16_t thing =
      actor_nearest_counted(b->w, c->x, c->y, &gap, &b->k->nearest);
  b->k->sought = true;
  rtl(b);
  set_flags_pld(b, false);
  c->y = thing;
  c->x = gap;
  const uint16_t id = word_at(b, thing, ACTOR_COLLIDE_ID);
  lda(b, id);
  cmp16(c, id, BOSS_ID_PLAYER_A);
  ran(b, BOSS_RUN_8B9C);
  bool minded = took(b, id == BOSS_ID_PLAYER_A);
  if (!minded) {
    cmp16(c, id, BOSS_ID_PLAYER_B);
    ran(b, BOSS_RUN_8BB0);
    minded = took(b, id == BOSS_ID_PLAYER_B);
  }
  if (!minded) {
    cmp16(c, id, BOSS_ID_MINDED);
    ran(b, BOSS_RUN_8BB5);
    minded = took(b, id == BOSS_ID_MINDED);
  }
  if (!minded) {
    took(b, true);
    ran(b, BOSS_RUN_8BBA);
    return nobody_near(b);
  }

  cmp16(c, gap, 0x0020);
  ran(b, BOSS_RUN_8BBC);
  if (took(b, gap < 0x0020)) return hop(b);
  cmp16(c, gap, 0x0068);
  ran(b, BOSS_RUN_8BC1);
  if (took(b, gap < 0x0068)) {
    ran(b, BOSS_RUN_8BFF);
    return begin_lining_up(b);
  }
  cmp16(c, gap, 0x0078);
  ran(b, BOSS_RUN_8BC6);
  if (took(b, gap < 0x0078)) {
    PORT_COVER(boss_closes_in);
    ldx(b, player_near(b, 0x0078, 0x8bee));
    aim_at_thing(b);
    ran(b, BOSS_RUN_8BE5);
    return begin_going(b);
  }
  return nobody_near(b);
}

// `$82:8C79`.
static uint32_t going_home(Boss* b) {
  PortCpu* c = b->c;
  lda(b, field(b, BOSS_DP_TEMPER));
  ran(b, BOSS_RUN_8C79);
  if (took(b, negative(c->a))) {
    PORT_COVER(boss_stung_going_home);
    ran(b, BOSS_RUN_8CAA);
    return begin_rampage(b);
  }
  uint16_t off = sub(b, boss_y(b), field(b, BOSS_DP_HOME_Y));
  ran(b, BOSS_RUN_8C7D);
  if (!took(b, !negative(off))) {
    off = negate(b, off);
    ran(b, BOSS_RUN_8C85);
  }
  cmp16(c, off, 0x0002);
  ran(b, BOSS_RUN_8C89);
  if (took(b, off < 0x0002)) {
    PORT_COVER(boss_got_home);
    ran(b, BOSS_RUN_8CAD);
    return choose(b);
  }
  lda(b, BOSS_STEP_PLAIN);
  const bool stuck = step(b, 0x8c91, BOSS_STEP_PLAIN);
  ran(b, BOSS_RUN_8C8E);
  if (took(b, stuck)) {
    ran(b, BOSS_RUN_8CAA);
    return begin_rampage(b);
  }
  const uint16_t left = count_down(b, BOSS_DP_TIMER);
  ran(b, BOSS_RUN_8C96);
  if (took(b, !negative(left))) return BOSS_HOME_RTS_PC;
  // Too long about it: a player within 120 and it goes to them.
  ldx(b, player_near(b, 0x0078, 0x8ca3));
  ran(b, BOSS_RUN_8C9A);
  if (took(b, c->x != 0)) {
    PORT_COVER(boss_home_gives_up);
    aim_at_thing(b);
    ran(b, BOSS_RUN_8CB0);
    return begin_going(b);
  }
  return BOSS_HOME_RTS_PC;
}

// `$82:8D4E`: no player, or no way to the spot.
static uint32_t lose_patience(Boss* b) {
  set_field(b, BOSS_DP_SPIT_FACING, 0);
  ran(b, BOSS_RUN_8D4E);
  return begin_rampage(b);
}

// `$82:8D53`: stung, or the player is too close beside it to spit at.
static uint32_t go_to_player(Boss* b) {
  PORT_COVER(boss_goes_to_player);
  set_field(b, BOSS_DP_TEMPER, BOSS_TEMPER_CALM);
  set_field(b, BOSS_DP_SPIT_FACING, 0);
  ldx(b, field(b, BOSS_DP_PLAYER));
  aim_at_thing(b);
  ran(b, BOSS_RUN_8D53);
  return begin_going(b);
}

static uint32_t in_place(Boss* b);

// `SEC : SBC : BPL : EOR #$FFFF : INC`: how far apart, with its two runs.
static uint16_t apart(Boss* b, uint16_t from, uint16_t to, BossRun taken_off,
                      BossRun turned) {
  uint16_t gap = sub(b, from, to);
  ran(b, taken_off);
  if (!took(b, !negative(gap))) {
    gap = negate(b, gap);
    ran(b, turned);
  }
  return gap;
}

// `$82:8CCE`.
static uint32_t lining_up(Boss* b) {
  PortCpu* c = b->c;
  const uint16_t player = player_near(b, BOSS_SIGHT, 0x8cd7);
  set_field(b, BOSS_DP_PLAYER, player);
  ldy(b, player);
  ran(b, BOSS_RUN_8CCE);
  if (took(b, player == 0)) {
    PORT_COVER(boss_lost_player);
    return lose_patience(b);
  }
  lda(b, field(b, BOSS_DP_TEMPER));
  ran(b, BOSS_RUN_8CDF);
  if (took(b, negative(c->a))) return go_to_player(b);

  uint16_t gap = apart(b, word_at(b, player, ACTOR_X), boss_x(b),
                       BOSS_RUN_8CE3, BOSS_RUN_8CEC);
  cmp16(c, gap, 0x0020);
  ran(b, BOSS_RUN_8CF0);
  if (took(b, gap < 0x0020)) return go_to_player(b);

  // The spot: 72 to the side it faces, and 32 above.
  uint16_t aside = 0x0048;
  lda(b, aside);
  ldx(b, field(b, BOSS_DP_SPIT_FACING));
  ran(b, BOSS_RUN_8CF5);
  if (!took(b, !negative(c->x))) {
    aside = negate(b, aside);
    ran(b, BOSS_RUN_8CFC);
  }
  const uint16_t spot_x = add(b, aside, word_at(b, player, ACTOR_X));
  set_field(b, BOSS_DP_SPOT_X, spot_x);
  const uint16_t spot_y = add(b, word_at(b, player, ACTOR_Y), 0xffe0);
  set_field(b, BOSS_DP_SPOT_Y, spot_y);

  gap = apart(b, spot_x, boss_x(b), BOSS_RUN_8D00, BOSS_RUN_8D17);
  cmp16(c, gap, 0x0012);
  ran(b, BOSS_RUN_8D1B);
  if (!took(b, gap >= 0x0012)) {
    gap = apart(b, spot_y, boss_y(b), BOSS_RUN_8D20, BOSS_RUN_8D28);
    cmp16(c, gap, 0x0012);
    ran(b, BOSS_RUN_8D2C);
    if (!took(b, gap >= 0x0012)) {
      took(b, true);
      ran(b, BOSS_RUN_8D31);
      return in_place(b);
    }
  }

  ldx(b, spot_x);
  ldy(b, spot_y);
  jsr(b, 0x8d37);
  const uint16_t way = way_to(b);
  ran(b, BOSS_RUN_8D33);
  if (took(b, way == 0)) return in_place(b);
  set_field(b, BOSS_DP_WAY, c->a = asl16(c, asl16(c, asl16(c, way))));
  lda(b, BOSS_STEP_FAST);
  const bool stuck = step(b, 0x8d44, BOSS_STEP_FAST);
  ran(b, BOSS_RUN_8D3C);
  if (took(b, stuck)) {
    PORT_COVER(boss_stuck_lining_up);
    return lose_patience(b);
  }
  lda(b, field(b, BOSS_DP_SPIT_FACING));
  set_field(b, BOSS_DP_FACING, c->a);
  ran(b, BOSS_RUN_8D49);
  return BOSS_LINED_UP_RTS_PC;
}

// --- Its turn ---------------------------------------------------------------

// `$82:957E`: to the state, by an `RTS` that comes back to the turn's end.
static uint32_t state_goes(Boss* b) {
  push16(b->w, b->c, (uint16_t)BOSS_STATE_PC);
  lda(b, (uint16_t)(field(b, BOSS_DP_STATE) - 1));
  push16(b->w, b->c, b->c->a);
  ran(b, BOSS_RUN_957E);
  return BOSS_STATE_PC;
}

// `$82:892E`: picture A of its four, its offset mirrored if it is. Stops on
// the upload that draws it.
static uint32_t picture(Boss* b) {
  PortCpu* c = b->c;
  ldx(b, asl16(c, asl16(c, c->a)));
  wram_w16(b->w, BOSS_BG_DP_SRC, table(b, (uint16_t)(0x895e + c->x)));
  wram_w16(b->w, BOSS_BG_DP_SRC_BANK, table(b, (uint16_t)(0x8960 + c->x)));
  lda(b, table(b, (uint16_t)(0x896e + c->x)));
  const uint16_t facing = field(b, BOSS_DP_FACING);
  test_bits(b, facing);
  ran(b, BOSS_RUN_892E);
  if (!took(b, !negative(facing))) {
    negate(b, c->a);
    ran(b, BOSS_RUN_8944);
  }
  wram_w16(b->w, W_BOSS_DRAW_DX, c->a);
  wram_w16(b->w, W_BOSS_DRAW_DY, table(b, (uint16_t)(0x8970 + c->x)));
  lda(b, facing);
  ran(b, BOSS_RUN_8948);

  const bool mirrored = took(b, facing != 0);
  if (mirrored) {
    PORT_COVER(boss_picture_mirrored);
    ran(b, BOSS_RUN_895A);
    jsr(b, 0x895a);
    ran(b, BOSS_RUN_891D);
  } else {
    ran(b, BOSS_RUN_8955);
    jsr(b, 0x8955);
    ran(b, BOSS_RUN_890C);
  }
  wram_w16(b->w, W_BOSS_PLANE_AT_X, mirrored ? 0x0032 : 0x003c);
  lda(b, BOSS_THREAD_BANK);
  wram_w16(b->w, W_BOSS_PLANE_AT_Y, c->a);
  return mirrored ? BOSS_PICTURE_FLIP_PC : BOSS_PICTURE_PC;
}

// `$82:9579`: its stride's next picture when that is due and the last has
// gone up, and then the state.
static uint32_t turn(Boss* b) {
  PortCpu* c = b->c;
  set_field(b, BOSS_DP_SCORED, 0);
  ran(b, BOSS_RUN_9579);
  jsr(b, 0x957b);

  const uint16_t left = count_down(b, BOSS_DP_STRIDE_LEFT);
  ran(b, BOSS_RUN_9220);
  if (!took(b, !negative(left))) {
    jsl(b, 0x9224);
    lda(b, wram_r16(b->w, W_BG_DMA_CURSOR));
    rtl(b);
    ran(b, BOSS_RUN_9224);
    ran(b, BOSS_RUN_80E0);
    if (!took(b, c->a != 0)) {
      PORT_COVER(boss_strides);
      set_field(b, BOSS_DP_STRIDE_LEFT, BOSS_STRIDE_FRAMES);
      const uint16_t stride = (uint16_t)(field(b, BOSS_DP_STRIDE) + 1) & 3;
      set_field(b, BOSS_DP_STRIDE, stride);
      ldx(b, asl16(c, stride));
      lda(b, table(b, (uint16_t)(0x9240 + c->x)));
      ran(b, BOSS_RUN_922A);
      jsr(b, 0x923c);
      return picture(b);
    }
    PORT_COVER(boss_stride_waits);
  }
  ran(b, BOSS_RUN_RTS);
  rts(b);
  return state_goes(b);
}

// `$82:959C`.
static uint32_t wakes(Boss* b) {
  lda(b, field(b, BOSS_DP_DEAD));
  ran(b, BOSS_RUN_959C);
  if (!took(b, b->c->a == 0)) return BOSS_DIES_PC;
  return turn(b);
}

// `$82:9589`: where its plane is scrolled to, which is its place and the
// picture's offset less where on the plane the picture is.
static uint32_t flashed(Boss* b) {
  ran(b, BOSS_RUN_9589);
  jsr(b, 0x9589);
  const uint16_t x = add(b, wram_r16(b->w, W_BOSS_DRAW_DX), boss_x(b));
  wram_w16(b->w, W_BOSS_PLANE_X,
           sub(b, x, wram_r16(b->w, W_BOSS_PLANE_AT_X)));
  const uint16_t y = add(b, wram_r16(b->w, W_BOSS_DRAW_DY), boss_y(b));
  wram_w16(b->w, W_BOSS_PLANE_Y,
           sub(b, y, wram_r16(b->w, W_BOSS_PLANE_AT_Y)));
  ran(b, BOSS_RUN_9248);
  rts(b);
  return BOSS_PARTS_PC;
}

// `$82:9586`: a hit shows as three frames of other colours.
static uint32_t turn_ends(Boss* b) {
  ran(b, BOSS_RUN_9586);
  jsr(b, 0x9586);
  lda(b, field(b, BOSS_DP_FLASH_LEFT));
  ran(b, BOSS_RUN_8F6A);
  if (took(b, b->c->a == 0)) {
    const uint16_t hit = field(b, BOSS_DP_HIT);
    test_bits(b, hit);
    ran(b, BOSS_RUN_8F7D);
    if (!took(b, !negative(hit))) {
      PORT_COVER(boss_flashes);
      set_field(b, BOSS_DP_HIT, 0);
      set_field(b, BOSS_DP_FLASH_LEFT, BOSS_FLASH_FRAMES);
      lda(b, 0xffc3);
      ldy(b, 0x0095);
      ran(b, BOSS_RUN_8F81);
      return BOSS_FLASH_PC;
    }
  } else {
    const uint16_t left = count_down(b, BOSS_DP_FLASH_LEFT);
    ran(b, BOSS_RUN_8F6E);
    if (!took(b, left != 0)) {
      PORT_COVER(boss_flash_over);
      lda(b, 0xffd1);
      ldy(b, 0x0092);
      ran(b, BOSS_RUN_8F72);
      return BOSS_UNFLASH_PC;
    }
  }
  ran(b, BOSS_RUN_RTS);
  rts(b);
  return flashed(b);
}

// --- Its spit ---------------------------------------------------------------

static uint16_t spit_field(const Boss* b, uint16_t at) {
  return field(b, (uint16_t)(at + b->c->x));
}

static void set_spit_field(Boss* b, uint16_t at, uint16_t v) {
  set_field(b, (uint16_t)(at + b->c->x), v);
}

// `LDA $48,X : CMP #$FFFF : BEQ`: is the one in X out?
static bool spit_out(Boss* b) {
  lda(b, spit_field(b, BOSS_DP_SPIT_RECORD));
  cmp16(b->c, b->c->a, BOSS_SPIT_NONE);
  return b->c->a != BOSS_SPIT_NONE;
}

// `$82:93E1`: the frame's other one.
static uint32_t spit_second(Boss* b) {
  ldx(b, pull16(b->w, b->c));
  const bool out = spit_out(b);
  ran(b, BOSS_RUN_93E1);
  if (took(b, !out)) return BOSS_SPITS_RTS_PC;
  set_field(b, BOSS_DP_SPIT, b->c->x);
  ran(b, BOSS_RUN_93E9);
  return BOSS_SPIT_SECOND_CALL_PC;
}

// `$82:9595`.
static uint32_t sleeps(Boss* b) {
  lda(b, 1);
  ran(b, BOSS_RUN_9595);
  return BOSS_YIELD_PC;
}

// `$82:93C6`: two of the four a frame, by turns. Stops on a call to move
// one, or on its own `RTS`.
static uint32_t spits_due(Boss* b) {
  PortCpu* c = b->c;
  ldy(b, asl16(c, wram_r16(b->w, W_FRAME_COUNT) & 0x0001u));
  push16(b->w, c, table(b, (uint16_t)(0x93f3 + c->y)));
  ldx(b, table(b, (uint16_t)(0x93ef + c->y)));
  const bool out = spit_out(b);
  ran(b, BOSS_RUN_93C6);
  if (!took(b, !out)) {
    set_field(b, BOSS_DP_SPIT, c->x);
    ran(b, BOSS_RUN_93DC);
    return BOSS_SPIT_FIRST_CALL_PC;
  }
  return spit_second(b);
}

// `$82:9592`.
static uint32_t spits(Boss* b) {
  ran(b, BOSS_RUN_9592);
  jsr(b, 0x9592);
  const uint32_t pc = spits_due(b);
  if (pc != BOSS_SPITS_RTS_PC) return pc;
  ran(b, BOSS_RUN_RTS);
  rts(b);
  return sleeps(b);
}

// `$82:93F7`: the one at `$68` flies, bursts where its flight ends, shows
// the burst's four pictures and is gone.
static uint32_t spit_moves(Boss* b) {
  PortCpu* c = b->c;
  ldx(b, field(b, BOSS_DP_SPIT));
  lda(b, spit_field(b, BOSS_DP_SPIT_LEFT));
  ran(b, BOSS_RUN_93F7);
  if (took(b, !negative(c->a))) {
    const uint16_t record = spit_field(b, BOSS_DP_SPIT_RECORD);
    ldy(b, record);
    set_field(b, BOSS_DP_SCRATCH, record);
    const uint16_t x =
        add(b, spit_field(b, BOSS_DP_SPIT_DX), word_at(b, record, ACTOR_X));
    set_word_at(b, record, ACTOR_X, x);
    set_field(b, BOSS_DP_SPOT_X, x);
    const uint16_t y =
        add(b, spit_field(b, BOSS_DP_SPIT_DY), word_at(b, record, ACTOR_Y));
    set_word_at(b, record, ACTOR_Y, y);
    set_field(b, BOSS_DP_SPOT_Y, y);
    ldx(b, field(b, BOSS_DP_SPIT));
    const uint16_t left = (uint16_t)(spit_field(b, BOSS_DP_SPIT_LEFT) - 1);
    set_spit_field(b, BOSS_DP_SPIT_LEFT, left);
    set_nz16(c, left);
    ran(b, BOSS_RUN_9420);
    if (took(b, !negative(left))) return BOSS_SPIT_RTS_PC;

    PORT_COVER(boss_spit_bursts);
    set_spit_field(b, BOSS_DP_SPIT_LEFT, 0xfff8);
    ldy(b, record);
    set_word_at(b, record, ACTOR_META, 0xa2bd);
    lda(b, 0x0090);
    set_word_at(b, record, ACTOR_META_BANK, c->a);
    ran(b, BOSS_RUN_9440);
    return BOSS_SPIT_BURST_PC;
  }

  const uint16_t left = (uint16_t)(c->a + 1);
  set_spit_field(b, BOSS_DP_SPIT_LEFT, left);
  set_nz16(c, left);
  ran(b, BOSS_RUN_93FD);
  if (took(b, negative(left))) {
    // A picture every other frame, and it is told on those.
    const uint16_t shown = negate(b, left);
    c->p = (uint8_t)((shown & 1) == 0 ? c->p | PORT_P_Z : c->p & ~PORT_P_Z);
    ran(b, BOSS_RUN_9404);
    if (took(b, (shown & 1) != 0)) {
      ran(b, BOSS_RUN_941D);
      return BOSS_SPIT_RTS_PC;
    }
    const uint16_t record = spit_field(b, BOSS_DP_SPIT_RECORD);
    ldy(b, record);
    set_field(b, BOSS_DP_SCRATCH, record);
    ldx(b, shown);
    lda(b, table(b, (uint16_t)(0x946b + shown)));
    set_word_at(b, record, ACTOR_META, c->a);
    ran(b, BOSS_RUN_940F);
    return BOSS_SPIT_BURSTING_PC;
  }
  PORT_COVER(boss_spit_gone);
  ran(b, BOSS_RUN_9401);
  ldx(b, field(b, BOSS_DP_SPIT));
  lda(b, spit_field(b, BOSS_DP_SPIT_RECORD));
  ran(b, BOSS_RUN_9459);
  return BOSS_SPIT_FREE_PC;
}

// `$82:9461`.
static uint32_t spit_gone(Boss* b) {
  ldx(b, field(b, BOSS_DP_SPIT));
  lda(b, BOSS_SPIT_NONE);
  set_spit_field(b, BOSS_DP_SPIT_RECORD, BOSS_SPIT_NONE);
  count_down(b, BOSS_DP_SPITS);
  ran(b, BOSS_RUN_9461);
  return BOSS_SPIT_GONE_RTS_PC;
}

// `$82:9475`: the box 32 round the record in Y, for `actor_notify_box`.
static uint32_t spit_hits(Boss* b) {
  const uint16_t record = field(b, BOSS_DP_SCRATCH);
  ldy(b, record);
  const uint16_t left = sub(b, word_at(b, record, ACTOR_X), 0x0020);
  wram_w16(b->w, NOTIFY_BOX_DP_X0, left);
  wram_w16(b->w, NOTIFY_BOX_DP_X1, add(b, left, 0x0040));
  const uint16_t top = sub(b, word_at(b, record, ACTOR_Y), 0x0020);
  wram_w16(b->w, NOTIFY_BOX_DP_Y0, top);
  wram_w16(b->w, NOTIFY_BOX_DP_Y1, add(b, top, 0x0040));
  lda(b, 0x0009);
  wram_w16(b->w, NOTIFY_BOX_DP_ID, b->c->a);
  ran(b, BOSS_RUN_9475);
  return BOSS_SPIT_TELL_PC;
}

// --- Spitting ---------------------------------------------------------------

static uint32_t spits_due(Boss* b);

// `INC` of a word of the direct page.
static void count_up(Boss* b, uint16_t at) {
  const uint16_t v = (uint16_t)(field(b, at) + 1);
  set_field(b, at, v);
  set_nz16(b->c, v);
}

// `JSL $8280E0` at `at`: has the last picture not gone up yet?
static bool picture_waits(Boss* b, uint16_t at) {
  jsl(b, at);
  lda(b, wram_r16(b->w, W_BG_DMA_CURSOR));
  rtl(b);
  ran(b, BOSS_RUN_80E0);
  return b->c->a != 0;
}

// The two parts that are its bottle: the one at `$24`, and at `$26` its
// drip.
static uint16_t bottle(const Boss* b) {
  return field(b, BOSS_PARTS_DP_FIRST);
}

static uint16_t drip(const Boss* b) {
  return field(b, BOSS_PARTS_DP_FIRST + BOSS_PARTS_DP_STRIDE);
}

// `$82:8E21`.
static uint32_t lingers_sleeps(Boss* b) {
  lda(b, 1);
  ran(b, BOSS_RUN_8E21);
  return BOSS_LINGER_YIELD_PC;
}

// `$82:8E1A`: seven frames with only its spit moving, and then it lines up
// again.
static uint32_t lingers(Boss* b) {
  const uint16_t left = count_down(b, BOSS_DP_STRIDE);
  ran(b, BOSS_RUN_8E1A);
  if (took(b, negative(left))) {
    ran(b, BOSS_RUN_8E2A);
    return begin_lining_up(b);
  }
  ran(b, BOSS_RUN_8E1E);
  jsr(b, 0x8e1e);
  const uint32_t pc = spits_due(b);
  if (pc != BOSS_SPITS_RTS_PC) return pc;
  ran(b, BOSS_RUN_RTS);
  rts(b);
  return lingers_sleeps(b);
}

// `$82:8E0E`: its mouth shut. With no side to spit from it goes home.
static uint32_t mouth_shut(Boss* b) {
  lda(b, field(b, BOSS_DP_SPIT_FACING));
  ran(b, BOSS_RUN_8E0E);
  if (!took(b, b->c->a != 0)) {
    ran(b, BOSS_RUN_8E12);
    return begin_home(b);
  }
  lda(b, 0x0007);
  set_field(b, BOSS_DP_STRIDE, b->c->a);
  ran(b, BOSS_RUN_8E15);
  return lingers(b);
}

// `$82:8DDE`: no more spit. The bottle goes, and its first picture comes
// back once the last has gone up.
static uint32_t stop_spitting(Boss* b) {
  const bool waits = picture_waits(b, 0x8dde);
  ran(b, BOSS_RUN_8DDE);
  if (!took(b, !waits)) {
    lda(b, 1);
    ran(b, BOSS_RUN_8DE4);
    return BOSS_STOP_YIELD_PC;
  }
  PORT_COVER(boss_stops_spitting);
  ldy(b, bottle(b));
  ldx(b, drip(b));
  set_word_at(b, b->c->y, ACTOR_META, 0);
  set_word_at(b, b->c->x, ACTOR_META, 0);
  set_word_at(b, b->c->y, ACTOR_META_BANK, 0);
  set_word_at(b, b->c->x, ACTOR_META_BANK, 0);
  set_field(b, BOSS_DP_STRIDE_LEFT, BOSS_STRIDE_FRAMES);
  lda(b, 0);
  ran(b, BOSS_RUN_8DED);
  jsr(b, 0x8e0b);
  return picture(b);
}

// `$82:8D98`: its mouth open, and the bottle in its hand the way it faces.
static uint32_t mouth_open(Boss* b) {
  PortCpu* c = b->c;
  ldy(b, bottle(b));
  ldx(b, drip(b));
  set_word_at(b, c->y, ACTOR_META, 0xd840);
  set_word_at(b, c->x, ACTOR_META, 0xd7ee);
  set_word_at(b, c->y, ACTOR_META_BANK, 0x008f);
  set_word_at(b, c->x, ACTOR_META_BANK, 0x008f);
  lda(b, field(b, BOSS_DP_FACING));
  ran(b, BOSS_RUN_8D98);
  if (took(b, c->a == 0)) {
    set_word_at(b, c->y, ACTOR_FLAGS,
                word_at(b, c->y, ACTOR_FLAGS) & (uint16_t)~BOSS_PART_MIRRORED);
    lda(b, word_at(b, c->x, ACTOR_FLAGS) & (uint16_t)~BOSS_PART_MIRRORED);
    set_word_at(b, c->x, ACTOR_FLAGS, c->a);
    ran(b, BOSS_RUN_8DC9);
  } else {
    set_word_at(b, c->y, ACTOR_FLAGS,
                word_at(b, c->y, ACTOR_FLAGS) | BOSS_PART_MIRRORED);
    lda(b, word_at(b, c->x, ACTOR_FLAGS) | BOSS_PART_MIRRORED);
    set_word_at(b, c->x, ACTOR_FLAGS, c->a);
    took(b, true);
    ran(b, BOSS_RUN_8DB5);
  }
  count_up(b, BOSS_DP_STRIDE_LEFT);
  ran(b, BOSS_RUN_8DDB);
  return BOSS_MOUTH_OPEN_RTS_PC;
}

// `$82:8D83`: the picture of its mouth open, once the last has gone up.
static uint32_t opens_mouth(Boss* b) {
  const bool waits = picture_waits(b, 0x8d83);
  ran(b, BOSS_RUN_8D83);
  if (!took(b, !waits)) {
    lda(b, 1);
    ran(b, BOSS_RUN_8D89);
    return BOSS_OPEN_YIELD_PC;
  }
  lda(b, 0x0003);
  ran(b, BOSS_RUN_8D92);
  jsr(b, 0x8d95);
  return picture(b);
}

// `$82:8D7B`: it turns to the player.
static uint32_t turns_to_spit(Boss* b) {
  set_field(b, BOSS_DP_STATE, BOSS_STATE_SPITTING);
  ran(b, BOSS_RUN_8D7B);
  jsr(b, 0x8d80);
  face_player(b);
  return opens_mouth(b);
}

// `$82:8D6C`: in place. It is heard, if the level has its sound.
static uint32_t spit_begins(Boss* b) {
  lda(b, wram_r16(b->w, W_BOSS_SOUNDS));
  cmp16(b->c, b->c->a, BOSS_SOUNDS_ITS_OWN);
  ran(b, BOSS_RUN_8D6C);
  if (!took(b, b->c->a != BOSS_SOUNDS_ITS_OWN)) {
    PORT_COVER(boss_heard);
    lda(b, BOSS_CRY_SFX);
    ran(b, BOSS_RUN_8D74);
    return BOSS_CRY_PC;
  }
  return turns_to_spit(b);
}

// `$82:8D69`: in place.
static uint32_t in_place(Boss* b) {
  PORT_COVER(boss_in_place);
  ran(b, BOSS_RUN_8D69);
  return spit_begins(b);
}

// `$82:8EF4`.
static uint32_t spit_frame_ends(Boss* b) {
  count_up(b, BOSS_DP_STRIDE_LEFT);
  ran(b, BOSS_RUN_8EF4);
  return BOSS_SPAT_RTS_PC;
}

// `$82:8EF7`: the pictures of one spit are done. Three times in four it
// spits again, if it still has a side to spit from.
static uint32_t spit_shown(Boss* b) {
  count_up(b, BOSS_DP_STRIDE_LEFT);
  lda(b, field(b, BOSS_DP_SPIT_FACING));
  ran(b, BOSS_RUN_8EF7);
  if (!took(b, b->c->a == 0)) {
    ran(b, BOSS_RUN_8EFD);
    return stop_spitting(b);
  }
  const uint16_t drawn = draw(b, 0x8f00);
  cmp16(b->c, drawn, 0x00be);
  ran(b, BOSS_RUN_8F00);
  if (!took(b, drawn < 0x00be)) {
    ran(b, BOSS_RUN_8F09);
    return stop_spitting(b);
  }
  PORT_COVER(boss_spits_again);
  lda(b, BOSS_STATE_SPITTING);
  set_field(b, BOSS_DP_STATE, b->c->a);
  ran(b, BOSS_RUN_8F0C);
  return BOSS_SPITS_AGAIN_RTS_PC;
}

// `$82:8EDC`: the bottle's next picture, every sixth frame, to the end of
// its list.
static uint32_t spit_shows(Boss* b) {
  PortCpu* c = b->c;
  const uint16_t left = count_down(b, BOSS_DP_PICTURE_LEFT);
  ran(b, BOSS_RUN_8EDC);
  if (!took(b, !negative(left))) {
    set_field(b, BOSS_DP_PICTURE_LEFT, 0x0005);
    ldy(b, (uint16_t)(field(b, BOSS_DP_PICTURE) + 2));
    set_field(b, BOSS_DP_PICTURE, c->y);
    lda(b, table(b, (uint16_t)(field(b, BOSS_DP_PICTURES) + c->y)));
    ran(b, BOSS_RUN_8EE0);
    if (took(b, c->a == 0)) return spit_shown(b);
    ldy(b, bottle(b));
    set_word_at(b, c->y, ACTOR_META, c->a);
    ran(b, BOSS_RUN_8EEF);
  }
  return spit_frame_ends(b);
}

// `$82:9324`: the record it was given is the spit, where it was aimed from,
// with as many frames to fly as a sixteenth of how far the player is and two.
static uint32_t spit_made(Boss* b) {
  PortCpu* c = b->c;
  const uint16_t record = c->a;
  ldx(b, field(b, BOSS_DP_SPIT));
  set_spit_field(b, BOSS_DP_SPIT_RECORD, record);
  set_field(b, BOSS_DP_SCRATCH, record);
  set_word_at(b, record, ACTOR_X, field(b, BOSS_DP_SPIT_FROM_X));
  set_word_at(b, record, ACTOR_Z, 0);
  set_word_at(b, record, ACTOR_Y, field(b, BOSS_DP_SPIT_FROM_Y));
  set_word_at(b, record, ACTOR_THREAD, wram_r16(b->w, W_SCHED_CUR_TASK));
  set_word_at(b, record, ACTOR_COLLIDE_ID, 0x0009);
  set_word_at(b, record, ACTOR_META_BANK, 0x0090);
  set_spit_field(b, BOSS_DP_SPIT_LEFT,
                 (uint16_t)((field(b, BOSS_DP_SPIT_FAR) >> 4) + 2));

  // Its picture, its flags and its step are by the way it goes.
  const uint16_t way = asl16(c, asl16(c, asl16(c, field(b, BOSS_DP_SPIT_WAY))));
  set_word_at(b, record, ACTOR_META, table(b, (uint16_t)(0x9382 + way)));
  set_word_at(b, record, ACTOR_FLAGS,
              table(b, (uint16_t)(0x9384 + way)) | ACTOR_DRAW |
                  word_at(b, record, ACTOR_FLAGS));
  c->y = way;
  ldx(b, field(b, BOSS_DP_SPIT));
  set_spit_field(b, BOSS_DP_SPIT_DX, table(b, (uint16_t)(0x937e + way)));
  lda(b, table(b, (uint16_t)(0x9380 + way)));
  set_spit_field(b, BOSS_DP_SPIT_DY, c->a);
  ran(b, BOSS_RUN_9324);
  rts(b);
  set_c(c, false);
  ran(b, BOSS_RUN_931C);
  rts(b);

  // ...and the bottle's pictures begin.
  ran(b, BOSS_RUN_8ECE);
  took(b, true);
  lda(b, BOSS_STATE_SPIT_SHOWS);
  set_field(b, BOSS_DP_STATE, c->a);
  set_field(b, BOSS_DP_PICTURE_LEFT, 0);
  set_field(b, BOSS_DP_PICTURE, 0);
  ran(b, BOSS_RUN_8ED3);
  return spit_shows(b);
}

// `$82:8E2D`.
static uint32_t spitting(Boss* b) {
  PortCpu* c = b->c;
  const uint16_t player = field(b, BOSS_DP_PLAYER);
  ldy(b, player);
  const uint16_t gap = apart(b, word_at(b, player, ACTOR_X), boss_x(b),
                             BOSS_RUN_8E2D, BOSS_RUN_8E38);
  cmp16(c, gap, 0x0020);
  ran(b, BOSS_RUN_8E3C);
  if (took(b, gap < 0x0020)) return stop_spitting(b);

  // Which way the player is from its mouth, 44 to the side it faces and 64
  // up, if they are within 128 of it.
  uint16_t aside = 0xffd4;
  lda(b, aside);
  ldx(b, field(b, BOSS_DP_FACING));
  ran(b, BOSS_RUN_8E41);
  if (!took(b, !negative(c->x))) {
    aside = negate(b, aside);
    ran(b, BOSS_RUN_8E48);
  }
  ldx(b, add(b, aside, boss_x(b)));
  ldy(b, add(b, 0xffc0, boss_y(b)));
  lda(b, 0x0080);
  jsl(b, 0x8e5c);
  PlayerPickRegs* r = &b->k->bearings[b->k->bearing_count++];
  player_bearing(b->w, b->rom, c->a, c->x, c->y, r);
  rtl(b);
  c->x = r->x;
  set_flags_pld(b, r->c);
  set_field(b, BOSS_DP_SPIT_FAR, c->x);
  lda(b, r->a);
  ldy(b, c->a);
  ran(b, BOSS_RUN_8E4C);
  if (!took(b, c->a != 0)) {
    PORT_COVER(boss_spit_out_of_reach);
    ran(b, BOSS_RUN_8E65);
    return stop_spitting(b);
  }
  lda(b, field(b, BOSS_DP_SPITS));
  cmp16(c, c->a, 0x0004);
  ran(b, BOSS_RUN_8E68);
  if (!took(b, c->a < 0x0004)) {
    PORT_COVER(boss_spits_all_out);
    ran(b, BOSS_RUN_8E6F);
    return stop_spitting(b);
  }

  // The drip shows, or not, by the frame.
  lda(b, c->x & 0x0003u);
  ran(b, BOSS_RUN_8E72);
  if (!took(b, c->a == 0)) {
    ldy(b, drip(b));
    lda(b, wram_r16(b->w, W_FRAME_COUNT) & 0x0002u);
    ran(b, BOSS_RUN_8E78);
    if (took(b, c->a != 0)) {
      lda(b, 0);
      set_word_at(b, c->y, ACTOR_META, 0);
      set_word_at(b, c->y, ACTOR_META_BANK, 0);
      ran(b, BOSS_RUN_8E90);
    } else {
      set_word_at(b, c->y, ACTOR_META, 0xd7ee);
      lda(b, 0x008f);
      set_word_at(b, c->y, ACTOR_META_BANK, c->a);
      took(b, true);
      ran(b, BOSS_RUN_8E82);
    }
  }

  // One of four spits by a draw, of the four for the side it faces: where
  // from, which way, and the bottle's pictures.
  set_field(b, BOSS_DP_SCRATCH, 0x0020u & field(b, BOSS_DP_FACING));
  const uint16_t which =
      (draw(b, 0x8ea3) & 0x0018u) | field(b, BOSS_DP_SCRATCH);
  set_field(b, BOSS_DP_SCRATCH, which);
  ldx(b, which);
  set_field(b, BOSS_DP_SPIT_FROM_X,
            add(b, table(b, (uint16_t)(0x8f12 + which)), boss_x(b)));
  set_field(b, BOSS_DP_SPIT_FROM_Y,
            add(b, table(b, (uint16_t)(0x8f14 + which)), boss_y(b)));
  set_field(b, BOSS_DP_SPIT_WAY, table(b, (uint16_t)(0x8f16 + which)));
  lda(b, table(b, (uint16_t)(0x8f18 + which)));
  set_field(b, BOSS_DP_PICTURES, c->a);
  ran(b, BOSS_RUN_8E9C);
  jsr(b, 0x8ecb);

  // `$82:9303`: a free one of the four, from the last.
  lda(b, 0x0018);
  ran(b, BOSS_RUN_9303);
  for (;;) {
    ldx(b, c->a);
    const bool out = spit_out(b);
    ran(b, BOSS_RUN_9306);
    if (took(b, !out)) break;
    sub(b, c->x, 0x0008);
    ran(b, BOSS_RUN_930E);
    if (took(b, !negative(c->a))) continue;
    // None: it has spat nothing this frame.
    PORT_COVER(boss_spit_no_slot);
    set_c(c, true);
    ran(b, BOSS_RUN_9315);
    rts(b);
    ran(b, BOSS_RUN_8ECE);
    ran(b, BOSS_RUN_8ED0);
    return spit_frame_ends(b);
  }
  PORT_COVER(boss_spits_one);
  set_field(b, BOSS_DP_SPIT, c->x);
  ran(b, BOSS_RUN_9317);
  jsr(b, 0x9319);
  count_up(b, BOSS_DP_SPITS);
  ran(b, BOSS_RUN_931E);
  return BOSS_SPIT_ALLOC_PC;
}

// --- The stretches ----------------------------------------------------------

static const struct {
  uint32_t pc;
  uint32_t (*run)(Boss* b);
} STRETCHES[] = {
    {BOSS_CHOOSE_PC, choose},
    {BOSS_PACING_PC, pacing},
    {BOSS_PACING_STEP_PC, pacing_step},
    {BOSS_RAMPAGING_PC, rampaging},
    {BOSS_GOING_PC, going},
    {BOSS_STAMPING_PC, stamping},
    {BOSS_HOME_PC, going_home},
    {BOSS_LINING_UP_PC, lining_up},
    {BOSS_SPIT_BEGINS_PC, spit_begins},
    {BOSS_TURNS_TO_SPIT_PC, turns_to_spit},
    {BOSS_OPENS_MOUTH_PC, opens_mouth},
    {BOSS_MOUTH_OPEN_PC, mouth_open},
    {BOSS_STOP_SPITTING_PC, stop_spitting},
    {BOSS_MOUTH_SHUT_PC, mouth_shut},
    {BOSS_LINGERS_PC, lingers},
    {BOSS_LINGERS_SLEEPS_PC, lingers_sleeps},
    {BOSS_SPITTING_PC, spitting},
    {BOSS_SPIT_SHOWS_PC, spit_shows},
    {BOSS_SPIT_MADE_PC, spit_made},
    {BOSS_SPIT_SECOND_PC, spit_second},
    {BOSS_SPIT_MOVES_PC, spit_moves},
    {BOSS_SPIT_GONE_PC, spit_gone},
    {BOSS_SPIT_HITS_PC, spit_hits},
    {BOSS_TURN_PC, turn},
    {BOSS_STATE_GOES_PC, state_goes},
    {BOSS_TURN_ENDS_PC, turn_ends},
    {BOSS_FLASHED_PC, flashed},
    {BOSS_SPITS_PC, spits},
    {BOSS_SLEEPS_PC, sleeps},
    {BOSS_WAKES_PC, wakes},
};

bool boss_thread_begins_at(uint32_t pc) {
  for (unsigned i = 0; i < sizeof STRETCHES / sizeof STRETCHES[0]; i++)
    if (STRETCHES[i].pc == pc) return true;
  return false;
}

void boss_thread_run(Wram* w, const Rom* rom, PortCpu* c, BossWork* k) {
  Boss b = {w, rom, c, k};
  k->v_known = true;
  for (unsigned i = 0; i < sizeof STRETCHES / sizeof STRETCHES[0]; i++) {
    if (STRETCHES[i].pc != c->pc) continue;
    c->pc = STRETCHES[i].run(&b);
    return;
  }
}
