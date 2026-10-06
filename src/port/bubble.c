// $81:F380  the bubble gun's bubble -- see port/bubble.h.

#include "port/bubble.h"

#include "port/coverage.h"
#include "port/oam.h"  // the display record's fields
#include "port/squirt.h"  // W_SQUIRTS_LIVE, the budget it shares

static uint16_t field(const Wram* w, const PortCpu* c, uint16_t at) {
  return wram_r16(w, (uint16_t)(c->d + at));
}

static void set_field(Wram* w, const PortCpu* c, uint16_t at, uint16_t v) {
  wram_w16(w, (uint16_t)(c->d + at), v);
}

static void lda(PortCpu* c, uint16_t v) {
  c->a = v;
  set_nz16(c, v);
}

// A word of one of its tables, read through the data bank.
static uint16_t table(const Wram* w, const Rom* rom, uint32_t at) {
  return bus_r16(w, rom, ((uint32_t)BUBBLE_BANK << 16) + at);
}

// `$80:AF2C` as a stretch meets it: the registers it leaves, carry for a
// tile that stops the bubble, and N and Z from its `PLD`.
static bool stopped(Wram* w, PortCpu* c, uint16_t x, uint16_t y,
                    BubbleWork* k) {
  TerrainRegs r;
  terrain_point_bit2(w, x, y, &r);
  if (k->tests < BUBBLE_MAX_TESTS) k->ground[k->tests] = r;
  k->tests++;
  k->overflow_known = false;
  c->a = r.a;
  c->x = r.x;
  c->y = r.y;
  set_c(c, r.blocked);
  set_nz16(c, c->d);
  k->blocks[BUB_TEST]++;
  if (r.blocked) k->blocks[BUB_TAKEN]++;
  return r.blocked;
}

static bool stopped_here(Wram* w, PortCpu* c, BubbleWork* k) {
  return stopped(w, c, field(w, c, BUBBLE_DP_X), field(w, c, BUBBLE_DP_Y), k);
}

static void yield(PortCpu* c, uint32_t at, BubbleWork* k) {
  lda(c, BUBBLE_YIELD_TICKS);
  k->blocks[BUB_TICKS]++;
  c->pc = at;
}

// `$81:F417`: on to the three pictures of the burst. See the header for what
// the two stores do.
static void burst(Wram* w, PortCpu* c, BubbleWork* k) {
  c->y = field(w, c, BUBBLE_DP_RECORD);
  wram_w16(w, (uint16_t)(c->y + ACTOR_META_BANK), BUBBLE_WATER_PICTURE);
  lda(c, BUBBLE_BURST_PICTURES);
  k->blocks[BUB_BURST]++;
  c->pc = BUBBLE_BURST_CALL_PC;
}

// `$81:F49B`: a step the way it faces.
static void step(Wram* w, const Rom* rom, PortCpu* c, BubbleWork* k) {
  const uint16_t at = (uint16_t)((field(w, c, BUBBLE_DP_FACING) - 2) << 1);
  set_c(c, false);
  set_field(w, c, BUBBLE_DP_X,
            adc16(c, table(w, rom, BUBBLE_STEPS + at),
                  field(w, c, BUBBLE_DP_X)));
  set_c(c, false);
  set_field(w, c, BUBBLE_DP_Y,
            adc16(c, table(w, rom, BUBBLE_STEPS + at + 2u),
                  field(w, c, BUBBLE_DP_Y)));
  k->overflow_known = true;
  k->blocks[BUB_MOVE]++;
}

// The record follows the bubble, and a frame is counted off. True when that
// was the last.
static bool placed_last(Wram* w, PortCpu* c, BubbleWork* k) {
  c->y = field(w, c, BUBBLE_DP_RECORD);
  wram_w16(w, (uint16_t)(c->y + ACTOR_X), field(w, c, BUBBLE_DP_X));
  wram_w16(w, (uint16_t)(c->y + ACTOR_Y), field(w, c, BUBBLE_DP_Y));
  const uint16_t left = (uint16_t)(field(w, c, BUBBLE_DP_FRAMES_LEFT) - 1);
  set_field(w, c, BUBBLE_DP_FRAMES_LEFT, left);
  k->blocks[BUB_PLACE]++;
  if (left != 0) k->blocks[BUB_TAKEN]++;
  return left == 0;
}

void bubble_launch(Wram* w, PortCpu* c, BubbleWork* k) {
  if (stopped(w, c, field(w, c, BUBBLE_DP_FROM_X),
              field(w, c, BUBBLE_DP_FROM_Y), k)) {
    // The branch is a `BCC` here, so it is the one not taken that costs
    // nothing more.
    PORT_COVER(bubble_unfired);
    k->blocks[BUB_TAKEN]--;
    c->pc = BUBBLE_UNFIRED_RTL_PC;
    return;
  }
  PORT_COVER(bubble_launched);
  k->blocks[BUB_TAKEN]++;
  set_c(c, false);
  lda(c, adc16(c, wram_r16(w, W_SQUIRTS_LIVE), BUBBLE_BUDGET));
  wram_w16(w, W_SQUIRTS_LIVE, c->a);
  k->overflow_known = true;
  // `JSR $F43E`, which begins with the call for a record and comes back in
  // `bubble_dress`.
  push16(w, c, 0xf397);
  k->blocks[BUB_COUNT_UP]++;
  c->pc = BUBBLE_ALLOC_CALL_PC;
}

void bubble_dress(Wram* w, const Rom* rom, PortCpu* c, BubbleWork* k) {
  const uint16_t record = c->a;
  set_field(w, c, BUBBLE_DP_RECORD, record);
  set_field(w, c, BUBBLE_DP_RECORD_08, record);
  k->blocks[BUB_DRESS_HEAD]++;

  // A player's side picks its own table of muzzles. Anyone else's bubble
  // leaves from where the first player's would.
  uint16_t side = field(w, c, BUBBLE_DP_SIDE);
  if (!(side & 0x8000u)) {
    PORT_COVER(bubble_of_player);
    k->blocks[BUB_TAKEN]++;
  } else {
    PORT_COVER(bubble_of_other);
    set_field(w, c, BUBBLE_DP_SIDE, BUBBLE_SIDE_OTHER);
    side = 0;
    k->blocks[BUB_DRESS_OTHER]++;
  }
  set_c(c, false);
  const uint16_t muzzle =
      adc16(c, (uint16_t)(field(w, c, BUBBLE_DP_FACING) << 1),
            table(w, rom, BUBBLE_MUZZLES + side));
  set_c(c, false);
  const uint16_t x =
      adc16(c, table(w, rom, muzzle), field(w, c, BUBBLE_DP_FROM_X));
  set_field(w, c, BUBBLE_DP_X, x);
  wram_w16(w, (uint16_t)(record + ACTOR_X), x);
  set_field(w, c, BUBBLE_DP_HEIGHT, BUBBLE_HEIGHT);
  wram_w16(w, (uint16_t)(record + ACTOR_Z), BUBBLE_HEIGHT);
  set_c(c, false);
  const uint16_t y =
      adc16(c, table(w, rom, muzzle + 2u), field(w, c, BUBBLE_DP_FROM_Y));
  set_field(w, c, BUBBLE_DP_Y, y);
  wram_w16(w, (uint16_t)(record + ACTOR_Y), y);
  k->overflow_known = true;

  wram_w16(w, (uint16_t)(record + ACTOR_META), BUBBLE_WATER_PICTURE);
  wram_w16(w, (uint16_t)(record + ACTOR_META_BANK), BUBBLE_PICTURE_BANK);
  wram_w16(w, (uint16_t)(record + ACTOR_THREAD), wram_r16(w, W_SCHED_CUR_TASK));
  c->x = field(w, c, BUBBLE_DP_SIDE);
  wram_w16(w, (uint16_t)(record + ACTOR_COLLIDE_ID),
           table(w, rom, BUBBLE_COLLIDE_IDS + c->x));
  const uint16_t flags = wram_r16(w, (uint16_t)(record + ACTOR_FLAGS));
  wram_w16(w, (uint16_t)(record + ACTOR_FLAGS), (uint16_t)(ACTOR_DRAW | flags));
  c->y = record;
  c->s = (uint16_t)(c->s + 2);  // the `RTS`, back to the launch
  k->blocks[BUB_DRESS]++;

  lda(c, BUBBLE_SFX);
  k->blocks[BUB_SFX]++;
  c->pc = BUBBLE_SFX_CALL_PC;
}

void bubble_first(Wram* w, PortCpu* c, BubbleWork* k) {
  set_field(w, c, BUBBLE_DP_FRAMES_LEFT, BUBBLE_RISING_FRAMES);
  k->blocks[BUB_FIRST]++;
  if (stopped_here(w, c, k)) {
    PORT_COVER(bubble_first_stopped);
    burst(w, c, k);
    return;
  }
  PORT_COVER(bubble_first_rose);
  yield(c, BUBBLE_RISING_YIELD_PC, k);
}

void bubble_rising(Wram* w, const Rom* rom, PortCpu* c, BubbleWork* k) {
  step(w, rom, c, k);
  if (stopped_here(w, c, k)) {
    PORT_COVER(bubble_rising_stopped);
    burst(w, c, k);
    return;
  }
  if (!placed_last(w, c, k)) {
    // The loop's head asks about the same place again.
    if (stopped_here(w, c, k)) {
      burst(w, c, k);
      return;
    }
    PORT_COVER(bubble_rose);
    yield(c, BUBBLE_RISING_YIELD_PC, k);
    return;
  }
  // The fourth frame: a bubble from here, which can be hit.
  PORT_COVER(bubble_formed);
  wram_w16(w, (uint16_t)(c->y + ACTOR_META), BUBBLE_PICTURE);
  wram_w16(w, (uint16_t)(c->y + ACTOR_META_BANK), BUBBLE_PICTURE_BANK);
  set_field(w, c, BUBBLE_DP_FRAMES_LEFT, BUBBLE_FLYING_FRAMES);
  set_field(w, c, BUBBLE_DP_HIT, 0);
  c->x = wram_r16(w, W_SCHED_CUR_TASK);
  wram_w16(w, (uint16_t)(W_THREAD_HANDLER + c->x), BUBBLE_HANDLER);
  wram_w16(w, (uint16_t)(W_THREAD_HANDLER_BANK + c->x), BUBBLE_BANK);
  c->y = BUBBLE_BANK;
  k->blocks[BUB_FLOAT]++;
  yield(c, BUBBLE_FLYING_YIELD_PC, k);
}

// One bubble fewer, and its record on the way to being freed.
static void end(Wram* w, PortCpu* c, BubbleWork* k) {
  set_c(c, true);
  wram_w16(w, W_SQUIRTS_LIVE,
           sbc16(c, wram_r16(w, W_SQUIRTS_LIVE), BUBBLE_BUDGET));
  k->overflow_known = true;
  lda(c, field(w, c, BUBBLE_DP_RECORD));
  k->blocks[BUB_END]++;
  c->pc = BUBBLE_FREE_JML_PC;
}

void bubble_flying(Wram* w, const Rom* rom, PortCpu* c, BubbleWork* k) {
  k->overflow_known = true;  // nothing has written it yet
  k->blocks[BUB_HIT_TEST]++;
  if (field(w, c, BUBBLE_DP_HIT) != 0) {
    PORT_COVER(bubble_hit);
    k->blocks[BUB_TAKEN]++;
    end(w, c, k);
    return;
  }
  step(w, rom, c, k);
  if (stopped_here(w, c, k)) {
    PORT_COVER(bubble_stopped);
    burst(w, c, k);
    return;
  }
  if (placed_last(w, c, k)) {
    PORT_COVER(bubble_spent);
    burst(w, c, k);
    return;
  }
  PORT_COVER(bubble_flew);
  yield(c, BUBBLE_FLYING_YIELD_PC, k);
}

void bubble_gone(Wram* w, PortCpu* c, BubbleWork* k) {
  PORT_COVER(bubble_gone);
  end(w, c, k);
}
