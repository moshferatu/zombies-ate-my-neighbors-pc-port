// The cooperative scheduler's bookkeeping, and the two vblank job queues.
//
// ZAMN's frame is driven by a 24-slot cooperative thread scheduler and two
// queues of jobs that NMI runs — see `docs/frame-skeleton.md`. The scheduler
// itself cannot be ported as a C function, because `thread_yield` suspends its
// caller mid-routine and resumes it later on a parked stack; that is the whole
// coroutine problem Phase 3 has to solve, and it is not solved here.
//
// What *can* be ported now is the bookkeeping around it: the routines that walk
// these tables without ever yielding. They are leaves, they are pure WRAM, and
// porting them is how the harness gets exercised against both return kinds —
// `thread_tick_waits` returns with `RTS`, the two queue adders with `RTL`.
//
// Port code: libc only.

#ifndef PORT_THREAD_H
#define PORT_THREAD_H

#include <stdbool.h>
#include <stdint.h>

#include "port/wram.h"

// `$80:8398` — age every live thread's wait counter by one tick.
//
// Run once per scheduler pass, from `scheduler_idle` just after the `WAI` that
// is the frame boundary. A slot is live when bit 15 of its wait word is set;
// the counter stops at `$8000` rather than wrapping, so a thread whose wait has
// expired stays runnable until the scheduler picks it up.
void thread_tick_waits(Wram* w);

// `$80:83AE` / `$80:8418` — register a job with the forced-blank (A) or
// post-blank (B) vblank queue.
//
// `addr`/`bank` are the far address of the job; the queue stores `addr - 1`
// because the dispatcher reaches it by pushing and executing `RTL`. A stored
// address of 0 is what marks a slot free, which is why the search below looks
// for a zero word.
//
// Returns the byte offset of the slot taken, or -1 if the queue was already
// full. Two faithfully reproduced quirks:
//
//   * The search runs *downwards* and stops at slot 0 without testing it, so
//     slot 0 is the fallback that gets taken when every other slot is busy —
//     overwriting whatever was there if the count ever disagrees with what is
//     actually occupied.
//   * Queue A is 16 slots and the count is capped at 16, but the search starts
//     at slot 14, so slot 15 is never allocated. Queue B has no such gap.
int vbl_queue_a_add(Wram* w, uint16_t addr, uint16_t bank);
int vbl_queue_b_add(Wram* w, uint16_t addr, uint16_t bank);

// `$80:8475` is how a thread says "call me back": it stores a far address into
// `thread_handler`/`thread_handler_bank` at its own slot. The other end,
// `$80:8480`, is `thread_call_handler` in **`port/collide.h`** — it lives there
// rather than here because what it does is enter actor behaviour, and the two
// handlers a collision reaches are its neighbours in that file.

#endif
