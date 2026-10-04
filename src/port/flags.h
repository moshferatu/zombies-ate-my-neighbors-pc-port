// Carry and overflow, for the ports that owe them to a thread.
//
// A state body is entered by a computed `RTS` and comes back to its thread's
// loop, which sooner or later calls `thread_yield`. That opens with `PHP`, so
// whatever carry and overflow the body left are parked in the thread's status
// byte, and handed back when it resumes. A port of such a body has to leave
// the two as the ROM would. Everything else in the registers is overwritten
// before anything reads it.
//
// So these do the ROM's arithmetic and remember what it did to the two flags.
// A body that wrote neither leaves the thread's own, which `c_set` and `v_set`
// say.
//
// Port code: libc only.

#ifndef PORT_FLAGS_H
#define PORT_FLAGS_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
  bool c, v;
  bool c_set, v_set;
} PortFlags;

static inline void flags_carry(PortFlags* f, bool c) {
  f->c = c;
  f->c_set = true;
}

static inline void flags_overflow(PortFlags* f, bool v) {
  f->v = v;
  f->v_set = true;
}

// Overflow is some callee's now, which the port does not follow.
static inline void flags_overflow_unknown(PortFlags* f) { f->v_set = false; }

// `ADC`, with whatever carry `carry_in` says came before it.
static inline uint16_t flags_adc(PortFlags* f, uint16_t a, uint16_t b,
                                 bool carry_in) {
  const uint32_t sum = (uint32_t)a + b + (carry_in ? 1u : 0u);
  flags_carry(f, sum > 0xffffu);
  flags_overflow(f, ((a ^ sum) & (b ^ sum) & 0x8000u) != 0);
  return (uint16_t)sum;
}

// `CLC : ADC`.
static inline uint16_t flags_add(PortFlags* f, uint16_t a, uint16_t b) {
  return flags_adc(f, a, b, false);
}

// `SEC : SBC`.
static inline uint16_t flags_sub(PortFlags* f, uint16_t a, uint16_t b) {
  const uint16_t r = (uint16_t)(a - b);
  flags_carry(f, a >= b);
  flags_overflow(f, ((a ^ b) & (a ^ r) & 0x8000u) != 0);
  return r;
}

// `CMP`: is `a` at least `b`?
static inline bool flags_at_least(PortFlags* f, uint16_t a, uint16_t b) {
  flags_carry(f, a >= b);
  return a >= b;
}

// `CMP : BEQ`.
static inline bool flags_same(PortFlags* f, uint16_t a, uint16_t b) {
  flags_carry(f, a >= b);
  return a == b;
}

// `ASL`.
static inline uint16_t flags_double(PortFlags* f, uint16_t a) {
  flags_carry(f, (a & 0x8000u) != 0);
  return (uint16_t)(a << 1);
}

#endif
