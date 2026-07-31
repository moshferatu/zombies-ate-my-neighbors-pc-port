#include "port/rng.h"

#include "port/coverage.h"

void rng_next(Wram* w, bool carry_in, RngResult* out) {
  // `LDA $0024 : ROL $0024 : EOR $0024 : ROR $0024`, all eight-bit. The two
  // rotates are a read-modify-write on the same byte, so the second one sees
  // what the first left; the `EOR` in between is what makes this a shift
  // register rather than a shift.
  //
  // What `$0024` holds after the `ROR` is never read — the `STA` at the end
  // overwrites it — so the only thing the second rotate contributes is its
  // carry-out, which is bit 0 of what the `ROL` produced.
  uint8_t state = wram_r8(w, W_RNG_STATE);
  uint8_t rolled = (uint8_t)((state << 1) | (carry_in ? 1 : 0));
  uint8_t a = (uint8_t)(state ^ rolled);
  bool c = (rolled & 1) != 0;

  // `INC $0025 : ADC $0025`. A plain counter, and the addend in the same
  // instruction — so the sequence is a shift register stirred by a clock rather
  // than a pure LFSR, and it cannot get stuck on zero the way one can.
  uint8_t counter = (uint8_t)(wram_r8(w, W_RNG_COUNTER) + 1);
  unsigned sum = (unsigned)a + counter + (c ? 1 : 0);
  uint8_t result = (uint8_t)sum;

  // `BVC +3 : INC $0025` — the counter advances *twice* on a call where the
  // addition overflowed as a signed one. This is the only routine in the port
  // that reads V, and the only one that publishes it; see `RngResult`.
  bool v = (((uint8_t)(a ^ result) & (uint8_t)(counter ^ result)) & 0x80) != 0;
  PORT_COVER_IF(v, rng_counter_twice, rng_counter_once);
  if (v) counter = (uint8_t)(counter + 1);

  wram_w8(w, W_RNG_COUNTER, counter);
  wram_w8(w, W_RNG_STATE, result);

  // `REP #$20 : AND #$00FF`. The mask is the last flag-setter and it is a 16-bit
  // one: N is bit 15 of a value whose high byte was just cleared, so N comes back
  // clear however negative the byte looks. Carry is the `ADC`'s, which neither
  // instruction touches.
  out->a = result;
  out->n = false;
  out->z = result == 0;
  out->c = sum > 0xff;
  out->v = v;
}
