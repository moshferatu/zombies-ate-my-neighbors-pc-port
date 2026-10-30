// The console's CPU with no 65816 in it, for `zamn`.
//
// It defines what LakeSnes's `cpu.h` declares, so the console links against
// it in place of `cpu.c`. The ports run every instruction; the harness takes
// the NMI and waits out a `WAI` itself (`cosim.c`). What is left here is what
// the CPU does that is not an instruction:
//
//   * the registers, which the harness reads and writes, and their share of a
//     saved state, in the same layout, so a quick save loads in either build;
//   * the reset: the bus reads the 65816 makes, which the clock counts, and
//     the vector;
//   * the wait for an interrupt, and an interrupt, on the way the core takes
//     them, should the harness ever leave one to it.
//
// An instruction is the one thing it cannot do. If the game reaches one that
// no port runs, it says where, and stops.
#include <stdio.h>
#include <stdlib.h>

#include "cpu.h"
#include "cpu/kind.h"

static CpuUnportedFn g_unported;

bool cpu_emulated(void) { return false; }

void cpu_on_unported(CpuUnportedFn fn) { g_unported = fn; }

Cpu* cpu_init(void* mem, CpuReadHandler read, CpuWriteHandler write,
              CpuIdleHandler idle) {
  Cpu* cpu = calloc(1, sizeof(Cpu));
  if (!cpu) return NULL;
  cpu->mem = mem;
  cpu->read = read;
  cpu->write = write;
  cpu->idle = idle;
  return cpu;
}

void cpu_free(Cpu* cpu) { free(cpu); }

void cpu_reset(Cpu* cpu, bool hard) {
  if (hard) {
    cpu->a = cpu->x = cpu->y = 0;
    cpu->sp = cpu->pc = cpu->dp = 0;
    cpu->k = cpu->db = 0;
    cpu->c = cpu->z = cpu->v = cpu->n = false;
    cpu->i = cpu->d = cpu->xf = cpu->mf = cpu->e = false;
    cpu->irqWanted = false;
  }
  cpu->waiting = false;
  cpu->stopped = false;
  cpu->nmiWanted = false;
  cpu->intWanted = false;
  cpu->resetWanted = true;
}

// The order is the saved state's, and LakeSnes's.
void cpu_handleState(Cpu* cpu, StateHandler* sh) {
  sh_handleBools(sh, &cpu->c, &cpu->z, &cpu->v, &cpu->n, &cpu->i, &cpu->d,
                 &cpu->xf, &cpu->mf, &cpu->e, &cpu->waiting, &cpu->stopped,
                 &cpu->irqWanted, &cpu->nmiWanted, &cpu->intWanted,
                 &cpu->resetWanted, NULL);
  sh_handleBytes(sh, &cpu->k, &cpu->db, NULL);
  sh_handleWords(sh, &cpu->a, &cpu->x, &cpu->y, &cpu->sp, &cpu->pc, &cpu->dp,
                 NULL);
}

void cpu_nmi(Cpu* cpu) { cpu->nmiWanted = true; }

void cpu_setIrq(Cpu* cpu, bool state) { cpu->irqWanted = state; }

static uint8_t bus_read(Cpu* cpu, uint32_t adr) {
  return cpu->read(cpu->mem, adr);
}

static void bus_idle(Cpu* cpu, bool waiting) { cpu->idle(cpu->mem, waiting); }

static uint16_t vector(Cpu* cpu, uint16_t at) {
  const uint8_t lo = bus_read(cpu, at);
  return (uint16_t)(lo | bus_read(cpu, (uint16_t)(at + 1)) << 8);
}

static void push(Cpu* cpu, uint8_t v) {
  cpu->write(cpu->mem, cpu->sp, v);
  cpu->sp--;
  if (cpu->e) cpu->sp = (uint16_t)((cpu->sp & 0xff) | 0x100);
}

static uint8_t status(const Cpu* cpu) {
  return (uint8_t)(cpu->n << 7 | cpu->v << 6 | cpu->mf << 5 | cpu->xf << 4 |
                   cpu->d << 3 | cpu->i << 2 | cpu->z << 1 | cpu->c);
}

// The reset: three reads where the pushes would be, emulation mode, and the
// vector at $FFFC.
static void reset(Cpu* cpu) {
  cpu->resetWanted = false;
  bus_read(cpu, (uint32_t)cpu->k << 16 | cpu->pc);
  bus_idle(cpu, false);
  for (int i = 0; i < 3; i++) bus_read(cpu, 0x100 | (cpu->sp-- & 0xff));
  cpu->sp = (uint16_t)((cpu->sp & 0xff) | 0x100);
  cpu->e = true;
  cpu->i = true;
  cpu->d = false;
  cpu->mf = cpu->xf = true;
  cpu->x &= 0xff;
  cpu->y &= 0xff;
  cpu->k = 0;
  cpu->pc = vector(cpu, 0xfffc);
}

// An interrupt, as the core takes it from between two instructions.
static void interrupt(Cpu* cpu) {
  bus_read(cpu, (uint32_t)cpu->k << 16 | cpu->pc);
  bus_idle(cpu, false);
  push(cpu, cpu->k);
  push(cpu, (uint8_t)(cpu->pc >> 8));
  push(cpu, (uint8_t)cpu->pc);
  push(cpu, status(cpu));
  cpu->i = true;
  cpu->d = false;
  cpu->k = 0;
  cpu->intWanted = false;
  const bool nmi = cpu->nmiWanted;
  cpu->nmiWanted = false;
  cpu->pc = vector(cpu, nmi ? 0xffea : 0xffee);
}

static void unported(Cpu* cpu) {
  const uint32_t pc = (uint32_t)cpu->k << 16 | cpu->pc;
  fprintf(stderr,
          "zamn: the game reached $%02X:%04X, and no port runs it. This "
          "build has no 65816 to run it, so it stops here.\n",
          (unsigned)(pc >> 16), (unsigned)(pc & 0xffff));
  if (g_unported) g_unported(pc);
  exit(3);
}

void cpu_runOpcode(Cpu* cpu) {
  if (cpu->resetWanted) {
    reset(cpu);
  } else if (cpu->stopped) {
    bus_idle(cpu, true);
  } else if (cpu->waiting) {
    if (cpu->irqWanted || cpu->nmiWanted) {
      cpu->waiting = false;
      bus_idle(cpu, false);
      cpu->intWanted = cpu->nmiWanted || (cpu->irqWanted && !cpu->i);
      bus_idle(cpu, false);
    } else {
      bus_idle(cpu, true);
    }
  } else if (cpu->intWanted) {
    interrupt(cpu);
  } else {
    unported(cpu);
  }
}
