// $82:AE76, $82:AEA2 and $82:BA26  a level's name -- see port/card.h.

#include "port/card.h"

#include "port/coverage.h"
#include "port/thread.h"

bool card_bounce_supported(const Wram* w) {
  return wram_r16(w, W_CARD_BOUNCE_AT) <= CARD_BOUNCE_STEPS * 2u;
}

// The job, queued. False if the queue was full, with the registers and the
// flags as the adder leaves them either way.
static bool queue_job(Wram* w, PortCpu* c, CardWork* k) {
  // What the adder leaves in Y: the last word its search read.
  const uint16_t probe = wram_r16(w, W_VBL_QUEUE_B + 4);
  VblQueueFlags f;
  k->slot = vbl_queue_b_add(w, CARD_JOB, CARD_BANK);
  k->blocks[CD_QUEUE]++;
  vbl_queue_flags(w, W_VBL_QUEUE_B_COUNT, CARD_BANK, k->slot >= 0, &f);
  c->p = (uint8_t)(c->p & ~(PORT_P_N | PORT_P_Z | PORT_P_C));
  if (f.n) c->p |= PORT_P_N;
  if (f.z) c->p |= PORT_P_Z;
  if (f.c) c->p |= PORT_P_C;
  if (k->slot < 0) {
    c->a = CARD_JOB;
    c->y = CARD_BANK;
    k->blocks[CD_TAKEN]++;
    return false;
  }
  c->a = CARD_BANK;
  c->x = (uint16_t)k->slot;
  c->y = k->slot == 0 ? probe : 0;
  return true;
}

void card_slide_frame(Wram* w, const Rom* rom, PortCpu* c, bool bounce,
                      CardWork* k) {
  const uint32_t wai = bounce ? CARD_BOUNCE_WAI_PC : CARD_DROP_WAI_PC;
  c->pc = wai;
  if (!queue_job(w, c, k)) {
    PORT_COVER(card_queue_full);
    return;
  }

  if (!bounce) {
    set_c(c, true);
    c->a = sbc16(c, wram_r16(w, W_CARD_SCROLL_Y), CARD_DROP_LINES);
    wram_w16(w, W_CARD_SCROLL_Y, c->a);
    const uint16_t left = (uint16_t)(wram_r16(w, W_CARD_FRAMES) - 1);
    wram_w16(w, W_CARD_FRAMES, left);
    set_nz16(c, left);
    k->blocks[CD_DROP]++;
    if (left & 0x8000u) {
      PORT_COVER(card_down);
      c->pc = CARD_DROP_END_PC;
    } else {
      PORT_COVER(card_dropped);
      k->blocks[CD_TAKEN]++;
    }
    return;
  }

  // `$82:AEC6`, which keeps the flags it was called with but for what its
  // last compare and its `SEC` or `CLC` set.
  const uint16_t at = wram_r16(w, W_CARD_BOUNCE_AT);
  const uint16_t step = rom_word(rom, CARD_BOUNCE_TABLE + at);
  wram_w16(w, W_CARD_BOUNCE_STEP, step);
  wram_w16(w, W_CARD_SCROLL_Y,
           (uint16_t)(wram_r16(w, W_CARD_SCROLL_Y) + step));
  c->x = (uint16_t)(at + 2);
  wram_w16(w, W_CARD_BOUNCE_AT, c->x);
  c->a = step;
  cmp16(c, step, 0);
  k->blocks[CD_BOUNCE]++;
  if (step == 0) {
    PORT_COVER(card_still);
    k->blocks[CD_BOUNCE_END]++;
    c->pc = CARD_BOUNCE_END_PC;
  } else {
    PORT_COVER(card_bounced);
    set_c(c, false);
    k->blocks[CD_BOUNCE_MORE]++;
    k->blocks[CD_TAKEN] += 2;  // the `BNE` to the `CLC`, and the `BCC` back
  }
}

void card_wait_frame(Wram* w, PortCpu* c, CardWork* k) {
  c->a = (uint16_t)((wram_r16(w, W_CARD_PADS) | wram_r16(w, W_CARD_PADS + 2)) &
                    CARD_WAIT_BUTTONS);
  set_nz16(c, c->a);
  k->blocks[CD_WAIT_PADS]++;
  c->pc = CARD_WAIT_END_PC;
  if (c->a != 0) {
    PORT_COVER(card_wait_pressed);
    k->blocks[CD_TAKEN]++;
    return;
  }
  const uint16_t left = (uint16_t)(wram_r16(w, W_CARD_WAIT_FRAMES) - 1);
  wram_w16(w, W_CARD_WAIT_FRAMES, left);
  set_nz16(c, left);
  k->blocks[CD_WAIT_COUNT]++;
  if (left & 0x8000u) {
    PORT_COVER(card_wait_over);
    return;
  }
  PORT_COVER(card_wait_waited);
  c->a = 1;
  set_nz16(c, 1);
  k->blocks[CD_WAIT_AGAIN]++;
  k->blocks[CD_TAKEN]++;
  c->pc = CARD_WAIT_YIELD_PC;
}
