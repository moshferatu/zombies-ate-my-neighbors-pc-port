// Phase 3 co-simulation harness.
//
// The problem this solves: we want to replace ZAMN's 65816 routines with C, one
// at a time, and *know* — not hope — that each replacement behaves identically.
// PLAN.md calls for the zelda3 answer: run the original ROM under the reference
// emulator and the C reimplementation together, and assert the state stays
// byte-identical.
//
// Phase 2 already built this pattern five times over, in miniature. Each
// `zamn_assets verify-*` command plays a movie under the core, intercepts one
// ROM routine, re-runs the C port on the arguments the ROM was called with, and
// diffs the result. This is that, generalised: a registry of ported routines
// and one engine that can drive them three ways.
//
// ## The three modes
//
// **Verify** (`COSIM_VERIFY`) is the correctness instrument, and the one whose
// results mean the most. At the routine's entry the harness snapshots WRAM and
// the registers, lets the *ROM* run the routine to its return, snapshots again,
// then rewinds the snapshot and runs the C port on it. It diffs all 128 KB of
// WRAM plus the registers. The ROM is doing the driving throughout, so nothing
// about the game's execution or its timing is perturbed — a divergence is
// entirely the port's fault, which is what makes the signal clean.
//
// **Native** (`COSIM_NATIVE`) actually substitutes. At the entry PC the harness
// runs the C port against the emulator's own WRAM, writes the result registers
// into the CPU, burns a cycle budget standing in for the work the ROM would
// have done, and jumps the PC to the routine's own `RTS`/`RTL` so the core
// performs a normal return. The ROM's instructions never execute. This is the
// seam the finished port grows out of.
//
// **Lockstep** (`cosim_lockstep`) is how native mode gets checked: two cores,
// same movie, one stock and one with routines substituted, with all of WRAM
// compared at every frame boundary. It is the whole-program version of what
// verify does per call — and, unlike verify, it *is* sensitive to timing, since
// a native routine returns on a cycle budget rather than by executing the
// original instructions. See `docs/cosim.md`.
//
// This header is harness code, not port code: it may use the emulator. The
// routines it drives live in `src/port/`, which may not.

#ifndef COSIM_COSIM_H
#define COSIM_COSIM_H

#include <stdbool.h>
#include <stdint.h>

#include "snes.h"

#include "assets/rom.h"
#include "port/coroutine.h"
#include "port/wram.h"

// ---------------------------------------------------------------------------
// Describing a ported routine
// ---------------------------------------------------------------------------

// The registers a routine reads and leaves behind. A 65816 routine's contract
// is its registers plus WRAM, and the port has to reproduce both, so the shim
// for each routine unpacks the first three fields on the way in and fills them
// on the way out.
//
// Flags are opt-in. Modelling N/Z/C exactly means reading whichever instruction
// happened to fall last in the routine, which is real work with no payoff for a
// routine whose callers never branch on the result. `flags` says which ones the
// shim actually modelled, and the report prints it, so the claim stays exactly
// as strong as the evidence.
enum {
  COSIM_FLAG_N = 1 << 0,
  COSIM_FLAG_Z = 1 << 1,
  COSIM_FLAG_C = 1 << 2,
};

// A, X and Y are claimed by default and every leaf routine claims all three.
// Resumable routines are why the mask exists.
//
// `thread_yield` does not preserve the accumulator or the index registers: the
// scheduler resumes a thread with `LDA thread_sp,X : TCS : PLD : PLP : PLB :
// RTL`, which restores D, P and B and leaves A holding the parked stack pointer
// and X holding the slot index. So a routine that suspends can only be held to
// the registers its *own* code re-established after the last resume. Claiming
// the rest would not be strictness, it would be asserting the scheduler's
// leftovers — and the moment the port's scheduler replaces the ROM's in Phase 4
// those leftovers legitimately change.
enum {
  COSIM_REG_A = 1 << 0,
  COSIM_REG_X = 1 << 1,
  COSIM_REG_Y = 1 << 2,
  COSIM_REG_ALL = COSIM_REG_A | COSIM_REG_X | COSIM_REG_Y,
};

typedef struct {
  uint16_t a, x, y;
  bool n, z, c;
  uint8_t flags;  // which of N/Z/C the shim modelled; 0 = none
  uint8_t regs;   // which of A/X/Y the shim modelled; defaults to all three
  // Inputs only, and never diffed. Direct page and data bank are part of a
  // 65816 routine's calling convention exactly as A/X/Y are, and two routines
  // need them: a collision handler runs on its *thread's* direct page (see
  // `port/collide.h`), and `$80:8480`'s exit flags come from the `PLB` that
  // restores its caller's data bank. Every routine here saves and restores both,
  // so there is nothing on the way out to compare.
  uint16_t d;
  uint8_t db;
} CosimRegs;

// How the routine gets back to its caller — which decides how many bytes of
// return address are on the stack, and which opcode `COSIM_NATIVE` jumps to.
typedef enum {
  COSIM_RTS,  // reached by JSR: 2 bytes on the stack
  COSIM_RTL,  // reached by JSL: 3 bytes
} CosimReturn;

// A range of WRAM this routine is allowed to differ in, and why.
//
// There is exactly one honest reason to use this and it is worth spelling out:
// a 65816 routine with one index register spills the caller's X into a scratch
// byte and restores it before returning. A C function keeps it in a local. The
// spill slot is dead storage that nothing reads across the call, so the port
// does not write it — and the diff has to be told, in writing, that this is the
// difference it is seeing. Anything else that turns up here is a porting bug
// wearing a disguise.
typedef struct {
  uint32_t offset, length;
  const char* why;
} CosimExclude;

// Run the C port. `w` is the WRAM to work on — a private copy under
// `COSIM_VERIFY`, the emulator's live RAM under `COSIM_NATIVE` — and `in` holds
// the registers as the ROM routine was entered. Fill `out`.
typedef void (*CosimShim)(Wram* w, const Rom* rom, const CosimRegs* in,
                          CosimRegs* out);

// Can the port stand in for *this* call?
//
// Some routines are mostly ported and partly not: `actor_overlap_pass` walks
// every visible pair itself, but when two of them actually touch it dispatches
// into the colliding actors' own handlers (`$80:BE8F` -> `$80:8480`), which is
// arbitrary game logic nobody has ported yet. Half a routine is still worth
// having — the walk is the expensive, fiddly part — but only if the half that is
// missing can never be silently skipped.
//
// So a routine may declare a guard, and the engine asks it *before* the port
// runs. `scratch` is a private, throwaway copy of live WRAM, so the guard is
// free to run the port itself and answer with whatever it returns; nothing it
// writes is kept. A `false` means the harness steps aside completely: the ROM's
// own instructions run, in both modes, and the call is counted as declined
// rather than checked. Nothing is claimed about a call the port did not make.
//
// The discipline this has to keep, or it stops being honest: a decline is an
// enumerated condition the routine's own code detects and reports, never a
// fallback for "the diff failed". Every one of them is printed.
typedef bool (*CosimGuard)(Wram* scratch, const Rom* rom, const CosimRegs* in);

// The same, for a routine that suspends — see `src/port/coroutine.h` and
// `docs/threads.md`.
//
// One call to this runs **one segment**: from the routine's entry to its first
// `thread_yield`, or from one resumption to the next, or from the last
// resumption to the `RTL`. `ctx` is the port's parked state, which the harness
// zeroes at entry and preserves untouched across every suspension; `ticks` is
// the sleep count the ROM would have had in A at its `JSL thread_yield`, and it
// is diffed like any other output. `out` is only read when the return is
// `PORT_RETURNED`, because a yield's contract is WRAM plus the tick count and
// nothing else — `thread_yield` clobbers the registers on the way through.
typedef PortStep (*CosimYieldShim)(Wram* w, const Rom* rom, const CosimRegs* in,
                                   CosimRegs* out, void* ctx, uint16_t* ticks);

typedef struct {
  const char* name;      // as it appears on the command line
  const char* symbol;    // the name in tools/symbols/zamn.sym
  uint32_t entry;        // PC the ROM routine starts at
  uint32_t ret_op;       // an RTS/RTL belonging to it, jumped to when substituting
  CosimReturn ret_kind;
  CosimShim run;
  // Set instead of `run` for a resumable routine, along with the three fields
  // below it. `run` and `run_yield` are mutually exclusive.
  CosimYieldShim run_yield;
  // Optional. Asked at the entry PC; a `false` leaves the call to the ROM.
  CosimGuard supported;
  // One past the routine's last byte. Used to decide whether a `JSL
  // thread_yield` the core is about to execute belongs to *this* routine —
  // every thread in the game yields, so the entry PC alone means nothing.
  uint32_t end;
  // A `JSL thread_yield` inside the routine. When the port suspends, native
  // mode puts the tick count in A and jumps here, so the *core* performs the
  // suspension: the same instruction, the same pushes, the same parked stack.
  // Same reasoning as `ret_op` — the routine already contains the code that
  // does the 65816 part correctly.
  uint32_t yield_op;
  int ctx_size;  // sizeof the port's context struct
  const CosimExclude* excludes;
  int exclude_count;
  // Cycles a substituted call burns in place of the ROM's instructions.
  // Measured, not guessed: `zamn_cosim verify` reports the real distribution
  // per routine and this is the mean it reported. See docs/cosim.md.
  int cycles;
  // How many bytes of stack the ROM's version pushes and abandons. Also
  // measured — it is the `stack` column `verify` prints, which is derived from
  // how deep the stack pointer actually went. Native mode uses it to know which
  // bytes it is *expected* to leave stale, since the port pushes nothing.
  int stack_bytes;
  // Checked per call by `verify`, never substituted by `run`. The report prints
  // the row with `verify only` where a verdict would go, so the exclusion is
  // visible rather than silent.
  //
  // Exactly one routine needs this, and what it needs it for is a property of the
  // routine rather than a gap in the port: **`$80:CCC8`'s body is a bus
  // handshake, and the thing that performs a handshake is the CPU.** Eight of its
  // eleven instructions are `CPY $2143 : BNE`, a spin that ends when the SPC700
  // echoes the last sequence number back. The port computes the routine's memory
  // effect and its register contract exactly — 23,997 calls on one movie, every
  // one passing — and `apu_drive` can put the right bytes on the right ports.
  // What neither can do is *wait*, because substitution's whole mechanism is to
  // stop the 65816 executing, and during a driver upload the SPC does not answer
  // until the CPU has been round that loop a while. Advancing the machine from
  // inside the hook without running opcodes is not a substitute for it: the SPC
  // was measured running 12,959 of its own cycles inside one such wait — still in
  // the driver's RAM-clear init loop — where the ROM's own spin at the same point
  // took about a thousand.
  //
  // `apu_play_sfx` sits directly on top of it and *is* substituted, which is not
  // an inconsistency but the same fact from the other side: a lone sound effect
  // finds the SPC caught up from sounds ago, so its wait is satisfied by the first
  // read and no spin happens. It is the uploader's 23,820 back-to-back commands
  // that need the wait to be real.
  //
  // And this is a statement about the harness, not about Phase 4. The finished
  // port owns its own main loop and can spin on `$2143` exactly as the ROM does.
  // What it cannot do is spin while impersonating one instruction inside somebody
  // else's core.
  bool verify_only;
} CosimRoutine;

// The registry. Every routine `src/port/` has replaced, in the order they were
// ported.
const CosimRoutine* cosim_routines(int* count);

// Look one up by `name`, or NULL.
const CosimRoutine* cosim_find(const char* name);

// ---------------------------------------------------------------------------
// Running
// ---------------------------------------------------------------------------

typedef enum {
  COSIM_VERIFY,  // ROM drives; the port is checked against it per call
  COSIM_NATIVE,  // the port drives; the ROM's instructions are skipped
} CosimMode;

// What one routine did over a run.
typedef struct {
  const CosimRoutine* routine;
  long calls;      // entries seen
  long checked;    // verified to completion (VERIFY) / substituted (NATIVE)
  long passed;
  long interrupted;  // abandoned: an interrupt landed inside the call window
  // Handed back to the ROM by the routine's own guard — see `CosimGuard`. These
  // are calls the port never made, so nothing about them is claimed either way.
  long declined;
  // Resumable routines only: suspensions seen. `checked` counts *segments* for
  // these — the run between two yields is what gets diffed — so a routine with
  // one activation and fifteen yields reports 1 call and 16 segments checked.
  long yields;
  // Widest run of dead stack the diff waived on any one call, in bytes — how
  // much of WRAM the routine's own pushes put out of reach. Printed so the
  // strength of an "OK" is visible rather than assumed.
  int stack_waived;
  // Cycle cost of the ROM's own instructions, over the calls that completed.
  long cycles_min, cycles_max;
  double cycles_mean;
  // First divergence, if any.
  bool failed;
  char detail[256];
} CosimStat;

// In-flight calls and the scratch WRAM the port runs against. Private to the
// engine, but held per-harness so two of them can run side by side — which is
// exactly what lockstep does.
typedef struct CosimPriv CosimPriv;

typedef struct {
  Snes* snes;
  Rom rom;
  CosimMode mode;
  CosimPriv* priv;

  // Which routines are live, as a bitmask over the registry.
  uint32_t enabled;

  CosimStat stats[32];
  int stat_count;

  long frames;
  bool stop_on_fail;
  bool verbose;
} Cosim;

// Attach to a core that already has the ROM loaded. Does not reset it.
void cosim_init(Cosim* c, Snes* snes, CosimMode mode);
void cosim_free(Cosim* c);

// Turn a routine on by name. False if there is no such routine.
bool cosim_enable(Cosim* c, const char* name);
void cosim_enable_all(Cosim* c);

// Step the core one CPU cycle, interposing on any enabled routine. This is the
// whole engine: everything else is reporting.
void cosim_step(Cosim* c);

// Step to the end of the current frame, as the reference core defines a frame.
void cosim_frame(Cosim* c);

// True if any enabled routine has diverged.
bool cosim_failed(const Cosim* c);

// Print the per-routine table. Returns the number of routines that failed.
int cosim_report(const Cosim* c);

// Print which of the port's marked branches this run actually took, and name
// the ones it did not. `full` prints every site with its hit count; otherwise
// only the summary and the untaken ones. Returns how many were never reached.
//
// This is a different question from the diff's, and it is the one the diff
// cannot ask: `verify` proves the port agrees with the ROM on the calls the
// movie made, which says nothing about a branch the movie never reaches. See
// `src/port/coverage.h`. The counters are process-global, so this reports the
// whole run rather than one `Cosim`.
//
// Never a failure. An untaken branch is a movie that has not been written yet.
int cosim_coverage_report(bool full);

// ---------------------------------------------------------------------------
// The decline census
// ---------------------------------------------------------------------------

// Record that a call was declined *because of* the code at `addr`.
//
// The `decl.` column counts declines and the coverage report names the branch
// that decided one, but neither says where the ROM went instead. A guard that
// knows the address calls this with it, and the report prints the distinct ones
// with their counts — so "1,959 dispatches to a handler the port does not have"
// becomes a list of handler entry points, in descending order of how much
// porting each one would buy.
//
// `kind` groups them ("handler", "player id table"); it must be a literal, and
// the pair (kind, addr) is the key. Counters are process-global, like the
// coverage ones, and for the same reason: they describe the run, not a `Cosim`.
void cosim_census_note(const char* kind, uint32_t addr);

// Print the census, most-declined first. Returns the number of distinct
// addresses. Silent, and 0, when nothing declined.
int cosim_census_report(void);

// ---------------------------------------------------------------------------
// Lockstep
// ---------------------------------------------------------------------------

// Run two cores on the same movie for `frames` frames — one stock, one with
// `enabled` routines substituted natively — and compare all of WRAM at every
// frame boundary.
//
// `movie_path` may be NULL for no input. Returns 0 if the two agreed for the
// whole run, and prints the first frame and address at which they did not.
int cosim_lockstep(const uint8_t* rom_data, int rom_len, const char* movie_path,
                   int frames, const char* const* names, int name_count,
                   bool verbose);

#endif
