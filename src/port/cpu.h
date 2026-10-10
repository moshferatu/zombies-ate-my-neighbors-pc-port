// The whole 65816, for the stretches of the ROM that leave by a jump.
//
// Most of `src/port/` is routines that are called and return, and a C
// function's arguments and result are all the machine they need. The
// scheduler, the NMI and the thread bodies are not like that: each is a run of
// instructions from one address to the next place control leaves it, and what
// it hands on is the whole register set. This is that register set, and the
// handful of instructions every one of those ports is made of, written once.
//
// The stack is WRAM. Every thread's, the scheduler's and the NMI's are in bank
// 0 under `$2000`, which is WRAM's first 8 KB, so `S` is a WRAM offset as it
// stands and a push is a WRAM write.
//
// Decimal mode is not modelled, so `adc16` and `sbc16` are binary only. The
// harness declines a stretch that would use them with it set.
//
// Port code: libc only.

#ifndef PORT_CPU_H
#define PORT_CPU_H

#include <stdbool.h>
#include <stdint.h>

#include "assets/rom.h"
#include "port/wram.h"

// The status register's bits, as `PHP` pushes them.
#define PORT_P_C 0x01
#define PORT_P_Z 0x02
#define PORT_P_I 0x04
#define PORT_P_D 0x08
#define PORT_P_X 0x10  // 8-bit index registers
#define PORT_P_M 0x20  // 8-bit accumulator
#define PORT_P_V 0x40
#define PORT_P_N 0x80

// The whole CPU, in and out. `pc` is the entry on the way in and, on the way
// out, the 24-bit address of the instruction the stretch stopped on, which is
// always one of the ROM's. It is in the entry's bank for every stretch but
// the NMI's first and last, which cross between `$00` and `$80`.
typedef struct {
  uint16_t a, x, y, s, d;
  uint8_t db, p;
  uint32_t pc;
} PortCpu;

static inline void set_nz16(PortCpu* c, uint16_t v) {
  c->p = (uint8_t)(c->p & ~(PORT_P_N | PORT_P_Z));
  if (v & 0x8000u) c->p |= PORT_P_N;
  if (v == 0) c->p |= PORT_P_Z;
}

static inline void set_nz8(PortCpu* c, uint8_t v) {
  c->p = (uint8_t)(c->p & ~(PORT_P_N | PORT_P_Z));
  if (v & 0x80u) c->p |= PORT_P_N;
  if (v == 0) c->p |= PORT_P_Z;
}

static inline void set_c(PortCpu* c, bool on) {
  c->p = (uint8_t)(on ? c->p | PORT_P_C : c->p & ~PORT_P_C);
}

static inline void set_v(PortCpu* c, bool on) {
  c->p = (uint8_t)(on ? c->p | PORT_P_V : c->p & ~PORT_P_V);
}

static inline bool flag(const PortCpu* c, uint8_t bit) {
  return (c->p & bit) != 0;
}

static inline void push8(Wram* w, PortCpu* c, uint8_t v) {
  wram_w8(w, c->s, v);
  c->s = (uint16_t)(c->s - 1);
}

static inline void push16(Wram* w, PortCpu* c, uint16_t v) {
  push8(w, c, (uint8_t)(v >> 8));
  push8(w, c, (uint8_t)v);
}

static inline uint8_t pull8(const Wram* w, PortCpu* c) {
  c->s = (uint16_t)(c->s + 1);
  return wram_r8(w, c->s);
}

static inline uint16_t pull16(const Wram* w, PortCpu* c) {
  uint16_t lo = pull8(w, c);
  return (uint16_t)(lo | (pull8(w, c) << 8));
}

// `PLP`. Setting the index-width bit truncates X and Y on the spot, as the
// hardware does; the accumulator keeps its high byte either way.
static inline void set_p(PortCpu* c, uint8_t p) {
  c->p = p;
  if (p & PORT_P_X) {
    c->x &= 0x00ffu;
    c->y &= 0x00ffu;
  }
}

// 16-bit `ADC`, `SBC` and `CMP`, binary mode. The results are the ROM's to
// the flag: `V` from the signs, `C` as carry out or not-borrow.
static inline uint16_t adc16(PortCpu* c, uint16_t a, uint16_t m) {
  uint32_t r = (uint32_t)a + m + (flag(c, PORT_P_C) ? 1u : 0u);
  set_v(c, ((a ^ r) & (m ^ r) & 0x8000u) != 0);
  set_c(c, r > 0xffffu);
  set_nz16(c, (uint16_t)r);
  return (uint16_t)r;
}

// Would a 16-bit `CLC : ADC` of these two set overflow? For the callable
// ports, which have no `PortCpu` but still owe the caller V.
static inline bool add16_overflows(uint16_t a, uint16_t b) {
  const uint16_t sum = (uint16_t)(a + b);
  return ((a ^ sum) & (b ^ sum) & 0x8000u) != 0;
}

static inline uint16_t sbc16(PortCpu* c, uint16_t a, uint16_t m) {
  return adc16(c, a, (uint16_t)~m);
}

static inline void cmp16(PortCpu* c, uint16_t r, uint16_t m) {
  set_c(c, r >= m);
  set_nz16(c, (uint16_t)(r - m));
}

static inline uint16_t asl16(PortCpu* c, uint16_t v) {
  set_c(c, (v & 0x8000u) != 0);
  v = (uint16_t)(v << 1);
  set_nz16(c, v);
  return v;
}

// `BIT` on a 16-bit operand from memory: N and V are its top two bits, Z is
// whether it shares a bit with A.
static inline void bit16(PortCpu* c, uint16_t m) {
  c->p = (uint8_t)(c->p & ~(PORT_P_N | PORT_P_V | PORT_P_Z));
  if (m & 0x8000u) c->p |= PORT_P_N;
  if (m & 0x4000u) c->p |= PORT_P_V;
  if ((c->a & m) == 0) c->p |= PORT_P_Z;
}

// `TSB` and `TRB` on a 16-bit operand from memory: Z is whether it shares a
// bit with A, and what comes back is the operand with A's bits set, or clear.
static inline uint16_t tsb16(PortCpu* c, uint16_t m) {
  c->p = (uint8_t)((c->a & m) == 0 ? c->p | PORT_P_Z : c->p & ~PORT_P_Z);
  return (uint16_t)(m | c->a);
}

static inline uint16_t trb16(PortCpu* c, uint16_t m) {
  c->p = (uint8_t)((c->a & m) == 0 ? c->p | PORT_P_Z : c->p & ~PORT_P_Z);
  return (uint16_t)(m & ~c->a);
}

// A data read at a 24-bit address, as the bus answers it: the first 8 KB of
// WRAM in every bank below `$40` and in `$80-$BF`, WRAM itself in `$7E-$7F`,
// and the cartridge wherever `rom.h` says it is. Anything else -- the PPU, the
// DMA registers -- is not something a port reads, and comes back 0; the
// guards keep the stretches that use this away from it.
static inline uint8_t bus_r8(const Wram* w, const Rom* rom, uint32_t addr24) {
  const uint8_t bank = (uint8_t)(addr24 >> 16);
  const uint16_t off = (uint16_t)addr24;
  if (bank == 0x7e || bank == 0x7f)
    return wram_r8(w, (uint32_t)(bank - 0x7e) << 16 | off);
  if ((bank & 0x40) == 0 && off < 0x2000) return wram_r8(w, off);
  uint32_t avail = 0;
  const uint8_t* p = rom_ptr(rom, addr24, &avail);
  return p && avail ? *p : 0;
}

// ...and a word, whose second byte is the next 24-bit address: the 65816
// carries an indexed address into the next bank rather than wrapping it.
static inline uint16_t bus_r16(const Wram* w, const Rom* rom, uint32_t addr24) {
  return (uint16_t)(bus_r8(w, rom, addr24) |
                    bus_r8(w, rom, (addr24 + 1) & 0xffffffu) << 8);
}

// Whether a data byte at `addr24` costs 2 more while `$420D` is clear: the
// cartridge through `$80-$FF`. WRAM costs 8 always, and a ROM byte through
// `$00-$7D` costs 8 always.
static inline bool bus_fast(uint32_t addr24) {
  const uint8_t bank = (uint8_t)(addr24 >> 16);
  const uint16_t off = (uint16_t)addr24;
  if (bank < 0x80) return false;
  if (bank >= 0xc0) return true;
  return off >= 0x8000;
}

#endif
