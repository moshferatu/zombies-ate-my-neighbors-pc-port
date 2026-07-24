// How a ported routine suspends.
//
// This is the decision `docs/frame-skeleton.md` flagged in Phase 1 and deferred
// through Phase 2: ZAMN's game logic is written as coroutines. A routine calls
// `thread_yield` half way down its body, the scheduler parks its stack pointer
// and runs somebody else, and some frames later it resumes on the next
// instruction with its stack frame intact. A plain C function cannot stand in
// for that, so every yielding routine ported from here on needs a way to stop
// in the middle and be re-entered.
//
// **The answer is an explicit resume point, and the suspended state is plain
// data.** No fibers, no saved machine stacks, no `ucontext`. The argument is in
// `docs/threads.md`; the short version is that `zamn_cosim verify` works by
// rewinding WRAM to the state a routine was entered with and running the port
// over it again, and you cannot rewind a native call stack. A suspended routine
// whose entire state is a `resume` index plus some locals in a struct can be
// snapshotted, copied and replayed as many times as the harness likes. One
// whose state is a parked machine stack cannot be checked at all.
//
// The same property is what makes PLAN.md's Phase 5 save states a `fwrite`:
// "where each thread is" stays a number in a struct rather than a stack layout
// that changes with the compiler.
//
// ## Writing one
//
// A resumable routine is a function over its own context struct, whose first
// member is a `PortCoro`. It returns what it did — yielded for N ticks, or
// returned — and the caller (in Phase 3, the harness; in Phase 4, the port's own
// scheduler) calls it again to resume:
//
//     typedef struct {
//       PortCoro co;
//       uint16_t some_local;   // anything that has to survive a yield
//     } FadeCtx;
//
//     PortStep fade_in(Wram* w, FadeCtx* c, uint16_t* ticks) {
//       switch (c->co.resume) {
//         case PORT_CORO_ENTRY:
//           ... straight-line code ...
//           return port_yield(&c->co, 1, ticks, 4);   // sleep 1 tick, resume at 4
//         case 4:
//           ... more straight-line code ...
//       }
//       return PORT_RETURNED;
//     }
//
// Two rules make this safe, and they are the whole cost of the approach:
//
//   1. **Nothing that has to survive a yield may be a C local.** It goes in the
//      context struct, which is exactly the promotion the 65816 version gets for
//      free by leaving it on its parked stack.
//   2. **Resume labels are the ROM address they correspond to**, low 16 bits —
//      `case 0x8927:` is `$80:8927`. They are arbitrary as far as C is
//      concerned, so they may as well say where in the listing the routine picks
//      up. `PORT_CORO_ENTRY` is 0 because a zeroed context means "not started",
//      which is what makes `memset` a valid way to begin a call.
//
// A routine that yields inside a routine it calls needs the callee to be
// resumable too, with its own `PortCoro` in the same context struct, and the
// caller's resume point re-enters it. That nesting is manual and it is the real
// price of not having fibers; `docs/threads.md` says why it is worth paying.
//
// Port code: libc only.

#ifndef PORT_COROUTINE_H
#define PORT_COROUTINE_H

#include <stdint.h>

// What a resumable routine did when it stopped.
typedef enum {
  PORT_RETURNED,  // it finished; the caller's `JSL` returns
  PORT_YIELDED,   // it called `thread_yield` and expects to be re-entered
} PortStep;

// A routine's parked position. Deliberately a struct rather than a bare
// `uint16_t`: it is the type that says "this routine can suspend", and putting
// it first in a context struct is what lets the harness zero a context without
// knowing anything else about it.
typedef struct {
  uint16_t resume;  // PORT_CORO_ENTRY, or the label to jump to
} PortCoro;

// A context that has been zeroed is a call that has not started.
#define PORT_CORO_ENTRY 0

// Suspend: remember where to come back to, and report the tick count the ROM's
// version would have had in A when it did `JSL thread_yield`.
//
// `ticks` is an out-parameter rather than the return value because the harness
// diffs it — a yield for the wrong number of ticks is a real divergence, and
// making it part of the routine's answer is what lets `verify` catch it.
static inline PortStep port_yield(PortCoro* co, uint16_t ticks,
                                  uint16_t* out_ticks, uint16_t resume) {
  co->resume = resume;
  *out_ticks = ticks;
  return PORT_YIELDED;
}

#endif
