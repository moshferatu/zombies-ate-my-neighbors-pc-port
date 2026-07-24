#include "cosim/cosim.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "analysis/movie.h"

// One in-flight call. Calls nest — an NMI can land inside a routine and the
// handler can call another registered routine — so these live on a stack.
typedef struct {
  int index;          // which registry entry
  uint16_t entry_sp;  // SP as the routine was entered
  uint16_t min_sp;    // lowest SP seen during the call — see dead_stack()
  uint32_t ret_pc;    // where it will return to, read off the stack at entry
  uint64_t cycles;    // core cycle count at entry
  CosimRegs in;
  Wram* before;  // WRAM as the routine found it
} CosimCall;

#define COSIM_MAX_DEPTH 8

struct CosimPriv {
  CosimCall stack[COSIM_MAX_DEPTH];
  int depth;
  Wram* scratch;  // where the C port runs during a verify
  // Native mode only: bytes a substituted call is expected to have left stale,
  // because the ROM's version would have pushed there and the port has no stack
  // in WRAM at all. See cosim_stale().
  uint8_t* stale;
};

// ---------------------------------------------------------------------------
// Reading the CPU
// ---------------------------------------------------------------------------

// The stack lives in bank 0, and bank 0 below $2000 is the WRAM mirror. Every
// stack ZAMN uses — the boot stack at $01FF, the per-thread stacks in
// $10xx-$12xx, the scheduler's at $125F and NMI's at $129F — is inside it, so
// reading `snes->ram` directly is both correct and free of the side effects
// `snes_read()` has on hardware registers.
static uint8_t stack_r8(Snes* snes, uint16_t addr) {
  return addr < 0x2000 ? snes->ram[addr] : snes_read(snes, addr);
}

static void regs_capture(Snes* snes, CosimRegs* r) {
  Cpu* cpu = snes->cpu;
  r->a = cpu->a;
  r->x = cpu->x;
  r->y = cpu->y;
  r->n = cpu->n;
  r->z = cpu->z;
  r->c = cpu->c;
  r->flags = COSIM_FLAG_N | COSIM_FLAG_Z | COSIM_FLAG_C;
}

// Where a routine will return to, from the return address its caller pushed.
static uint32_t return_pc(Snes* snes, CosimReturn kind) {
  Cpu* cpu = snes->cpu;
  uint16_t sp = cpu->sp;
  uint16_t lo = stack_r8(snes, (uint16_t)(sp + 1));
  uint16_t hi = stack_r8(snes, (uint16_t)(sp + 2));
  uint16_t off = (uint16_t)(((hi << 8) | lo) + 1);
  uint8_t bank = kind == COSIM_RTL ? stack_r8(snes, (uint16_t)(sp + 3)) : cpu->k;
  return ((uint32_t)bank << 16) | off;
}

static uint16_t return_sp(uint16_t entry_sp, CosimReturn kind) {
  return (uint16_t)(entry_sp + (kind == COSIM_RTL ? 3 : 2));
}

// Only interpose at a clean instruction boundary — the same gate the Phase 2
// verifiers use.
static bool at_instruction(Snes* snes) {
  Cpu* cpu = snes->cpu;
  return !cpu->resetWanted && !cpu->stopped && !cpu->waiting && !cpu->intWanted;
}

static uint32_t cpu_pc24(Snes* snes) {
  return ((uint32_t)snes->cpu->k << 16) | snes->cpu->pc;
}

// ---------------------------------------------------------------------------
// Setup
// ---------------------------------------------------------------------------

void cosim_init(Cosim* c, Snes* snes, CosimMode mode) {
  memset(c, 0, sizeof *c);
  c->snes = snes;
  c->rom.data = snes->cart->rom;
  c->rom.size = snes->cart->romSize;
  c->mode = mode;

  int count = 0;
  const CosimRoutine* all = cosim_routines(&count);
  c->stat_count = count;
  for (int i = 0; i < count; i++) {
    c->stats[i].routine = &all[i];
    c->stats[i].cycles_min = -1;
  }

  c->priv = (CosimPriv*)calloc(1, sizeof(CosimPriv));
  // Verify mode needs somewhere to rewind to and somewhere to run; native mode
  // works on the emulator's memory in place and needs neither.
  if (mode == COSIM_VERIFY) {
    c->priv->scratch = (Wram*)malloc(sizeof(Wram));
    for (int i = 0; i < COSIM_MAX_DEPTH; i++)
      c->priv->stack[i].before = (Wram*)malloc(sizeof(Wram));
  } else {
    c->priv->stale = (uint8_t*)calloc(WRAM_SIZE, 1);
  }
}

void cosim_free(Cosim* c) {
  if (!c->priv) return;
  free(c->priv->scratch);
  free(c->priv->stale);
  for (int i = 0; i < COSIM_MAX_DEPTH; i++) free(c->priv->stack[i].before);
  free(c->priv);
  c->priv = NULL;
}

bool cosim_enable(Cosim* c, const char* name) {
  int count = 0;
  const CosimRoutine* all = cosim_routines(&count);
  for (int i = 0; i < count; i++) {
    if (!strcmp(all[i].name, name)) {
      c->enabled |= 1u << i;
      return true;
    }
  }
  return false;
}

void cosim_enable_all(Cosim* c) {
  int count = 0;
  cosim_routines(&count);
  c->enabled = count >= 32 ? 0xffffffffu : (1u << count) - 1u;
}

// ---------------------------------------------------------------------------
// The diff
// ---------------------------------------------------------------------------

static bool excluded(const CosimRoutine* r, uint32_t off) {
  for (int i = 0; i < r->exclude_count; i++)
    if (off >= r->excludes[i].offset &&
        off < r->excludes[i].offset + r->excludes[i].length)
      return true;
  return false;
}

// `$7E:1234` / `$7F:0100` for a WRAM offset.
static const char* wram_str(uint32_t off, char* buf, int size) {
  snprintf(buf, size, "$%02X:%04X", off < 0x10000 ? 0x7e : 0x7f, off & 0xffff);
  return buf;
}

static void note(CosimStat* s, const char* fmt, ...) {
  if (s->failed) return;  // keep the first divergence, not the last
  s->failed = true;
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(s->detail, sizeof s->detail, fmt, ap);
  va_end(ap);
}

// Stack the ROM routine scribbled on and then abandoned.
//
// A 65816 routine that pushes anything leaves the pushed bytes behind: `PHY`
// writes them, `PLA` reads them back, and nothing erases them. The C port has
// no stack in WRAM at all, so those bytes are guaranteed to differ, and they
// are guaranteed not to matter — they sit below the stack pointer the caller
// will resume with, which makes them free space by definition.
//
// The window is derived rather than declared: it runs from the deepest the
// stack pointer got during the call up to where it started. A routine that
// pushed nothing gets an empty window and no leeway at all, and nothing outside
// the few bytes a routine actually touched is ever waved through.
static bool dead_stack(const CosimCall* call, uint32_t off) {
  return off > (uint32_t)call->min_sp && off <= (uint32_t)call->entry_sp;
}

// Diff the port's result against the ROM's. `theirs` is the emulator's WRAM,
// which at this point holds what the ROM routine produced.
static void compare(CosimStat* s, const CosimCall* call, const Wram* ours,
                    const Wram* theirs, const CosimRegs* rom_regs,
                    const CosimRegs* our_regs) {
  const CosimRoutine* r = s->routine;
  char buf[32];

  for (uint32_t off = 0; off < WRAM_SIZE; off++) {
    if (ours->bytes[off] == theirs->bytes[off]) continue;
    if (excluded(r, off) || dead_stack(call, off)) continue;
    note(s, "WRAM %s: ROM $%02X, port $%02X (SP $%04X..$%04X during the call)",
         wram_str(off, buf, sizeof buf), theirs->bytes[off], ours->bytes[off],
         call->min_sp, call->entry_sp);
    return;
  }

  if (our_regs->a != rom_regs->a)
    note(s, "A: ROM $%04X, port $%04X", rom_regs->a, our_regs->a);
  else if (our_regs->x != rom_regs->x)
    note(s, "X: ROM $%04X, port $%04X", rom_regs->x, our_regs->x);
  else if (our_regs->y != rom_regs->y)
    note(s, "Y: ROM $%04X, port $%04X", rom_regs->y, our_regs->y);
  else if ((our_regs->flags & COSIM_FLAG_N) && our_regs->n != rom_regs->n)
    note(s, "flag N: ROM %d, port %d", rom_regs->n, our_regs->n);
  else if ((our_regs->flags & COSIM_FLAG_Z) && our_regs->z != rom_regs->z)
    note(s, "flag Z: ROM %d, port %d", rom_regs->z, our_regs->z);
  else if ((our_regs->flags & COSIM_FLAG_C) && our_regs->c != rom_regs->c)
    note(s, "flag C: ROM %d, port %d", rom_regs->c, our_regs->c);
}

static void record_cycles(CosimStat* s, long cycles) {
  if (s->cycles_min < 0 || cycles < s->cycles_min) s->cycles_min = cycles;
  if (cycles > s->cycles_max) s->cycles_max = cycles;
  s->cycles_mean += ((double)cycles - s->cycles_mean) / (double)s->checked;
}

// ---------------------------------------------------------------------------
// The engine
// ---------------------------------------------------------------------------

// Substitute: run the port against the emulator's own memory, publish the
// registers, and hand the core back a routine that has already finished.
static void run_native(Cosim* c, const CosimRoutine* r, CosimStat* s) {
  Snes* snes = c->snes;
  Cpu* cpu = snes->cpu;

  CosimRegs in, out;
  regs_capture(snes, &in);
  out = in;
  r->run((Wram*)snes->ram, &c->rom, &in, &out);

  cpu->a = out.a;
  cpu->x = out.x;
  cpu->y = out.y;
  if (out.flags & COSIM_FLAG_N) cpu->n = out.n;
  if (out.flags & COSIM_FLAG_Z) cpu->z = out.z;
  if (out.flags & COSIM_FLAG_C) cpu->c = out.c;

  // The ROM's version of this routine pushes; the port does not, so the bytes
  // it would have written keep whatever they held before. Note them, so the
  // whole-program diff can tell "stale because nobody pushed here" apart from
  // "wrong". `stack_bytes` is not a guess — it is what `verify` measured the
  // ROM's stack pointer actually doing.
  for (int i = 0; i < r->stack_bytes; i++) {
    uint32_t off = (uint32_t)(uint16_t)(cpu->sp - i);
    if (off < WRAM_SIZE) c->priv->stale[off] = 1;
  }

  // Stand in for the work the ROM's instructions would have done, so the rest
  // of the machine — the PPU's beam position, the APU, DMA — still sees a call
  // that took about as long as it used to.
  if (r->cycles > 0) snes_runCycles(snes, r->cycles);

  // Return the way the routine itself does, by executing its own RTS/RTL. That
  // borrows the core's exact stack and bank handling instead of reimplementing
  // it here, which is one fewer thing to get wrong.
  cpu->k = (uint8_t)(r->ret_op >> 16);
  cpu->pc = (uint16_t)r->ret_op;

  s->calls++;
  s->checked++;
  s->passed++;
}

// Begin a verification: remember everything the port will need, and let the ROM
// run the routine.
static void begin_verify(Cosim* c, int index, const CosimRoutine* r, CosimStat* s) {
  s->calls++;
  if (c->priv->depth >= COSIM_MAX_DEPTH) return;  // absurdly deep; just let it run

  CosimCall* call = &c->priv->stack[c->priv->depth++];
  call->index = index;
  call->entry_sp = c->snes->cpu->sp;
  call->min_sp = c->snes->cpu->sp;
  call->ret_pc = return_pc(c->snes, r->ret_kind);
  call->cycles = c->snes->cycles;
  regs_capture(c->snes, &call->in);
  memcpy(call->before, c->snes->ram, sizeof(Wram));
}

// The ROM routine has just returned. Rewind and run the port over the same
// input, then diff.
static void end_verify(Cosim* c, CosimCall* call) {
  CosimStat* s = &c->stats[call->index];
  const CosimRoutine* r = s->routine;

  CosimRegs rom_regs;
  regs_capture(c->snes, &rom_regs);

  memcpy(c->priv->scratch, call->before, sizeof(Wram));
  CosimRegs out = call->in;
  out.flags = 0;
  r->run(c->priv->scratch, &c->rom, &call->in, &out);

  s->checked++;
  record_cycles(s, (long)(c->snes->cycles - call->cycles));
  int waived = (int)(call->entry_sp - call->min_sp);
  if (waived > s->stack_waived) s->stack_waived = waived;

  bool was_failed = s->failed;
  compare(s, call, c->priv->scratch, (const Wram*)c->snes->ram, &rom_regs, &out);
  if (!s->failed) {
    s->passed++;
  } else if (!was_failed && c->verbose) {
    printf("  %s: first divergence at call %ld — %s\n", r->name, s->calls, s->detail);
  }
}

void cosim_step(Cosim* c) {
  Snes* snes = c->snes;

  // How deep the stack got is what tells the diff which bytes the routine was
  // entitled to scribble on. Sampling between instructions is enough: a push
  // leaves SP one below the byte it wrote, so the low-water mark always sits
  // just under the lowest address touched.
  for (int i = 0; i < c->priv->depth; i++) {
    CosimCall* call = &c->priv->stack[i];
    if (snes->cpu->sp < call->min_sp) call->min_sp = snes->cpu->sp;
  }

  if (at_instruction(snes)) {
    uint32_t pc = cpu_pc24(snes);

    // Has an in-flight call returned? Both the PC and the stack pointer have to
    // match, which is what keeps an NMI that happens to pass through the same
    // address from ending the call early.
    while (c->priv->depth > 0) {
      CosimCall* call = &c->priv->stack[c->priv->depth - 1];
      const CosimRoutine* r = c->stats[call->index].routine;
      if (pc != call->ret_pc ||
          snes->cpu->sp != return_sp(call->entry_sp, r->ret_kind))
        break;
      c->priv->depth--;
      end_verify(c, call);
    }

    for (int i = 0; i < c->stat_count; i++) {
      if (!(c->enabled & (1u << i))) continue;
      const CosimRoutine* r = c->stats[i].routine;
      if (pc != r->entry) continue;
      if (c->mode == COSIM_NATIVE) {
        run_native(c, r, &c->stats[i]);
        return;  // the PC moved; do not also execute the entry instruction
      }
      begin_verify(c, i, r, &c->stats[i]);
      break;
    }
  } else if (snes->cpu->intWanted && c->priv->depth > 0) {
    // An interrupt is about to land inside a call we are verifying. The handler
    // will change WRAM that the port, which only models the routine, cannot
    // account for — so every call currently in flight has to be abandoned
    // rather than reported as a divergence that is really ours.
    while (c->priv->depth > 0) {
      CosimCall* call = &c->priv->stack[--c->priv->depth];
      c->stats[call->index].interrupted++;
    }
  }

  snes_runCpuCycle(snes);
}

void cosim_frame(Cosim* c) {
  Snes* snes = c->snes;
  while (snes->inVblank) cosim_step(c);
  uint32_t frame = snes->frames;
  while (!snes->inVblank && frame == snes->frames) cosim_step(c);
  snes_readBBus(snes, 0x40);  // keep the APU fed, as the other tools do
  c->frames++;
}

bool cosim_failed(const Cosim* c) {
  for (int i = 0; i < c->stat_count; i++)
    if ((c->enabled & (1u << i)) && c->stats[i].failed) return true;
  return false;
}

const CosimRoutine* cosim_find(const char* name) {
  int count = 0;
  const CosimRoutine* all = cosim_routines(&count);
  for (int i = 0; i < count; i++)
    if (!strcmp(all[i].name, name)) return &all[i];
  return NULL;
}

int cosim_report(const Cosim* c) {
  printf("\n%-20s %8s %8s %8s %6s %6s  %-20s %s\n", "routine", "calls", "checked",
         "passed", "int.", "stack", "ROM cycles", "result");
  printf("%-20s %8s %8s %8s %6s %6s  %-20s %s\n", "--------------------",
         "--------", "--------", "--------", "------", "------",
         "--------------------", "------");

  int failures = 0;
  for (int i = 0; i < c->stat_count; i++) {
    if (!(c->enabled & (1u << i))) continue;
    const CosimStat* s = &c->stats[i];
    char cycles[32] = "-";
    if (s->checked > 0 && c->mode == COSIM_VERIFY)
      snprintf(cycles, sizeof cycles, "%ld..%ld, mean %.0f", s->cycles_min,
               s->cycles_max, s->cycles_mean);

    const char* verdict;
    if (s->failed) { verdict = "FAIL"; failures++; }
    else if (s->checked == 0) verdict = "not reached";
    else verdict = "OK";

    printf("%-20s %8ld %8ld %8ld %6ld %6d  %-20s %s\n", s->routine->name, s->calls,
           s->checked, s->passed, s->interrupted, s->stack_waived, cycles, verdict);
    if (s->failed) printf("%22s%s\n", "", s->detail);
  }
  return failures;
}

// ---------------------------------------------------------------------------
// Lockstep
// ---------------------------------------------------------------------------

typedef struct {
  Snes* snes;
  Cosim cosim;
  Movie movie;
  bool have_movie;
  uint32_t last_frame;  // the PPU frame this side last fed input for
} Side;

// The `WAI` inside `scheduler_idle` — the point the *game* considers a frame
// over, as opposed to the point the PPU does. `docs/frame-skeleton.md` called
// this one in advance, and it is not a detail: comparing at the PPU's vblank
// instead catches the two cores mid-way through whatever bulk WRAM work happens
// to straddle it, and reports a transient as a divergence. At the `WAI` every
// thread is parked and the game's state is quiescent.
#define SCHEDULER_IDLE_WAI 0x808371

// True when this core is sitting halted on that `WAI`.
//
// It costs nothing to wait for: once the scheduler is running, the idle state
// between frames *is* this instruction, so a core that has just run up to the
// start of vblank is normally already here — with `WAI` executed, the program
// counter one past it, and the CPU stopped until NMI. A frame whose work
// overran and left the CPU somewhere else simply does not get compared, which
// is reported rather than papered over.
static bool at_sync_point(const Cosim* c) {
  const Cpu* cpu = c->snes->cpu;
  return cpu->waiting && cpu_pc24(c->snes) == SCHEDULER_IDLE_WAI + 1;
}

// The stacks, from `docs/wram-map.md`: the per-thread stacks at $1120-$114F,
// the scheduler's own topped at $125F, NMI's at $129F, and the thread bookkeepig
// tables between them. Nothing else lives in this range.
#define STACK_AREA_LO 0x1000
#define STACK_AREA_HI 0x1300

// Can this byte's difference be explained without appealing to a bug?
//
// Three things can, and they are not equally strong — which is the point of
// separating them:
//
//   * a scratch byte the routine's descriptor declares the port does not write;
//   * a byte a substituted call is *known* to have left stale, because the
//     ROM's version would have pushed there. Native mode marks these as it
//     goes, from the push footprint `verify` measured, so the set is exactly
//     the bytes some real substitution skipped;
//   * any byte in the stack area at all.
//
// That last rule is a region, not a derivation, and it is deliberately the
// weakest thing here. Stack residue compounds: the port pushes nothing, so a
// substituted call leaves two bytes stale, and every later push and pop at a
// different depth reshuffles which *other* dead bytes the two machines disagree
// about. Tracking that exactly would mean modelling each of the 24 thread
// stacks' live extents, which buys nothing — no code reads below its own stack
// pointer.
//
// What it costs is real and worth stating plainly: inside $7E:1000-$7E:12FF
// this run proves nothing. Everything outside it — all 127 KB of actual game
// state — is compared byte for byte, and `verify` covers the routines'
// behaviour exactly, stack included.
static bool accounted_for(const Cosim* c, uint32_t off) {
  if (off >= STACK_AREA_LO && off < STACK_AREA_HI) return true;
  if (c->priv->stale && c->priv->stale[off]) return true;
  for (int i = 0; i < c->stat_count; i++) {
    if (!(c->enabled & (1u << i))) continue;
    if (excluded(c->stats[i].routine, off)) return true;
  }
  return false;
}

static bool side_start(Side* s, const uint8_t* rom_data, int rom_len,
                       CosimMode mode, const char* movie_path) {
  memset(s, 0, sizeof *s);
  s->snes = snes_init();
  if (!snes_loadRom(s->snes, rom_data, rom_len)) return false;
  cosim_init(&s->cosim, s->snes, mode);
  // Each side replays the movie against its own frame counter. That is the
  // point of driving by scheduler pass rather than by PPU frame: the two cores
  // no longer have to agree on wall-clock timing, only on game state.
  if (movie_path && !movie_load(&s->movie, movie_path)) return false;
  s->have_movie = movie_path != NULL;
  snes_reset(s->snes, true);
  return true;
}

// Advance one side by one scheduler pass: run until it is parked on the `WAI`
// again, having first left the one it was on. False if it never gets there —
// which is normal only during boot, before the scheduler exists.
static bool side_pass(Side* s, long budget) {
  bool left = !at_sync_point(&s->cosim);
  for (long i = 0; i < budget; i++) {
    if (s->have_movie && s->snes->frames != s->last_frame) {
      s->last_frame = s->snes->frames;
      uint16_t buttons = movie_state(&s->movie, (int)s->last_frame);
      for (int b = 0; b < 12; b++)
        snes_setButtonState(s->snes, 1, b, (buttons >> b) & 1);
    }
    if (at_sync_point(&s->cosim)) {
      if (left) {
        // The APU is fed once per pass, as the other tools feed it once per
        // frame; a pass and a frame are the same thing here.
        snes_readBBus(s->snes, 0x40);
        return true;
      }
    } else {
      left = true;
    }
    cosim_step(&s->cosim);
  }
  return false;
}

int cosim_lockstep(const uint8_t* rom_data, int rom_len, const char* movie_path,
                   int frames, const char* const* names, int name_count,
                   bool verbose) {
  // Only the native side gets a harness; the reference side runs stock. Both
  // are driven by the same movie, so any difference between their WRAM is
  // caused by the substitution and nothing else.
  Side ref, nat;
  if (!side_start(&ref, rom_data, rom_len, COSIM_VERIFY, movie_path)) return 1;
  if (!side_start(&nat, rom_data, rom_len, COSIM_NATIVE, movie_path)) return 1;
  ref.cosim.enabled = 0;

  if (name_count == 0) {
    cosim_enable_all(&nat.cosim);
  } else {
    for (int i = 0; i < name_count; i++) {
      // `-r none` substitutes nothing. It is the control: two stock cores on
      // the same movie must stay identical for the whole run, or a divergence
      // anywhere else means nothing.
      if (!strcmp(names[i], "none")) continue;
      if (!cosim_enable(&nat.cosim, names[i])) {
        fprintf(stderr, "error: no ported routine named '%s'\n", names[i]);
        return 2;
      }
    }
  }

  printf("Running %d frames twice — stock, and with the port substituted —\n"
         "and comparing all %d KB of WRAM once per scheduler pass.\n\n",
         frames, WRAM_SIZE / 1024);

  // Boot happens before there is a scheduler to synchronise on, so the two
  // sides are run by PPU frame until both reach their first `WAI`. Nothing is
  // substituted that early, so there is nothing to compare yet either.
  long booted = 0;
  while (booted < frames &&
         (!at_sync_point(&ref.cosim) || !at_sync_point(&nat.cosim))) {
    side_pass(&ref, 4000000);
    side_pass(&nat, 4000000);
    booted++;
  }

  int rc = 0;
  bool reported = false;
  long compared = 0, unsynced = 0, diverged_frames = 0, explained_only = 0;
  uint32_t worst = 0, last_differ = 0, worst_unexplained = 0;
  uint32_t worst_unexplained_at[8];
  int worst_unexplained_n = 0;
  long worst_unexplained_pass = -1;
  for (long pass = booted; pass < frames; pass++) {
    // One pass each. Timing may have drifted between the two cores, but they
    // are now at the same point in the *game*, which is what the diff is about.
    bool ref_ok = side_pass(&ref, 4000000);
    bool nat_ok = side_pass(&nat, 4000000);
    if (!ref_ok || !nat_ok) { unsynced++; continue; }
    compared++;

    uint32_t differ = 0, unexplained = 0;
    for (uint32_t i = 0; i < WRAM_SIZE; i++) {
      if (ref.snes->ram[i] == nat.snes->ram[i]) {
        // Agreed again: whatever went stale here has been overwritten by both
        // sides, so it stops being excusable. Keeping the set tight matters —
        // a byte marked stale forever would mask a genuine divergence later.
        nat.cosim.priv->stale[i] = 0;
        continue;
      }
      differ++;
      if (!accounted_for(&nat.cosim, i)) unexplained++;
    }
    explained_only += (differ > 0 && unexplained == 0) ? 1 : 0;
    if (differ == 0) continue;

    // The first divergence gets described in full, and then the run *keeps
    // going*. Whether a difference is inert or fatal is the whole question, and
    // stopping at the first byte answers it by assumption: dead stack a routine
    // pushed and abandoned stays a fixed handful of bytes forever, while a
    // wrong answer feeds back into the game and spreads within a frame or two.
    if (!reported) {
      char buf[32];
      printf("First difference at pass %ld — %u byte%s:\n", pass, differ,
             differ == 1 ? "" : "s");
      uint32_t shown = 0;
      for (uint32_t i = 0; i < WRAM_SIZE && shown < 16; i++) {
        if (ref.snes->ram[i] == nat.snes->ram[i]) continue;
        printf("    %s  stock $%02X, native $%02X   %s\n",
               wram_str(i, buf, sizeof buf), ref.snes->ram[i], nat.snes->ram[i],
               accounted_for(&nat.cosim, i)
                   ? "(dead: a push the port never made, or declared scratch)"
                   : "*** unexplained ***");
        shown++;
      }
      reported = true;
    }
    if (unexplained > 0 && unexplained >= worst_unexplained) {
      // Keep the worst offender's addresses; a summary that says "5 bytes were
      // unexplained" without saying which is not a finding, it is a rumour.
      worst_unexplained_pass = pass;
      worst_unexplained_n = 0;
      for (uint32_t i = 0; i < WRAM_SIZE && worst_unexplained_n < 8; i++) {
        if (ref.snes->ram[i] == nat.snes->ram[i]) continue;
        if (accounted_for(&nat.cosim, i)) continue;
        worst_unexplained_at[worst_unexplained_n++] = i;
      }
    }
    if (unexplained > 0) rc = 1;
    if (differ > worst) worst = differ;
    if (unexplained > worst_unexplained) worst_unexplained = unexplained;
    diverged_frames++;
    last_differ = differ;
  }

  if (diverged_frames == 0) {
    printf("Identical at all %ld compared scheduler passes.\n", compared);
  } else {
    printf("\n%ld of %ld compared passes differed; at most %u byte%s at once,\n"
           "%u at the end of the run.\n",
           diverged_frames, compared, worst, worst == 1 ? "" : "s", last_differ);
    if (rc == 0)
      printf("Every one, on all %ld passes, was inside the stacks ($7E:1000-$7E:12FF)\n"
             "or a declared scratch byte. No byte of live game state ever differed.\n",
             explained_only);
    else {
      char buf[32];
      printf("Up to %u byte%s could not be accounted for — worst at pass %ld:\n",
             worst_unexplained, worst_unexplained == 1 ? "" : "s",
             worst_unexplained_pass);
      for (int i = 0; i < worst_unexplained_n; i++)
        printf("    %s  stock $%02X, native $%02X\n",
               wram_str(worst_unexplained_at[i], buf, sizeof buf),
               ref.snes->ram[worst_unexplained_at[i]],
               nat.snes->ram[worst_unexplained_at[i]]);
    }
  }
  if (booted > 0)
    printf("%ld pass%s spent booting, before there was a scheduler to sync on.\n",
           booted, booted == 1 ? "" : "es");
  if (unsynced > 0)
    printf("%ld pass%s not compared — a side never came back to the WAI.\n",
           unsynced, unsynced == 1 ? " was" : "es were");
  cosim_report(&nat.cosim);

  movie_free(&ref.movie);
  movie_free(&nat.movie);
  cosim_free(&nat.cosim);
  cosim_free(&ref.cosim);
  snes_free(nat.snes);
  snes_free(ref.snes);
  return rc;
}
