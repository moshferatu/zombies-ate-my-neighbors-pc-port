// $81:FD11  the squirt gun's water in flight -- see port/squirt.h.

#include "port/squirt.h"

#include "port/coverage.h"
#include "port/oam.h"  // the display record's fields

static uint16_t field(const Wram* w, uint16_t page, uint16_t at) {
  return wram_r16(w, (uint16_t)(page + at));
}

static void set_field(Wram* w, uint16_t page, uint16_t at, uint16_t v) {
  wram_w16(w, (uint16_t)(page + at), v);
}

// `$81:FDF7`: a step along its line, and the picture with it.
static void step(Wram* w, uint16_t page, uint16_t* x, uint16_t* y) {
  const uint16_t record = field(w, page, SQUIRT_DP_RECORD);
  *x = (uint16_t)(field(w, page, SQUIRT_DP_X) +
                  field(w, page, SQUIRT_DP_STEP_X));
  set_field(w, page, SQUIRT_DP_X, *x);
  wram_w16(w, (uint16_t)(record + ACTOR_X), *x);
  *y = (uint16_t)(field(w, page, SQUIRT_DP_Y) +
                  field(w, page, SQUIRT_DP_STEP_Y));
  set_field(w, page, SQUIRT_DP_Y, *y);
  wram_w16(w, (uint16_t)(record + ACTOR_Y), *y);
}

SquirtNext squirt_flight_frame(Wram* w, uint16_t page, TerrainRegs* ground) {
  uint16_t x, y;
  step(w, page, &x, &y);

  terrain_point_bit2(w, x, y, ground);
  if (ground->blocked) return SQUIRT_HIT_GROUND;

  const uint16_t left = (uint16_t)(field(w, page, SQUIRT_DP_FRAMES_LEFT) - 1);
  set_field(w, page, SQUIRT_DP_FRAMES_LEFT, left);
  return left == 0 ? SQUIRT_SPENT : SQUIRT_FLIES;
}

// ---------------------------------------------------------------------------
// The other seven stretches
// ---------------------------------------------------------------------------

static void lda(PortCpu* c, uint16_t v) {
  c->a = v;
  set_nz16(c, v);
}

// A word of one of the shot's tables.
static uint16_t table(const Rom* rom, uint32_t at) {
  return rom_word(rom, 0x810000u | (at & 0xffffu));
}

// `$80:AF2C` as a stretch meets it: the registers it leaves, carry for a
// tile that stops water, and N and Z from its `PLD`.
static bool stopped(Wram* w, PortCpu* c, uint16_t x, uint16_t y,
                    SquirtWork* k) {
  terrain_point_bit2(w, x, y, &k->ground);
  k->tested = true;
  k->overflow_known = false;
  c->a = k->ground.a;
  c->x = k->ground.x;
  c->y = k->ground.y;
  set_c(c, k->ground.blocked);
  set_nz16(c, c->d);
  return k->ground.blocked;
}

// What a call made here in C leaves on the stack: the return address the
// ROM's `JSR` or `JSL` at `at` pushed, under a stack pointer back where it
// was. The dress's two calls write over the return address it came in by,
// which is above anything the harness lets a port leave different.
static void called(Wram* w, PortCpu* c, uint16_t at, bool far) {
  const uint16_t s = c->s;
  if (far) push8(w, c, 0x81);
  push16(w, c, (uint16_t)(at + (far ? 3 : 2)));
  c->s = s;
}

// `$80:8475`, `thread_set_handler`, for the thread that is running. `at` is
// the `JSL`.
static void set_handler(Wram* w, PortCpu* c, uint16_t at, uint16_t addr,
                        uint16_t bank) {
  called(w, c, at, true);
  c->x = wram_r16(w, W_SCHED_CUR_TASK);
  wram_w16(w, (uint16_t)(W_THREAD_HANDLER + c->x), addr);
  wram_w16(w, (uint16_t)(W_THREAD_HANDLER_BANK + c->x), bank);
  c->y = bank;
  lda(c, bank);
}

// `$81:FDD7`: the picture for the way it faces, and that picture's flags.
// `at` is the `JSR`.
static void show(Wram* w, const Rom* rom, PortCpu* c, uint16_t at,
                 uint16_t pictures, SquirtWork* k) {
  const uint16_t record = field(w, c->d, SQUIRT_DP_RECORD);
  called(w, c, at, false);
  c->y = record;
  set_c(c, false);
  c->x = adc16(c, pictures, field(w, c->d, SQUIRT_DP_FACING_AT));
  k->overflow_known = true;
  wram_w16(w, (uint16_t)(record + ACTOR_META), table(rom, c->x + 2u));
  wram_w16(w, (uint16_t)(record + ACTOR_META_BANK), SQUIRT_PICTURE_BANK);
  lda(c, (uint16_t)(0x8000u | table(rom, c->x) |
                    wram_r16(w, (uint16_t)(record + ACTOR_FLAGS))));
  wram_w16(w, (uint16_t)(record + ACTOR_FLAGS), c->a);
  k->blocks[SQ_PICTURE]++;
}

static void yield(PortCpu* c, uint16_t ticks, uint32_t at, SquirtWork* k) {
  lda(c, ticks);
  k->blocks[SQ_TICKS]++;
  c->pc = at;
}

void squirt_launch(Wram* w, PortCpu* c, SquirtWork* k) {
  k->blocks[SQ_AIM]++;
  if (stopped(w, c, field(w, c->d, SQUIRT_DP_FROM_X),
              field(w, c->d, SQUIRT_DP_FROM_Y), k)) {
    PORT_COVER(squirt_unfired);
    k->blocks[SQ_UNFIRED]++;
    c->pc = SQUIRT_UNFIRED_RTL_PC;
    return;
  }
  PORT_COVER(squirt_launched);
  set_c(c, false);
  wram_w16(w, W_SQUIRTS_LIVE, adc16(c, wram_r16(w, W_SQUIRTS_LIVE), 1));
  k->overflow_known = true;
  // `JSR $FD5B`, which begins with the sound and comes back in `squirt_dress`.
  push16(w, c, (uint16_t)(0xfcc9 + 2));
  lda(c, SQUIRT_SFX);
  k->blocks[SQ_COUNT_UP]++;
  c->pc = SQUIRT_SFX_CALL_PC;
}

void squirt_dress(Wram* w, const Rom* rom, PortCpu* c, SquirtWork* k) {
  const uint16_t page = c->d;
  const uint16_t record = c->a;
  const uint16_t side = field(w, page, SQUIRT_DP_SIDE);
  PORT_COVER(squirt_dressed);
  set_field(w, page, SQUIRT_DP_RECORD, record);
  set_field(w, page, SQUIRT_DP_SIDE_KEPT, side);
  wram_w16(w, (uint16_t)(record + ACTOR_Z),
           (uint16_t)(table(rom, SQUIRT_HEIGHTS + side) - 2));
  wram_w16(w, (uint16_t)(record + ACTOR_META), SQUIRT_PICTURE);
  wram_w16(w, (uint16_t)(record + ACTOR_META_BANK), SQUIRT_PICTURE_BANK);
  wram_w16(w, (uint16_t)(record + ACTOR_THREAD), wram_r16(w, W_SCHED_CUR_TASK));
  wram_w16(w, (uint16_t)(record + ACTOR_COLLIDE_ID),
           table(rom, SQUIRT_COLLIDE_IDS + side));
  wram_w16(w, (uint16_t)(record + ACTOR_FLAGS),
           (uint16_t)(0x8000u | wram_r16(w, (uint16_t)(record + ACTOR_FLAGS))));

  // The step it will fly by.
  const uint16_t facing = field(w, page, SQUIRT_DP_FACING);
  const uint16_t at = (uint16_t)(facing * 2);
  set_field(w, page, SQUIRT_DP_FACING_KEPT, facing);
  set_field(w, page, SQUIRT_DP_FACING_AT, at);
  set_field(w, page, SQUIRT_DP_STEP_X, table(rom, SQUIRT_STEPS + at));
  set_field(w, page, SQUIRT_DP_STEP_Y, table(rom, SQUIRT_STEPS + at + 2u));

  // It starts at the muzzle, two pixels above where it was fired from.
  const uint16_t muzzle = (uint16_t)(table(rom, SQUIRT_MUZZLES + side) + at);
  const uint16_t x = (uint16_t)(field(w, page, SQUIRT_DP_FROM_X) +
                                table(rom, muzzle));
  const uint16_t y = (uint16_t)(field(w, page, SQUIRT_DP_FROM_Y) - 2 +
                                table(rom, muzzle + 2u));
  set_field(w, page, SQUIRT_DP_X, x);
  wram_w16(w, (uint16_t)(record + ACTOR_X), x);
  set_field(w, page, SQUIRT_DP_Y, y);
  wram_w16(w, (uint16_t)(record + ACTOR_Y), y);
  set_field(w, page, SQUIRT_DP_FRAMES_LEFT, SQUIRT_FRAMES);
  c->s = (uint16_t)(c->s + 2);  // the `RTS`
  k->blocks[SQ_DRESS]++;

  // $FCCC: what it hits is told to `$81:FE0E`.
  set_handler(w, c, 0xfcd2, 0xfe0e, 0x0081);
  k->blocks[SQ_HANDLER]++;
  show(w, rom, c, 0xfcd9, SQUIRT_PICTURES_LAUNCH, k);
  yield(c, SQUIRT_YIELD_TICKS, SQUIRT_LAUNCH_YIELD_PC, k);
}

// A step and the tile test. True when it flies on.
static bool flies_on(Wram* w, PortCpu* c, SquirtWork* k) {
  uint16_t x, y;
  step(w, c->d, &x, &y);
  k->blocks[SQ_MOVE_TEST]++;
  if (stopped(w, c, x, y, k)) {
    k->blocks[SQ_TAKEN]++;
    c->pc = SQUIRT_END_PC;
    return false;
  }
  return true;
}

void squirt_first_frame(Wram* w, PortCpu* c, SquirtWork* k) {
  if (!flies_on(w, c, k)) {
    PORT_COVER(squirt_first_stopped);
    return;
  }
  PORT_COVER(squirt_first_flew);
  yield(c, SQUIRT_YIELD_TICKS, SQUIRT_FIRST_YIELD_PC, k);
}

void squirt_second_frame(Wram* w, const Rom* rom, PortCpu* c, SquirtWork* k) {
  if (!flies_on(w, c, k)) {
    PORT_COVER(squirt_second_stopped);
    return;
  }
  PORT_COVER(squirt_second_flew);
  show(w, rom, c, 0xfd07, SQUIRT_PICTURES_FLIGHT, k);
  yield(c, SQUIRT_YIELD_TICKS, SQUIRT_YIELD_PC, k);
}

void squirt_splash(Wram* w, PortCpu* c, SquirtWork* k) {
  PORT_COVER(squirt_splashed);
  k->overflow_known = true;  // nothing here writes it
  set_handler(w, c, 0xfd26, 0, 0);
  c->y = field(w, c->d, SQUIRT_DP_RECORD);
  wram_w16(w, (uint16_t)(c->y + ACTOR_META), SQUIRT_SPLASH_PICTURE);
  lda(c, SQUIRT_SPLASH_TICKS);
  k->blocks[SQ_SPLASH]++;
  c->pc = SQUIRT_SPLASH_YIELD_PC;
}

void squirt_splash_2(Wram* w, PortCpu* c, SquirtWork* k) {
  PORT_COVER(squirt_splashed_2);
  k->overflow_known = true;
  c->y = field(w, c->d, SQUIRT_DP_RECORD);
  wram_w16(w, (uint16_t)(c->y + ACTOR_META), SQUIRT_SPLASH_2_PICTURE);
  lda(c, SQUIRT_SPLASH_2_TICKS);
  k->blocks[SQ_SPLASH_2]++;
  c->pc = SQUIRT_SPLASH_2_YIELD_PC;
}

void squirt_gone(Wram* w, PortCpu* c, SquirtWork* k) {
  PORT_COVER(squirt_gone);
  set_c(c, true);
  wram_w16(w, W_SQUIRTS_LIVE, sbc16(c, wram_r16(w, W_SQUIRTS_LIVE), 1));
  k->overflow_known = true;
  lda(c, field(w, c->d, SQUIRT_DP_RECORD));
  k->blocks[SQ_GONE]++;
  c->pc = SQUIRT_FREE_JML_PC;
}
