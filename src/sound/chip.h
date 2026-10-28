// What the loader and the driver are given of the chip: the clock, the ports,
// timer 0, the DSP and the RAM, each at the cycle the instruction the C stands
// for would have used it. Private to `src/sound`.
//
// The convention: `s->now` is where the current instruction begins. The C
// spends an instruction's cycles after doing what it does, and an access that
// matters to the cycle takes its offset into the instruction, `at`: a store
// writes on its last cycle and a load reads on its last, so `MOV $F3, A` (four
// cycles) writes at 3 and `MOV A, $FD` (three) reads at 2. (A store reads its
// target the cycle before it writes it. Nothing the driver stores to minds.)
// A port is the exception: the game sees it whole instructions at a time, so
// the instruction's start is what counts and there is no `at`.

#ifndef SOUND_CHIP_H
#define SOUND_CHIP_H

#include "sound/sound.h"

static inline void spend(Sound* s, int cycles) { s->now += (uint64_t)cycles; }

// The DSP's ticks up to and including cycle `c`.
void chip_sync(Sound* s, uint64_t c);

// The current instruction reads a port. False if the game's side is not known
// as far as this cycle yet: then the program stops here, to be resumed.
bool chip_port_in(Sound* s, int port, uint8_t* val);
// ...writes one.
void chip_port_out(Sound* s, int port, uint8_t val);

// The DSP's registers, through `$F2` and `$F3`.
void chip_dsp_write(Sound* s, int at, uint8_t reg, uint8_t val);
uint8_t chip_dsp_read(Sound* s, int at, uint8_t reg);

// A byte of RAM the DSP may read: a sample, the directory, the echo's buffer.
void chip_ram_put(Sound* s, int at, uint16_t adr, uint8_t val);

// CONTROL (`$F1`): the timers on and off, and two bits that clear the ports
// the game writes.
void chip_control(Sound* s, int at, uint8_t val);
// T0TARGET (`$FA`), and T0OUT (`$FD`), which is cleared by reading it.
void chip_t0_target(Sound* s, uint8_t val);
uint8_t chip_t0_read(Sound* s, int at);

// Tells the test build where the program is.
static inline void chip_at(Sound* s, uint16_t adr) {
  if (s->watch) s->watch(s->watch_ctx, SOUND_SEEN_AT, adr, 0, s->now);
}
// ...with a register's value there.
static inline void chip_mark(Sound* s, uint16_t adr, uint8_t reg) {
  if (s->watch) s->watch(s->watch_ctx, SOUND_SEEN_AT, adr, reg, s->now);
}

// Somewhere the C was not written to go. The program stops for good.
void chip_halt(Sound* s, const char* why);

// The driver (`driver.c`). It begins at `$0600` and stops at its reads of the
// ports; this goes on from wherever it stopped, until it stops again.
void driver_run(Sound* s);

#endif
