#include "cosim/cosim.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "analysis/movie_apply.h"
#include "port/apu.h"
#include "port/coverage.h"

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
  // --- resumable routines only ---
  // A yielding call is not one comparison but a chain of them, one per segment
  // between suspensions. `before`, `in`, `min_sp` and `cycles` above are all
  // retaken at each resumption, so each segment is diffed against the state the
  // routine actually resumed with rather than the state it was first entered
  // with — which is the whole point, because arbitrary other threads ran in
  // between and moved WRAM under it.
  void* ctx;            // the port's parked state; zeroed at entry
  bool suspended;       // parked inside thread_yield, waiting to be resumed
  bool segment_spoiled; // an interrupt landed mid-segment; do not diff this one
  uint32_t resume_pc;   // where the current suspension will come back to
  long segment;         // which segment is in flight, from 0
  // Where the ROM's APU traffic stood when this segment began. Everything
  // logged after it is this call's, which is how a nested call's sends are kept
  // out of its caller's comparison — the caller's window simply also contains
  // them, exactly as its own sends would be.
  uint32_t apu_mark;
} CosimCall;

#define COSIM_MAX_DEPTH 8

// `thread_yield`, `$80:8353`. Reaching it is how a segment ends; see
// `docs/threads.md`.
#define THREAD_YIELD_ENTRY 0x808353

// ---------------------------------------------------------------------------
// The APU: the one output a WRAM diff cannot see
// ---------------------------------------------------------------------------
//
// `port/apu.h` hands the actual bus traffic to a hook, because the port cannot
// wait for an SPC700 and, under `verify`, must not write to one — the ROM
// already did, and the port is being replayed over a snapshot of the past. That
// leaves a real hole: a port that computed the sequence counter correctly and
// then sent nothing at all would pass every byte of every diff, because the
// ports are not memory.
//
// So the traffic is compared too, and by the same rule as everything else here:
// watch what the *ROM's own instruction* put on the bus. `$80:CCD1  STX $2142`
// is that instruction — X is the command, A the parameter — which is exactly
// where `zamn_assets verify-music` hooks for the same reason.
//
// Two logs, both process-global for the reason the coverage counters are: they
// describe the run and not a `Cosim`. Only calls inside an interception are
// logged, which used to keep a data-set upload's 23,820 commands out — until
// `$80:CCC8` was registered on its own entry and every one of them became an
// interception, one command per call. The ring is 64 deep and nothing ported
// sends more than one command per call, so a window is always 1 and the extra
// volume costs nothing.
#define APU_SEND_STORE 0x80ccd1
#define COSIM_APU_LOG 64

typedef struct {
  uint16_t cmd_param[COSIM_APU_LOG];  // cmd << 8 | param
  uint32_t count;                     // monotonic; may run past the buffer
} ApuLog;

static ApuLog g_apu_rom;   // what the ROM's instructions sent, across the run
static ApuLog g_apu_port;  // what the port asked for, during one segment

// A guard answers "can the port serve this call?" by *running the port* on a
// throwaway copy of WRAM. That is fine for memory and it is not fine for a
// sound card: five nested guards asking about one pickup would play the pickup
// five times, and in native mode they would be real. So the hook is muted for
// the length of a dry run, the same way `guard_allows` saves and restores the
// coverage counters — for exactly the same reason, and one line apart.
static bool g_apu_dry;

// A ring on both sides: `count` is monotonic for the ROM's log, which runs for
// the whole movie, and reset per segment for the port's. Reads index it the
// same way, which is the point — a log that wrote linearly and read modulo
// would quietly start comparing every call against the first one.
static void apu_log_add(ApuLog* log, uint8_t cmd, uint8_t param) {
  log->cmd_param[log->count % COSIM_APU_LOG] =
      (uint16_t)((uint16_t)cmd << 8 | param);
  log->count++;
}

// The hook `verify` installs: record, send nothing. Writing to the emulated
// APU here would be sending every sound effect twice.
static void apu_record(void* ctx, uint8_t seq, uint8_t cmd, uint8_t param) {
  (void)ctx;
  (void)seq;  // the sequence counter is WRAM, and the diff already checks it
  if (g_apu_dry) return;
  apu_log_add(&g_apu_port, cmd, param);
}

// ...and the one `run` installs: do it for real, including the wait.
//
// **In practice the wait does not happen, and that is load-bearing rather than
// lucky.** Every substituted caller of this is a sound effect, and a sound effect
// finds the SPC caught up from sounds ago: the first read already matches and the
// loop body is never entered. The one path that *would* enter it — a data-set
// upload's 23,820 back-to-back commands — is why `$80:CCC8` is registered
// `verify_only`, because no amount of care in here makes a substituted spin work.
// See `CosimRoutine::verify_only` for the measurement and the argument.
//
// The loop is still written to burn the machine's time rather than the APU's,
// with `snes_runCycles` and not `apu_runCycles`. Two reasons, and the first holds
// whether or not the body ever runs: fast-forwarding the APU alone moves it
// forward without moving `snes->cycles`, which puts the APU on a different clock
// from the rest of the machine — the last thing lockstep wants. And the second is
// what the ROM's `CPY $2143 : BNE` actually buys for the price of its two
// instructions: the beam moves, HDMA fires, the APU catches up, and an NMI comes
// due if one is due. It runs no CPU opcodes, which is right — the only opcodes
// the ROM would have run here are the two this loop stands in for. That it is
// *still* not enough for an upload is exactly the finding.
//
// The bound keeps a wedged SPC from hanging the harness rather than failing it,
// and it is generous on purpose: `verify` measured the ROM's own worst case at
// 85,450 master cycles, a frame and a half.
#define APU_SPIN_LIMIT 262144
static void apu_drive(void* ctx, uint8_t seq, uint8_t cmd, uint8_t param) {
  Snes* snes = (Snes*)ctx;
  if (g_apu_dry) return;
  // Reading through `snes_readBBus` rather than off `apu->outPorts` is what the
  // ROM's `CPY $2143` does, catch-up and all.
  for (int spun = 0;
       spun < APU_SPIN_LIMIT &&
       snes_readBBus(snes, APU_PORT_SEQ & 0xff) != seq;
       spun += 32)
    snes_runCycles(snes, 32);
  snes_writeBBus(snes, APU_PORT_CMD & 0xff, cmd);
  snes_writeBBus(snes, APU_PORT_PARAM & 0xff, param);
  snes_writeBBus(snes, APU_PORT_SEQ & 0xff, (uint8_t)(seq + 1));
}

static void run_native_segment(Cosim* c, CosimCall* call);

struct CosimPriv {
  CosimCall stack[COSIM_MAX_DEPTH];
  int depth;
  Wram* scratch;  // where the C port runs during a verify
  // Where a routine's guard runs, in both modes: a throwaway copy of live WRAM,
  // so asking "can the port handle this call?" can be answered by running the
  // port and looking, without any of it being kept. Allocated only if some
  // routine actually declares a guard.
  Wram* guard;
  // Native mode only: bytes a substituted call is expected to have left stale,
  // because the ROM's version would have pushed there and the port has no stack
  // in WRAM at all. See cosim_stale().
  uint8_t* stale;
  // The port's window onto the APU, held here so it outlives every call that
  // uses it. See the `ApuLog` comment above.
  ApuPorts apu;
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
  r->v = cpu->v;
  r->d = cpu->dp;
  r->db = cpu->db;
  r->flags = COSIM_FLAG_N | COSIM_FLAG_Z | COSIM_FLAG_C | COSIM_FLAG_V;
  r->regs = COSIM_REG_ALL;
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
  // The registry outgrew a 32-bit mask once and the symptom was `verify`
  // silently measuring nothing — see `Cosim::enabled`. Say so instead.
  if (count > COSIM_MAX_ROUTINES) {
    fprintf(stderr,
            "error: %d routines registered but COSIM_MAX_ROUTINES is %d — widen\n"
            "       Cosim::enabled and Cosim::stats together, in cosim.h.\n",
            count, COSIM_MAX_ROUTINES);
    exit(2);
  }
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
  // Room for a resumable routine's parked state, in both modes — this is the
  // whole of what "the port is suspended here" means, and it being plain
  // copyable data is the reason the port suspends this way at all. One buffer
  // per nesting level, sized for the largest context any routine declares.
  size_t ctx_max = 0;
  for (int i = 0; i < count; i++) {
    if (all[i].run_yield && (size_t)all[i].ctx_size > ctx_max)
      ctx_max = (size_t)all[i].ctx_size;
    if (all[i].supported && !c->priv->guard)
      c->priv->guard = (Wram*)malloc(sizeof(Wram));
  }
  if (ctx_max > 0)
    for (int i = 0; i < COSIM_MAX_DEPTH; i++)
      c->priv->stack[i].ctx = calloc(1, ctx_max);

  // Give the port an APU. Which one depends on what this harness is for: under
  // `verify` the ROM has already made the noise and the port is only asked what
  // it would have sent; under `run` the port *is* the game, so it drives the
  // real ports. Lockstep builds two harnesses and only the substituting one
  // ever runs port code, so the single hook is never contended — see
  // `port/apu.h` on why there is only one.
  c->priv->apu.send = mode == COSIM_VERIFY ? apu_record : apu_drive;
  c->priv->apu.ctx = snes;
  apu_attach(&c->priv->apu);
}

void cosim_free(Cosim* c) {
  if (!c->priv) return;
  apu_attach(NULL);  // the hook lives in `priv`, which is about to go
  free(c->priv->scratch);
  free(c->priv->guard);
  free(c->priv->stale);
  for (int i = 0; i < COSIM_MAX_DEPTH; i++) {
    free(c->priv->stack[i].before);
    free(c->priv->stack[i].ctx);
  }
  free(c->priv);
  c->priv = NULL;
}

bool cosim_enable(Cosim* c, const char* name) {
  int count = 0;
  const CosimRoutine* all = cosim_routines(&count);
  for (int i = 0; i < count; i++) {
    if (!strcmp(all[i].name, name)) {
      c->enabled |= UINT64_C(1) << i;
      return true;
    }
  }
  return false;
}

void cosim_enable_all(Cosim* c) {
  int count = 0;
  cosim_routines(&count);
  c->enabled = count >= 64 ? ~UINT64_C(0) : (UINT64_C(1) << count) - 1;
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

// Diff the WRAM half. Split out from compare() because a suspension is checked
// on WRAM and the tick count alone — the registers do not survive one.
static void compare_wram(CosimStat* s, const CosimCall* call, const Wram* ours,
                         const Wram* theirs) {
  const CosimRoutine* r = s->routine;
  char buf[32];
  for (uint32_t off = 0; off < WRAM_SIZE; off++) {
    if (ours->bytes[off] == theirs->bytes[off]) continue;
    if (excluded(r, off) || dead_stack(call, off)) continue;
    note(s, "WRAM %s: ROM $%02X, port $%02X (SP $%04X..$%04X during segment %ld)",
         wram_str(off, buf, sizeof buf), theirs->bytes[off], ours->bytes[off],
         call->min_sp, call->entry_sp, call->segment);
    return;
  }
}

// Diff what each side put on the APU's ports during this segment. See the
// `ApuLog` comment: the ports are not memory, so nothing above can see this.
static void compare_apu(CosimStat* s, const CosimCall* call) {
  uint32_t theirs = g_apu_rom.count - call->apu_mark;
  uint32_t ours = g_apu_port.count;
  if (theirs == 0 && ours == 0) return;  // the overwhelmingly common case

  if (call->apu_mark + COSIM_APU_LOG < g_apu_rom.count ||
      ours > COSIM_APU_LOG) {
    // Never truncate quietly. Nothing ported sends more than one command per
    // call, so reaching this means something is being intercepted that the
    // design above did not anticipate, and that is worth stopping for.
    note(s, "APU: %u commands in one segment overflowed the %d-entry log",
         theirs > ours ? theirs : ours, COSIM_APU_LOG);
    return;
  }
  if (theirs != ours) {
    note(s, "APU: the ROM sent %u command%s, the port %u", theirs,
         theirs == 1 ? "" : "s", ours);
    return;
  }
  for (uint32_t i = 0; i < ours; i++) {
    uint16_t rom = g_apu_rom.cmd_param[(call->apu_mark + i) % COSIM_APU_LOG];
    if (rom == g_apu_port.cmd_param[i]) continue;
    note(s, "APU command %u: ROM $%02X/$%02X, port $%02X/$%02X", i, rom >> 8,
         rom & 0xff, g_apu_port.cmd_param[i] >> 8,
         g_apu_port.cmd_param[i] & 0xff);
    return;
  }
}

// Diff the port's result against the ROM's. `theirs` is the emulator's WRAM,
// which at this point holds what the ROM routine produced.
static void compare(CosimStat* s, const CosimCall* call, const Wram* ours,
                    const Wram* theirs, const CosimRegs* rom_regs,
                    const CosimRegs* our_regs) {
  compare_wram(s, call, ours, theirs);
  if (s->failed) return;
  compare_apu(s, call);
  if (s->failed) return;

  if ((our_regs->regs & COSIM_REG_A) && our_regs->a != rom_regs->a)
    note(s, "A: ROM $%04X, port $%04X", rom_regs->a, our_regs->a);
  else if ((our_regs->regs & COSIM_REG_X) && our_regs->x != rom_regs->x)
    note(s, "X: ROM $%04X, port $%04X", rom_regs->x, our_regs->x);
  else if ((our_regs->regs & COSIM_REG_Y) && our_regs->y != rom_regs->y)
    note(s, "Y: ROM $%04X, port $%04X", rom_regs->y, our_regs->y);
  else if ((our_regs->flags & COSIM_FLAG_N) && our_regs->n != rom_regs->n)
    note(s, "flag N: ROM %d, port %d", rom_regs->n, our_regs->n);
  else if ((our_regs->flags & COSIM_FLAG_Z) && our_regs->z != rom_regs->z)
    note(s, "flag Z: ROM %d, port %d", rom_regs->z, our_regs->z);
  else if ((our_regs->flags & COSIM_FLAG_C) && our_regs->c != rom_regs->c)
    note(s, "flag C: ROM %d, port %d", rom_regs->c, our_regs->c);
  else if ((our_regs->flags & COSIM_FLAG_V) && our_regs->v != rom_regs->v)
    note(s, "flag V: ROM %d, port %d", rom_regs->v, our_regs->v);
}

static void record_cycles(CosimStat* s, long cycles) {
  if (s->cycles_min < 0 || cycles < s->cycles_min) s->cycles_min = cycles;
  if (cycles > s->cycles_max) s->cycles_max = cycles;
  s->cycles_mean += ((double)cycles - s->cycles_mean) / (double)s->checked;
}

// ---------------------------------------------------------------------------
// The engine
// ---------------------------------------------------------------------------

// Write back whatever the shim claimed to model, and nothing else.
static void native_publish(Cosim* c, const CosimRegs* out) {
  Cpu* cpu = c->snes->cpu;
  if (out->regs & COSIM_REG_A) cpu->a = out->a;
  if (out->regs & COSIM_REG_X) cpu->x = out->x;
  if (out->regs & COSIM_REG_Y) cpu->y = out->y;
  if (out->flags & COSIM_FLAG_N) cpu->n = out->n;
  if (out->flags & COSIM_FLAG_Z) cpu->z = out->z;
  if (out->flags & COSIM_FLAG_C) cpu->c = out->c;
  if (out->flags & COSIM_FLAG_V) cpu->v = out->v;
}

// Publish a finished routine's registers and hand the core its own RTS/RTL.
//
// Returning by pointing the program counter at the routine's own return
// instruction borrows the core's exact stack and bank handling instead of
// reimplementing it here, which is one fewer thing to get wrong.
static void native_return(Cosim* c, const CosimRoutine* r, const CosimRegs* out) {
  Cpu* cpu = c->snes->cpu;
  native_publish(c, out);

  // The ROM's version of this routine pushes; the port does not, so the bytes
  // it would have written keep whatever they held before. Note them, so the
  // whole-program diff can tell "stale because nobody pushed here" apart from
  // "wrong". `stack_bytes` is not a guess — it is what `verify` measured the
  // ROM's stack pointer actually doing.
  for (int i = 0; i < r->stack_bytes; i++) {
    uint32_t off = (uint32_t)(uint16_t)(cpu->sp - i);
    if (off < WRAM_SIZE) c->priv->stale[off] = 1;
  }

  cpu->k = (uint8_t)(r->ret_op >> 16);
  cpu->pc = (uint16_t)r->ret_op;
}

// ...and the mirror image: suspend by jumping to the routine's own
// `JSL thread_yield`, with the sleep count in A where the ROM would have put it.
//
// This is the same idea as `native_return`, and it is what makes substituting a
// coroutine tractable at all. The port does not have to model parking a stack
// pointer, choosing the next thread, or coming back: the core executes the real
// `JSL`, the real scheduler parks the real frame, and the real scheduler
// resumes it. Because it is literally the ROM's own instruction, the stack
// footprint of a substituted suspension is not an approximation of the ROM's —
// it *is* the ROM's.
//
// The registers and flags are published first, and that is not housekeeping.
// `thread_yield` opens with `PHP`, so whatever is set here is what the thread
// carries through the suspension and gets back on resume.
static void native_yield(Cosim* c, const CosimRoutine* r, CosimCall* call,
                         const CosimRegs* out, uint16_t ticks) {
  Cpu* cpu = c->snes->cpu;
  native_publish(c, out);
  cpu->a = ticks;
  cpu->k = (uint8_t)(r->yield_op >> 16);
  cpu->pc = (uint16_t)r->yield_op;
  call->suspended = true;
  call->resume_pc = r->yield_op + 4;  // past the 4-byte JSL
}

// Run one segment of a substituted routine against the emulator's own memory,
// then either suspend or return.
static void run_native_segment(Cosim* c, CosimCall* call) {
  CosimStat* s = &c->stats[call->index];
  const CosimRoutine* r = s->routine;
  Snes* snes = c->snes;

  CosimRegs in, out;
  regs_capture(snes, &in);
  out = in;
  uint16_t ticks = 0;
  PortStep step =
      r->run_yield((Wram*)snes->ram, &c->rom, &in, &out, call->ctx, &ticks);

  s->checked++;
  s->passed++;

  // Stand in for the work the ROM's instructions would have done, so the rest
  // of the machine — the PPU's beam position, the APU, DMA — still sees a
  // segment that took about as long as it used to.
  if (r->cycles > 0) snes_runCycles(snes, r->cycles);

  if (step == PORT_YIELDED) {
    s->yields++;
    native_yield(c, r, call, &out, ticks);
    return;
  }
  native_return(c, r, &out);
  // Pop it: a resumable call keeps a frame for its context, and this is the end
  // of it. It is the top of the stack by construction — anything called during
  // a segment returned before the segment did.
  if (c->priv->depth > 0 && &c->priv->stack[c->priv->depth - 1] == call)
    c->priv->depth--;
}

// Ask a routine's guard whether the port can stand in for the call that is
// about to happen. Read-only as far as the game is concerned: the guard works
// on a copy, which is what lets it answer by running the port and looking.
static bool guard_allows(Cosim* c, const CosimRoutine* r) {
  if (!r->supported) return true;
  memcpy(c->priv->guard, c->snes->ram, sizeof(Wram));
  CosimRegs in;
  regs_capture(c->snes, &in);

  // The guard answers by running the port, so it trips branch-coverage marks —
  // and it must, because a *declined* call is only ever seen here: the port is
  // never run again for it. But a call the guard allows will be run for real a
  // moment later, and counting both would double every site in the routine.
  //
  // So: keep what the dry run recorded exactly when the dry run is the only run
  // there will be. Each call is then counted once, by whichever pass was real.
  unsigned long before[PORT_COVER_COUNT];
  port_cover_save(before);
  g_apu_dry = true;  // nothing a dry run asks for reaches the APU — see apu_record
  bool ok = r->supported(c->priv->guard, &c->rom, &in);
  g_apu_dry = false;
  if (ok) port_cover_restore(before);
  return ok;
}

// Substitute at a routine's entry. False if the routine could not be taken over
// — which today means only that the call stack is absurdly deep — in which case
// the ROM's own code runs and nothing is claimed.
static bool run_native(Cosim* c, int index, const CosimRoutine* r, CosimStat* s) {
  Snes* snes = c->snes;

  if (r->run_yield) {
    if (c->priv->depth >= COSIM_MAX_DEPTH) return false;
    CosimCall* call = &c->priv->stack[c->priv->depth++];
    call->index = index;
    call->entry_sp = snes->cpu->sp;
    call->min_sp = snes->cpu->sp;
    call->suspended = false;
    call->segment_spoiled = false;
    call->segment = 0;
    call->ret_pc = return_pc(snes, r->ret_kind);
    memset(call->ctx, 0, (size_t)r->ctx_size);
    s->calls++;
    run_native_segment(c, call);
    return true;
  }

  CosimRegs in, out;
  regs_capture(snes, &in);
  out = in;
  r->run((Wram*)snes->ram, &c->rom, &in, &out);

  // Stand in for the work the ROM's instructions would have done.
  if (r->cycles > 0) snes_runCycles(snes, r->cycles);
  native_return(c, r, &out);

  s->calls++;
  s->checked++;
  s->passed++;
  return true;
}

// Start a segment: the state the port will be rewound to and run over. Called
// at the routine's entry, and again at every resumption — because between a
// yield and the resumption arbitrary other threads ran, so the state the
// routine picks up with is not the state it left.
static void segment_start(Cosim* c, CosimCall* call) {
  call->entry_sp = c->snes->cpu->sp;
  call->min_sp = c->snes->cpu->sp;
  call->cycles = c->snes->cycles;
  call->segment_spoiled = false;
  call->apu_mark = g_apu_rom.count;
  regs_capture(c->snes, &call->in);
  memcpy(call->before, c->snes->ram, sizeof(Wram));
}

// Begin a verification: remember everything the port will need, and let the ROM
// run the routine.
static void begin_verify(Cosim* c, int index, const CosimRoutine* r, CosimStat* s) {
  s->calls++;
  if (c->priv->depth >= COSIM_MAX_DEPTH) return;  // absurdly deep; just let it run

  CosimCall* call = &c->priv->stack[c->priv->depth++];
  call->index = index;
  call->ret_pc = return_pc(c->snes, r->ret_kind);
  call->suspended = false;
  call->resume_pc = 0;
  call->segment = 0;
  if (r->run_yield) memset(call->ctx, 0, (size_t)r->ctx_size);
  segment_start(c, call);
}

// Run the port over one segment and diff what it produced against what the ROM
// just produced. Returns what the port said it did, so the caller can check the
// port and the ROM agree about *whether* the routine suspended.
static PortStep verify_segment(Cosim* c, CosimCall* call, CosimRegs* out,
                               uint16_t* ticks) {
  const CosimRoutine* r = c->stats[call->index].routine;
  memcpy(c->priv->scratch, call->before, sizeof(Wram));
  g_apu_port.count = 0;
  *out = call->in;
  out->flags = 0;
  out->regs = COSIM_REG_ALL;
  if (!r->run_yield) {
    r->run(c->priv->scratch, &c->rom, &call->in, out);
    return PORT_RETURNED;
  }
  PortStep step =
      r->run_yield(c->priv->scratch, &c->rom, &call->in, out, call->ctx, ticks);
  // The sleep count *is* the accumulator — it is what the ROM has in A when it
  // executes `JSL thread_yield`. Deciding that here rather than in each shim
  // keeps the two from drifting apart.
  if (step == PORT_YIELDED) out->a = *ticks;
  return step;
}

static void record_segment(Cosim* c, CosimCall* call, CosimStat* s) {
  s->checked++;
  record_cycles(s, (long)(c->snes->cycles - call->cycles));
  int waived = (int)(call->entry_sp - call->min_sp);
  if (waived > s->stack_waived) s->stack_waived = waived;
}

static void report_first(Cosim* c, CosimStat* s, bool was_failed) {
  if (s->failed && !was_failed && c->verbose)
    printf("  %s: first divergence at call %ld — %s\n", s->routine->name, s->calls,
           s->detail);
}

// The ROM routine has reached its `JSL thread_yield`. That ends a segment: the
// port gets rewound and run over the same input, and everything it produced is
// diffed against what the ROM produced.
//
// A suspension is checked exactly as hard as a return, and that is a deliberate
// answer to the carry bug in `docs/cosim.md`. It would be easy to argue that a
// yield has no outputs because the routine has not finished — but `thread_yield`
// opens with `PHP` and the scheduler resumes with `PLP`, so the flags at the
// `JSL` are parked with the thread and handed back to it. In native mode nothing
// else would ever set them. An unchecked output is an unchecked output whether
// the routine is on its way out or on its way to sleep.
static void suspend_verify(Cosim* c, CosimCall* call) {
  CosimStat* s = &c->stats[call->index];
  bool was_failed = s->failed;

  s->yields++;
  call->resume_pc = return_pc(c->snes, COSIM_RTL);
  call->suspended = true;

  if (call->segment_spoiled) {
    s->interrupted++;
    // The port still has to run, or its context would fall a segment behind the
    // ROM and every later comparison would be meaningless. It just is not
    // scored.
    CosimRegs out;
    uint16_t ticks = 0;
    verify_segment(c, call, &out, &ticks);
    return;
  }

  CosimRegs rom_regs;
  regs_capture(c->snes, &rom_regs);

  CosimRegs out;
  uint16_t ticks = 0xffff;
  PortStep step = verify_segment(c, call, &out, &ticks);
  record_segment(c, call, s);

  if (step != PORT_YIELDED)
    note(s, "segment %ld: the ROM suspended at $%06X, the port returned",
         call->segment, THREAD_YIELD_ENTRY);
  else if (ticks != rom_regs.a)
    // The same difference `compare` would find in A, reported in the terms the
    // routine is written in. A sleep of the wrong length is worth naming.
    note(s, "segment %ld: sleep count — ROM %u ticks, port %u", call->segment,
         rom_regs.a, ticks);
  else
    compare(s, call, c->priv->scratch, (const Wram*)c->snes->ram, &rom_regs, &out);

  if (!s->failed) s->passed++;
  report_first(c, s, was_failed);
}

// ...and it has come back. Everything the port will be rewound to has to be
// retaken from here, not from the entry.
static void resume_verify(Cosim* c, CosimCall* call) {
  call->suspended = false;
  call->segment++;
  segment_start(c, call);
}

// The ROM routine has returned for good. Same as a suspension, plus the
// registers — which are outputs again, because the routine's own code
// re-established them after the last resumption.
static void end_verify(Cosim* c, CosimCall* call) {
  CosimStat* s = &c->stats[call->index];
  bool was_failed = s->failed;

  if (call->segment_spoiled) {
    s->interrupted++;
    return;
  }

  CosimRegs rom_regs;
  regs_capture(c->snes, &rom_regs);

  CosimRegs out;
  uint16_t ticks = 0;
  PortStep step = verify_segment(c, call, &out, &ticks);
  record_segment(c, call, s);

  if (step != PORT_RETURNED)
    note(s, "segment %ld: the ROM returned, the port suspended for %u ticks",
         call->segment, ticks);
  else
    compare(s, call, c->priv->scratch, (const Wram*)c->snes->ram, &rom_regs, &out);

  if (!s->failed) s->passed++;
  report_first(c, s, was_failed);
}

// The innermost in-flight call that is parked inside `thread_yield` and is
// waiting for exactly this address, or NULL. The stack-pointer test is what
// distinguishes a genuine resumption from the many other times execution
// wanders past the same instruction — while a routine is suspended the whole
// rest of the game is running.
static CosimCall* find_resume(Cosim* c, uint32_t pc) {
  for (int i = c->priv->depth - 1; i >= 0; i--) {
    CosimCall* call = &c->priv->stack[i];
    if (call->suspended && pc == call->resume_pc &&
        c->snes->cpu->sp == call->entry_sp)
      return call;
  }
  return NULL;
}

// The call this `JSL thread_yield` belongs to, or NULL.
//
// Every thread in the game yields, so being at `thread_yield` proves nothing on
// its own. What settles it is the return address the `JSL` just pushed: if it
// points inside the routine's own body, this is that routine suspending. A
// yield from something the routine *called* has a return address in the callee
// and is not matched — correctly, because a nested yield is a different thing
// the port would have to model, and silently treating it as this routine's
// would be exactly the kind of quiet approximation the harness exists to refuse.
static CosimCall* find_yield(Cosim* c) {
  uint32_t site = return_pc(c->snes, COSIM_RTL);
  for (int i = c->priv->depth - 1; i >= 0; i--) {
    CosimCall* call = &c->priv->stack[i];
    const CosimRoutine* r = c->stats[call->index].routine;
    if (call->suspended || !r->run_yield) continue;
    if (site > r->entry && site < r->end) return call;
  }
  return NULL;
}

void cosim_step(Cosim* c) {
  Snes* snes = c->snes;

  // How deep the stack got is what tells the diff which bytes the routine was
  // entitled to scribble on. Sampling between instructions is enough: a push
  // leaves SP one below the byte it wrote, so the low-water mark always sits
  // just under the lowest address touched.
  //
  // A suspended call is skipped, and that is not a detail. While a routine is
  // parked the scheduler switches to other threads' stacks entirely, so the
  // stack pointer goes far below anything this routine touched; carrying that
  // low-water mark into the next segment would waive most of a kilobyte of WRAM
  // for free. The window is per segment, and it is retaken at every resumption.
  for (int i = 0; i < c->priv->depth; i++) {
    CosimCall* call = &c->priv->stack[i];
    if (!call->suspended && snes->cpu->sp < call->min_sp)
      call->min_sp = snes->cpu->sp;
  }

  if (at_instruction(snes)) {
    uint32_t pc = cpu_pc24(snes);

    // The ROM is about to put a command on the APU's ports. Logged only inside
    // an interception, because that is the only window anything compares. Since
    // `apu_send` was registered, a data-set upload's 23,820 commands each arrive
    // inside their own interception, so each is a window of one. Under `run` the
    // ROM never reaches this instruction for a substituted routine, so there is
    // nothing to log and nothing to compare — native mode's check is lockstep,
    // as it is for everything else. An upload is the exception, and deliberately:
    // `apu_send` is `verify_only`, so under `run` the ROM does execute it, at
    // depth 0, and these writes are not logged because nothing is comparing them.
    if (pc == APU_SEND_STORE && c->priv->depth > 0)
      apu_log_add(&g_apu_rom, (uint8_t)snes->cpu->x, (uint8_t)snes->cpu->a);

    // A suspended call coming back to life. Checked before anything else: this
    // address is inside the routine's body, so nothing else should claim it.
    CosimCall* resumed = find_resume(c, pc);
    if (resumed) {
      if (c->mode == COSIM_NATIVE) {
        run_native_segment(c, resumed);
        return;  // the PC moved
      }
      resume_verify(c, resumed);
    }

    // Has an in-flight call returned? Both the PC and the stack pointer have to
    // match, which is what keeps an NMI that happens to pass through the same
    // address from ending the call early.
    while (c->priv->depth > 0) {
      CosimCall* call = &c->priv->stack[c->priv->depth - 1];
      const CosimRoutine* r = c->stats[call->index].routine;
      if (call->suspended) break;  // parked; it cannot be returning
      if (pc != call->ret_pc ||
          snes->cpu->sp != return_sp(call->entry_sp, r->ret_kind))
        break;
      c->priv->depth--;
      end_verify(c, call);
    }

    // A ported routine is about to suspend. Only verify mode sees this: native
    // mode never lets the ROM's instructions run, so its suspensions are the
    // ones it issues itself in run_native_segment().
    if (c->mode == COSIM_VERIFY && pc == THREAD_YIELD_ENTRY) {
      CosimCall* yielding = find_yield(c);
      if (yielding) suspend_verify(c, yielding);
    }

    for (int i = 0; i < c->stat_count; i++) {
      if (!(c->enabled & (UINT64_C(1) << i))) continue;
      const CosimRoutine* r = c->stats[i].routine;
      if (pc != r->entry) continue;
      // Verified but never substituted — see `CosimRoutine::verify_only`. Nothing
      // is counted, because nothing was offered: the report says `verify only`
      // against a row of zeroes rather than pretending this was a decline.
      if (c->mode == COSIM_NATIVE && r->verify_only) break;
      // The port has said it cannot handle this one. Step aside entirely and
      // let the ROM's own instructions run — in both modes, so that `verify`
      // and `run` decline exactly the same calls.
      if (!guard_allows(c, r)) {
        c->stats[i].calls++;
        c->stats[i].declined++;
        break;
      }
      if (c->mode == COSIM_NATIVE) {
        if (!run_native(c, i, r, &c->stats[i])) break;
        return;  // the PC moved; do not also execute the entry instruction
      }
      begin_verify(c, i, r, &c->stats[i]);
      break;
    }
  } else if (snes->cpu->intWanted && c->priv->depth > 0 &&
             c->mode == COSIM_VERIFY) {
    // An interrupt is about to land inside a call we are verifying. The handler
    // will change WRAM that the port, which only models the routine, cannot
    // account for — so the call has to be abandoned rather than reported as a
    // divergence that is really ours.
    //
    // A *suspended* call is the one case where an interrupt is not a problem
    // but the entire point: being parked across an NMI is what yielding is for,
    // and the state the routine resumes with is re-snapshotted anyway. So the
    // walk stops at the first suspended call and leaves it alone.
    //
    // A resumable routine caught mid-segment is not abandoned either, because
    // dropping it would strand its context a segment behind the ROM's and make
    // every later comparison meaningless. The segment is marked spoiled instead:
    // the port is still run, to keep the two in step, but the result is counted
    // as interrupted rather than diffed.
    while (c->priv->depth > 0) {
      CosimCall* call = &c->priv->stack[c->priv->depth - 1];
      if (call->suspended) break;
      if (c->stats[call->index].routine->run_yield) {
        call->segment_spoiled = true;
        break;
      }
      c->priv->depth--;
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
    if ((c->enabled & (UINT64_C(1) << i)) && c->stats[i].failed) return true;
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
  printf("\n%-20s %8s %7s %8s %8s %6s %6s %6s  %-20s %s\n", "routine", "calls",
         "yields", "checked", "passed", "int.", "decl.", "stack", "ROM cycles",
         "result");
  printf("%-20s %8s %7s %8s %8s %6s %6s %6s  %-20s %s\n", "--------------------",
         "--------", "-------", "--------", "--------", "------", "------",
         "------", "--------------------", "------");

  int failures = 0;
  for (int i = 0; i < c->stat_count; i++) {
    if (!(c->enabled & (UINT64_C(1) << i))) continue;
    const CosimStat* s = &c->stats[i];
    char cycles[32] = "-";
    if (s->checked > 0 && c->mode == COSIM_VERIFY)
      snprintf(cycles, sizeof cycles, "%ld..%ld, mean %.0f", s->cycles_min,
               s->cycles_max, s->cycles_mean);

    const char* verdict;
    if (s->failed) { verdict = "FAIL"; failures++; }
    else if (c->mode == COSIM_NATIVE && s->routine->verify_only)
      verdict = "verify only";
    else if (s->checked > 0) verdict = "OK";
    else if (s->declined > 0) verdict = "all declined";
    else verdict = "not reached";

    char yields[16] = "-";
    if (s->routine->run_yield) snprintf(yields, sizeof yields, "%ld", s->yields);

    printf("%-20s %8ld %7s %8ld %8ld %6ld %6ld %6d  %-20s %s\n",
           s->routine->name, s->calls, yields, s->checked, s->passed,
           s->interrupted, s->declined, s->stack_waived, cycles, verdict);
    if (s->failed) printf("%22s%s\n", "", s->detail);
  }
  return failures;
}

// ---------------------------------------------------------------------------
// Branch coverage
// ---------------------------------------------------------------------------

// What the per-call diff cannot tell you: whether this movie ever made a call
// that *reaches* a given branch. See `src/port/coverage.h` for the argument;
// the short version is that a branch no input takes is agreed on by the ROM and
// the port for the same reason — neither of them runs it.
//
// This does not fail a run. An untaken branch is not a defect, it is a movie
// that has not been written yet, and saying so is the whole job.
int cosim_coverage_report(bool full) {
  int taken = 0;
  for (int i = 0; i < PORT_COVER_COUNT; i++)
    if (port_cover_hits[i] > 0) taken++;

  printf("\nBranch coverage: %d of %d marked sites taken.\n", taken,
         PORT_COVER_COUNT);
  // Worth saying once, where the numbers are: under `verify` the harness runs
  // the port once per *interception*, so a routine reached both directly and
  // through a ported caller has its sites counted once for each. The hit counts
  // are therefore call-weighted rather than event-weighted. Whether a site was
  // reached at all — the only thing this report claims — is unaffected.

  if (full) {
    printf("\n%-20s %-20s %10s  %s\n", "routine", "site", "hits", "what it means");
    printf("%-20s %-20s %10s  %s\n", "--------------------",
           "--------------------", "----------", "-------------");
    for (int i = 0; i < PORT_COVER_COUNT; i++)
      printf("%-20s %-20s %10lu  %s\n", port_cover_routine(i),
             port_cover_name(i), port_cover_hits[i], port_cover_what(i));
  }

  int missed = PORT_COVER_COUNT - taken;
  if (missed == 0) {
    printf("Every marked branch was reached — nothing here is unexercised.\n");
    return 0;
  }
  printf("%d never reached, so the port's code for %s unchecked by this run:\n",
         missed, missed == 1 ? "it is" : "them is");
  for (int i = 0; i < PORT_COVER_COUNT; i++) {
    if (port_cover_hits[i] > 0) continue;
    printf("    %-20s %-20s %s\n", port_cover_routine(i), port_cover_name(i),
           port_cover_what(i));
  }
  printf("These are not failures. They are the movie's gaps, and the fix for\n"
         "every one of them is an input that makes the game do it.\n");
  return missed;
}

// ---------------------------------------------------------------------------
// The decline census
// ---------------------------------------------------------------------------

// Where the ROM went when the port stepped aside. See `cosim_census_note`.
//
// Fixed capacity on purpose: the whole value of this is that the list is short
// enough to read and work through. If it ever overflows, that is the report
// saying the port is further from covering a path than a list can express, and
// the overflow line says so rather than silently truncating.
#define CENSUS_MAX 64

typedef struct {
  const char* kind;
  uint32_t addr;
  long count;
} CensusEntry;

static CensusEntry census[CENSUS_MAX];
static int census_count;
static long census_overflow;

void cosim_census_note(const char* kind, uint32_t addr) {
  for (int i = 0; i < census_count; i++) {
    if (census[i].addr == addr && !strcmp(census[i].kind, kind)) {
      census[i].count++;
      return;
    }
  }
  if (census_count == CENSUS_MAX) { census_overflow++; return; }
  census[census_count].kind = kind;
  census[census_count].addr = addr;
  census[census_count].count = 1;
  census_count++;
}

int cosim_census_report(void) {
  if (census_count == 0) return 0;

  printf("\nDeclined to, by address — where the ROM went when the port stepped\n"
         "aside. Each line is one unported routine, and the count is how much\n"
         "of this run porting it would have bought:\n");
  printf("\n%-16s %-10s %10s\n", "reached from", "address", "declines");
  printf("%-16s %-10s %10s\n", "----------------", "----------", "----------");

  // Selection sort, descending. 64 entries at most and it runs once.
  bool done[CENSUS_MAX] = {false};
  for (int n = 0; n < census_count; n++) {
    int best = -1;
    for (int i = 0; i < census_count; i++)
      if (!done[i] && (best < 0 || census[i].count > census[best].count)) best = i;
    done[best] = true;
    printf("%-16s $%02X:%04X   %10ld\n", census[best].kind,
           (census[best].addr >> 16) & 0xff, census[best].addr & 0xffff,
           census[best].count);
  }
  if (census_overflow)
    printf("...and %ld more declines at addresses past the %d this can hold.\n",
           census_overflow, CENSUS_MAX);
  return census_count;
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
    if (!(c->enabled & (UINT64_C(1) << i))) continue;
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
      movie_apply(&s->movie, s->snes, (int)s->last_frame);
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
  //
  // The native side is built *second* on purpose: `cosim_init` claims the
  // port's one APU hook, so whichever harness is constructed last owns it, and
  // the one that should own it is the one that runs port code.
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
