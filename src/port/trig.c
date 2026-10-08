// $80:9C90 and $80:9570 — see port/trig.h.

#include "port/trig.h"

#include "port/coverage.h"

void sin_deg(const Rom* rom, uint16_t deg, uint16_t caller_x, SinRegs* out) {
  // `TAX : LDA $839431,X`, sixteen-bit index. No mask and no range check: the
  // callers reduce, and a caller that did not would read whatever follows the
  // table. Reproduced rather than repaired.
  uint32_t avail = 0;
  const uint8_t* p = rom_ptr(rom, SIN_TABLE + deg, &avail);
  uint8_t byte = (p && avail) ? *p : 0;

  // `CMP #$FF`, eight bits wide, and it is the only thing that touches carry
  // for the rest of the routine.
  out->c = byte >= SIN_SENTINEL;
  if (byte == SIN_SENTINEL) {
    // `LDA #$0080` — sixteen bits by now, so this is +128 and not a byte with
    // its sign bit set. The one index the table cannot hold.
    PORT_COVER(sin_sentinel);
    out->a = SIN_SENTINEL_VALUE;
  } else if (byte & 0x80u) {
    // `BIT #$0080 : ORA #$FF00`. The `BIT` looks at the sixteen-bit A, but the
    // only bit it tests is bit 7, which is the loaded byte's sign.
    PORT_COVER(sin_negative);
    out->a = (uint16_t)(0xff00u | byte);
  } else {
    PORT_COVER(sin_positive);
    out->a = byte;
  }

  // `PLX : RTS`. N and Z are the pull's, so they describe the caller's own X —
  // and since the ROM's callers pass an index below 360, N is always clear.
  out->n = (caller_x & 0x8000u) != 0;
  out->z = caller_x == 0;
}

// --- $80:9570 ---------------------------------------------------------------

// `CPY #$0168 : BCC + : LDY #$0000` — the reduction is a *clamp to zero*, not a
// subtraction, and it is used on two different quantities four instructions
// apart. Both start below 360 and step by 1 or 4, so the two are the same
// thing; written once because they are written once in the ROM's own idiom.
static uint16_t wave_wrap(uint16_t deg) {
  return deg >= WAVE_DEGREES ? 0 : deg;
}

void wave_hdma_build_counted(Wram* w, const Rom* rom, uint16_t dp,
                             uint16_t in_x, uint16_t in_y, bool in_c,
                             WaveRegs* out, WaveWork* work) {
  for (int i = 0; i < WAVE_BLOCK_COUNT; i++) work->blocks[i] = 0;

  const uint16_t len = wram_r16(w, (uint16_t)(dp + WAVE_DP_LENGTH));

  // `LDA $70 : BMI $95DA`. The effect is over and the caller's `BPL` is about
  // to notice; nothing is written and carry is not touched.
  if (len & 0x8000u) {
    PORT_COVER(wave_over);
    work->blocks[WAVE_BLK_OVER]++;
    out->a = len;
    out->x = in_x;
    out->y = in_y;
    out->n = true;
    out->z = false;
    out->c = in_c;
    return;
  }
  PORT_COVER(wave_building);

  const uint16_t stepped =
      (uint16_t)(wram_r16(w, (uint16_t)(dp + WAVE_DP_PHASE)) + 1);
  uint16_t phase = wave_wrap(stepped);
  work->blocks[stepped >= WAVE_DEGREES ? WAVE_BLK_PROLOGUE_WRAP
                                       : WAVE_BLK_PROLOGUE]++;
  wram_w16(w, (uint16_t)(dp + WAVE_DP_PHASE), phase);

  // `LDA #$00F8 : STA $7E8000,X` at X = 0. The high half is the first
  // parameter's low byte a moment later.
  wram_w16(w, W_WAVE_HDMA, WAVE_REPEAT_HEADER);

  uint16_t x = 1, deg = phase, a = 0;
  do {
    const uint16_t advanced = (uint16_t)(deg + WAVE_DEGREES_PER_LINE);
    deg = wave_wrap(advanced);
    work->blocks[advanced >= WAVE_DEGREES ? WAVE_BLK_HEAD_WRAP : WAVE_BLK_HEAD]++;
    work->blocks[WAVE_BLK_ITER]++;

    SinRegs s;
    sin_deg(rom, deg, x, &s);
    // `BIT #$8000 : BEQ +2 : ORA #$FF00`, which cannot change a bit.
    a = s.a;
    // Carry set is the sentinel arm; bit 15 set is the sign-extending one, and
    // it is also what decides the caller's dead `ORA`. So the three arms of
    // `sin_deg` and the two of its caller are one count, not two.
    work->blocks[s.c ? WAVE_BLK_SIN_SENTINEL
                     : (a & 0x8000u) ? WAVE_BLK_SIN_NEGATIVE
                                     : WAVE_BLK_SIN_POSITIVE]++;
    wram_w16(w, (uint16_t)(W_WAVE_HDMA + x), a);
    x = (uint16_t)(x + 2);

    if (x == WAVE_SECOND_HEADER) {
      PORT_COVER(wave_second_header);
      work->blocks[WAVE_BLK_HEADER_FIXUP]++;
      // `STA $7E7FFF,X`, so the low byte lands one below the header and takes
      // the high byte of the parameter just written with it.
      wram_w16(w, (uint16_t)(W_WAVE_HDMA - 1 + x), WAVE_HEADER_FIXUP);
      x++;
    } else {
      work->blocks[WAVE_BLK_HEADER_SKIP]++;
    }
    work->blocks[x < len ? WAVE_BLK_STEP : WAVE_BLK_EXIT]++;
  } while (x < len);

  wram_w16(w, (uint16_t)(W_WAVE_HDMA + x), 0);

  out->x = x;
  out->y = deg;
  out->c = true;  // `CMP #$0000` against a value that cannot be below it

  // `CMP #$0000 : BNE` — A is still the last sine, carried past the terminator
  // store by `PHA`/`PLA`.
  if (a != 0) {
    PORT_COVER(wave_off_axis);
    work->blocks[WAVE_BLK_OFF_AXIS]++;
    out->a = a;
    out->n = (a & 0x8000u) != 0;
    out->z = false;
    return;
  }

  const uint16_t hold = wram_r16(w, (uint16_t)(dp + WAVE_DP_HOLD));
  if (hold != 0) {
    PORT_COVER(wave_hold);
    work->blocks[WAVE_BLK_HOLD]++;
    uint16_t left = (uint16_t)(hold - 1);
    wram_w16(w, (uint16_t)(dp + WAVE_DP_HOLD), left);
    out->a = left;
    out->n = (left & 0x8000u) != 0;
    out->z = left == 0;
    return;
  }

  // `DEC $70 : DEC $70` — one scanline off the bottom, and the flags are the
  // second `DEC`'s. A is the zero the `LDA $76` left.
  PORT_COVER(wave_retract);
  work->blocks[WAVE_BLK_RETRACT]++;
  uint16_t shorter = (uint16_t)(len - 2);
  wram_w16(w, (uint16_t)(dp + WAVE_DP_LENGTH), shorter);
  out->a = 0;
  out->n = (shorter & 0x8000u) != 0;
  out->z = shorter == 0;
}

void wave_hdma_build(Wram* w, const Rom* rom, uint16_t dp, uint16_t in_x,
                     uint16_t in_y, bool in_c, WaveRegs* out) {
  WaveWork ignored;
  wave_hdma_build_counted(w, rom, dp, in_x, in_y, in_c, out, &ignored);
}

// --- $80:9512 and $80:9515 ---------------------------------------------------

void wave_thread_call(Wram* w, PortCpu* c) {
  PORT_COVER(wave_thread_called);
  push16(w, c, (uint16_t)(WAVE_THREAD_CALL_PC + 2));
  c->pc = WAVE_HDMA_ENTRY;
}

void wave_thread_tests(const Wram* w, PortCpu* c, WaveThreadWork* k) {
  for (int pad = 0; pad < 2; pad++) {
    c->a = wram_r16(w, (uint16_t)(W_WAVE_PADS + 2 * pad));
    cmp16(c, c->a, WAVE_PAD_START);
    k->blocks[WT_PAD]++;
    if (c->a == WAVE_PAD_START) {
      PORT_COVER(wave_thread_start);
      k->blocks[WT_TAKEN]++;
      c->pc = WAVE_THREAD_END_PC;
      return;
    }
  }
  c->a = wram_r16(w, (uint16_t)(c->d + WAVE_DP_LENGTH));
  set_nz16(c, c->a);
  k->blocks[WT_LENGTH]++;
  if (c->a & 0x8000u) {
    PORT_COVER(wave_thread_over);
    c->pc = WAVE_THREAD_END_PC;
    return;
  }
  PORT_COVER(wave_thread_again);
  k->blocks[WT_TAKEN]++;
  c->a = WAVE_THREAD_YIELD_TICKS;
  set_nz16(c, c->a);
  k->blocks[WT_AGAIN]++;
  c->pc = WAVE_THREAD_YIELD_PC;
}
