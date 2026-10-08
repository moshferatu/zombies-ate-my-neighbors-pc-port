#include "port/apu.h"

#include <stddef.h>

#include "port/coverage.h"

// The one APU. See the header for why this is a file-scope pointer and not a
// parameter.
static const ApuPorts* g_apu;

void apu_attach(const ApuPorts* ports) { g_apu = ports; }

// ---------------------------------------------------------------------------
// $80:CCC8  apu_send
// ---------------------------------------------------------------------------

void apu_send(Wram* w, uint16_t a, uint16_t x, ApuSendRegs* out) {
  // `SEP #$30 : LDY $1E`. Eight bits wide for the whole routine, so the counter
  // is one byte and it wraps at 256 — which is the whole protocol: the SPC
  // acknowledges by copying it back, and a byte is plenty because the CPU never
  // has more than one command outstanding. It is also the instruction that makes
  // the two callers' register widths stop mattering; see the header.
  uint8_t seq = wram_r8(w, W_APU_SEQ);
  uint8_t cmd = (uint8_t)x, param = (uint8_t)a;

  // `CPY $2143 : BNE` — the wait — then `STX $2142 : STA $2141 : INY : STY
  // $2143`. All four instructions are on the host's side of the line, in that
  // order, and a port with no APU attached simply skips them.
  if (g_apu && g_apu->send) g_apu->send(g_apu->ctx, seq, cmd, param);

  // `STY $1E`. The counter advances whether or not anybody was listening,
  // because it is game state and not hardware state — the ROM would have
  // written it too.
  uint8_t next = (uint8_t)(seq + 1);
  wram_w8(w, W_APU_SEQ, next);

  if (!out) return;
  // A is read by `STA $2141` and never written, so the whole 16-bit register
  // comes back exactly as it went in — high byte included, whatever it happens
  // to hold. On the uploader's path that is the high half of a pointer word left
  // over from `$80:CC84  LDA $80CCE0,X`; it is junk, and it is *preserved* junk.
  out->a = a;
  // X likewise, except that `SEP #$30` clears the high byte of an index register
  // on the way in. A caller that arrived 8 bits wide had it cleared already.
  //
  // **This mask is transcribed, not diffed, and nothing can check it.** Deleting
  // it passes all 23,632 calls on every movie, because the six call sites in the
  // ROM are `LDX #$0001`, `#$0002`, `#$0006`, `#$0008`, `#$000A` and `#$0013` —
  // every one of them a small constant whose high byte is already zero. So the
  // one place register width could still have shown through is a place no caller
  // ever puts anything. Same shape as the doubled-id index in
  // `src/port/collide.c`: not a branch, so no coverage mark can express it, and
  // the only defence is to write it down. Found by perturbation.
  out->x = x & 0xff;
  // Y is the post-increment counter, one byte wide, wrapping at 256 — and the
  // wrap is real rather than theoretical. Returning a 16-bit `seq + 1` instead
  // passes 251 calls and fails on **call 256** with `Y: ROM $0000, port $0100`,
  // which no sound effect would ever have shown: it takes an uploader sending
  // 23,820 commands in a row to go round the counter, and it goes round 93 times.
  out->y = next;
  // `INY` is genuinely the last flag-setting instruction here — unlike in
  // `apu_play_sfx`, where a `PLD` follows and takes them over — so N and Z
  // describe the new sequence counter.
  out->n = (next & 0x80) != 0;
  out->z = next == 0;
  // Carry is the wait's: `CPY $2143 : BNE` only falls through when the two are
  // equal, and equal sets carry. Set on every exit, because the SPC answered.
  out->c = true;
}

// ---------------------------------------------------------------------------
// $80:CC3B  apu_play_sfx
// ---------------------------------------------------------------------------

void apu_play_sfx(Wram* w, uint16_t id, uint16_t caller_dp, ApuSfxRegs* out) {
  // `PHD : PEA $0000 : PLD`. `apu_send` reads `$1E` on direct page zero no
  // matter who called it, which is why `W_APU_SEQ` is an address in
  // `port/wram.h` rather than an offset in `port/collide.h`: it is a global,
  // not a field of whichever thread happens to be making the noise.
  apu_send(w, id, APU_CMD_PLAY_SFX, NULL);

  // `REP #$30 : PLD : RTL`, and every one of these comes out of that.
  //
  // A survives untouched — `SEP #$30` hides its high byte rather than clearing
  // it, and the low byte is only ever read by the `STA $2141`.
  out->a = id;
  // X was `LDX #$0001` sixteen bits wide, then narrowed to 8 and widened back.
  // Narrowing an index register *clears* its high byte, so what returns is the
  // command, zero-extended — which happens to be the same word that went in.
  out->x = APU_CMD_PLAY_SFX;
  // Y is the post-increment counter, and it came back through the same
  // narrowing, so it is the byte and not a word.
  out->y = wram_r8(w, W_APU_SEQ);

  // N and Z are the `PLD`'s, not the `INY`'s. That is easy to get wrong from
  // the listing — `INY` is visibly the last arithmetic in the routine — but
  // `PLD` sets both from the 16-bit value it pulls, and it runs two
  // instructions later. So the flags a caller sees describe *its own direct
  // page*.
  out->n = (caller_dp & 0x8000) != 0;
  out->z = caller_dp == 0;
  // Carry is the wait's. `CPY $2143` leaves the loop only when the two are
  // equal, and equal sets carry; nothing after it touches C. So this is set on
  // every exit, and it is set because the SPC answered.
  out->c = true;
}

// ---------------------------------------------------------------------------
// $80:CC13, $80:CC27  a command with its argument built in
// ---------------------------------------------------------------------------

void apu_fixed_command(Wram* w, uint8_t cmd, uint16_t caller_dp,
                       ApuSfxRegs* out) {
  if (cmd == APU_CMD_PLAY_SFX) {
    PORT_COVER(apu_fixed_1);
  } else {
    PORT_COVER(apu_fixed_2);
  }
  apu_send(w, APU_FIXED_PARAM, cmd, NULL);
  // As `apu_play_sfx` leaves them: the registers narrowed and widened again,
  // N and Z from the `PLD`, carry from the wait.
  out->a = APU_FIXED_PARAM;
  out->x = cmd;
  out->y = wram_r8(w, W_APU_SEQ);
  out->n = (caller_dp & 0x8000) != 0;
  out->z = caller_dp == 0;
  out->c = true;
}

// ---------------------------------------------------------------------------
// $80:CCBF  apu_next_byte
// ---------------------------------------------------------------------------

void apu_next_byte(Wram* w, const Rom* rom, uint16_t in_a, ApuNextRegs* out) {
  // `LDA [$18]`. Three bytes on direct page zero, and the bank is the third —
  // read but never written, which is what makes the wrap below a wrap.
  uint16_t lo = wram_r8(w, W_APU_SRC);
  uint16_t hi = wram_r8(w, W_APU_SRC + 1);
  uint8_t bank = (uint8_t)wram_r8(w, W_APU_SRC_BANK);
  uint32_t at = ((uint32_t)bank << 16) | (uint32_t)((hi << 8) | lo);

  uint32_t avail = 0;
  const uint8_t* p = rom_ptr(rom, at, &avail);
  // Every set the uploader walks lives in ROM, so this is the only reading
  // this routine can do. A pointer the host could not resolve reads as zero
  // rather than trapping, which is what `rom_word` does two files over.
  uint8_t byte = (p && avail) ? *p : 0;

  // `INC $18 : BNE +2 : INC $19` — eight bits at a time, so the carry is a
  // branch and the bank is out of reach. Whether the second `INC` runs is the
  // only decision in the routine and it decides the flags, so it is a site.
  lo = (uint16_t)((lo + 1) & 0xffu);
  wram_w8(w, W_APU_SRC, (uint8_t)lo);
  uint16_t last = lo;
  if (lo == 0) {
    PORT_COVER(apu_src_wrap);
    hi = (uint16_t)((hi + 1) & 0xffu);
    wram_w8(w, W_APU_SRC + 1, (uint8_t)hi);
    last = hi;
  } else {
    PORT_COVER(apu_src_step);
  }

  if (!out) return;
  // The high byte is the caller's, untouched: `SEP #$30` hid it and the 8-bit
  // `LDA` could not have written it. Same shape as `apu_send`'s A.
  out->a = (uint16_t)((in_a & 0xff00u) | byte);
  // ...and N and Z are the surviving `INC`'s, which is the cursor rather than
  // the byte. On the wrapping call `INC $18` set Z, and then `INC $19` took the
  // flags straight back off it.
  out->n = (last & 0x80u) != 0;
  out->z = last == 0;
}

// ---------------------------------------------------------------------------
// The uploads, traced
// ---------------------------------------------------------------------------
//
// Written against the whole CPU (`port/cpu.h`), because the routines leave by
// their own exits and hand on every register. What the instructions do to A
// is followed exactly, high byte included, since `XBA` brings it back down.

static void sep(PortCpu* c, uint8_t bits) { set_p(c, (uint8_t)(c->p | bits)); }

static void rep(PortCpu* c, uint8_t bits) {
  c->p = (uint8_t)(c->p & ~bits);
}

// `LDA #imm`, `LDA dp` and the like with the accumulator 8 bits wide: the high
// byte stays.
static void lda8(PortCpu* c, uint8_t v) {
  c->a = (uint16_t)((c->a & 0xff00u) | v);
  set_nz8(c, v);
}

static void xba(PortCpu* c) {
  c->a = (uint16_t)(c->a << 8 | c->a >> 8);
  set_nz8(c, (uint8_t)c->a);
}

// `ADC #imm`, 8 bits wide, binary. The guards keep decimal mode away.
static void adc8(PortCpu* c, uint8_t m) {
  const uint8_t a = (uint8_t)c->a;
  const unsigned r = a + m + (flag(c, PORT_P_C) ? 1u : 0u);
  set_v(c, ((a ^ r) & (m ^ r) & 0x80u) != 0);
  set_c(c, r > 0xffu);
  lda8(c, (uint8_t)r);
}

// A wait's compare, once it has matched: equal sets carry and zero.
static void matched(PortCpu* c) {
  set_c(c, true);
  set_nz8(c, 0);
}

// Where `[$18]` points: three bytes on direct page zero.
static uint32_t apu_src(const Wram* w) {
  return (uint32_t)wram_r8(w, W_APU_SRC) |
         (uint32_t)wram_r8(w, W_APU_SRC + 1) << 8 |
         (uint32_t)wram_r8(w, W_APU_SRC_BANK) << 16;
}

static void jsr(Wram* w, PortCpu* c, HwTrace* t, uint16_t ret) {
  hw_run(t, APU_JSR);
  push16(w, c, ret);
  hw_stack(t, c->s);
}

static void rts(const Wram* w, PortCpu* c, HwTrace* t) {
  hw_run(t, APU_RTS);
  (void)pull16(w, c);
  hw_stack(t, c->s);
}

// `$80:CCC8` from its first instruction to its `RTS`, not included.
static void send_body(Wram* w, PortCpu* c, HwTrace* t) {
  hw_run(t, AS_HEAD);
  sep(c, PORT_P_M | PORT_P_X);
  const uint8_t seq = wram_r8(w, W_APU_SEQ);
  c->y = seq;
  set_nz8(c, seq);
  hw_wait8(t, APU_PORT_SEQ, seq);
  matched(c);
  hw_run(t, AS_STORE);
  hw_w8(t, APU_PORT_CMD, (uint8_t)c->x);
  hw_run(t, AS_STORE);
  hw_w8(t, APU_PORT_PARAM, (uint8_t)c->a);
  hw_run(t, AS_INY);
  c->y = (uint8_t)(seq + 1);
  set_nz8(c, (uint8_t)c->y);
  hw_w8(t, APU_PORT_SEQ, (uint8_t)c->y);
  hw_run(t, AS_TAIL);
  wram_w8(w, W_APU_SEQ, (uint8_t)c->y);
}

void apu_send_traced(Wram* w, PortCpu* c, HwTrace* t) {
  send_body(w, c, t);
  c->pc = APU_SEND_EXIT;
}

// `$80:CCBF` from its first instruction to its `RTS`, not included. The same
// as `apu_next_byte`, with the byte's price depending on where it is.
static void next_body(Wram* w, const Rom* rom, PortCpu* c, HwTrace* t) {
  const uint32_t at = apu_src(w);
  hw_run(t, bus_fast(at) ? NB_READ_FAST : NB_READ_SLOW);
  lda8(c, bus_r8(w, rom, at));
  const uint8_t lo = (uint8_t)(wram_r8(w, W_APU_SRC) + 1u);
  wram_w8(w, W_APU_SRC, lo);
  set_nz8(c, lo);
  if (lo != 0) {
    hw_run(t, NB_STEP);
    return;
  }
  hw_run(t, NB_CARRY);
  const uint8_t hi = (uint8_t)(wram_r8(w, W_APU_SRC + 1) + 1u);
  wram_w8(w, W_APU_SRC + 1, hi);
  set_nz8(c, hi);
}

static void call_next(Wram* w, const Rom* rom, PortCpu* c, HwTrace* t,
                      uint16_t ret) {
  jsr(w, c, t, ret);
  next_body(w, rom, c, t);
  rts(w, c, t);
}

static void call_send(Wram* w, PortCpu* c, HwTrace* t, uint16_t ret) {
  jsr(w, c, t, ret);
  send_body(w, c, t);
  rts(w, c, t);
}

void apu_load_set_traced(Wram* w, const Rom* rom, PortCpu* c, HwTrace* t) {
  // The nine instructions before `SEP #$30`, and the only wide ones.
  hw_run(t, LS_HEAD);
  rep(c, PORT_P_M | PORT_P_X);
  c->a &= 0x00ffu;
  c->a = asl16(c, c->a);
  c->a = asl16(c, c->a);
  c->x = c->a;
  c->a = rom_word(rom, APU_SET_TABLE + 2u + c->x);
  wram_w16(w, W_APU_SRC_BANK, c->a);
  c->a = rom_word(rom, APU_SET_TABLE + c->x);
  set_nz16(c, c->a);
  wram_w16(w, W_APU_SRC, c->a);
  sep(c, PORT_P_M | PORT_P_X);

  for (;;) {
    // `$80:CC92`: the block's count, low byte then high.
    call_next(w, rom, c, t, 0xcc94);
    hw_run(t, LS_LO);
    wram_w8(w, W_APU_BLOCK_LEFT, (uint8_t)c->a);
    call_next(w, rom, c, t, 0xcc99);
    hw_run(t, LS_HI);
    wram_w8(w, W_APU_BLOCK_LEFT + 1, (uint8_t)c->a);
    lda8(c, (uint8_t)(c->a | wram_r8(w, W_APU_BLOCK_LEFT)));
    // The `$0000` that ends the set, and the only way this routine leaves.
    if ((uint8_t)c->a == 0) {
      PORT_COVER(apu_set_end);
      hw_run(t, LS_END);
      c->pc = APU_LOAD_SET_EXIT;
      return;
    }
    PORT_COVER(apu_set_block);
    hw_run(t, LS_BLOCK);
    c->x = APU_CMD_BLOCK;
    set_nz8(c, APU_CMD_BLOCK);
    call_send(w, c, t, 0xcca5);

    for (;;) {
      // `$80:CCA6`: one byte, under command $06.
      call_next(w, rom, c, t, 0xcca8);
      hw_run(t, LS_BYTE);
      c->x = APU_CMD_BYTE;
      set_nz8(c, APU_CMD_BYTE);
      call_send(w, c, t, 0xccad);

      // `LDA $1C : BNE +2 : DEC $1D : + DEC $1C`, the 16-bit borrow.
      uint8_t left = wram_r8(w, W_APU_BLOCK_LEFT);
      lda8(c, left);
      if (left != 0) {
        PORT_COVER(apu_set_count);
        hw_run(t, LS_COUNT);
      } else {
        PORT_COVER(apu_set_borrow);
        hw_run(t, LS_BORROW);
        const uint8_t hi = (uint8_t)(wram_r8(w, W_APU_BLOCK_LEFT + 1) - 1u);
        wram_w8(w, W_APU_BLOCK_LEFT + 1, hi);
        set_nz8(c, hi);
      }
      hw_run(t, LS_DEC);
      left = (uint8_t)(left - 1u);
      wram_w8(w, W_APU_BLOCK_LEFT, left);
      lda8(c, (uint8_t)(left | wram_r8(w, W_APU_BLOCK_LEFT + 1)));
      if ((uint8_t)c->a != 0) {
        hw_run(t, LS_MORE);
        continue;
      }
      hw_run(t, LS_NEXT);
      break;
    }
  }
}

// `MVN dst,src`: A + 1 bytes from `src:X` to `dst:Y`, and the data bank left
// on the destination. Both copies in this file go to WRAM from cartridge.
static void mvn(Wram* w, const Rom* rom, PortCpu* c, HwTrace* t, uint8_t dst,
                uint8_t src) {
  const uint32_t n = (uint32_t)c->a + 1u;
  if (n > 0xffffu) hw_run_n(t, AB_MVN, 0xffffu);
  hw_run_n(t, AB_MVN, (uint16_t)(n > 0xffffu ? n - 0xffffu : n));
  for (uint32_t i = 0; i < n; i++) {
    const uint8_t v = bus_r8(w, rom, (uint32_t)src << 16 | c->x);
    wram_w8(w, (uint32_t)(dst - 0x7eu) << 16 | c->y, v);
    c->x = (uint16_t)(c->x + 1u);
    c->y = (uint16_t)(c->y + 1u);
  }
  c->a = 0xffffu;
  c->db = dst;
}

// `$80:CB61`, from its first instruction to its `RTS`, which it executes: the
// caller's `JSR` has pushed the return address already.
static void ipl_upload(Wram* w, const Rom* rom, PortCpu* c, HwTrace* t) {
  hw_run(t, IP_HEAD);
  push8(w, c, c->p);
  hw_stack(t, c->s);
  rep(c, PORT_P_M | PORT_P_X);
  c->y = 0;
  c->a = 0xbbaau;
  set_nz16(c, c->a);
  // The SPC700's boot ROM says it is ready with `$BBAA` on the first two ports.
  hw_wait16(t, 0x2140, 0xbbaau);
  set_c(c, true);
  set_nz16(c, 0);
  hw_run(t, IP_START);
  sep(c, PORT_P_M);
  lda8(c, 0xcc);

  const uint32_t src = apu_src(w);
  for (;;) {
    // `$80:CB9D`: the kick value kept, then a block's size and destination.
    hw_run(t, IP_HDR);
    push8(w, c, (uint8_t)c->a);
    hw_stack(t, c->s);
    rep(c, PORT_P_M);
    const uint16_t size = bus_r16(w, rom, (src + c->y) & 0xffffffu);
    c->y = (uint16_t)(c->y + 2u);
    c->x = size;
    const uint16_t dest = bus_r16(w, rom, (src + c->y) & 0xffffffu);
    c->a = dest;
    c->y = (uint16_t)(c->y + 2u);
    set_nz16(c, c->y);
    hw_w16(t, 0x2142, dest);

    // `$2141` is 1 for a block to write and 0 for the jump that ends it all,
    // and `ADC #$7F` turns the same bit into V for the `BVS` below.
    hw_run(t, IP_FLAG);
    sep(c, PORT_P_M);
    cmp16(c, c->x, 1);
    const bool more = flag(c, PORT_P_C);
    lda8(c, 0);
    set_c(c, false);
    lda8(c, more ? 1 : 0);
    hw_w8(t, 0x2141, (uint8_t)c->a);
    hw_run(t, IP_KICK);
    adc8(c, 0x7f);
    lda8(c, pull8(w, c));
    hw_stack(t, c->s);
    hw_w8(t, 0x2140, (uint8_t)c->a);
    hw_wait8(t, 0x2140, (uint8_t)c->a);
    matched(c);
    if (!flag(c, PORT_P_V)) break;

    // `$80:CB77`: the first byte goes with a counter of zero.
    PORT_COVER(ipl_block);
    hw_run(t, IP_FIRST);
    lda8(c, bus_r8(w, rom, (src + c->y) & 0xffffffu));
    c->y = (uint16_t)(c->y + 1u);
    xba(c);
    lda8(c, 0);
    rep(c, PORT_P_M);
    hw_w16(t, 0x2140, c->a);
    for (;;) {
      // `SEP #$20 : DEX : BNE`. X is 16 bits wide here.
      sep(c, PORT_P_M);
      c->x = (uint16_t)(c->x - 1u);
      set_nz16(c, c->x);
      if (c->x == 0) break;
      // `$80:CB7F`: the next byte under the counter, once the SPC has echoed
      // the last one.
      hw_run(t, IP_NEXT);
      xba(c);
      lda8(c, bus_r8(w, rom, (src + c->y) & 0xffffffu));
      c->y = (uint16_t)(c->y + 1u);
      xba(c);
      hw_wait8(t, 0x2140, (uint8_t)c->a);
      matched(c);
      hw_run(t, IP_SEND);
      lda8(c, (uint8_t)(c->a + 1u));
      rep(c, PORT_P_M);
      hw_w16(t, 0x2140, c->a);
    }
    // `$80:CB94`: the last byte's echo, then the next kick is the counter
    // plus four, and never zero. No site on the loop: it runs only when a
    // block's last counter is `$FC`, and the driver's two blocks end on `$B6`
    // and `$C2`, so no input reaches it.
    hw_run(t, IP_ENDB);
    hw_wait8(t, 0x2140, (uint8_t)c->a);
    matched(c);
    hw_run(t, IP_ADC);
    adc8(c, 3);
    while (flag(c, PORT_P_Z)) {
      hw_run(t, IP_BEQ_T);
      hw_run(t, IP_ADC);
      adc8(c, 3);
    }
    hw_run(t, IP_BEQ_N);
  }
  PORT_COVER(ipl_done);
  hw_run(t, IP_DONE);
  set_p(c, pull8(w, c));
  (void)pull16(w, c);
  hw_stack(t, c->s);
}

void apu_boot_traced(Wram* w, const Rom* rom, PortCpu* c, HwTrace* t) {
  hw_run(t, AB_HEAD);
  rep(c, PORT_P_M | PORT_P_X);
  push8(w, c, c->db);
  hw_stack(t, c->s);
  c->a = 0x7fffu;
  c->x = 0x8000u;
  c->y = 0;
  set_nz16(c, 0);
  mvn(w, rom, c, t, 0x7f, 0x91);
  hw_run(t, AB_MID);
  c->a = 0x1a85u;
  c->x = 0xba3du;
  set_nz16(c, c->x);
  mvn(w, rom, c, t, 0x7f, 0x95);

  // `PLB`, then the cursor pointed at the copy, from the first entry of the
  // set table, and `JSR $CB61`.
  hw_run(t, AB_POINT);
  c->db = pull8(w, c);
  set_nz8(c, c->db);
  sep(c, PORT_P_M);
  rep(c, PORT_P_X);
  lda8(c, 0);
  wram_w8(w, W_APU_SEQ, 0);
  for (uint32_t i = 0; i < 3; i++) {
    uint32_t avail = 0;
    const uint8_t* p = rom_ptr(rom, APU_SET_TABLE + i, &avail);
    lda8(c, p && avail ? *p : 0);
    wram_w8(w, W_APU_SRC + i, (uint8_t)c->a);
  }
  push16(w, c, 0xcb4f);
  hw_stack(t, c->s);
  ipl_upload(w, rom, c, t);

  // `LDA #$00` into all four ports, and `REP #$30 : RTL`.
  hw_run(t, AB_ZERO);
  lda8(c, 0);
  for (uint16_t port = 0x2140; port < 0x2144; port++) {
    if (port != 0x2140) hw_run(t, AS_STORE);
    hw_w8(t, port, 0);
  }
  hw_run(t, AB_REP);
  rep(c, PORT_P_M | PORT_P_X);
  c->pc = APU_BOOT_EXIT;
}
