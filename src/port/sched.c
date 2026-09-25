#include "port/sched.h"

#include "port/coverage.h"

// ---------------------------------------------------------------------------
// The 65816 pieces every stretch here is made of: `port/cpu.h`, and one more
// ---------------------------------------------------------------------------

// An address in the bank this stretch is running in. The scheduler runs in
// two: `$80` from `thread_yield`, and `$00` from `thread_exit`, whose return
// address `thread_spawn` builds with a bank of zero -- and from there on, the
// scan, the `WAI` and the next frame's housekeeping, until some thread yields.
// `pc` holds the entry until the exit is chosen.
static uint32_t here(const PortCpu* c, uint32_t addr) {
  return (c->pc & 0xff0000u) | (addr & 0xffffu);
}

// ---------------------------------------------------------------------------
// The scheduler
// ---------------------------------------------------------------------------

// `$80:836A`, the scan. X is the slot just parked or freed, and the walk starts
// one past it; `at_slot` enters at `$80:8386` instead, on X itself, which is
// where `LDX #$0000` after the tick lands.
//
// Only the upper end is tested. Past slot 23 is the `WAI`, and after it the
// walk starts again from 0 — so a slot below the one that yielded waits for
// the next frame, and within one frame the threads run in slot order.
static void sched_scan(Wram* w, PortCpu* c, SchedWork* k, bool at_slot) {
  for (;; at_slot = false) {
    if (!at_slot) {
      // `INX : INX : CPX #$0030 : BNE $8386`
      c->x = (uint16_t)(c->x + 2);
      const uint16_t r = (uint16_t)(c->x - 0x0030);
      set_nz16(c, r);
      set_c(c, c->x >= 0x0030);
      if (c->x == 0x0030) {
        PORT_COVER(sched_idle);
        k->blocks[SCHED_WRAP]++;
        c->pc = here(c, SCHED_WAI_PC);
        return;
      }
      k->blocks[SCHED_STEP]++;
    }
    // `LDA $1180,X : BPL $836A`
    c->a = wram_r16(w, W_THREAD_WAIT + c->x);
    set_nz16(c, c->a);
    if (!(c->a & 0x8000u)) {
      k->blocks[SCHED_EMPTY]++;
      continue;
    }
    // `ASL A : BNE $836A`. Bit 15 is the live bit, so this is "any ticks left"
    // with the carry always set by it.
    c->a = (uint16_t)(c->a << 1);
    set_c(c, true);
    set_nz16(c, c->a);
    if (c->a != 0) {
      k->blocks[SCHED_ASLEEP]++;
      continue;
    }
    // `STX $08 : LDA $11B0,X : TCS : PLD : PLP : PLB`, and the `RTL` after it
    // is the core's. `PLB` is last, so N and Z are the data bank's, whatever
    // the status byte just pulled said.
    PORT_COVER(sched_switch);
    k->blocks[SCHED_RESUME]++;
    wram_w16(w, W_SCHED_CUR_TASK, c->x);
    c->a = wram_r16(w, W_THREAD_SP + c->x);
    set_nz16(c, c->a);
    c->s = c->a;
    c->d = pull16(w, c);
    set_nz16(c, c->d);
    set_p(c, pull8(w, c));
    c->db = pull8(w, c);
    set_nz8(c, c->db);
    c->pc = here(c, SCHED_RESUME_PC);
    return;
  }
}

void thread_yield_port(Wram* w, PortCpu* c, SchedWork* k) {
  // `PHB : PHP : REP #$30 : PHD`. The status byte a thread resumes with is the
  // one its `JSL` found, flags and widths both.
  push8(w, c, c->db);
  push8(w, c, c->p);
  c->p = (uint8_t)(c->p & ~(PORT_P_M | PORT_P_X));
  push16(w, c, c->d);
  // `PEA $0000 : PLD` and `PHK : PLB`: the scheduler's own page and bank. The
  // three bytes are pushed and pulled straight back, and they are written.
  push16(w, c, 0x0000);
  c->d = pull16(w, c);
  set_nz16(c, c->d);
  push8(w, c, 0x80);
  c->db = pull8(w, c);
  set_nz8(c, c->db);
  // `LDX $08 : ORA #$8000 : STA $1180,X`: live, with A ticks to go.
  c->x = wram_r16(w, W_SCHED_CUR_TASK);
  set_nz16(c, c->x);
  c->a |= 0x8000u;
  set_nz16(c, c->a);
  wram_w16(w, W_THREAD_WAIT + c->x, c->a);
  // `TSC : STA $11B0,X`: where this thread's stack stands, with its status,
  // page and bank on top of the `JSL`'s return address.
  c->a = c->s;
  set_nz16(c, c->a);
  wram_w16(w, W_THREAD_SP + c->x, c->a);
  k->blocks[SCHED_PARK]++;
  sched_scan(w, c, k, false);
}

void thread_exit_port(Wram* w, PortCpu* c, SchedWork* k) {
  // `PHK : PLB : LDA #$0000 : TCD`. The bank is `$00`, in practice.
  push8(w, c, (uint8_t)(c->pc >> 16));
  c->db = pull8(w, c);
  set_nz8(c, c->db);
  c->a = 0;
  c->d = 0;
  set_nz16(c, 0);
  // `LDX $08`, then the slot's wait word and its handler, both halves, zeroed.
  c->x = wram_r16(w, W_SCHED_CUR_TASK);
  set_nz16(c, c->x);
  wram_w16(w, W_THREAD_WAIT + c->x, 0);
  wram_w16(w, W_THREAD_HANDLER + c->x, 0);
  wram_w16(w, W_THREAD_HANDLER_BANK + c->x, 0);
  // `DEC $06 : BRA $836A`
  const uint16_t live = (uint16_t)(wram_r16(w, W_THREAD_COUNT) - 1);
  wram_w16(w, W_THREAD_COUNT, live);
  set_nz16(c, live);
  PORT_COVER(sched_thread_ended);
  k->blocks[SCHED_EXIT]++;
  sched_scan(w, c, k, false);
}

void sched_wake(Wram* w, PortCpu* c, SchedWork* k) {
  // `INC $20 : BNE : INC $22`. A 32-bit tick. The carry comes every 65,536
  // frames, about eighteen minutes of play, which no movie is long enough
  // for; `--poke` of `$20` to `$FFFF` is how it was checked.
  const uint16_t lo = (uint16_t)(wram_r16(w, W_SCHED_TICK) + 1);
  wram_w16(w, W_SCHED_TICK, lo);
  set_nz16(c, lo);
  if (lo == 0) {
    PORT_COVER(sched_tick_carry);
    const uint16_t hi = (uint16_t)(wram_r16(w, W_SCHED_TICK + 2) + 1);
    wram_w16(w, W_SCHED_TICK + 2, hi);
    set_nz16(c, hi);
    k->blocks[SCHED_WAKE_CARRY]++;
  } else {
    k->blocks[SCHED_WAKE]++;
  }
  // `LDX #$125F : TXS`
  c->x = SCHED_STACK;
  set_nz16(c, c->x);
  c->s = c->x;
  c->pc = here(c, SCHED_OAM_PC);
}

void sched_rescan(Wram* w, PortCpu* c, SchedWork* k) {
  // `JSR $8398`. The return address is `$8382`, high byte first.
  push16(w, c, 0x8382);
  k->blocks[SCHED_TICK_HEAD]++;
  // `thread_tick_waits`, in line, because its A and flags are this stretch's
  // too. The same walk as `thread_tick_waits` in `port/thread.c`.
  c->x = 0x002e;
  set_nz16(c, c->x);
  for (;;) {
    c->a = wram_r16(w, W_THREAD_WAIT + c->x);
    set_nz16(c, c->a);
    if (!(c->a & 0x8000u)) {
      PORT_COVER(wait_empty);
      k->blocks[SCHED_TICK_EMPTY]++;
    } else {
      // `CMP #$8000 : BEQ`. A is at least $8000 here, so carry is set.
      set_c(c, true);
      set_nz16(c, (uint16_t)(c->a - 0x8000u));
      if (c->a == 0x8000u) {
        PORT_COVER(wait_expired);
        k->blocks[SCHED_TICK_DONE]++;
      } else {
        PORT_COVER(wait_tick);
        c->a = (uint16_t)(c->a - 1);
        set_nz16(c, c->a);
        wram_w16(w, W_THREAD_WAIT + c->x, c->a);
        k->blocks[SCHED_TICK_STEP]++;
      }
    }
    // `DEX : DEX : BPL`
    c->x = (uint16_t)(c->x - 2);
    set_nz16(c, c->x);
    if (c->x & 0x8000u) break;
    k->blocks[SCHED_TICK_NEXT]++;
  }
  // `RTS`, then `LDX #$0000`, and the scan from slot 0.
  (void)pull16(w, c);
  k->blocks[SCHED_TICK_TAIL]++;
  c->x = 0;
  set_nz16(c, 0);
  sched_scan(w, c, k, true);
}

// ---------------------------------------------------------------------------
// The vblank dispatchers
// ---------------------------------------------------------------------------

const VblQueueDesc VBL_QUEUE_A_DESC = {
    .entry = 0x8083e0u,
    .resume = 0x808401u,
    .dispatch = 0x808400u,
    .done = 0x808417u,
    .table = W_VBL_QUEUE_A,
    .count_at = W_VBL_QUEUE_A_COUNT,
    .last = 0x0038,
};

const VblQueueDesc VBL_QUEUE_B_DESC = {
    .entry = 0x80843du,
    .resume = 0x80845eu,
    .dispatch = 0x80845du,
    .done = 0x808474u,
    .table = W_VBL_QUEUE_B,
    .count_at = W_VBL_QUEUE_B_COUNT,
    .last = 0x001c,
};

void vbl_queue_run(Wram* w, const VblQueueDesc* q, bool resumed, PortCpu* c,
                   VblRunWork* k) {
  bool step = false;  // go straight to the `DEX` at the bottom of the walk
  if (!resumed) {
    // `LDA $0C : BEQ <RTS> : STA $10 : LDX #<last>`
    c->a = wram_r16(w, q->count_at);
    set_nz16(c, c->a);
    if (c->a == 0) {
      PORT_COVER(vbl_run_idle);
      k->blocks[VBL_RUN_EMPTY_QUEUE]++;
      c->pc = q->done;
      return;
    }
    wram_w16(w, W_VBL_QUEUE_REMAINING, c->a);
    c->x = q->last;
    set_nz16(c, c->x);
    k->blocks[VBL_RUN_HEAD]++;
  } else {
    // `LDX $12 : BCS <DEX>`: the job's own carry says whether it stays.
    c->x = wram_r16(w, W_VBL_QUEUE_INDEX);
    set_nz16(c, c->x);
    if (c->p & PORT_P_C) {
      PORT_COVER(vbl_run_kept);
      k->blocks[VBL_RUN_KEPT]++;
    } else {
      // `LDA #$0000 : STA <slot> : DEC <count> : DEC $10 : BEQ <RTS>`
      PORT_COVER(vbl_run_dropped);
      c->a = 0;
      wram_w16(w, (uint32_t)q->table + c->x, 0);
      const uint16_t count = (uint16_t)(wram_r16(w, q->count_at) - 1);
      wram_w16(w, q->count_at, count);
      const uint16_t left = (uint16_t)(wram_r16(w, W_VBL_QUEUE_REMAINING) - 1);
      wram_w16(w, W_VBL_QUEUE_REMAINING, left);
      set_nz16(c, left);
      k->blocks[VBL_RUN_DROPPED]++;
      if (left == 0) {
        PORT_COVER(vbl_run_last_dropped);
        k->blocks[VBL_RUN_DROPPED_OUT]++;
        c->pc = q->done;
        return;
      }
      k->blocks[VBL_RUN_DROPPED_ON]++;
    }
    step = true;
  }

  for (;; step = true) {
    if (step) {
      // `DEX : DEX : DEX : DEX : BPL`
      c->x = (uint16_t)(c->x - 4);
      set_nz16(c, c->x);
      if (c->x & 0x8000u) {
        k->blocks[VBL_RUN_END]++;
        c->pc = q->done;
        return;
      }
      k->blocks[VBL_RUN_NEXT]++;
    }
    // `LDA <slot>,X : BEQ <DEX>`
    c->a = wram_r16(w, (uint32_t)q->table + c->x);
    set_nz16(c, c->a);
    if (c->a == 0) {
      k->blocks[VBL_RUN_FREE_SLOT]++;
      continue;
    }
    // `PHK : PEA <the RTL below> : SEP #$20 : LDA <slot+2>,X : PHA : REP #$30
    //  : LDA <slot>,X : PHA : STX $12`. The job's return lands one past the
    // `RTL`, on `LDX $12`. The bank is pushed as a byte, so A's high byte is
    // whatever the slot word left there.
    PORT_COVER(vbl_run_job);
    push8(w, c, 0x80);
    push16(w, c, (uint16_t)q->dispatch);
    c->p |= PORT_P_M;
    const uint8_t bank = wram_r8(w, (uint32_t)q->table + c->x + 2);
    c->a = (uint16_t)((c->a & 0xff00u) | bank);
    set_nz8(c, bank);
    push8(w, c, bank);
    c->p = (uint8_t)(c->p & ~(PORT_P_M | PORT_P_X));
    c->a = wram_r16(w, (uint32_t)q->table + c->x);
    set_nz16(c, c->a);
    push16(w, c, c->a);
    wram_w16(w, W_VBL_QUEUE_INDEX, c->x);
    k->blocks[VBL_RUN_JOB]++;
    c->pc = q->dispatch;
    return;
  }
}

// ---------------------------------------------------------------------------
// The NMI handler
// ---------------------------------------------------------------------------

// `PLB : PLD : PLY : PLX : PLA`, each setting N and Z, which is how the handler
// leaves on both of its paths.
static void nmi_pull_all(const Wram* w, PortCpu* c) {
  c->db = pull8(w, c);
  set_nz8(c, c->db);
  c->d = pull16(w, c);
  set_nz16(c, c->d);
  c->y = pull16(w, c);
  set_nz16(c, c->y);
  c->x = pull16(w, c);
  set_nz16(c, c->x);
  c->a = pull16(w, c);
  set_nz16(c, c->a);
}

void nmi_enter(Wram* w, PortCpu* c, NmiWork* k) {
  // `REP #$30 : PHA : PHX : PHY : PHD : PHB`, whatever the interrupted code
  // had, at 16 bits.
  c->p = (uint8_t)(c->p & ~(PORT_P_M | PORT_P_X));
  push16(w, c, c->a);
  push16(w, c, c->x);
  push16(w, c, c->y);
  push16(w, c, c->d);
  push8(w, c, c->db);
  // `LDA #$0000 : TCD : PHK : PLB`
  c->a = 0;
  c->d = 0;
  set_nz16(c, 0);
  push8(w, c, 0x80);
  c->db = pull8(w, c);
  set_nz8(c, c->db);
  // `INC $16`: every NMI counts, the re-entered ones too.
  const uint16_t frames = (uint16_t)(wram_r16(w, W_NMI_FRAME_COUNTER) + 1);
  wram_w16(w, W_NMI_FRAME_COUNTER, frames);
  set_nz16(c, frames);
  // `LDA #$8000 : TSB $14 : BNE $81F3`. `TSB` sets only Z, from the bits that
  // were already set, so N stays the `LDA`'s.
  c->a = 0x8000;
  set_nz16(c, c->a);
  const uint16_t flags = wram_r16(w, W_NMI_FLAGS);
  c->p = (uint8_t)(c->p & ~PORT_P_Z);
  if ((flags & c->a) == 0) c->p |= PORT_P_Z;
  wram_w16(w, W_NMI_FLAGS, (uint16_t)(flags | c->a));
  if (flags & 0x8000u) {
    // A handler is already running: undo the pushes and go.
    PORT_COVER(nmi_reentered);
    nmi_pull_all(w, c);
    k->blocks[NMI_ENTER_BUSY]++;
    c->pc = NMI_RETURN_PC;
    return;
  }
  k->blocks[NMI_ENTER]++;
  c->pc = NMI_BLANK_PC;
}

void nmi_stack(Wram* w, PortCpu* c, NmiWork* k) {
  // `REP #$30 : TSC : STA $04 : LDA #$129F : TCS`
  c->p = (uint8_t)(c->p & ~(PORT_P_M | PORT_P_X));
  c->a = c->s;
  set_nz16(c, c->a);
  wram_w16(w, W_NMI_SAVED_SP, c->a);
  c->a = NMI_STACK;
  set_nz16(c, c->a);
  c->s = c->a;
  k->blocks[NMI_STACK_RUN]++;
  c->pc = NMI_FLUSH_PC;
}

// `LDA <pad> : STA <raw> : XBA : AND #$000F : TAX : LDA $81F9,X : AND #$00FF
// : STA <dir>`. The high byte of the pad word is B Y Select Start and the
// D-pad, and its low nibble is the D-pad.
static void nmi_pad(Wram* w, const Rom* rom, PortCpu* c, uint16_t pad,
                    uint32_t raw, uint32_t dir) {
  c->a = pad;
  set_nz16(c, c->a);
  wram_w16(w, raw, c->a);
  c->a = (uint16_t)((c->a >> 8) | (c->a << 8));
  set_nz8(c, (uint8_t)c->a);
  c->a &= 0x000fu;
  set_nz16(c, c->a);
  c->x = c->a;
  set_nz16(c, c->x);
  c->a = rom_word(rom, NMI_DIR_TABLE + c->x);
  set_nz16(c, c->a);
  c->a &= 0x00ffu;
  set_nz16(c, c->a);
  wram_w16(w, dir, c->a);
}

void nmi_input(Wram* w, const Rom* rom, PortCpu* c, uint16_t joy1,
               uint16_t joy2, NmiWork* k) {
  // `REP #$30`
  c->p = (uint8_t)(c->p & ~(PORT_P_M | PORT_P_X));
  nmi_pad(w, rom, c, joy1, W_JOY_RAW, W_JOY_DIR);
  nmi_pad(w, rom, c, joy2, W_JOY_RAW + 2, W_JOY_DIR + 2);
  k->blocks[NMI_INPUT_RUN]++;
  c->pc = NMI_QUEUE_B_PC;
}

void nmi_leave(Wram* w, PortCpu* c, NmiWork* k) {
  // `LDA $1EB4 : BNE + : INC $24`
  c->a = wram_r16(w, W_RNG_HOLD);
  set_nz16(c, c->a);
  if (c->a == 0) {
    PORT_COVER(nmi_rng_tick);
    const uint16_t r = (uint16_t)(wram_r16(w, W_RNG_STATE) + 1);
    wram_w16(w, W_RNG_STATE, r);
    set_nz16(c, r);
    k->blocks[NMI_LEAVE_TICK]++;
  } else {
    PORT_COVER(nmi_rng_held);
    k->blocks[NMI_LEAVE]++;
  }
  // `LDA $04 : TCS : LDA #$8000 : TRB $14`, and out the way it came in.
  c->a = wram_r16(w, W_NMI_SAVED_SP);
  set_nz16(c, c->a);
  c->s = c->a;
  c->a = 0x8000;
  set_nz16(c, c->a);
  const uint16_t flags = wram_r16(w, W_NMI_FLAGS);
  c->p = (uint8_t)(c->p & ~PORT_P_Z);
  if ((flags & c->a) == 0) c->p |= PORT_P_Z;
  wram_w16(w, W_NMI_FLAGS, (uint16_t)(flags & ~c->a));
  nmi_pull_all(w, c);
  c->pc = NMI_RETURN_PC;
}

// ---------------------------------------------------------------------------
// The reset's WRAM clear
// ---------------------------------------------------------------------------

// `MVN`, with A+1 bytes from X in `src` to Y in `dst`, one at a time and in
// order: every block move here overlaps its own destination by one byte, which
// is how it fills. X and Y wrap inside their banks; the data bank ends as the
// destination's.
static void mvn(Wram* w, PortCpu* c, uint8_t dst, uint8_t src, ResetWork* k) {
  const uint32_t from = (uint32_t)(src - 0x7e) << 16;
  const uint32_t to = (uint32_t)(dst - 0x7e) << 16;
  do {
    wram_w8(w, to | c->y, wram_r8(w, from | c->x));
    c->x = (uint16_t)(c->x + 1);
    c->y = (uint16_t)(c->y + 1);
    k->moved++;
  } while (c->a-- != 0);
  c->db = dst;
}

static void lda(PortCpu* c, uint16_t v) {
  c->a = v;
  set_nz16(c, v);
}

static void ldx(PortCpu* c, uint16_t v) {
  c->x = v;
  set_nz16(c, v);
}

static void ldy(PortCpu* c, uint16_t v) {
  c->y = v;
  set_nz16(c, v);
}

void reset_clear(Wram* w, PortCpu* c, ResetWork* k) {
  static const struct {
    uint16_t at, magic;
  } MAGIC[] = {{0x2000, 0xa675}, {0x2062, 0x98a3}, {0x2122, 0x4102},
               {0x2126, 0x2217}};

  lda(c, 0x0000);
  wram_w16(w, 0x0000, c->a);
  bool warm = true;
  for (int i = 0; i < 4 && warm; i++) {
    lda(c, wram_r16(w, MAGIC[i].at));
    cmp16(c, c->a, MAGIC[i].magic);
    k->blocks[i == 0 ? RESET_HEAD : RESET_CHECK]++;
    if (c->a != MAGIC[i].magic) warm = false;
  }

  if (warm) {
    // $80EC: $0000-$1FFF, then $2128 to the end of the bank
    PORT_COVER(reset_warm);
    lda(c, 0x1ffe);
    ldx(c, 0x0000);
    ldy(c, 0x0001);
    k->blocks[RESET_SETUP]++;
    mvn(w, c, 0x7e, 0x7e, k);
    lda(c, 0x0000);
    wram_w16(w, 0x2128, c->a);
    lda(c, 0xded5);
    ldx(c, 0x2128);
    ldy(c, 0x2129);
    k->blocks[RESET_WARM]++;
    mvn(w, c, 0x7e, 0x7e, k);
    k->blocks[RESET_BRA]++;
  } else {
    // $810D: the whole bank, then the magic
    PORT_COVER(reset_cold);
    k->blocks[RESET_TAKEN]++;
    lda(c, 0xfffe);
    ldx(c, 0x0000);
    ldy(c, 0x0001);
    k->blocks[RESET_SETUP]++;
    mvn(w, c, 0x7e, 0x7e, k);
    for (int i = 0; i < 4; i++) {
      lda(c, MAGIC[i].magic);
      wram_w16(w, MAGIC[i].at, c->a);
    }
    k->blocks[RESET_MAGIC]++;
  }

  // $8135: bank $7E over bank $7F
  lda(c, 0x0000);
  ldx(c, c->a);
  ldy(c, c->a);
  lda(c, (uint16_t)(c->a - 1));
  mvn(w, c, 0x7f, 0x7e, k);
  // PHK : PLB : REP #$30
  push8(w, c, (uint8_t)(c->pc >> 16));
  c->db = pull8(w, c);
  set_nz8(c, c->db);
  c->p = (uint8_t)(c->p & ~(PORT_P_M | PORT_P_X));
  // The NMI's vector at $00, and the screen's brightness at $136C.
  lda(c, 0x8179);
  wram_w16(w, 0x0000, c->a);
  lda(c, 0x0080);
  wram_w16(w, 0x0002, c->a);
  lda(c, 0x0080);
  wram_w16(w, 0x136c, c->a);
  k->blocks[RESET_TAIL]++;
  c->pc = RESET_NMI_ON_PC;
}
