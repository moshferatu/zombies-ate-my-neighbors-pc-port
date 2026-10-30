// Whether this build has an emulated 65816.
//
// `zamn` has none: `cpu/none.c` stands where LakeSnes's `cpu.c` is, keeping
// the registers, the reset and the wait for an interrupt, and running no
// instruction. Every instruction of the game is a port's. `zamn_with_ppu`
// links the emulated one (`cpu/emulated.c`), and so `--stock` and `-r`.
#ifndef ZAMN_CPU_KIND_H
#define ZAMN_CPU_KIND_H

#include <stdbool.h>
#include <stdint.h>

// True when the 65816 can run an instruction no port runs.
bool cpu_emulated(void);

// What to do, before the game stops, when it reaches an instruction no port
// runs and there is no 65816 to run it: told the address. The build with a
// 65816 never calls it.
typedef void (*CpuUnportedFn)(uint32_t pc);
void cpu_on_unported(CpuUnportedFn fn);

#endif
