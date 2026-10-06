// $81:B4EA  the axe an evil doll throws -- see port/axe.h.

#include "port/axe.h"

#include "port/coverage.h"
#include "port/oam.h"  // the display record's fields
#include "port/squirt.h"  // W_SQUIRTS_LIVE, the budget the axe shares

static uint16_t field(const Wram* w, uint16_t page, uint16_t at) {
  return wram_r16(w, (uint16_t)(page + at));
}

static void set_field(Wram* w, uint16_t page, uint16_t at, uint16_t v) {
  wram_w16(w, (uint16_t)(page + at), v);
}

static void lda(PortCpu* c, uint16_t v) {
  c->a = v;
  set_nz16(c, v);
}

// A word of one of the tables, read through the data bank.
static uint16_t table(const Wram* w, const Rom* rom, uint32_t at) {
  return bus_r16(w, rom, ((uint32_t)AXE_BANK << 16) + at);
}

void axe_launch(Wram* w, PortCpu* c, AxeWork* k) {
  PORT_COVER(axe_launched);
  set_c(c, false);
  lda(c, adc16(c, wram_r16(w, W_SQUIRTS_LIVE), AXE_BUDGET));
  wram_w16(w, W_SQUIRTS_LIVE, c->a);
  // `JSR $B523`, which begins with the call for a record and comes back in
  // `axe_dress`.
  push16(w, c, 0xb4f6);
  k->blocks[AX_LAUNCH]++;
  c->pc = AXE_ALLOC_CALL_PC;
}

// `$81:B630`: which set of four pictures, from the way it goes. Each step is
// compared with -1 for the carry that would tell one way from the other. See
// the header for why it never does.
static void choose_set(Wram* w, const Rom* rom, PortCpu* c, AxeWork* k) {
  const uint16_t page = c->d;
  uint16_t way = 0;
  k->blocks[AX_SET_HEAD]++;
  if (field(w, page, AXE_DP_STEP_X) == 0) {
    k->blocks[AX_TAKEN]++;
  } else {
    PORT_COVER(axe_goes_across);
    cmp16(c, field(w, page, AXE_DP_STEP_X), 0xffff);
    way = adc16(c, way, 1);
    way = asl16(c, way);
    way = asl16(c, way);
    k->blocks[AX_SET_X]++;
  }
  k->blocks[AX_SET_MID]++;
  if (field(w, page, AXE_DP_STEP_Y) == 0) {
    k->blocks[AX_TAKEN]++;
  } else {
    PORT_COVER(axe_goes_down);
    cmp16(c, field(w, page, AXE_DP_STEP_Y), 0xffff);
    way = adc16(c, way, 2);
    k->blocks[AX_SET_Y]++;
  }
  c->x = way;
  uint16_t set = (uint16_t)(table(w, rom, AXE_SETS + way) & 0x00ffu);
  set = asl16(c, set);
  set = asl16(c, set);
  set_field(w, page, AXE_DP_SET, set);
  k->blocks[AX_SET_TAIL]++;
}

void axe_dress(Wram* w, const Rom* rom, PortCpu* c, AxeWork* k) {
  const uint16_t page = c->d;
  const uint16_t record = c->a;
  PORT_COVER(axe_dressed);
  set_field(w, page, AXE_DP_RECORD, record);

  const uint16_t x = field(w, page, AXE_DP_FROM_X);
  const uint16_t y = field(w, page, AXE_DP_FROM_Y);
  set_field(w, page, AXE_DP_X, x);
  wram_w16(w, (uint16_t)(record + ACTOR_X), x);
  wram_w16(w, (uint16_t)(record + ACTOR_Z), 0);
  set_field(w, page, AXE_DP_Y, y);
  wram_w16(w, (uint16_t)(record + ACTOR_Y), y);
  set_field(w, page, AXE_DP_STEP_X, asl16(c, field(w, page, AXE_DP_WAY_X)));
  set_field(w, page, AXE_DP_STEP_Y, asl16(c, field(w, page, AXE_DP_WAY_Y)));

  wram_w16(w, (uint16_t)(record + ACTOR_META), AXE_PICTURE);
  wram_w16(w, (uint16_t)(record + ACTOR_META_BANK), AXE_PICTURE_BANK);
  wram_w16(w, (uint16_t)(record + ACTOR_THREAD), wram_r16(w, W_SCHED_CUR_TASK));
  wram_w16(w, (uint16_t)(record + ACTOR_COLLIDE_ID), AXE_COLLIDE_ID);
  wram_w16(w, (uint16_t)(record + ACTOR_FLAGS),
           (uint16_t)(0x8000u | wram_r16(w, (uint16_t)(record + ACTOR_FLAGS))));

  set_field(w, page, AXE_DP_HITS_LEFT, AXE_HITS);
  set_field(w, page, AXE_DP_TURN_EVERY, AXE_TURN_EVERY);
  set_field(w, page, AXE_DP_TURN_WAIT, 0);
  set_field(w, page, AXE_DP_LEAVE, 0);
  set_field(w, page, AXE_DP_TURN, 0);
  set_field(w, page, AXE_DP_FRAMES, AXE_FRAMES);
  k->blocks[AX_DRESS]++;
  choose_set(w, rom, c, k);

  // What it hits is told to `$81:B592`.
  c->x = wram_r16(w, W_SCHED_CUR_TASK);
  wram_w16(w, (uint16_t)(W_THREAD_HANDLER + c->x), AXE_HANDLER);
  wram_w16(w, (uint16_t)(W_THREAD_HANDLER_BANK + c->x), AXE_BANK);
  c->y = AXE_BANK;
  k->blocks[AX_HANDLER]++;

  set_field(w, page, AXE_DP_STATE, AXE_STATE_FLY);
  c->s = (uint16_t)(c->s + 2);  // the `RTS`, back to the launch
  k->blocks[AX_DRESS_TAIL]++;

  set_field(w, page, AXE_DP_HIT_ID, 0);
  lda(c, AXE_YIELD_TICKS);
  k->blocks[AX_AGAIN]++;
  c->pc = AXE_YIELD_PC;
}

// `$81:B5B0`, the one state body: turn, step, and ask whether it may be
// there.
static void fly(Wram* w, PortCpu* c, AxeWork* k) {
  const uint16_t page = c->d;
  k->blocks[AX_FLY_HEAD]++;
  if (field(w, page, AXE_DP_TURN_WAIT) != 0) {
    k->blocks[AX_TAKEN]++;
  } else {
    const uint16_t turn = (uint16_t)(field(w, page, AXE_DP_TURN) + 1);
    set_field(w, page, AXE_DP_TURN, turn);
    k->blocks[AX_TURN]++;
    if (turn != AXE_TURNS) {
      PORT_COVER(axe_turned);
      k->blocks[AX_TAKEN]++;
    } else {
      PORT_COVER(axe_turned_round);
      set_field(w, page, AXE_DP_TURN, 0);
      k->blocks[AX_TURN_WRAP]++;
    }
  }

  // Twice the step, and the second sum takes the first's carry: four pixels
  // right or down, but three left or up.
  set_c(c, false);
  const uint16_t step_x = field(w, page, AXE_DP_STEP_X);
  const uint16_t step_y = field(w, page, AXE_DP_STEP_Y);
  uint16_t x = adc16(c, field(w, page, AXE_DP_X), step_x);
  x = adc16(c, x, step_x);
  set_field(w, page, AXE_DP_X, x);
  set_c(c, false);
  uint16_t y = adc16(c, field(w, page, AXE_DP_Y), step_y);
  y = adc16(c, y, step_y);
  set_field(w, page, AXE_DP_Y, y);
  k->blocks[AX_MOVE]++;

  terrain_point_bit2(w, x, y, &k->ground);
  k->tested = true;
  bool stopped = k->ground.blocked;
  if (stopped) {
    PORT_COVER(axe_stopped_ground);
    k->blocks[AX_TAKEN]++;
  } else {
    step_tether_blocked(w, x, y, &k->leash);
    k->leashed = true;
    k->blocks[AX_LEASH]++;
    stopped = k->leash.blocked;
    if (stopped) {
      PORT_COVER(axe_stopped_leash);
      k->blocks[AX_TAKEN]++;
    } else {
      PORT_COVER(axe_flew);
      k->blocks[AX_INC]++;
    }
  }
  // `INC $0A` for an axe that flies on, and `DEC $0A` for every axe.
  if (stopped)
    set_field(w, page, AXE_DP_LEAVE,
              (uint16_t)(field(w, page, AXE_DP_LEAVE) - 1));
  k->blocks[AX_FLY_TAIL]++;
}

// `$81:B5EA`: the record follows the axe, and shows the picture its turn and
// its set name.
static void show(Wram* w, const Rom* rom, PortCpu* c, AxeWork* k) {
  const uint16_t page = c->d;
  const uint16_t record = field(w, page, AXE_DP_RECORD);
  const uint16_t wait = (uint16_t)(field(w, page, AXE_DP_TURN_WAIT) - 1);
  set_field(w, page, AXE_DP_TURN_WAIT, wait);
  k->blocks[AX_SHOW_HEAD]++;
  if (!(wait & 0x8000u)) {
    k->blocks[AX_TAKEN]++;
  } else {
    set_field(w, page, AXE_DP_TURN_WAIT, field(w, page, AXE_DP_TURN_EVERY));
    k->blocks[AX_SHOW_RESET]++;
  }
  wram_w16(w, (uint16_t)(record + ACTOR_X), field(w, page, AXE_DP_X));
  wram_w16(w, (uint16_t)(record + ACTOR_Y), field(w, page, AXE_DP_Y));

  set_c(c, false);
  uint16_t at =
      adc16(c, field(w, page, AXE_DP_TURN), field(w, page, AXE_DP_SET));
  at = asl16(c, at);
  at = asl16(c, at);
  const uint32_t frames = field(w, page, AXE_DP_FRAMES);
  const uint16_t mask = table(w, rom, frames + at);
  const uint16_t flags = wram_r16(w, (uint16_t)(record + ACTOR_FLAGS));
  k->blocks[AX_SHOW_MID]++;
  k->blocks[AX_TAKEN]++;  // the `BMI`, or the `BRA` after the `ORA`
  if (mask & 0x8000u) {
    PORT_COVER(axe_shown_masked);
    wram_w16(w, (uint16_t)(record + ACTOR_FLAGS), (uint16_t)(flags & mask));
    k->blocks[AX_SHOW_AND]++;
  } else {
    PORT_COVER(axe_shown_ored);
    wram_w16(w, (uint16_t)(record + ACTOR_FLAGS), (uint16_t)(flags | mask));
    k->blocks[AX_SHOW_ORA]++;
  }
  c->x = record;
  c->y = asl16(c, table(w, rom, frames + at + 2u));
  wram_w16(w, (uint16_t)(record + ACTOR_META),
           table(w, rom, AXE_PICTURES + c->y));
  wram_w16(w, (uint16_t)(record + ACTOR_META_BANK), AXE_PICTURE_BANK);
  k->blocks[AX_SHOW_TAIL]++;
}

void axe_frame(Wram* w, const Rom* rom, PortCpu* c, AxeWork* k) {
  const uint16_t page = c->d;
  // `PEA $B507 : LDA $0E : DEC : PHA : RTS`, and the state body's own `RTS`.
  const uint16_t s = c->s;
  push16(w, c, 0xb507);
  push16(w, c, (uint16_t)(field(w, page, AXE_DP_STATE) - 1));
  c->s = s;
  k->blocks[AX_DISPATCH]++;

  fly(w, c, k);
  show(w, rom, c, k);
  k->blocks[AX_LOOP]++;

  const uint16_t leave = field(w, page, AXE_DP_LEAVE);
  if (leave == 0) {
    k->blocks[AX_TAKEN]++;
    set_field(w, page, AXE_DP_HIT_ID, 0);
    lda(c, AXE_YIELD_TICKS);
    k->blocks[AX_AGAIN]++;
    c->pc = AXE_YIELD_PC;
    return;
  }
  // The handler counts `$0A` up on the hit that spends it; a tile or the
  // leash counts it down. A `BMI` to the next instruction tells them apart
  // and does nothing about it.
  if (leave & 0x8000u) {
    PORT_COVER(axe_ended_stopped);
    k->blocks[AX_TAKEN]++;
  } else {
    PORT_COVER(axe_ended_hit);
  }
  set_c(c, true);
  wram_w16(w, W_SQUIRTS_LIVE,
           sbc16(c, wram_r16(w, W_SQUIRTS_LIVE), AXE_BUDGET));
  lda(c, field(w, page, AXE_DP_RECORD));
  k->blocks[AX_LEAVE]++;
  c->pc = AXE_FREE_JML_PC;
}
