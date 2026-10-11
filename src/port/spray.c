// The spray of the fire extinguisher and the flamethrower -- see
// port/spray.h.

#include "port/spray.h"

#include "port/coverage.h"
#include "port/oam.h"  // the display record's fields

typedef struct {
  Wram* w;
  const Rom* rom;
  PortCpu* c;
  SprayWork* k;
} Spray;

// --- The machine, as these stretches use it ---------------------------------

static uint16_t field(const Spray* s, uint16_t at) {
  return wram_r16(s->w, (uint16_t)(s->c->d + at));
}

static void set_field(Spray* s, uint16_t at, uint16_t v) {
  wram_w16(s->w, (uint16_t)(s->c->d + at), v);
}

// A word of a record.
static uint16_t word_at(const Spray* s, uint16_t record, uint16_t at) {
  return wram_r16(s->w, (uint16_t)(record + at));
}

static void set_word_at(Spray* s, uint16_t record, uint16_t at, uint16_t v) {
  wram_w16(s->w, (uint16_t)(record + at), v);
}

// A word of one of its tables, which it reads through the data bank.
static uint16_t table(const Spray* s, uint16_t at) {
  return bus_r16(s->w, s->rom, (uint32_t)SPRAY_BANK << 16 | at);
}

static void ran(Spray* s, SprayRun run) { s->k->runs[run]++; }

// A branch: counted if it is taken.
static bool took(Spray* s, bool taken) {
  if (taken) s->k->taken++;
  return taken;
}

static void lda(Spray* s, uint16_t v) {
  s->c->a = v;
  set_nz16(s->c, v);
}

// `CLC : ADC`.
static uint16_t add(Spray* s, uint16_t to, uint16_t v) {
  set_c(s->c, false);
  return s->c->a = adc16(s->c, to, v);
}

// --- The stretches ----------------------------------------------------------

// `$81:FBEA`: which kind it is, by the weapon its owner holds. `$80:9D6A`
// says which player's slot that is.
static void choose_kind(Spray* s) {
  PortCpu* c = s->c;
  ran(s, SPRAY_RUN_FBEA);
  push8(s->w, c, SPRAY_BANK);
  push16(s->w, c, 0xfbef);
  ran(s, SPRAY_RUN_9D6A);
  c->x = 0;
  if (!took(s, field(s, SPRAY_DP_SIDE) == wram_r16(s->w, W_SCORE_SLOT_SIDE))) {
    ran(s, SPRAY_RUN_9D72);
    c->x = 2;
  }
  ran(s, SPRAY_RUN_9D74);
  (void)pull16(s->w, c);
  (void)pull8(s->w, c);

  ran(s, SPRAY_RUN_FBF0);
  const uint16_t weapon = wram_r16(s->w, (uint16_t)(W_PLAYER_WEAPON + c->x));
  if (took(s, weapon == SPRAY_FLAMETHROWER)) {
    PORT_COVER(spray_of_flame);
    set_field(s, SPRAY_DP_KIND, SPRAY_FLAME);
  } else {
    PORT_COVER(spray_of_foam);
    ran(s, SPRAY_RUN_FBFD);
    set_field(s, SPRAY_DP_KIND, SPRAY_FOAM);
  }
  ran(s, SPRAY_RUN_FC01);
}

// `$81:FAF5`: the thread begins. Its kind and its charge, and then its sound
// is the harness's to make.
static void starts(Spray* s) {
  PortCpu* c = s->c;
  ran(s, SPRAY_RUN_FAF5);
  push16(s->w, c, 0xfaf7);
  choose_kind(s);
  (void)pull16(s->w, c);

  ran(s, SPRAY_RUN_FAF8);
  wram_w16(s->w, W_SPAWN_LOAD,
           add(s, wram_r16(s->w, W_SPAWN_LOAD), SPRAY_WEIGHT));
  push16(s->w, c, SPRAY_MADE_RETURN);
  ran(s, SPRAY_RUN_FB79);
  lda(s, SPRAY_SOUND);
  s->k->v_known = true;
  c->pc = SPRAY_SOUND_PC;
}

// `$81:FB67`: its charge comes off, and its record is the ROM's to free.
static void ends(Spray* s) {
  PortCpu* c = s->c;
  ran(s, SPRAY_RUN_FB67);
  set_c(c, true);
  wram_w16(s->w, W_SPAWN_LOAD,
           sbc16(c, wram_r16(s->w, W_SPAWN_LOAD), SPRAY_WEIGHT));
  ran(s, SPRAY_RUN_FB73);
  lda(s, field(s, SPRAY_DP_RECORD));
  s->k->v_known = true;
  c->pc = SPRAY_FREE_PC;
}

// `$81:FB0A`: a frame. The next picture if this one's frames are up, a step
// further out from where its owner is now, and there if the ground lets it
// be.
static void frame(Spray* s) {
  PortCpu* c = s->c;
  const uint16_t record = field(s, SPRAY_DP_RECORD);

  ran(s, SPRAY_RUN_FB0A);
  const uint16_t frames_left =
      (uint16_t)(field(s, SPRAY_DP_FRAMES_LEFT) - 1);
  set_field(s, SPRAY_DP_FRAMES_LEFT, frames_left);
  if (!took(s, frames_left != 0)) {
    ran(s, SPRAY_RUN_FB0E);
    const uint16_t size = field(s, SPRAY_DP_SIZE);
    set_field(s, SPRAY_DP_FRAMES_LEFT, size);
    if (took(s, size == SPRAY_LAST_SIZE)) {
      PORT_COVER(spray_spent);
      ends(s);
      return;
    }
    PORT_COVER(spray_grew);
    ran(s, SPRAY_RUN_FB18);
    set_field(s, SPRAY_DP_SIZE, (uint16_t)(size + 1));
    const uint16_t picture = field(s, SPRAY_DP_PICTURE);
    set_word_at(s, record, ACTOR_META,
                table(s, (uint16_t)(SPRAY_PICTURES + picture +
                                    field(s, SPRAY_DP_KIND) * 4)));
    set_field(s, SPRAY_DP_PICTURE, (uint16_t)(picture + 2));
  }

  ran(s, SPRAY_RUN_FB2E);
  const uint16_t owner = field(s, SPRAY_DP_OWNER);
  const uint16_t out_x = (uint16_t)(field(s, SPRAY_DP_OUT_X) +
                                    field(s, SPRAY_DP_STEP_X));
  const uint16_t out_y = (uint16_t)(field(s, SPRAY_DP_OUT_Y) +
                                    field(s, SPRAY_DP_STEP_Y));
  const uint16_t x = (uint16_t)(out_x + word_at(s, owner, ACTOR_X));
  const uint16_t y = (uint16_t)(out_y + word_at(s, owner, ACTOR_Y));
  set_field(s, SPRAY_DP_OUT_X, out_x);
  set_field(s, SPRAY_DP_NEXT_X, x);
  set_field(s, SPRAY_DP_OUT_Y, out_y);
  set_field(s, SPRAY_DP_NEXT_Y, y);
  terrain_point_bit2(s->w, x, y, &s->k->ground);
  s->k->tested = true;
  c->x = s->k->ground.x;
  c->y = s->k->ground.y;
  if (took(s, s->k->ground.blocked)) {
    PORT_COVER(spray_met_ground);
    ends(s);
    return;
  }

  PORT_COVER(spray_went_on);
  ran(s, SPRAY_RUN_FB52);
  c->y = record;
  set_word_at(s, record, ACTOR_X, x);
  set_word_at(s, record, ACTOR_Y, y);
  lda(s, 1);
  set_c(c, false);
  c->pc = SPRAY_SLEEP_PC;
}

// `$81:FB84`: its record, in A. Where it starts and its step are its
// owner's side's, for the way they face. Then the frame it was made in.
static void made(Spray* s) {
  PortCpu* c = s->c;
  const uint16_t record = c->a;
  const uint16_t side = field(s, SPRAY_DP_SIDE);
  const uint16_t kind = field(s, SPRAY_DP_KIND);

  ran(s, SPRAY_RUN_FB84);
  set_field(s, SPRAY_DP_RECORD, record);
  const uint16_t place =
      (uint16_t)(field(s, SPRAY_DP_WAY) * 4 +
                 table(s, (uint16_t)(SPRAY_PLACES + side)));
  set_field(s, SPRAY_DP_OUT_X, 0);
  set_word_at(s, record, ACTOR_X,
              (uint16_t)(field(s, SPRAY_DP_OWNER_X) + table(s, place)));
  set_field(s, SPRAY_DP_OUT_Y, 0);
  set_word_at(s, record, ACTOR_Y,
              (uint16_t)(field(s, SPRAY_DP_OWNER_Y) +
                         table(s, (uint16_t)(place + 2))));
  set_field(s, SPRAY_DP_STEP_X, table(s, (uint16_t)(place + 4)));
  set_field(s, SPRAY_DP_STEP_Y, table(s, (uint16_t)(place + 6)));
  set_word_at(s, record, ACTOR_META, SPRAY_FIRST_PICTURE);
  set_word_at(s, record, ACTOR_META_BANK, SPRAY_META_BANK);
  set_word_at(s, record, ACTOR_Z,
              table(s, (uint16_t)(SPRAY_HEIGHTS + side)));
  c->x = (uint16_t)(kind * 2 + side);
  set_word_at(s, record, ACTOR_COLLIDE_ID,
              table(s, (uint16_t)(SPRAY_COLLIDE_IDS + c->x)));
  set_word_at(s, record, ACTOR_THREAD, wram_r16(s->w, W_SCHED_CUR_TASK));
  set_word_at(s, record, ACTOR_FLAGS,
              (uint16_t)(ACTOR_DRAW | word_at(s, record, ACTOR_FLAGS)));
  set_field(s, SPRAY_DP_SIZE, SPRAY_FIRST_SIZE);
  set_field(s, SPRAY_DP_PICTURE, 0);
  c->y = record;
  (void)pull16(s->w, c);

  ran(s, SPRAY_RUN_FB05);
  set_field(s, SPRAY_DP_FRAMES_LEFT, SPRAY_FIRST_FRAMES);
  frame(s);
}

bool spray_begins_at(uint32_t pc) {
  return pc == SPRAY_STARTS_PC || pc == SPRAY_MADE_PC ||
         pc == SPRAY_WAKES_PC;
}

void spray_run(Wram* w, const Rom* rom, PortCpu* c, SprayWork* k) {
  Spray s = {.w = w, .rom = rom, .c = c, .k = k};
  switch (c->pc) {
    case SPRAY_STARTS_PC:
      starts(&s);
      break;
    case SPRAY_MADE_PC:
      made(&s);
      break;
    case SPRAY_WAKES_PC:
      ran(&s, SPRAY_RUN_FB65);
      k->taken++;  // the `BRA`
      frame(&s);
      break;
  }
}
