// The hardware registers, for a routine whose work is writing them.
//
// A vblank job does little else. `$80:B947` sends each sprite's tiles to VRAM
// by filling in DMA channel 0 and starting it, then sends the whole OAM the
// same way, and nearly every instruction in it is a store to `$21xx` or
// `$43xx`. There is nothing in WRAM to compare, and the machine is only right
// if each byte reaches its register in the ROM's order, and each DMA starts on
// the ROM's cycle: a transfer holds the CPU for as long as it runs, and where
// it starts decides where everything after it falls.
//
// So a routine like that does not write the machine. It records what it
// wrote, in order, between the runs of its own instructions: `hw_run` for a
// stretch of the ROM's code, named by a block number the way the other ports
// count theirs, and `hw_w8` for each byte stored to a register. The harness
// prices the runs, so each write has the cycle the ROM's store reaches the bus
// on, and makes the writes on those cycles while the budget is spent. `verify`
// compares every one of them, address, value and cycle, against the ROM's.
//
// A run ends where a store's write begins. A store's own cycles before that,
// its opcode and operand fetches, belong to the run in front of it. A 16-bit
// store is two writes, low byte first, and `hw_w16` records both.
//
// Port code: libc only.

#ifndef PORT_HW_H
#define PORT_HW_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
  HW_RUN,    // `arg` is a block number
  HW_WRITE,  // `arg` is a register, `$2100-$21FF` or `$4200-$437F`
} HwKind;

typedef struct {
  uint16_t arg;
  uint8_t kind;
  uint8_t val;
} HwStep;

// `$80:B947` at its fullest records about 1,300 steps: 64 sprites of two
// transfers each.
#define HW_TRACE_MAX 4096

typedef struct {
  int n;
  bool full;  // a step did not fit; the harness refuses such a trace
  HwStep step[HW_TRACE_MAX];
} HwTrace;

static inline void hw_step(HwTrace* t, HwKind kind, uint16_t arg, uint8_t val) {
  if (t->n == HW_TRACE_MAX) {
    t->full = true;
    return;
  }
  t->step[t->n].arg = arg;
  t->step[t->n].kind = (uint8_t)kind;
  t->step[t->n].val = val;
  t->n++;
}

static inline void hw_run(HwTrace* t, int block) {
  hw_step(t, HW_RUN, (uint16_t)block, 0);
}

static inline void hw_w8(HwTrace* t, uint16_t reg, uint8_t v) {
  hw_step(t, HW_WRITE, reg, v);
}

static inline void hw_w16(HwTrace* t, uint16_t reg, uint16_t v) {
  hw_w8(t, reg, (uint8_t)v);
  hw_w8(t, (uint16_t)(reg + 1), (uint8_t)(v >> 8));
}

#endif
