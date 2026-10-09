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
#include "port/hw.h"
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
// `V` is here for one routine and it was `run` that asked for it, not `verify`.
// `$80:9D39`'s `ADC` is the only arithmetic in the port whose overflow output
// outlives the call: the scheduler pushes `P` onto a thread's stack, so the bit
// becomes a byte of WRAM that the whole-program diff compares. Substituting the
// generator without publishing V left that byte differing by exactly `$40` —
// once on level 9, once on level 29, on movies where every other byte matched.
enum {
  COSIM_FLAG_N = 1 << 0,
  COSIM_FLAG_Z = 1 << 1,
  COSIM_FLAG_C = 1 << 2,
  COSIM_FLAG_V = 1 << 3,
};

// How many routines the registry may hold. It sizes `Cosim::stats` and
// `Cosim::enabled`, and `cosim_init` asserts the registry is within it — see
// the note on `enabled` for what happened the one time nothing did.
//
// It was 64 until the registry reached 65, at which point that assert did its
// job and said so instead of quietly measuring nothing. The mask is now a small
// bitset rather than a machine word, so the next raise is this line alone. It
// was 128 until the scheduler, the dispatchers and the NMI took it to 138, and
// 256 until the squirt gun's thread and the monster's walk took it to 257, and
// 448 until the martians' start and ends took it to 449, and 512 until the
// fishman's leap and twenty more thread ends took it to 519. It is 768.
#define COSIM_MAX_ROUTINES 768
#define COSIM_MASK_WORDS ((COSIM_MAX_ROUTINES + 63) / 64)

// Which routines are switched on. A struct rather than a `uint64_t` so it keeps
// assigning and comparing by value the way the old scalar did — `main_sdl.c`
// saves one across an F1 toggle by plain assignment, and that still works.
typedef struct {
  uint64_t w[COSIM_MASK_WORDS];
} CosimMask;

static inline bool cosim_mask_get(const CosimMask* m, int i) {
  return (m->w[i >> 6] >> (i & 63)) & 1u;
}

static inline void cosim_mask_set(CosimMask* m, int i) {
  m->w[i >> 6] |= UINT64_C(1) << (i & 63);
}

static inline void cosim_mask_none(CosimMask* m) {
  for (int i = 0; i < COSIM_MASK_WORDS; i++) m->w[i] = 0;
}

static inline void cosim_mask_first(CosimMask* m, int n) {
  cosim_mask_none(m);
  for (int i = 0; i < n; i++) cosim_mask_set(m, i);
}

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
  bool n, z, c, v;
  uint8_t flags;  // which of N/Z/C/V the shim modelled; 0 = none
  uint8_t regs;   // which of A/X/Y the shim modelled; defaults to all three
  // Inputs only, and never diffed. Direct page and data bank are part of a
  // 65816 routine's calling convention exactly as A/X/Y are, and two routines
  // need them: a collision handler runs on its *thread's* direct page (see
  // `port/collide.h`), and `$80:8480`'s exit flags come from the `PLB` that
  // restores its caller's data bank. Every routine here saves and restores both,
  // so there is nothing on the way out to compare.
  uint16_t d;
  uint8_t db;
  // The stack pointer, on entry, and it is here for the same reason as the two
  // above: **a stacked argument is part of a calling convention too.**
  // `$80:CD20` is the first routine in the registry whose caller passes
  // something on the stack rather than in a register — `PEA <source address>`
  // and then `JSL`, which the routine reads back with `LDA $06,S` once its own
  // `PHD` is down. At the shim's entry that word is at `s + 4`: three bytes of
  // return address the `JSL` pushed, and then the argument.
  //
  // Never diffed, and it could not be: the port pushes nothing, so its stack
  // pointer at the end is its own business. `stack_bytes` is what describes the
  // outgoing side.
  uint16_t s;
  // Is `$420D` set — are banks $80+ being fetched at 6 master cycles a byte
  // rather than 8? Also an input and also never diffed; it is here for the cost
  // models (`cosim_cost`), which are the only thing in the harness that cares
  // how long an instruction takes.
  //
  // The boot code sets it and this project assumed for eleven rounds that it
  // stayed set. It does not: on level 49 the same three instructions of
  // `$80:C139` were measured at 86 cycles and at 98, and the difference is
  // exactly 2 per byte of program the run fetches.
  bool fastrom;
  // The whole status byte, as `PHP` would push it, and where to go on at.
  // Both are for the routines that leave by a jump rather than a return (see
  // `CosimRoutine::exits`): on the way in `p` is what the CPU held, which the
  // first two above repeat in part; on the way out it is the status byte to
  // install, widths and all, and `pc` is the exit to continue at. Every other
  // routine leaves both alone.
  uint8_t p;
  uint32_t pc;
  // For those routines too: the bits of `p` the shim does not claim, which
  // stay as the CPU has them and go unchecked. Zero unless the shim says
  // otherwise. A stretch that runs a whole enemy frame ends on registers the
  // game overwrites before reading, and on an overflow some callee left that
  // the port does not follow; `regs` says which of A, X and Y it claims, as it
  // does for every other routine.
  uint8_t p_keep;
  // The auto-joypad latch, `$4218` and `$421A`. Inputs only, for the one
  // stretch of the NMI that reads them: a latch rather than a bus access, so
  // reading it here has no effect on the machine, and it holds still from the
  // end of the auto-read to the next vblank.
  uint16_t joy[2];
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

// A guard that only has to *look*: at the registers, or at a word of WRAM,
// never by running the port. It reads live WRAM and costs nothing, which is
// what a routine the scheduler calls twenty times a frame needs; `CosimGuard`
// copies all 128 KB first. Asked before `supported`, and a `false` is a
// decline like any other.
typedef bool (*CosimAccepts)(const Wram* live, const CosimRegs* in);

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

// A stretch of WRAM a routine publishes to the NMI, and how the ROM writes it.
//
// It is a span rather than a word because the two vblank queues are dispatched
// off their *slots*: `$80:83E0` reads `$12A0,X` and skips the slot when it is
// zero, using the count at `$0C` only as a gate and a running budget. So the
// instruction that publishes a queue A job is the `STA $12A0,X` at `$83CC`, not
// the `INC $0C` two instructions later, and holding back the count alone leaves
// the job visible a frame early exactly as before. The VRAM queue is the other
// way round -- `$80:9E7B` bounds its loop with `CPX $CE` -- so there the count
// really is the publishing word and the span is two bytes.
//
// `add` is the difference between `INC $0C` and `STA $12A0,X`, and it is not a
// detail: while a commit is held back the NMI handler writes these same
// locations itself, and `$80:840B` decrements the very count a queue add is
// waiting to increment. Replaying an absolute value over that would wipe the
// handler's decrement out; replaying the delta the routine computed does not.
// An `add` span is one 16-bit word. A plain span is replayed byte by byte and
// only where the routine actually changed something, which is what leaves the
// dispatcher's own zeroing of the *other* slots alone.
typedef struct {
  uint16_t at;   // WRAM offset
  uint16_t len;  // bytes; 2 when `add`
  bool add;      // ROM increments this word in place; otherwise it stores bytes
} CosimCommitSpan;

// Four is `boss_bg_queue_flip`, the widest commit in the registry: a BG DMA
// cursor, then a queue A slot table and its count.
#define COSIM_MAX_COMMIT 4

// The widest span, `$12A0`'s sixteen four-byte slots.
#define COSIM_COMMIT_BYTES 64

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
  // mode puts the tick count in A and stands the CPU here, and the `JSL` is
  // made as the ROM's is: the same pushes, the same parked stack. Same
  // reasoning as `ret_op`.
  uint32_t yield_op;
  int ctx_size;  // sizeof the port's context struct
  const CosimExclude* excludes;
  int exclude_count;
  // Cycles a substituted call burns in place of the ROM's instructions.
  // Measured, not guessed: `zamn_cosim verify` reports the real distribution
  // per routine and this is the mean it reported. See docs/cosim.md.
  //
  // A mean is only good enough while the errors are small and independent. When
  // they are not — a routine whose cost is the length of a list it was handed —
  // the shim reports the real cost per call with `cosim_cost` and this is the
  // fallback for the paths it declines to price.
  int cycles;
  // How many bytes of stack the ROM's version pushes and abandons. Also
  // measured — it is the `stack` column `verify` prints, which is derived from
  // how deep the stack pointer actually went. Native mode uses it to know which
  // bytes it is *expected* to leave stale, since the port pushes nothing.
  //
  // A routine with `exits` writes the stack itself and normally declares
  // none. One that does declare some is a stretch with calls inside it, made
  // as C calls: what the ROM pushed under them is waived as it is for any
  // other routine.
  int stack_bytes;
  // Checked per call by `verify`, never substituted by `run`. The report prints
  // the row with `verify only` where a verdict would go, so the exclusion is
  // visible rather than silent.
  //
  // The first routine that needed this was `$80:CCC8 apu_send`, whose body is
  // a bus handshake: it spins on `$2143` until the SPC700 echoes the last
  // command back, and substituting it used to mean not waiting at all. The
  // trace can wait now (`HW_WAIT8`), read by read on the ROM's cycles, so it
  // is substituted, and so is the uploader above it.
  //
  // **The reason a routine lands here now is call volume.** `cycles` is one constant standing
  // in for a distribution — `lzss_read_byte` really costs anywhere from 98 to
  // 298 — and that is harmless while the errors are independent and few. The two
  // LZSS leaves are neither: they are called 1,071,108 times across the corpus,
  // almost all of it inside a single multi-frame decompression, and the error
  // correlates with the data rather than cancelling against it. Substituted, a
  // level load finishes three frames off, which is enough to move every input a
  // movie applies by frame index afterwards.
  //
  // No budget fixes that, and the failed attempt is the argument: the mean is
  // the mean *by construction*, so if a mean could make the totals agree it
  // already would. What is left over is variance, and a constant has none. So
  // these are verified on every call — 107,678 of them, all 128 KB compared —
  // and never substituted, which keeps the frontend's framebuffer check honest
  // for the other fifty-six.
  bool verify_only;

  // ...and the mirror of it, which is the first piece of Phase 4 in this file.
  //
  // `verify_only` means *checked on every call and never substituted*. This
  // means *substituted and never checked per call*, and the routine it was
  // added for is `$80:C05A sprite_cache_init`: 0.97 of a frame of `memset`, so
  // an NMI lands inside virtually every call and `verify` abandons all of them.
  //
  // **The thing worth being precise about is that those are two different
  // failures.** An interrupt breaks *rewind-and-replay*: the ROM's NMI handler
  // wrote WRAM inside the call window, and the port models the routine rather
  // than the handler, so the diff would be reporting the harness's problem as
  // the port's. It breaks nothing about substitution — under `run` the ROM
  // never executes the routine, so there is no window to land inside. The core
  // waits at the instruction after the `JSL` while the budget is burned, and
  // takes any NMI that falls due exactly as it would have.
  //
  // So a `run_only` routine is not unchecked. It is checked by a different
  // claim: `run` compares all 128 KB of WRAM once per scheduler pass, so what
  // stands behind it is *the stretch containing it agreed*, not *the call
  // agreed*. That is a weaker claim per call and a broader one per movie, and
  // it is the claim Phase 4 has to be built on anyway — a port that owns its own
  // main loop has no per-call boundary left to rewind to.
  //
  // Two conditions before anything else gets this flag, and both are the point
  // rather than paperwork:
  //
  //   * **`verify` must be unable to score it, not merely unwilling.** The
  //     reason has to be structural — an interrupt, or a body that outlives a
  //     frame — and the interrupted count in a `verify` report is the evidence.
  //     A routine that could be checked per call and is not is just unchecked.
  //   * **`.cycles` must be a count, not a mean.** Every other entry's budget is
  //     an average `verify` measured; there is no measurement here, so the
  //     figure has to come from the instruction stream instead. `$80:C05A` has
  //     no data dependence and one loop with a known trip count, so
  //     `tools/cycles816.py` prices it exactly. A routine whose cost varies with
  //     its input cannot honestly be given a constant nobody watched.
  //
  // `tools/native_share.py` no longer lists such an address in `BLOCKED`: the
  // share it earns is real on the `run` side of the report and zero on the
  // `verify` side, and the report says which.
  bool run_only;

  // --- The words this routine publishes to the NMI ---------------------------
  //
  // A ported routine runs atomically and the ROM's version does not. Where that
  // shows is a routine which writes a payload and then one word that *publishes*
  // it -- a queue's slot and then its count, four parallel arrays and then their
  // cursor. The NMI handler reads that word to decide whether there is a job
  // waiting, so an interrupt landing between the payload and the publish sees no
  // job, while the same interrupt landing anywhere inside the port's budget sees
  // one already queued. `$7E:000C` came back `$03` against the ROM's `$04` for
  // exactly that reason; see "Sizing the atomicity hazard" in docs/cosim.md.
  //
  // Naming the word here is what lets the harness hold it back. The port writes
  // it as it always did, `run_native` puts the old value straight back, and
  // `burn_spend` publishes it once the budget is spent -- which is where the
  // ROM's own store falls, because in all ten routines that need this the
  // publishing write is the last one the routine makes. That makes the ordering
  // exact rather than approximate, and it costs the routines nothing: none of
  // them know it is happening.
  //
  // Spans are in the order the ROM writes them.
  const CosimCommitSpan* commit;
  int commit_count;

  // --- Routines that do not return ---------------------------------------------
  //
  // `thread_yield` is entered by one thread and leaves into another, by an
  // `RTL` through a stack that is not the one it was called on. A vblank
  // dispatcher leaves into a job by pushing an address and executing `RTL`,
  // and the job comes back to an address inside it. Neither ends where its
  // caller's return address says, so neither fits `ret_op`.
  //
  // So a routine may name its **exits** instead: the instructions of its own
  // that control leaves it by. The port stops on one and says which in
  // `CosimRegs::pc`, and hands over the whole register set -- the stack
  // pointer, the direct page, the data bank and the status byte as well as A,
  // X and Y -- because an exit like that can change all of them. The exit
  // instruction is then made as `ret_op` is: by the harness when it only
  // moves control, and by the core otherwise. See `leave` in `cosim.c`.
  //
  // The entry does not have to be a subroutine's. `$80:8401` is where a job
  // returns into the dispatcher; `$80:8372` is the instruction after the
  // scheduler's `WAI`. Any instruction the ROM reaches is an entry the harness
  // can take, and for these it is the only kind there is.
  //
  // What it means for the other two instruments:
  //
  //   * `verify` ends the call when the ROM reaches any of the exits, and
  //     compares every register as well as WRAM and the exit taken. The port
  //     writes the stack as the ROM does, so no dead stack is waived at all.
  //   * `run` publishes everything but the program counter at once, spends the
  //     budget parked on the entry, and moves the program counter to the exit
  //     when the budget is spent. An interrupt taken while parked is taken on
  //     the stack the routine left, which is how the ROM's own instructions
  //     would have met it: after the pushes.
  //
  // No `commit`: nothing that publishes to the NMI is written this way.
  const uint32_t* exits;
  int exit_count;
  // Reached by an `RTL` or by falling into it, never by a `JSR`/`JSL`, so
  // serving it serves no call. It says so to a reader and nothing reads it:
  // the call share finds out how each entry was reached, as it is reached.
  // See `call_served` in `cosim.c`.
  bool uncalled;
  // Optional, and asked before `supported`. See `CosimAccepts`.
  CosimAccepts accepts;

  // --- Routines that write the hardware ----------------------------------------
  //
  // The port records its register writes in a trace (`port/hw.h`) and the shim
  // hands it over priced, with `cosim_hw`. Under `run` each write is made on its
  // own cycle while the budget is spent, by the same bus access the ROM's store
  // makes, so a DMA it starts runs where the ROM's did and holds the CPU as long.
  // Under `verify` every register write the ROM made inside the call is compared
  // with the port's, address, value and cycle, and the cost model is held to the
  // CPU's own cycles exactly, with refresh and DMA taken out.
  //
  // The APU's ports are in the trace too, reads as well as writes, for the
  // sound routines that wait on the SPC700 (`HW_WAIT8`). For every other
  // routine their traffic has its own log, `ApuLog`.
  bool hw;
  // A call that runs for frames, so an interrupt lands inside nearly every
  // one: the sound uploads. `verify` abandons a call an interrupt lands in,
  // because the handler changes WRAM the port knows nothing about. A routine
  // with this set is not abandoned. The handler's stretch is set aside
  // instead, from the vector to the `RTI` that comes back to the routine: its
  // cycles come off the call's clock, its register accesses are not the
  // call's, and each byte of WRAM it changed is left out of the call's diff,
  // and out of the diff of any call around it that has this set too. A call
  // around it without it is counted as interrupted, as before.
  // `CosimStat::irq_waived` counts the bytes that differed at the end and were
  // let off this way, and `irq_waived_ours` those the port had also written,
  // which are the ones nothing checked.
  bool through_interrupts;
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

// ---------------------------------------------------------------------------
// Routines whose cost is a function of their input
// ---------------------------------------------------------------------------

// Report what the call this shim just served would have cost the 65816.
//
// `CosimRoutine::cycles` is one constant standing in for a distribution, and for
// most of the registry that is fine: the errors are small, independent, and they
// cancel. For a routine that walks a list they are none of those things.
// `actor_depth_sort` is the case that forced this — 92 cycles on an empty
// display list, 7,524 on a full one, declared at the mean of 1,605 — and the
// symptom was not a failed diff but a framebuffer: substituted, `level25-2p`
// parts from the stock core around frame 2,700, because a routine that runs
// every frame and is wrong by thousands of cycles moves the rest of the machine.
//
// No constant fixes that, and the shape of the failure is the argument. The mean
// is the mean *by construction*, so if some constant could make the totals agree
// the measured one already would; what is left over is variance, and a constant
// has none. The routine, on the other hand, knows exactly what it did — it just
// did it — so it can say. A shim calls this from inside `run` with the cost of
// the particular call, and native mode burns that instead of the constant.
//
// Two rules, and they are what keep this a measurement rather than a fudge:
//
//   * **The number is the ROM's own instruction cycles and excludes DRAM
//     refresh.** The core adds 40 whenever a run of cycles crosses the end of a
//     scanline, and where those land is a property of the machine's clock, not
//     of the call. So a reported cost is burned in per-access pieces and the
//     core inserts refresh wherever it falls due, exactly as it did while the
//     ROM's own instructions were executing. (A declared `cycles` constant is
//     burned in one piece, as it always was: it was *measured* refresh-inclusive,
//     so chunking it would count refresh twice.)
//
//   * **It is checked on every call.** `verify` already knows what the ROM's
//     instructions really cost, so a reported cost is diffed against that and
//     the report prints the error — see `CosimStat::model_err_min`. A model
//     nobody checks is a guess with a struct around it.
//
// Costing a routine is optional and per call: a shim that does not call this
// gets `cycles`, and one that can only price some of its paths can report on
// those and stay silent on the rest.
void cosim_cost(int cycles);

// Cycles the core inserts for a DRAM refresh, once per scanline. It is the unit
// a correct cost model's residual error comes in — see `model_refresh_exact`.
#define COSIM_REFRESH_CYCLES 40

// Master cycles in one NTSC frame: 262 scanlines of 1,364. The unit the drift
// between two lockstepped cores is worth reading in, because one whole frame of
// it is what a pass overrunning vblank costs — see `CosimStat::budget_residual`
// and the parting in `cosim_lockstep`.
#define COSIM_FRAME_CYCLES 357366

// One straight-line run of the ROM, priced. `tools/cycles816.py` prints both
// numbers for any range of the listing, and a model is a table of these.
//
// `cycles` is what the run costs with FastROM on. `bytes` is its length, and
// every one of those bytes is fetched from the program stream exactly once — so
// while `$420D` is clear each costs 2 more. Carrying the length rather than
// assuming the register never changes is the difference between a model that is
// right on 40 movies and one that is right on 43.
//
// `dp` is how many of the run's instructions use direct-page addressing. Each
// of them costs one extra internal cycle while `D`'s low byte is non-zero, and
// unlike the other two columns that is a property of the *caller*, not of the
// run. Every routine priced before the collision handlers ran on page zero, so
// the column was `0` everywhere and did not need to exist; `$80:84A2  TCD`
// installs the target thread's own page, and the table at `$80:82DE` tiles
// `$7E:0100-$7E:0CFF` at stride `$80` — so twelve of the twenty-four threads
// run unaligned and twelve do not. A handler model that ignored this would be
// right on half the actors in the game.
//
// `ins`, when a run has it, is the same run an instruction at a time, for a
// burn that has to take an interrupt where the ROM would. See `CosimInsn`.
typedef struct CosimInsn CosimInsn;
typedef struct {
  int cycles;
  int bytes;
  int dp;
  const CosimInsn* ins;
  int ins_count;
} CosimRun;

// One instruction of a run: its cycles and FastROM bytes, counted as
// `CosimRun` counts them, and the length of its last bus cycle. The core polls
// for an interrupt just before that cycle, on every instruction these runs are
// made of, and takes it after the instruction if the poll saw it. `last` is 0
// for a store cut off before its write, because the write is an event of its
// own and the poll comes before it.
struct CosimInsn {
  uint8_t cycles, bytes, last;
  bool last_fast;  // ...and that last cycle reads a FastROM byte
};

static inline int cosim_run_cycles_dp(const CosimRun* r, bool fastrom,
                                      bool dp_unaligned) {
  return r->cycles + (fastrom ? 0 : 2 * r->bytes) +
         (dp_unaligned ? 6 * r->dp : 0);
}

static inline int cosim_run_cycles(const CosimRun* r, bool fastrom) {
  return cosim_run_cycles_dp(r, fastrom, false);
}

// One register write, one bare access or one wait, at a cycle of a routine's
// budget. `at` is counted from the routine's entry in the CPU's own cycles,
// with no refresh or DMA in it, which is how a cost model counts too, and as
// if every wait before it ended on its first read.
//
// A bare access is what follows a write to `$420B`. The core starts a DMA two
// accesses after the write that asks for it, and lines the CPU up again on the
// length of the second, so those two have to be accesses of the lengths the
// ROM's next instruction makes, not a slice of the budget. Every instruction
// opens with two program fetches or with a fetch and an idle, and the jobs'
// instruction after each `STA $420B` is `REP #$20`, two fetches.
//
// A wait is `port/hw.h`'s: the compare's `pre` cycles of fetches, its reads,
// `len` cycles of them, and `post` for the branch falling through. A read that
// does not match takes the branch instead, one idle longer, and goes round
// again, so each costs `pre + len + post + 6` more than the event's price.
//
// A read is `HW_READ`: the load's three fetches are the run's in front of it,
// and the read is `len`. A low wait is `HW_WAIT_LOW`, priced as the other
// waits are, with the `LSR` in `post` beside the branch.
//
// A stack event takes no time. It says where the ROM's stack pointer is from
// `at` on, `val`, for an interrupt that lands in the burn: see `HW_STACK`.
//
// A run event is a run with `CosimRun::ins`: block `addr`, `val` times over.
// Its cycles are the ones between it and the next event.
typedef enum {
  COSIM_HW_WRITE,
  COSIM_HW_ACCESS,
  COSIM_HW_WAIT8,
  COSIM_HW_WAIT16,
  COSIM_HW_STACK,
  COSIM_HW_RUN,
  COSIM_HW_READ,
  COSIM_HW_WAIT_LOW,
} CosimHwKind;

typedef struct {
  int at;
  uint16_t addr;
  uint16_t val;
  uint8_t kind;
  uint8_t len;
  uint8_t pre, post;
} CosimHwEvent;

// What a wait costs each time round that does not end it.
static inline int cosim_hw_spin(const CosimHwEvent* e) {
  return e->pre + e->len + e->post + 6;
}

static inline bool cosim_hw_is_wait(const CosimHwEvent* e) {
  return e->kind == COSIM_HW_WAIT8 || e->kind == COSIM_HW_WAIT16 ||
         e->kind == COSIM_HW_WAIT_LOW;
}

// Does this read end the wait?
static inline bool cosim_hw_wait_met(const CosimHwEvent* e, uint16_t got) {
  return e->kind == COSIM_HW_WAIT_LOW ? (got & 1) == 0 : got == e->val;
}

// Price a trace and hand it over: each `HW_RUN` costs `runs[block]`, each
// write the access its register takes, and the total is reported as
// `cosim_cost` would report it. A trace that overflowed is refused: the shim
// gets `false`, nothing is handed over, and the harness keeps the routine's
// declared `cycles` and makes no writes -- so a guard has to keep that from
// happening, and the ones here bound the lists they walk.
bool cosim_hw(const HwTrace* t, const CosimRun* runs, bool fastrom);

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
  // ...and how well the routine predicted its own, for the ones that priced
  // themselves with `cosim_cost`. `modelled` is how many calls reported at all,
  // so a routine that can only price some of its paths is visible as a
  // `modelled` below `checked` rather than as a silently weaker claim.
  //
  // The error is `actual - model`, and a *correct* model does not have an error
  // of zero: it excludes DRAM refresh, so what is left over is 40 cycles for
  // every scanline the call happened to cross. `model_refresh_exact` counts the
  // calls whose error was exactly that — a non-negative multiple of
  // `COSIM_REFRESH_CYCLES` — and it is the number that says whether the model is
  // right. Anything else is the model being wrong about an instruction.
  //
  // ...on a call where refresh is the only thing the machine adds, which is not
  // every call. **HDMA steals cycles from the CPU too**, and how many depends on
  // how many channels the PPU has armed and what they are transferring — a
  // property of the frame being drawn, not of the routine's arguments, and so
  // exactly as unpredictable from the input as refresh is and not as tidy. Three
  // movies in the corpus run it: level 1's map screen and both level 49 probes.
  // Calls made while any channel was armed are counted in `model_hdma` and held
  // only to `error >= 0`; the sharp test is the other 40 movies'.
  long modelled, model_refresh_exact, model_hdma;
  long model_err_min, model_err_max;
  double model_err_mean;
  // Register writes compared, for a routine that makes them (`hw`). For one
  // that does not, the writes the ROM made inside its calls anyway: a port
  // that computes a product itself, say, where the ROM used the multiplier.
  // Nothing checks those, which is why they are counted where they can be seen.
  long hw_writes;
  long hw_unmodelled;
  // ...and for the sound routines, the waits compared, and how many times
  // round their loops the ROM went beyond the first read of each.
  long hw_waits, hw_spins;
  // `CosimRoutine::through_interrupts`: the interrupts set aside, counted
  // against the innermost call they landed in; the bytes that differed at the
  // end of a call and were let off because one had changed them; and of those,
  // the ones the port wrote as well.
  long irq_windows, irq_waived, irq_waived_ours;
  // What a substituted run would have *paid* for these calls, less what the ROM
  // actually spent on them. Positive is over-payment: a native core reaching the
  // same point in the game later than a stock one.
  //
  // This is the quantity behind the parting in `cosim_lockstep` — two timelines
  // separate when one of them has accumulated enough of it to overrun a vblank
  // the other did not — and it is not the same thing as `model_err_*` above.
  // That says whether a model is *right*; this says what being slightly wrong
  // is worth, summed over a run and signed, so a routine 6 cycles light on a
  // million calls outranks one 6,000 cycles light on ten. A mean is scored the
  // same way, which is the only place the cost of using one shows up as a
  // number rather than as a caveat.
  //
  // Refresh is taken off first for a reported cost, because the burn spends it
  // in pieces and the core puts the refreshes back — so the part of the error
  // that is a whole number of refreshes is not a debt, and an exact model scores
  // exactly zero here rather than 40 per scanline it crossed.
  long long budget_residual;
  long budget_calls;
  // The first call whose error was not a whole number of refreshes, with the
  // two pieces of the calling convention that most often explain one: an
  // unaligned direct page costs an extra internal cycle per direct-page
  // instruction, and a data bank of $80 or above reaches a ROM table two cycles
  // a byte faster than a low one does. Printed under the row, because "the
  // model is wrong somewhere in 43 movies" is not a lead and this is.
  bool model_bad;
  long model_bad_model, model_bad_actual;
  uint16_t model_bad_d;
  uint8_t model_bad_db;
  // First divergence, if any.
  bool failed;
  char detail[256];
} CosimStat;

// In-flight calls and the scratch WRAM the port runs against. Private to the
// engine, but held per-harness so two of them can run side by side — which is
// exactly what lockstep does.
typedef struct CosimPriv CosimPriv;

// ---------------------------------------------------------------------------
// How much of the run was native
// ---------------------------------------------------------------------------
//
// The per-routine table above says what each ported routine did. What it cannot
// say is what share of the whole that *is* — 82 routines is a number with no
// denominator attached, and it cannot tell a 17-byte leaf from a 2 KB state
// machine. `tools/native_share.py` answers that question offline, from a traced
// profile plus a call-graph closure. This answers it live, from the seam
// itself, so that a session anybody plays reports its own number.
//
// Two denominators, because there are two honest questions:
//
//   * **work** — of the cycles the CPU spent working, how many belonged to code
//     the port executed instead. This is the one that matters: it weights a
//     routine by how much of the machine's time it actually costs. Native
//     mode already burns a measured cycle budget in place of every substituted
//     routine (`CosimRoutine::cycles`), so the numerator is not an estimate —
//     it is the same number the rest of the machine was advanced by.
//
//   * **calls** — of the subroutine calls the game made, how many the port
//     served. Cruder, since every call counts the same, but it is the thing
//     people mean when they ask how much of the game is ported, and it is
//     exact: the engine counts a `JSR`/`JSL` when it executes, and a serve
//     when a port takes the entry that call went to. A port taking an entry
//     reached any other way, by an `RTL` or by falling into it, serves no
//     call, and an entry that begins with a `JSR` makes none by being served.
//
// Both denominators shrink correctly as the port grows: a call made *inside* a
// substituted routine never executes, so neither its cycles nor its `JSR` are
// ever counted. Two things are outside both, and it is worth knowing which way
// they bias: a thread body and a vblank job are entered by `RTL` rather than by
// a call, so their cycles are in the work denominator and unreachable by the
// call one — see `tools/native_share.py` for the full account of that family.
typedef struct {
  // Every cycle the core advanced while the harness was stepping it.
  uint64_t cycles_total;
  // ...of which, burned standing in for a substituted routine's instructions.
  uint64_t cycles_native;
  // ...of which, spent halted on a `WAI` — the scheduler idling until NMI. The
  // CPU is not executing anything at all here.
  uint64_t cycles_idle;
  // ...of which, spent going round one of the ROM's declared busy-wait loops.
  // See `src/cosim/waits.h` for why these come out of the denominator.
  uint64_t cycles_wait;
  // `JSR`/`JSL`/`JSR (abs,X)` instructions made, by the 65816 or by `leave`...
  uint64_t calls_total;
  // ...of which, the ones a port served at the entry they went to.
  uint64_t calls_native;
  // How many times a substituted call's budget was stopped part-way because an
  // interrupt had fallen due, and resumed after the core had taken it.
  //
  // Not a diagnostic: it is the evidence for the one thing about a long
  // substitution that cannot be checked by comparing memory. A routine that
  // spans two vblank boundaries owes the game two NMIs, and the core's
  // `nmiWanted` is a single bool — so this number is how many NMIs would
  // otherwise have been dropped or handed over late. Zero on a session made
  // only of short calls, and that is the correct answer there.
  uint64_t burns_parked;
  // How many times the harness made the instruction a port left by: its
  // `RTS` or `RTL`, or the call or jump at an exit. See `leave` in `cosim.c`.
  uint64_t leaves;
  // ...of `cycles_native`, the DMA a substituted routine started, which the
  // core runs inside the burn (`CosimRoutine::hw`). Under the ROM the same
  // transfers are in the work denominator and not in the numerator, so the
  // report says what the share is without them.
  uint64_t cycles_native_dma;
  // Instructions the 65816 executed. An interrupt taken and a `WAI` waited
  // out are not instructions, and under `run` the harness makes both.
  uint64_t core_instructions;
} CosimWork;

// The same, reduced to the two percentages and their denominators.
typedef struct {
  uint64_t cycles_work;    // total - idle - wait: what a CPU was actually doing
  uint64_t cycles_native;
  uint64_t calls_total;    // executed + served
  uint64_t calls_native;
  uint64_t calls_declined; // handed back by a guard; part of `calls_total`
  double work_share;       // 0..1
  double call_share;       // 0..1
} CosimShare;

// (`cosim_share` and `cosim_share_report` are declared below `Cosim`.)

// ---------------------------------------------------------------------------
// Where the 65816 took over
// ---------------------------------------------------------------------------
//
// Under `run` the core is asked for one thing: an instruction of the ROM's
// that no port has. The harness takes an interrupt itself and waits out a
// `WAI` itself, through the console's bus as the core does, so every other
// step of the machine is a port's or the harness's. With nothing switched on
// the machine is the core's alone, as it is under `verify`.
//
// A takeover is a place the core was handed the program counter: the address,
// and how it came to be there. A game with no CPU would stop at each one, so
// the list is what is left to port, by address, and it is empty when nothing
// is.
//
// One is counted when the core executes an instruction and the step before
// was not its own. An interrupt that lands in the core's code and comes back
// to it is not a second one. The core's instructions are counted against the
// takeover they followed, until a port next runs.
typedef enum {
  COSIM_TOOK_RESET,     // the reset vector's target: the boot code
  COSIM_TOOK_DECLINED,  // a registered entry whose port turned the call down
  COSIM_TOOK_UNSERVED,  // a registered entry `run` never substitutes
  COSIM_TOOK_RETURN,    // a port returned to it: its caller is not ported
  COSIM_TOOK_CALL,      // a port called it or jumped to it
  COSIM_TOOK_EXIT,      // the instruction a port left by, which the harness
                        // does not make
} CosimTookHow;

typedef struct {
  uint32_t pc;
  uint8_t how;
  // The routine that turned the call down or is never served, or the port
  // that handed over. NULL for the boot code.
  const CosimRoutine* routine;
  uint64_t times;
  uint64_t instructions;
  long first_frame;  // the console's count of frames, the first time
} CosimTakeover;

// An instruction of the ROM's that something outside the harness wants to know
// the machine has reached -- see `cosim_watch`.
typedef void (*CosimWatchFn)(Snes* snes, void* ctx);
typedef struct {
  uint32_t pc;
  CosimWatchFn fn;
  void* ctx;
} CosimWatch;
#define COSIM_MAX_WATCHES 16

typedef struct {
  Snes* snes;
  Rom rom;
  CosimMode mode;
  CosimPriv* priv;

  // Which routines are live, as a bitmask over the registry.
  //
  // **This was `uint32_t` and the registry outgrew it at the 33rd routine**, in
  // the round that ported level 21's three handlers. The failure was not a
  // divergence: `1u << 32` is undefined, `stats[32]` was one past the end of a
  // fixed array, and what came out was `verify` reporting *zero* calls checked
  // on every movie with an empty routine table — a harness that had stopped
  // measuring rather than a port that had stopped working. Widened, and
  // `COSIM_MAX_ROUTINES` is asserted at init so the next one says so.
  CosimMask enabled;

  CosimStat stats[COSIM_MAX_ROUTINES];
  int stat_count;

  // Cycles and calls, native and otherwise — see `CosimWork`.
  CosimWork work;

  long frames;
  bool stop_on_fail;
  bool verbose;

  // Counters for what the core itself executed, when someone asked for them:
  // see `src/cosim/profile.h`. NULL, and then free, unless set; the caller owns
  // it, allocates it, saves it and frees it.
  struct CosimProfile* profile;

  // See `cosim_watch`.
  CosimWatch watches[COSIM_MAX_WATCHES];
  int watch_count;
  // One bit per low byte of a watched `pc`, so that an instruction nobody
  // watches costs one test however many watches there are.
  uint32_t watch_filter[8];
} Cosim;

// Reduce `c->work` to shares. Safe with an empty run: everything reads 0.
void cosim_share(const Cosim* c, CosimShare* out);

// Print it. Two lines and the caveats under native mode; under verify it says
// why the question does not apply, because there the ROM ran everything.
void cosim_share_report(const Cosim* c);

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

// Call `fn` whenever the CPU is about to execute the instruction at `pc`. False
// if there is no room for another.
//
// It fires the same in every mode, and for a substituted routine's `ret_op`
// that is the point: the core still executes that instruction, once the budget
// is spent, so a watch on it hears the routine end at the moment the ROM's
// version would have, with its results in memory. It is how the widescreen
// learns when the sprite pass has finished and when the NMI sends its OAM
// (`widescreen_pass_done`), which are the game's own events and not the
// frame's, so what it copies does not depend on how long anything took. The
// monster sounds of other sample sets (`src/bank_sfx.h`) watch eleven checks
// and `apu_send`'s store the same way.
bool cosim_watch(Cosim* c, uint32_t pc, CosimWatchFn fn, void* ctx);

// True between frames when the harness holds nothing of its own -- no call on
// its stack, no budget part burned -- so that `snes_saveState` has the lot.
// And, for the other direction, forgetting whatever it holds: after
// `snes_loadState` that belonged to a machine that is gone.
bool cosim_idle(const Cosim* c);
void cosim_forget_calls(Cosim* c);

// True if any enabled routine has diverged.
bool cosim_failed(const Cosim* c);

// The takeovers so far, in the order they were first met.
const CosimTakeover* cosim_takeovers(const Cosim* c, int* count);

// Print the `most` commonest, and how many there were. Returns the number of
// distinct ones. Says nothing under `verify`, where the ROM runs everything.
int cosim_takeover_report(const Cosim* c, int most);

// Write all of them to `path` as CSV, replacing it. False if it cannot be
// written.
bool cosim_takeover_save(const Cosim* c, const char* path);

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
// `stock` and `native` are two consoles as `snes_init` makes them, which this
// takes and frees. They are the caller's to make because each must have a
// PPU, whose memories are compared, and this file is also linked by a game
// that has none.
//
// `movie_path` may be NULL for no input. Returns 0 if the two agreed for the
// whole run, and prints the first frame and address at which they did not.
int cosim_lockstep(Snes* stock, Snes* native, const uint8_t* rom_data, int rom_len,
                   const char* movie_path, int frames, const char* const* names,
                   int name_count, bool verbose);

#endif
