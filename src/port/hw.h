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
// ## Waiting on the SPC700
//
// The sound routines also *read* a register, and only to wait: `CPY $2143 :
// BNE` goes round until the SPC700 has echoed the last command back. How many
// times is the SPC's business, and the port cannot know it. It does not need
// to, because what the routine does after the wait does not depend on how
// long the wait was. So the port records the wait itself, `hw_wait8` or
// `hw_wait16`: the register, and the value that ends it. The harness makes
// the reads on the ROM's cycles while the budget is spent, and each read the
// value does not match costs one more time round the loop, which moves
// everything after it later by exactly that much.
//
// A wait is the whole loop, compare and branch, from the compare's first
// fetch to the end of the branch that falls through. The runs either side of
// it leave both instructions out.
//
// ## The NMI's two reads
//
// The NMI handler reads two registers, and neither for a value it keeps.
// `LDA $4210` acknowledges the interrupt: reading it is what lets the next
// one happen. `hw_read8` records a read like that, and the harness makes it
// on the ROM's cycle. `LDA $4212 : LSR : BCS` waits for the joypads to have
// been read, going round until bit 0 is clear, and `hw_wait_low` records it
// as the other waits are recorded: the whole loop, with the runs either side
// leaving all three instructions out.
//
// ## Where the stack is
//
// An upload runs for frames and the NMI lands inside it, where the ROM is a
// `JSR` or a `PHP` deep. The handler saves the stack pointer it finds, and
// `thread_spawn` later copies that into a thread's page, so a port that let
// the NMI land at its entry's depth would leave a different number behind.
// `hw_stack` records the stack pointer after each push or pull, and a burn
// that stops for an interrupt puts it there first.
//
// Port code: libc only.

#ifndef PORT_HW_H
#define PORT_HW_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
  HW_RUN,     // `arg` is a block number and `val` how many times it runs
  HW_WRITE,   // `arg` is a register, `$2100-$21FF` or `$4200-$437F`
  HW_WAIT8,   // `CMP`/`CPY abs : BNE` on the byte at `arg`, until it is `val`
  HW_WAIT16,  // ...and a 16-bit `CMP`, on the word at `arg`
  HW_STACK,   // the stack pointer is now `val`
  HW_READ,    // `LDA abs` of the register at `arg`, for what reading it does
  HW_WAIT_LOW,  // `LDA abs : LSR : BCS` on the byte at `arg`, until bit 0 is clear
} HwKind;

typedef struct {
  uint16_t arg;
  uint16_t val;
  uint8_t kind;
} HwStep;

// `$80:B947` at its fullest records about 1,300 steps: 64 sprites of two
// transfers each. The sound uploads need far more, and bring their own.
#define HW_TRACE_MAX 4096

// `step` is the caller's, `cap` entries long.
typedef struct {
  int n, cap;
  bool full;  // a step did not fit; the harness refuses such a trace
  HwStep* step;
} HwTrace;

static inline void hw_step(HwTrace* t, HwKind kind, uint16_t arg, uint16_t val) {
  if (t->n == t->cap) {
    t->full = true;
    return;
  }
  t->step[t->n].arg = arg;
  t->step[t->n].kind = (uint8_t)kind;
  t->step[t->n].val = val;
  t->n++;
}

static inline void hw_run(HwTrace* t, int block) {
  hw_step(t, HW_RUN, (uint16_t)block, 1);
}

// The same run `n` times over: an `MVN`, which is one instruction per byte.
static inline void hw_run_n(HwTrace* t, int block, uint16_t n) {
  if (n) hw_step(t, HW_RUN, (uint16_t)block, n);
}

static inline void hw_w8(HwTrace* t, uint16_t reg, uint8_t v) {
  hw_step(t, HW_WRITE, reg, v);
}

static inline void hw_w16(HwTrace* t, uint16_t reg, uint16_t v) {
  hw_w8(t, reg, (uint8_t)v);
  hw_w8(t, (uint16_t)(reg + 1), (uint8_t)(v >> 8));
}

static inline void hw_wait8(HwTrace* t, uint16_t reg, uint8_t v) {
  hw_step(t, HW_WAIT8, reg, v);
}

static inline void hw_wait16(HwTrace* t, uint16_t reg, uint16_t v) {
  hw_step(t, HW_WAIT16, reg, v);
}

static inline void hw_read8(HwTrace* t, uint16_t reg) {
  hw_step(t, HW_READ, reg, 0);
}

static inline void hw_wait_low(HwTrace* t, uint16_t reg) {
  hw_step(t, HW_WAIT_LOW, reg, 0);
}

static inline void hw_stack(HwTrace* t, uint16_t s) {
  hw_step(t, HW_STACK, 0, s);
}

#endif
