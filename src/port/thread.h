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

#include "assets/rom.h"
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

// The three flags either adder leaves, given the queue it worked on, the Y it
// was called with, and whether the job went in. Carry is the return value — set
// means refused — and the comment in `src/cosim/routines.c` says what leaving it
// unclaimed cost.
//
// `y_in` is only read on the refused path, where the exit is `PLY : RTL` and so
// N and Z belong to the caller's own Y rather than to the compare that refused.
//
// This is here rather than in the shim that used to own it because `$80:C07F`
// ends `JML $8083AE` and so returns these same three flags as its own. Two
// copies of one fact is one copy too many.
typedef struct {
  bool n, z, c;
} VblQueueFlags;

void vbl_queue_flags(const Wram* w, uint32_t count_at, uint16_t y_in, bool added,
                     VblQueueFlags* out);

// --- $80:825E ---------------------------------------------------------------
//
// **How a thread gets its first stack frame**, and the exact counterpart of
// `enemy_survived_react` in `port/collide.h`: that one splices a call into a
// thread that already exists, this one manufactures the whole parked state of a
// thread that does not.
//
// Everything the scheduler needs to resume a thread is nine bytes on that
// thread's own stack, because `$80:8390` resumes one with
//
//     LDA $11B0,X : TCS : PLD : PLP : PLB : RTL
//
// — so `thread_spawn` writes a D, a P, a DB and a far return address that lands
// on the entry point, and under that a second far return address to
// `$80:833E`, which is where a thread body's own `RTL` goes to free the slot.
// A brand-new thread and a thread parked in the middle of `thread_yield` are
// the same nine bytes; the only difference is what is in them.
//
// The routine is also how a spawner passes arguments: its last act is to copy
// the **caller's first five direct-page words** into the new thread's page
// (`$80:82B5  LDA ($01,S),Y`, reading through the `D` the opening `PHD` saved).
// `$80:FA26` is the example — it fills `$00`, `$02` and `$04` with a position
// and then spawns.
//
// `entry` and `bank` are the far address to start at, and `caller_dp` is the
// page those five words come from. Returns the slot index already doubled, or
// -1 when all 24 slots are live — which the ROM reports as `A = 0`, and so
// cannot be told from slot 0. The search runs downwards from slot 23, so slot 0
// is the last one taken and that collision is very nearly unreachable.
#define THREAD_SPAWN_ENTRY 0x80825eu
// 24 words in ROM: the stack pointer each slot starts with.
#define THREAD_SP_TABLE 0x80830eu
// 24 more: the direct page each slot runs on, `$7E:0100` at stride `$80`.
#define THREAD_DP_TABLE 0x8082deu
// `$80:82AB  LDA #$8001` — live, and runnable on the next tick.
#define THREAD_WAIT_NEW 0x8001
// `$80:829E  LDA #$833E : DEC A` — where a thread body returns to when it ends.
#define THREAD_EXIT_RETURN 0x833d
// How many of the caller's direct-page words the new thread inherits.
#define THREAD_SPAWN_ARGS 5

int thread_spawn(Wram* w, const Rom* rom, uint16_t entry, uint16_t bank,
                 uint16_t caller_dp);

// --- $80:9D5B  spawn_has_room — carry set means no ---------------------------
//
// Six instructions, fifteen bytes, and **every spawn in the game goes through
// it**: 37,151 calls over the eleven profiled movies, from fourteen `JSL` sites
// spread across banks `$80`, `$81`, `$82` and `$83`.
//
//     LDA $00DE : CMP #$008A : BCS out : LDA $0006 : CMP #$0012
//     out: RTL
//
// Two ceilings, and they are different kinds of thing. The second is the
// scheduler's: eighteen live threads out of the twenty-four slots
// `thread_spawn` hands out, so six are held back for whatever is not an actor.
// The first is `W_SPAWN_LOAD` against 138, and that one is a **weighted**
// census — see `port/wram.h`. An actor charges its own weight when it spawns
// and refunds it when it dies, over 137 sites and 21 distinct weights running
// from 1 to 40, so 138 is not a population but a load: what it limits is how
// much the board is *worth* rather than how much of it there is.
//
// ## The wait loop this is the condition of
//
// `$81:80EC actor_list_spawn` is where it shows: it walks a level's spawn list
// and, before each entry,
//
//     LDA #$0001 : JSL thread_yield : JSL $809D5B : BCS back
//
// — a frame at a time, forever, until the board has room. That is why the
// routine is called 37,151 times for far fewer actors than that: most calls are
// a *refusal*, and the spawner's answer to a refusal is to sleep a frame and
// ask again. A spawn list is therefore not a schedule; it is a queue that
// drains at whatever rate the players clear the board.
//
// ## Both compares are `CMP`, so both are unsigned and both can be equal
//
// `BCS` is taken on equal, so 138 and 18 are the first *refused* values, not the
// last accepted ones. The carry a caller reads back is whichever compare ran
// last, which is the first one only when it refused — and that is the whole
// return value. A and the flags are the same word looked at twice.
#define SPAWN_HAS_ROOM_ENTRY 0x809d5bu

// `CMP #$008A` — the weighted census's ceiling.
#define SPAWN_LOAD_MAX 0x008a
// `CMP #$0012` — eighteen of the twenty-four scheduler slots.
#define SPAWN_THREAD_MAX 0x0012

// A is whichever of the two counters was last loaded, and N/Z belong to the
// compare that went with it. X and Y are never touched.
typedef struct {
  uint16_t a;
  bool n, z;
  bool c;  // set means **no room**, and it is the only thing any caller reads
} SpawnRoomRegs;

void spawn_has_room(const Wram* w, SpawnRoomRegs* out);

// `$80:8475` is how a thread says "call me back": it stores a far address into
// `thread_handler`/`thread_handler_bank` at its own slot. The other end,
// `$80:8480`, is `thread_call_handler` in **`port/collide.h`** — it lives there
// rather than here because what it does is enter actor behaviour, and the two
// handlers a collision reaches are its neighbours in that file.

#endif
