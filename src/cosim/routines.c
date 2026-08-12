// The registry: every routine `src/port/` has replaced, plus the shim that
// adapts it to the 65816's calling convention.
//
// The split matters. `src/port/` holds ordinary C with ordinary signatures —
// `sprite_frame_tile(Wram*, uint16_t frame)` — and knows nothing about
// registers, stacks or emulators. Everything about *how the ROM called it*
// lives here, in the harness, and dies with the harness in Phase 4. Without
// that line, "port code" would slowly turn into 65816 written in C.
//
// A shim's whole job is to say what the registers meant on the way in and what
// the ROM leaves in them on the way out. The second half is the fiddly one, so
// each shim below cites the instruction that decides it.

#include <string.h>

#include "cosim/cosim.h"

#include "port/apu.h"
#include "port/boss.h"
#include "port/bossbg.h"
#include "port/camera.h"
#include "port/collide.h"
#include "port/fade.h"
#include "port/floor.h"
#include "port/hud.h"
#include "port/levelmap.h"
#include "port/lzss.h"
#include "port/monster.h"
#include "port/oam.h"
#include "port/player.h"
#include "port/rng.h"
#include "port/score.h"
#include "port/sprite_cache.h"
#include "port/step.h"
#include "port/terrain.h"
#include "port/thread.h"
#include "port/trig.h"

// ---------------------------------------------------------------------------
// $80:B9D6  sprite_frame_tile — A = frame number, A = OAM tile word
// ---------------------------------------------------------------------------

// The routine opens with `STX $38` and closes with `LDX $38`: with one index
// register, spilling the caller's X to scratch is the only way to use X for the
// lookup. `sprite_frame_tile()` keeps it in a C local, so the spill slot is the
// one place its WRAM legitimately differs from the ROM's. Nothing reads $38
// across the call — `$80:CD20` uses the same two bytes as decompressor state,
// which is only safe because both treat it as scratch.
static const CosimExclude SPRITE_TILE_EXCLUDES[] = {
    {0x0038, 2, "the ROM spills the caller's X here; the port keeps it in a local"},
};

// What one lookup cost the 65816, from which of its runs the lookup took. Same
// shape as the display list's three walks, with one difference worth naming:
// **this is the first model whose bytes are not all program bytes.**
//
// `LDA $B447,X`, `LDA $B547,X` and `LDA $B647,X` read the slot geometry through
// the data bank, and with the data bank at `$80` that is a fast ROM read — 6
// master cycles a byte while `$420D` is set and 8 while it is clear, exactly
// like an opcode fetch. `CosimRun::bytes` is "bytes that cost 2 more with
// FastROM off", so those six data bytes belong in it: the hit path is 12
// program bytes and 14 counted ones.
//
// This is what made `tools/cycles816.py` grow a `fast_rom()` and start calling
// the column FastROM bytes rather than program bytes. Every model before this
// one either touched WRAM only or reached its table through a low bank, where
// the two counts are the same number, so the distinction had never come up.
//
// The whole thing turns on the data bank being `$80`, so the shim checks.
static const CosimRun TILE_COST[TILE_BLOCK_COUNT] = {
    // $80:B9D6..$80:B9EB, `BMI` not taken. 288, and 288 is exactly the minimum
    // `verify` measures over the corpus — the model's first witness.
    [TILE_BLK_HIT]        = {104 + 184, 10 + 14},
    // The same prologue with the branch taken, then $80:B9EC..$80:B9F5.
    [TILE_BLK_MISS]       = {104 + 6 + 132, 10 + 12},
    // $80:B9F6 `CMP : BNE` not taken, then `INX : INX : CPX : BNE` taken.
    [TILE_BLK_SCAN_NEXT]  = {52 + 54 + 6, 5 + 7},
    // ...and the same with that `BNE` falling through to `LDX #$0000 : BRA`.
    [TILE_BLK_SCAN_WRAP]  = {52 + 54 + 30 + 6, 5 + 7 + 5},
    [TILE_BLK_SCAN_FOUND] = {52 + 6, 5},
    // $80:BA07..$80:BA11, `BMI` taken.
    [TILE_BLK_SLOT_EMPTY] = {120 + 6, 11},
    // ...not taken, so $80:BA12..$80:BA19 unmaps the frame that was there.
    [TILE_BLK_SLOT_EVICT] = {120 + 70, 11 + 8},
    // $80:BA1A..$80:BA50. Two ROM table reads, hence 55 program bytes and 59.
    [TILE_BLK_TAIL]       = {724, 55 + 4},
};

static int tile_cycles(const SpriteTileWork* k, bool fast) {
  int cycles = 0;
  for (int i = 0; i < TILE_BLOCK_COUNT; i++)
    cycles += k->blocks[i] * cosim_run_cycles(&TILE_COST[i], fast);
  return cycles;
}

static void shim_sprite_frame_tile(Wram* w, const Rom* rom, const CosimRegs* in,
                                   CosimRegs* out) {
  (void)rom;
  uint16_t queued_before = wram_r16(w, W_SPRITE_UPLOAD_COUNT);

  SpriteTileWork work;
  out->a = sprite_frame_tile_counted(w, in->a, &work);

  // The direct page has to be zero rather than merely page-aligned: the port
  // reads `W_SPRITE_TICK` and friends at their absolute addresses, so a call on
  // any other page would be a porting bug before it was a pricing one. The data
  // bank has to be `$80` for the three table reads above to be fast ROM.
  if (in->d == 0 && in->db == 0x80) cosim_cost(tile_cycles(&work, in->fastrom));

  // `LDX $38` puts the caller's X back, unchanged.
  out->x = in->x;

  // Y is only touched on the miss path, where `LDY $7C ... INY INY STY $7C`
  // leaves it holding the new upload count. On a hit the routine never mentions
  // Y at all.
  uint16_t queued_after = wram_r16(w, W_SPRITE_UPLOAD_COUNT);
  out->y = queued_after != queued_before ? queued_after : in->y;

  // That same `LDX $38` is the last flag-setting instruction before the `RTS`,
  // so N and Z describe the restored X.
  out->n = (out->x & 0x8000) != 0;
  out->z = out->x == 0;

  // Carry is claimed rather than derived. The last instruction to touch it is
  // `ASL A` on the hit path — carry is bit 15 of the frame number — and the
  // `ADC` that adds the frame array's bank on the miss path. Frame numbers are
  // 12 bits and the bank add cannot overflow, so both are 0 for every frame the
  // game can actually draw. Asserting the simple answer and letting `verify`
  // check it against all 10,354 real calls is a better trade than duplicating
  // the address arithmetic here to predict a bit that never varies.
  out->c = false;
  out->flags = COSIM_FLAG_N | COSIM_FLAG_Z | COSIM_FLAG_C;
}

// ---------------------------------------------------------------------------
// $80:B9C7  sprite_cache_age — no arguments
// ---------------------------------------------------------------------------

static void shim_sprite_cache_age(Wram* w, const Rom* rom, const CosimRegs* in,
                                  CosimRegs* out) {
  (void)rom;
  sprite_cache_age(w);

  // `LDA $0020 : DEC A` before the loop, and the loop never reloads A.
  out->a = (uint16_t)(wram_r16(w, W_SCHED_TICK) - 1);
  // The loop is `LDX #$00FE ... DEX DEX BPL`, so it falls out at $FFFE with the
  // branch's N and Z describing it.
  out->x = 0xfffe;
  out->y = in->y;
  out->n = true;
  out->z = false;
  out->flags = COSIM_FLAG_N | COSIM_FLAG_Z;
}

// ---------------------------------------------------------------------------
// $80:8398  thread_tick_waits — no arguments
// ---------------------------------------------------------------------------

static void shim_thread_tick_waits(Wram* w, const Rom* rom, const CosimRegs* in,
                                   CosimRegs* out) {
  (void)rom;
  thread_tick_waits(w);

  // A holds whatever the last iteration — slot 0 — left there, and each of the
  // three paths through the loop body leaves A equal to the word the slot ends
  // up holding: unchanged when the slot is empty or already expired, and the
  // decremented value when it is stored. So A is just slot 0, afterwards.
  out->a = wram_r16(w, W_THREAD_WAIT);
  out->x = 0xfffe;  // `DEX DEX BPL`, as above
  out->y = in->y;
  out->n = true;
  out->z = false;
  out->flags = COSIM_FLAG_N | COSIM_FLAG_Z;

  // The only instruction here that touches carry is `CMP #$8000`, and it is
  // only reached for a slot that is live — where A is $8000 or above by
  // definition, so the comparison always sets it. A pass over 24 empty slots
  // never executes the `CMP` at all and leaves carry alone, which is why the
  // claim is conditional rather than a flat `true`.
  for (int slot = 0; slot < WRAM_THREAD_SLOTS; slot++) {
    if (wram_r16(w, W_THREAD_WAIT + (uint32_t)slot * 2) & 0x8000) {
      out->c = true;
      out->flags |= COSIM_FLAG_C;
      break;
    }
  }
}

// ---------------------------------------------------------------------------
// $80:83AE / $80:8418  the vblank queue adders — A = address, Y = bank
// ---------------------------------------------------------------------------

// Carry is these two routines' *return value*, and it is the one flag here that
// a caller definitely reads: `$82:AE3A` and `$82:AEA8` both do
// `JSL vbl_queue_b_add : BCS <back>` and spin until the job is accepted.
//
// `CPY #$0008 : BCS` is what sets it — carry clear means the count was below
// the cap and the job went in, carry set means it did not. Nothing after that
// touches carry, so it survives to the `RTL` on both paths.
//
// This is worth dwelling on, because getting it wrong is what the harness was
// built to catch and it very nearly was not caught. `verify` compares only the
// flags a shim claims to model, and the first version of these shims claimed N
// and Z but not carry — so 107 calls passed while the substitution left carry
// at whatever the caller happened to have. Under `run`, the caller's retry loop
// never exited and the routine was entered 147,405 times instead of 107. An
// unclaimed flag is not a small omission; it is an unchecked output.
//
// The three lines that decide it now live in `port/thread.c`, because
// `$80:C07F` ends `JML $8083AE` and hands the same three flags back as its own
// — see `port/hud.c`. What stays here is which of them this shim claims.
static void queue_flags(Wram* w, uint32_t count_at, uint16_t cap, bool added,
                        CosimRegs* out) {
  VblQueueFlags f;
  vbl_queue_flags(w, count_at, cap, added, &f);
  out->n = f.n;
  out->z = f.z;
  out->c = f.c;
  out->flags = COSIM_FLAG_N | COSIM_FLAG_Z | COSIM_FLAG_C;
}

static void shim_vbl_queue_a_add(Wram* w, const Rom* rom, const CosimRegs* in,
                                 CosimRegs* out) {
  (void)rom;
  int slot = vbl_queue_a_add(w, in->a, in->y);
  if (slot < 0) {  // full: `PLY : RTL` puts everything back
    out->a = in->a;
    out->x = in->x;
    out->y = in->y;
    queue_flags(w, W_VBL_QUEUE_A_COUNT, W_VBL_QUEUE_A_SLOTS, false, out);
    return;
  }
  // `DEC A : TAY` leaves the stored address in both A and Y, and X is the slot
  // the search stopped on.
  out->a = (uint16_t)(in->a - 1);
  out->y = out->a;
  out->x = (uint16_t)slot;
  queue_flags(w, W_VBL_QUEUE_A_COUNT, W_VBL_QUEUE_A_SLOTS, true, out);
}

static void shim_vbl_queue_b_add(Wram* w, const Rom* rom, const CosimRegs* in,
                                 CosimRegs* out) {
  (void)rom;
  // Queue B has no `TAY`, so Y keeps the last word its search read. The search
  // stops the moment it reads a zero, so that is 0 — except when it falls off
  // the bottom without testing slot 0, which leaves slot 1's address in Y.
  uint16_t probe = wram_r16(w, W_VBL_QUEUE_B + 4);

  int slot = vbl_queue_b_add(w, in->a, in->y);
  if (slot < 0) {
    out->a = in->a;
    out->x = in->x;
    out->y = in->y;
    queue_flags(w, W_VBL_QUEUE_B_COUNT, W_VBL_QUEUE_B_SLOTS, false, out);
    return;
  }
  // The bank is pulled back off the stack into A *after* the address is stored,
  // so A ends up holding the bank rather than the address.
  out->a = in->y;
  out->x = (uint16_t)slot;
  out->y = slot == 0 ? probe : 0;
  queue_flags(w, W_VBL_QUEUE_B_COUNT, W_VBL_QUEUE_B_SLOTS, true, out);
}

// ---------------------------------------------------------------------------
// $80:891A  fade_in — no arguments, and it suspends
// ---------------------------------------------------------------------------

// The first shim for a routine that does not run to completion.
//
// A suspension is an exit like any other, and the reason to say so out loud is
// the carry bug above. It would be easy to treat a yield as "the routine is not
// finished, so there is nothing to check yet" — but the state at the `JSL
// thread_yield` is handed straight to `PHP`, parked with the thread, and given
// back by `PLP` when it resumes. Anything wrong there is wrong for the rest of
// the routine, and in native mode nothing else would ever set it. So a
// suspension declares its registers and flags exactly as a return does.
//
// `in` is captured per *segment* — at the routine's entry, and again at each
// resumption — so "unchanged" here means unchanged across this run of the
// routine's own instructions, not across the suspension. That is the claim the
// routine's listing can actually support, and it is the one that survives Phase
// 4 replacing the scheduler underneath it.
//
//   * Neither X nor Y is mentioned anywhere in `$80:891A-$80:8932`, so both come
//     back as the segment found them.
//   * At a suspension, `LDA #$0001` is the last instruction before the `JSL`:
//     A is the sleep count, and N and Z describe it.
//   * Carry at a suspension depends on **which** suspension, and this is the one
//     thing here that is not obvious from reading the routine top to bottom. The
//     loop reaches the `JSL` by falling through `CMP #$000F`, which borrows for
//     every brightness below 15 and so leaves carry clear. The *first*
//     suspension is entered from the top of the routine and never executes that
//     `CMP` at all, so it carries the caller's own carry through untouched.
//     Fifteen of the sixteen segments agree with the simple answer, which is
//     exactly why it is worth getting right rather than guessing.
//   * At the return, `LDA $136C : CMP #$000F` is the tail: A is $000F, and 15
//     minus 15 is zero with no borrow, so N=0, Z=1, C=1. Nothing between that
//     and the `RTL` touches any of them.
static PortStep shim_fade_in(Wram* w, const Rom* rom, const CosimRegs* in,
                             CosimRegs* out, void* ctx, uint16_t* ticks) {
  (void)rom;
  FadeCtx* fade = (FadeCtx*)ctx;
  // Which segment is about to run, read before the call advances it.
  bool from_top = fade->co.resume == PORT_CORO_ENTRY;

  PortStep step = fade_in(w, fade, ticks);

  out->x = in->x;
  out->y = in->y;
  out->flags = COSIM_FLAG_N | COSIM_FLAG_Z | COSIM_FLAG_C;

  if (step == PORT_YIELDED) {
    // A is filled in from `ticks` by the harness — the sleep count *is* the
    // accumulator, and having one place decide that keeps the two from drifting.
    out->n = false;  // the count is 1: positive, non-zero
    out->z = false;
    out->c = from_top ? in->c : false;
    return step;
  }

  out->a = 0x000f;
  out->n = false;
  out->z = true;
  out->c = true;
  return step;
}

// ---------------------------------------------------------------------------
// $80:BC7F  actor_depth_sort — no arguments
// ---------------------------------------------------------------------------

// The relink needs to remember the record in front of the one it is looking at,
// and with one index register the only place to keep it is a direct-page byte.
// It is the same $38 `sprite_frame_tile` spills the caller's X into, and it is
// scratch for the same reason: nothing reads it across the call. The port keeps
// the predecessor in a C local.
static const CosimExclude DEPTH_SORT_EXCLUDES[] = {
    {0x0038, 2, "the ROM keeps the walk's predecessor here; the port uses a local"},
};

// What one pass would have cost the 65816, from what the pass did — the routine
// `cosim_cost` was written for, and the case that showed why a constant is not
// enough. `$80:BC7F` runs every frame and costs anywhere from 92 cycles to
// 7,524; declared at its mean of 1,605 it drifts `movies/level25-2p` off the
// stock core's framebuffer by frame 2,700.
//
// Every constant below is the sum of one straight-line run of the listing, at
// the access times this ROM actually gets: 6 master cycles for an opcode or
// operand byte (the boot code sets `$420D`, so banks $80+ are fast), 8 for a
// data byte — the display list and its head both live under `$7E:2000`, and
// every bank the data-bank register can hold reaches them in 8 — and 6 for an
// internal cycle. `LDX $1B5E : BEQ : RTS` is 34 + 18 + 40 = 92, which is exactly
// the minimum `verify` measures, and the whole model is checked against the ROM
// on every call.
//
// Refresh is deliberately absent, and so is the fetch penalty: see `cosim_cost`
// and `CosimRun`.
//
// $80:BC7F LDX $1B5E : BEQ (taken) : $80:BCE1 RTS.
static const CosimRun SORT_EMPTY = {34 + 18 + 40, 6};
// ...BEQ not taken, then LDY $12,X : BEQ (taken) : RTS.
static const CosimRun SORT_SINGLE = {34 + 12 + 34 + 18 + 40, 10};
// The same four instructions with both branches falling through: what every
// call with a pair in it starts with.
static const CosimRun SORT_PROLOGUE = {34 + 12 + 34 + 12, 9};
// $80:BCB0 LDY $12,X : BEQ, not taken — one more record to look at.
static const CosimRun SORT_STEP = {34 + 12, 4};
// ...and taken, which is where every full walk ends: + $80:BCE1 RTS.
static const CosimRun SORT_EXIT = {34 + 18 + 40, 5};
// $80:BCAD STX $38 : TYX, the advance both no-swap paths branch to and the head
// falls into.
static const CosimRun SORT_ADVANCE = {28 + 12, 3};
// $80:BCA3 / $80:BCCF, the two relinks. The head's moves the list head; the
// other has a predecessor to fix up, reloads X from `$38`, and jumps back to
// the top of the walk rather than through the advance.
static const CosimRun SORT_SWAP_HEAD = {40 + 34 + 34 + 34, 10};
static const CosimRun SORT_SWAP_MID = {40 + 34 + 34 + 28 + 34 + 28 + 40 + 18, 18};

// The four compares, in `ActorSortCmp` order. Each is `LDA $00,X : EOR $0000,Y
// : AND #$0020` — 92 cycles over 8 bytes — plus its own branch and whichever
// second test it needed, and each includes the branch it ends on, so the caller
// adds nothing.
static const CosimRun SORT_CMP[ACTOR_SORT_CMP_COUNT] = {
    [ACTOR_SORT_CMP_FIRST_SWAP] = {92 + 12 + 40 + 18 + 18, 18},
    [ACTOR_SORT_CMP_FIRST_NOSWAP] = {92 + 12 + 40 + 18 + 12 + 18, 20},
    [ACTOR_SORT_CMP_Y_SWAP] = {92 + 18 + 34 + 40 + 12, 17},
    [ACTOR_SORT_CMP_Y_NOSWAP] = {92 + 18 + 34 + 40 + 18, 17},
};

static int depth_sort_cycles(const ActorSortWork* k, bool fast) {
  if (k->empty) return cosim_run_cycles(&SORT_EMPTY, fast);
  if (k->single) return cosim_run_cycles(&SORT_SINGLE, fast);

  int cycles = cosim_run_cycles(&SORT_PROLOGUE, fast) +
               cosim_run_cycles(&SORT_EXIT, fast);
  for (int i = 0; i < ACTOR_SORT_CMP_COUNT; i++)
    cycles += k->compares[i] * cosim_run_cycles(&SORT_CMP[i], fast);
  if (k->swap_head) cycles += cosim_run_cycles(&SORT_SWAP_HEAD, fast);
  cycles += k->swap_mid * cosim_run_cycles(&SORT_SWAP_MID, fast);
  cycles += k->steps * cosim_run_cycles(&SORT_STEP, fast);
  // Once for the head, then once per loop step that did not relink — a mid-list
  // swap jumps straight back to `$80:BCB0`.
  cycles += (1 + k->steps - k->swap_mid) * cosim_run_cycles(&SORT_ADVANCE, fast);
  return cycles;
}

static void shim_actor_depth_sort(Wram* w, const Rom* rom, const CosimRegs* in,
                                  CosimRegs* out) {
  (void)rom;
  ActorSortWork work;
  uint16_t tail = actor_depth_sort_counted(w, &work);

  // Every direct-page address the routine touches costs one extra internal
  // cycle when the direct page is not page-aligned, and the model above does not
  // carry that term because no caller has ever presented one. If a caller ever
  // does, this reports nothing and the routine falls back to its declared mean —
  // visibly, as a `priced` below `checked` in the cost-model report.
  //
  // The data bank needs no such guard: every address the walk touches is under
  // `$2000`, which costs 8 cycles a byte through any bank there is.
  if ((in->d & 0xff) == 0)
    cosim_cost(depth_sort_cycles(&work, in->fastrom));

  // Every one of the three `RTS` paths is reached by a taken `BEQ`, so N and Z
  // are the same on all of them however the walk ended.
  out->n = false;
  out->z = true;
  out->flags = COSIM_FLAG_N | COSIM_FLAG_Z;

  // X is whichever record the walk stopped on: the tail, found by `LDY $12,X`
  // reading a zero link — or 0 from `LDX $1B5E` when the list was empty, which
  // is the one path that never touches Y.
  out->x = tail;
  out->y = tail != 0 ? 0 : in->y;
  out->regs = COSIM_REG_X | COSIM_REG_Y;

  // A and carry are not claimed, and the reason is worth writing down given how
  // much of this file is about unclaimed outputs being unchecked ones.
  //
  // Both are left holding an intermediate of whichever comparison ended the
  // pass, and *which* intermediate differs per path: A is either the flags word
  // masked to bit 5, or the Y coordinate that lost a `CMP`, or the link a swap
  // read; carry is that `CMP`'s result, or the caller's own if the pass never
  // reached one. Reproducing that here would mean writing the comparison a
  // second time in the shim, which is exactly the drift this file exists to
  // prevent.
  //
  // They are dead. The single caller is `$80:BD27`, and the next thing it does
  // is `JSR $80:BCE2`, which opens `LDY #$0000 : LDX $1B5E : BEQ` — it reads
  // neither — and whose own first use of carry is a `SEC`.
}

// ---------------------------------------------------------------------------
// $80:BCE2  actor_cull — no arguments
// ---------------------------------------------------------------------------

// What each run of `$80:BCE2` costs, indexed by `ActorCullBlock`. Same rules as
// the sort's table above: 6 cycles for a program byte with FastROM on, 8 for a
// byte of the display list, the camera or `visible_actors` — all of which live
// under `$7E:2000` and cost 8 through every bank the data-bank register can hold
// — and 6 for an internal cycle. Branch costs are folded into the block the
// outcome names, so a tally of the blocks needs nothing added to it.
//
// The empty-list case is the check that the rest is priced the same way:
// `LDY #$0000 : LDX $1B5E : BEQ` taken plus `STY $9C : RTS` is 70 + 68 = 138,
// which is exactly the minimum `verify` measures over the corpus.
static const CosimRun CULL_COST[CULL_BLOCK_COUNT] = {
    [CULL_BLK_EMPTY] = {18 + 34 + 18 + 28 + 40, 11},
    [CULL_BLK_PROLOGUE] = {18 + 34 + 12, 8},
    [CULL_BLK_UNDRAWN] = {34 + 18, 4},
    [CULL_BLK_DRAWN] = {34 + 12, 4},
    [CULL_BLK_SCREEN] = {12 + 18, 3},
    [CULL_BLK_WORLD] = {12 + 12, 3},
    // LDA $02,X : SEC : SBC $1B6A : CMP #$FF80 : BCS — 98 over 9 bytes, plus
    // the branch. The Y axis is the same five instructions off `$06` and `$1B6C`.
    [CULL_BLK_X_HIGH] = {98 + 18, 11},
    [CULL_BLK_X_TEST] = {98 + 12, 11},
    [CULL_BLK_X_IN] = {18 + 12, 5},
    [CULL_BLK_X_OUT] = {18 + 18, 5},
    [CULL_BLK_Y_HIGH] = {98 + 18, 11},
    [CULL_BLK_Y_TEST] = {98 + 12, 11},
    [CULL_BLK_Y_IN] = {18 + 12, 5},
    [CULL_BLK_Y_OUT] = {18 + 18, 5},
    [CULL_BLK_EMIT] = {12 + 40 + 12 + 12, 6},
    [CULL_BLK_ADVANCE] = {34 + 12 + 18, 5},
    [CULL_BLK_EXIT] = {34 + 12 + 12 + 28 + 40, 8},
};

static int cull_cycles(const ActorCullWork* k, bool fast) {
  int cycles = 0;
  for (int i = 0; i < CULL_BLOCK_COUNT; i++)
    cycles += k->blocks[i] * cosim_run_cycles(&CULL_COST[i], fast);
  return cycles;
}

static void shim_actor_cull(Wram* w, const Rom* rom, const CosimRegs* in,
                            CosimRegs* out) {
  (void)rom;
  ActorCullWork work;
  actor_cull_counted(w, &work);

  // `LDA $00,X`, `LDA $02,X`, `LDA $06,X`, `LDA $12,X` and `STY $9C` each cost
  // one extra internal cycle when the direct page is not page-aligned, and the
  // table does not carry that term because no caller presents one. The data bank
  // needs no guard: every address the walk touches is under `$2000`.
  if ((in->d & 0xff) == 0) cosim_cost(cull_cycles(&work, in->fastrom));

  // The walk ends on `LDA $12,X : TAX : BNE`, so it falls out with the zero
  // link in both A and X. An empty list exits earlier, from `LDX $1B5E : BEQ`,
  // which leaves X zero the same way but never touches A. `actor_cull` does not
  // move the list, so its head still says which of the two happened.
  out->a = wram_r16(w, W_ACTOR_LIST_HEAD) != 0 ? 0 : in->a;
  out->x = 0;
  // `STY $9C` is the count, straight out of Y.
  out->y = wram_r16(w, W_VISIBLE_ACTOR_COUNT);
  out->n = false;
  out->z = true;
  out->flags = COSIM_FLAG_N | COSIM_FLAG_Z;

  // Carry is the last window comparison the walk happened to make — one of four
  // `CMP`s, on whichever record was tested last, or the caller's if every record
  // was skipped on its flags. It is dead: the caller's next instruction is
  // `JSR $80:BC23`, which reaches its first `ADC` through the `CLC` at
  // $80:BC2F.
}

// ---------------------------------------------------------------------------
// $80:BC23  oam_buffer_clear — no arguments
// ---------------------------------------------------------------------------

// The one routine here whose cost does not depend on anything, and it is worth
// pricing anyway.
//
// `PHD : LDA #$13BE : TCD` means the caller's direct page cannot reach it, the
// eight iterations are `LDX #$0008` and nothing else, and every address it
// touches is under `$2000`. So this is a straight line 372 bytes long and there
// is nothing to count: 94 for the prologue, 24 for `LDY #$E0 : CLC`, 3,898 for
// eight passes of the sixteen `STY`s (482 each, +6 for the seven taken `BNE`s),
// 562 for `LDA #$AAAA` and its sixteen stores, and 92 to unwind.
//
// Every direct-page access in it pays an extra internal cycle, because `$13BE`
// is not page-aligned and neither is any of the eight pages `ADC #$0040` walks
// it through — the low byte alternates `$BE` and `$FE` and is never `$00`.
//
// **The interesting part is that pricing a constant is not redundant**, and the
// reason is the difference between the two burns rather than anything about
// this routine. `.cycles` is a mean of what `verify` *measured*, so it already
// contains the three or four refreshes the call crossed; `cycles_burn` hands it
// to the core in a single piece and the core adds one more. A reported cost is
// instruction cycles only and `cycles_burn_modelled` hands it over twelve at a
// time, so the core puts every refresh back exactly where the scanlines are.
//
// For this routine that is 4,814 + 40 against a real 4,790..4,830: about 43
// cycles a call too slow, every call, forever. It is the least interesting kind
// of drift there is and also the easiest to leave in place, because a routine
// whose measured spread is 40 wide looks like one there is nothing left to say
// about.
static const CosimRun OAM_CLEAR_COST = {94 + 24 + 3898 + 562 + 92, 372};

static void shim_oam_buffer_clear(Wram* w, const Rom* rom, const CosimRegs* in,
                                  CosimRegs* out) {
  (void)rom;
  oam_buffer_clear(w);
  cosim_cost(cosim_run_cycles(&OAM_CLEAR_COST, in->fastrom));

  // `LDA #$AAAA` and the sixteen stores of it are the last thing to touch A.
  out->a = 0xaaaa;
  // `SEP #$10` zeroes the high bytes of both index registers on the way in, so
  // when `REP #$30` widens them again X is the loop counter run down to 0 and Y
  // is still the $E0 it was seeded with.
  out->x = 0x0000;
  out->y = 0x00e0;
  // `PLD` is the last flag-setting instruction, so N and Z describe the direct
  // page it restores rather than anything the routine computed. The only caller
  // is `sprite_build_oam`, which has just done `PEA $0000 : PLD`, so what comes
  // back off the stack is zero.
  out->n = false;
  out->z = true;
  // `CLC` at $80:BC2F, and the `ADC #$0040` that walks the direct page across
  // the buffer eight times starts at $13BE and never carries out of 16 bits.
  out->c = false;
  out->flags = COSIM_FLAG_N | COSIM_FLAG_Z | COSIM_FLAG_C;
}

// ---------------------------------------------------------------------------
// $80:825E  thread_spawn — A:Y = the far entry, D = the caller's page
// ---------------------------------------------------------------------------

// The one routine so far whose *third* argument is the caller's direct page
// itself rather than something on it: its last act is to copy five words off
// that page onto the new thread's, so `in->d` is an input in the same way
// `player_collide`'s is, and for a completely different reason.
static void shim_thread_spawn(Wram* w, const Rom* rom, const CosimRegs* in,
                              CosimRegs* out) {
  int slot = thread_spawn(w, rom, in->a, in->y, in->d);

  // `TXA : RTL` on success, `LDA #$0000 : RTL` when the board is full — and the
  // ROM cannot tell those two apart either, because slot 0 doubled is also 0.
  out->a = slot < 0 ? 0 : (uint16_t)slot;
  // X is the slot the search settled on and survives to the `RTL`; on the full
  // path the search ran off the bottom at $FFFE and `PLA : PLD` do not touch it.
  out->x = slot < 0 ? 0xfffe : (uint16_t)slot;
  // Y is *not* the bank any more by the time it returns: the argument copy ends
  // `LDY #$0008 : LDA ($01,S),Y`, so what comes back is the last offset it read.
  // The harness found this on call 1 — WRAM matched and only Y did.
  out->y = slot < 0 ? in->y : (THREAD_SPAWN_ARGS - 1) * 2;
  // `TXA` is the last flag-setting instruction on the success path and the
  // `LDA #$0000` on the other; both describe what is in A.
  out->n = (out->a & 0x8000) != 0;
  out->z = out->a == 0;
  out->flags = COSIM_FLAG_N | COSIM_FLAG_Z;
}

// ---------------------------------------------------------------------------
// $80:8480  thread_call_handler — X = slot x2, Y = the argument, and it may
//                                 decline
// ---------------------------------------------------------------------------

// The dispatcher's tail, and the only place it is written down.
//
// `$80:8480` has two exits and they set N and Z from completely different
// instructions. A slot with no handler leaves through `LDA $1300,X : ORA
// $1330,X : BEQ $84B0`, so the flags describe the zero that `ORA` produced. A
// slot with one leaves through `PLX : PLD : PLB`, and `PLB` is the last of
// those to set a flag — so N and Z describe the *data bank* being restored,
// which has nothing to do with anything the routine computed. That is why
// `CosimRegs` carries `db`: it is an input to this routine's flags.
//
// A, X, Y and carry are the port's, because they are the handler's and the
// dispatcher passes them straight through.
static void handler_exit(const CosimRegs* in, const ThreadCallResult* t,
                         CosimRegs* out) {
  out->a = t->a;
  out->x = t->x;
  out->y = t->y;
  out->c = t->c;
  out->n = t->entered ? (in->db & 0x80) != 0 : false;
  out->z = t->entered ? in->db == 0 : true;
  out->flags = COSIM_FLAG_N | COSIM_FLAG_Z | COSIM_FLAG_C;
}

static bool guard_thread_call_handler(Wram* scratch, const Rom* rom,
                                      const CosimRegs* in) {
  ThreadCallResult out;
  if (thread_call_handler(scratch, rom, in->x, in->y, in->c, &out)) return true;

  // Declined. `handler_unported` already counts these; what it cannot say is
  // *which* handler, and a count with no address is not a work list. The
  // dispatcher hands the address back in `unported`.
  //
  // It is zero when the port *has* the handler and the handler declined one
  // level down — a jump-table entry, or an id an enemy `JML`s out on. Censusing
  // the door those came through would name the wrong routine, and each of them
  // is named properly by its own registry entry's guard.
  //
  // **This used to be a list of the four handlers that could decline internally,
  // and by the eighth copy of `$81:8888` it wanted eight.** Nothing would have
  // reported a missing entry: the symptom is a census line naming a routine the
  // port already has, which is exactly the shape of the bug that hid
  // `monster_collide`'s missing dispatch for four rounds.
  if (out.unported) cosim_census_note("handler", out.unported);
  return false;
}

// What the dispatch cost, or `false` when the handler it entered has no cost
// table yet. Defined far below, after every handler's own cost function, since
// it is the sum of the frame and one of those — see `ThreadCallWork`.
static bool thread_call_cycles(const ThreadCallWork* k, const CosimRegs* in,
                               int* out);

static void shim_thread_call_handler(Wram* w, const Rom* rom,
                                     const CosimRegs* in, CosimRegs* out) {
  ThreadCallResult t;
  ThreadCallWork work;
  // the guard allowed it
  thread_call_handler_counted(w, rom, in->x, in->y, in->c, &t, &work);
  handler_exit(in, &t, out);

  // `LDA $1300,X` and the two beside it reach the thread tables through the
  // data bank, and every bank that mirrors low WRAM costs the same 8 a byte —
  // so the model holds for `$7E` and for the `$80` the sprite pass leaves, and
  // asks only that it is not a bank where `$1300` would be ROM.
  int cycles;
  if ((in->db < 0x40 || (in->db >= 0x80 && in->db < 0xc0)) &&
      thread_call_cycles(&work, in, &cycles))
    cosim_cost(cycles);
}

// ---------------------------------------------------------------------------
// $80:F7F7  player_collide — A = the other actor's id, D = the player's page
// ---------------------------------------------------------------------------

// Registered even though `thread_call_handler` already calls it, for the reason
// that made `actor_collide_notify` worth its own entry: the enclosing routine
// declines every dispatch whose handler is unported, so a handler seen only
// through it would never be offered the calls that go somewhere else. Its own
// entry PC gets all of them.
//
// It is also the first ported routine whose direct page is not `$0000`. The
// dispatcher installed the player thread's page from `$80:82DE` before `RTL`ing
// here, and every `$xx` in the listing is an offset into it — which is why the
// shim hands the port `in->d` rather than assuming, and why a listing read with
// `zamn_disasm` needs the same caveat (it resolves direct-page operands as
// though `D` were zero).
static bool guard_player_collide(Wram* scratch, const Rom* rom,
                                 const CosimRegs* in) {
  ActorHandlerRegs r = {.a = in->a, .x = in->x, .y = in->y};
  uint32_t unported = 0;
  if (player_collide(scratch, rom, in->d, in->a, &r, &unported)) return true;
  // Name the routine that is missing, and let the port say which one that is.
  // For most ids it is the jump-table entry — the entry, not the id, because
  // twelve ids sharing a target are one piece of work and `$80:F8D6` is more
  // use in a report than `$29`. For a pickup it is not the entry at all:
  // `$80:F87B` *is* ported and what it ran out of road on is the auto-select
  // it tail-calls. Censusing the table entry there would put a routine that
  // already exists at the top of the work list.
  cosim_census_note("player id table", unported);
  return false;
}

// Both handlers return the same way — `CLC : RTL` for the player, `CLC : RTL`
// or `SEC : RTL` for the enemy — so what a shim has to say is just which
// registers the path it took left behind. The port fills all of them, because
// which exit ran is exactly the thing the port knows and the shim does not.
static void handler_regs(const ActorHandlerRegs* r, CosimRegs* out) {
  out->a = r->a;
  out->x = r->x;
  out->y = r->y;
  out->n = r->n;
  out->z = r->z;
  out->c = r->c;
  out->flags = COSIM_FLAG_N | COSIM_FLAG_Z | COSIM_FLAG_C;
}

// What `$80:F950` costs, indexed by `PlayerHurtBlock`. Every one of its reads
// is direct-page except `LDA $1CBC,X`, which goes through the data bank to a
// WRAM mirror, so the third column carries almost the whole routine.
static const CosimRun HURT_COST[HURT_BLOCK_COUNT] = {
    [HURT_BLK_STATE_A] = {28 + 18 + 18, 7, 1},
    [HURT_BLK_STATE_B] = {28 + 18 + 12 + 18 + 18, 12, 1},
    // ...and neither matched, so `LDX $0E : LDA $1CBC,X : CMP #$0004` runs.
    [HURT_BLK_STATE_PASS] = {28 + 18 + 12 + 18 + 12 + 28 + 40 + 18, 20, 2},
    [HURT_BLK_WEAPON_OTHER] = {18, 2},
    [HURT_BLK_WEAPON_MATCH] = {12 + 28, 4, 1},
    [HURT_BLK_HEALTH_SET] = {18, 2},
    [HURT_BLK_HEALTH_ZERO] = {12, 2},
    [HURT_BLK_TIMER] = {28, 2, 1},
    [HURT_BLK_IFRAMES] = {18, 2},
    // $80:F96E LDA #$8001 : STA $50 : LDA #$0040 : STA $52, behind the `BPL`
    // that did not branch.
    [HURT_BLK_TAKEN] = {12 + 18 + 28 + 18 + 28, 12, 2},
    [HURT_BLK_RTS] = {40, 1},
};

static int hurt_cycles(const uint16_t* blk, bool fast, bool dp_unaligned) {
  int cycles = 0;
  for (int i = 0; i < HURT_BLOCK_COUNT; i++)
    cycles +=
        blk[i] * cosim_run_cycles_dp(&HURT_COST[i], fast, dp_unaligned);
  return cycles;
}

// `$80:F7F7`'s own two blocks. The dispatch block runs from the `CMP` to the
// `RTL` and includes the `JSR ($F808,X)` but not what it reached: that is the
// target's, and `PlayerCollideWork::target` says which.
static const CosimRun PLAYER_COST[PLAYER_BLOCK_COUNT] = {
    [PLAYER_BLK_IGNORE] = {18 + 18 + 12 + 42, 7},
    [PLAYER_BLK_DISPATCH] = {18 + 12 + 12 + 12 + 34 + 28 + 52 + 12 + 42, 19, 1},
};

// `$80:F87A`, the entry seventeen ids share: a bare `RTS` and nothing else.
static const CosimRun PLAYER_TARGET_NOP = {40, 1};

// Returns false when the id dispatched to a jump-table entry with no table
// here. Ten of the seventeen targets are ported and two are priced, so this
// declines on more calls than it serves for now — see `docs/cosim.md`.
static bool player_cycles(const PlayerCollideWork* k, bool fast,
                          bool dp_unaligned, int* out) {
  int cycles = 0;
  for (int i = 0; i < PLAYER_BLOCK_COUNT; i++)
    cycles +=
        k->blocks[i] * cosim_run_cycles_dp(&PLAYER_COST[i], fast, dp_unaligned);

  if (k->blocks[PLAYER_BLK_IGNORE]) {  // the id was out of range: no dispatch
    *out = cycles;
    return true;
  }
  switch (k->target) {
    case PLAYER_COLLIDE_NOP:
      cycles += cosim_run_cycles_dp(&PLAYER_TARGET_NOP, fast, dp_unaligned);
      break;
    case PLAYER_COLLIDE_HURT:
      cycles += hurt_cycles(k->hurt, fast, dp_unaligned);
      break;
    default:
      return false;
  }
  *out = cycles;
  return true;
}

static void shim_player_collide(Wram* w, const Rom* rom, const CosimRegs* in,
                                CosimRegs* out) {
  ActorHandlerRegs r = {.a = in->a, .x = in->x, .y = in->y};
  PlayerCollideWork work;
  // the guard allowed it
  player_collide_counted(w, rom, in->d, in->a, &r, NULL, &work);
  handler_regs(&r, out);

  int cycles;
  if (player_cycles(&work, in->fastrom, (in->d & 0xff) != 0, &cycles))
    cosim_cost(cycles);
}

// ---------------------------------------------------------------------------
// $80:CCC8  apu_send — X = the command, A = its parameter
// ---------------------------------------------------------------------------

// The one routine in the registry with two kinds of caller that disagree about
// register width, and the reason it took this long to register: 8 bits from the
// data-set uploader, 16 from `apu_play_sfx`. `port/apu.h` argues why that turns
// out not to need a width field in `CosimRegs` — the routine's own opening `SEP
// #$30` normalises it — and this is where the argument gets tested, on both
// widths at once.
static void shim_apu_send(Wram* w, const Rom* rom, const CosimRegs* in,
                          CosimRegs* out) {
  (void)rom;
  ApuSendRegs r;
  apu_send(w, in->a, in->x, &r);
  out->a = r.a;
  out->x = r.x;
  out->y = r.y;
  out->n = r.n;
  out->z = r.z;
  out->c = r.c;
  out->flags = COSIM_FLAG_N | COSIM_FLAG_Z | COSIM_FLAG_C;
}

// ---------------------------------------------------------------------------
// $80:CC3B  apu_play_sfx — A = the sound effect id
// ---------------------------------------------------------------------------

// The first ported routine that talks to hardware, and the first whose flags
// are decided by a register nobody thought they were passing: `PLD` restores
// the caller's direct page and sets N and Z from it. See `port/apu.h`.
//
// Registered in its own right rather than only through the two collision
// entries that reach it, and this one earns it more than most: the movie's
// callers are six different routines (`$80:9738`, `$80:D085`, `$80:E9D1`,
// `$80:EA9F`, `$80:F87F`, `$82:AE94`), only one of which is on the collision
// path. Everything else about it would go unchecked.
static void shim_apu_play_sfx(Wram* w, const Rom* rom, const CosimRegs* in,
                              CosimRegs* out) {
  (void)rom;
  ApuSfxRegs r;
  apu_play_sfx(w, in->a, in->d, &r);
  out->a = r.a;
  out->x = r.x;
  out->y = r.y;
  out->n = r.n;
  out->z = r.z;
  out->c = r.c;
  out->flags = COSIM_FLAG_N | COSIM_FLAG_Z | COSIM_FLAG_C;
}

// ---------------------------------------------------------------------------
// $80:CCBF  apu_next_byte — nothing in, one byte out, cursor advanced
// ---------------------------------------------------------------------------

// The most-called entry in this registry, and the one where a shim's "which
// registers did you model" mask earns its keep the other way round: X and Y are
// never touched by five instructions that mention neither, so they are claimed
// by simply handing back what came in, and any surprise is a diff.
//
// The routine runs eight bits wide, and `out->a`'s high byte is the caller's —
// see `port/apu.h`, which is the same preservation `apu_send` documents.
static void shim_apu_next_byte(Wram* w, const Rom* rom, const CosimRegs* in,
                               CosimRegs* out) {
  ApuNextRegs r;
  apu_next_byte(w, rom, in->a, &r);
  out->a = r.a;
  out->x = in->x;
  out->y = in->y;
  out->n = r.n;
  out->z = r.z;
  out->flags = COSIM_FLAG_N | COSIM_FLAG_Z;
}

// ---------------------------------------------------------------------------
// $80:CC7C  apu_load_set — written, and with no shim
// ---------------------------------------------------------------------------
//
// The other two APU routines' caller, and the reason they are called a quarter
// of a million times each. `apu_load_set()` exists in `port/apu.c` and is the
// largest single item the ranking still offers — 1.25M instructions over the
// six profiled movies, 0.7% of everything the game does.
//
// It cannot be checked. A call is about 104,000 instructions, which is some
// eight frames, and the harness abandons any call an interrupt lands inside;
// every one of them does. There is no shim here because a `static` function
// nothing references is a warning, and no registry entry because the row would
// read `not reached` forever. The registry section below says the rest.

// ---------------------------------------------------------------------------
// $80:9D5B  spawn_has_room — nothing in, carry out
// ---------------------------------------------------------------------------

// Six instructions, no arguments and no writes: it reads two globals and
// answers with a flag. The port takes a `const Wram*` for that reason, which is
// the only shim here whose routine could not modify memory if it wanted to.
static void shim_spawn_has_room(Wram* w, const Rom* rom, const CosimRegs* in,
                                CosimRegs* out) {
  (void)rom;
  SpawnRoomRegs r;
  spawn_has_room(w, &r);
  out->a = r.a;
  out->x = in->x;
  out->y = in->y;
  out->n = r.n;
  out->z = r.z;
  out->c = r.c;
  out->flags = COSIM_FLAG_N | COSIM_FLAG_Z | COSIM_FLAG_C;
}

// ---------------------------------------------------------------------------
// $80:9C90  sin_deg — A = degrees, A = sin x 128
// ---------------------------------------------------------------------------

// Registered at the inner `JSR` target rather than at `$80:9C8C`, the four-byte
// `JSR : RTL` trampoline in front of it, because that is where the work is and
// every call reaches it either way. The trampoline is therefore *subsumed*: it
// is two instructions neither side can get wrong.
//
// N and Z come off the closing `PLX`, so they belong to the caller's own index
// register — the third routine in this registry whose flags describe an
// argument nobody thought they were passing, after `apu_play_sfx`'s `PLD` and
// `terrain_point_bit2`'s. Carry is the `CMP #$FF`'s and means *sentinel*.
static void shim_sin_deg(Wram* w, const Rom* rom, const CosimRegs* in,
                         CosimRegs* out) {
  (void)w;
  SinRegs r;
  sin_deg(rom, in->a, in->x, &r);
  out->a = r.a;
  out->x = in->x;
  out->y = in->y;
  out->n = r.n;
  out->z = r.z;
  out->c = r.c;
  out->flags = COSIM_FLAG_N | COSIM_FLAG_Z | COSIM_FLAG_C;
}

// ---------------------------------------------------------------------------
// $80:F327  actor_publish_pos — nothing in; the record is on the thread's page
// ---------------------------------------------------------------------------

// Everything it needs is on `in->d`, which is why `CosimRegs` carries a direct
// page at all: this routine takes no register arguments and would be
// uncallable without it.
static void shim_actor_publish_pos(Wram* w, const Rom* rom, const CosimRegs* in,
                                   CosimRegs* out) {
  (void)rom;
  PublishRegs r;
  actor_publish_pos(w, in->d, in->x, in->y, &r);
  out->a = r.a;
  out->x = r.x;
  out->y = r.y;
  out->n = r.n;
  out->z = r.z;
  out->flags = COSIM_FLAG_N | COSIM_FLAG_Z;
}

// ---------------------------------------------------------------------------
// $81:8024  nearest_player_dist — the point is on the page too
// ---------------------------------------------------------------------------

static void shim_nearest_player_dist(Wram* w, const Rom* rom,
                                     const CosimRegs* in, CosimRegs* out) {
  (void)rom;
  NearestRegs r;
  nearest_player_dist(w, in->d, in->y, &r);
  out->a = r.a;
  out->x = r.x;
  out->y = r.y;
  out->n = r.n;
  out->z = r.z;
  out->c = r.c;
  out->flags = COSIM_FLAG_N | COSIM_FLAG_Z | COSIM_FLAG_C;
}

// ---------------------------------------------------------------------------
// $80:C05A  sprite_cache_init — A:Y is the frame array, and DB is the answer
// ---------------------------------------------------------------------------

// **The first routine here that `verify` cannot score and `run` can.**
// `$80:C05A` is 16,900 instructions of two stores in a loop — 346,734 master
// cycles, which is 0.97 of a frame — so an NMI lands inside very nearly every
// call and the per-call harness abandons all of them: two calls on `boot.zmv`,
// two on `level1.zmv`, zero checked, twice interrupted. For three rounds that
// was read as "unregisterable", and `tools/native_share.py` still carried the
// address in `BLOCKED` beside `$80:CD20 lzss_decompress` and `$80:AD2B
// blockmap_expand`.
//
// That reading was one instrument too narrow. What an interrupt breaks is the
// *rewind-and-replay* claim: the ROM's NMI handler wrote WRAM inside the call
// window and the port, which models the routine and not the handler, cannot
// account for it. It breaks nothing about substitution. Under `run` the ROM
// never executes these instructions at all, so there is no window for an
// interrupt to land inside — the core sits at the instruction after the `JSL`
// while the budget is burned, and if an NMI falls due during it the core takes
// it exactly as it would have anyway.
//
// So this is the first entry marked `run_only`, and it is the mirror of
// `verify_only` in both mechanism and honesty: that flag means *checked on
// every call and never substituted*, this one means *substituted and never
// checked per call*. What checks it instead is `run` itself, which compares all
// 128 KB of WRAM once per scheduler pass — a claim about a stretch rather than
// about a call, which is the claim Phase 4 has to be built on. See
// `CosimRoutine::run_only`.
//
// **Its cycle figure is a count rather than a mean**, which is the other half of
// what makes it safe to substitute unmeasured. Every other entry's `.cycles` is
// an average `verify` observed; there is no average to observe here, and there
// does not need to be one — the routine has no data dependence and no branch
// that is not the loop, so `tools/cycles816.py` prices it exactly:
//
//     prologue $C05A..$C06A                                       184
//     loop 1   4,097 x (STA abs,X + DEX + DEX) + 4,096 taken BPL  335,948
//     LDX #$00FE                                                   18
//     loop 2   128 x the same + 127 taken BPL                   10,490
//     epilogue PLB : PLB : RTL                                      94
//
// `BPL` and not `BNE` is why the counts are 4,097 and 128 rather than 4,096 and
// 127 — the same off-by-one that makes the routine write `$2002` bytes, which
// `port/sprite_cache.h` records from the other direction.
//
// **0.97 of a frame is a safety margin and not a coincidence to ignore.** The
// core takes at most one pending interrupt when it resumes, so a substituted
// call that spans more than one NMI boundary would leave one NMI un-taken that
// the ROM took — a real divergence, and the thing that makes "outlives a frame"
// the right worry even under `run`. At 346,734 the call fits inside a frame and
// can straddle at most one boundary, so it cannot. With FastROM *off* the same
// routine costs 405,930, which can straddle two. Measured, `run` burns 357,194
// master cycles a call including the refreshes the core adds, which is 346,734
// plus 261 of them — so `$420D` is set on every call any movie makes, and the
// margin is real rather than assumed. If an input ever reaches this routine
// with FastROM off, this is the line that says what to expect.
//
// It is also the first routine here whose *only* claimed flags come from
// `in->db`. That field was added for `$80:8480 thread_call_handler`, whose exit
// `PLB` restores its caller's bank; this is the same instruction used the same
// way, two banks apart.
static const CosimRun SPRITE_CACHE_INIT_COST = {184 + 335948 + 18 + 10490 + 94,
                                                29598};

static void shim_sprite_cache_init(Wram* w, const Rom* rom, const CosimRegs* in,
                                   CosimRegs* out) {
  (void)rom;  // no table, no ROM read
  SpriteCacheInitRegs r;
  sprite_cache_init(w, in->a, in->y, in->db, &r);
  out->a = r.a;
  out->x = r.x;
  out->y = r.y;
  out->n = r.n;
  out->z = r.z;
  out->flags = COSIM_FLAG_N | COSIM_FLAG_Z;
  cosim_cost(cosim_run_cycles(&SPRITE_CACHE_INIT_COST, in->fastrom));
}

// ---------------------------------------------------------------------------
// $80:9570  wave_hdma_build — everything is on the wobble thread's page
// ---------------------------------------------------------------------------

// **Three of the sixteen blocks price a routine that has a registry entry of
// its own.** The far call at `$80:959A` costs the trampoline `$80:9C8C JSR :
// RTL` plus whichever arm of `$80:9C90 sin_deg` the table byte sent it down,
// plus the caller's own `BIT #$8000 : BEQ : ORA #$FF00`, which is decided by
// the same bit as the arm. `sin_deg` is in the registry two entries up and that
// is not a duplicate: when `wave_hdma_build` is substituted its body never
// runs, so the `JSL` never happens and the shim that would have priced those
// 223 calls is never entered. One entry is what `verify` checks `sin_deg`
// with; this is what `run` pays for it.
//
// The two agree, which is the check. `sin_deg`'s own row measures a floor of
// **240** cycles over 2,676 calls, and 118 + 48 + 74 — the shared prologue, the
// sentinel arm, `PLX : RTS` — is 240 exactly. Its call count is the other
// check: **2,676 is 12 x 223**, the twelve calls a level movie makes here
// against the 223 iterations of a full-length table, and it was measured long
// before anything counted the loop.
//
// The three arms differ by 36 cycles and the negative one is taken on roughly
// half the circle, so the difference is real money — about 2,700 cycles a
// full-length call, which is 1.7% of it and forty times the spread the twelve
// level-movie calls measure between them.
// In the three `SIN` rows, 82 is the trampoline's `JSR $9C90 : RTL`; 118 is
// `PHX : SEP : TAX : LDA $839431,X : CMP : REP` — the `LDA` is a long-indexed
// read out of bank $83, so with FastROM on it costs 30 and the data byte is one
// of the run's fetched bytes; 74 is `PLX : RTS`; and the 36 or 48 on the end is
// the caller's. None of it touches the direct page: `sin_deg` is written to be
// callable from anywhere and it is.
static const CosimRun WAVE_COST[WAVE_BLOCK_COUNT] = {
    // $80:9570 LDA $70 : BMI taken : $95DA RTS.
    [WAVE_BLK_OVER] = {28 + 18 + 40, 5, 1},
    // ...BMI not taken, through `LDA #$00F8 : STA $7E8000,X : INX`. The header
    // store is sixteen bits into bank $7E, which is 8 a byte and never fast.
    [WAVE_BLK_PROLOGUE] = {244, 26, 3},
    // ...with `$957C LDA #$0000` in it, which is the phase's yearly wrap.
    [WAVE_BLK_PROLOGUE_WRAP] = {256, 29, 3},
    // $80:958D INY x4 : CPY #$0168 : BCC taken.
    [WAVE_BLK_HEAD] = {48 + 18 + 18, 9},
    // ...not taken : LDY #$0000. Twice or three times a call, four degrees of
    // arc at a time.
    [WAVE_BLK_HEAD_WRAP] = {48 + 18 + 12 + 18, 12},
    // $80:9599 TYA : JSL $809C8C ... $95A6 STA $7E8000,X : INX : INX : CPX
    // #$00F1 — everything in the body that does not depend on an outcome.
    [WAVE_BLK_ITER] = {12 + 54 + 40 + 12 + 12 + 18, 14},
    // $80:9C9C BNE not taken : LDA #$0080 : BRA taken, and bit 15 clear, so the
    // caller's `BEQ` skips its `ORA`: + 36.
    [WAVE_BLK_SIN_SENTINEL] = {82 + 118 + 12 + 18 + 18 + 74 + 36, 31},
    // ...taken : BIT #$0080 : BEQ not taken : ORA #$FF00 : BRA taken. The one
    // arm that sets bit 15, so the caller's dead `ORA` runs too: + 48.
    [WAVE_BLK_SIN_NEGATIVE] = {82 + 118 + 18 + 18 + 12 + 18 + 18 + 74 + 48, 39},
    // ...and `BEQ` taken : AND #$00FF.
    [WAVE_BLK_SIN_POSITIVE] = {82 + 118 + 18 + 18 + 18 + 18 + 74 + 36, 34},
    // $80:95AF BNE taken: 222 of the 223 iterations of a full-length call.
    [WAVE_BLK_HEADER_SKIP] = {18, 2},
    // ...not taken : PHA : LDA #$F800 : STA $7E7FFF,X : INX : PLA. Once.
    [WAVE_BLK_HEADER_FIXUP] = {12 + 28 + 18 + 40 + 12 + 34, 12},
    // $80:95BB CPX $70 : BCC taken / not taken.
    [WAVE_BLK_STEP] = {28 + 18, 4, 1},
    [WAVE_BLK_EXIT] = {28 + 12, 4, 1},
    // $80:95BF PHA : LDA #$0000 : STA $7E8000,X : PLA : CMP #$0000 — 138 over
    // 12 bytes — and then one of three tails, each ending on the shared `RTS`.
    [WAVE_BLK_OFF_AXIS] = {138 + 18 + 40, 15},
    [WAVE_BLK_HOLD] = {138 + 12 + 28 + 12 + 12 + 28 + 18 + 40, 24, 2},
    // `DEC $70` twice, and a direct-page read-modify-write is 50 apiece.
    [WAVE_BLK_RETRACT] = {138 + 12 + 28 + 18 + 50 + 50 + 40, 23, 3},
};

static int wave_cycles(const WaveWork* k, bool fast, bool dp_unaligned) {
  int cycles = 0;
  for (int i = 0; i < WAVE_BLOCK_COUNT; i++)
    cycles += k->blocks[i] * cosim_run_cycles_dp(&WAVE_COST[i], fast,
                                                 dp_unaligned);
  return cycles;
}

// Carry is an input for the same reason `enemy_collide`'s is: one exit does not
// touch it. Here it is the first instruction's `BMI`, which is the exit taken
// on every frame after the effect has finished — so the path that needs the
// passthrough is the common one rather than the rare one.
static void shim_wave_hdma_build(Wram* w, const Rom* rom, const CosimRegs* in,
                                 CosimRegs* out) {
  WaveRegs r;
  WaveWork work;
  wave_hdma_build_counted(w, rom, in->d, in->x, in->y, in->c, &r, &work);
  out->a = r.a;
  out->x = r.x;
  out->y = r.y;
  out->n = r.n;
  out->z = r.z;
  out->c = r.c;
  out->flags = COSIM_FLAG_N | COSIM_FLAG_Z | COSIM_FLAG_C;
  cosim_cost(wave_cycles(&work, in->fastrom, (in->d & 0xff) != 0));
}

// ---------------------------------------------------------------------------
// $81:9BF3  actor_step_bearing — A is a doubled direction
// ---------------------------------------------------------------------------

// X is claimed, and it is the leftover of whichever of three *other* ported
// routines refused the second axis. That is only checkable because all three
// model their own X: `terrain_blocked_enemy` and `actor_at_point` write it, and
// `terrain_out_of_bounds` provably does not touch either index register. The
// port therefore reproduces a register it never chose, by composition.
static void shim_actor_step_bearing(Wram* w, const Rom* rom, const CosimRegs* in,
                                    CosimRegs* out) {
  StepBearingRegs r;
  actor_step_bearing(w, rom, in->d, in->a, in->y, &r);
  out->a = r.a;
  out->x = r.x;
  out->y = r.y;
  out->n = r.n;
  out->z = r.z;
  out->c = r.c;
  out->flags = COSIM_FLAG_N | COSIM_FLAG_Z | COSIM_FLAG_C;
}

// ---------------------------------------------------------------------------
// $81:C16B, $81:C00B — the monster's walk, and its load
// ---------------------------------------------------------------------------

// Two entries for what is nearly one routine: `$81:C16B` falls into `$81:C00B`
// on three of its four paths, so registering the caller alone would leave the
// callee unchecked on the frames it is reached from anywhere else — and there
// is nowhere else, which is exactly the sort of claim worth making the harness
// prove rather than reading off a listing.
//
// V is not claimed by either. On `$81:C00B`'s working path it is a coordinate
// addition's overflow, on its guard path it is the caller's own, and the walk
// above never touches it; the one caller in the ROM reads none of the four.
static void shim_monster_place_carried(Wram* w, const Rom* rom,
                                       const CosimRegs* in, CosimRegs* out) {
  MonsterCarryRegs r;
  monster_place_carried(w, rom, in->d, in->a, in->x, &r);
  out->a = r.a;
  out->x = r.x;
  out->y = r.y;
  out->n = r.n;
  out->z = r.z;
  out->c = r.c;
  out->flags = COSIM_FLAG_N | COSIM_FLAG_Z | COSIM_FLAG_C;
}

static void shim_monster_anim(Wram* w, const Rom* rom, const CosimRegs* in,
                              CosimRegs* out) {
  MonsterAnimRegs r;
  monster_anim(w, rom, in->d, in->x, &r);
  out->a = r.a;
  out->x = r.x;
  out->y = r.y;
  out->n = r.n;
  out->z = r.z;
  out->c = r.c;
  out->flags = COSIM_FLAG_N | COSIM_FLAG_Z | COSIM_FLAG_C;
}

// ---------------------------------------------------------------------------
// $81:BB75, $81:BBA4  monster_seek, monster_deliver — nothing in but the page
// ---------------------------------------------------------------------------
//
// The other half of each of the creature's states: `monster_anim` draws and one
// of these two decides where to go. Neither takes an argument — the first
// instruction of one is `STZ $24` and of the other `LDA $0A` — so the direct
// page is the whole calling convention.
//
// Both end in a `JMP` to a two-instruction stub whose `RTS` is the one that
// returns, which is why `ret_op` below points at an address that does not look
// like either routine's last instruction. `ret_op` decides where a *substituted*
// call is sent, not how a returning one is recognised, so any `RTS` inside the
// routine does the job; see `port/monster.h`.
static void shim_monster_seek(Wram* w, const Rom* rom, const CosimRegs* in,
                              CosimRegs* out) {
  (void)rom;
  MonsterSeekRegs r;
  monster_seek(w, in->d, &r);
  out->a = r.a;
  out->x = r.x;
  out->y = r.y;
  out->n = r.n;
  out->z = r.z;
  out->c = r.c;
  out->flags = COSIM_FLAG_N | COSIM_FLAG_Z | COSIM_FLAG_C;
}

static void shim_monster_deliver(Wram* w, const Rom* rom, const CosimRegs* in,
                                 CosimRegs* out) {
  (void)rom;
  MonsterDeliverRegs r;
  monster_deliver(w, in->d, &r);
  out->a = r.a;
  out->x = r.x;
  out->y = r.y;
  out->n = r.n;
  out->z = r.z;
  out->c = r.c;
  out->flags = COSIM_FLAG_N | COSIM_FLAG_Z | COSIM_FLAG_C;
}

// ---------------------------------------------------------------------------
// $80:BE0C, $80:BE41 — a display record's two ends
// ---------------------------------------------------------------------------

static void shim_actor_slot_alloc(Wram* w, const Rom* rom, const CosimRegs* in,
                                  CosimRegs* out) {
  (void)rom;
  SlotAllocRegs r;
  actor_slot_alloc(w, in->db, &r);
  out->a = r.a;
  out->x = r.x;
  out->y = r.y;
  out->n = r.n;
  out->z = r.z;
  out->c = r.c;
  out->flags = COSIM_FLAG_N | COSIM_FLAG_Z | COSIM_FLAG_C;
}

// `in->d` is not scratch here the way it is everywhere else in this file — the
// routine installs page zero for itself and puts the caller's back with `PLD`,
// which is what decides N and Z on the only path that changes anything.
static void shim_actor_slot_free(Wram* w, const Rom* rom, const CosimRegs* in,
                                 CosimRegs* out) {
  (void)rom;
  SlotFreeRegs r;
  actor_slot_free(w, in->a, in->d, in->x, in->y, &r);
  out->a = r.a;
  out->x = r.x;
  out->y = r.y;
  out->n = r.n;
  out->z = r.z;
  out->c = r.c;
  out->flags = COSIM_FLAG_N | COSIM_FLAG_Z | COSIM_FLAG_C;
}

// ---------------------------------------------------------------------------
// $81:8888  enemy_collide — the same argument, on an enemy's page
// ---------------------------------------------------------------------------

// Carry is an input as well as an output, and only on one path: `enemy_die`
// hands whatever it arrived with to `score_add`, whose discard path passes it
// straight through to the `RTL`. Every other exit sets it outright.
static bool guard_enemy_collide(Wram* scratch, const Rom* rom,
                                const CosimRegs* in) {
  ActorHandlerRegs r = {.a = in->a, .x = in->x, .y = in->y, .c = in->c};
  uint32_t unported = 0;
  if (enemy_collide(scratch, rom, in->d, in->a, &r, &unported)) return true;
  // Two ids left, and the census names the routine each goes to rather than the
  // id, for the reason `player_collide` does: the routine is the piece of work.
  cosim_census_note("enemy id", unported);
  return false;
}

// The two exits that end inside `$81:8888`, indexed by `EnemyCollideBlock`.
// `ENEMY_BLK_DEEP` has no entry on purpose: a call that reached it left through
// a `JML` or a `JSR` into a routine this file does not describe, and the model
// declines rather than pricing the part it can see.
static const CosimRun ENEMY_COST[ENEMY_BLOCK_COUNT] = {
    [ENEMY_BLK_IGNORE] = {18 + 12 + 12 + 42, 7},
    // $81:888F through the damage subtraction, the `BMI` falling through, and
    // the `BEQ` at `$81:88AF` taking it to the `CLC : RTL` at `$81:88C8`.
    [ENEMY_BLK_NO_DAMAGE] = {312 + 18 + 54, 43, 3},
};

static bool enemy_cycles(const EnemyCollideWork* k, bool fast,
                         bool dp_unaligned, int* out) {
  if (k->blocks[ENEMY_BLK_DEEP]) return false;
  int cycles = 0;
  for (int i = 0; i < ENEMY_BLOCK_COUNT; i++)
    cycles +=
        k->blocks[i] * cosim_run_cycles_dp(&ENEMY_COST[i], fast, dp_unaligned);
  *out = cycles;
  return true;
}

static void shim_enemy_collide(Wram* w, const Rom* rom, const CosimRegs* in,
                               CosimRegs* out) {
  ActorHandlerRegs r = {.a = in->a, .x = in->x, .y = in->y, .c = in->c};
  EnemyCollideWork work;
  // the guard allowed it
  enemy_collide_counted(w, rom, in->d, in->a, &r, NULL, &work);
  handler_regs(&r, out);

  int cycles;
  if (enemy_cycles(&work, in->fastrom, (in->d & 0xff) != 0, &cycles))
    cosim_cost(cycles);
}

// ---------------------------------------------------------------------------
// $81:C4A6  monster_collide — a second enemy subsystem, on a third kind of page
// ---------------------------------------------------------------------------

// Carry is an input on the same one path `enemy_collide`'s is: the death tail
// hands whatever arrived to `score_add`, whose discard path passes it through.
static bool guard_monster_collide(Wram* scratch, const Rom* rom,
                                  const CosimRegs* in) {
  ActorHandlerRegs r = {.a = in->a, .x = in->x, .y = in->y, .c = in->c};
  uint32_t unported = 0;
  if (monster_collide(scratch, rom, in->d, in->a, &r, &unported)) return true;
  // One decline left — id `$5D`'s `JML $81:BB05`, the other splice in the
  // `$81:8506` family — and the census names the routine rather than the id for
  // the reason it always has.
  cosim_census_note("monster id", unported);
  return false;
}

// The five exits that end inside `$81:C4A6`, indexed by `MonsterCollideBlock`,
// and the same table prices `$81:C440` — see the enum for the three bytes that
// differ and why none of them is on a priced path.
//
// **The last block is a cross-check and it is an exact one.** This routine is
// `$81:8888` on a different page, and its zero-damage exit prices to
// `384` over `43` bytes with `3` direct-page instructions — which is
// `ENEMY_BLK_NO_DAMAGE`, digit for digit, arrived at from a listing eleven
// kilobytes away and never compared until it was written down. Neither has ever
// been measured: no shot in the game carries a damage-table entry of zero, so
// both are transcription and both say the same thing.
static const CosimRun MONSTER_COST[MONSTER_BLOCK_COUNT] = {
    // $81:C4A6 CMP #$005C : BCS not taken : CMP #$000C : BCC taken, into the
    // shared `CLC : RTL` at `$81:C50A`.
    [MON_BLK_IGNORE_LOW] = {18 + 12 + 18 + 18 + 12 + 42, 12},
    // ...taken instead, then `CMP #$0033 : BCC` not taken, which falls into a
    // *different* `CLC : RTL` — the one at `$81:C4B5`.
    [MON_BLK_IGNORE_HIGH] = {18 + 12 + 18 + 12 + 18 + 12 + 12 + 42, 17},
    // ...the third comparison branching to `$81:C4EC LDA $26 : BNE` taken.
    [MON_BLK_LATCHED] = {96 + 28 + 18 + 12 + 42, 21, 1},
    // ...not taken, so the record is rewritten, the latch stored and
    // `JSR $C04A` — `LDA #$C050 : STA $12 : RTS`, 86 — queues the next routine.
    // The `BNE $C503` is taken here: `$0042` is not the one value that redirects.
    [MON_BLK_TAKE] = {96 + 28 + 12 + 28 + 18 + 40 + 34 + 18 + 18 + 28 + 40 + 86 +
                          12 + 42,
                      48, 4},
    // ...and not taken, which costs the branch's 6 back and one more absolute
    // load: `LDA $0046` at 34 over 3 bytes.
    [MON_BLK_TAKE_ALT] = {96 + 28 + 12 + 28 + 18 + 40 + 34 + 18 + 12 + 34 + 28 +
                              40 + 86 + 12 + 42,
                          51, 4},
    // $81:C4B7 through the damage subtraction, the `BMI` falling through, and
    // the `BEQ` at `$81:C4D7` taking it to the `CLC : RTL` at `$81:C50A`.
    [MON_BLK_NO_DAMAGE] = {18 + 18 + 312 + 18 + 54, 43, 3},
};

static bool monster_cycles(const MonsterCollideWork* k, bool fast,
                           bool dp_unaligned, int* out) {
  if (k->blocks[MON_BLK_DEEP]) return false;
  int cycles = 0;
  for (int i = 0; i < MONSTER_BLOCK_COUNT; i++)
    cycles +=
        k->blocks[i] * cosim_run_cycles_dp(&MONSTER_COST[i], fast, dp_unaligned);
  *out = cycles;
  return true;
}

static void shim_monster_collide(Wram* w, const Rom* rom, const CosimRegs* in,
                                 CosimRegs* out) {
  ActorHandlerRegs r = {.a = in->a, .x = in->x, .y = in->y, .c = in->c};
  MonsterCollideWork work;
  // the guard allowed it
  monster_collide_counted(w, rom, in->d, in->a, &r, NULL, &work);
  handler_regs(&r, out);

  int cycles;
  if (monster_cycles(&work, in->fastrom, (in->d & 0xff) != 0, &cycles))
    cosim_cost(cycles);
}

// ---------------------------------------------------------------------------
// $81:C440  monster_c440_collide — the same creature one stage earlier
// ---------------------------------------------------------------------------

// Registered separately even though the port body is shared with the routine
// above, and the reason is the same one that made `player_collide` worth its own
// entry: the two are different addresses in the ROM, so `verify` intercepting
// one never intercepts the other, and the flags each leaves are checked only at
// its own entry PC. A shared implementation is a claim that they compute the
// same thing; two registry entries are what test it.
static bool guard_monster_c440_collide(Wram* scratch, const Rom* rom,
                                       const CosimRegs* in) {
  ActorHandlerRegs r = {.a = in->a, .x = in->x, .y = in->y, .c = in->c};
  uint32_t unported = 0;
  if (monster_c440_collide(scratch, rom, in->d, in->a, &r, &unported)) return true;
  cosim_census_note("c440 id", unported);
  return false;
}

static void shim_monster_c440_collide(Wram* w, const Rom* rom,
                                      const CosimRegs* in, CosimRegs* out) {
  ActorHandlerRegs r = {.a = in->a, .x = in->x, .y = in->y, .c = in->c};
  MonsterCollideWork work;
  // the guard allowed it
  monster_c440_collide_counted(w, rom, in->d, in->a, &r, NULL, &work);
  handler_regs(&r, out);

  int cycles;
  if (monster_cycles(&work, in->fastrom, (in->d & 0xff) != 0, &cycles))
    cosim_cost(cycles);
}

// ---------------------------------------------------------------------------
// $81:B41C  enemy_b41c_collide — the same routine a third time
// ---------------------------------------------------------------------------

// Carry is not an input here, unlike both twins: this copy's death path awards
// nothing, so there is no `score_add` to pass a caller's carry through. Every
// exit sets it.
static bool guard_enemy_b41c_collide(Wram* scratch, const Rom* rom,
                                     const CosimRegs* in) {
  ActorHandlerRegs r = {.a = in->a, .x = in->x, .y = in->y, .c = in->c};
  uint32_t unported = 0;
  if (enemy_b41c_collide(scratch, rom, in->d, in->a, &r, &unported)) return true;
  cosim_census_note("b41c id", unported);
  return false;
}

static void shim_enemy_b41c_collide(Wram* w, const Rom* rom,
                                    const CosimRegs* in, CosimRegs* out) {
  ActorHandlerRegs r = {.a = in->a, .x = in->x, .y = in->y, .c = in->c};
  enemy_b41c_collide(w, rom, in->d, in->a, &r, NULL);  // the guard allowed it
  handler_regs(&r, out);
}

// ---------------------------------------------------------------------------
// $81:D7F6  enemy_d7f6_collide — the fifth copy, on level 17
// ---------------------------------------------------------------------------

static bool guard_enemy_d7f6_collide(Wram* scratch, const Rom* rom,
                                     const CosimRegs* in) {
  ActorHandlerRegs r = {.a = in->a, .x = in->x, .y = in->y, .c = in->c};
  uint32_t unported = 0;
  if (enemy_d7f6_collide(scratch, rom, in->d, in->a, &r, &unported)) return true;
  cosim_census_note("d7f6 id", unported);
  return false;
}

static void shim_enemy_d7f6_collide(Wram* w, const Rom* rom,
                                    const CosimRegs* in, CosimRegs* out) {
  ActorHandlerRegs r = {.a = in->a, .x = in->x, .y = in->y, .c = in->c};
  enemy_d7f6_collide(w, rom, in->d, in->a, &r, NULL);  // the guard allowed it
  handler_regs(&r, out);
}

// ---------------------------------------------------------------------------
// $81:9B6B  enemy_9b6b_collide — the sixth copy, on level 21
// ---------------------------------------------------------------------------

static bool guard_enemy_9b6b_collide(Wram* scratch, const Rom* rom,
                                     const CosimRegs* in) {
  ActorHandlerRegs r = {.a = in->a, .x = in->x, .y = in->y, .c = in->c};
  uint32_t unported = 0;
  if (enemy_9b6b_collide(scratch, rom, in->d, in->a, &r, &unported)) return true;
  cosim_census_note("9b6b id", unported);
  return false;
}

static void shim_enemy_9b6b_collide(Wram* w, const Rom* rom,
                                    const CosimRegs* in, CosimRegs* out) {
  ActorHandlerRegs r = {.a = in->a, .x = in->x, .y = in->y, .c = in->c};
  enemy_9b6b_collide(w, rom, in->d, in->a, &r, NULL);  // the guard allowed it
  handler_regs(&r, out);
}

// ---------------------------------------------------------------------------
// $81:9063  enemy_9063_collide — the seventh copy, on level 5
// ---------------------------------------------------------------------------

static bool guard_enemy_9063_collide(Wram* scratch, const Rom* rom,
                                     const CosimRegs* in) {
  ActorHandlerRegs r = {.a = in->a, .x = in->x, .y = in->y, .c = in->c};
  uint32_t unported = 0;
  if (enemy_9063_collide(scratch, rom, in->d, in->a, &r, &unported)) return true;
  cosim_census_note("9063 id", unported);
  return false;
}

static void shim_enemy_9063_collide(Wram* w, const Rom* rom,
                                    const CosimRegs* in, CosimRegs* out) {
  ActorHandlerRegs r = {.a = in->a, .x = in->x, .y = in->y, .c = in->c};
  enemy_9063_collide(w, rom, in->d, in->a, &r, NULL);  // the guard allowed it
  handler_regs(&r, out);
}

// ---------------------------------------------------------------------------
// $81:AC92  enemy_ac92_collide — the ninth copy, on level 49
// ---------------------------------------------------------------------------

static bool guard_enemy_ac92_collide(Wram* scratch, const Rom* rom,
                                     const CosimRegs* in) {
  ActorHandlerRegs r = {.a = in->a, .x = in->x, .y = in->y, .c = in->c};
  uint32_t unported = 0;
  if (enemy_ac92_collide(scratch, rom, in->d, in->a, &r, &unported)) return true;
  cosim_census_note("ac92 id", unported);
  return false;
}

static void shim_enemy_ac92_collide(Wram* w, const Rom* rom,
                                    const CosimRegs* in, CosimRegs* out) {
  ActorHandlerRegs r = {.a = in->a, .x = in->x, .y = in->y, .c = in->c};
  enemy_ac92_collide(w, rom, in->d, in->a, &r, NULL);  // the guard allowed it
  handler_regs(&r, out);
}

// ---------------------------------------------------------------------------
// $81:E6E4  enemy_e6e4_collide — the tenth copy, and the one with two owners
// ---------------------------------------------------------------------------
//
// It has no `supported` guard, and that is a claim rather than an omission: both
// ids that leave by `JML` are served (`$81:83C6` is `enemy_bubble_react`,
// `$81:847E` is `enemy_freeze`), so there is no argument this routine can be
// handed that it declines. `actor_845e_collide` is the other entry with none,
// for the opposite reason — it cannot write, so there is nothing to try on a
// scratch copy. This one can write plenty; it just never gives up.

static void shim_enemy_e6e4_collide(Wram* w, const Rom* rom,
                                    const CosimRegs* in, CosimRegs* out) {
  ActorHandlerRegs r = {.a = in->a, .x = in->x, .y = in->y, .c = in->c};
  enemy_e6e4_collide(w, rom, in->d, in->a, &r, NULL);
  handler_regs(&r, out);
}

// ---------------------------------------------------------------------------
// $81:845E  actor_845e_collide — no WRAM at all, so no `w` and no guard body
// ---------------------------------------------------------------------------

// It cannot decline and it cannot write, so `supported` is left NULL: there is
// nothing to try on a scratch copy. It is the first entry in the registry with
// that shape, and the reason is the routine's, not the harness's.
static void shim_actor_845e_collide(Wram* w, const Rom* rom,
                                    const CosimRegs* in, CosimRegs* out) {
  (void)w;
  (void)rom;
  ActorHandlerRegs r = {.a = in->a, .x = in->x, .y = in->y, .c = in->c};
  actor_845e_collide(in->a, &r);
  handler_regs(&r, out);
}

// ---------------------------------------------------------------------------
// $81:EDAA  shot_edaa_collide — one byte
// ---------------------------------------------------------------------------

// `out` arrives as a copy of `in`, so "nothing changed" needs no assignment at
// all — only the claim. All four flags, because an `RTL` sets none of them and
// the point of the entry is that this is checkable: the one routine in the
// registry whose entire specification is that it does nothing.
static void shim_shot_edaa_collide(Wram* w, const Rom* rom, const CosimRegs* in,
                                   CosimRegs* out) {
  (void)w;
  (void)rom;
  (void)in;
  shot_edaa_collide();
  out->flags =
      COSIM_FLAG_N | COSIM_FLAG_Z | COSIM_FLAG_C | COSIM_FLAG_V;
}

// ---------------------------------------------------------------------------
// $81:F6A3  shot_f6a3_collide — one shot handler, four weapons
// ---------------------------------------------------------------------------

// It cannot decline — three named ids and an else — so there is nothing for a
// guard to try, and like `actor_845e` it leaves `supported` NULL. Unlike
// `actor_845e` it does write, so it gets `w` and the whole-WRAM diff covers the
// one store.
static void shim_shot_f6a3_collide(Wram* w, const Rom* rom, const CosimRegs* in,
                                   CosimRegs* out) {
  (void)rom;
  ActorHandlerRegs r = {.a = in->a, .x = in->x, .y = in->y, .c = in->c};
  shot_f6a3_collide(w, in->d, in->a, &r);
  handler_regs(&r, out);
}

// ---------------------------------------------------------------------------
// $82:F4EF  actor_f4ef_collide — the two players, and nothing else
// ---------------------------------------------------------------------------

static void shim_actor_f4ef_collide(Wram* w, const Rom* rom, const CosimRegs* in,
                                    CosimRegs* out) {
  (void)rom;
  ActorHandlerRegs r = {.a = in->a, .x = in->x, .y = in->y, .c = in->c};
  actor_f4ef_collide(w, in->d, in->a, &r);
  handler_regs(&r, out);
}

// ---------------------------------------------------------------------------
// $81:D301  enemy_d301_collide — the eighth copy, on level 9
// ---------------------------------------------------------------------------

static bool guard_enemy_d301_collide(Wram* scratch, const Rom* rom,
                                     const CosimRegs* in) {
  ActorHandlerRegs r = {.a = in->a, .x = in->x, .y = in->y, .c = in->c};
  uint32_t unported = 0;
  if (enemy_d301_collide(scratch, rom, in->d, in->a, &r, &unported)) return true;
  cosim_census_note("d301 id", unported);
  return false;
}

static void shim_enemy_d301_collide(Wram* w, const Rom* rom,
                                    const CosimRegs* in, CosimRegs* out) {
  // The seeded carry is load-bearing here in a way it is not for the rest of the
  // family: a survivor's tail hands whatever `enemy_survived_react` returned to
  // `rng_next`, whose `ROL` shifts it into the state byte. Get it wrong and the
  // creature draws a different number.
  ActorHandlerRegs r = {.a = in->a, .x = in->x, .y = in->y, .c = in->c};
  enemy_d301_collide(w, rom, in->d, in->a, &r, NULL);  // the guard allowed it
  handler_regs(&r, out);
}

// ---------------------------------------------------------------------------
// $81:847E  enemy_freeze — reached by JML, so its RTL is its caller's caller's
// ---------------------------------------------------------------------------

// Registered on its own entry PC as well as being called from six handlers, for
// `player_collide`'s reason: a routine seen only through a caller that declines
// is never offered the calls that go somewhere else. Here it is the other way
// round — every one of the six is ported — but the entry PC is also the only
// place the *flags* it leaves can be checked against the ROM's at the exact
// instruction the ROM leaves them.
//
// `in->y` is a genuine input: the raw collision id is still in Y from
// `$80:84A3  TYA`, and bit 15 of it is which player's tally this counts.
static void shim_enemy_freeze(Wram* w, const Rom* rom, const CosimRegs* in,
                              CosimRegs* out) {
  (void)rom;
  ActorHandlerRegs r = {.a = in->a, .x = in->x, .y = in->y, .c = in->c};
  enemy_freeze(w, in->d, &r);
  handler_regs(&r, out);
}

// ---------------------------------------------------------------------------
// $81:83C6  enemy_bubble_react — the `$5D` twin's twin, reached the same way
// ---------------------------------------------------------------------------

// Registered on its own entry PC for `enemy_freeze`'s reason, and `in->y` is
// *not* an input here: this routine never reads Y, having no side to credit.
static void shim_enemy_bubble_react(Wram* w, const Rom* rom,
                                    const CosimRegs* in, CosimRegs* out) {
  (void)rom;
  ActorHandlerRegs r = {.a = in->a, .x = in->x, .y = in->y, .c = in->c};
  enemy_bubble_react(w, in->d, &r);
  handler_regs(&r, out);
}

// ---------------------------------------------------------------------------
// $80:9D39  rng_next — no arguments, and one of them is the caller's carry
// ---------------------------------------------------------------------------

static void shim_rng_next(Wram* w, const Rom* rom, const CosimRegs* in,
                          CosimRegs* out) {
  (void)rom;
  RngResult rng;
  rng_next(w, in->c, &rng);
  out->a = rng.a;
  // X and Y are never mentioned between the entry and the `RTL`.
  out->x = in->x;
  out->y = in->y;
  out->n = rng.n;
  out->z = rng.z;
  out->c = rng.c;
  out->v = rng.v;
  // The only shim in the registry that claims V, and the only one that needs to.
  out->flags = COSIM_FLAG_N | COSIM_FLAG_Z | COSIM_FLAG_C | COSIM_FLAG_V;
}

// ---------------------------------------------------------------------------
// $82:DEEB and $82:F1C2 — the two smallest handlers in the game
// ---------------------------------------------------------------------------

static void shim_actor_deeb_collide(Wram* w, const Rom* rom,
                                    const CosimRegs* in, CosimRegs* out) {
  (void)rom;
  ActorHandlerRegs r = {.a = in->a, .x = in->x, .y = in->y, .c = in->c};
  actor_deeb_collide(w, in->d, in->a, &r);
  handler_regs(&r, out);
}

// This routine contains no `CLC` and no `SEC`, which the first version of the
// port read as "carry passes through" — and `verify` failed it on call 2 with
// `flag C: ROM 1, port 0`. `CMP` sets carry; every exit here has run one. The
// seed stays because the shim's job is to hand the port what the ROM was called
// with, not because anything depends on it now.
static void shim_actor_f1c2_collide(Wram* w, const Rom* rom,
                                    const CosimRegs* in, CosimRegs* out) {
  (void)rom;
  ActorHandlerRegs r = {.a = in->a, .x = in->x, .y = in->y, .c = in->c};
  actor_f1c2_collide(w, in->d, in->a, &r);
  handler_regs(&r, out);
}

// ---------------------------------------------------------------------------
// $81:F534  actor_f534_collide, and $83:A264  victim_a264_collide
// ---------------------------------------------------------------------------
//
// Neither declares a guard: between them they answer every id they are given
// and the only routine either calls (`$81:8191`) is inlined into the port.

static void shim_actor_f534_collide(Wram* w, const Rom* rom,
                                    const CosimRegs* in, CosimRegs* out) {
  (void)rom;
  ActorHandlerRegs r = {.a = in->a, .x = in->x, .y = in->y, .c = in->c};
  actor_f534_collide(w, in->d, in->a, &r);
  handler_regs(&r, out);
}

static void shim_victim_a264_collide(Wram* w, const Rom* rom,
                                     const CosimRegs* in, CosimRegs* out) {
  (void)rom;
  ActorHandlerRegs r = {.a = in->a, .x = in->x, .y = in->y, .c = in->c};
  victim_a264_collide(w, in->d, in->a, &r);
  handler_regs(&r, out);
}

// ---------------------------------------------------------------------------
// $81:CDDE  enemy_cdde_collide — the first handler with no guard
// ---------------------------------------------------------------------------

// No `supported` hook, and that is the entry worth noticing rather than an
// omission. Every other collision handler in the registry has ids it hands back
// — a jump-table entry nobody has written, a `JML` into the `$81:8506` family —
// and declares a guard to say so honestly. This one answers all three of its ids
// itself and the only routine it calls (`$81:CC0A`) is ported with it, so there
// is no condition under which it steps aside and nothing for a guard to report.
static void shim_enemy_cdde_collide(Wram* w, const Rom* rom,
                                    const CosimRegs* in, CosimRegs* out) {
  (void)rom;  // no table lookup: this one's damage is a decrement
  ActorHandlerRegs r = {.a = in->a, .x = in->x, .y = in->y, .c = in->c};
  enemy_cdde_collide(w, in->d, in->a, &r);
  handler_regs(&r, out);
}

// ---------------------------------------------------------------------------
// $81:B592  enemy_b592_collide — no guard either, and even less to guard
// ---------------------------------------------------------------------------

static void shim_enemy_b592_collide(Wram* w, const Rom* rom,
                                    const CosimRegs* in, CosimRegs* out) {
  (void)rom;
  ActorHandlerRegs r = {.a = in->a, .x = in->x, .y = in->y, .c = in->c};
  enemy_b592_collide(w, in->d, in->a, &r);
  handler_regs(&r, out);
}

// ---------------------------------------------------------------------------
// $82:9660  boss_9660_collide — no guard either, and the first in bank $82
// ---------------------------------------------------------------------------

// `rom` is back, because unlike the two above this one does index
// `ENEMY_DAMAGE_TABLE`. `in->d` matters more here than anywhere else in the
// registry: this handler reads *both* its thread's page and two absolute
// globals, and getting the two confused is the one way to write it wrong.
static void shim_boss_9660_collide(Wram* w, const Rom* rom, const CosimRegs* in,
                                   CosimRegs* out) {
  ActorHandlerRegs r = {.a = in->a, .x = in->x, .y = in->y, .c = in->c};
  boss_9660_collide(w, rom, in->d, in->a, &r);
  handler_regs(&r, out);
}

// ---------------------------------------------------------------------------
// $81:FE0E  shot_collide — the same argument again, on a weapon shot's page
// ---------------------------------------------------------------------------

// Carry is an input on exactly one of its three paths: id 0 reaches the `RTL`
// without executing a single `CMP`, so what leaves in carry is what arrived.
// The other two paths set it. No guard — the port has all of this routine, so
// there is no condition under which it could decline.

// What each exit of `$81:FE0E` costs, indexed by `ShotCollideBlock`.
//
// **The first table in this file with a third column.** A handler runs on the
// page `$80:84A2  TCD` installed, not on page zero, and half of the twenty-four
// pages in `$80:82DE` have a non-zero low byte — so the two `STA`s in the tail
// cost an idle more on half the actors in the game. Every routine priced before
// this one ran on page zero and could leave the column implicitly `0`.
//
// The four stop prefixes are cumulative walks down the `CMP` chain: 12 for the
// `TAY`, then 12 and 18 for each comparison that misses, and 18 for the branch
// that finally hits.
static const CosimRun SHOT_COST[SHOT_BLOCK_COUNT] = {
    [SHOT_BLK_STOP_A] = {12 + 18, 3},
    [SHOT_BLK_STOP_B] = {12 + 12 + 18 + 18, 8},
    [SHOT_BLK_STOP_C] = {12 + 12 + 18 + 12 + 18 + 18, 13},
    [SHOT_BLK_STOP_D] = {12 + 12 + 18 + 12 + 18 + 12 + 18 + 18, 18},
    // $81:FE21 LDY $0A : LDA #$0000 : STA $000E,Y : LDA #$0001 : STA $42 : RTL.
    // `LDY $0A` and `STA $42` are the two direct-page instructions; the store in
    // between goes through the record's absolute address and does not care.
    [SHOT_BLK_TAIL] = {174, 14, 2},
    // Falling through all four tests to `$81:FE20 RTL` — and 156 is exactly the
    // minimum `verify` measures for this routine.
    [SHOT_BLK_FLY] = {156, 19},
};

static int shot_cycles(const ShotCollideWork* k, bool fast, bool dp_unaligned) {
  int cycles = 0;
  for (int i = 0; i < SHOT_BLOCK_COUNT; i++)
    cycles += k->blocks[i] * cosim_run_cycles_dp(&SHOT_COST[i], fast,
                                                 dp_unaligned);
  return cycles;
}

static void shim_shot_collide(Wram* w, const Rom* rom, const CosimRegs* in,
                              CosimRegs* out) {
  (void)rom;  // no table, no ROM read
  ActorHandlerRegs r = {.a = in->a, .x = in->x, .y = in->y, .c = in->c};
  ShotCollideWork work;
  shot_collide_counted(w, in->d, in->a, &r, &work);
  handler_regs(&r, out);
  cosim_cost(shot_cycles(&work, in->fastrom, (in->d & 0xff) != 0));
}

// ---------------------------------------------------------------------------
// $82:9660  boss_9660_collide, priced
// ---------------------------------------------------------------------------
//
// This one has no shim of its own — the handler is not in the registry and is
// only ever reached through `$80:8480` — so unlike the four models above there
// is no `verify` minimum to check it against. What stands in for that witness is
// the same thing that found the last two tool bugs: the dispatcher's own error
// has to stay a non-negative multiple of 40 on the 9,802 calls of `level25-2p`
// that arrive here, and a table that is wrong anywhere makes it negative
// somewhere.
//
// It is also the first handler priced whose costs are not all direct-page.
// `LDY $0078`, `LDX $000E,Y` and `LDA $0020` are absolute reads of low WRAM
// through the data bank; `$3A` to `$44` are on the thread's own page. The two
// columns are what tells them apart, and `$82:948F` writing absolute `$003C` as
// a *coordinate* in the routine that seeds direct `$3C` to 70 is what makes
// getting it wrong plausible.
static const CosimRun BOSS_COST[BOSS_BLOCK_COUNT] = {
    // $82:9660..$9668, the `BEQ` taking it to `$9674  STZ $42 : CLC : RTL`.
    [BOSS_BLK_INVULN] = {92 + 18 + 82, 15, 1},
    // ...falling through, then `LDX $40` and its `BNE` to the same three.
    [BOSS_BLK_FLASHING] = {92 + 12 + 28 + 18 + 82, 19, 2},
    // ...and the `CMP #$005C` under it not carrying, which lands there again.
    [BOSS_BLK_IGNORE] = {92 + 12 + 28 + 12 + 18 + 12 + 82, 24, 2},
    // ...or carrying, so `$9678  STA $42 : AND #$7FFF` and into the chain.
    [BOSS_BLK_HIT] = {92 + 12 + 28 + 12 + 18 + 18 + 46, 25, 2},
    // $967D `CMP #$0062` matching, the tick read, and one of the two answers.
    [BOSS_BLK_REMAP_62_CHEAP] = {18 + 12 + 52 + 12 + 18 + 18, 18},
    [BOSS_BLK_REMAP_62_DEAR] = {18 + 12 + 52 + 18 + 18 + 18, 18},
    // ...or not, and `$968F  CMP #$0070` matching one comparison later.
    [BOSS_BLK_REMAP_70_CHEAP] = {18 + 18 + 18 + 12 + 52 + 12 + 18 + 18, 23},
    [BOSS_BLK_REMAP_70_DEAR] = {18 + 18 + 18 + 12 + 52 + 18 + 18 + 18, 23},
    // Two comparisons in, and no tick: these two are not a coin toss.
    [BOSS_BLK_REMAP_61] = {18 + 18 + 18 + 18 + 18 + 12 + 18 + 18, 20},
    [BOSS_BLK_REMAP_6F] = {18 + 18 + 18 + 18 + 18 + 18 + 18 + 12 + 18 + 18, 25},
    // All four `CMP`s and all four branches taken, and nothing rewritten.
    [BOSS_BLK_REMAP_NONE] = {18 + 18 + 18 + 18 + 18 + 18 + 18 + 18, 20},
    // $96BA through the `SBC $818561,X` — whose two-byte read of a ROM table is
    // why the byte column is 19 for seventeen bytes of instruction — then the
    // `BMI` taking it to `$96D5  DEC $3A` and the shared `SEC : RTL`.
    [BOSS_BLK_DIED] = {230 + 18 + 50 + 54, 25, 4},
    // ...not taken, and `CMP $3C : BEQ` finding the subtraction took nothing.
    [BOSS_BLK_NO_DAMAGE] = {230 + 12 + 28 + 18 + 54, 27, 4},
    // ...or finding it took something, so `$96D1  STA $3C` first.
    [BOSS_BLK_SURVIVED] = {230 + 12 + 28 + 12 + 28 + 18 + 54, 31, 5},
};

// No guard. Every exit of `$82:9660` is inside `$82:9660` — there is no `JML`
// out of it and nothing under it that is not ported — so unlike `enemy_cycles`
// this one cannot fail, and the dispatcher's `switch` treats it like
// `shot_cycles` rather than like the two that can decline.
static int boss_cycles(const BossCollideWork* k, bool fast, bool dp_unaligned) {
  int cycles = 0;
  for (int i = 0; i < BOSS_BLOCK_COUNT; i++)
    cycles +=
        k->blocks[i] * cosim_run_cycles_dp(&BOSS_COST[i], fast, dp_unaligned);
  return cycles;
}

// ---------------------------------------------------------------------------
// $80:8480  thread_call_handler, priced
// ---------------------------------------------------------------------------

// The frame the dispatcher builds around a handler. Four blocks and no table:
// its control flow is two branches, and everything it touches is either the
// stack, low WRAM through the data bank, or the page table in bank $80 ROM.
//
// None of its own instructions is direct-page — it is running on the *caller's*
// page and deliberately touches nothing on it — so the third column is 0 here
// and non-zero only in the handler tables it sums.
static const CosimRun THREAD_CALL_NONE = {40 + 40 + 18 + 42, 9};
// $80:8480..$80:84A4, the `BEQ` falling through and the `RTL` that enters the
// handler. The two `PHA`s at `$80:848F` and `$80:8496` run under `SEP #$20` and
// push one byte each, and `PEA` pushes its operand rather than reading it —
// between them that is the 6 cycles this constant was first written 510 for.
static const CosimRun THREAD_CALL_ENTER = {504, 39};
// $80:84A5 PLX : BCC taken : PLD : PLB : RTL.
static const CosimRun THREAD_CALL_RESUME = {34 + 18 + 34 + 26 + 42, 6};
// ...and not taken, so `LDA #$8000 : STA $1180,X` parks the thread first.
static const CosimRun THREAD_CALL_PARK = {34 + 12 + 18 + 40 + 34 + 26 + 42, 12};

static bool thread_call_cycles(const ThreadCallWork* k, const CosimRegs* in,
                               int* out) {
  bool fast = in->fastrom;
  int cycles = k->blocks[THREAD_CALL_BLK_NONE] *
                   cosim_run_cycles(&THREAD_CALL_NONE, fast) +
               k->blocks[THREAD_CALL_BLK_ENTER] *
                   cosim_run_cycles(&THREAD_CALL_ENTER, fast) +
               k->blocks[THREAD_CALL_BLK_RESUME] *
                   cosim_run_cycles(&THREAD_CALL_RESUME, fast) +
               k->blocks[THREAD_CALL_BLK_PARK] *
                   cosim_run_cycles(&THREAD_CALL_PARK, fast);

  // No handler ran, so the frame is the whole routine — and this is the path
  // that costs 140, which is what `verify` measures as the minimum.
  if (k->entry == 0) {
    *out = cycles;
    return true;
  }

  // `$80:84A2  TCD` installed the thread's own page a few instructions ago, and
  // whether its low byte is zero is what the handler's direct-page instructions
  // cost an extra idle on.
  bool dp_unaligned = (k->dp & 0xff) != 0;
  switch (k->entry) {
    case SHOT_COLLIDE_ENTRY:
      cycles += shot_cycles(&k->shot, fast, dp_unaligned);
      break;
    case BOSS_9660_COLLIDE_ENTRY:
      cycles += boss_cycles(&k->boss, fast, dp_unaligned);
      break;
    case PLAYER_COLLIDE_ENTRY: {
      int handler;
      if (!player_cycles(&k->player, fast, dp_unaligned, &handler)) return false;
      cycles += handler;
      break;
    }
    case ENEMY_COLLIDE_ENTRY: {
      int handler;
      if (!enemy_cycles(&k->enemy, fast, dp_unaligned, &handler)) return false;
      cycles += handler;
      break;
    }
    // The two copies share a table because they share a body. `$81:C440` is
    // 96% of what `sprite_build_oam` used to decline on and `$81:C4A6` was on
    // the same work list; pricing one without the other would have meant
    // writing the same six constants twice.
    case MONSTER_COLLIDE_ENTRY:
    case MONSTER_C440_COLLIDE_ENTRY: {
      int handler;
      if (!monster_cycles(&k->monster, fast, dp_unaligned, &handler))
        return false;
      cycles += handler;
      break;
    }
    default:
      // A handler with no table. Not an error and not a gap in the port — the
      // port has twenty-five of these and this file prices four of them.
      //
      // Which addresses land here is the work list for the next round, and the
      // way to read it is a run with a `printf` on this line: `$82:9660` was
      // first by a distance and is now above, so what is left is `$81:C4A6`,
      // `$81:C440`, `$80:CAEE` and `$81:8888`'s three deep exits. It is not
      // wired into `cosim_census_note` — this function is called again for
      // every level of the four models stacked on top of it, so a census here
      // would count one dispatch up to four times and read like a frequency
      // when it is not one.
      return false;
  }
  *out = cycles;
  return true;
}

// ---------------------------------------------------------------------------
// $83:A364  victim_collide — the same argument again, on a victim's page
// ---------------------------------------------------------------------------

// No guard, for the same reason `shot_collide` has none: every exit is ported,
// so there is no condition it could decline on. Carry is an output on all nine
// paths and an input on none — eight `SEC`s and a `CLC`, and the entry guard's
// `BNE` reaches one of them without reading it.
static void shim_victim_collide(Wram* w, const Rom* rom, const CosimRegs* in,
                                CosimRegs* out) {
  (void)rom;  // no table, no ROM read
  ActorHandlerRegs r = {.a = in->a, .x = in->x, .y = in->y};
  victim_collide(w, in->d, in->a, &r);
  handler_regs(&r, out);
}

// ---------------------------------------------------------------------------
// $80:CAEE  object_collide — the same argument, on the object manager's page
// ---------------------------------------------------------------------------

// No guard, for the same reason `shot_collide` and `victim_collide` have none.
// Carry is an output on all three paths and an input on none: two `CLC`s and a
// `SEC`, and nothing reads it on the way to any of them.
static void shim_object_collide(Wram* w, const Rom* rom, const CosimRegs* in,
                                CosimRegs* out) {
  (void)rom;  // no table, no ROM read
  ActorHandlerRegs r = {.a = in->a, .x = in->x, .y = in->y};
  object_collide(w, in->d, in->a, &r);
  handler_regs(&r, out);
}

// ---------------------------------------------------------------------------
// $80:EA63  weapon_select_next — no arguments but the player's direct page
// ---------------------------------------------------------------------------

// Registered in its own right for the reason `score_add` is: `player_pickup`
// reaches it, and so does `$80:D267`, the player's input handler noticing that
// B was pressed. Those two call sites have nothing to do with each other, and
// the second one is by far the more common — a pickup is rare and pressing B
// is not.
//
// It is reached two different ways, too. `$80:D267` is a `JSR`; `$80:F8A8` is a
// `JMP`, so on that path the return address on the stack is `player_collide`'s
// and the frame is still `COSIM_RTS`-shaped. The engine reads the frame rather
// than assuming, so both work.
static void shim_weapon_select_next(Wram* w, const Rom* rom, const CosimRegs* in,
                                    CosimRegs* out) {
  WeaponSelectRegs r;
  weapon_select_next(w, rom, in->d, &r);
  out->a = r.a;
  out->x = r.x;
  out->y = r.y;
  out->n = r.n;
  out->z = r.z;
  out->c = r.c;
  out->flags = COSIM_FLAG_N | COSIM_FLAG_Z | COSIM_FLAG_C;
}

// ---------------------------------------------------------------------------
// $80:EAA8  item_select_next — the same, one array over
// ---------------------------------------------------------------------------

// Registered separately from `weapon_select_next` even though the two are the
// same twenty-two instructions, because they are two routines in the ROM at two
// addresses with four callers between them and no way to tell from a call site
// which one is meant. `$80:F903` is the `JMP` under `item_pickup`; `$80:D278` is
// the input handler noticing **A**, which is to items what B is to weapons.
static void shim_item_select_next(Wram* w, const Rom* rom, const CosimRegs* in,
                                  CosimRegs* out) {
  WeaponSelectRegs r;
  item_select_next(w, rom, in->d, &r);
  out->a = r.a;
  out->x = r.x;
  out->y = r.y;
  out->n = r.n;
  out->z = r.z;
  out->c = r.c;
  out->flags = COSIM_FLAG_N | COSIM_FLAG_Z | COSIM_FLAG_C;
}

// ---------------------------------------------------------------------------
// $80:C7D9  score_add — X = the BCD award, A's sign = the side
// ---------------------------------------------------------------------------

// Registered in its own right for the reason `player_collide` is: the collision
// path reaches it, but so does the victim-rescue thread at `$83:A1EC`, and those
// two call sites have nothing to do with each other. Intercepted here it is
// checked on both — and on the second one even in `run` mode, where
// `enemy_collide` is substituted whole and the ROM never reaches the first.
//
// The `BMI` at the entry means this is the second routine whose *input* includes
// a flag. `player_collide` needed `d`; this one needs `n`, which `CosimRegs`
// already carries because the diff compares it on the way out.
static bool guard_score_add(Wram* scratch, const Rom* rom, const CosimRegs* in) {
  ScoreResult out;
  return score_add(scratch, rom, in->n, in->x, in->c, &out);
}

static void shim_score_add(Wram* w, const Rom* rom, const CosimRegs* in,
                           CosimRegs* out) {
  ScoreResult r;
  score_add(w, rom, in->n, in->x, in->c, &r);  // the guard allowed it
  out->a = r.a;
  out->x = r.x;
  // Y is never mentioned between the entry and any of the three `RTL`s.
  out->y = in->y;
  out->n = r.n;
  out->z = r.z;
  out->c = r.c;
  out->flags = COSIM_FLAG_N | COSIM_FLAG_Z | COSIM_FLAG_C;
}

// ---------------------------------------------------------------------------
// $80:BE8F  actor_collide_notify — X = the pair's second record, and it may
//                                  decline
// ---------------------------------------------------------------------------

// Registered in its own right even though `actor_overlap_pass` already calls
// it, and that is the point: the pass declines every call containing a
// collision the port cannot dispatch, so without this entry the plumbing would
// only ever be checked on the passes where nothing happened. Intercepted here,
// it is checked on **every** collision the movie produces, including the ones
// that go on to enter a handler and end the enclosing pass.
//
// The two arguments are where the ROM left them at `$80:BF0E  JSR $BE8F`: the
// inner record in X, and the outer one reachable through the walk cursor the
// pass parked in `$3C`. `LDX $1380,Y` is `visible_actors + 2 + $3C`, two past
// the base because the cursor has already been stepped back.
static uint16_t notify_outer(const Wram* w) {
  return wram_r16(w, W_VISIBLE_ACTORS + 2 + wram_r16(w, W_OVERLAP_CURSOR));
}

static bool guard_actor_collide_notify(Wram* scratch, const Rom* rom,
                                       const CosimRegs* in) {
  ThreadCallResult tail = {.c = in->c};
  return actor_collide_notify(scratch, rom, notify_outer(scratch), in->x, &tail);
}

// $80:BE8F..$80:BEC8 in full, both `JSL`s included and neither dispatch. The
// routine has no branches — twenty-six instructions of straight line — so this
// is one constant rather than a table, and the only thing that varies call to
// call is what the two dispatches cost.
//
// 856 + 2 x 140, the two slots having no handler registered, is 1,136, which is
// exactly the minimum `verify` measures for this routine.
static const CosimRun NOTIFY_COST = {856, 58, 23};

static bool notify_cycles(const CollideNotifyWork* k, const CosimRegs* in,
                          int* out) {
  int cycles =
      cosim_run_cycles_dp(&NOTIFY_COST, in->fastrom, (in->d & 0xff) != 0);
  for (int i = 0; i < 2; i++) {
    int call;
    if (!thread_call_cycles(&k->call[i], in, &call)) return false;
    cycles += call;
  }
  *out = cycles;
  return true;
}

static void shim_actor_collide_notify(Wram* w, const Rom* rom,
                                      const CosimRegs* in, CosimRegs* out) {
  uint16_t a = notify_outer(w);
  ThreadCallResult tail = {.c = in->c};
  CollideNotifyWork work;
  // the guard allowed it
  actor_collide_notify_counted(w, rom, a, in->x, &tail, &work);

  // The routine's last instruction is the second `JSL $80:8480`, so everything
  // it returns is really the dispatcher's — including X and Y, which are the
  // arguments `$80:BEC0`/`$80:BEC2` set up and which the dispatcher hands back
  // untouched. So this defers to `handler_exit` rather than restating it, which
  // keeps one description of that tail rather than two.
  handler_exit(in, &tail, out);

  int cycles;
  if ((in->db < 0x40 || (in->db >= 0x80 && in->db < 0xc0)) &&
      notify_cycles(&work, in, &cycles))
    cosim_cost(cycles);
}

// ---------------------------------------------------------------------------
// $80:BEC9  actor_overlap_pass — no arguments, and it may decline
// ---------------------------------------------------------------------------

// The first routine whose port covers only part of what the ROM's version does,
// so it is the first to need a guard — see `CosimGuard` in `cosim.h`.
//
// The guard is the whole routine, run on a throwaway copy of WRAM. That is not
// a shortcut: "can the port handle this call?" and "what does the port do with
// this call?" are the same question here, because the condition it declines on
// is one only the walk can find. Asking it any other way would mean writing the
// pairwise test a second time in the harness, where it could drift.
//
// The condition has narrowed twice since it was written. A hit no longer ends
// the pass by itself: `actor_collide_notify` serves the dispatch, and
// `thread_call_handler` serves the two handlers a collision in ordinary play
// reaches. What is left to decline is a collision that enters a *third*
// handler, or one of the two on a branch that leaves through unported code.
static bool guard_actor_overlap_pass(Wram* scratch, const Rom* rom,
                                     const CosimRegs* in) {
  (void)in;
  return actor_overlap_pass(scratch, rom);
}

// What each run of `$80:BEC9` costs, indexed by `ActorOverlapBlock`. Same rules
// as the two tables above; `visible_actors`, the records and the four scratch
// words are all under `$7E:2000` and cost 8 a byte through every bank.
//
// The empty-list case is again the check: `LDY $9C : BEQ` taken plus `RTL` is
// 28 + 18 + 42 = 88, which is exactly the minimum `verify` measures.
//
// There is no entry for the hit. `$80:BF0D PHY : JSR $BE8F : PLY` would be 102
// cycles of its own, but the `JSR` enters a dispatch into two actor handlers and
// what *they* cost is a tree this table does not describe, so a pass with a hit
// in it is not priced at all rather than priced short. See `ActorOverlapWork`.
static const CosimRun OVL_COST[OVL_BLOCK_COUNT] = {
    [OVL_BLK_EMPTY] = {28 + 18 + 42, 5},
    [OVL_BLK_SINGLE] = {28 + 12 + 24 + 18 + 42, 9},
    [OVL_BLK_PROLOGUE] = {28 + 12 + 24 + 12, 8},
    // $80:BED1 LDX $137E,Y : DEY : DEY : STY $3C : LDA $0E,X — 126 over 9 bytes
    // — and the `BEQ` that decides whether the record has an id at all.
    [OVL_BLK_OUTER_NOID] = {126 + 18, 11},
    // ...and the four instructions that publish it: 126 + 12 + 152.
    [OVL_BLK_OUTER_ID] = {126 + 12 + 152, 21},
    // $80:BEE6 LDX $137E,Y : LDA $0E,X — 74 over 5 bytes — plus its `BEQ`.
    [OVL_BLK_INNER_NOID] = {74 + 18, 7},
    [OVL_BLK_INNER_ID] = {74 + 12, 7},
    [OVL_BLK_SAME_ID] = {28 + 18, 4},
    [OVL_BLK_DIFF_ID] = {28 + 12, 4},
    // LDA $02,X : SEC : SBC $38 : CLC : ADC #$0008 : CMP #$0010 — 122 over 12
    // bytes — plus the `BCS`. The Y axis is the same six off `$06` and `$3A`.
    [OVL_BLK_X_FAR] = {122 + 18, 14},
    [OVL_BLK_X_NEAR] = {122 + 12, 14},
    [OVL_BLK_Y_FAR] = {122 + 18, 14},
    [OVL_BLK_Y_NEAR] = {122 + 12, 14},
    [OVL_BLK_INNER_NEXT] = {24 + 18, 4},
    [OVL_BLK_INNER_DONE] = {24 + 12, 4},
    [OVL_BLK_OUTER_NEXT] = {28 + 18, 4},
    [OVL_BLK_OUTER_DONE] = {28 + 12 + 42, 5},
};

// `$80:BF0D  PHY : JSR $BE8F : PLY`, the three instructions a hit costs on top
// of the two axis tests — not counting the dispatch itself, which is
// `notify_cycles`.
static const CosimRun OVL_HIT = {28 + 40 + 34, 5};

// False when a hit in this pass entered a handler with no cost table, or when
// there were more hits than `ActorOverlapWork` can describe. The blocks are
// summed either way: what is missing is never a *part* of the answer, it is the
// answer, so there is no version of this that reports a number it half knows.
static bool overlap_cycles(const ActorOverlapWork* k, const CosimRegs* in,
                           int* out) {
  bool fast = in->fastrom;
  int cycles = 0;
  for (int i = 0; i < OVL_BLOCK_COUNT; i++)
    cycles += k->blocks[i] * cosim_run_cycles(&OVL_COST[i], fast);

  if (k->hits > OVL_MAX_PRICED_HITS) return false;
  for (int i = 0; i < k->hits; i++) {
    int notify;
    if (!notify_cycles(&k->notify[i], in, &notify)) return false;
    cycles += cosim_run_cycles(&OVL_HIT, fast) + notify;
  }
  *out = cycles;
  return true;
}

static void shim_actor_overlap_pass(Wram* w, const Rom* rom, const CosimRegs* in,
                                    CosimRegs* out) {
  // The guard already established it will not decline.
  ActorOverlapWork work;
  actor_overlap_pass_counted(w, rom, &work);

  // Priced only off a page-aligned direct page — `$9C`, `$3C`, `$4A`, `$38`,
  // `$3A` and the four `$xx,X` reads would each cost one more internal cycle
  // otherwise. A hit no longer disqualifies the pass by itself; what does is a
  // hit whose handler has no table, and `overlap_cycles` is what knows that.
  int cycles;
  if ((in->d & 0xff) == 0 && overlap_cycles(&work, in, &cycles))
    cosim_cost(cycles);

  // All three `RTL` paths arrive with Y zero and the flags of whatever loaded
  // it: `LDY $9C` on an empty list, `DEY DEY` on a single record, and `LDY $3C`
  // at the end of the walk, which is what the loop exits on.
  out->y = 0;
  out->n = false;
  out->z = true;
  out->flags = COSIM_FLAG_N | COSIM_FLAG_Z;
  out->regs = COSIM_REG_Y;

  // A, X and carry are not claimed, for the same reason `actor_depth_sort`
  // does not claim them: each is an intermediate of whichever comparison the
  // walk happened to stop on — the id that read zero, or one of the four box
  // tests — and predicting it here would mean writing the walk twice. They are
  // dead. The only caller is `$80:BDCC`, whose next instructions are `PLD :
  // PLB : LDA $20 ... TAX` and then a `SEC`, so all three are overwritten
  // before anything reads them.
}

// ---------------------------------------------------------------------------
// $80:BD1F  sprite_build_oam — one argument, the caller's page, and it may decline
// ---------------------------------------------------------------------------

// The one address the pass legitimately leaves alone, and it is the same $38 two
// of the routines it calls already declare. `actor_depth_sort` keeps its walk
// predecessor there and `sprite_frame_tile` spills the emitter's X there; the
// port keeps both in C locals.
//
// It does reproduce the second of the two, because that one is derivable: the
// last frame lookup of a pass spills the OAM index of the last piece drawn,
// which is four bytes back from where the buffer ends. That is worth doing even
// though the exclude means it is not checked here — dropping it moves the first
// divergence from call 192 to call 130 with the exclude off, so it is right far
// more often than not. What is left is the depth sort's spill on passes that
// draw nothing, which would need that routine to model its own walk in WRAM.
//
// The exclude costs less coverage than it looks: `actor_overlap_pass` writes $38
// too and runs after everything else here, so whenever it has a record to test,
// this address is checked exactly — by that routine, registered separately and
// compared on all 1,016 of its own calls.
static const CosimExclude BUILD_OAM_EXCLUDES[] = {
    {0x0038, 2, "scratch: actor_depth_sort's walk predecessor, then the emitter's X"},
};

static bool guard_sprite_build_oam(Wram* scratch, const Rom* rom,
                                   const CosimRegs* in) {
  return sprite_build_oam(scratch, rom, in->d);
}

// ---------------------------------------------------------------------------
// ...and what a pass costs, which is the sum of six of these tables.
//
// `$80:BD1F` is the outermost substituted routine on the sprite path, so it is
// the only one whose model is ever consulted: the sort, the cull, the buffer
// clear, the overlap pass and the frame cache are all reached from inside it
// and every one of them has exactly one caller. Their models have been right
// and unreachable for two rounds. This is where they start being used.
//
// It also settles the guards. Standalone, four of those five have to check the
// caller's direct page before they can price anything, and the cache has to
// check the data bank as well. Reached from here they need neither: `$80:BD21
// PEA $0000 : PLD` and `$80:BD25 PHK : PLB` establish page zero and bank $80
// for the whole pass. Only two instructions run outside that window — the
// `LDA $20` and `LDA $BDE6,X` after `$80:BDD0 PLD : PLB` — and they are the
// only reason anything below asks about the caller at all.
// ---------------------------------------------------------------------------

static CosimRun run_plus(CosimRun a, CosimRun b) {
  CosimRun r = {a.cycles + b.cycles, a.bytes + b.bytes};
  return r;
}

// One emitter, in `SpriteEmitBlock` order, priced as `$80:BA51` — the unflipped
// one. The other three are this plus the deltas below; see `SpriteEmitWork` for
// why that is a fair description of them rather than a convenience.
static const CosimRun EMIT_COST[EMIT_BLOCK_COUNT] = {
    [EMIT_BLK_PROLOGUE]   = {28, 2},
    // $80:BA53..$80:BA60 is 164 over 16, and every path through the piece pays
    // it: `LDY #$0002 : LDA [$8A],Y : CLC : ADC $90 : STA $13BF,X : CMP #$FFF1`.
    // Two of those 16 bytes are the metasprite word itself, read through a long
    // pointer into bank $8F or $90 — fast ROM, and so FastROM-sensitive.
    [EMIT_BLK_Y_HIGH]     = {164 + 18, 18},
    [EMIT_BLK_Y_LOW]      = {164 + 12 + 18 + 12, 23},
    [EMIT_BLK_Y_DROP]     = {164 + 12 + 18 + 18, 23},
    // $80:BA68..$80:BA76, 174 over 17, then the same two-test shape on x.
    [EMIT_BLK_X_NEAR]     = {174 + 18, 19},
    [EMIT_BLK_X_DROP]     = {174 + 12 + 18 + 18, 24},
    // ...and $80:BA7E..$80:BA89, the four instructions that set the sprite's
    // ninth x bit in the high table. Two ROM table reads, hence 16 bytes.
    [EMIT_BLK_X_WRAP]     = {174 + 12 + 18 + 12 + 152, 40},
    // $80:BA8A..$80:BAA8, 390 over 35: the attribute word, the frame number,
    // the `JSR $B9D6` itself — 40, with the lookup's own cost coming from
    // `TILE_COST` — and the four `INX`.
    [EMIT_BLK_PIECE]      = {390 + 12, 37},
    [EMIT_BLK_PIECE_FULL] = {390 + 18, 37},
    // $80:BAAB..$80:BAB4, the pointer advance and `DEC $86`.
    [EMIT_BLK_NEXT]       = {136 + 18, 12},
    [EMIT_BLK_DONE]       = {136 + 12, 12},
    [EMIT_BLK_EXIT]       = {68, 3},
};

// `EOR #$FFFF : SEC : SBC #$000F`, the mirror. In front of the y offset for a
// vertical flip, in front of the x offset for a horizontal one.
static const CosimRun EMIT_MIRROR = {48, 7};
// `EOR #$4000` / `#$8000` / `#$C000` on the finished OAM word — one instruction
// with three operands, present in all three flipped emitters and in none of the
// unflipped one.
static const CosimRun EMIT_FLIP_EOR = {18, 3};
// The loop-back. Three extra bytes of body per piece put `$80:BA53` out of a
// relative branch's reach, so the flipped emitters spend `BEQ : JMP` where
// `$80:BAB5` spends a `BNE` — dearer to go round, dearer to stop.
static const CosimRun EMIT_FAR_NEXT = {12, 3};
static const CosimRun EMIT_FAR_DONE = {6, 0};

static int emit_cycles(const SpriteBuildWork* k, bool fast) {
  int cycles = 0;
  for (int flip = 0; flip < 8; flip += 2) {
    const bool fx = (flip & SPRITE_FLIP_X) != 0;
    const bool fy = (flip & SPRITE_FLIP_Y) != 0;
    for (int i = 0; i < EMIT_BLOCK_COUNT; i++) {
      if (k->emit[flip][i] == 0) continue;
      CosimRun r = EMIT_COST[i];
      if (fy && (i == EMIT_BLK_Y_HIGH || i == EMIT_BLK_Y_LOW ||
                 i == EMIT_BLK_Y_DROP))
        r = run_plus(r, EMIT_MIRROR);
      if (fx && (i == EMIT_BLK_X_NEAR || i == EMIT_BLK_X_DROP ||
                 i == EMIT_BLK_X_WRAP))
        r = run_plus(r, EMIT_MIRROR);
      if (flip != SPRITE_FLIP_NONE) {
        if (i == EMIT_BLK_PIECE || i == EMIT_BLK_PIECE_FULL)
          r = run_plus(r, EMIT_FLIP_EOR);
        if (i == EMIT_BLK_NEXT) r = run_plus(r, EMIT_FAR_NEXT);
        if (i == EMIT_BLK_DONE) r = run_plus(r, EMIT_FAR_DONE);
      }
      cycles += k->emit[flip][i] * cosim_run_cycles(&r, fast);
    }
  }
  return cycles;
}

// The walk itself, `$80:BD1F`..`$80:BDE2`, in `SpriteBuildBlock` order. The
// three `JSR`s and the one `JSL` are in here at 40 and 54; what they call is
// not.
static const CosimRun BUILD_COST[BUILD_BLOCK_COUNT] = {
    // $80:BD1F..$80:BD36 is 378 over 25 with the `BEQ` falling through, so an
    // empty pass is that with the branch taken plus `$80:BD77 BRA $BDCC`.
    [BUILD_BLK_EMPTY]      = {378 + 6 + 18, 27},
    [BUILD_BLK_NONEMPTY]   = {378 + 46, 30},
    // $80:BD3D..$80:BD43 `STY $9A : LDX $137E,Y : LDA $00,X`, then the `BPL`.
    [BUILD_BLK_UNDRAWN]    = {114 + 6, 9},
    [BUILD_BLK_DRAWN]      = {114, 9},
    // Each of the next three pairs ends on the store the two paths share, so
    // one of each pair is counted per drawable record and nothing is left over.
    [BUILD_BLK_PRIO_PLAIN] = {18 + 18 + 18 + 28, 10},
    [BUILD_BLK_PRIO_TOP]   = {18 + 18 + 12 + 18 + 28, 13},
    [BUILD_BLK_ATTR_PLAIN] = {18 + 34 + 18 + 18 + 28, 12},
    [BUILD_BLK_ATTR_SET]   = {190 + 28, 21},
    [BUILD_BLK_SCREEN]     = {34 + 12 + 12 + 124 + 18, 15},
    [BUILD_BLK_WORLD]      = {34 + 12 + 18 + 262, 24},
    // $80:BD8C onwards: three ways to be rejected, at 7, 16 and 21 bytes, and
    // then the count byte. The prefix each one shares is folded in, so exactly
    // one of the five below is counted per drawable record.
    [BUILD_BLK_NO_META]    = {34 + 18 + 18, 7},
    [BUILD_BLK_BANK_LOW]   = {64 + 28 + 34 + 18 + 18, 16},
    [BUILD_BLK_BANK_HIGH]  = {64 + 92 + 18 + 18, 21},
    [BUILD_BLK_EMPTY_META] = {186 + 140, 34},
    // ...and the one that draws: `INC $8A : LDA $00,X : AND #$0006 : TAX` and
    // the `JSR ($BDEA,X)` at 52, whose two vector bytes come out of bank $80
    // and so belong in the byte column with the program.
    [BUILD_BLK_DRAW]       = {186 + 300, 47},
    [BUILD_BLK_FULL]       = {18 + 18, 5},
    [BUILD_BLK_NOT_FULL]   = {18 + 12, 5},
    [BUILD_BLK_NEXT]       = {28 + 12 + 12 + 28 + 18 + 18, 11},
    // ...or the last record, which falls through to `$80:BDC4`'s terminator.
    [BUILD_BLK_LAST]       = {92 + 86, 16},
    // $80:BDCC..$80:BDE2, with the `JSL` at 54 and the `RTL` at 42. Priced with
    // the four-byte table read fast; `build_cycles` corrects it when the data
    // bank the `PLB` restored cannot reach bank $80 in six.
    [BUILD_BLK_EPILOGUE]   = {314, 25},
};

// The two program bytes of `$80:BDD8 LDA $BDE6,X` cost 8 apiece rather than 6
// when the caller's data bank is a low one, and stop being FastROM-sensitive
// when they do. It is the only instruction in the pass that asks.
static const CosimRun BUILD_EPILOGUE_SLOW_TABLE = {4, -2};

// False for the same reason `overlap_cycles` is: a collision inside this pass
// entered a handler with no table. Everything else about the pass is priced
// whether or not that happens.
static bool build_cycles(const SpriteBuildWork* k, const CosimRegs* in,
                         int* out) {
  const bool fast = in->fastrom;
  int cycles = 0;
  for (int i = 0; i < BUILD_BLOCK_COUNT; i++) {
    CosimRun r = BUILD_COST[i];
    if (i == BUILD_BLK_EPILOGUE && in->db < 0x80)
      r = run_plus(r, BUILD_EPILOGUE_SLOW_TABLE);
    cycles += k->blocks[i] * cosim_run_cycles(&r, fast);
  }
  // ...and everything the walk called. `oam_buffer_clear` runs once per pass
  // and is the only one of the five that costs the same every time.
  cycles += depth_sort_cycles(&k->sort, fast);
  cycles += cull_cycles(&k->cull, fast);
  cycles += cosim_run_cycles(&OAM_CLEAR_COST, fast);
  int overlap;
  if (!overlap_cycles(&k->overlap, in, &overlap)) return false;
  cycles += overlap;
  cycles += emit_cycles(k, fast);
  cycles += tile_cycles(&k->tile, fast);
  *out = cycles;
  return true;
}

static void shim_sprite_build_oam(Wram* w, const Rom* rom, const CosimRegs* in,
                                  CosimRegs* out) {
  // The guard already established it will not decline.
  SpriteBuildWork work;
  sprite_build_oam_counted(w, rom, in->d, &work);

  // `$80:BDD2 LDA $20` runs after `PLD`, on the caller's page, so an unaligned
  // one costs an internal cycle the table above does not carry. `$80:BDDE STA
  // $1B64` runs after `PLB` and reaches low WRAM in 8 through every bank a
  // caller could plausibly leave behind — but not through $40..$7F or $C0+,
  // where it would not be WRAM at all, so those decline rather than guess.
  // ...and the pass still inherits `actor_overlap_pass`'s refusal, but it is a
  // narrower one than it was. A pair that actually touched sends `$80:BF0E JSR
  // $BE8F` into the collision handler tree; four of those handlers now have
  // cost tables, so a pass whose collisions all landed on one of the four is
  // priced like any other, and only the rest report nothing. That is why
  // `build_cycles` returns a bool now instead of taking `hits == 0` as a
  // precondition.
  int cycles;
  if ((in->d & 0xff) == 0 &&
      (in->db < 0x40 || (in->db >= 0x80 && in->db < 0xc0)) &&
      build_cycles(&work, in, &cycles))
    cosim_cost(cycles);

  // The tail at `$80:BDD2` is what decides all of this, and it runs on every
  // path: `LDA $20 : AND #$0003 : TAX : LDA $BDE6,X : AND #$00FF : STA $1B64 :
  // SEC : RTL`.
  //
  //   * A is the table entry after the mask — the value just stored.
  //   * X is the low two bits of `$20` on the **caller's** page, from the `TAX`.
  //     `$80:BDD0  PLD` has already restored it, so this is `in->d + $20` and
  //     not `W_SCHED_TICK`; two of the three callers are threads. Because all
  //     four table entries are $80, the difference is invisible in WRAM and
  //     shows up here and nowhere else — `movies/level49-bubble.zmv` is the
  //     first input to reach one of those callers, and it failed on this
  //     register with 128 KB matching.
  //   * `AND #$00FF` is the last flag-setting instruction, so N and Z describe
  //     that same value. It is $80 for all four entries, so Z is false and N is
  //     false too: $0080 is positive in 16 bits.
  //   * Carry is the `SEC`, and it is the one output here a caller could
  //     plausibly read.
  //
  // Y is never mentioned between `$80:BDD0` and the `RTL`, but it is not the
  // caller's either — the pass ran a great deal of code that used it. It is
  // whatever `actor_overlap_pass` left, and that is 0 on all three of its exits,
  // including the one taken when nothing is visible at all.
  out->a = wram_r16(w, W_SPRITE_PASS_PHASE);
  out->x = (uint16_t)(wram_r16(w, (uint32_t)((in->d + SPRITE_PASS_PHASE_DP) &
                                            0xffff)) &
                     3);
  out->y = 0;
  out->n = (out->a & 0x8000) != 0;
  out->z = out->a == 0;
  out->c = true;
  out->flags = COSIM_FLAG_N | COSIM_FLAG_Z | COSIM_FLAG_C;
}

// ---------------------------------------------------------------------------
// $80:B123  actor_nearest — X, Y = the point; X = winner, A = its distance
// ---------------------------------------------------------------------------
//
// `$80:B189  LDX $44 : LDA $38 : PLD : RTL`.
//
// **N and Z do not come from the distance, they come from the `PLD`** — a pull
// sets them from the value pulled, so what the caller sees is the sign and
// zeroness of *its own direct page*, restored one instruction before the `RTL`.
// The first version of this shim read them off `LDA $38` and passed 1,006 of
// 1,006 calls on `movies/level1.zmv`, because every search on that movie found
// something and a distance under `$8000` has the same sign bit as a thread page
// does. It failed the moment a search came up empty and left `$FFFF` in A:
// `flag N: ROM 0, port 1`, on four movies at once.
//
// Carry is the one flag that is really the routine's: `PLD` does not touch it,
// so it is still what the `CPX #$185E` that ended the walk left — always clear,
// because the walk always ends the same way, running a fixed 32 slots. Y is
// untouched after the `STY $3C` on the way in.
static void shim_actor_nearest(Wram* w, const Rom* rom, const CosimRegs* in,
                               CosimRegs* out) {
  (void)rom;
  uint16_t dist = 0;
  uint16_t found = actor_nearest(w, in->x, in->y, &dist);
  out->a = dist;
  out->x = found;
  out->y = in->y;
  out->n = (in->d & 0x8000u) != 0;
  out->z = in->d == 0;
  out->c = false;
  out->flags = COSIM_FLAG_N | COSIM_FLAG_Z | COSIM_FLAG_C;
}

// ---------------------------------------------------------------------------
// $80:B093  actor_gap — Y = a record; A = its distance from $38/$3A
// ---------------------------------------------------------------------------
//
// **The only routine in the registry whose point arrives in memory rather than
// in registers.** It is a `JSR` leaf inside bank $80 with two callers, both of
// which write `$38` and `$3A` before entering it, so there is nothing to read
// off `in` but Y — and reading them from WRAM inside the shim is not a shortcut,
// it is the calling convention.
//
// Carry is declared on two of the three exits and withheld on the third. See
// `ActorGapRegs`: the empty-record exit does not execute anything that writes
// it, so claiming a value there would be asserting what the *caller* left, and
// `out->flags` is per call precisely so a shim can decline.
//
// X is untouched; Y is the record, and the routine never writes either.
static void shim_actor_gap(Wram* w, const Rom* rom, const CosimRegs* in,
                           CosimRegs* out) {
  (void)rom;
  ActorGapRegs r;
  actor_gap(w, in->y, &r);
  out->a = r.a;
  out->x = in->x;
  out->y = in->y;
  out->n = r.n;
  out->z = r.z;
  out->c = r.c;
  out->flags = COSIM_FLAG_N | COSIM_FLAG_Z | (r.has_c ? COSIM_FLAG_C : 0);
}

// ---------------------------------------------------------------------------
// $80:B18F  actor_nearest_id3 — X, Y = the point; X = winner, A = its distance
// ---------------------------------------------------------------------------
//
// `actor_nearest`'s register contract exactly, because it is `actor_nearest`
// with one collision id in place of four: N and Z off the closing `PLD` and so
// the caller's own direct page, carry clear from the `CPX` that ends a walk
// which always ends the same way, Y untouched from the `STY $3C` on the way in.
static void shim_actor_nearest_id3(Wram* w, const Rom* rom, const CosimRegs* in,
                                   CosimRegs* out) {
  (void)rom;
  uint16_t dist = 0;
  uint16_t found = actor_nearest_id3(w, in->x, in->y, &dist);
  out->a = dist;
  out->x = found;
  out->y = in->y;
  out->n = (in->d & 0x8000u) != 0;
  out->z = in->d == 0;
  out->c = false;
  out->flags = COSIM_FLAG_N | COSIM_FLAG_Z | COSIM_FLAG_C;
}

// ---------------------------------------------------------------------------
// $80:B22A  actor_bearing — X = the record to look from, Y = the one to find
// ---------------------------------------------------------------------------
//
// A is the direction, X is the table index that produced it, Y is the record
// asked about and is never written. N and Z are the closing `PLD`'s — the same
// split as `actor_nearest`, and for the same reason.
//
// Carry is the routine's own and is **not** the horizontal `CMP`'s: the `ADC`
// under it overwrites it on every path but the equal one. See
// `ActorBearingRegs`, where the 2,078 diverging calls that said so are
// recorded.
static void shim_actor_bearing(Wram* w, const Rom* rom, const CosimRegs* in,
                               CosimRegs* out) {
  ActorBearingRegs r;
  actor_bearing(w, rom, in->x, in->y, &r);
  out->a = r.a;
  out->x = r.x;
  out->y = in->y;
  out->n = (in->d & 0x8000u) != 0;
  out->z = in->d == 0;
  out->c = r.c;
  out->flags = COSIM_FLAG_N | COSIM_FLAG_Z | COSIM_FLAG_C;
}

// ---------------------------------------------------------------------------
// $80:B1EC  actor_bearing_point — A = a record, X and Y = the point
// ---------------------------------------------------------------------------
//
// The same three registers as `actor_bearing`, and the same flags, off a
// routine that reaches only three of its twelve table entries. Y out is the
// record — `PHA : ... : PLA : TAY` puts the accumulator argument there on the
// way in and nothing moves it afterwards — which is *not* the `y` that came in,
// and is the one place this routine's contract differs from its sibling's.
static void shim_actor_bearing_point(Wram* w, const Rom* rom,
                                     const CosimRegs* in, CosimRegs* out) {
  ActorBearingRegs r;
  actor_bearing_point(w, rom, in->a, in->x, in->y, &r);
  out->a = r.a;
  out->x = r.x;
  out->y = in->a;
  out->n = (in->d & 0x8000u) != 0;
  out->z = in->d == 0;
  out->c = r.c;
  out->flags = COSIM_FLAG_N | COSIM_FLAG_Z | COSIM_FLAG_C;
}

// ---------------------------------------------------------------------------
// $80:B26B  player_in_range — A = the range, X and Y = the point
// ---------------------------------------------------------------------------
//
// Three exits, three different carries, and two of them return the same player;
// `PlayerPickRegs` is where the four cases are set out. N and Z are the `PLD`'s
// as everywhere in this family.
static void shim_player_in_range(Wram* w, const Rom* rom, const CosimRegs* in,
                                 CosimRegs* out) {
  (void)rom;
  PlayerPickRegs r;
  player_in_range(w, in->a, in->x, in->y, &r);
  out->a = r.a;
  out->x = r.x;
  out->y = r.y;
  out->n = (in->d & 0x8000u) != 0;
  out->z = in->d == 0;
  out->c = r.c;
  out->flags = COSIM_FLAG_N | COSIM_FLAG_Z | COSIM_FLAG_C;
}

// ---------------------------------------------------------------------------
// $80:B2A5  player_bearing — A = the range, X and Y = the point
// ---------------------------------------------------------------------------
//
// The busiest of the six: 19,898 calls over twenty-seven sites in three banks,
// and the one the enemies actually steer by.
//
// All three registers are claimed and all three are different per exit — the
// direction exit hands back a distance in X and a record in Y, both pulled off
// the stack around the table lookup, while the zero exit hands back the
// caller's own `x` and whatever `$D4` held. Carry is clear on every direction
// exit and set on the zero one, and it is not a `SEC`/`CLC` pair that makes it
// so: it is the `ASL` that doubles a word-table index, which never carries.
static void shim_player_bearing(Wram* w, const Rom* rom, const CosimRegs* in,
                                CosimRegs* out) {
  PlayerPickRegs r;
  player_bearing(w, rom, in->a, in->x, in->y, &r);
  out->a = r.a;
  out->x = r.x;
  out->y = r.y;
  out->n = (in->d & 0x8000u) != 0;
  out->z = in->d == 0;
  out->c = r.c;
  out->flags = COSIM_FLAG_N | COSIM_FLAG_Z | COSIM_FLAG_C;
}

// ---------------------------------------------------------------------------
// $80:BF67  actor_at_point — A = self, X, Y = the point; carry = occupied
// ---------------------------------------------------------------------------
//
// Both exits are `PLD` and then an explicit `SEC` or `CLC`, so **carry is the
// routine's own** and N and Z are the `PLD`'s — the same split as
// `actor_nearest`, arrived at from the opposite direction, and the reason to
// read the last three instructions of a routine rather than the last one that
// looks like it computes something.
//
// A, X and Y are all claimed and none of them is tidy. The ROM never tidies
// them: it falls out of the loop with whatever the last iteration left, so what
// a caller sees is the last comparison's arithmetic in A, the loop index in X —
// `$FFFE` when the walk ran out, because the count is a byte count and always
// even — and the last entry it looked at in Y, which on the found path is the
// record that matched and is presumably the point.
static void shim_actor_at_point(Wram* w, const Rom* rom, const CosimRegs* in,
                                CosimRegs* out) {
  (void)rom;
  AtPointRegs r;
  actor_at_point(w, in->a, in->x, in->y, &r);
  out->a = r.a;
  out->x = r.x;
  out->y = r.y;
  out->c = r.found;
  out->n = (in->d & 0x8000u) != 0;
  out->z = in->d == 0;
  out->flags = COSIM_FLAG_N | COSIM_FLAG_Z | COSIM_FLAG_C;
}

// ---------------------------------------------------------------------------
// $80:BFC8  actor_obstacle_at_point — X, Y = the point; carry = the step is
//           blocked
// ---------------------------------------------------------------------------
//
// The same flag contract as `actor_at_point` next door, for the same reason:
// `PLD` and then an explicit `SEC`/`CLC`, so carry is the routine's and N and Z
// are the caller's own direct page coming back off the stack. Written that way
// from the start this time, rather than found by four movies failing at once.
//
// **A is an input here even though the routine never reads it.** There is no
// `STA` on the way in, so the `LDX $9C : BEQ` path returns with the caller's
// accumulator untouched, and a shim that published a constant would diverge on
// the first frame with an empty display list. `in->a` is not passed because the
// routine wants it — it is passed because the routine's silence about it is
// part of the contract.
static void shim_actor_obstacle_at_point(Wram* w, const Rom* rom,
                                         const CosimRegs* in, CosimRegs* out) {
  (void)rom;
  ObstacleRegs r;
  actor_obstacle_at_point(w, in->a, in->x, in->y, &r);
  out->a = r.a;
  out->x = r.x;
  out->y = r.y;
  out->c = r.blocked;
  out->n = (in->d & 0x8000u) != 0;
  out->z = in->d == 0;
  out->flags = COSIM_FLAG_N | COSIM_FLAG_Z | COSIM_FLAG_C;
}

// ---------------------------------------------------------------------------
// $80:AE14 / $80:AE97  terrain_blocked — X, Y = the point; carry = blocked
// ---------------------------------------------------------------------------
//
// Two routines, one shim shape. Both open `PHD` and close `PLD : RTL`, so N and
// Z are the caller's direct page yet again; the difference is where carry comes
// from. `$80:AE14` ends its last probe on `LSR A` and lets the shifted-out bit
// *be* the answer — no branch, no `SEC`, the carry is simply bit 0 of the
// attribute word — while `$80:AE97` tests with `BIT #$0002`, which cannot set
// carry, and so needs an explicit `CLC`/`SEC` at each of its two exits.
//
// That difference is also why A comes back shifted from one and not the other,
// and `port/terrain.c` applies it in the wrapper rather than the shared body.
static void shim_terrain_blocked(Wram* w, const Rom* rom, const CosimRegs* in,
                                 CosimRegs* out) {
  (void)rom;
  TerrainRegs r;
  terrain_blocked(w, in->x, in->y, &r);
  out->a = r.a;
  out->x = r.x;
  out->y = r.y;
  out->c = r.blocked;
  out->n = (in->d & 0x8000u) != 0;
  out->z = in->d == 0;
  out->flags = COSIM_FLAG_N | COSIM_FLAG_Z | COSIM_FLAG_C;
}

static void shim_terrain_blocked_enemy(Wram* w, const Rom* rom,
                                       const CosimRegs* in, CosimRegs* out) {
  (void)rom;
  TerrainRegs r;
  terrain_blocked_enemy(w, in->x, in->y, &r);
  out->a = r.a;
  out->x = r.x;
  out->y = r.y;
  out->c = r.blocked;
  out->n = (in->d & 0x8000u) != 0;
  out->z = in->d == 0;
  out->flags = COSIM_FLAG_N | COSIM_FLAG_Z | COSIM_FLAG_C;
}

// ---------------------------------------------------------------------------
// $80:B422  terrain_out_of_bounds — X, Y = the point; carry = off the map
// ---------------------------------------------------------------------------
//
// **The first routine here with no `PHD`, and it is the interesting case.**
// Every other shim in this file that publishes N and Z takes them from the
// `PLD` on the way out, because a pull sets them from the value pulled. This
// one never touches the direct page, so there is nothing to take them from and
// they are simply whatever the instruction that decided the answer left.
//
// There are six of those, and they do not agree. Four exits arrive at a shared
// `SEC : RTL`, which does not touch N or Z — so those carry the flags of the
// `TXA`, `TYA` or `CMP` that branched to it. A fifth branches straight to the
// `RTL` and keeps its own compare's carry rather than the `SEC`'s. The sixth
// falls off the end, and there the last `CMP` is the entire answer.
//
// Reading the routine's final instruction — which is `CMP $00B4` — and
// publishing that everywhere would be right on one path in six.
static void shim_terrain_out_of_bounds(Wram* w, const Rom* rom,
                                       const CosimRegs* in, CosimRegs* out) {
  (void)rom;
  BoundsRegs r;
  terrain_out_of_bounds(w, in->x, in->y, &r);
  out->a = r.a;
  out->x = in->x;  // `TXA`/`TYA` read them and nothing writes either
  out->y = in->y;
  out->n = r.n;
  out->z = r.z;
  out->c = r.c;
  out->flags = COSIM_FLAG_N | COSIM_FLAG_Z | COSIM_FLAG_C;
}

// ---------------------------------------------------------------------------
// $82:90F7  terrain_blocked_wide — X, Y = the point; carry = blocked
// ---------------------------------------------------------------------------
//
// `$80:AE14` with five tiles across instead of three, the loop written out ten
// times, a nine-bit tile mask, and a second rule: a tile whose index is below
// `W_TILE_PRIORITY_BELOW` is refused before its attribute word is read.
//
// `A` is therefore not always an attribute word. On the two exits the priority
// threshold decides it is the tile *index*, unshifted — and on those exits `Y`
// is the map offset, except for the first probe, which does not index at all
// and returns the caller's own `Y`.
static void shim_terrain_blocked_wide(Wram* w, const Rom* rom,
                                      const CosimRegs* in, CosimRegs* out) {
  (void)rom;
  TerrainRegs r;
  terrain_blocked_wide(w, in->x, in->y, &r);
  out->a = r.a;
  out->x = r.x;
  out->y = r.y;
  out->c = r.blocked;
  out->n = (in->d & 0x8000u) != 0;
  out->z = in->d == 0;
  out->flags = COSIM_FLAG_N | COSIM_FLAG_Z | COSIM_FLAG_C;
}

// ---------------------------------------------------------------------------
// $80:AF2C / $80:B05F / $80:B03B / $80:AF66 — the rest of the attribute word
// ---------------------------------------------------------------------------
//
// Four more masks out of the same sixteen-bit word, three of them one tile and
// the fourth the familiar six. All four `PHD`/`PLD`, so N and Z are the
// caller's direct page and only carry is ever the answer.
//
// The one thing worth watching in a shim here is **A**, because the four do not
// agree about it. `$80:AF2C` tests with `BIT #$0004` and hands back the whole
// attribute word; the other three test with `AND` and hand back the mask or
// zero, which says nothing carry did not. And `$80:AF2C` has a fifth exit
// before any of that — `JSL $80B422` deciding the point is off the map, with A
// the bounds test's own and X and Y never touched.
static void shim_terrain_point_bit2(Wram* w, const Rom* rom,
                                    const CosimRegs* in, CosimRegs* out) {
  (void)rom;
  TerrainRegs r;
  terrain_point_bit2(w, in->x, in->y, &r);
  out->a = r.a;
  out->x = r.x;
  out->y = r.y;
  out->c = r.blocked;
  out->n = (in->d & 0x8000u) != 0;
  out->z = in->d == 0;
  out->flags = COSIM_FLAG_N | COSIM_FLAG_Z | COSIM_FLAG_C;
}

static void shim_terrain_point_bit8(Wram* w, const Rom* rom,
                                    const CosimRegs* in, CosimRegs* out) {
  (void)rom;
  TerrainRegs r;
  terrain_point_bit8(w, in->x, in->y, &r);
  out->a = r.a;
  out->x = r.x;
  out->y = r.y;
  out->c = r.blocked;
  out->n = (in->d & 0x8000u) != 0;
  out->z = in->d == 0;
  out->flags = COSIM_FLAG_N | COSIM_FLAG_Z | COSIM_FLAG_C;
}

// X and Y are tile coordinates here, not pixels — this is the one whose caller
// has already divided. X comes back doubled, which is `tilemap_tile_addr`'s
// `PLX` showing through rather than anything this routine decided.
static void shim_terrain_tile_bit3(Wram* w, const Rom* rom, const CosimRegs* in,
                                   CosimRegs* out) {
  (void)rom;
  TerrainRegs r;
  terrain_tile_bit3(w, in->x, in->y, &r);
  out->a = r.a;
  out->x = r.x;
  out->y = r.y;
  out->c = r.blocked;
  out->n = (in->d & 0x8000u) != 0;
  out->z = in->d == 0;
  out->flags = COSIM_FLAG_N | COSIM_FLAG_Z | COSIM_FLAG_C;
}

// The inverted one: carry *clear* means all six probes carried bit 12.
static void shim_terrain_footprint_bit12(Wram* w, const Rom* rom,
                                         const CosimRegs* in, CosimRegs* out) {
  (void)rom;
  TerrainRegs r;
  terrain_footprint_bit12(w, in->x, in->y, &r);
  out->a = r.a;
  out->x = r.x;
  out->y = r.y;
  out->c = r.blocked;
  out->n = (in->d & 0x8000u) != 0;
  out->z = in->d == 0;
  out->flags = COSIM_FLAG_N | COSIM_FLAG_Z | COSIM_FLAG_C;
}

// ---------------------------------------------------------------------------
// $80:AFFB  partner_near — A = the caller's record, X/Y = a point
// ---------------------------------------------------------------------------
//
// Sat in the middle of those four in the ROM and belongs with `$80:A8B3`
// instead: the other test about the other player, with the leash taken off.
//
// **No `PHD`**, which puts it in the small class `terrain_out_of_bounds`
// started — N and Z are whatever decided, and there are two kinds of decision.
// The two absent-player exits leave a `BIT`'s flags, and `BIT abs` sets Z from
// **A AND memory** rather than from memory, so the routine's existence check is
// really an overlap test that works only because every actor record pointer in
// the game has bits 11 and 12 set. `port/step.h` says why at length.
static void shim_partner_near(Wram* w, const Rom* rom, const CosimRegs* in,
                              CosimRegs* out) {
  (void)rom;
  PartnerRegs r;
  partner_near(w, in->a, in->x, in->y, &r);
  out->a = r.a;
  out->x = r.x;
  out->y = r.y;
  out->n = r.n;
  out->z = r.z;
  out->c = r.c;
  out->flags = COSIM_FLAG_N | COSIM_FLAG_Z | COSIM_FLAG_C;
}

// ---------------------------------------------------------------------------
// $80:E450  step_propose — the mover's page in D; $34/$36 = where it wants to be
// ---------------------------------------------------------------------------
//
// A `JSR`, so the direct page is the caller's and the caller is a thread: `D`
// is the mover's own 128-byte page, and the shim hands it over rather than
// assuming, exactly as the collision handlers above do.
//
// The published flags are not the ones the routine's last instruction sets.
// `CPX #$0000 : BEQ` sits between the first add and the second, so the single-
// step path — which is most of them — returns that compare's flags and not the
// arithmetic's: `Z` set, `C` set, `N` clear, regardless of where the mover
// ended up. Only a double step returns the `ADC`'s.
static void shim_step_propose(Wram* w, const Rom* rom, const CosimRegs* in,
                              CosimRegs* out) {
  StepProposeRegs r;
  step_propose(w, rom, in->d, &r);
  out->a = r.a;
  out->x = r.x;
  out->y = r.y;
  out->n = r.n;
  out->z = r.z;
  out->c = r.c;
  out->flags = COSIM_FLAG_N | COSIM_FLAG_Z | COSIM_FLAG_C;
}

// ---------------------------------------------------------------------------
// $80:A8B3  step_tether_blocked — X, Y = the candidate; carry = too far away
// ---------------------------------------------------------------------------
//
// Back to a `PHD`, so N and Z are the caller's direct page again.
//
// The three register outputs are all live and all different per exit, which is
// why they are worth stating: the alone exit leaves `A` at zero — the `LDA
// #$0000` that set the direct page, never touched again — with `Y` the zero
// record it just read and `X` the candidate it was handed. The inside-window
// exit leaves the biased Y offset in `A` and the reference record in `Y`. The
// far path overwrites all three with the two players' separation, `$D2` and
// `$D4`.
static void shim_step_tether_blocked(Wram* w, const Rom* rom,
                                     const CosimRegs* in, CosimRegs* out) {
  (void)rom;
  TetherRegs r;
  step_tether_blocked(w, in->x, in->y, &r);
  out->a = r.a;
  out->x = r.x;
  out->y = r.y;
  out->c = r.blocked;
  out->n = (in->d & 0x8000u) != 0;
  out->z = in->d == 0;
  out->flags = COSIM_FLAG_N | COSIM_FLAG_Z | COSIM_FLAG_C;
}

// ---------------------------------------------------------------------------
// $80:A54D / $80:A588 / $80:9E6D -- the last three leaves under the camera
// ---------------------------------------------------------------------------
//
// Small enough to take together, and between them they finish the layer the
// four scroll routines stand on. See `port/camera.h` for each.
//
// `$80:9E6D` is the one worth pausing on. It opens `BIT $26`, and `BIT` against
// memory sets N from **bit 15 of the operand** but Z from **A AND the operand**
// -- so the Z this routine returns on its first exit is a fact about the
// caller's accumulator, which it never loads and has no other use for. A shim
// that derived Z from anything the routine computes would be wrong on every
// call that takes that path, and right by accident on the rest.
static void shim_camera_window_update(Wram* w, const Rom* rom,
                                      const CosimRegs* in, CosimRegs* out) {
  (void)rom;
  CameraWindowRegs r;
  camera_window_update(w, &r);
  out->a = r.a;
  out->x = in->x;  // neither index is mentioned in twenty-seven instructions
  out->y = in->y;
  out->n = r.n;
  out->z = r.z;
  out->c = r.c;
  out->flags = COSIM_FLAG_N | COSIM_FLAG_Z | COSIM_FLAG_C;
}

static void shim_camera_split_y(Wram* w, const Rom* rom, const CosimRegs* in,
                                CosimRegs* out) {
  (void)rom;
  CameraSplitRegs r;
  camera_split_y(w, &r);
  out->a = r.a;
  out->x = in->x;
  out->y = in->y;
  out->n = r.n;
  out->z = r.z;
  out->c = r.c;
  out->flags = COSIM_FLAG_N | COSIM_FLAG_Z | COSIM_FLAG_C;
}

static void shim_vram_queue_request(Wram* w, const Rom* rom,
                                    const CosimRegs* in, CosimRegs* out) {
  (void)rom;
  VramRequestRegs r;
  vram_queue_request(w, in->a, &r);
  out->a = r.a;
  out->x = in->x;
  out->y = in->y;
  out->n = r.n;
  out->z = r.z;
  // Seven bytes and none of them touches carry, on any of the three paths.
  out->c = in->c;
  out->flags = COSIM_FLAG_N | COSIM_FLAG_Z | COSIM_FLAG_C;
}

// ---------------------------------------------------------------------------
// $80:A61D  tilemap_copy_row -- X = column, Y = row
// ---------------------------------------------------------------------------
//
// `tilemap_copy_column`'s twin, and the last leaf under the Y-axis scroll
// routines. It buys its own 66-byte strip out of the arena instead of being
// handed one, which is why it carries the allocator's guard as well.
//
// Its outputs are the least summary-like in the registry: A and carry are
// *whatever the thirty-third tile happened to be*, N and Z belong to a `DEY`
// that has already run off the end, and X belongs to the allocator's `PLX`
// three instructions before the loop even started. Four registers, four
// unrelated origins, and the routine returns nothing that describes its work.
static bool guard_tilemap_copy_row(Wram* scratch, const Rom* rom,
                                   const CosimRegs* in) {
  (void)rom;
  (void)in;
  if (tilemap_copy_row_supported(scratch)) return true;
  cosim_census_note("tilemap arena exhausted", TILEMAP_COPY_ROW_ENTRY);
  return false;
}

static void shim_tilemap_copy_row(Wram* w, const Rom* rom, const CosimRegs* in,
                                  CosimRegs* out) {
  (void)rom;
  TilemapCopyRegs r;
  tilemap_copy_row(w, in->x, in->y, &r);
  out->a = r.a;
  out->x = r.x;
  out->y = r.y;
  out->n = r.n;
  out->z = r.z;
  out->c = r.c;
  out->flags = COSIM_FLAG_N | COSIM_FLAG_Z | COSIM_FLAG_C;
}

// ---------------------------------------------------------------------------
// $80:A401  tilemap_buffer_alloc -- A = bytes wanted; A = where they start
// ---------------------------------------------------------------------------
//
// The arena `$80:A5E5` fills. Registered with a guard rather than a coverage
// site, which is the interesting choice here: the routine's one branch is a
// spin waiting for the arena to be given back, no input in ten traces has ever
// taken it, and a site nothing can reach would sit in the untaken list forever
// diluting the number `coverage.h` exists to keep. A guard says the same thing
// and costs nothing while it never fires. See `port/camera.h`.
static bool guard_tilemap_buffer_alloc(Wram* scratch, const Rom* rom,
                                       const CosimRegs* in) {
  (void)rom;
  if (tilemap_buffer_alloc_supported(scratch, in->a)) return true;
  cosim_census_note("tilemap arena exhausted", TILEMAP_BUFFER_ALLOC_ENTRY);
  return false;
}

static void shim_tilemap_buffer_alloc(Wram* w, const Rom* rom,
                                      const CosimRegs* in, CosimRegs* out) {
  (void)rom;
  TilemapAllocRegs r;
  tilemap_buffer_alloc(w, in->a, &r);
  out->a = r.a;
  out->x = r.x;
  out->y = r.y;
  out->n = r.n;
  out->z = r.z;
  out->c = r.c;
  out->flags = COSIM_FLAG_N | COSIM_FLAG_Z | COSIM_FLAG_C;
}

// ---------------------------------------------------------------------------
// $80:A5E5  tilemap_copy_column -- A = tiles, X = column, Y = row
// ---------------------------------------------------------------------------
//
// One step up the camera chain from `tilemap_tile_addr`, and the routine that
// puts a new strip of map on screen when the view has drifted eight pixels. The
// count arrives in A, goes onto the stack, and comes back off through `$01,S`
// after the `JSL` -- so the shim has nothing to do about it, but the registry
// entry's `stack_bytes` does.
//
// Its flags are, for once, the ones a reader would guess: `ADC $54` is the last
// thing before the `RTS` and N, Z and C all describe the destination pointer it
// returns in A. The chain's other two routines both end on a pull.
static void shim_tilemap_copy_column(Wram* w, const Rom* rom,
                                     const CosimRegs* in, CosimRegs* out) {
  (void)rom;
  TilemapCopyRegs r;
  tilemap_copy_column(w, in->a, in->x, in->y, &r);
  out->a = r.a;
  out->x = r.x;
  out->y = r.y;
  out->n = r.n;
  out->z = r.z;
  out->c = r.c;
  out->flags = COSIM_FLAG_N | COSIM_FLAG_Z | COSIM_FLAG_C;
}

// ---------------------------------------------------------------------------
// $80:AD1C  tilemap_tile_addr -- X = column, Y = row; A = the address
// ---------------------------------------------------------------------------
//
// The leaf under the camera. `$80:A93F` is the top portable row on the work
// ranking at 1.6%, and it cannot be substituted without the four tilemap scroll
// routines it dispatches to, which cannot be substituted without this. Fifteen
// bytes, no calls, 42,167 of them, and `hotbytes.py` says every byte runs
// exactly once per call.
//
// Three registers come back and no two of them come from the same place: A from
// the `ADC`, X from a `PLX` that puts back a *doubled* column rather than the
// caller's, and N and Z from that `PLX` rather than from A. See
// `port/terrain.h`.
static void shim_tilemap_tile_addr(Wram* w, const Rom* rom, const CosimRegs* in,
                                   CosimRegs* out) {
  (void)rom;
  TilemapAddrRegs r;
  tilemap_tile_addr(w, in->x, in->y, &r);
  out->a = r.a;
  out->x = r.x;
  out->y = in->y;  // never mentioned after the `TYA`
  out->n = r.n;
  out->z = r.z;
  out->c = r.c;
  out->flags = COSIM_FLAG_N | COSIM_FLAG_Z | COSIM_FLAG_C;
}

// ---------------------------------------------------------------------------
// $80:ADC8 / $80:ADF3  tile_attrs_at_pixel / tile_attrs_at_tile
// ---------------------------------------------------------------------------
//
// One tile's attribute word, which is what `terrain_blocked` reads six of. 22
// call sites across banks $80, $81 and $82 reach the pixel form and four reach
// the tile form, and between them they are how everything that is not a
// footprint test asks the map a question.
//
// X and Y come straight back out: `PHX : PHY` at the top saved the caller's,
// the six `LSR`s work on copies, and `PLY : PLX` put the originals back. The
// shim therefore hands `in->x` and `in->y` through rather than modelling them,
// which is also why `port/terrain.h`'s register struct has neither.
//
// **N and Z are the closing `PLB`'s**, so they are the caller's data bank byte
// and not the attribute word -- the same trap as every `PHD` routine above,
// one register over. `$80:8480` is the only other place `CosimRegs::db` is
// read, and it is read for exactly this.
static void shim_tile_attrs_at_pixel(Wram* w, const Rom* rom,
                                     const CosimRegs* in, CosimRegs* out) {
  (void)rom;
  TileAttrsRegs r;
  tile_attrs_at_pixel(w, in->x, in->y, &r);
  out->a = r.a;
  out->x = in->x;
  out->y = in->y;
  out->c = r.c;
  out->n = (in->db & 0x80u) != 0;
  out->z = in->db == 0;
  out->flags = COSIM_FLAG_N | COSIM_FLAG_Z | COSIM_FLAG_C;
}

static void shim_tile_attrs_at_tile(Wram* w, const Rom* rom,
                                    const CosimRegs* in, CosimRegs* out) {
  (void)rom;
  TileAttrsRegs r;
  tile_attrs_at_tile(w, in->x, in->y, &r);
  out->a = r.a;
  out->x = in->x;
  out->y = in->y;
  out->c = r.c;
  out->n = (in->db & 0x80u) != 0;
  out->z = in->db == 0;
  out->flags = COSIM_FLAG_N | COSIM_FLAG_Z | COSIM_FLAG_C;
}

// ---------------------------------------------------------------------------
// $80:E86D  floor_effect -- what the tile under the player does to them
// ---------------------------------------------------------------------------
//
// The first thing `$80:D1FF` does every frame, before it looks at a button.
// Two of its three callees are already in this registry -- `$80:ADC8` and
// `$80:AE14` -- and the third, `$80:F935`, has exactly one call site in the
// cartridge and is inlined into `port/floor.c` rather than registered.
//
// It takes no argument. Everything comes out of the player thread's direct
// page, which is the caller's and is why `in->d` is passed through; there is no
// `PHD` anywhere in it, so there is also no `PLD` to take N and Z from and the
// flags are whichever comparison the exit stopped at. Eleven of those.
static void shim_floor_effect(Wram* w, const Rom* rom, const CosimRegs* in,
                              CosimRegs* out) {
  FloorRegs r;
  floor_effect(w, rom, in->d, &r);
  out->a = r.a;
  out->x = r.x;
  out->y = r.y;
  out->n = r.n;
  out->z = r.z;
  out->c = r.c;
  out->flags = COSIM_FLAG_N | COSIM_FLAG_Z | COSIM_FLAG_C;
}

// ---------------------------------------------------------------------------
// $80:D1FF  player_state_normal -- the player's ordinary frame
// ---------------------------------------------------------------------------
//
// **A state handler, not a subroutine.** `$80:D1EC JMP ($D1EF,X)` reaches it
// 26,972 times and the one real `JSR $D1FF` at `$80:D40B` reaches it 726 more.
// The harness does not mind -- `cosim_step` intercepts on `pc == r->entry` and
// never looks at how the PC got there, and the `RTS` returns to whoever called
// the dispatcher -- but the ranking's calls column undercounts its entries by
// 38x, which matters when reading the standing check.
//
// Everything it reaches is already C except `$80:EAE1 item_use`, so the guard
// declines the frames that would reach that and nothing else.
static void shim_player_state_normal(Wram* w, const Rom* rom,
                                     const CosimRegs* in, CosimRegs* out) {
  PlayerStateRegs r;
  player_state_normal(w, rom, in->d, &r);
  out->a = r.a;
  out->x = r.x;
  out->y = r.y;
  out->n = r.n;
  out->z = r.z;
  out->c = r.c;
  out->flags = COSIM_FLAG_N | COSIM_FLAG_Z | COSIM_FLAG_C;
}

static bool guard_player_state_normal(const Wram* w, const Rom* rom,
                                      const CosimRegs* in) {
  (void)rom;
  return player_state_normal_supported(w, in->d);
}

// ---------------------------------------------------------------------------
// $80:CD20  lzss_decompress — the stream picks every branch, and the price is
// still exact
// ---------------------------------------------------------------------------
//
// Four frames a call at the smallest and seventy-one at the largest, measured on
// the five `level1.zmv` makes, so the same structural fact as `$80:AD2B` above:
// an NMI lands inside every one of them, `verify` abandons the lot, and what
// stands behind the substitution is `run`'s per-pass comparison of all 128 KB
// rather than a per-call diff. `run_only`. The shim is four lines — the argument
// is the
// word at `s + 4` (`$80:CD27  LDA $06,S`, six deep because the routine's own
// `PHD` is already down) and the exit is `$80:CDD2  SEC : LDA $2C : SBC $40 :
// TAY : PLD : RTL`.
//
// **What is new here is the price.** Everything registered before this is costed
// one of two ways: a mean `verify` watched the ROM take, or a count nothing had
// to watch because the trip counts fall out of the arguments. This routine is
// neither. Every branch in it is decided by the compressed stream — how many
// tokens, which are literals, how long each match runs — and there is no
// argument, table or piece of state that predicts any of it.
//
// It is exact anyway, and the reason is worth stating because it is the general
// answer rather than a lucky property of this routine: **the port decompresses
// the same stream, so it takes the same branches, so it can count them.** The
// counts are not derived from the input, they are produced by doing the work.
// `LzssWork` is that tally and this is the ordinary `_counted` pattern; the
// thing that had to be checked was not whether the counts could be had but
// whether anything else moved the clock, and three things do:
//
//   * the `MVN` at `$CD42`, which is one instruction paid for `$0FEE` times;
//   * `LDA [$28]`, 4 master cycles dearer when the stream is in WRAM than in
//     the cartridge — hence `reads_fast` and `head_fast`;
//   * where the stream runs out, because the four places it can do that pop
//     different numbers of bytes on the way to `$CDD2`. Not a defensive case:
//     **every stream this game contains ends inside a match**, at the `BCS` on
//     `$CD87` — 31 of 31 measured across fifteen movies — because a flag byte
//     carries eight bits and the data runs out before all eight are spent, so
//     the leftover zero bits read as matches and the first of them finds
//     nothing. The tidy end-at-the-refill case has never once been seen, and a
//     model that assumed it would be short on every call in the game.
//
// Nothing else: `PEA $0000 : PLD` puts the routine on page zero, so no
// direct-page penalty anywhere, and the destination is WRAM by the guard.

// The window fill and everything either side of it. `MVN` is split out because
// it is one instruction whose cost is not in the instruction: `cpu.c` moves a
// single byte and then rewinds PC by three rather than looping inside the
// opcode, so each byte re-fetches all three program bytes and then reads one,
// writes one and idles twice. The count is A+1 and A is `$0FED`.
static const CosimRun LZSS_PROLOGUE_ROM = {668, 53};   // $CD20..$CD53, less MVN
static const CosimRun LZSS_PROLOGUE_WRAM = {672, 51};  // ...header word in WRAM
static const CosimRun LZSS_MVN_BYTE = {46, 3};         // $CD42, $0FEE of them
#define LZSS_MVN_BYTES 0x0feeu

// $CDD2..$CDD9, the `RTL` included. **Every model in this file is priced to the
// window `verify` measures**, which runs from the entry PC to the caller's
// return address and therefore contains the routine's own `RTS` or `RTL`. That
// is the only window a measurement can be taken over, so it is the one a model
// is written for, and the one place the boundary has to be stated is here.
//
// `run` used to disagree with that, silently. `native_return` parks the CPU on
// `ret_op` and the core executes it for real, so a budget that also contained it
// paid for it twice — 40 cycles for an `RTS`, which is a DRAM refresh to the
// cycle and so never looked like anything, and 42 for an `RTL`, which is what
// finally showed up: burn-end to back-at-the-caller measured 42 longer than the
// ROM's entry-to-`RTL` on three calls running. The harness now takes the tail
// off the budget itself, once, for every substituted call. See `tail_cycles` in
// `cosim/cosim.c`; nothing in this file has to remember it.
static const CosimRun LZSS_EPILOGUE = {156, 8};

// The token loop. Every branch is priced not taken and `LZSS_TAKEN` is added
// back per taken outcome, which is the same arrangement as `blockmap_cycles`.
static const CosimRun LZSS_HEAD = {24, 3};          // $CD56..$CD57
static const CosimRun LZSS_REFILL = {70, 8};        // $CD59..$CD5F
static const CosimRun LZSS_SHIFT = {36, 4};         // $CD61..$CD63
static const CosimRun LZSS_LITERAL = {338, 29};     // $CD65..$CD80, BRA taken
static const CosimRun LZSS_MATCH_HEAD = {460, 38};  // $CD82..$CDA6
static const CosimRun LZSS_RUN_BYTE = {384, 34};    // $CDA8..$CDC8
static const CosimRun LZSS_MATCH_TAIL = {114, 6};   // $CDCA..$CDCE, BRA taken
#define LZSS_TAKEN 6

// The two leaves. They are in the registry in their own right and `verify`
// scores both on every one of the million-odd calls the corpus makes — so these
// four numbers are the only ones in this model that have already been checked
// against the ROM by measurement, and they are the ones the model leans on
// hardest. Under substitution the leaves never execute, so their cost has to be
// here; `verify` is what says it is right.
static const CosimRun LZSS_READ_ROM = {258, 17};  // $CDDA..$CDE8, stream in ROM
static const CosimRun LZSS_READ_WRAM = {262, 15};
static const CosimRun LZSS_READ_SPENT = {98, 6};  // $CDDA, $CDE9, $CDEA
static const CosimRun LZSS_WRITE = {170, 9};      // $CDEB..$CDF3, dest in WRAM

// A stream that stops in the middle of a token. Each includes the `PLA`s at
// `$CDD0`/`$CDD1` that unwind what the block had pushed, which is the only
// reason the three differ at all and the reason `end` distinguishes them.
static const CosimRun LZSS_END_LIT = {120, 7};  // $CD65..$CD69, one PLA
static const CosimRun LZSS_END_M1 = {182, 9};   // $CD82..$CD87, two
static const CosimRun LZSS_END_M2 = {262, 16};  // $CD82..$CD8E, two

static int lzss_cycles(const LzssWork* k, bool fast) {
  // `tokens_lit` and `tokens_match` count blocks *entered*, so the last one is
  // in there whether or not the stream let it finish. These four are what the
  // model actually multiplies.
  const uint32_t tokens = k->tokens_lit + k->tokens_match;
  const uint32_t lits_full = k->tokens_lit - (k->end == 1 ? 1u : 0u);
  const uint32_t match_full = k->tokens_match - (k->end >= 2 ? 1u : 0u);
  // One `$CD56` per entered block, and one more when the stream ended at the
  // flag refill — that iteration reached the head and got no further.
  const uint32_t heads = tokens + (k->end == 0 ? 1u : 0u);
  // Every `JSR $CDDA` the call made: one per refill, one per literal, two per
  // match unless the first of the two is what ended it. Exactly one of them
  // found the stream spent, because that is what ended the loop.
  const uint32_t reads = k->refills + k->tokens_lit + 2u * k->tokens_match -
                         (k->end == 2 ? 1u : 0u);
  const uint32_t reads_slow = reads - 1u - k->reads_fast;

  long cycles = cosim_run_cycles(
                    k->head_fast ? &LZSS_PROLOGUE_ROM : &LZSS_PROLOGUE_WRAM,
                    fast) +
                cosim_run_cycles(&LZSS_EPILOGUE, fast) +
                (long)LZSS_MVN_BYTES * cosim_run_cycles(&LZSS_MVN_BYTE, fast);

  cycles += (long)heads * cosim_run_cycles(&LZSS_HEAD, fast);
  cycles += (long)k->refills * cosim_run_cycles(&LZSS_REFILL, fast);
  cycles += (long)tokens * cosim_run_cycles(&LZSS_SHIFT, fast);
  cycles += (long)lits_full * cosim_run_cycles(&LZSS_LITERAL, fast);
  cycles += (long)match_full * (cosim_run_cycles(&LZSS_MATCH_HEAD, fast) +
                                cosim_run_cycles(&LZSS_MATCH_TAIL, fast));
  cycles += (long)k->run_bytes * cosim_run_cycles(&LZSS_RUN_BYTE, fast);

  cycles += (long)k->reads_fast * cosim_run_cycles(&LZSS_READ_ROM, fast);
  cycles += (long)reads_slow * cosim_run_cycles(&LZSS_READ_WRAM, fast);
  cycles += cosim_run_cycles(&LZSS_READ_SPENT, fast);
  // One `JSR $CDEB` per literal that finished and one per byte of every match.
  cycles += (long)(lits_full + k->run_bytes) * cosim_run_cycles(&LZSS_WRITE,
                                                               fast);

  // The four branches, each of which the blocks above priced not taken.
  cycles += (long)LZSS_TAKEN *
            ((long)(heads - k->refills) +               // $CD57, flag bits left
             (long)k->tokens_match +                    // $CD63, a match token
             (long)(k->run_bytes - match_full) +        // $CDC8, another byte
             (long)(k->end == 0 ? 1 : 0));              // $CD5F, stream spent

  switch (k->end) {
    case 1:
      cycles += cosim_run_cycles(&LZSS_END_LIT, fast);
      break;
    case 2:
      cycles += cosim_run_cycles(&LZSS_END_M1, fast);
      break;
    case 3:
      cycles += cosim_run_cycles(&LZSS_END_M2, fast);
      break;
    default:
      break;
  }
  return (int)cycles;
}

static void shim_lzss_decompress(Wram* w, const Rom* rom, const CosimRegs* in,
                                 CosimRegs* out) {
  LzssDecompressRegs r;
  LzssWork work;
  lzss_decompress_wram_counted(w, rom, in->s, in->a, in->x, in->y, &r, &work);
  out->a = r.a;
  out->x = r.x;
  out->y = r.y;
  out->c = r.c;
  // The closing `PLD` restores the caller's page, so N and Z describe *that* and
  // not the count — the same trap every `PHD` routine in this file sets.
  out->n = (in->d & 0x8000u) != 0;
  out->z = in->d == 0;
  out->flags = COSIM_FLAG_N | COSIM_FLAG_Z | COSIM_FLAG_C;
  cosim_cost(lzss_cycles(&work, in->fastrom));
}

static bool guard_lzss_decompress(Wram* scratch, const Rom* rom,
                                  const CosimRegs* in) {
  return lzss_decompress_supported(scratch, rom, in->s, in->a, in->x);
}

// ---------------------------------------------------------------------------
// $80:CDDA  lzss_read_byte / $80:CDEB  lzss_write_byte — its two leaves, which
// *can* be checked
// ---------------------------------------------------------------------------
//
// What stops `$80:CD20` being scored per call is that one call is frames long;
// these are eight instructions, so the interrupt problem inverts — a call
// is far too short for an NMI to land in, and the 1,071,108 of them across the
// corpus are **2.6% of every instruction the game executes**, which is more work
// than any single routine left on the ranking.
//
// They were the first pair of routines in the registry whose only caller was not
// in it, and now that it is they are worth more rather than less. Under `run`
// the body is substituted and these never execute, so their cost lives in
// `lzss_cycles` above as four constants — and `verify`, where the body is not
// substituted, still measures them against the ROM a million times a corpus.
// **That is the only part of a `run_only` model this project can check by
// measurement, and it happens to be the part that carries most of the total.**
//
// Both are `RTS` leaves that push nothing. Neither ends where it looks like it
// does: each closes on an `INC` of a direct-page pointer, so N and Z describe
// *the pointer*, not the byte. `port/lzss.h` has the detail.
//
// **Both are `verify_only`, and the volume that makes them worth having is
// exactly what stops them being substituted.** `cycles` is one number
// standing in for a range, and a million calls packed inside one multi-frame
// decompression do not let the error cancel: substituted, a level load lands
// three frames off and every input a movie applies by frame index afterwards
// moves with it. Tuning the budget cannot fix it -- the mean is already the
// mean, so what is left is variance and a constant has none -- and it was
// tried before being written down. See `CosimRoutine::verify_only`, which
// this is the second and quite different reason for.

static void shim_lzss_read_byte(Wram* w, const Rom* rom, const CosimRegs* in,
                                CosimRegs* out) {
  (void)in;
  LzssReadRegs r;
  lzss_read_byte_regs(w, rom, &r);
  out->a = r.a;
  // Neither index register is mentioned anywhere in the fifteen bytes.
  out->x = in->x;
  out->y = in->y;
  out->n = r.n;
  out->z = r.z;
  out->c = r.spent;  // `CLC` on the way out with a byte, `SEC` when spent
  out->flags = COSIM_FLAG_N | COSIM_FLAG_Z | COSIM_FLAG_C;
}

static void shim_lzss_write_byte(Wram* w, const Rom* rom, const CosimRegs* in,
                                 CosimRegs* out) {
  (void)rom;
  LzssWriteRegs r;
  lzss_write_byte_regs(w, in->a, &r);
  out->a = r.a;
  out->x = in->x;
  out->y = in->y;
  out->n = r.n;
  out->z = r.z;
  // Nine bytes and not one of them touches carry, so it arrives back as it
  // came. Claiming that rather than omitting it is the point: `verify` then
  // checks the claim 697,920 times instead of ignoring the flag.
  out->c = in->c;
  out->flags = COSIM_FLAG_N | COSIM_FLAG_Z | COSIM_FLAG_C;
}

// ---------------------------------------------------------------------------
// $80:A599  camera_split_x -- no arguments, and neither index touched
// ---------------------------------------------------------------------------
//
// `camera_split_y`'s counterpart, and four times the routine, because the
// tilemap is 64 columns stored as two 32x32 screens `$400` words apart: a row
// that crosses the seam is two transfers rather than one wrapped one. It hands
// back both of them, as `$5C`/`$5E`/`$60` and `$62`/`$64`/`$66`.
//
// Its two branches write those six words in opposite orders and it would be
// easy to publish two different flag expressions to match. They are the same
// one: both close on `LDA #$0042 : SEC : SBC <the run this branch measured>`,
// so A, N, Z and C agree even though the store underneath them does not.
static void shim_camera_split_x(Wram* w, const Rom* rom, const CosimRegs* in,
                                CosimRegs* out) {
  (void)rom;
  CameraSplitRegs r;
  camera_split_x(w, &r);
  out->a = r.a;
  out->x = in->x;
  out->y = in->y;
  out->n = r.n;
  out->z = r.z;
  out->c = r.c;
  out->flags = COSIM_FLAG_N | COSIM_FLAG_Z | COSIM_FLAG_C;
}

// ---------------------------------------------------------------------------
// $80:A68B / $A70A / $A789 / $A816 -- the four scroll routines
// ---------------------------------------------------------------------------
//
// The camera moves one pixel at a time and these are the four ways it can do
// it. Everything ported into `port/camera.h` before now exists to serve them,
// and they exist to serve `$80:A93F`, which is the 1.6% the chain was for.
//
// Two of the four are not in the listing as code at all -- `$80:A70A` and
// `$A816` are `.db` runs the tracer never proved were instructions -- and
// decoding them by hand is what shows they are their partners byte for byte
// with six substitutions. `port/camera.c` therefore has one X scroller and one
// Y scroller and a table of the six differences, which is the only way to write
// a mirror down such that the mirroring is checkable.
//
// **All four take carry as an input**, which nothing else in this registry
// does, and one exit is why: `$80:A68B` and `$A816` open `LDA $1B6A : BEQ out`
// with no `CMP` anywhere on that path, so a camera already against the near
// edge of the map returns the caller's carry untouched. The forward pair's
// `CMP $B8` overwrites it before anything can observe it, so they are handed it
// and ignore it -- and are handed it anyway, because a shim that passed
// `false` would be asserting something about the caller instead of about the
// routine.
//
// On the exits that do reach a strip, the three registers come from three
// places again: A, N and Z from `$80:9E6D`, the last call any of them makes; X
// from `STX $CE`, so it is the VRAM queue's new length; and Y from the `TAY`
// that indexed the destination table, so it is the tilemap cursor doubled.
// Carry belongs to the `ADC $1B7E` that built the last destination.
static bool guard_camera_scroll_left(Wram* scratch, const Rom* rom,
                                     const CosimRegs* in) {
  (void)rom;
  (void)in;
  if (camera_scroll_left_supported(scratch)) return true;
  cosim_census_note("tilemap arena exhausted", CAMERA_SCROLL_LEFT_ENTRY);
  return false;
}

static bool guard_camera_scroll_right(Wram* scratch, const Rom* rom,
                                      const CosimRegs* in) {
  (void)rom;
  (void)in;
  if (camera_scroll_right_supported(scratch)) return true;
  cosim_census_note("tilemap arena exhausted", CAMERA_SCROLL_RIGHT_ENTRY);
  return false;
}

static bool guard_camera_scroll_down(Wram* scratch, const Rom* rom,
                                     const CosimRegs* in) {
  (void)rom;
  (void)in;
  if (camera_scroll_down_supported(scratch)) return true;
  cosim_census_note("tilemap arena exhausted", CAMERA_SCROLL_DOWN_ENTRY);
  return false;
}

static bool guard_camera_scroll_up(Wram* scratch, const Rom* rom,
                                   const CosimRegs* in) {
  (void)rom;
  (void)in;
  if (camera_scroll_up_supported(scratch)) return true;
  cosim_census_note("tilemap arena exhausted", CAMERA_SCROLL_UP_ENTRY);
  return false;
}

static void publish_camera_scroll(const CameraScrollRegs* r, CosimRegs* out) {
  out->a = r->a;
  out->x = r->x;
  out->y = r->y;
  out->n = r->n;
  out->z = r->z;
  out->c = r->c;
  out->flags = COSIM_FLAG_N | COSIM_FLAG_Z | COSIM_FLAG_C;
}

static void shim_camera_scroll_left(Wram* w, const Rom* rom,
                                    const CosimRegs* in, CosimRegs* out) {
  CameraScrollIn args = {in->x, in->y, in->c};
  CameraScrollRegs r;
  camera_scroll_left(w, rom, &args, &r);
  publish_camera_scroll(&r, out);
}

static void shim_camera_scroll_right(Wram* w, const Rom* rom,
                                     const CosimRegs* in, CosimRegs* out) {
  CameraScrollIn args = {in->x, in->y, in->c};
  CameraScrollRegs r;
  camera_scroll_right(w, rom, &args, &r);
  publish_camera_scroll(&r, out);
}

static void shim_camera_scroll_down(Wram* w, const Rom* rom,
                                    const CosimRegs* in, CosimRegs* out) {
  CameraScrollIn args = {in->x, in->y, in->c};
  CameraScrollRegs r;
  camera_scroll_down(w, rom, &args, &r);
  publish_camera_scroll(&r, out);
}

static void shim_camera_scroll_up(Wram* w, const Rom* rom, const CosimRegs* in,
                                  CosimRegs* out) {
  CameraScrollIn args = {in->x, in->y, in->c};
  CameraScrollRegs r;
  camera_scroll_up(w, rom, &args, &r);
  publish_camera_scroll(&r, out);
}

// ---------------------------------------------------------------------------
// $80:A93F  camera_follow -- no arguments; the camera one pixel further on
// ---------------------------------------------------------------------------
//
// The top of the camera chain and the whole reason for reading it bottom-up:
// eleven routines had to go in before this one could, because a substituted
// routine has to do everything the ROM's does and there is no way to call back
// into the ROM half-way through.
//
// It picks the point the view should centre on -- one player, the other, or the
// midpoint -- and then moves the camera **one pixel** towards it per axis. The
// delta is computed in full and then only its sign is used, by an `ASL A` whose
// result is discarded and whose carry is the answer. That is why the view
// drifts after the players rather than snapping to them.
//
// A `PHD` routine, so N and Z are the caller's direct page, and every one of
// its exits is `PLD : SEC : RTL`, so **carry is set on all four and says
// nothing**. The three register outputs are worth stating because two of them
// are leftovers: A is the Y delta, or zero on the exits that never compute one;
// X is that delta unless a scroll routine overwrote it; and Y is `$0006`, the
// index the record read left behind, unless one did.
//
// The guard is the four scroll routines' arena guard asked once for both of
// them: a call can reach one X scroll and one Y scroll, the second buys its
// strip out of what the first left, so the question has to be asked about the
// sum rather than about either.
static bool guard_camera_follow(Wram* scratch, const Rom* rom,
                                const CosimRegs* in) {
  (void)rom;
  (void)in;
  if (camera_follow_supported(scratch)) return true;
  cosim_census_note("tilemap arena exhausted", CAMERA_FOLLOW_ENTRY);
  return false;
}

static void shim_camera_follow(Wram* w, const Rom* rom, const CosimRegs* in,
                               CosimRegs* out) {
  CameraFollowRegs r;
  camera_follow(w, rom, in->x, in->y, &r);
  out->a = r.a;
  out->x = r.x;
  out->y = r.y;
  out->n = (in->d & 0x8000u) != 0;  // the closing `PLD`
  out->z = in->d == 0;
  out->c = true;  // ...and the `SEC` under it, on every exit
  out->flags = COSIM_FLAG_N | COSIM_FLAG_Z | COSIM_FLAG_C;
}

// ---------------------------------------------------------------------------
// $80:B379  actor_aligned -- X, Y = a point; A = which way something is
// ---------------------------------------------------------------------------
//
// `actor_nearest`'s sibling, called by the same enemy body and answering the
// other half of its question: not *who is closest* but *is anything lined up
// with me right now*. It returns a doubled direction index, or zero.
//
// A `PHD` routine like `camera_follow`, so N and Z are the caller's direct page
// and neither means anything about the search -- the ROM's own caller does
// `TAX : BEQ` to get the answer's Z back. Carry is the leftover of whichever
// `SBC` picked the direction, and is clear on the no-match exit because a `CPX`
// ended the loop there. X is the record that matched, or `$184A`, which is the
// loop counter one stride below the table rather than a pointer to anything.
// Y is the argument, untouched.
//
// No guard: it reads 32 fixed records out of WRAM and cannot fail.
static void shim_actor_aligned(Wram* w, const Rom* rom, const CosimRegs* in,
                               CosimRegs* out) {
  (void)rom;
  ActorAlignedRegs r;
  actor_aligned(w, in->x, in->y, &r);
  out->a = r.a;
  out->x = r.x;
  out->y = in->y;
  out->n = (in->d & 0x8000u) != 0;  // the closing `PLD`
  out->z = in->d == 0;
  out->c = r.c;
  out->flags = COSIM_FLAG_N | COSIM_FLAG_Z | COSIM_FLAG_C;
}

// ---------------------------------------------------------------------------
// $80:BF1B  actor_notify_box -- no arguments; everything in a rectangle told
// ---------------------------------------------------------------------------
//
// The blast radius. `actor_overlap_pass` asks who is touching whom; this is one
// actor asking who is inside a box and telling all of them, and it is how every
// attack that is not a contact hit reaches its victims.
//
// It dispatches through `thread_call_handler`, so it inherits the decline that
// `actor_collide_notify` and `sprite_build_oam` already have: **false means some
// actor in the box has a handler the port does not**, and the harness gives the
// whole call back to the ROM. A decline may leave `w` partly written, because
// the records before it in the walk have been told and the ROM would have told
// them too.
//
// Every register is claimed. They are all leftovers of the last record the walk
// looked at, which is the sort of thing this project usually declines to claim
// -- but four of the five call sites read return immediately, so "it is dead"
// would be a guess about the caller's caller rather than a fact, and 9,784
// calls is enough for the harness to settle it either way.
static bool guard_actor_notify_box(Wram* scratch, const Rom* rom,
                                   const CosimRegs* in) {
  ThreadCallResult tail = {.c = in->c};
  ActorNotifyRegs r;
  return actor_notify_box(scratch, rom, in->a, in->c, &tail, &r);
}

// What each run of `$80:BF1B` costs, indexed by `ActorNotifyBlock`. The whole
// routine is 76 bytes of listing and 14 direct-page instructions, and every
// block below is a straight run between two branches of it.
//
// The seven per-record blocks are nested prefixes of one another — each is the
// one above it plus the instructions that ask the next question — which is why
// they climb 92, 132, 206, 246, 320, 360, 572 rather than being independent
// numbers. A transcription error in any one of them therefore shows up as a
// broken spacing, and the spacings are **40, 74, 40, 74, 40** and then the
// dispatch preamble's 212.
//
// The alternation is not decoration. A test that can reuse what is already in A
// costs `CMP` + the branch's six = 40; one that has to fetch a fresh word from
// the record first costs `LDA $xx,X` on top of that = 74. So the id test and
// the two *upper* bounds are 40, and the two *lower* bounds — which open each
// axis — are 74. Any table where those five numbers are not in that order has
// an instruction in the wrong block.
//
// `NOTIFY_BLK_HIT` carries `JSL $808480`'s own 54 and not a cycle of what it
// dispatches into: that is `thread_call_cycles`, summed per hit below, exactly
// as `OVL_HIT` carries `JSR $BE8F` and leaves the rest to `notify_cycles`.
static const CosimRun NOTIFY_BOX_COST[NOTIFY_BLOCK_COUNT] = {
    [NOTIFY_BLK_PROLOGUE] = {28 + 34 + 34 + 18, 8},
    [NOTIFY_BLK_BOUND_KEPT] = {34 + 18 + 12 + 12, 6, 1},
    [NOTIFY_BLK_BOUND_CLAMPED] = {34 + 12 + 34 + 12 + 12, 8, 2},
    [NOTIFY_BLK_BOUND_NEXT] = {18, 2},
    [NOTIFY_BLK_BOUND_DONE] = {12, 2},
    // `LDY $9C : BEQ` taken, and the shared `PLD : RTL` at `$80:BF65`.
    [NOTIFY_BLK_NO_ACTORS] = {28 + 18 + 34 + 42, 6, 1},
    // ...not taken, then `DEY DEY : BEQ` taken into the same two instructions.
    [NOTIFY_BLK_ONE_ACTOR] = {28 + 12 + 12 + 12 + 18 + 34 + 42, 10, 1},
    // ...and not taken either, which is the walk.
    [NOTIFY_BLK_WALK] = {28 + 12 + 12 + 12 + 12, 8, 1},
    // $80:BF35 LDX $137E,Y : LDA $0E,X : BEQ taken.
    [NOTIFY_BLK_NO_ID] = {40 + 34 + 18, 7, 1},
    // ...not taken, then `CMP $40 : BEQ` taken.
    [NOTIFY_BLK_SELF_ID] = {40 + 34 + 12 + 28 + 18, 11, 2},
    // ...not taken, then `LDA $02,X : CMP $38 : BCC` taken.
    [NOTIFY_BLK_LEFT_OF] = {40 + 34 + 12 + 28 + 12 + 34 + 28 + 18, 17, 4},
    // ...not taken, then `CMP $3A : BCS` taken.
    [NOTIFY_BLK_RIGHT_OF] = {40 + 34 + 12 + 28 + 12 + 34 + 28 + 12 + 28 + 18,
                             21, 5},
    // ...not taken, then `LDA $06,X : CMP $3C : BCC` taken.
    [NOTIFY_BLK_ABOVE] = {40 + 34 + 12 + 28 + 12 + 34 + 28 + 12 + 28 + 12 + 34 +
                              28 + 18,
                          27, 7},
    // ...not taken, then `CMP $3E : BCS` taken.
    [NOTIFY_BLK_BELOW] = {40 + 34 + 12 + 28 + 12 + 34 + 28 + 12 + 28 + 12 + 34 +
                              28 + 12 + 28 + 18,
                          31, 8},
    // ...not taken: inside the box. `PHY : LDA $0C,X : STX $78 : TAX :
    // LDY $40 : JSL $808480 : PLY`.
    [NOTIFY_BLK_HIT] = {40 + 34 + 12 + 28 + 12 + 34 + 28 + 12 + 28 + 12 + 34 +
                            28 + 12 + 28 + 12 + 28 + 34 + 28 + 12 + 28 + 54 +
                            34,
                        44, 11},
    // $80:BF61 DEY DEY : BPL taken.
    [NOTIFY_BLK_LOOP_NEXT] = {12 + 12 + 18, 4},
    // ...not taken, and the `PLD : RTL` the early exits share.
    [NOTIFY_BLK_LOOP_DONE] = {12 + 12 + 12 + 34 + 42, 6},
};

// False when a hit in this box entered a handler with no cost table, or when
// there were more hits than the array can describe — the same all-or-nothing
// `overlap_cycles` makes, for the same reason.
static bool notify_box_cycles(const ActorNotifyWork* k, const CosimRegs* in,
                              int* out) {
  bool fast = in->fastrom;
  // The routine forces its own page to zero at `$80:BF1D PEA $0000 : PLD`
  // before it touches a direct-page word, so the caller's `D` cannot make any
  // of the fourteen cost an extra idle — which is why, alone among the models
  // here, this one takes no `dp_unaligned` and its shims check no `in->d`.
  int cycles = 0;
  for (int i = 0; i < NOTIFY_BLOCK_COUNT; i++)
    cycles += k->blocks[i] * cosim_run_cycles(&NOTIFY_BOX_COST[i], fast);

  if (k->hits > NOTIFY_BOX_MAX_PRICED_HITS) return false;
  for (int i = 0; i < k->hits; i++) {
    int call;
    if (!thread_call_cycles(&k->call[i], in, &call)) return false;
    cycles += call;
  }
  *out = cycles;
  return true;
}

static void shim_actor_notify_box(Wram* w, const Rom* rom, const CosimRegs* in,
                                  CosimRegs* out) {
  ThreadCallResult tail = {.c = in->c};
  ActorNotifyRegs r;
  ActorNotifyWork work;
  actor_notify_box_counted(w, rom, in->a, in->c, &tail, &r, &work);

  int cycles;
  if (notify_box_cycles(&work, in, &cycles)) cosim_cost(cycles);

  out->a = r.a;
  out->x = r.x;
  out->y = r.y;
  out->c = r.c;
  out->n = (in->d & 0x8000u) != 0;  // the closing `PLD`
  out->z = in->d == 0;
  out->flags = COSIM_FLAG_N | COSIM_FLAG_Z | COSIM_FLAG_C;
}

// ---------------------------------------------------------------------------
// $80:B3F1  actor_snap_to -- X, Y = two records; X moved onto Y if it is close
// ---------------------------------------------------------------------------
//
// `actor_aligned` finds something lined up to within a tile; this closes the
// last pixel of it, per axis, and it is the one routine in the registry with no
// `PHD` whose flags are therefore its own. The X axis runs first and everything
// it leaves is overwritten by the Y axis, so what the caller gets back describes
// Y alone: carry **set** means Y did not snap.
static void shim_actor_snap_to(Wram* w, const Rom* rom, const CosimRegs* in,
                               CosimRegs* out) {
  (void)rom;
  ActorSnapRegs r;
  actor_snap_to(w, in->x, in->y, &r);
  out->a = r.a;
  out->x = in->x;  // both are indices; neither is written
  out->y = in->y;
  out->n = r.n;
  out->z = r.z;
  out->c = r.c;
  out->flags = COSIM_FLAG_N | COSIM_FLAG_Z | COSIM_FLAG_C;
}

// ---------------------------------------------------------------------------
// $82:8014 / $82:8069  boss_bg_queue -- no arguments; twenty DMA jobs
// ---------------------------------------------------------------------------
//
// The big-figure blitter, and a pair in the same shape as the four scroll
// routines: two entries that differ in one thing, here whether the figure is
// mirrored on the way to VRAM.
//
// Neither takes a register argument -- everything comes out of direct page,
// which they force to zero themselves -- and neither *produces* one either.
// Both end on `LDA #$81C9 : LDY #$0082 : JSL $8083AE : PLD : RTL`, so all
// three registers and the carry belong to `vbl_queue_a_add` and say only
// whether the vblank queue had room, and N and Z are the caller's direct page
// off the closing `PLD`. That leaves the entire result of a 2,746-instruction
// routine in WRAM, which is the easiest kind of routine to check and the
// reason these two passed first run.
//
// The guards differ, because what the two routines read differs. The plain one
// only ever reads the four header bytes; the mirrored one reads all 560 and
// stages a flipped copy, so its guard has to ask about the whole figure.
static bool guard_boss_bg_queue(Wram* scratch, const Rom* rom,
                                const CosimRegs* in) {
  (void)in;
  if (boss_bg_queue_supported(scratch, rom)) return true;
  cosim_census_note("figure header unreadable", BOSS_BG_QUEUE_ENTRY);
  return false;
}

static bool guard_boss_bg_queue_flip(Wram* scratch, const Rom* rom,
                                     const CosimRegs* in) {
  (void)in;
  if (boss_bg_queue_flip_supported(scratch, rom)) return true;
  cosim_census_note("figure unreadable", BOSS_BG_QUEUE_FLIP_ENTRY);
  return false;
}

static void boss_bg_out(const BossBgRegs* r, const CosimRegs* in,
                        CosimRegs* out) {
  out->a = r->a;
  out->x = r->x;
  out->y = r->y;
  out->n = (in->d & 0x8000u) != 0;  // the closing `PLD`
  out->z = in->d == 0;
  out->c = r->c;  // ...and `vbl_queue_a_add`'s own `CPY #$0010`, untouched
                  // by everything between it and the `RTL`
  out->flags = COSIM_FLAG_N | COSIM_FLAG_Z | COSIM_FLAG_C;
}

static void shim_boss_bg_queue(Wram* w, const Rom* rom, const CosimRegs* in,
                               CosimRegs* out) {
  BossBgRegs r;
  boss_bg_queue(w, rom, &r);
  boss_bg_out(&r, in, out);
}

static void shim_boss_bg_queue_flip(Wram* w, const Rom* rom,
                                    const CosimRegs* in, CosimRegs* out) {
  BossBgRegs r;
  boss_bg_queue_flip(w, rom, &r);
  boss_bg_out(&r, in, out);
}

// ---------------------------------------------------------------------------
// $82:8F93  boss_step — the direction in `$16`, `#$6969` in A for double speed
// ---------------------------------------------------------------------------
//
// A `JSR` from the boss thread, so the direct page is the caller's and the shim
// hands it over rather than assuming — even though every call site reaches the
// figure's position at a fixed `$1E62` and the thread's `D` has been zero every
// time the harness has looked.
//
// N and Z are the exit compare's and A is not: `CMP` does not write the
// accumulator, so the routine returns the coordinate it loaded while the flags
// describe the difference. X and Y are `terrain_blocked_wide`'s leftovers from
// the last probe the pass loop ran, and there is always one — the loop tests
// its counter at the bottom.
static void shim_boss_step(Wram* w, const Rom* rom, const CosimRegs* in,
                           CosimRegs* out) {
  BossStepRegs r;
  boss_step(w, rom, in->d, in->a, &r);
  out->a = r.a;
  out->x = r.x;
  out->y = r.y;
  out->n = r.n;
  out->z = r.z;
  out->c = r.c;
  out->flags = COSIM_FLAG_N | COSIM_FLAG_Z | COSIM_FLAG_C;
}

// ---------------------------------------------------------------------------
// $82:9265, $82:92D6  boss_place_parts, boss_stomp — the same two words again
// ---------------------------------------------------------------------------
//
// `boss_step` moves `$1E62`/`$1E64`; these two are what the same thread does
// with them on the next two instructions. Neither takes a register argument.
//
// `boss_place_parts` reads four record pointers off the direct page and one
// mirror flag, and writes eight words. Its V is that of the `ADC` its flags
// come from and is not claimed — the caller's next instruction is another
// `JSR`, and no exit of this routine has ever had a reader for it.
static void shim_boss_place_parts(Wram* w, const Rom* rom, const CosimRegs* in,
                                  CosimRegs* out) {
  BossPartsRegs r;
  boss_place_parts(w, rom, in->d, &r);
  out->a = r.a;
  out->x = r.x;
  out->y = r.y;
  out->n = r.n;
  out->z = r.z;
  out->c = r.c;
  out->flags = COSIM_FLAG_N | COSIM_FLAG_Z | COSIM_FLAG_C;
}

// `boss_stomp` is five stores and a `JSL`, so it inherits `actor_notify_box`'s
// decline whole — including the part where a declined call may already have
// told some of the actors in the box. The guard has to build the box before it
// can ask, because what the walk finds is what decides the answer; it does that
// to the harness's scratch copy, which is thrown away either way.
//
// **Neither `in->a` nor `in->c` is passed through**, and that is the whole of
// what this shim gets wrong if it is written the obvious way: the `JSL` hands
// `actor_notify_box` an A and a carry that `$82:92D6` made itself, three and
// two instructions earlier. Passing the caller's A instead fails on call 1 of
// `level25-lane` with `A: ROM $000A, port $021D` — `$000A` being the id the
// routine had just loaded. See `port/boss.h`.
static bool guard_boss_stomp(Wram* scratch, const Rom* rom,
                             const CosimRegs* in) {
  (void)in;
  return boss_stomp_supported(scratch, rom);
}

// Everything `$82:92D6` does that is not `actor_notify_box`: `$82:92D6`
// through `$82:92FB STA $0040` is 376 over 40 bytes, then `JSL $80BF1B` at 54
// and the `RTS` at 40. **All five stores are absolute** — `STA $0038` and not
// `STA $38` — because the routine that reads them forces `D` to zero itself, so
// there is no direct-page column here and none is owed.
//
// **470 + `actor_notify_box`'s own floor of 642 is 1,112, and 1,112 is the
// number already written in this routine's registry entry below** — recorded as
// its measured minimum over 6,549 calls, rounds before either half of that sum
// existed. Neither figure was fitted to the other: one is the corpus reporting
// the cheapest call it ever saw, the other is two listings added up. The
// cheapest call is a boss standing on a board holding one visible actor, and it
// costs 1,112 cycles with no refresh in it at all.
static const CosimRun BOSS_STOMP_COST = {376 + 54 + 40, 45};

static void shim_boss_stomp(Wram* w, const Rom* rom, const CosimRegs* in,
                            CosimRegs* out) {
  BossStompRegs r;
  ActorNotifyWork work;
  boss_stomp_counted(w, rom, &r, &work);

  int cycles;
  if (notify_box_cycles(&work, in, &cycles))
    cosim_cost(cycles + cosim_run_cycles(&BOSS_STOMP_COST, in->fastrom));

  out->a = r.a;
  out->x = r.x;
  out->y = r.y;
  out->c = r.c;
  // The `PLD` inside `actor_notify_box`, restoring the page this routine was
  // called on — so two of the three flags describe the boss thread and not the
  // box.
  out->n = (in->d & 0x8000u) != 0;
  out->z = in->d == 0;
  out->flags = COSIM_FLAG_N | COSIM_FLAG_Z | COSIM_FLAG_C;
}

// ---------------------------------------------------------------------------
// $80:ACF6  blockmap_cell_ptr — X = column, Y = row
// ---------------------------------------------------------------------------
//
// No `PHD`, so the direct page is the caller's and the shim hands it over. Ten
// call sites in four banks reach it and this registry has one of their callers,
// so it will be a both-sides row for a while yet.
static void shim_blockmap_cell_ptr(Wram* w, const Rom* rom, const CosimRegs* in,
                                   CosimRegs* out) {
  (void)rom;
  BlockCellRegs r;
  blockmap_cell_ptr(w, in->d, in->x, in->y, &r);
  out->a = r.a;
  out->x = r.x;
  out->y = in->y;  // never touched
  out->n = r.n;
  out->z = r.z;
  out->c = r.c;
  out->flags = COSIM_FLAG_N | COSIM_FLAG_Z | COSIM_FLAG_C;
}

static bool guard_blockmap_cell_ptr(Wram* scratch, const Rom* rom,
                                    const CosimRegs* in) {
  (void)rom;
  return blockmap_cell_ptr_supported(scratch, in->d);
}

// ---------------------------------------------------------------------------
// $80:AD2B  blockmap_expand — ten frames, and the second routine `verify`
// structurally cannot check
// ---------------------------------------------------------------------------
//
// This entry stood as a comment saying the routine could not be registered, for
// the same reason `$80:C05A` did: **one call is about ten frames long, so
// `verify` reported one call, one interruption and nothing checked** on every
// movie tried — `level1`, `level1-rescue`, `level9`, `level25-boss` and
// `level53` alike. There was no movie on which a call completed inside a frame
// and there cannot be.
//
// That is a statement about rewind-and-replay and not about substitution, which
// is the distinction `CosimRoutine::run_only` exists to make. Under `run` the
// ROM never executes any of it, so there is no call window for an NMI to land
// inside; what checks the port is `run`'s comparison of all 128 KB at the next
// scheduler pass, and every pass after it for the rest of the movie. The map
// this builds is the map the whole level is played on, so a port that got one
// cell wrong would be visible in the very next frame the camera drew.
//
// **It is also what the parked burn was built for.** Ten frames is ten NMIs, and
// the core latches one — see `CosimBurn` in `cosim.c`. `$80:C05A` fits inside a
// frame and so never needed it; this does not, and `run` reports the parks.
//
// ## Why the cost is a count and not a mean
//
// `run_only` requires it — a mean is something `verify` watched, and `verify`
// has never seen this routine finish. It is available here because **no branch
// in the routine depends on the data it reads.** The nest is
// `$B0` rows x `$AE >> 1` cells x 8 block rows x 8 words, and every trip count
// falls out of the level record. So the port counts the four loops and the two
// bank questions (`BlockExpandWork`) and this prices them, exactly as a hot
// dispatcher's branch outcomes are priced — the difference being that here the
// answer is exact rather than an average.
//
// Level 1 is 22 x 13 = **286 cells, which is exactly the 286 calls `verify`
// measures `blockmap_cell_ptr` taking** on `level1.zmv`: the shape of this model
// is confirmed by a count the harness makes independently of it. And that
// routine's own measured floor, 334 master cycles, is to the cycle what
// `tools/cycles816.py` prices its 21 bytes at — so the same tool that priced the
// rest of this table is checked against a real measurement inside this very
// call. Level 1 comes out at 3,604,294 cycles, 10.09 frames.
//
// ## ...and it was checked against the ROM anyway
//
// `verify` cannot score this call, but that is a limit of rewind-and-replay and
// not of the clock, so the ROM's own execution was timed directly: the cycles
// spent with the program counter inside `$80:ACF6..$80:AD91` on `level1.zmv`,
// with the eleven interrupts that land in the middle attributed to the handler
// where they belong. **3,713,172 cycles**, less the 2,722 DRAM refreshes the
// core adds at 40 apiece, is **3,604,292** — two cycles under this model, on a
// figure of three and a half million.
//
// Two is not zero, and the difference is the probe's rather than the model's: a
// `snes_runCycle` granule is two master cycles, so attributing a step to one
// side or the other of the boundary is worth exactly this much. It is stated as
// a measurement and not as the refresh-exact residual the per-call models are
// held to, because that standard needs `verify`, and `verify` is the thing this
// routine cannot have.
//
// ## The blocks
//
// Two of them come in pairs because the block map's bank is level data — `$A8`
// out of the level record, WRAM on some levels and cartridge on others — and a
// cartridge operand byte costs 6 rather than 8 and moves into the FastROM
// column. The block *library* is `$7E` at every call site the ROM has, but the
// port checks rather than trusting it, so the copy loop carries the same pair;
// `words_rom` is expected to stay zero and the report will say so if it does not.
static const CosimRun BLOCK_PROLOGUE = {208, 13};   // $AD2B..$AD37, once
static const CosimRun BLOCK_ROW_HEAD = {68, 5};     // $AD38..$AD3C, per row
static const CosimRun BLOCK_ROW_TAIL = {140, 8};    // $AD88..$AD8F, BNE not taken
static const CosimRun BLOCK_CELL_WRAM = {514, 40};  // $AD3D..$AD64
static const CosimRun BLOCK_CELL_ROM = {510, 42};   // ...map in the cartridge
static const CosimRun BLOCK_TILE_ADDR = {250, 15};  // $80:AD1C, called per cell
static const CosimRun BLOCK_CELL_PTR = {334, 21};   // $80:ACF6, likewise
static const CosimRun BLOCK_LIB_PTR = {250, 17};    // $80:AD0B, likewise
static const CosimRun BLOCK_ROW_START = {18, 3};    // $AD65..$AD67, per block row
static const CosimRun BLOCK_WORD_WRAM = {140, 8};   // $AD68..$AD6F, BPL not taken
// The same eight bytes with the library in the cartridge: the two operand bytes
// of `LDA [$28],Y` cost 6 rather than 8, and they join the byte column. Derived
// rather than measured, and the pair above is the check on the derivation —
// `cycles816.py` prices the cell head at exactly this difference, -4 and +2.
static const CosimRun BLOCK_WORD_ROM = {136, 10};
static const CosimRun BLOCK_ROW_ADVANCE = {206, 18};  // $AD70..$AD81, BNE not taken
static const CosimRun BLOCK_CELL_TAIL = {112, 6};     // $AD82..$AD87, BNE not taken
static const CosimRun BLOCK_EPILOGUE = {76, 2};       // $AD90..$AD91, the RTL too

// A branch this routine takes rather than falls through. Every one of the four
// is a `DEC`/`DEY` loop closing, so how often each is taken follows from the
// trip counts and is not counted separately — see `BlockExpandWork`.
#define BLOCK_TAKEN 6

static int blockmap_cycles(const BlockExpandWork* k, bool fast) {
  const uint32_t cells_wram = k->cells - k->cells_rom;
  const uint32_t words_wram = k->words - k->words_rom;
  long cycles = cosim_run_cycles(&BLOCK_PROLOGUE, fast) +
                cosim_run_cycles(&BLOCK_EPILOGUE, fast);

  cycles += (long)k->rows * (cosim_run_cycles(&BLOCK_ROW_HEAD, fast) +
                             cosim_run_cycles(&BLOCK_ROW_TAIL, fast));
  cycles += (long)k->cells * (cosim_run_cycles(&BLOCK_TILE_ADDR, fast) +
                              cosim_run_cycles(&BLOCK_CELL_PTR, fast) +
                              cosim_run_cycles(&BLOCK_LIB_PTR, fast) +
                              cosim_run_cycles(&BLOCK_CELL_TAIL, fast));
  cycles += (long)cells_wram * cosim_run_cycles(&BLOCK_CELL_WRAM, fast);
  cycles += (long)k->cells_rom * cosim_run_cycles(&BLOCK_CELL_ROM, fast);
  cycles += (long)k->block_rows * (cosim_run_cycles(&BLOCK_ROW_START, fast) +
                                   cosim_run_cycles(&BLOCK_ROW_ADVANCE, fast));
  cycles += (long)words_wram * cosim_run_cycles(&BLOCK_WORD_WRAM, fast);
  cycles += (long)k->words_rom * cosim_run_cycles(&BLOCK_WORD_ROM, fast);

  // ...and the four loop-backs. Each loop takes its branch on every trip but
  // the one that ends it, so the count is the difference between the trips and
  // the trips of the loop outside it — and the outermost is `rows - 1`.
  cycles += (long)BLOCK_TAKEN *
            ((long)(k->words - k->block_rows) +
             (long)(k->block_rows - k->cells) + (long)(k->cells - k->rows) +
             (long)(k->rows ? k->rows - 1 : 0));
  return (int)cycles;
}

static void shim_blockmap_expand(Wram* w, const Rom* rom, const CosimRegs* in,
                                 CosimRegs* out) {
  BlockExpandRegs r;
  BlockExpandWork work;
  blockmap_expand_counted(w, rom, &r, &work);
  out->a = r.a;
  out->x = r.x;
  out->y = r.y;
  out->c = r.c;
  // The closing `PLD` restores the caller's page, so N and Z describe *that*
  // and not the count — the same trap every `PHD` routine in this file sets.
  out->n = (in->d & 0x8000u) != 0;
  out->z = in->d == 0;
  out->flags = COSIM_FLAG_N | COSIM_FLAG_Z | COSIM_FLAG_C;
  cosim_cost(blockmap_cycles(&work, in->fastrom));
}

static bool guard_blockmap_expand(Wram* scratch, const Rom* rom,
                                  const CosimRegs* in) {
  (void)in;
  return blockmap_expand_supported(scratch, rom);
}

// ---------------------------------------------------------------------------
// $80:C07F, $80:C0A3, $80:C139  the status panel
// ---------------------------------------------------------------------------

// Three entries for a tree of twenty routines, and the reason there are three
// rather than one is that the two panels have callers `$80:C07F` does not.
// `$80:C1D1`, `$80:C1F1`, `$80:C1F5` and `$80:C215` — the screen transitions —
// reach them directly, so registering only the top would leave those four sites
// running the ROM and checking nothing.
//
// All three take the caller's direct page, because `$1E`, `$20`, `$22` and `$24`
// are scratch on whatever thread's page is current. `$80:C07F` is a `JSL`
// target from three banks; the two panels are `JSR`ed from bank `$80` only.
//
// Carry is claimed on all three, and it is the flag that took the work. Nothing
// in the cluster returns anything in it, but there is no path through a panel
// that leaves it alone: a `CMP` against a shadow sets it six times over, and
// below that so do the shifts that build a table index and the `ROR $1E` inside
// every printed digit. See `port/hud.h`.
// What each straight-line run of the HUD tree costs the 65816, indexed by
// `HudBlock`. Every number came out of `tools/cycles816.py`, which prices the
// listing rather than being told what it costs; the whole table is checked
// against the ROM on every call, and the cost-model report says so.
//
// The tree is sixteen routines but only five shapes of cost: the refresh's own
// two branches, the panel's six comparisons, three adapters that are three
// instructions each, four drawing routines whose loops are all counted rather
// than terminated, and one digit — which is where the variation actually lives,
// eleven of them per full redraw at three different prices.
static const CosimRun HUD_BLOCK_COST[HUD_BLOCK_COUNT] = {
    // $80:C07F. P1 costs the extra `BRA $C090` on the way back from the panel;
    // P2 costs the taken `BNE` instead. Both include the `JSR`.
    [HUD_BLK_REFRESH_P1] = {28 + 18 + 28 + 12 + 40 + 18, 14},
    [HUD_BLK_REFRESH_P2] = {28 + 18 + 28 + 18 + 40, 12},
    [HUD_BLK_REFRESH_IDLE] = {34 + 18 + 42, 6},
    [HUD_BLK_REFRESH_QUEUE] = {34 + 12 + 34 + 18 + 18 + 24, 18},
    // $80:83AE, tail-jumped into. `ACCEPTED` is the prologue, the compare that
    // found a free slot and the whole of the store; `BUSY` is one turn of the
    // search that did not; `FELL` is all fifteen turns and no free slot, which
    // ends on a `BNE` that falls through rather than a `BEQ` that is taken.
    [HUD_BLK_QUEUE_FULL] = {28 + 34 + 18 + 18 + 34 + 42, 11},
    [HUD_BLK_QUEUE_ACCEPTED] = {110 + 58 + 248, 12 + 5 + 14},
    [HUD_BLK_QUEUE_BUSY] = {40 + 12 + 48 + 18, 11},
    [HUD_BLK_QUEUE_FELL] = {110 + 13 * 118 + 112 + 248, 12 + 13 * 11 + 11 + 14},
    // $80:C0A3 and $80:C139, which are the same code and so the same price.
    [HUD_BLK_PANEL_OFF] = {34 + 12 + 40, 6},
    [HUD_BLK_PANEL_ON] = {34 + 18, 5},
    [HUD_BLK_PANEL_RTS] = {40, 1},
    // `LDA : CMP long : BEQ`, then the store, the `JSR` and the `INC $1E7A`.
    [HUD_BLK_FIELD_SAME] = {34 + 40 + 18, 9},
    [HUD_BLK_FIELD_CHANGED] = {34 + 40 + 12 + 40 + 40 + 56, 19},
    // ...the same with `LDA : ASL : TAX : LDA table,X` in front of the compare.
    [HUD_BLK_COUNT_SAME] = {34 + 12 + 12 + 40 + 40 + 18, 14},
    [HUD_BLK_COUNT_CHANGED] = {34 + 12 + 12 + 40 + 40 + 12 + 40 + 40 + 56, 24},
    // The score: two compares, and three ways out of them.
    [HUD_BLK_SCORE_SAME] = {34 + 40 + 12 + 34 + 40 + 18, 18},
    [HUD_BLK_SCORE_LOW] = {34 + 40 + 18, 9},
    [HUD_BLK_SCORE_HIGH] = {34 + 40 + 12 + 34 + 40 + 12, 18},
    [HUD_BLK_SCORE_TAIL] = {40 + 34 + 40 + 34 + 40 + 56, 20},
    // $80:C6E4 .. $80:C7AB.
    [HUD_BLK_ADAPT_SCORE] = {18 + 18 + 18, 9},
    [HUD_BLK_ADAPT_HEALTH] = {18 + 18, 6},
    [HUD_BLK_ADAPT_COUNT_NONE] = {18 + 34 + 18 + 18, 11},
    [HUD_BLK_ADAPT_COUNT_OVER] = {18 + 34 + 12 + 18 + 18 + 18, 16},
    [HUD_BLK_ADAPT_COUNT_SHOWN] =
        {18 + 34 + 12 + 18 + 12 + 12 + 12 + 18 + 12 + 18, 22},
    [HUD_BLK_ADAPT_ICON_SHOWN] = {34 + 12 + 18 + 18, 11},
    // The blank path is the long one: it clears the icon *and* the count beside
    // it, so it pays for two `hud_blank`s as well as these six instructions.
    [HUD_BLK_ADAPT_ICON_NONE] = {34 + 18 + 18 + 40 + 18 + 18, 17},
    // $80:C580: one `LDA #$0000` and six stores.
    [HUD_BLK_BLANK] = {18 + 6 * 40 + 40, 28},
    // $80:C59C and the $80:C379 it jumps into — ten tiles out of a table.
    [HUD_BLK_HEALTH] = {326 + 1248, 26 + 101},
    // $80:C5C2 and $80:C666: four tiles each, and one `LDY $20` apart.
    [HUD_BLK_ICON_WEAPON] = {308 + 340, 24 + 28},
    [HUD_BLK_ICON_ITEM] = {280 + 340, 22 + 28},
    // $80:C4EC. The common tail — store, `INX INX`, `RTS` — is 104 over 7
    // bytes, and the print's `CLC : ADC : SEC : ROR $1E` is 92 over 7.
    [HUD_BLK_DIGIT_NONZERO] = {18 + 92 + 104, 2 + 7 + 7},
    [HUD_BLK_DIGIT_LEAD] = {12 + 28 + 18 + 92 + 104, 6 + 7 + 7},
    [HUD_BLK_DIGIT_BLANK] = {12 + 28 + 12 + 18 + 18 + 104, 11 + 7},
    // The `BIT $1E` both renderers end on, and the forced zero when nothing
    // printed.
    [HUD_BLK_DIGITS_END_PRINTED] = {28 + 18, 4},
    [HUD_BLK_DIGITS_END_ZERO] = {28 + 12 + 18 + 40, 11},
    // $80:C519 and $80:C553 with their digits taken out. The score's includes
    // the two dead instructions at `$80:C526` — the port does not have them
    // because they leave nothing behind, but the machine still pays for them.
    [HUD_BLK_DIGITS8] = {128 + 80 + 1290 + 40, 13 + 6 + 4 * 27 + 1},
    [HUD_BLK_DIGITS3] = {410, 34},
};

static int hud_cycles(const HudWork* k, bool fast) {
  int cycles = 0;
  for (int i = 0; i < HUD_BLOCK_COUNT; i++)
    cycles += k->blocks[i] * cosim_run_cycles(&HUD_BLOCK_COST[i], fast);
  return cycles;
}

// Two things about the caller decide prices the table above does not carry, so
// a call that presents either goes unpriced and falls back to the declared mean
// — visibly, as a `priced` below `checked` in the cost-model report.
//
// **The direct page has to be page-aligned.** Every routine in the cluster
// scratches `$1E`, `$20`, `$22` and `$24` on the caller's, and a direct page
// with a low byte costs one extra internal cycle on each of those.
//
// **The data bank has to be a slow one, and this is the assumption that was
// wrong first time round.** A LoROM cartridge appears twice and only the `$80`+
// copy is fast, so `$80:C5A3 LDA $C5B6,X` — an instruction in bank $80 reading a
// table through the *data bank* — costs 40 cycles through bank $00 and 36
// through bank $80. Every caller in the corpus leaves a low bank there, the
// three-instruction difference is real, and it showed up as exactly three calls
// out of 233 whose model was short by 12.
static void hud_report_cost(const CosimRegs* in, const HudWork* work) {
  if ((in->d & 0xff) == 0 && in->db < 0x80)
    cosim_cost(hud_cycles(work, in->fastrom));
}

static void shim_hud_panel1(Wram* w, const Rom* rom, const CosimRegs* in,
                            CosimRegs* out) {
  HudWork work = {0};
  HudPanelRegs r = {.a = in->a, .x = in->x, .y = in->y,
                    .n = in->n, .z = in->z, .c = in->c, .work = &work};
  hud_panel(w, rom, in->d, HUD_SIDE_P1, &r);
  hud_report_cost(in, &work);
  out->a = r.a;
  out->x = r.x;
  out->y = r.y;
  out->n = r.n;
  out->z = r.z;
  out->c = r.c;
  out->flags = COSIM_FLAG_N | COSIM_FLAG_Z | COSIM_FLAG_C;
}

static void shim_hud_panel2(Wram* w, const Rom* rom, const CosimRegs* in,
                            CosimRegs* out) {
  HudWork work = {0};
  HudPanelRegs r = {.a = in->a, .x = in->x, .y = in->y,
                    .n = in->n, .z = in->z, .c = in->c, .work = &work};
  hud_panel(w, rom, in->d, HUD_SIDE_P2, &r);
  hud_report_cost(in, &work);
  out->a = r.a;
  out->x = r.x;
  out->y = r.y;
  out->n = r.n;
  out->z = r.z;
  out->c = r.c;
  out->flags = COSIM_FLAG_N | COSIM_FLAG_Z | COSIM_FLAG_C;
}

static void shim_hud_refresh(Wram* w, const Rom* rom, const CosimRegs* in,
                             CosimRegs* out) {
  // A is not an input: `LDA $24` overwrites it before anything reads it. X, Y
  // and carry are, because the quiet paths hand all three straight back.
  HudWork work = {0};
  HudRefreshRegs r = {.x = in->x, .y = in->y, .c = in->c, .work = &work};
  hud_refresh(w, rom, in->d, &r);
  hud_report_cost(in, &work);
  out->a = r.a;
  out->x = r.x;
  out->y = r.y;
  out->n = r.n;
  out->z = r.z;
  out->c = r.c;
  out->flags = COSIM_FLAG_N | COSIM_FLAG_Z | COSIM_FLAG_C;
}

// ---------------------------------------------------------------------------
// $80:CB61 and $80:CB1A — the SPC700 upload, and why neither is registered
// ---------------------------------------------------------------------------
//
// `$80:CB61 apu_ipl_upload` is the largest registerable-looking row the ranking
// has left: 4,351,919 instructions over eleven calls, 1.5%, with another
// 6,496,644 of the machine waiting inside it. `$80:CB1A apu_boot` is the `MVN`
// pair and the four port writes around it, and it calls `$CB61`.
//
// Neither is here, and there are two independent reasons — one measured, one
// structural.
//
// **Measured.** Both were registered temporarily against an empty shim, which
// is enough to read the harness's own verdict because `$80:CB61` writes no WRAM
// at all: `1 call, 1 interrupted, 0 checked` on `boot.zmv`, where the screen is
// off and NMI has the best chance of being disabled, and the same on
// `level25-lane`. About 986,000 instructions per call is many frames, and an
// interrupt lands in every one.
//
// **Structural, and the one that would still apply if the frame problem went
// away.** The routine's entire observable effect is on the SPC700, through
// `$2140`-`$2143`, one byte at a time and gated on the SPC's replies. There is
// nothing in WRAM for a diff to compare, and a substituted port would have to
// drive the real handshake through the host — which is what `apu_send` does,
// and why `apu_send` is `verify_only` and can never be substituted. `$80:CB61`
// could at best be the same, on a call the harness cannot reach the end of.
//
// `tools/native_share.py` carries both verdicts in `BLOCKED`.

// ---------------------------------------------------------------------------
// The registry
// ---------------------------------------------------------------------------

// `cycles` is the mean cost of the ROM's own instructions and `stack_bytes` the
// deepest its stack pointer went, both as `zamn_cosim verify` measured them over
// **movies/level1-rescue.zmv** — the longer of the two movies, and the only one
// that produces a collision at all. `cycles` is what a substituted call burns in
// native mode so the rest of the machine still sees a call that took about as
// long as it used to.
//
// The whole column moved when the collision handlers were ported, and not
// because the ROM changed: a routine's mean is taken over the calls the port
// *serves*, and the 1,226 passes containing a collision used to be declined.
// They are the expensive ones. Re-measure and update these whenever a guard's
// answer changes; `verify` prints the range it saw alongside the mean.
static const CosimRoutine ROUTINES[] = {
    {
        .name = "sprite_frame_tile",
        .symbol = "$80:B9D6",
        .entry = 0x80b9d6,
        .ret_op = 0x80b9eb,  // the hit path's RTS; either one returns the same
        .ret_kind = COSIM_RTS,
        .run = shim_sprite_frame_tile,
        .excludes = SPRITE_TILE_EXCLUDES,
        .exclude_count = 1,
        .cycles = 302,
        .stack_bytes = 2,   // the `PHA` at $80:BA2B on the miss path
    },
    {
        .name = "sprite_cache_age",
        .symbol = "$80:B9C7",
        .entry = 0x80b9c7,
        .ret_op = 0x80b9d5,
        .ret_kind = COSIM_RTS,
        .run = shim_sprite_cache_age,
        .cycles = 10914,
        .stack_bytes = 0,   // pushes nothing
    },
    {
        .name = "thread_tick_waits",
        .symbol = "$80:8398",
        .entry = 0x808398,
        .ret_op = 0x8083ad,
        .ret_kind = COSIM_RTS,
        .run = shim_thread_tick_waits,
        .cycles = 3204,
        .stack_bytes = 0,   // pushes nothing
    },
    {
        .name = "hud_refresh",
        .symbol = "$80:C07F",
        .entry = 0x80c07f,
        // `$C0A2`, the quiet path's `RTL`. The other exit is a `JML $8083AE`
        // and returns through *that* routine's `RTL` — which is fine, because a
        // return is detected by PC and stack pointer rather than by address,
        // and `ret_op` is only where a substituted call is teleported to.
        .ret_op = 0x80c0a2,
        .ret_kind = COSIM_RTL,
        .run = shim_hud_refresh,
        .cycles = 1367,
        .stack_bytes = 6,  // `JSR` a panel, `JSR` an adapter, `JSR` a digit
    },
    {
        .name = "hud_panel1",
        .symbol = "$80:C0A3",
        .entry = 0x80c0a3,
        .ret_op = 0x80c138,
        .ret_kind = COSIM_RTS,
        .run = shim_hud_panel1,
        .cycles = 1247,
        .stack_bytes = 4,
    },
    {
        .name = "hud_panel2",
        .symbol = "$80:C139",
        .entry = 0x80c139,
        .ret_op = 0x80c1ce,
        .ret_kind = COSIM_RTS,
        .run = shim_hud_panel2,
        // Lower than its twin's only because player 2 is usually absent, and
        // the panel-off exit is eight cycles of work. On a two-player movie the
        // two means are within one percent of each other.
        .cycles = 694,
        .stack_bytes = 4,
    },
    {
        .name = "vbl_queue_a_add",
        .symbol = "$80:83AE",
        .entry = 0x8083ae,
        .ret_op = 0x8083d2,  // RTL
        .ret_kind = COSIM_RTL,
        .run = shim_vbl_queue_a_add,
        .cycles = 743,
        .stack_bytes = 2,   // the opening `PHY`
    },
    {
        .name = "vbl_queue_b_add",
        .symbol = "$80:8418",
        .entry = 0x808418,
        .ret_op = 0x80843a,  // RTL
        .ret_kind = COSIM_RTL,
        .run = shim_vbl_queue_b_add,
        .cycles = 393,
        .stack_bytes = 2,   // the opening `PHY`
    },
    {
        .name = "actor_depth_sort",
        .symbol = "$80:BC7F",
        .entry = 0x80bc7f,
        .ret_op = 0x80bce1,
        .ret_kind = COSIM_RTS,
        .run = shim_actor_depth_sort,
        .excludes = DEPTH_SORT_EXCLUDES,
        .exclude_count = 1,
        .cycles = 1605,
        .stack_bytes = 0,  // pushes nothing
    },
    {
        .name = "actor_cull",
        .symbol = "$80:BCE2",
        .entry = 0x80bce2,
        .ret_op = 0x80bd1e,
        .ret_kind = COSIM_RTS,
        .run = shim_actor_cull,
        .cycles = 2675,
        .stack_bytes = 0,  // pushes nothing
    },
    {
        .name = "oam_buffer_clear",
        .symbol = "$80:BC23",
        .entry = 0x80bc23,
        .ret_op = 0x80bc7e,
        .ret_kind = COSIM_RTS,
        .run = shim_oam_buffer_clear,
        .cycles = 4814,
        .stack_bytes = 2,  // the opening `PHD`
    },
    {
        .name = "thread_spawn",
        .symbol = "$80:825E",
        .entry = 0x80825e,
        .ret_op = 0x8082d7,  // RTL; the full-board path has its own at $82DD
        .ret_kind = COSIM_RTL,
        .run = shim_thread_spawn,
        // 330 calls on `movies/level1-2p.zmv`, spanning 1,558..2,944 — the
        // spread is the slot search, which runs downwards and so costs more the
        // emptier the board is.
        .cycles = 2327,
        .stack_bytes = 4,  // the opening PHD and the PHA under it
    },
    {
        .name = "thread_call_handler",
        .symbol = "$80:8480",
        .entry = 0x808480,
        .ret_op = 0x8084b0,  // RTL; both exits converge on it
        .ret_kind = COSIM_RTL,
        .run = shim_thread_call_handler,
        .supported = guard_thread_call_handler,
        .cycles = 958,
        // 11 until a handler could die: `enemy_die`'s `JSR` and the `JSL` to
        // `score_add` under it are six bytes deeper than anything else reaches.
        .stack_bytes = 19,
    },
    {
        .name = "player_collide",
        .symbol = "$80:F7F7",
        .entry = 0x80f7f7,
        .ret_op = 0x80f807,  // RTL
        .ret_kind = COSIM_RTL,
        .run = shim_player_collide,
        .supported = guard_player_collide,
        .cycles = 515,
        // 2 until the pickup entry was ported; its `JSL apu_play_sfx` and the
        // weapon selector under it are nine bytes deeper than the hit path.
        .stack_bytes = 11,
    },
    {
        .name = "enemy_collide",
        .symbol = "$81:8888",
        .entry = 0x818888,
        // A bare `RTL`, deliberately: the death branch returns carry *set*, and
        // `native_publish` has already put it in the flags by the time the core
        // gets here. Pointing this at the `CLC` two bytes earlier would undo the
        // one output that parks the thread.
        .ret_op = 0x81888e,
        .ret_kind = COSIM_RTL,
        .run = shim_enemy_collide,
        .supported = guard_enemy_collide,
        // 84 for the ignore branch, which is 1,225 of the 1,226; 1,150 for the
        // one death, which is why the mean barely moves off the floor.
        .cycles = 88,
        .stack_bytes = 9,   // the ignore branch pushes nothing; a death, 9
    },
    {
        .name = "monster_collide",
        .symbol = "$81:C4A6",
        .entry = 0x81c4a6,
        // `$81:C50B`, a bare `RTL`, for `enemy_collide`'s reason: three of the
        // four exits set carry themselves and two of those set it, so landing on
        // a `CLC` or a `SEC` would overwrite the answer the port already
        // published. The `CLC` that shares this exit is the byte before.
        .ret_op = 0x81c50b,
        .ret_kind = COSIM_RTL,
        .run = shim_monster_collide,
        .supported = guard_monster_collide,
        // Measured 120..540, mean 135, over 186 calls on
        // `movies/level45-race.zmv`. The floor is the ignore branch, which is
        // 157 of them; the ceiling is taking an object.
        .cycles = 120,
        // 2 observed, and every call so far is an ignore, a latch or a take —
        // none of which nests. Left at the death path's depth, which is the same
        // `JSR` into `$81:BBEB` plus its `JSL score_add` that `enemy_collide`
        // budgets 9 for, because that path is transcribed and will want it.
        .stack_bytes = 9,
    },
    {
        .name = "monster_c440",
        .symbol = "$81:C440",
        .entry = 0x81c440,
        // `$81:C4A5`, this copy's own bare `RTL` — the one two bytes into
        // `$81:C4A4  CLC : RTL`. Not `$81:C50B`: the copies are separate code
        // and pointing one at the other's exit would work by luck and stop
        // working the day either moves.
        .ret_op = 0x81c4a5,
        .ret_kind = COSIM_RTL,
        .run = shim_monster_c440_collide,
        .supported = guard_monster_c440_collide,
        // Measured 120..1322, mean 267, over the 151 calls
        // `movies/level25.zmv` makes — and unlike the spider's, this sample is
        // not all ignores: 115 of them are hits, 105 survivors and 10 deaths.
        // The mean is twice `monster_collide`'s for exactly that reason.
        .cycles = 267,
        .stack_bytes = 9,  // the same death path, budgeted the same way
    },
    {
        .name = "enemy_b41c",
        .symbol = "$81:B41C",
        .entry = 0x81b41c,
        // `$81:B422`, the first of this routine's four bare `RTL`s, chosen for
        // `enemy_collide`'s reason: every exit decides carry for itself — two set
        // it and two clear it — so landing anywhere that runs a `CLC` or a `SEC`
        // would overwrite the answer the port has already published.
        .ret_op = 0x81b422,
        .ret_kind = COSIM_RTL,
        .run = shim_enemy_b41c_collide,
        .supported = guard_enemy_b41c_collide,
        // Measured 84..1312, mean 235, over the 66 calls
        // `movies/level29-fighting.zmv` makes. The floor is the ignore branch and
        // the ceiling is a survivor, whose `JML $81:8506` walks a parked stack.
        .cycles = 235,
        // 2, measured over those 66 — which included a death. This copy's death
        // path is the cheap one: no `JSR $81:8727`, because it awards nothing.
        // The `$61` branch's `JSR $B168` pushes the same 2 when something finally
        // takes it.
        .stack_bytes = 2,
    },
    {
        .name = "enemy_d7f6",
        .symbol = "$81:D7F6",
        .entry = 0x81d7f6,
        // `$81:D7FC`, the ignore path's `RTL`. The death tail sets carry and the
        // two ignore exits clear it, so the same rule as everywhere in this
        // family applies: land on a bare `RTL` and let `native_publish`'s flags
        // stand.
        .ret_op = 0x81d7fc,
        .ret_kind = COSIM_RTL,
        .run = shim_enemy_d7f6_collide,
        .supported = guard_enemy_d7f6_collide,
        // Measured 84..124, mean 86, over the 128 calls `movies/level17.zmv`
        // makes — and every one of them is the ignore branch, so this is the
        // cost of two instructions and nothing else. The damage path will be
        // dearer; it is budgeted low deliberately, because a budget that is too
        // small shows up as a `run` divergence rather than hiding.
        .cycles = 86,
        // The death tail calls nothing; a survivor leaves through `$81:8506`,
        // which pushes nothing either. Budgeted 2 rather than 0 because the one
        // id that declines is the only path with a `JML` this port does not
        // follow, and a budget that is too small fails a call.
        .stack_bytes = 2,
    },
    {
        .name = "enemy_9b6b",
        .symbol = "$81:9B6B",
        .entry = 0x819b6b,
        // `$81:9B71`, the ignore path's `RTL`, for the family's usual reason.
        .ret_op = 0x819b71,
        .ret_kind = COSIM_RTL,
        .run = shim_enemy_9b6b_collide,
        .supported = guard_enemy_9b6b_collide,
        // Measured 84..124, mean 86, over the 33 calls `movies/level21.zmv`
        // makes — all of them the ignore branch, so this is two instructions.
        .cycles = 86,
        // 0 observed, for the same reason. Left at 2, which is what the damage
        // path's `JML` into `$81:8506` will want.
        .stack_bytes = 2,
    },
    {
        .name = "enemy_9063",
        .symbol = "$81:9063",
        .entry = 0x819063,
        .ret_op = 0x819069,  // the ignore path's RTL
        .ret_kind = COSIM_RTL,
        .run = shim_enemy_9063_collide,
        .supported = guard_enemy_9063_collide,
        // Measured 84..490, mean 98, over the 32 calls `movies/level5.zmv`
        // makes. The 490 is the one call that is not an ignore.
        .cycles = 98,
        .stack_bytes = 2,
    },
    {
        .name = "enemy_ac92",
        .symbol = "$81:AC92",
        .entry = 0x81ac92,
        .ret_op = 0x81ac98,  // the ignore path's RTL, after its own CLC
        .ret_kind = COSIM_RTL,
        .run = shim_enemy_ac92_collide,
        .supported = guard_enemy_ac92_collide,
        // Measured 98..1126, mean 109, over the 309 calls
        // `movies/level49-corner.zmv` makes. Almost all of them are ignores,
        // which is two instructions; the 1,126 is a hit that spliced.
        .cycles = 109,
        .stack_bytes = 2,
    },
    {
        .name = "enemy_e6e4",
        .symbol = "$81:E6E4",
        .entry = 0x81e6e4,
        // `$81:E6F1`, the ignore path's `RTL` after its own `CLC`. The airborne
        // guard and the no-damage path share a *different* `CLC : RTL` at
        // `$81:E72A`, and the death tail sets carry — so, as everywhere in this
        // family, land on a bare `RTL` and let `native_publish`'s flags stand.
        .ret_op = 0x81e6f1,
        .ret_kind = COSIM_RTL,
        .run = shim_enemy_e6e4_collide,
        // No guard: nothing it can be handed makes it decline. See above.
        .supported = NULL,
        // Measured 164..570, mean 192, over the 367 calls
        // `movies/level37-e6e4.zmv` makes. The floor is higher than the rest of
        // the family's — 164 against `enemy_d7f6`'s 84 — because even the ignore
        // path reads the display record first, which is `LDY $08 : LDX $0004,Y`
        // before any comparison happens. A guard costs every caller, including
        // the ones it does not refuse.
        .cycles = 192,
        .stack_bytes = 2,
    },
    {
        .name = "actor_845e",
        .symbol = "$81:845E",
        .entry = 0x81845e,
        .ret_op = 0x81847b,  // the pass path's RTL, after its CLC
        .ret_kind = COSIM_RTL,
        .run = shim_actor_845e_collide,
        // Nothing to guard: it cannot decline and it writes no WRAM.
        .supported = NULL,
        // Measured 104..322, mean 115, over the 38 calls
        // `movies/level49-corner.zmv` makes.
        .cycles = 115,
        // It pushes nothing and calls nothing — the only entry in the registry
        // that touches neither the stack nor WRAM.
        .stack_bytes = 0,
    },
    {
        .name = "shot_edaa",
        .symbol = "$81:EDAA",
        .entry = 0x81edaa,
        .ret_op = 0x81edaa,  // the entry *is* the RTL
        .ret_kind = COSIM_RTL,
        .run = shim_shot_edaa_collide,
        // Nothing to guard: one instruction, no ids, no stores, no decline.
        .supported = NULL,
        // Measured 42..82, mean 43, over the 62 calls
        // `movies/level25-boss.zmv` makes — which is worth a line, because an
        // `RTL` is six cycles and this is the entry where the difference between
        // *the routine* and *reaching the routine* is the whole number. What the
        // harness measures is entry PC to return, and for a one-byte routine that
        // is almost entirely the `JSL` and the bus.
        .cycles = 43,
        .stack_bytes = 0,
    },
    {
        .name = "shot_f6a3",
        .symbol = "$81:F6A3",
        .entry = 0x81f6a3,
        // `$81:F6B7`, the store path's `RTL`. The ignore path has its own two
        // instructions earlier at `$81:F6B3`; either would do, and this is the
        // one the majority of calls do not take, which is the same choice
        // `actor_845e` made and for the same reason: pick the return the harness
        // can be sure it is watching.
        .ret_op = 0x81f6b7,
        .ret_kind = COSIM_RTL,
        .run = shim_shot_f6a3_collide,
        // Nothing to guard: three ids and an else, and neither exit declines.
        .supported = NULL,
        // Measured 118..188, mean 148, over the 273 calls
        // `movies/level25-heavy.zmv` makes.
        .cycles = 148,
        .stack_bytes = 0,
    },
    {
        .name = "actor_f4ef",
        .symbol = "$82:F4EF",
        .entry = 0x82f4ef,
        // `$82:F4FE`, the store path's `RTL`; the ignore path has its own four
        // bytes earlier at `$82:F4FA`. Same choice as `shot_f6a3`, and the first
        // entry in this registry whose bank is `$82`.
        .ret_op = 0x82f4fe,
        .ret_kind = COSIM_RTL,
        .run = shim_actor_f4ef_collide,
        // Two ids and an else; neither exit declines.
        .supported = NULL,
        // Measured 118 exactly, on all seven calls `movies/level21-bubble.zmv`
        // makes — the only entry in the registry with no spread at all, because
        // sixteen bytes of comparisons have nothing to be slow about.
        .cycles = 118,
        .stack_bytes = 0,
    },
    {
        .name = "enemy_d301",
        .symbol = "$81:D301",
        .entry = 0x81d301,
        // `$81:D361`, the `RTL` the three carry-clearing exits share. The other
        // five set carry themselves, so the same rule as the rest of the family:
        // land on a bare `RTL` and let `native_publish`'s flags stand rather than
        // on the `CLC` at `$81:D360`.
        .ret_op = 0x81d361,
        .ret_kind = COSIM_RTL,
        .run = shim_enemy_d301_collide,
        .supported = guard_enemy_d301_collide,
        // Measured 140..140 over the three calls `movies/level9.zmv` makes —
        // all of them the `$FF` id, which is six instructions. The damage path
        // is dearer and this is budgeted low deliberately: a budget that is too
        // small shows up as a `run` divergence rather than hiding.
        .cycles = 140,
        // The `$FF` path pushes nothing. Budgeted for the survivor's tail, which
        // is a `JSL` into `$81:8506` and a `JSR` into `$81:D142`.
        .stack_bytes = 2,
    },
    {
        .name = "enemy_freeze",
        .symbol = "$81:847E",
        .entry = 0x81847e,
        // `$81:84D3`, the `RTL` after the `SEC`. Two of the three exits clear
        // carry and one sets it, so the same rule as the rest of the family:
        // land on a bare `RTL` and let `native_publish`'s flags stand.
        .ret_op = 0x8184d3,
        .ret_kind = COSIM_RTL,
        .run = shim_enemy_freeze,
        // Measured 180..1240, mean 296, over the 73 calls
        // `movies/level17-weapon.zmv` puts through the entry PC itself. The
        // 1,240 is a freeze: the counter, the guard, the slot lookup and the
        // splice. The 180 is a hit that only counted.
        .cycles = 296,
        .stack_bytes = 2,  // `JSL $80:9D6A` on the acting path; nothing else
    },
    {
        .name = "enemy_bubble",
        .symbol = "$81:83C6",
        .entry = 0x8183c6,
        // `$81:8403`, the `RTL` after the splice's `SEC`. Same rule as
        // `enemy_freeze`: land on a bare `RTL` so `native_publish`'s flags stand
        // rather than on the `CLC` at `$81:83D0`.
        .ret_op = 0x818403,
        .ret_kind = COSIM_RTL,
        .run = shim_enemy_bubble_react,
        // Measured 906..946, mean 933, over the three calls
        // `movies/level49-corner.zmv` puts through it — all of them the splice,
        // so the guard's own refusal has never been timed.
        .cycles = 933,
        // `PHA` and `PLX` inside the splice, which is all it pushes: it calls
        // nothing.
        .stack_bytes = 2,
    },
    {
        .name = "rng",
        .symbol = "$80:9D39",
        .entry = 0x809d39,
        .ret_op = 0x809d5a,  // its one `RTL`
        .ret_kind = COSIM_RTL,
        .run = shim_rng_next,
        // Measured 338..412, mean 355, and the mean is the same on every movie
        // in the corpus to within two cycles — it has one branch and it is three
        // instructions long. The spread is the `BVC` and nothing else.
        .cycles = 355,
        .stack_bytes = 0,  // it calls nothing
    },
    {
        .name = "actor_deeb",
        .symbol = "$82:DEEB",
        .entry = 0x82deeb,
        // `$82:DEF1`, the ignore path's `RTL`, and it has to be that one: the
        // other exit's `SEC` is two instructions before its `RTL`, so landing
        // there would run nothing but the return anyway — but landing on the
        // `CLC` at `$82:DEF0` would clear the carry the port just published.
        .ret_op = 0x82def1,
        .ret_kind = COSIM_RTL,
        .run = shim_actor_deeb_collide,
        // Measured 118..124, mean 119, over the twelve calls
        // `movies/level49.zmv` makes — all of them the id it answers to.
        .cycles = 119,
        .stack_bytes = 0,
    },
    {
        .name = "actor_f1c2",
        .symbol = "$82:F1C2",
        .entry = 0x82f1c2,
        .ret_op = 0x82f1da,  // one of its two bare RTLs; neither touches carry
        .ret_kind = COSIM_RTL,
        .run = shim_actor_f1c2_collide,
        // Measured 256..256 over both calls `movies/level37.zmv` makes.
        .cycles = 256,
        .stack_bytes = 0,
    },
    {
        .name = "actor_f534",
        .symbol = "$81:F534",
        .entry = 0x81f534,
        // `$81:F54E`, the fall-through `RTL`. All three exits clear carry, so
        // as with `enemy_cdde` the choice is not load-bearing — and a bare `RTL`
        // is picked anyway so that it does not become load-bearing by accident.
        .ret_op = 0x81f54e,
        .ret_kind = COSIM_RTL,
        .run = shim_actor_f534_collide,
        // Measured 178..266, mean 216, over the five calls
        // `movies/level21.zmv` makes — 15 marks on the unguarded latch and 10
        // on the guarded one, so both of its stores are in the sample.
        .cycles = 216,
        .stack_bytes = 0,  // it calls nothing and pushes nothing
    },
    {
        .name = "victim_a264",
        .symbol = "$83:A264",
        .entry = 0x83a264,
        // `$83:A2B3`, the ignore path's `RTL`. This one *is* load-bearing in the
        // other direction: four of the six exits set carry, and landing on the
        // `CLC` two bytes back would clear the answer the port published.
        .ret_op = 0x83a2b3,
        .ret_kind = COSIM_RTL,
        .run = shim_victim_a264_collide,
        // Measured 318..358, mean 339, over four calls — and none of them took
        // a path that calls `$81:8191`, so this is the shot-clears exit's cost
        // and nothing else's.
        .cycles = 339,
        // The `JSL $81:8191` it makes on two paths, inlined by the port but not
        // by the ROM.
        .stack_bytes = 4,
    },
    {
        .name = "enemy_cdde",
        .symbol = "$81:CDDE",
        .entry = 0x81cdde,
        // `$81:CDFA`. All four exits are `CLC : RTL`, so unlike the enemy family
        // the choice is not load-bearing here — carry is false whatever this
        // lands on. Pointed at the bare `RTL` anyway, because the day an exit
        // stops clearing carry is not the day to discover the rule was being
        // relied on by accident.
        .ret_op = 0x81cdfa,
        .ret_kind = COSIM_RTL,
        .run = shim_enemy_cdde_collide,
        // Measured 118..158, mean 120, over the 32 calls
        // `movies/level29-fighting.zmv` makes — and every one of them took the
        // same branch, so this is the ignore path's cost and nothing else's.
        .cycles = 120,
        // 0 observed, for the same reason: nothing in the corpus reaches the
        // `JSR $CC0A`. Left at 2, which is what that `JSR` will push the day
        // something does, because a budget that is too small fails a call and one
        // that is too large only waives two dead bytes.
        .stack_bytes = 2,
    },
    {
        .name = "enemy_b592",
        .symbol = "$81:B592",
        .entry = 0x81b592,
        // `$81:B59D`, the first of three bare `RTL`s. Every exit clears carry,
        // as `enemy_cdde`'s do, and the same reasoning applies to picking one.
        .ret_op = 0x81b59d,
        .ret_kind = COSIM_RTL,
        .run = shim_enemy_b592_collide,
        // Measured 114..154, mean 122, over the five calls
        // `movies/level29-firstaid.zmv` makes — all five the ignore branch.
        .cycles = 122,
        .stack_bytes = 0,  // it calls nothing and pushes nothing
    },
    {
        .name = "boss_9660",
        .symbol = "$82:9660",
        .entry = 0x829660,
        // `$82:9677`, the `RTL` the ignore path clears carry into. The other
        // exit is `$82:96D8` and it *sets* carry — the two are not
        // interchangeable in the ROM, but they are here, because
        // `native_publish` puts the port's flags in place before the core
        // executes this instruction and an `RTL` sets none of them.
        .ret_op = 0x829677,
        .ret_kind = COSIM_RTL,
        .run = shim_boss_9660_collide,
        // Measured 192..792, mean 264, over the 5,126 calls
        // `movies/level25.zmv` makes. The spread is the three ignore paths
        // against the damage path's `SBC $818561,X` — a long-addressed read out
        // of another bank — and the mean is the ignore path's, because 10,032
        // of the 10,112 marks land there.
        .cycles = 264,
        .stack_bytes = 0,  // it calls nothing and pushes nothing
    },
    {
        .name = "shot_collide",
        .symbol = "$81:FE0E",
        .entry = 0x81fe0e,
        // Two `RTL`s, at $FE20 and $FE2E, and they are not interchangeable: the
        // expire path arrives with A = 1 and carry set by a `CMP`, the pass path
        // with A = the id. `native_publish` has already put the port's answer in
        // the registers, so either one returns correctly — this is the shorter.
        .ret_op = 0x81fe20,
        .ret_kind = COSIM_RTL,
        .run = shim_shot_collide,
        .cycles = 40,
        .stack_bytes = 0,  // it pushes nothing at all
    },
    {
        .name = "victim_collide",
        .symbol = "$83:A364",
        .entry = 0x83a364,
        // Six `RTL`s. `$A3C9` is the shared tail two of the exits reach — the
        // entry guard's `BNE` and the `$34` case falling through — and, like
        // `shot_collide`'s, which one is named does not affect what returns:
        // `native_publish` has already put the port's registers in place.
        .ret_op = 0x83a3c9,
        .ret_kind = COSIM_RTL,
        .run = shim_victim_collide,
        .cycles = 40,
        .stack_bytes = 0,  // it pushes nothing at all
    },
    {
        .name = "object_collide",
        .symbol = "$80:CAEE",
        .entry = 0x80caee,
        // Two `RTL`s, at $CB06 and $CB17, and as with the two handlers above
        // which one is named does not affect what returns — `native_publish`
        // has already put the port's registers in place. The shared ignore tail
        // is the shorter.
        .ret_op = 0x80cb06,
        .ret_kind = COSIM_RTL,
        .run = shim_object_collide,
        .cycles = 40,
        .stack_bytes = 0,  // it pushes nothing at all
    },
    {
        // Before `apu_play_sfx`, because it is that routine's callee: the
        // registry reads callees-first so the report reads the way the call
        // chain does.
        .name = "apu_send",
        .symbol = "$80:CCC8",
        .entry = 0x80ccc8,
        .ret_op = 0x80ccdd,  // RTS
        .ret_kind = COSIM_RTS,
        .run = shim_apu_send,
        // Never substituted, so this budget is never spent — see
        // `CosimRoutine::verify_only`, which is where the whole argument lives.
        // The measured distribution is 218..85,184 with a mean of 2,615..2,649
        // across the four movies, and the reason no figure in it is usable is that
        // the spread *is* the SPC700 deciding how long the 65816 waits. 218 is
        // what a call that did not wait at all costs, which is the only part of
        // the routine a budget could honestly stand for.
        .cycles = 218,
        .stack_bytes = 0,   // it pushes nothing at all
        .verify_only = true
    },
    {
        .name = "apu_play_sfx",
        .symbol = "$80:CC3B",
        .entry = 0x80cc3b,
        .ret_op = 0x80cc4b,  // RTL
        .ret_kind = COSIM_RTL,
        .run = shim_apu_play_sfx,
        // Measured, and by far the most variable routine in the registry for
        // its size: 484..85,450, because eight of its eleven instructions are a
        // spin on `$2143` and what they cost is how long the SPC700 took to
        // acknowledge the *previous* command. 85,450 is a wait of a frame and a
        // half. The mean is 507 on one movie and 2,134 on another, so no single
        // figure is right — so this is the measured **minimum**, the cost of a
        // call that did not wait, and every substituted call is one of those: a
        // lone sound effect finds the SPC caught up from sounds ago, so
        // `apu_drive`'s spin exits on its first read. Charging the mean would be
        // billing a wait that did not happen.
        .cycles = 484,
        .stack_bytes = 4,  // PHD + PEA, then the `JSR $CCC8` at the same depth
    },
    {
        // Also a callee of the uploader `apu_send` serves, and the third of the
        // three routines the data-set path is made of. Unlike the other two it
        // is ordinary: it touches no register, waits for nothing, and is
        // substituted like anything else.
        .name = "apu_next_byte",
        .symbol = "$80:CCBF",
        .entry = 0x80ccbf,
        .ret_op = 0x80ccc7,  // RTS
        .ret_kind = COSIM_RTS,
        .run = shim_apu_next_byte,
        // 134..202, and the mean is **138 on every movie measured** — the
        // flattest profile in this registry by a distance, because there is one
        // branch in it and it is taken once in 256. Cheaper entries exist (the
        // collision dispatchers bottom out at 40) but none of them is called a
        // quarter of a million times.
        .cycles = 138,
        .stack_bytes = 0,   // no pushes; five instructions and four of them are
                            // a byte-wide increment
    },
    // `$80:CC7C apu_load_set` would go here, after the two routines it calls,
    // and it is written — `port/apu.h`, `apu_load_set()` — but there is no
    // entry for it and no shim. **Every call to it is interrupted.** It sends
    // about 23,800 commands per set and spends roughly 104,000 instructions
    // doing it, which is eight frames or so, and an NMI lands inside all of
    // them: 5 calls / 5 interrupted / 0 checked on `level25-lane`, and 3/3/0 on
    // `boot.zmv`, where the screen is off and the load happens before the title.
    // A routine that outlives a frame cannot be verified per call, so
    // registering it would add a row that says `not reached` on every movie in
    // the corpus and check nothing.
    //
    // The C stands, unverified, exactly as `sprite_cache_init` does; the
    // address is declared in `tools/native_share.py`'s `BLOCKED` so the ranking
    // stops offering it. See `docs/cosim.md`.
    {
        .name = "spawn_has_room",
        .symbol = "$80:9D5B",
        .entry = 0x809d5b,
        .ret_op = 0x809d69,  // RTL; both compares fall onto it
        .ret_kind = COSIM_RTL,
        .run = shim_spawn_has_room,
        // 112..198, call-weighted 150 over 15,393 calls on five movies. The
        // two ends are the two exits: 112 is a refusal on the census, which
        // is three instructions, and 198 is both compares. The mean sits
        // nearer the top because most calls get past the first ceiling.
        .cycles = 150,
        .stack_bytes = 0,
    },
    {
        .name = "sin_deg",
        .symbol = "$80:9C90",
        .entry = 0x809c90,
        .ret_op = 0x809cb1,  // RTS, after the PLX that decides N and Z
        .ret_kind = COSIM_RTS,
        .run = shim_sin_deg,
        // 240..324, mean 286 — and **2,676 calls, to the call, on every one of
        // the five movies measured**, which are different levels of different
        // lengths with different inputs. Bisecting `level1` says why: nothing
        // reaches it before frame 900 and nothing reaches it after frame 1,200,
        // so all 2,676 are one burst in the level-entry transition and none of
        // them is play. The count is a property of the ROM, not of the input,
        // which is a thing very few rows in this table can say.
        .cycles = 286,
        .stack_bytes = 2,  // the PHX, and the index register is sixteen bits
                           // because the table is 360 entries long — which the
                           // harness's measured stack column independently
                           // confirms
    },
    {
        .name = "actor_publish_pos",
        .symbol = "$80:F327",
        .entry = 0x80f327,
        .ret_op = 0x80f337,  // the single-record RTS; the other exit is $F353
        .ret_kind = COSIM_RTS,
        .run = shim_actor_publish_pos,
        // 244..614, call-weighted 353 over 17,076 calls on five movies. The
        // floor is the single-record path — six instructions — and the ceiling
        // is the stacked pair, which is eleven and does four more memory
        // accesses. The mean therefore reads as *what fraction of the board is
        // two records tall*, and it moves the most of anything in this
        // registry between movies: 251 on `level25-lane` against 413 on
        // `level1-2p`.
        .cycles = 353,
        .stack_bytes = 0,
    },
    {
        .name = "nearest_player_dist",
        .symbol = "$81:8024",
        .entry = 0x818024,
        .ret_op = 0x81807d,  // RTS
        .ret_kind = COSIM_RTS,
        .run = shim_nearest_player_dist,
        // 556..978, call-weighted 643 over 8,240 calls on five movies, and the
        // one routine in this round whose cost is a straight function of how
        // many players are on the board: four one-player movies all land
        // between 598 and 609, and `level1-2p` alone measures 918. The second
        // player is the second half of the routine, and it is the only input
        // that turns it on.
        .cycles = 643,
        .stack_bytes = 0,
    },
    {
        .name = "wave_hdma_build",
        .symbol = "$80:9570",
        .entry = 0x809570,
        .ret_op = 0x8095da,  // RTS; every path in the routine converges on it
        .ret_kind = COSIM_RTS,
        .run = shim_wave_hdma_build,
        // **The most expensive substitutable routine in the registry**, ahead
        // of `$82:8069 boss_bg_queue_flip` at 89,008 and `sprite_build_oam` at
        // 43,111 — and unlike either of those it is neither a DMA nor a pass
        // over the whole board. 68,996..164,382, call-weighted 115,606 over
        // 2,021 calls on seven movies — a third of a frame's CPU budget in one
        // call, because the loop makes ~223 far calls to `sin_deg` and does two
        // long-addressed stores per scanline.
        //
        // The distribution is two populations rather than one. Every level
        // movie measures exactly 12 calls at 163,164..163,514, the full-length
        // table built twelve times in the level-entry transition; `boot.zmv`
        // makes 1,077 at a mean of 96,406 and a floor of 1,176, which is the
        // title sequence holding the wobble and then retracting it two bytes at
        // a time until there is almost no table left to build. `level45-race`
        // sits between the two at 884 calls and 135,762.
        //
        // So `.cycles` is a number no call is ever near, and until the model
        // above existed the budget drift table measured what that was worth:
        // -47,703 cycles a call on a level movie, two orders of magnitude
        // worse per call than anything else in the registry. It is kept because
        // a declared mean is what a routine falls back to when its shim reports
        // nothing, and this one always reports — but it is the fallback now and
        // not the price.
        .cycles = 115606,
        .stack_bytes = 7,  // JSL (3) into the trampoline, its JSR (2), and the
                           // PHX inside sin_deg (2)
    },
    {
        .name = "actor_step_bearing",
        .symbol = "$81:9BF3",
        .entry = 0x819bf3,
        .ret_op = 0x819c61,  // RTS, shared with the rest-frame exit
        .ret_kind = COSIM_RTS,
        .run = shim_actor_step_bearing,
        // 134..16,904, mean 5,154 over 16,569 calls — and the spread is the
        // whole story: 134 is a rest frame, which is four instructions, and the
        // ceiling is six calls into three other ported routines, two of which
        // walk the visible-actor list. One movie in the corpus reaches it at
        // all (`level21-bubble`), which is what a per-actor behaviour looks
        // like from here.
        .cycles = 5154,
        .stack_bytes = 7,  // the deepest of the three JSLs: $80:BF67's own
                           // PHD and PEA under the call's three bytes
    },
    {
        .name = "monster_anim",
        .symbol = "$81:C16B",
        .entry = 0x81c16b,
        .ret_op = 0x81c1a6,  // the RTS after the JSR; the mirror exits at $C1B0
        .ret_kind = COSIM_RTS,
        .run = shim_monster_anim,
        // 280..1,166, call-weighted 473 over 26,736 calls on the two movies
        // that meet the creature — `level25-lane` and `level45-race`, at 461
        // and 480, which is as close as two movies get in this table.
        .cycles = 473,
        .stack_bytes = 2,  // the JSR into $81:C00B on three of the four paths
    },
    {
        .name = "monster_place_carried",
        .symbol = "$81:C00B",
        .entry = 0x81c00b,
        .ret_op = 0x81c025,  // RTS
        .ret_kind = COSIM_RTS,
        .run = shim_monster_place_carried,
        // 104..400, call-weighted 120 over 24,422 calls, and the two ends are
        // the two paths: 104 is the `CPY #$FFFF` guard and an `RTS`, and the
        // rest is two table reads and two coordinate adds. The 2,314-call gap
        // between this and `monster_anim` is the mirrored exit that returns
        // without calling it.
        .cycles = 120,
        .stack_bytes = 0,
    },
    {
        .name = "monster_seek",
        .symbol = "$81:BB75",
        .entry = 0x81bb75,
        // Three exits in two routines: `$BB92`, `$BBA3`, and the `RTS` at
        // `$81:BEE2` that the `JMP $BEDA` tail returns through. This is the
        // first of them, chosen because it is the one that touches nothing.
        .ret_op = 0x81bb92,
        .ret_kind = COSIM_RTS,
        .run = shim_monster_seek,
        // 6,494..10,948, call-weighted 7,964 over 27,280 calls on four movies.
        // Eleven of its own instructions and one `JSL actor_nearest`, which is
        // 32 slots walked whatever the board looks like — so this budget is
        // almost entirely the callee's, and the 4,454-cycle spread is how many
        // of those 32 hold something worth measuring.
        .cycles = 7964,
        .stack_bytes = 7,  // the `JSL`s' three bytes with `actor_nearest`'s own
                           // PHD and `player_in_range`'s deeper pushes on top
    },
    {
        .name = "monster_deliver",
        .symbol = "$81:BBA4",
        .entry = 0x81bba4,
        .ret_op = 0x81bbea,  // likewise: the plain `RTS`, not the stub's
        .ret_kind = COSIM_RTS,
        .run = shim_monster_deliver,
        // **Not measured, because no movie in the corpus reaches it.** The
        // creature has to pick somebody up and carry them, and 43 movies never
        // once do; the profiler agrees, counting zero calls at `$81:BBA4` in
        // every one of the eleven traces. This is `monster_seek`'s figure,
        // which is defensible rather than measured: the two routines make the
        // same single `JSL actor_nearest` and that call is nearly all of the
        // budget, and the extra work here is three compares and, on one path,
        // an `actor_slot_free`. It has never been spent and, on the evidence,
        // may never be.
        .cycles = 7964,
        .stack_bytes = 7,  // likewise `actor_nearest`'s, which is the deepest
                           // of the two calls it can make
    },
    {
        .name = "actor_slot_alloc",
        .symbol = "$80:BE0C",
        .entry = 0x80be0c,
        .ret_op = 0x80be3a,  // the success RTL; a full board returns at $BE27
        .ret_kind = COSIM_RTL,
        .run = shim_actor_slot_alloc,
        // 458..2,578, call-weighted 1,277 over 1,606 calls on six movies. The
        // floor is a first-slot hit and the ceiling is a scan that walked all
        // 32, so the mean reads as **how full the board is**: 1,008 on
        // `level9-weapons` against 1,631 on `level25-lane`. It is the one
        // routine in this registry whose cost is a linear search nothing
        // bounds but the array.
        .cycles = 1277,
        .stack_bytes = 3,  // PHB, and the PEA that costs a byte more than it
                           // needs to
    },
    {
        .name = "actor_slot_free",
        .symbol = "$80:BE41",
        .entry = 0x80be41,
        .ret_op = 0x80be7c,  // RTL; the two declines branch straight to it
        .ret_kind = COSIM_RTL,
        .run = shim_actor_slot_free,
        // 502..2,288, call-weighted 1,071 over 1,552 calls on six movies, and
        // the shape is the same as the allocator's for the same reason: the
        // unlink walks the list, so a long list is a slow free. 502 is a
        // decline, which is four instructions.
        .cycles = 1071,
        .stack_bytes = 2,  // PHD
    },
    {
        .name = "weapon_select_next",
        .symbol = "$80:EA63",
        .entry = 0x80ea63,
        .ret_op = 0x80eaa3,  // RTS; both exits converge on it
        .ret_kind = COSIM_RTS,
        .run = shim_weapon_select_next,
        .cycles = 3227,
        // The unchanged exit pushes nothing — `level1-rescue.zmv` measures 0 —
        // and a change adds the `JSR $EA4B` and the `JSL apu_play_sfx` under it.
        .stack_bytes = 7,
    },
    {
        .name = "item_select_next",
        .symbol = "$80:EAA8",
        .entry = 0x80eaa8,
        .ret_op = 0x80eae0,  // RTS; both exits converge on it, as next door
        .ret_kind = COSIM_RTS,
        .run = shim_item_select_next,
        // Measured on `movies/level1-keys.zmv`, the only movie that reaches it
        // at all: five calls spanning 1,038..12,486, and this is their mean.
        // The spread is `apu_play_sfx`'s — the two exits differ by a sound
        // effect, and a sound effect's cost is how long the SPC700 took to
        // acknowledge the last one.
        .cycles = 4316,
        .stack_bytes = 4,  // the `JSL apu_play_sfx` the changed exit ends with
    },
    {
        .name = "score_add",
        .symbol = "$80:C7D9",
        .entry = 0x80c7d9,
        .ret_op = 0x80c818,  // the discard entry's bare RTL; all three return alike
        .ret_kind = COSIM_RTL,
        .run = shim_score_add,
        .supported = guard_score_add,
        .cycles = 524,
        .stack_bytes = 4,  // the opening `PHX`, plus the `JSR $C7C2` under it
    },
    {
        .name = "actor_collide_notify",
        .symbol = "$80:BE8F",
        .entry = 0x80be8f,
        .ret_op = 0x80bec8,  // RTS
        .ret_kind = COSIM_RTS,
        .run = shim_actor_collide_notify,
        .supported = guard_actor_collide_notify,
        .cycles = 2842,
        .stack_bytes = 22,   // the `JSL $80:8480` it ends on, and all of what that reaches
    },
    {
        .name = "actor_overlap_pass",
        .symbol = "$80:BEC9",
        .entry = 0x80bec9,
        .ret_op = 0x80bf1a,  // RTL
        .ret_kind = COSIM_RTL,
        .run = shim_actor_overlap_pass,
        .supported = guard_actor_overlap_pass,
        .cycles = 4426,
        .stack_bytes = 26,  // its `PHY`, plus the deepest the dispatch under it goes
    },
    {
        .name = "sprite_build_oam",
        .symbol = "$80:BD1F",
        .entry = 0x80bd1f,
        .ret_op = 0x80bde2,  // RTL
        .ret_kind = COSIM_RTL,
        .run = shim_sprite_build_oam,
        .supported = guard_sprite_build_oam,
        .excludes = BUILD_OAM_EXCLUDES,
        .exclude_count = 1,
        // The most variable routine in the registry: 5,864 when nothing is on
        // screen and **137,160** on `level25-boss.zmv`, where the ceiling used
        // to read 66,412 because no movie had put that much on the board. A
        // whole NTSC frame is 357,366 master cycles — see `COSIM_FRAME_CYCLES`,
        // not the 57,000 this comment claimed — so the worst pass is 38% of a
        // frame and the mean is 12%.
        //
        // 5,014 of the 6,042 calls `level25-boss` makes are priced; the other
        // 1,028 are the ones whose collisions land on a handler with no cost
        // table, and they are also the expensive ones, so the fallback mean of
        // 43,111 leaves the largest single debt in the drift table — -41.7M
        // cycles over that movie. That is the next thing to fix and it is a
        // guard to widen, not a model to write.
        .cycles = 43111,
        .stack_bytes = 32,  // PHB + PHD + the deepest nested JSR/JSL
    },
    {
        .name = "fade_in",
        .symbol = "$80:891A",
        .entry = 0x80891a,
        .end = 0x808933,     // one past the `RTL`; bounds the yield-site test
        .ret_op = 0x808932,  // RTL
        .ret_kind = COSIM_RTL,
        .run_yield = shim_fade_in,
        .yield_op = 0x808923,  // the `JSL thread_yield` native mode jumps to
        .ctx_size = (int)sizeof(FadeCtx),
        // Per *segment*, not per call: what `verify` measured a run between two
        // suspensions to cost (162..238, mean 201 over 16 segments).
        .cycles = 199,
        .stack_bytes = 3,  // pushes nothing of its own
    },
    {
        .name = "actor_nearest",
        .symbol = "$80:B123",
        .entry = 0x80b123,
        .ret_op = 0x80b18e,  // RTL, after the PLD that undoes the opening PHD
        .ret_kind = COSIM_RTL,
        .run = shim_actor_nearest,
        // A fixed 32 slots whatever the board holds, so the spread is narrow
        // and it is all in how many records get as far as the subtraction:
        // 6,498..7,592 over 1,006 calls on movies/level1.zmv.
        .cycles = 7195,
        .stack_bytes = 2,  // the opening PHD
    },
    {
        // Before its two callers, because the registry reads callees-first.
        .name = "actor_gap",
        .symbol = "$80:B093",
        .entry = 0x80b093,
        // Two `RTS`es, at $B0B6 and $B0BA, and neither is preceded by anything
        // that has to run: `native_publish` has already put the answer and the
        // flags in place, and an `RTS` writes none of either.
        .ret_op = 0x80b0b6,
        .ret_kind = COSIM_RTS,
        .run = shim_actor_gap,
        // 88..490 over 44,164 calls on ten movies, call-weighted. The floor is
        // the empty-record exit, which is four instructions, and the ceiling is
        // both absolute values being taken; nothing here varies with the board,
        // so this is as tight as a budget in the registry gets.
        .cycles = 230,
        .stack_bytes = 0,  // it pushes nothing at all
    },
    {
        .name = "actor_nearest_id3",
        .symbol = "$80:B18F",
        .entry = 0x80b18f,
        .ret_op = 0x80b1eb,  // RTL, after the PLD that undoes the opening PHD
        .ret_kind = COSIM_RTL,
        .run = shim_actor_nearest_id3,
        // 5,482..8,256 over 147 calls on five movies. A fixed 32 slots like
        // `actor_nearest`, and a wider spread than its 6,498..7,592 for the
        // opposite reason to the usual one: with one id instead of four, more
        // slots are dismissed before the subtraction and fewer after it.
        .cycles = 6909,
        .stack_bytes = 2,  // the opening PHD
    },
    {
        .name = "actor_bearing_point",
        .symbol = "$80:B1EC",
        .entry = 0x80b1ec,
        .ret_op = 0x80b21d,  // RTL
        .ret_kind = COSIM_RTL,
        .run = shim_actor_bearing_point,
        // 604..748 over 35 calls on two movies, which is the thinnest sample in
        // the registry and is the sample the corpus has: three call sites, and
        // 45 executions of them in eleven traced movies.
        .cycles = 679,
        .stack_bytes = 4,  // the opening PHD and the PHA under it
    },
    {
        .name = "actor_bearing",
        .symbol = "$80:B22A",
        .entry = 0x80b22a,
        .ret_op = 0x80b25e,  // RTL
        .ret_kind = COSIM_RTL,
        .run = shim_actor_bearing,
        // 514..760 over 12,172 calls on ten movies, call-weighted. Straight-line
        // code with two branches in it, so the spread is only which of them are
        // taken.
        .cycles = 611,
        .stack_bytes = 2,  // the opening PHD
    },
    {
        .name = "player_in_range",
        .symbol = "$80:B26B",
        .entry = 0x80b26b,
        // Three `RTL`s — $B298, $B29E, $B2A4 — and which one is named does not
        // affect what returns, for the same reason as everywhere else here.
        .ret_op = 0x80b298,
        .ret_kind = COSIM_RTL,
        .run = shim_player_in_range,
        // 1,002..1,298 over 3,385 calls on five movies, call-weighted, and both
        // `actor_gap` calls are inside it — which is the whole of why it costs
        // four and a half times what one of those does.
        .cycles = 1104,
        .stack_bytes = 4,  // the opening PHD and the PEA under it
    },
    {
        .name = "player_bearing",
        .symbol = "$80:B2A5",
        .entry = 0x80b2a5,
        .ret_op = 0x80b2d2,  // the RTL on the nobody-in-range path
        .ret_kind = COSIM_RTL,
        .run = shim_player_bearing,
        // 1,002..1,636 over 18,697 calls on seven movies, call-weighted. The
        // floor is the nobody-in-range exit and the ceiling is the direction
        // path with its stack traffic and its long-addressed table read, and the
        // mean sits near the top because 99 calls in 100 find somebody.
        .cycles = 1548,
        // The opening PHD, the PEI that saves the winning distance, and the PHY
        // under that — the deepest of the six, and only on the direction path.
        .stack_bytes = 6,
    },
    {
        .name = "actor_at_point",
        .symbol = "$80:BF67",
        .entry = 0x80bf67,
        .ret_op = 0x80bfc0,  // the RTL on the found path, after its SEC
        .ret_kind = COSIM_RTL,
        .run = shim_actor_at_point,
        // 698..6,958 across seven movies, and the spread is the board: unlike
        // `actor_nearest`'s fixed 32 slots this walks only what the cull kept,
        // and it stops early when it finds something. 2,645 is the mean
        // weighted by the 26,796 calls those movies made, not one movie's.
        .cycles = 2645,
        .stack_bytes = 4,  // the opening PHD and the PEA under it
    },
    {
        .name = "actor_obstacle_at_point",
        .symbol = "$80:BFC8",
        .entry = 0x80bfc8,
        .ret_op = 0x80c041,  // the RTL on the blocked path, after its SEC
        .ret_kind = COSIM_RTL,
        .run = shim_actor_obstacle_at_point,
        // 426..7,794, call-weighted over 34,211 calls on eleven movies rather
        // than taken from one. Borrowing `actor_at_point`'s 2,645 would have
        // been 27% low: same loop, but a filter that accepts far fewer ids
        // means far fewer early exits, so the walk usually runs to the end.
        .cycles = 3630,
        .stack_bytes = 4,  // the opening PHD and the PEA under it
    },
    {
        .name = "camera_window_update",
        .symbol = "$80:A54D",
        .entry = 0x80a54d,
        .ret_op = 0x80a587,
        .ret_kind = COSIM_RTS,
        .run = shim_camera_window_update,
        // 608..648 over 227 calls. Twenty-seven instructions and no branch,
        // so the whole 40-cycle band is the bus.
        .cycles = 613,
        .stack_bytes = 0,  // no push at all
    },
    {
        .name = "camera_split_y",
        .symbol = "$80:A588",
        .entry = 0x80a588,
        .ret_op = 0x80a598,
        .ret_kind = COSIM_RTS,
        .run = shim_camera_split_y,
        // 206..246 over 203 calls; six instructions, same band.
        .cycles = 210,
        .stack_bytes = 0,
    },
    {
        .name = "vram_queue_request",
        .symbol = "$80:9E6D",
        .entry = 0x809e6d,
        .ret_op = 0x809e7a,
        .ret_kind = COSIM_RTS,
        .run = shim_vram_queue_request,
        // 166..206. The three exits are 4, 5 and 7 instructions, so this is
        // a mean over paths as well as over the bus -- narrow because the
        // paths barely differ.
        .cycles = 173,
        .stack_bytes = 0,
    },
    {
        .name = "tilemap_copy_row",
        .symbol = "$80:A61D",
        .entry = 0x80a61d,
        .ret_op = 0x80a64e,
        .ret_kind = COSIM_RTS,
        .run = shim_tilemap_copy_row,
        .supported = guard_tilemap_copy_row,
        // 8,074..10,126. The loop count is fixed at 33, so unlike
        // `tilemap_copy_column` this spread is the priority branch and
        // the bus rather than a variable trip count -- a 2,000-cycle band
        // around a routine that always does the same amount of work.
        .cycles = 9314,
        // The JSL to $80:AD1C is 3 and its own PHA is 2 under that. The
        // JSR to $80:A401 only reaches 4, so 5 is the floor.
        .stack_bytes = 5,
    },
    {
        .name = "tilemap_buffer_alloc",
        .symbol = "$80:A401",
        .entry = 0x80a401,
        .ret_op = 0x80a415,
        .ret_kind = COSIM_RTS,
        .run = shim_tilemap_buffer_alloc,
        .supported = guard_tilemap_buffer_alloc,
        // 342..382, and the 40 cycles are the bus. The spin would put the
        // ceiling in the thousands; it has never once been entered, which
        // is the same thing the guard's zero declines says from the front.
        .cycles = 370,
        .stack_bytes = 2,  // the PHA it reads back through `$01,S`
    },
    {
        .name = "tilemap_copy_column",
        .symbol = "$80:A5E5",
        .entry = 0x80a5e5,
        .ret_op = 0x80a61c,
        .ret_kind = COSIM_RTS,
        .run = shim_tilemap_copy_column,
        // 1,526..14,250 over 303 calls, and unlike every other spread in
        // this registry it is not the bus or a branch: it is the count.
        // The loop body is fixed, so the cost is linear in how many tiles
        // the caller asked for, and the mean is a mean over strip lengths.
        .cycles = 8411,
        // Its own PHA, the JSL to $80:AD1C, and that routine's PHA under
        // it -- 2 + 3 + 2, which is what `verify` measured.
        .stack_bytes = 7,
    },
    {
        .name = "tilemap_tile_addr",
        .symbol = "$80:AD1C",
        .entry = 0x80ad1c,
        .ret_op = 0x80ad2a,
        .ret_kind = COSIM_RTL,
        .run = shim_tilemap_tile_addr,
        // 250..290, and the 40-cycle spread is the bus rather than the
        // routine: there are no branches in it at all.
        .cycles = 258,
        .stack_bytes = 2,  // the PHA it reads back through `$01,S`
    },
    {
        .name = "lzss_decompress",
        .symbol = "$80:CD20",
        .entry = 0x80cd20,
        .ret_op = 0x80cdd9,  // the RTL, after the PLD that restores the caller
        .ret_kind = COSIM_RTL,
        .run = shim_lzss_decompress,
        .supported = guard_lzss_decompress,
        // Never used: the shim reports the call's exact cost and every call
        // reports one. It is level 1's first stream — 4.5 frames, and the
        // smallest of the five that movie makes — so the scale is on show and
        // nothing silently falls back to a number nobody measured.
        .cycles = 1591682,
        // How deep it goes, not how much it leaks — everything balances by the
        // `RTL`. `PHD` and `PEA $0000` put it 4 down, the `PLD` at `$CD24`
        // takes 2 back, and the deepest point is inside a match: `PHA` and
        // `PHY` at `$CD82`, then the `JSR` to a byte helper.
        .stack_bytes = 8,
        .run_only = true,
    },
    {
        .name = "lzss_read_byte",
        .symbol = "$80:CDDA",
        .entry = 0x80cdda,
        .ret_op = 0x80cde8,  // the CLC path's RTS; the SEC path's is two later
                             // and identical, and RTS touches no flag
        .ret_kind = COSIM_RTS,
        .run = shim_lzss_read_byte,
        // 98..298 over 37,923 calls, and the spread is the whole routine:
        // the floor is the four-instruction `SEC` exit at the end of a
        // stream, the ceiling the eight-instruction read.
        .cycles = 266,
        .stack_bytes = 0,  // fifteen bytes, no push
        .verify_only = true,
    },
    {
        .name = "lzss_write_byte",
        .symbol = "$80:CDEB",
        .entry = 0x80cdeb,
        .ret_op = 0x80cdf3,
        .ret_kind = COSIM_RTS,
        .run = shim_lzss_write_byte,
        // 170..210 over 69,755 calls. No branches, so the 40-cycle spread
        // is the bus: `STA [$2C]` into WRAM against the same into a
        // register-mapped page.
        .cycles = 175,
        .stack_bytes = 0,
        .verify_only = true,
    },
    {
        .name = "terrain_blocked",
        .symbol = "$80:AE14",
        .entry = 0x80ae14,
        .ret_op = 0x80ae96,  // the RTL, after the PLD that undoes the PHD
        .ret_kind = COSIM_RTL,
        .run = shim_terrain_blocked,
        // 678..1,776, call-weighted over 41,635 calls on eleven movies. The
        // spread is how many of the six probes it gets through before one of
        // them blocks, and the floor is a first probe that already has.
        .cycles = 1591,
        .stack_bytes = 4,  // the PHD, and the PHA/PLA that stashes X under it
    },
    {
        .name = "terrain_blocked_enemy",
        .symbol = "$80:AE97",
        .entry = 0x80ae97,
        .ret_op = 0x80af28,  // the RTL on the clear path, after its CLC
        .ret_kind = COSIM_RTL,
        .run = shim_terrain_blocked_enemy,
        // 696..1,842 over 45,015 calls — the same loop, so the same shape.
        .cycles = 1578,
        .stack_bytes = 4,
    },
    {
        .name = "terrain_out_of_bounds",
        .symbol = "$80:B422",
        .entry = 0x80b422,
        .ret_op = 0x80b444,  // the RTL both the compare exits reach
        .ret_kind = COSIM_RTL,
        .run = shim_terrain_out_of_bounds,
        // 138..390 over 59,422 calls, and the narrowest spread of anything in
        // this registry: six exits, none of them a loop.
        .cycles = 359,
        .stack_bytes = 0,  // it pushes nothing at all
    },
    {
        .name = "terrain_blocked_wide",
        .symbol = "$82:90F7",
        .entry = 0x8290f7,
        .ret_op = 0x829188,  // the RTS on the clear path, after its PLD
        .ret_kind = COSIM_RTS,
        .run = shim_terrain_blocked_wide,
        // 634..3,268, call-weighted over 127,455 calls on five movies, and the
        // most expensive leaf in the registry. The floor is the first tile
        // failing the priority test; the ceiling is all ten probes, both reads
        // each, with nothing found — and because the loop is unrolled the
        // ceiling is a straight line rather than an iteration count.
        .cycles = 3167,
        .stack_bytes = 4,  // the PHD, and the PHA/PLA that stashes X under it
    },
    {
        .name = "terrain_point_bit2",
        .symbol = "$80:AF2C",
        .entry = 0x80af2c,
        .ret_op = 0x80af62,  // the RTL on the clear path, after its CLC
        .ret_kind = COSIM_RTL,
        .run = shim_terrain_point_bit2,
        // 1,046..1,086 over 4,477 calls on `level25-lane`, and the flattest
        // profile of anything in this registry: forty cycles between the
        // cheapest call and the dearest. There is no loop and the bounds test
        // in front of it is nearly as straight, so what looks like two paths
        // costs the same either way.
        .cycles = 1077,
        .stack_bytes = 5,  // the PHD, then the JSL under it — the deeper path
    },
    {
        .name = "terrain_footprint_bit12",
        .symbol = "$80:AF66",
        .entry = 0x80af66,
        .ret_op = 0x80aff7,  // the RTL all six probes reach, after its CLC
        .ret_kind = COSIM_RTL,
        .run = shim_terrain_footprint_bit12,
        // 810..2,128 over 166 calls on `level49-corner`, the only movie in the
        // corpus that reaches it. The floor is the first probe answering and
        // the ceiling is all six — the same shape as `terrain_blocked`, run
        // the other way round.
        .cycles = 885,
        .stack_bytes = 4,
    },
    {
        .name = "terrain_tile_bit3",
        .symbol = "$80:B03B",
        .entry = 0x80b03b,
        .ret_op = 0x80b05b,  // the RTL on the clear path
        .ret_kind = COSIM_RTL,
        .run = shim_terrain_tile_bit3,
        // 700..746 over 426 calls on `level5`, `level21` and `level21-spin`,
        // the only three movies that reach it. Forty-six cycles of spread and
        // no loop: the tile either has the bit or it does not.
        .cycles = 721,
        .stack_bytes = 7,  // the PHD, the JSL, and tilemap_tile_addr's own PHA
    },
    {
        .name = "terrain_point_bit8",
        .symbol = "$80:B05F",
        .entry = 0x80b05f,
        .ret_op = 0x80b08f,  // the RTL on the clear path
        .ret_kind = COSIM_RTL,
        .run = shim_terrain_point_bit8,
        // 630..676 over 5,806 calls on `level37-e6e4`. One tile, no bounds
        // test in front of it, so it is `terrain_point_bit2` minus the `JSL`.
        .cycles = 655,
        .stack_bytes = 4,
    },
    {
        .name = "partner_near",
        .symbol = "$80:AFFB",
        .entry = 0x80affb,
        .ret_op = 0x80b038,  // the RTL on the far path, after its CLC
        .ret_kind = COSIM_RTL,
        .run = shim_partner_near,
        // 152..192 over **three calls in the whole corpus**, and the cheapest
        // entry in this registry — because all three are one-player, where the
        // second `BIT` answers eleven instructions in and nothing is measured.
        // What a two-player call costs is not known, and the corpus has never
        // made one.
        .cycles = 165,
        .stack_bytes = 0,  // no PHD and no pushes, the same as $80:B422
    },
    {
        .name = "step_propose",
        .symbol = "$80:E450",
        .entry = 0x80e450,
        .ret_op = 0x80e485,  // the RTS, after the store the flags do not come from
        .ret_kind = COSIM_RTS,
        .run = shim_step_propose,
        // 536..696, call-weighted over 32,306 calls on eleven movies. The
        // narrowest spread in the registry after `terrain_out_of_bounds`:
        // there is no loop in it, and the 160 cycles are the second add.
        .cycles = 647,
        .stack_bytes = 0,  // no pushes; it is two table reads and four adds
    },
    {
        .name = "step_tether_blocked",
        .symbol = "$80:A8B3",
        .entry = 0x80a8b3,
        .ret_op = 0x80a8eb,  // the RTL on the allowed path, after its CLC
        .ret_kind = COSIM_RTL,
        .run = shim_step_tether_blocked,
        // 322..1,450, call-weighted over 52,988 calls on eleven movies, and
        // the widest ratio in the registry that is not a loop: 322 is the
        // one-player exit eleven instructions in, 1,450 is the far path with
        // four absolute differences in it. Which one a movie gets is decided
        // entirely by whether a second player is on the board.
        .cycles = 519,
        .stack_bytes = 2,  // the opening PHD, and nothing else
    },
    {
        .name = "camera_split_x",
        .symbol = "$80:A599",
        .entry = 0x80a599,
        .ret_op = 0x80a5c0,  // the first branch's RTS; the other is at $A5E4
                             // and RTS touches no flag either way
        .ret_kind = COSIM_RTS,
        .run = shim_camera_split_x,
        // 452..522, call-weighted over 455 calls on six movies. Twenty-two
        // instructions, no loop, and the two branches are the same length, so
        // the whole 70-cycle spread is the bus.
        .cycles = 458,
        .stack_bytes = 0,
    },
    {
        .name = "camera_scroll_left",
        .symbol = "$80:A68B",
        .entry = 0x80a68b,
        .ret_op = 0x80a709,  // the one RTL all three exits reach
        .ret_kind = COSIM_RTL,
        .run = shim_camera_scroll_left,
        .supported = guard_camera_scroll_left,
        // 94..17,032, call-weighted over 15,896 calls on six movies, and the
        // widest spread in the registry by a distance -- 180x, where the next
        // worst is `tilemap_copy_column`'s 9x. Three exits of wildly different
        // lengths is only half of it; the other half is that *which* exit a
        // movie takes is a property of the movie. `movies/level49.zmv` holds
        // the camera against the left edge of the map for its whole length and
        // contributes 13,560 calls at 94 cycles each, which is what drags this
        // mean down to a value no single call has ever cost.
        .cycles = 394,
        .stack_bytes = 9,
    },
    {
        .name = "camera_scroll_right",
        .symbol = "$80:A70A",
        .entry = 0x80a70a,
        .ret_op = 0x80a788,  // two RTLs, at $A711 and here; this is the tail
        .ret_kind = COSIM_RTL,
        .run = shim_camera_scroll_right,
        .supported = guard_camera_scroll_right,
        // 116..18,476 over 2,504 calls. Same shape as its mirror, without a
        // movie that pins the camera against this edge -- so the mean lands
        // near the strip path rather than far below every call.
        .cycles = 1883,
        .stack_bytes = 9,
    },
    {
        .name = "camera_scroll_down",
        .symbol = "$80:A789",
        .entry = 0x80a789,
        .ret_op = 0x80a815,  // the early exits share an RTL at $A790
        .ret_kind = COSIM_RTL,
        .run = shim_camera_scroll_down,
        .supported = guard_camera_scroll_down,
        // 116..14,578 over 10,378 calls, and the same bimodality one axis
        // over: `level13` and `level29-fighting` between them are 7,304 calls
        // that cross no tile boundary at all.
        .cycles = 342,
        .stack_bytes = 7,
    },
    {
        .name = "camera_scroll_up",
        .symbol = "$80:A816",
        .entry = 0x80a816,
        .ret_op = 0x80a8a3,  // ...and this one's at $A81B
        .ret_kind = COSIM_RTL,
        .run = shim_camera_scroll_up,
        .supported = guard_camera_scroll_up,
        // 250..15,032 over 2,159 calls -- the narrowest of the four, because
        // nothing in the corpus scrolls upward for long without stopping.
        .cycles = 1731,
        .stack_bytes = 7,
    },
    {
        .name = "camera_follow",
        .symbol = "$80:A93F",
        .entry = 0x80a93f,
        .ret_op = 0x80a9cb,  // the main RTL; the early exits share one at $A952
                             // and both are the same PLD : SEC : RTL
        .ret_kind = COSIM_RTL,
        .run = shim_camera_follow,
        .supported = guard_camera_follow,
        // 266..27,816, call-weighted over 94,784 calls on seven movies. The
        // floor is the exit that finds both deltas already zero -- two thirds
        // of all calls -- and the ceiling is a two-player frame that scrolls on
        // both axes at once, which is four routines deep and buys two strips.
        .cycles = 1062,
        // The PHD is 2, the JSL into a scroll routine 3, and that routine's own
        // 9 under it. The X pair are the deep ones; a movie that only ever
        // scrolls on Y measures 12.
        .stack_bytes = 14,
    },
    {
        .name = "actor_aligned",
        .symbol = "$80:B379",
        .entry = 0x80b379,
        .ret_op = 0x80b3f0,  // the no-match RTL; the four direction exits have
                             // one each at $B3BA, $B3BF, $B3DB and $B3E0, and
                             // all five are the same PLD : RTL
        .ret_kind = COSIM_RTL,
        .run = shim_actor_aligned,
        // 758..7,386 over 6,921 calls on level21-bubble, which is the only
        // movie in the corpus that runs this enemy at all. The floor is a
        // match in a high slot and the ceiling is all 32 walked for nothing;
        // the mean sits near the top because nothing is usually lined up.
        .cycles = 4576,
        // The PHD, and nothing else — it calls nothing.
        .stack_bytes = 2,
    },
    {
        .name = "actor_notify_box",
        .symbol = "$80:BF1B",
        .entry = 0x80bf1b,
        .ret_op = 0x80bf66,
        .ret_kind = COSIM_RTL,
        .run = shim_actor_notify_box,
        .supported = guard_actor_notify_box,
        // 642..11,272, call-weighted across 8,556 calls on six movies -- the
        // fallback now and not the price, since `notify_box_cycles` reports
        // every call it can.
        //
        // The floor is worth being exact about, because the model reproduces it
        // and the old description does not. 642 is **not** "a box that found
        // nothing to tell": it is specifically the *one-record refusal* at
        // `$80:BF33`, with all four bounds kept. An empty visible list leaves
        // through `$80:BF2F` two instructions earlier and would cost 606, and
        // that number has never been measured because no call in the corpus has
        // ever found `$9C` at zero -- `notify_no_actors` is untaken on every
        // movie. The ceiling is a box that entered several handlers, so the
        // spread is the handlers' rather than the walk's; the walk is 32
        // records whatever happens.
        .cycles = 5545,
        // Its own PHD and PHY, plus the deepest the dispatch under it goes.
        // Movies that only ever blast one actor measure 18.
        .stack_bytes = 24,
    },
    {
        .name = "actor_snap_to",
        .symbol = "$80:B3F1",
        .entry = 0x80b3f1,
        .ret_op = 0x80b421,
        .ret_kind = COSIM_RTL,
        .run = shim_actor_snap_to,
        // 334..496 over 5,515 calls on level25-lane. The floor is neither axis
        // snapping and the ceiling is both, and there are only four shapes it
        // can have, so the spread is the narrowest in the registry after the
        // boss blitter's.
        .cycles = 407,
        // It calls nothing and pushes nothing.
        .stack_bytes = 0,
    },
    {
        .name = "boss_bg_queue",
        .symbol = "$82:8014",
        .entry = 0x828014,
        .ret_op = 0x828068,
        .ret_kind = COSIM_RTL,
        .run = shim_boss_bg_queue,
        .supported = guard_boss_bg_queue,
        // 11,528..11,686 over 903 calls on level25-lane -- a spread of 158
        // cycles, or 1.4%, and the narrowest of any routine in the registry.
        // Nothing about this routine varies except which of four stored
        // figures it was pointed at, and all four are the same size.
        .cycles = 11565,
        // PHD is 2 and the PEA under it is popped by the PLD, then the closing
        // JSL is 3 with vbl_queue_a_add's own PHY on top.
        .stack_bytes = 7,
    },
    {
        .name = "boss_bg_queue_flip",
        .symbol = "$82:8069",
        .entry = 0x828069,
        .ret_op = 0x8280df,
        .ret_kind = COSIM_RTL,
        .run = shim_boss_bg_queue_flip,
        .supported = guard_boss_bg_queue_flip,
        // 88,980..89,256 over 1,126 calls -- 276 cycles, 0.3%, on a mean seven
        // and a half times larger, because the extra work is the mirror loop
        // and the mirror loop runs a fixed 280 times whatever else happens.
        // This is the largest budget in the registry, by a factor of two, and
        // it is spent entirely on moving 560 bytes of WRAM so that a figure
        // can face the other way.
        .cycles = 89008,
        .stack_bytes = 7,
    },
    {
        .name = "boss_step",
        .symbol = "$82:8F93",
        .entry = 0x828f93,
        // The bare `RTS` on the stuck path. There are two, one under a `SEC`
        // and one under a `CLC`, and the teleport must land on an instruction
        // that does not touch the carry the shim has just published — so it
        // lands on the `RTS` itself rather than on either flag setter.
        .ret_op = 0x829032,
        .ret_kind = COSIM_RTS,
        .run = shim_boss_step,
        // 2,076..15,616, call-weighted over 42,207 calls on the five level-25
        // movies -- the only movies in the corpus that reach it at all. The
        // floor is a direction whose first probe is already in terrain; the
        // ceiling is a diagonal that runs both passes and finds all four probes
        // clear, which is four `terrain_blocked_wide` calls in one step. Almost
        // all of the budget is those calls: the routine's own arithmetic is
        // about sixty instructions and the probes are the rest.
        .cycles = 11770,
        // Two bytes of `JSR` return address with `terrain_blocked_wide`'s own
        // four on top of it, and the routine pushes nothing itself.
        .stack_bytes = 6,
    },
    {
        .name = "boss_place_parts",
        .symbol = "$82:9265",
        .entry = 0x829265,
        .ret_op = 0x8292c5,  // the only RTS
        .ret_kind = COSIM_RTS,
        .run = shim_boss_place_parts,
        // 1,090..1,142, call-weighted 1,127 over 6,560 calls on the two
        // level-25 movies that raise the figure. **The flattest distribution in
        // this registry**: a 52-cycle spread on a routine that costs eleven
        // hundred, because there is exactly one branch in it and both arms are
        // an `LDY` of a constant. Everything else is 40 straight-line
        // instructions with no call, no loop and no early exit.
        .cycles = 1127,
        .stack_bytes = 0,  // it calls nothing and pushes nothing
    },
    {
        .name = "boss_stomp",
        .symbol = "$82:92D6",
        .entry = 0x8292d6,
        .ret_op = 0x829302,  // the RTS under the JSL
        .ret_kind = COSIM_RTS,
        .run = shim_boss_stomp,
        .supported = guard_boss_stomp,
        // 1,112..13,054, call-weighted 8,230 over 6,549 calls — the fallback
        // now and not the price. Next to `boss_place_parts` above, which runs
        // on the same frames and the same two movies, it is the clearest
        // measurement of what a walk costs: the two routines differ by one
        // `JSL`, and that `JSL` is between 90% and 99% of this one. Which is
        // also why this entry is priced by `actor_notify_box`'s table and owns
        // only the 470 cycles of stores around it — see `BOSS_STOMP_COST`.
        .cycles = 8230,
        .stack_bytes = 21,  // two bytes of `JSR` return address, and
                            // `actor_notify_box`'s dispatch under it
    },
    {
        .name = "blockmap_cell_ptr",
        .symbol = "$80:ACF6",
        .entry = 0x80acf6,
        .ret_op = 0x80ad0a,  // the RTL, after the PLX the flags come from
        .ret_kind = COSIM_RTL,
        .run = shim_blockmap_cell_ptr,
        .supported = guard_blockmap_cell_ptr,
        // 334..374, call-weighted over 1,410 calls on four movies. There is not
        // a branch in the routine, so the 40-cycle spread is the bus and
        // nothing else -- the same shape, and very nearly the same number, as
        // its twin `$80:AD1C tilemap_tile_addr` at 258.
        .cycles = 343,
        .stack_bytes = 2,  // the PHA it reads back through `$01,S`
    },
    {
        .name = "tile_attrs_at_pixel",
        .symbol = "$80:ADC8",
        .entry = 0x80adc8,
        .ret_op = 0x80adf2,  // the RTL, after the second PLB the flags are from
        .ret_kind = COSIM_RTL,
        .run = shim_tile_attrs_at_pixel,
        // 960..1000, and the 40-cycle spread is the bus: there is not a branch
        // in the routine. Very nearly four times `$80:AD1C tilemap_tile_addr`'s
        // 258, which is most of what it does.
        .cycles = 990,
        // PHB, PHD, PHX, PHY are seven and the PEA makes nine, but the PLB
        // takes one back *before* the JSL -- so the deepest point is eight, the
        // three the JSL to $80:AD1C pushes, and that routine's own PHA under
        // them. Thirteen, which is what `verify` measured.
        .stack_bytes = 13,
    },
    {
        .name = "tile_attrs_at_tile",
        .symbol = "$80:ADF3",
        .entry = 0x80adf3,
        .ret_op = 0x80ae13,
        .ret_kind = COSIM_RTL,
        .run = shim_tile_attrs_at_tile,
        // 840..880, call-weighted over the 76 calls the whole corpus makes --
        // 52 on `level9-weapons` and 24 on `level29-ice`, both through
        // `$81:D0D4`, and nothing else in 42 movies reaches it. 128 cycles
        // under the pixel form, which is the six `LSR`s and the two transfers
        // around them almost exactly.
        .cycles = 862,
        .stack_bytes = 13,
    },
    {
        .name = "floor_effect",
        .symbol = "$80:E86D",
        .entry = 0x80e86d,
        .ret_op = 0x80e8d2,  // the RTS every path but $80:E88C reaches
        .ret_kind = COSIM_RTS,
        .run = shim_floor_effect,
        // 1,248..1,792, call-weighted. The floor is 1,248 and everything above
        // it is the two nested calls: `tile_attrs_at_pixel` on every single
        // call, and `terrain_blocked` on the one conveyor direction that asks.
        .cycles = 1322,
        // The routine pushes nothing of its own. The deepest point is the
        // `JSL` to `$80:ADC8` -- three bytes, with that routine's own thirteen
        // under them -- and the `JSR` to the inlined `$80:F935` is only two.
        .stack_bytes = 16,
    },
    {
        .name = "player_state_normal",
        .symbol = "$80:D1FF",
        .entry = 0x80d1ff,
        .ret_op = 0x80d2e5,  // the RTS all eleven paths converge on
        .ret_kind = COSIM_RTS,
        .run = shim_player_state_normal,
        .supported = guard_player_state_normal,
        // 2,104..7,012, call-weighted over 24,419 calls on five movies. The
        // floor is `floor_effect` plus the four countdowns and nothing else --
        // which is most frames -- and the ceiling is a button edge that reaches
        // `apu_play_sfx`, whose cost is how long the SPC700 took to acknowledge
        // the previous sound.
        .cycles = 2434,
        // It pushes nothing of its own: two bytes for the `JSR $E86D`, and
        // `floor_effect`'s own sixteen under that.
        .stack_bytes = 18,
    },
    {
        .name = "sprite_cache_init",
        .symbol = "$80:C05A",
        .entry = 0x80c05a,
        .ret_op = 0x80c07e,  // the RTL, after the two PLBs
        .ret_kind = COSIM_RTL,
        .run = shim_sprite_cache_init,
        // Counted, not measured — there is nothing here for `verify` to have
        // measured. The derivation is above the shim.
        .cycles = 346734,
        // `PHB` then `PEA $007E`: one byte and two, and the exit pulls all
        // three. Nothing else in the body touches the stack.
        .stack_bytes = 3,
        .run_only = true,
    },
    {
        .name = "blockmap_expand",
        .symbol = "$80:AD2B",
        .entry = 0x80ad2b,
        .ret_op = 0x80ad91,  // the RTL, after the PLD that restores the caller
        .ret_kind = COSIM_RTL,
        .run = shim_blockmap_expand,
        .supported = guard_blockmap_expand,
        // Never used: the shim reports the call's exact cost, and every call
        // reports one. It is the level-1 figure, so a reader who wants a sense
        // of the scale has one and nothing silently falls back to it.
        .cycles = 3604294,
        // `PHD` then `PEA $0000`, four bytes; the `PLD` two instructions later
        // takes the `PEA`'s back and the one at `$AD90` takes the `PHD`'s.
        .stack_bytes = 4,
        .run_only = true,
    },
};

const CosimRoutine* cosim_routines(int* count) {
  *count = (int)(sizeof ROUTINES / sizeof ROUTINES[0]);
  return ROUTINES;
}
