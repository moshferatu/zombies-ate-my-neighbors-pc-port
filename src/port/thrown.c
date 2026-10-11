// What weapons 7 to 11 send out -- see port/thrown.h.

#include "port/thrown.h"

#include "port/coverage.h"
#include "port/oam.h"  // the display record's fields

typedef struct {
  Wram* w;
  const Rom* rom;
  PortCpu* c;
  ThrownWork* k;
} Thrown;

// --- The machine, as these stretches use it ---------------------------------

static uint16_t field(const Thrown* t, uint16_t at) {
  return wram_r16(t->w, (uint16_t)(t->c->d + at));
}

static void set_field(Thrown* t, uint16_t at, uint16_t v) {
  wram_w16(t->w, (uint16_t)(t->c->d + at), v);
}

// A word of its record.
static uint16_t word_at(const Thrown* t, uint16_t record, uint16_t at) {
  return wram_r16(t->w, (uint16_t)(record + at));
}

static void set_word_at(Thrown* t, uint16_t record, uint16_t at, uint16_t v) {
  wram_w16(t->w, (uint16_t)(record + at), v);
}

// A word of one of its tables, which it reads through the data bank.
static uint16_t table(const Thrown* t, uint16_t at) {
  return bus_r16(t->w, t->rom, (uint32_t)THROWN_BANK << 16 | at);
}

static void ran(Thrown* t, ThrownRun run) { t->k->runs[run]++; }

// A branch: counted if it is taken.
static bool took(Thrown* t, bool taken) {
  if (taken) t->k->taken++;
  return taken;
}

static bool negative(uint16_t v) { return (v & 0x8000u) != 0; }

static void lda(Thrown* t, uint16_t v) {
  t->c->a = v;
  set_nz16(t->c, v);
}

// `CLC : ADC`.
static uint16_t add(Thrown* t, uint16_t to, uint16_t v) {
  set_c(t->c, false);
  return t->c->a = adc16(t->c, to, v);
}

// `DEC` of a word of the direct page.
static uint16_t count_down(Thrown* t, uint16_t at) {
  const uint16_t v = (uint16_t)(field(t, at) - 1);
  set_field(t, at, v);
  set_nz16(t->c, v);
  return v;
}

// What a routine called with `PHD` leaves in N and Z when it pulls it.
static void set_flags_pld(Thrown* t, bool carry) {
  PortCpu* c = t->c;
  c->p = (uint8_t)(c->p & ~(PORT_P_N | PORT_P_Z));
  if (negative(c->d)) c->p |= PORT_P_N;
  if (c->d == 0) c->p |= PORT_P_Z;
  set_c(c, carry);
}

uint16_t thrown_weapon_of(const Wram* w, uint16_t side) {
  const uint16_t slot = side == wram_r16(w, W_SCORE_SLOT_SIDE) ? 0 : 2;
  return wram_r16(w, (uint16_t)(W_PLAYER_WEAPON + slot));
}

// --- Making it --------------------------------------------------------------

// `$81:F55E`: the thread begins. Which of the five it is goes on the stack,
// and its record is the harness's to ask for.
static void starts(Thrown* t) {
  PortCpu* c = t->c;
  ran(t, THROWN_RUN_F55E);
  push16(t->w, c, THROWN_READY_RETURN);

  // `$80:9D6A` says which player's slot its side has.
  ran(t, THROWN_RUN_F60D);
  push8(t->w, c, THROWN_BANK);
  push16(t->w, c, 0xf612);
  ran(t, THROWN_RUN_9D6A);
  c->x = 0;
  if (!took(t, field(t, THROWN_DP_SIDE) == wram_r16(t->w, W_SCORE_SLOT_SIDE))) {
    ran(t, THROWN_RUN_9D72);
    c->x = 2;
  }
  ran(t, THROWN_RUN_9D74);
  (void)pull16(t->w, c);
  (void)pull8(t->w, c);

  ran(t, THROWN_RUN_F613);
  set_c(c, true);
  c->a = sbc16(c, wram_r16(t->w, (uint16_t)(W_PLAYER_WEAPON + c->x)),
               THROWN_FIRST_WEAPON);
  t->k->v_known = true;
  if (!took(t, !negative(c->a))) {
    // A weapon before the first of the five: its owner has changed it
    // since they fired. The return address comes off and the thread ends.
    PORT_COVER(thrown_weapon_changed);
    ran(t, THROWN_RUN_F61C);
    lda(t, pull16(t->w, c));
    c->pc = THROWN_QUIT_PC;
    return;
  }
  ran(t, THROWN_RUN_F61E);
  push16(t->w, c, c->a);
  t->k->v_known = true;
  c->pc = THROWN_RECORD_PC;
}

// `$81:F623`: its record, in A and in Y. Its row of `THROWN_KINDS` onto its
// page, and the record set up where it was sent from.
static void made(Thrown* t) {
  PortCpu* c = t->c;
  const uint16_t record = c->y;

  ran(t, THROWN_RUN_F623);
  set_field(t, THROWN_DP_RECORD, c->a);
  set_field(t, THROWN_DP_OWN_RECORD, c->a);
  const uint16_t which = pull16(t->w, c);
  push16(t->w, c, (uint16_t)(which * 4));
  const uint16_t kind = (uint16_t)(THROWN_KINDS + which * 16);
  const uint16_t picture_frames =
      table(t, (uint16_t)(kind + THROWN_KIND_PICTURE_FRAMES));
  set_field(t, THROWN_DP_PICTURE_FRAMES, picture_frames);
  set_field(t, THROWN_DP_FRAMES_LEFT, picture_frames);
  set_field(t, THROWN_DP_RISE, table(t, (uint16_t)(kind + THROWN_KIND_RISE)));
  set_field(t, THROWN_DP_SLOWS,
            table(t, (uint16_t)(kind + THROWN_KIND_SLOWS)));
  set_field(t, THROWN_DP_PICTURE_COUNT,
            table(t, (uint16_t)(kind + THROWN_KIND_PICTURE_COUNT)));
  const uint16_t pictures =
      table(t, (uint16_t)(kind + THROWN_KIND_PICTURES));
  set_field(t, THROWN_DP_PICTURES, pictures);
  set_field(t, THROWN_DP_STEPS,
            table(t, (uint16_t)(kind + THROWN_KIND_STEPS)));
  set_field(t, THROWN_DP_END_PICTURES,
            table(t, (uint16_t)(kind + THROWN_KIND_END_PICTURES)));

  c->x = add(t, pull16(t->w, c), field(t, THROWN_DP_SIDE));
  set_word_at(t, record, ACTOR_COLLIDE_ID,
              table(t, (uint16_t)(THROWN_COLLIDE_IDS + c->x)));
  set_word_at(t, record, ACTOR_X, field(t, THROWN_DP_FROM_X));
  set_word_at(t, record, ACTOR_Z, THROWN_HEIGHT);
  set_word_at(t, record, ACTOR_Y, field(t, THROWN_DP_FROM_Y));
  push16(t->w, c, record);
  (void)pull16(t->w, c);
  set_word_at(t, record, ACTOR_META, table(t, (uint16_t)(pictures + 2)));
  set_word_at(t, record, ACTOR_META_BANK, THROWN_META_BANK);
  set_word_at(t, record, ACTOR_THREAD, wram_r16(t->w, W_SCHED_CUR_TASK));
  set_word_at(t, record, ACTOR_FLAGS,
              (uint16_t)(THROWN_FLAGS | word_at(t, record, ACTOR_FLAGS)));
  set_field(t, THROWN_DP_PICTURE, 0);
  set_field(t, THROWN_DP_AGE, 0);
  set_field(t, THROWN_DP_HIT, 0);
  set_field(t, THROWN_DP_LIFE, THROWN_LIFE);
  c->a = THROWN_HANDLER;
  c->y = THROWN_HANDLER_BANK;
  set_nz16(c, c->y);
  t->k->v_known = true;
  c->pc = THROWN_HANDLER_PC;
}

// `$81:F6A2`: its handler is in. Back from making it, to its charge, and
// then its sound is the harness's to make.
static void ready(Thrown* t) {
  ran(t, THROWN_RUN_F6A2);
  (void)pull16(t->w, t->c);
  ran(t, THROWN_RUN_F561);
  wram_w16(t->w, W_SPAWN_LOAD,
           add(t, wram_r16(t->w, W_SPAWN_LOAD), THROWN_WEIGHT));
  lda(t, THROWN_SOUND);
  t->k->v_known = true;
  t->c->pc = THROWN_SOUND_PC;
}

// `$81:F572`: a tick's sleep.
static void sleeps(Thrown* t) {
  ran(t, THROWN_RUN_F572);
  lda(t, 1);
  t->k->v_known = true;
  t->c->pc = THROWN_SLEEP_PC;
}

// --- A frame ----------------------------------------------------------------

// `$81:F5B9`: it stops touching things, and the pictures of its end are the
// harness's to play.
static void bursts(Thrown* t) {
  PortCpu* c = t->c;
  ran(t, THROWN_RUN_F5B9);
  c->y = field(t, THROWN_DP_RECORD);
  set_word_at(t, c->y, ACTOR_COLLIDE_ID, 0);
  lda(t, field(t, THROWN_DP_END_PICTURES));
  c->pc = THROWN_BURST_PC;
}

// `$81:FEEF`: its height gains its rise, and every so many frames its rise
// is one less.
static void rises(Thrown* t, uint16_t record) {
  PortCpu* c = t->c;
  ran(t, THROWN_RUN_FEEF);
  c->y = record;
  set_word_at(t, record, ACTOR_Z,
              add(t, field(t, THROWN_DP_RISE), word_at(t, record, ACTOR_Z)));
  const uint16_t age = (uint16_t)(field(t, THROWN_DP_AGE) + 1);
  set_field(t, THROWN_DP_AGE, age);
  if (!took(t, (age & field(t, THROWN_DP_SLOWS)) != 0)) {
    PORT_COVER(thrown_slows);
    ran(t, THROWN_RUN_FF02);
    (void)count_down(t, THROWN_DP_RISE);
  }
  ran(t, THROWN_RUN_FF04);
}

// `$81:F5D9`: a step the way it goes.
static void steps(Thrown* t, uint16_t record) {
  PortCpu* c = t->c;
  const uint16_t step =
      (uint16_t)(field(t, THROWN_DP_STEPS) + field(t, THROWN_DP_WAY) * 2);
  ran(t, THROWN_RUN_F5D9);
  c->x = record;
  set_word_at(t, record, ACTOR_X,
              add(t, table(t, step), word_at(t, record, ACTOR_X)));
  set_word_at(t, record, ACTOR_Y,
              add(t, table(t, (uint16_t)(step + 2)),
                  word_at(t, record, ACTOR_Y)));
  c->y = (uint16_t)(field(t, THROWN_DP_WAY) * 2 + 2);
}

// `$81:F5F4`: the next of its pictures when this one's frames are up, and
// the first after the last. A picture comes with a word for its record's
// flags: bits to set, or with the top bit a mask to keep.
static void shows(Thrown* t, uint16_t record) {
  PortCpu* c = t->c;
  ran(t, THROWN_RUN_F5F4);
  if (took(t, !negative(count_down(t, THROWN_DP_FRAMES_LEFT)))) {
    PORT_COVER(thrown_picture_stays);
    ran(t, THROWN_RUN_F60C);
    return;
  }
  ran(t, THROWN_RUN_F5F8);
  set_field(t, THROWN_DP_FRAMES_LEFT, field(t, THROWN_DP_PICTURE_FRAMES));
  uint16_t picture = (uint16_t)(field(t, THROWN_DP_PICTURE) + 1);
  if (took(t, picture < field(t, THROWN_DP_PICTURE_COUNT))) {
    PORT_COVER(thrown_next_picture);
  } else {
    PORT_COVER(thrown_first_picture_again);
    ran(t, THROWN_RUN_F603);
    picture = 0;
  }
  ran(t, THROWN_RUN_F606);
  set_field(t, THROWN_DP_PICTURE, picture);

  ran(t, THROWN_RUN_FF2A);
  c->x = record;
  c->y = asl16(c, asl16(c, picture));
  const uint16_t pictures = field(t, THROWN_DP_PICTURES);
  const uint16_t flags = table(t, (uint16_t)(pictures + c->y));
  if (took(t, negative(flags))) {
    PORT_COVER(thrown_flags_kept);
    ran(t, THROWN_RUN_FF3A);
    set_word_at(t, record, ACTOR_FLAGS,
                (uint16_t)(flags & word_at(t, record, ACTOR_FLAGS)));
  } else {
    PORT_COVER(thrown_flags_set);
    ran(t, THROWN_RUN_FF32);
    set_word_at(t, record, ACTOR_FLAGS,
                (uint16_t)(flags | word_at(t, record, ACTOR_FLAGS)));
    t->k->taken++;  // the `BRA`
  }
  ran(t, THROWN_RUN_FF40);
  c->y = (uint16_t)(c->y + 2);
  set_word_at(t, record, ACTOR_META, table(t, (uint16_t)(pictures + c->y)));
}

// `$81:F579`: a frame. See the header for the order it asks things in.
static void wakes(Thrown* t) {
  PortCpu* c = t->c;
  const uint16_t record = field(t, THROWN_DP_RECORD);
  t->k->v_known = true;

  ran(t, THROWN_RUN_F579);
  c->y = record;
  lda(t, field(t, THROWN_DP_HIT));
  if (!took(t, c->a == 0)) {
    PORT_COVER(thrown_hit);
    ran(t, THROWN_RUN_F57F);
    bursts(t);
    return;
  }

  ran(t, THROWN_RUN_F582);
  if (took(t, negative(count_down(t, THROWN_DP_LIFE)))) {
    PORT_COVER(thrown_ran_out);
    c->pc = THROWN_END_PC;
    return;
  }

  ran(t, THROWN_RUN_F586);
  step_tether_blocked(t->w, word_at(t, record, ACTOR_X),
                      word_at(t, record, ACTOR_Y), &t->k->leash);
  t->k->leashed = true;
  c->a = t->k->leash.a;
  c->x = t->k->leash.x;
  c->y = t->k->leash.y;
  set_flags_pld(t, t->k->leash.blocked);
  if (t->k->leash.v_set) set_v(c, t->k->leash.v);
  if (took(t, t->k->leash.blocked)) {
    PORT_COVER(thrown_past_the_leash);
    c->pc = THROWN_END_PC;
    return;
  }

  ran(t, THROWN_RUN_F594);
  c->y = record;
  lda(t, word_at(t, record, ACTOR_Z));
  if (took(t, negative(c->a))) {
    PORT_COVER(thrown_landed);
    bursts(t);
    return;
  }
  ran(t, THROWN_RUN_F59B);
  cmp16(c, c->a, THROWN_CLEARS_GROUND);
  if (took(t, c->a >= THROWN_CLEARS_GROUND)) {
    PORT_COVER(thrown_over_the_ground);
  } else {
    ran(t, THROWN_RUN_F5A0);
    terrain_point_bit2(t->w, word_at(t, record, ACTOR_X),
                       word_at(t, record, ACTOR_Y), &t->k->ground);
    t->k->tested = true;
    c->x = t->k->ground.x;
    set_c(c, t->k->ground.blocked);
    if (took(t, t->k->ground.blocked)) {
      PORT_COVER(thrown_met_ground);
      bursts(t);
      t->k->v_known = false;
      return;
    }
    PORT_COVER(thrown_ground_clear);
  }

  ran(t, THROWN_RUN_F5AE);
  rises(t, record);
  ran(t, THROWN_RUN_F5B1);
  steps(t, record);
  ran(t, THROWN_RUN_F5B4);
  shows(t, record);
  ran(t, THROWN_RUN_F5B7);
  t->k->taken++;  // the `BRA`
  sleeps(t);
}

bool thrown_begins_at(uint32_t pc) {
  return pc == THROWN_STARTS_PC || pc == THROWN_MADE_PC ||
         pc == THROWN_READY_PC || pc == THROWN_CRIED_PC ||
         pc == THROWN_WAKES_PC;
}

void thrown_run(Wram* w, const Rom* rom, PortCpu* c, ThrownWork* k) {
  Thrown t = {.w = w, .rom = rom, .c = c, .k = k};
  switch (c->pc) {
    case THROWN_STARTS_PC:
      starts(&t);
      break;
    case THROWN_MADE_PC:
      made(&t);
      break;
    case THROWN_READY_PC:
      ready(&t);
      break;
    case THROWN_CRIED_PC:
      sleeps(&t);
      break;
    case THROWN_WAKES_PC:
      wakes(&t);
      break;
  }
}
