// The 65816's decimal-mode `ADC`, 16 bits wide.
//
// This lived inside `port/score.c` while the score was the only decimal
// arithmetic in the game. It is not: `$80:F87B` adds a pickup amount to an
// inventory counter with `SED` on too, and the two routines have nothing to do
// with each other beyond both running on the same CPU. So it moves here, where
// what it models is a *processor behaviour* rather than a rule about scores.
//
// Written out nibble by nibble rather than as "unpack the digits, add, repack"
// because those two are not the same function. The hardware's carry propagation
// is defined for operands that are not valid BCD at all — `$0A + $01` is `$11`,
// not `$0B` — and the diff compares the word that lands in WRAM. Both counters
// stay valid BCD in practice, so this is a difference that should never show
// up; the point is that if it ever does, it will be the ROM's answer and not a
// plausible-looking approximation of it.
//
// Port code: libc only.

#ifndef PORT_BCD_H
#define PORT_BCD_H

#include <stdbool.h>
#include <stdint.h>

// `carry` is both the carry in (the `CLC`/`SEC` before the `ADC`) and the carry
// out. `adjusted` is set — never cleared — when some digit ran past 9 and the
// decimal correction actually changed the answer.
//
// That last output exists for the coverage report and it is the caller's to
// name, because what it means depends on the caller: an award large enough to
// need it is a different event from a pickup large enough to need it. Without
// somebody marking it, the whole of this function could be replaced by `a +
// value` and every diff would still pass — which is exactly the kind of gap
// `port/coverage.h` exists to make visible.
//
// It is no longer a gap on both sides. `movies/level1-pickups.zmv` pays $0099
// into an ammo counter holding $0150, which carries the tens digit, so
// `pickup_digit_carry` is taken and deleting the second line of the adjust
// below fails on that call at `$7E:1CCC` — ROM $49, port $E9. `score_add`'s
// side of it is still untaken: no award any movie makes needs the correction.
static inline uint16_t bcd_add16(uint16_t a, uint16_t value, bool* carry,
                                 bool* adjusted) {
  int result = (a & 0xf) + (value & 0xf) + (*carry ? 1 : 0);
  if (result > 0x9) { result = ((result + 0x6) & 0xf) + 0x10; *adjusted = true; }
  result = (a & 0xf0) + (value & 0xf0) + result;
  if (result > 0x9f) { result = ((result + 0x60) & 0xff) + 0x100; *adjusted = true; }
  result = (a & 0xf00) + (value & 0xf00) + result;
  if (result > 0x9ff) { result = ((result + 0x600) & 0xfff) + 0x1000; *adjusted = true; }
  result = (a & 0xf000) + (value & 0xf000) + result;
  if (result > 0x9fff) { result += 0x6000; *adjusted = true; }
  *carry = result > 0xffff;
  return (uint16_t)result;
}

#endif
