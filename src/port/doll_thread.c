// $81:B2B0  an evil doll's thread, round its loop -- see port/doll.h.

#include "port/doll.h"

#include "port/coverage.h"
#include "port/rng.h"
#include "port/squirt.h"  // W_SQUIRTS_LIVE, the budget it shares
#include "port/thread.h"

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

// What a call made here in C leaves on the stack: the return address the
// ROM's `JSR` or `JSL` at `at` pushed, under a stack pointer back where it
// was.
static void called(Wram* w, PortCpu* c, uint16_t at, bool far) {
  const uint16_t s = c->s;
  if (far) push8(w, c, DOLL_BANK);
  push16(w, c, (uint16_t)(at + (far ? 3 : 2)));
  c->s = s;
}

// `STZ $5A : LDA #$0001`, and the yield.
static void again(Wram* w, PortCpu* c, DollThreadWork* k) {
  set_field(w, c, DOLL_DP_HIT_ID, 0);
  lda(c, DOLL_YIELD_TICKS);
  k->blocks[DT_AGAIN]++;
  c->pc = DOLL_FRAME_YIELD_PC;
}

void doll_launch(Wram* w, PortCpu* c, DollThreadWork* k) {
  PORT_COVER(doll_launched);
  set_c(c, false);
  lda(c, adc16(c, wram_r16(w, W_SQUIRTS_LIVE), DOLL_BUDGET));
  wram_w16(w, W_SQUIRTS_LIVE, c->a);
  // `JSR $B393`, which begins with the call for a record.
  push16(w, c, 0xb2bc);
  k->blocks[DT_LAUNCH]++;
  c->pc = DOLL_ALLOC_CALL_PC;
}

void doll_first_record(Wram* w, PortCpu* c, DollThreadWork* k) {
  set_field(w, c, DOLL_DP_RECORD, c->a);
  k->blocks[DT_FIRST]++;
  c->pc = DOLL_ALLOC_2_CALL_PC;
}

// `$80:AA0D`, which nothing else calls: is the point on the screen? Within
// 256 across and 240 down of the camera, its near edges excluded. Carry is
// the answer and overflow is whatever its last sum left.
static bool on_screen(Wram* w, PortCpu* c, uint16_t x, uint16_t y,
                      DollThreadWork* k) {
  const uint16_t at[2] = {x, y};
  const uint16_t camera[2] = {wram_r16(w, W_CAMERA_X), wram_r16(w, W_CAMERA_Y)};
  const uint16_t span[2] = {0x0100, 0x00f0};
  for (int i = 0; i < 2; i++) {
    wram_w16(w, DOLL_SCREEN_SCRATCH, at[i]);
    k->blocks[DT_SCREEN_LOW]++;
    if (camera[i] >= at[i]) {
      k->blocks[DT_TAKEN]++;
      k->blocks[DT_SCREEN_END]++;
      set_c(c, false);
      return false;
    }
    set_c(c, false);
    const uint16_t far_edge = adc16(c, camera[i], span[i]);
    k->blocks[DT_SCREEN_HIGH]++;
    if (far_edge < at[i]) {
      k->blocks[DT_TAKEN]++;
      k->blocks[DT_SCREEN_END]++;
      set_c(c, false);
      return false;
    }
  }
  k->blocks[DT_SCREEN_END]++;
  set_c(c, true);
  return true;
}

void doll_dress(Wram* w, PortCpu* c, DollThreadWork* k) {
  const uint16_t axe = c->a;
  const uint16_t record = field(w, c, DOLL_DP_RECORD);
  const uint16_t task = wram_r16(w, W_SCHED_CUR_TASK);
  PORT_COVER(doll_dressed);
  set_field(w, c, DOLL_DP_AXE, axe);

  // Both records where it was spawned. Only the doll's has a picture yet.
  const uint16_t x = field(w, c, DOLL_DP_AXE_X);
  const uint16_t y = field(w, c, DOLL_DP_AXE_Y);
  const uint16_t both[2] = {record, axe};
  set_field(w, c, DOLL_DP_X, x);
  set_field(w, c, DOLL_DP_Y, y);
  for (int i = 0; i < 2; i++) {
    wram_w16(w, (uint16_t)(both[i] + ACTOR_X), x);
    wram_w16(w, (uint16_t)(both[i] + ACTOR_Z), 0);
    wram_w16(w, (uint16_t)(both[i] + ACTOR_Y), y);
    wram_w16(w, (uint16_t)(both[i] + ACTOR_THREAD), task);
    wram_w16(w, (uint16_t)(both[i] + ACTOR_COLLIDE_ID), DOLL_COLLIDE_ID);
  }
  wram_w16(w, (uint16_t)(record + ACTOR_META), DOLL_FIRST_PICTURE);
  wram_w16(w, (uint16_t)(record + ACTOR_META_BANK), DOLL_THREAD_PICTURE_BANK);
  wram_w16(w, (uint16_t)(axe + ACTOR_META), 0);
  wram_w16(w, (uint16_t)(axe + ACTOR_META_BANK), 0);
  wram_w16(w, (uint16_t)(record + ACTOR_ATTR), DOLL_ATTR);

  // No handler until it lands: a doll in the air cannot be hit.
  called(w, c, 0xb3f3, true);
  wram_w16(w, (uint16_t)(W_THREAD_HANDLER + task), 0);
  wram_w16(w, (uint16_t)(W_THREAD_HANDLER_BANK + task), 0);
  k->blocks[DT_DRESS_A]++;

  set_field(w, c, DOLL_DP_HEALTH, DOLL_HEALTH);
  set_field(w, c, DOLL_DP_STEP_EVERY, 1);
  set_field(w, c, DOLL_DP_STEP_WAIT, 0);
  set_field(w, c, DOLL_DP_LEAVE, 0);
  set_field(w, c, DOLL_DP_HIT, 0);
  set_field(w, c, DOLL_DP_HIT_ID, 0);
  set_field(w, c, 0x7e, 0);
  set_field(w, c, DOLL_DP_FACING, 0);
  const uint16_t flags = wram_r16(w, (uint16_t)(record + ACTOR_FLAGS));
  wram_w16(w, (uint16_t)(record + ACTOR_FLAGS), (uint16_t)(ACTOR_DRAW | flags));
  called(w, c, 0xb418, false);
  set_field(w, c, DOLL_DP_STATE, DOLL_STATE_OPENING);
  set_field(w, c, DOLL_DP_FRAMES, DOLL_FRAMES_OPENING);
  k->blocks[DT_DRESS_B]++;
  c->s = (uint16_t)(c->s + 2);  // the `RTS`, back to the launch

  // Its arrival is heard, if it is on the screen and `$1F52` is 3.
  c->x = x;
  c->y = y;
  called(w, c, 0xb2c1, true);
  k->blocks[DT_SEEN_CALL]++;
  const bool seen = on_screen(w, c, x, y, k);
  k->blocks[DT_SEEN_BCC]++;
  if (!seen) {
    PORT_COVER(doll_arrived_unseen);
    k->blocks[DT_TAKEN]++;
    again(w, c, k);
    return;
  }
  cmp16(c, wram_r16(w, W_DOLL_HEARD), DOLL_HEARD);
  k->blocks[DT_SEEN_KIND]++;
  if (wram_r16(w, W_DOLL_HEARD) != DOLL_HEARD) {
    PORT_COVER(doll_arrived_unheard);
    k->blocks[DT_TAKEN]++;
    again(w, c, k);
    return;
  }
  PORT_COVER(doll_arrived_heard);
  lda(c, DOLL_ARRIVAL_SFX);
  k->blocks[DT_SEEN_SFX]++;
  c->pc = DOLL_SFX_CALL_PC;
}

void doll_again(Wram* w, PortCpu* c, DollThreadWork* k) {
  again(w, c, k);
}

void doll_opening(Wram* w, PortCpu* c, DollThreadWork* k) {
  PORT_COVER(doll_opening);
  // `PEA $B2E6 : LDA $0E : DEC : PHA : RTS`, into the state body, which plays
  // a list of pictures and comes back in `doll_opened`.
  push16(w, c, 0xb2e6);
  const uint16_t s = c->s;
  push16(w, c, (uint16_t)(DOLL_STATE_OPENING - 1));
  c->s = s;
  lda(c, DOLL_OPENING_PICTURES);
  k->blocks[DT_OPENING]++;
  c->pc = DOLL_PICTURES_CALL_PC;
}

void doll_opened(Wram* w, PortCpu* c, DollThreadWork* k) {
  PORT_COVER(doll_opened);
  set_field(w, c, DOLL_DP_STATE, DOLL_STATE_LEAP_OUT);
  // 40 pixels below, and one more when the list's player left carry set: the
  // sum has no `CLC`.
  const uint16_t x = field(w, c, DOLL_DP_X);
  const uint16_t y = field(w, c, DOLL_DP_Y);
  const uint16_t below = adc16(c, y, DOLL_LEAP_DOWN);
  k->blocks[DT_OPENED]++;

  // `$81:B470`, to a point straight below: nothing across, and down.
  called(w, c, 0xb1ee, false);
  set_c(c, true);
  set_field(w, c, DOLL_DP_FLIGHT_DX, sbc16(c, x, x));
  set_field(w, c, DOLL_DP_FLIGHT_STEP_X, 1);
  set_c(c, true);
  const uint16_t down = sbc16(c, below, y);
  set_field(w, c, DOLL_DP_FLIGHT_DY, down);
  set_field(w, c, DOLL_DP_FLIGHT_STEP_Y, 1);
  set_field(w, c, DOLL_DP_FLIGHT_ACC_X, 0);
  set_field(w, c, DOLL_DP_FLIGHT_ACC_Y, 0);
  c->x = 1;
  k->blocks[DT_FLY_TO]++;

  set_field(w, c, DOLL_DP_RISE, 2);
  set_field(w, c, DOLL_DP_RISE_26, 3);
  set_field(w, c, DOLL_DP_RISE_TICK, 0);
  set_field(w, c, DOLL_DP_STRIDE, 0);
  set_field(w, c, DOLL_DP_STEP_EVERY, 1);
  k->blocks[DT_OPENED_TAIL]++;
  c->s = (uint16_t)(c->s + 2);  // the state body's `RTS`

  // The loop's tick, whose return address lies over the state body's.
  called(w, c, 0xb2e7, false);
  doll_tick(w, c->d, &k->tick);
  c->y = field(w, c, DOLL_DP_RECORD);
  k->ticked = true;
  k->blocks[DT_FRAME_TICK]++;
  const uint16_t leave = field(w, c, DOLL_DP_LEAVE);
  if (leave == 0) {
    k->blocks[DT_TAKEN]++;
    again(w, c, k);
    return;
  }
  lda(c, leave);
  c->pc = DOLL_FRAME_LEAVE_PC;
}

// One doll fewer, and its axe's record on the way to being freed.
static void gone(Wram* w, PortCpu* c, DollThreadWork* k) {
  set_c(c, true);
  wram_w16(w, W_SQUIRTS_LIVE,
           sbc16(c, wram_r16(w, W_SQUIRTS_LIVE), DOLL_BUDGET));
  lda(c, field(w, c, DOLL_DP_AXE));
  k->blocks[DT_GONE]++;
  c->pc = DOLL_FREE_AXE_CALL_PC;
}

void doll_end(Wram* w, PortCpu* c, DollThreadWork* k) {
  c->x = DOLL_POINTS;
  lda(c, field(w, c, DOLL_DP_HIT_ID));
  k->blocks[DT_END_HEAD]++;
  if (c->a != 0) {
    PORT_COVER(doll_destroyed);
    c->pc = DOLL_SCORE_CALL_PC;
    return;
  }
  PORT_COVER(doll_left_quietly);
  k->blocks[DT_TAKEN]++;
  gone(w, c, k);
}

void doll_scored(Wram* w, PortCpu* c, DollThreadWork* k) {
  wram_w16(w, W_DOLLS_DESTROYED,
           (uint16_t)(wram_r16(w, W_DOLLS_DESTROYED) + 1));
  lda(c, DOLL_DYING_PICTURES);
  k->blocks[DT_SCORED]++;
  c->pc = DOLL_DYING_CALL_PC;
}

void doll_burst(Wram* w, const Rom* rom, PortCpu* c, DollThreadWork* k) {
  RngResult r;
  rng_next(w, flag(c, PORT_P_C), &r);
  k->drew = true;
  k->draw_v = r.v;
  set_v(c, r.v);
  cmp16(c, r.a, DOLL_FLAME_CHANCE);
  k->blocks[DT_BURST_DRAW]++;
  if (r.a >= DOLL_FLAME_CHANCE) {
    PORT_COVER(doll_burst_plain);
    k->blocks[DT_TAKEN]++;
  } else {
    // What it leaves, a pixel above where it stood: see `port/flame.h`.
    PORT_COVER(doll_burst_flame);
    set_field(w, c, DOLL_DP_AXE_X, field(w, c, DOLL_DP_X));
    set_field(w, c, DOLL_DP_AXE_Y, (uint16_t)(field(w, c, DOLL_DP_Y) - 1));
    k->spawned = true;
    k->spawn_slot = thread_spawn(w, rom, DOLL_FLAME_THREAD, DOLL_BANK, c->d);
    c->x = k->spawn_slot < 0 ? 0xfffe : (uint16_t)k->spawn_slot;
    c->y = k->spawn_slot < 0 ? DOLL_BANK : (THREAD_SPAWN_ARGS - 1) * 2;
    k->blocks[DT_BURST_SPAWN]++;
  }
  lda(c, DOLL_ASHES_PICTURES);
  k->blocks[DT_BURST_TAIL]++;
  c->pc = DOLL_ASHES_CALL_PC;
}

void doll_gone(Wram* w, PortCpu* c, DollThreadWork* k) {
  PORT_COVER(doll_gone);
  gone(w, c, k);
}

void doll_last(Wram* w, PortCpu* c, DollThreadWork* k) {
  lda(c, field(w, c, DOLL_DP_RECORD));
  k->blocks[DT_LAST]++;
  c->pc = DOLL_FREE_JML_PC;
}
