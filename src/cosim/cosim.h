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

typedef struct {
  uint16_t a, x, y;
  bool n, z, c;
  uint8_t flags;  // which of the above the shim modelled; 0 = none
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

typedef struct {
  const char* name;      // as it appears on the command line
  const char* symbol;    // the name in tools/symbols/zamn.sym
  uint32_t entry;        // PC the ROM routine starts at
  uint32_t ret_op;       // an RTS/RTL belonging to it, jumped to when substituting
  CosimReturn ret_kind;
  CosimShim run;
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
