#include "cosim/cosim.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "analysis/movie_apply.h"
#include "cosim/waits.h"
#include "port/apu.h"
#include "port/coverage.h"
#include "port/thread.h"

// One in-flight call. Calls nest — an NMI can land inside a routine and the
// handler can call another registered routine — so these live on a stack.
typedef struct {
  int index;          // which registry entry
  uint16_t entry_sp;  // SP as the routine was entered
  uint16_t min_sp;    // lowest SP seen during the call — see dead_stack()
  uint32_t ret_pc;    // where it will return to, read off the stack at entry
  uint64_t cycles;    // core cycle count at entry
  bool hdma;          // ...and whether the PPU was stealing any, at entry
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
  // The block the 24 thread stacks occupy, read out of the ROM at init rather
  // than declared. See stack_area().
  uint32_t stack_lo, stack_hi;
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
  r->s = cpu->sp;
  r->fastrom = snes->fastMem;
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

// Everything above the thread stacks that is also stack: the scheduler's own,
// topped at $125F, NMI's at $129F, and the thread bookkeeping tables between
// them. The bottom end is not a constant — see stack_area_lo() — because it is
// a table in the ROM, and this file had it wrong by sixteen stacks.
#define STACK_AREA_HI 0x1300

// Where the thread stacks start, read out of the ROM instead of described.
//
// `$80:830E` is a 24-entry table of initial stack pointers, one per scheduler
// slot, and `thread_spawn` installs slot n's entry with `TCS`. The table *is*
// the map, so there is no reason to keep a second copy of it in a comment: the
// entries are 24 values exactly 48 bytes apart, which makes the block
// contiguous from $7E:0CF6 up to $7E:1175.
//
// This replaces a hardcoded $1000. That number came from `docs/wram-map.md`,
// which says "$7E:1120-$7E:114F, 48 B, active thread stacks" — a trace had seen
// *one* stack, slot 23's, and the note described it as all of them. Sixteen of
// the twenty-four are below $1000, so their dead bytes were being reported as
// unexplained divergences, which is what the "Cause B" investigation in
// `docs/cosim.md` was chasing.
//
// The low end is clamped to $0D00 rather than $0CF6. The 24 direct pages run to
// $0CFF and slot 11's owns $0C80-$0CFF, whose offsets $76-$7F are live state —
// `STEP_DP_SPEED_CLASS` is one of them. Waiving those ten bytes would blind the
// diff to real data in order to excuse stack residue nothing has ever been seen
// to leave there. Reporting them if it ever happens is the cheaper mistake.
static uint32_t stack_area_lo(const Rom* rom) {
  uint32_t lo = 0xffffu;
  for (uint32_t slot = 0; slot < 24 * 2; slot += 2) {
    uint32_t top = rom_word(rom, THREAD_SP_TABLE + slot);
    if (top < lo) lo = top;
  }
  lo -= 0x2f;  // the stacks are 48 bytes each and grow down from their top
  return lo < 0x0d00u ? 0x0d00u : lo;
}

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
  c->priv->stack_lo = stack_area_lo(&c->rom);
  c->priv->stack_hi = STACK_AREA_HI;
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
      cosim_mask_set(&c->enabled, i);
      return true;
    }
  }
  return false;
}

void cosim_enable_all(Cosim* c) {
  int count = 0;
  cosim_routines(&count);
  cosim_mask_first(&c->enabled, count);
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
// What the call the port just served would have cost
// ---------------------------------------------------------------------------

// What the last shim reported, or -1 if it reported nothing. A global for the
// same reason the coverage counters are one: the shim signature belongs to all
// 111 routines and this concerns four of them, and there is never more than one
// shim running — a port routine calls no other shim, and lockstep steps its two
// cores one after the other rather than side by side.
static int g_cosim_cost = -1;

void cosim_cost(int cycles) { g_cosim_cost = cycles; }

// Compare a reported cost against what the ROM's own instructions really took.
// See `cosim_cost` for why a right answer here is a multiple of 40 rather than
// zero, and `CosimStat::model_hdma` for when it is neither.
//
// Is the PPU stealing cycles from the CPU? See `CosimStat::model_hdma`: an
// armed channel transfers at the start of every scanline, and those cycles land
// inside a routine's measured cost without being any part of what the routine
// did.
static bool hdma_armed(const Snes* snes) {
  for (int i = 0; i < 8; i++)
    if (snes->dma->channel[i].hdmaActive) return true;
  return false;
}

static void record_model(CosimStat* s, long actual, const CosimRegs* in,
                         bool hdma) {
  if (g_cosim_cost < 0) return;
  const long err = actual - (long)g_cosim_cost;

  s->modelled++;
  if (s->modelled == 1) {
    s->model_err_min = s->model_err_max = err;
  } else {
    if (err < s->model_err_min) s->model_err_min = err;
    if (err > s->model_err_max) s->model_err_max = err;
  }
  s->model_err_mean += ((double)err - s->model_err_mean) / (double)s->modelled;
  if (hdma) {
    // Only the direction is checkable here: the model must never claim a call
    // cost *more* than the ROM took, whatever the PPU was doing alongside.
    s->model_hdma++;
    if (err >= 0) return;
  } else if (err >= 0 && err % COSIM_REFRESH_CYCLES == 0) {
    s->model_refresh_exact++;
    return;
  }
  if (!s->model_bad) {
    s->model_bad = true;
    s->model_bad_model = g_cosim_cost;
    s->model_bad_actual = actual;
    s->model_bad_d = in->d;
    s->model_bad_db = in->db;
  }
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

// Burn a substituted routine's cycle budget, and remember that we did.
//
// Every cycle counted here is a cycle the 65816 would have spent executing the
// routine's own instructions and did not, which makes this the numerator of the
// native work share — measured at the seam rather than inferred from a trace.
static void cycles_burn(Cosim* c, int cycles) {
  if (cycles <= 0) return;
  uint64_t before = c->snes->cycles;
  snes_runCycles(c->snes, cycles);
  c->work.cycles_native += c->snes->cycles - before;
}

// The longest run of cycles a real 65816 access can take, and the size a
// reported cost is burned in.
//
// `snes_runCycles` adds 40 for a DRAM refresh when *one call* crosses the end of
// a scanline, so a routine's cost handed over in one piece gets one refresh
// however many scanlines it spans, while the ROM executing the same work in
// twelve-cycle bites gets one per scanline. For a 92-cycle call that is nothing;
// for a 7,524-cycle one it is five refreshes the machine never sees. Burning a
// cost in pieces no longer than a single access puts them back exactly where the
// clock says they belong, and costs one loop.
#define COSIM_BURN_PIECE 12

// Burn a cost the routine reported for itself. See `cosim_cost`: the number
// excludes refresh, which is precisely what this lets the core add.
static void cycles_burn_modelled(Cosim* c, int cycles) {
  uint64_t before = c->snes->cycles;
  while (cycles > 0) {
    const int piece = cycles < COSIM_BURN_PIECE ? cycles : COSIM_BURN_PIECE;
    snes_runCycles(c->snes, piece);
    cycles -= piece;
  }
  c->work.cycles_native += c->snes->cycles - before;
}

// The cost of the call that just ran: whatever the shim reported, or the
// routine's declared constant if it reported nothing.
static void cycles_burn_call(Cosim* c, const CosimRoutine* r) {
  if (g_cosim_cost >= 0) cycles_burn_modelled(c, g_cosim_cost);
  else cycles_burn(c, r->cycles);
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
  g_cosim_cost = -1;
  PortStep step =
      r->run_yield((Wram*)snes->ram, &c->rom, &in, &out, call->ctx, &ticks);

  s->checked++;
  s->passed++;

  // Stand in for the work the ROM's instructions would have done, so the rest
  // of the machine — the PPU's beam position, the APU, DMA — still sees a
  // segment that took about as long as it used to.
  //
  // The delta is measured rather than assumed to be `r->cycles`, because
  // `snes_runCycles` adds 40 for a DRAM refresh when the budget crosses the end
  // of a scanline. Those cycles are the machine's, so they are the port's here:
  // whatever the burn actually advanced the core by is what the ROM's own
  // instructions no longer have to. See `CosimWork`.
  cycles_burn_call(c, r);

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
    // The *call*, not the segments. A routine that suspends fifteen times is
    // still one `JSL` the ROM did not have to serve, and the call share's
    // denominator counts calls.
    c->work.calls_native++;
    run_native_segment(c, call);
    return true;
  }

  CosimRegs in, out;
  regs_capture(snes, &in);
  out = in;
  g_cosim_cost = -1;
  r->run((Wram*)snes->ram, &c->rom, &in, &out);

  // Stand in for the work the ROM's instructions would have done.
  cycles_burn_call(c, r);
  native_return(c, r, &out);

  s->calls++;
  s->checked++;
  s->passed++;
  c->work.calls_native++;
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
  call->hdma = hdma_armed(c->snes);
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
  g_cosim_cost = -1;
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
  const long actual = (long)(c->snes->cycles - call->cycles);
  record_cycles(s, actual);
  // Sampled at both ends, because a channel armed at any point in the window
  // will have transferred somewhere inside it.
  record_model(s, actual, &call->in, call->hdma || hdma_armed(c->snes));
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

static void cosim_step_inner(Cosim* c) {
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
      if (!cosim_mask_get(&c->enabled, i)) continue;
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

// The opcode about to execute, straight out of the cartridge image.
//
// Read from `c->rom` rather than through the core's bus, deliberately: a bus
// read of an I/O address has side effects, and instrumentation that changes the
// machine it is measuring is worse than no instrumentation. Every instruction
// this game executes is in the `$80-$BF` FastROM mirror, so `rom_ptr` answers
// for all of them and returns NULL for anything that is not cartridge — which
// is then simply not counted, rather than guessed at.
static int opcode_at(const Cosim* c, uint32_t pc) {
  uint32_t avail = 0;
  const uint8_t* p = rom_ptr(&c->rom, pc, &avail);
  return p ? *p : -1;
}

// One instruction, with the accounting around it.
//
// `snes_runCpuCycle` is one whole opcode, not one clock, so the cycles the core
// advances across a step are exactly that instruction's cost and the PC at the
// top is exactly the instruction being paid for. That is what makes this
// measurement cheap enough to leave switched on in a build people play: one
// range check and one cartridge byte per instruction.
//
// Substitution is the one case where a step spends cycles without executing an
// instruction — `run_native` burns the budget and returns before
// `snes_runCpuCycle`. Those cycles are attributed by `cycles_burn`, and they
// cannot land in `cycles_idle` or `cycles_wait` as well, because the PC at the
// top of such a step is a registry entry: never a wait site, never halted.
void cosim_step(Cosim* c) {
  Snes* snes = c->snes;
  const uint64_t before = snes->cycles;

  // Nothing is executing at all: the CPU is halted on the scheduler's `WAI`
  // (or a `STP`), waiting for the NMI that starts the next frame. `waiting` is
  // also what makes `at_instruction` false, so this is the same test the engine
  // below already trusts, asked one line earlier.
  const bool halted = snes->cpu->waiting || snes->cpu->stopped;

  // ...and if something is, which instruction, and is it a call or a spin.
  uint32_t pc = 0;
  bool counted_call = false;
  bool spinning = false;
  if (!halted && at_instruction(snes)) {
    pc = cpu_pc24(snes);
    spinning = cosim_is_wait_site(pc);
    switch (opcode_at(c, pc)) {
      case 0x20:  // JSR abs
      case 0x22:  // JSL long
      case 0xFC:  // JSR (abs,X)
        counted_call = true;
        break;
      default:
        break;
    }
  }

  cosim_step_inner(c);

  const uint64_t spent = snes->cycles - before;
  c->work.cycles_total += spent;
  if (halted) c->work.cycles_idle += spent;
  else if (spinning) c->work.cycles_wait += spent;
  // Every call the game made, including the ones the port went on to serve:
  // substitution happens at the *callee's* entry PC, so the caller's `JSR` has
  // already executed by then and is counted here either way. `calls_native` is
  // a subset of this, not a second bucket to add to it.
  //
  // The denominator shrinks as the port grows, which is the property that makes
  // the ratio mean anything: calls made *inside* a substituted routine never
  // execute at all, so a routine that used to contribute its own call plus six
  // of its callees' now contributes one — and that one is served.
  if (counted_call) c->work.calls_total++;
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
    if (cosim_mask_get(&c->enabled, i) && c->stats[i].failed) return true;
  return false;
}

const CosimRoutine* cosim_find(const char* name) {
  int count = 0;
  const CosimRoutine* all = cosim_routines(&count);
  for (int i = 0; i < count; i++)
    if (!strcmp(all[i].name, name)) return &all[i];
  return NULL;
}

// What the routines that priced themselves got right, and by how much they were
// wrong. Silent unless something reported — see `cosim_cost`.
//
// The column that matters is `refresh-exact`. A cost model is the routine's own
// instruction cycles and nothing else, so its error is what the *machine* added
// on top: 40 cycles per scanline the call crossed, and never zero. What makes a
// model right is that every one of those errors is a whole number of refreshes.
// One call whose error is not says the model has an instruction wrong, and no
// amount of averaging hides it here.
//
// The denominator is calls made while the PPU was not also stealing cycles. An
// armed HDMA channel transfers at every scanline start and those cycles land
// inside the measurement, in amounts that depend on what is being drawn — so
// calls made under HDMA are counted separately and held only to the direction:
// the model must never claim more than the ROM took. See `CosimStat::model_hdma`.
static void cost_model_report(const Cosim* c) {
  int any = 0;
  for (int i = 0; i < c->stat_count; i++)
    if (cosim_mask_get(&c->enabled, i) && c->stats[i].modelled > 0) any++;
  if (!any) return;

  printf("\ncost models (error is what the ROM took, less what the port said it\n"
         "would; a correct model is short by one 40-cycle DRAM refresh per\n"
         "scanline the call crossed, and by more wherever HDMA was running)\n\n");
  printf("  %-20s %10s %10s  %-24s %14s %s\n", "routine", "priced", "of",
         "error", "refresh-exact", "under HDMA");
  for (int i = 0; i < c->stat_count; i++) {
    if (!cosim_mask_get(&c->enabled, i)) continue;
    const CosimStat* s = &c->stats[i];
    if (s->modelled == 0) continue;

    char err[32], exact[32];
    snprintf(err, sizeof err, "%+ld..%+ld, mean %+.0f", s->model_err_min,
             s->model_err_max, s->model_err_mean);
    const long quiet = s->modelled - s->model_hdma;
    snprintf(exact, sizeof exact, "%ld/%ld", s->model_refresh_exact, quiet);
    // `model_bad` and not `exact != quiet`: a call made under HDMA is not held
    // to refresh-exactness, but it is still held to the direction, and a model
    // that over-claimed there would slip past a comparison of those two counts.
    printf("  %-20s %10ld %10ld  %-24s %14s %10ld%s\n", s->routine->name,
           s->modelled, s->checked, err, exact, s->model_hdma,
           s->model_bad ? "  <-- MODEL WRONG" : "");
    if (s->model_bad)
      printf("  %-20s   first wrong: model %ld, ROM %ld, out by %ld — D=$%04X, DB=$%02X\n",
             "", s->model_bad_model, s->model_bad_actual,
             s->model_bad_actual - s->model_bad_model, s->model_bad_d,
             s->model_bad_db);
  }
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
    if (!cosim_mask_get(&c->enabled, i)) continue;
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

  cost_model_report(c);
  return failures;
}

// ---------------------------------------------------------------------------
// How much of the run was native
// ---------------------------------------------------------------------------

// `12345678` -> `"12,345,678"`. These numbers run to nine digits and the report
// is meant to be read at a glance, which an undifferentiated run of digits is
// not. Four rotating buffers so several can appear in one `printf`; nothing
// here prints more than three, and the fourth is the margin that stops a future
// fourth argument silently overwriting the first.
static const char* fmt_u64(uint64_t v) {
  static char buf[4][32];
  static int slot;
  char* out = buf[slot = (slot + 1) & 3];

  char digits[24];
  int n = 0;
  do { digits[n++] = (char)('0' + (v % 10)); v /= 10; } while (v);

  int w = 0;
  for (int i = n - 1; i >= 0; i--) {
    out[w++] = digits[i];
    if (i && i % 3 == 0) out[w++] = ',';
  }
  out[w] = '\0';
  return out;
}

void cosim_share(const Cosim* c, CosimShare* out) {
  const CosimWork* w = &c->work;
  memset(out, 0, sizeof *out);

  // The denominator is the cycles a CPU was actually doing something. `idle` is
  // a halted processor and `wait` is a spinning one; neither is work, and
  // neither is work the port could take — see `src/cosim/waits.h`. Clamped
  // rather than trusted: the three buckets are measured independently and a
  // negative denominator should read as zero, not as an enormous share.
  uint64_t not_work = w->cycles_idle + w->cycles_wait;
  out->cycles_work = w->cycles_total > not_work ? w->cycles_total - not_work : 0;
  out->cycles_native = w->cycles_native;
  out->calls_total = w->calls_total;
  out->calls_native = w->calls_native;

  for (int i = 0; i < c->stat_count; i++)
    if (cosim_mask_get(&c->enabled, i))
      out->calls_declined += (uint64_t)c->stats[i].declined;

  if (out->cycles_work)
    out->work_share = (double)out->cycles_native / (double)out->cycles_work;
  if (out->calls_total)
    out->call_share = (double)out->calls_native / (double)out->calls_total;
}

void cosim_share_report(const Cosim* c) {
  CosimShare s;
  cosim_share(c, &s);

  if (c->mode != COSIM_NATIVE) {
    // Under `verify` the ROM executes every instruction of every routine and
    // the port is replayed alongside it, so nothing was substituted and the
    // honest answer is not "0%" — it is that the question was not asked.
    printf("\nNative share: not measured under verify — the ROM ran every\n"
           "instruction here by design, and the port was checked against it\n"
           "rather than standing in for it. Use `run`, or the game itself.\n");
    return;
  }

  printf("\nNative share — how much of this session the port ran, not the 65816\n");
  printf("\n  %-8s %14s of %-14s %6.1f%%\n", "work",
         fmt_u64(s.cycles_native), fmt_u64(s.cycles_work), 100.0 * s.work_share);
  printf("  %-8s %14s of %-14s %6.1f%%\n", "calls",
         fmt_u64(s.calls_native), fmt_u64(s.calls_total), 100.0 * s.call_share);

  // What each row means, and what it is not, because a percentage with no
  // denominator stated is the thing this whole report exists to replace.
  printf("\n  work  is SNES cycles: the budget every substituted routine burns\n"
         "        in place of the instructions the ROM no longer executes,\n"
         "        over the cycles the CPU spent working. %s more were\n"
         "        spent halted on the scheduler's WAI and %s going round\n"
         "        the %d busy-wait loops in src/cosim/waits.h; porting a spin\n"
         "        gives a spin, so neither is in the denominator.\n",
         fmt_u64(c->work.cycles_idle), fmt_u64(c->work.cycles_wait),
         COSIM_WAIT_SITE_COUNT);
  printf("  calls is JSR/JSL: every subroutine call the game made, against the\n"
         "        ones the port served at the callee's entry. Calls made inside\n"
         "        a substituted routine never execute, so they leave the\n"
         "        denominator as the port grows");
  if (s.calls_declined)
    printf("; %s were offered to the port\n        and handed back by a guard — the census below says to what.\n",
           fmt_u64(s.calls_declined));
  else
    printf(". Nothing was declined.\n");
  printf("\n  Neither counts a thread body or a vblank job as a call: the\n"
         "  scheduler and the vblank dispatcher reach those by RTL, so there is\n"
         "  no call to intercept and none to count. Their cycles are in the work\n"
         "  denominator, where they belong.\n");
  // How strong the work figure is, stated rather than left to be assumed. The
  // numerator is a per-routine mean standing in for a distribution, so it is
  // the one number here that is an estimate — and the size of the error is
  // measurable, by running the same movie `--stock` and comparing the work
  // denominators. On level 1 at 2,400 PPU frames that is 273.1M stock against
  // 282.3M substituted: the budgets over-pay for what they displaced by about
  // 16%, so this row reads high by roughly that much and not by a factor.
  //
  // Note that `zamn.exe --frames N` and `zamn_cosim run -f N` do not measure
  // the same stretch of game — N PPU frames against N scheduler passes after
  // boot — so their percentages differ for that reason before any other, and
  // only runs of the same kind are worth putting side by side.
  printf("\n  The work numerator is the measured *mean* cost of each routine,\n"
         "  so it stands in for a distribution and the row is an estimate. Run\n"
         "  the same input --stock and compare the two denominators to see by\n"
         "  how much: they should differ by about the budget, and do.\n");
  printf("\n  `tools/native_share.py` measures the same quantity offline from a\n"
         "  traced profile — instructions and a call-graph closure rather than\n"
         "  cycles at the seam, independent all the way down. Its\n"
         "  `...substituted only, likewise` line is the one to compare against\n"
         "  this row, over the same input and the same number of frames. Do not\n"
         "  compare it against the line above that, which counts routines that\n"
         "  are written and deliberately never substituted.\n");
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


// Can this byte's difference be explained without appealing to a bug?
//
// Four things can, and they are not equally strong — which is the point of
// separating them:
//
//   * a scratch byte the routine's descriptor declares the port does not write;
//   * a byte a substituted call is *known* to have left stale, because the
//     ROM's version would have pushed there. Native mode marks these as it
//     goes, from the push footprint `verify` measured, so the set is exactly
//     the bytes some real substitution skipped;
//   * `nmi_saved_sp`, which is not state at all — see below;
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
// What it costs is real and worth stating plainly: inside the stack area this
// run proves nothing. Everything outside it — 126 KB of actual game state — is
// compared byte for byte, and `verify` covers the routines' behaviour exactly,
// stack included.
//
// `nmi_saved_sp` is the narrow one, and it is exempt for a reason no other
// address in WRAM has: **its whole lifetime is one interrupt.** `$80:819C`
// writes it with `TSC : STA $04` and `$80:81EB` reads it back with
// `LDA $04 : TCS` seventy-nine bytes later, and nothing else in the game reads
// it — so at a sync point, between two NMIs, it is not live state, it is a
// fossil of *where the last NMI happened to land*. A substituted routine
// returns on a cycle budget rather than by executing the original instructions,
// so the two cores reach any given point a few cycles apart and NMI catches one
// of them a call deeper than the other. That is real drift and it is worth
// counting — `run` reports how many passes it showed up on — but it is a
// difference in timing, not in what the game computed, and no instruction can
// observe it.
static bool accounted_for(const Cosim* c, uint32_t off) {
  if (off >= c->priv->stack_lo && off < STACK_AREA_HI) return true;
  if (off == W_NMI_SAVED_SP || off == W_NMI_SAVED_SP + 1) return true;
  if (c->priv->stale && c->priv->stale[off]) return true;
  for (int i = 0; i < c->stat_count; i++) {
    if (!cosim_mask_get(&c->enabled, i)) continue;
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
// The game's own idea of what frame it is on. `$80:8186  INC $16` runs once per
// NMI and nothing else writes it, which makes it the only clock both cores
// agree to keep — see the resync in cosim_lockstep().
static uint16_t side_frame(const Side* s) {
  return (uint16_t)(s->snes->ram[W_NMI_FRAME_COUNTER] |
                    s->snes->ram[W_NMI_FRAME_COUNTER + 1] << 8);
}

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
  cosim_mask_none(&ref.cosim.enabled);

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
  bool reported = false, reported_bad = false;
  long compared = 0, unsynced = 0, diverged_frames = 0, explained_only = 0;
  // How often the two cores' last NMI landed at different call depths. Waived
  // by accounted_for() and counted here rather than either failed on or hidden:
  // it is the only visible symptom of the cycle budget being an estimate.
  long nmi_drift = 0;
  uint32_t nmi_drift_worst = 0;
  // The pass at which the two cores stopped being on the same frame, after
  // which nothing is comparable. See the loop.
  long parted_at = -1;
  uint32_t worst = 0, last_differ = 0, worst_unexplained = 0;
  uint32_t worst_unexplained_at[8];
  // The values as they stood *at that pass*. Reading them back out of the cores
  // when the run ends prints whatever the game has since written there, which
  // for a while had this report showing bytes that were identical on both sides
  // under a heading saying they differed.
  uint8_t worst_unexplained_ref[8], worst_unexplained_nat[8];
  int worst_unexplained_n = 0;
  long worst_unexplained_pass = -1;
  for (long pass = booted; pass < frames; pass++) {
    // One pass each. Timing may have drifted between the two cores, but they
    // are now at the same point in the *game*, which is what the diff is about.
    bool ref_ok = side_pass(&ref, 4000000);
    bool nat_ok = side_pass(&nat, 4000000);
    if (!ref_ok || !nat_ok) { unsynced++; continue; }

    // Where this run stops being able to prove anything, and why it is a
    // property of the method rather than a bug in the port.
    //
    // A scheduler pass is *nearly* a frame. The exception is a pass whose work
    // overruns vblank: it takes two NMIs instead of one, and on a level where
    // the game already fills most of a frame, whether that happens is decided
    // by a few hundred cycles either way. A substituted core spends a different
    // number of cycles doing the same work — that is what substitution *is* —
    // so sooner or later one side overruns a pass the other does not, and from
    // then on the two cores are one frame apart.
    //
    // Nothing can be realigned. Aligning on `$16` costs the lagging side an
    // extra scheduler pass, which puts `sched_tick` out by one instead: the two
    // clocks genuinely disagree, because one core really did run a pass in two
    // frames and the other in one. They are not computing different answers,
    // they are running different timelines of the same game — the difference
    // between a console that dropped a frame and one that did not.
    //
    // So the comparison stops here and says so. Everything before this point is
    // a real result; everything after would be frame N against frame N+1, and
    // reporting that as thousands of differing bytes is what this harness did
    // for months. Both cores keep running, because the call counts, coverage
    // and census below are still worth having.
    // Once parted, permanently: the counters can come back level later if the
    // other side overruns too, and comparing then would be worse than not
    // comparing at all — the frame numbers would agree while the game states
    // behind them had spent a hundred frames apart.
    uint16_t frame_ref = side_frame(&ref), frame_nat = side_frame(&nat);
    if (parted_at >= 0 || frame_ref != frame_nat) {
      if (parted_at < 0) {
        parted_at = pass;
        printf("\nThe two timelines part at pass %ld: stock is on frame %u and\n"
               "native on frame %u, so one of them overran vblank on a pass the\n"
               "other did not. Nothing past here is comparable — see the note in\n"
               "cosim_lockstep(). %ld passes were compared before it.\n",
               pass, frame_ref, frame_nat, compared);
      }
      continue;
    }
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

    uint32_t ref_sp = ref.snes->ram[W_NMI_SAVED_SP] |
                      (uint32_t)ref.snes->ram[W_NMI_SAVED_SP + 1] << 8;
    uint32_t nat_sp = nat.snes->ram[W_NMI_SAVED_SP] |
                      (uint32_t)nat.snes->ram[W_NMI_SAVED_SP + 1] << 8;
    if (ref_sp != nat_sp) {
      uint32_t gap = ref_sp > nat_sp ? ref_sp - nat_sp : nat_sp - ref_sp;
      nmi_drift++;
      if (gap > nmi_drift_worst) nmi_drift_worst = gap;
    }

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
    // ...and separately, the first pass carrying a difference nothing accounts
    // for. That is a different event from the first difference and the one that
    // matters: dead stack shows up early and stays inert, so a run that ends
    // badly did something *between* the two, and the gap is where to look.
    if (unexplained > 0 && !reported_bad) {
      char buf[32];
      printf("\nFirst *unaccounted* difference at pass %ld — %u of %u byte%s:\n",
             pass, unexplained, differ, differ == 1 ? "" : "s");
      uint32_t shown = 0;
      for (uint32_t i = 0; i < WRAM_SIZE && shown < 16; i++) {
        if (ref.snes->ram[i] == nat.snes->ram[i]) continue;
        if (accounted_for(&nat.cosim, i)) continue;
        printf("    %s  stock $%02X, native $%02X\n", wram_str(i, buf, sizeof buf),
               ref.snes->ram[i], nat.snes->ram[i]);
        shown++;
      }
      reported_bad = true;
    }
    if (unexplained > 0 && unexplained >= worst_unexplained) {
      // Keep the worst offender's addresses; a summary that says "5 bytes were
      // unexplained" without saying which is not a finding, it is a rumour.
      worst_unexplained_pass = pass;
      worst_unexplained_n = 0;
      for (uint32_t i = 0; i < WRAM_SIZE && worst_unexplained_n < 8; i++) {
        if (ref.snes->ram[i] == nat.snes->ram[i]) continue;
        if (accounted_for(&nat.cosim, i)) continue;
        worst_unexplained_ref[worst_unexplained_n] = ref.snes->ram[i];
        worst_unexplained_nat[worst_unexplained_n] = nat.snes->ram[i];
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
      printf("Every one, on all %ld passes, was inside the stacks "
             "($7E:%04X-$7E:%04X)\n"
             "or a declared scratch byte. No byte of live game state ever differed.\n",
             explained_only, nat.cosim.priv->stack_lo, STACK_AREA_HI - 1);
    else {
      char buf[32];
      printf("Up to %u byte%s could not be accounted for — worst at pass %ld:\n",
             worst_unexplained, worst_unexplained == 1 ? "" : "s",
             worst_unexplained_pass);
      for (int i = 0; i < worst_unexplained_n; i++)
        printf("    %s  stock $%02X, native $%02X\n",
               wram_str(worst_unexplained_at[i], buf, sizeof buf),
               worst_unexplained_ref[i], worst_unexplained_nat[i]);
    }
  }
  if (parted_at >= 0)
    printf("Comparison stopped at pass %ld of %d, where the timelines parted.\n",
           parted_at, frames);
  if (nmi_drift > 0)
    printf("NMI landed at a different call depth on %ld of %ld passes, by at\n"
           "most %u bytes of stack — the cycle budget being an estimate, made\n"
           "visible. Nothing reads nmi_saved_sp outside the NMI that wrote it.\n",
           nmi_drift, compared, nmi_drift_worst);
  if (booted > 0)
    printf("%ld pass%s spent booting, before there was a scheduler to sync on.\n",
           booted, booted == 1 ? "" : "es");
  if (unsynced > 0)
    printf("%ld pass%s not compared — a side never came back to the WAI.\n",
           unsynced, unsynced == 1 ? " was" : "es were");
  cosim_report(&nat.cosim);
  // ...and what fraction of the run that table represents. The native side is
  // the one to ask: the reference side has an empty mask by construction, so
  // its share is zero and saying so would be a fact about the control, not
  // about the port.
  cosim_share_report(&nat.cosim);

  movie_free(&ref.movie);
  movie_free(&nat.movie);
  cosim_free(&nat.cosim);
  cosim_free(&ref.cosim);
  snes_free(nat.snes);
  snes_free(ref.snes);
  return rc;
}
