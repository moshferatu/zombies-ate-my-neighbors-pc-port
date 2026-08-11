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

void wave_hdma_build(Wram* w, const Rom* rom, uint16_t dp, uint16_t in_x,
                     uint16_t in_y, bool in_c, WaveRegs* out) {
  const uint16_t len = wram_r16(w, (uint16_t)(dp + WAVE_DP_LENGTH));

  // `LDA $70 : BMI $95DA`. The effect is over and the caller's `BPL` is about
  // to notice; nothing is written and carry is not touched.
  if (len & 0x8000u) {
    PORT_COVER(wave_over);
    out->a = len;
    out->x = in_x;
    out->y = in_y;
    out->n = true;
    out->z = false;
    out->c = in_c;
    return;
  }
  PORT_COVER(wave_building);

  uint16_t phase = wave_wrap((uint16_t)(wram_r16(w, (uint16_t)(dp + WAVE_DP_PHASE)) + 1));
  wram_w16(w, (uint16_t)(dp + WAVE_DP_PHASE), phase);

  // `LDA #$00F8 : STA $7E8000,X` at X = 0. The high half is the first
  // parameter's low byte a moment later.
  wram_w16(w, W_WAVE_HDMA, WAVE_REPEAT_HEADER);

  uint16_t x = 1, deg = phase, a = 0;
  do {
    deg = wave_wrap((uint16_t)(deg + WAVE_DEGREES_PER_LINE));

    SinRegs s;
    sin_deg(rom, deg, x, &s);
    // `BIT #$8000 : BEQ +2 : ORA #$FF00`, which cannot change a bit.
    a = s.a;
    wram_w16(w, (uint16_t)(W_WAVE_HDMA + x), a);
    x = (uint16_t)(x + 2);

    if (x == WAVE_SECOND_HEADER) {
      PORT_COVER(wave_second_header);
      // `STA $7E7FFF,X`, so the low byte lands one below the header and takes
      // the high byte of the parameter just written with it.
      wram_w16(w, (uint16_t)(W_WAVE_HDMA - 1 + x), WAVE_HEADER_FIXUP);
      x++;
    }
  } while (x < len);

  wram_w16(w, (uint16_t)(W_WAVE_HDMA + x), 0);

  out->x = x;
  out->y = deg;
  out->c = true;  // `CMP #$0000` against a value that cannot be below it

  // `CMP #$0000 : BNE` — A is still the last sine, carried past the terminator
  // store by `PHA`/`PLA`.
  if (a != 0) {
    PORT_COVER(wave_off_axis);
    out->a = a;
    out->n = (a & 0x8000u) != 0;
    out->z = false;
    return;
  }

  const uint16_t hold = wram_r16(w, (uint16_t)(dp + WAVE_DP_HOLD));
  if (hold != 0) {
    PORT_COVER(wave_hold);
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
  uint16_t shorter = (uint16_t)(len - 2);
  wram_w16(w, (uint16_t)(dp + WAVE_DP_LENGTH), shorter);
  out->a = 0;
  out->n = (shorter & 0x8000u) != 0;
  out->z = shorter == 0;
}
