// The loops the game waits in, a turn at a time -- see port/hold.h.

#include "port/hold.h"

#include "port/coverage.h"

const Hold HOLDS_BY[HOLD_COUNT] = {
#define X(name, sym, pc, over_pc, until, at, n) \
  [HOLD_##name] = {pc, over_pc, until, at, n},
    HOLDS(X)
#undef X
};

// `BIT #n` with a 16-bit accumulator: it has no operand in memory to take N
// and V from, so Z is all it sets.
static bool shares_a_bit(PortCpu* c, uint16_t n) {
  const bool shared = (c->a & n) != 0;
  c->p = (uint8_t)(shared ? c->p & ~PORT_P_Z : c->p | PORT_P_Z);
  return shared;
}

static bool turn(const Wram* w, PortCpu* c, const Hold* hold) {
  const uint16_t word = wram_r16(w, hold->at);
  switch (hold->until) {
    case HOLD_TOP_BIT:
      bit16(c, word);
      return (word & 0x8000u) != 0;
    case HOLD_ZERO:
      c->a = word;
      set_nz16(c, c->a);
      return word == 0;
    case HOLD_AT_LEAST:
      c->a = word;
      cmp16(c, c->a, hold->n);
      return word >= hold->n;
    case HOLD_EQUAL:
      c->a = word;
      cmp16(c, c->a, hold->n);
      return word == hold->n;
    case HOLD_ANY_BIT:
      c->a = (uint16_t)(word & hold->n);
      set_nz16(c, c->a);
      return c->a != 0;
    case HOLD_PADS_OFF:
    case HOLD_PADS_ON: {
      const uint16_t other = wram_r16(w, (uint16_t)(hold->at + 2));
      c->a = (uint16_t)(word | other);
      set_nz16(c, c->a);
      const bool on = shares_a_bit(c, hold->n);
      return hold->until == HOLD_PADS_ON ? on : !on;
    }
    default:
      return true;
  }
}

bool hold_turn(const Wram* w, PortCpu* c, const Hold* hold) {
  const bool over = turn(w, c, hold);
  if (over) PORT_COVER(hold_over);
  else PORT_COVER(hold_goes_on);
  c->pc = over ? hold->over_pc : hold->pc;
  return over;
}
