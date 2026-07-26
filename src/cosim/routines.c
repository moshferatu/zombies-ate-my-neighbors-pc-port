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
#include "port/collide.h"
#include "port/fade.h"
#include "port/oam.h"
#include "port/player.h"
#include "port/score.h"
#include "port/sprite_cache.h"
#include "port/thread.h"

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

static void shim_sprite_frame_tile(Wram* w, const Rom* rom, const CosimRegs* in,
                                   CosimRegs* out) {
  (void)rom;
  uint16_t queued_before = wram_r16(w, W_SPRITE_UPLOAD_COUNT);

  out->a = sprite_frame_tile(w, in->a);

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
static void queue_flags(Wram* w, uint32_t count_at, uint16_t cap, bool added,
                        CosimRegs* out) {
  uint16_t count = wram_r16(w, count_at);
  if (added) {
    // `INC` of the count is the last thing to run, and it leaves carry alone.
    out->n = (count & 0x8000) != 0;
    out->z = count == 0;
    out->c = false;
  } else {
    // Refused: N and Z are what the `CPY` that refused produced.
    uint16_t diff = (uint16_t)(count - cap);
    out->n = (diff & 0x8000) != 0;
    out->z = diff == 0;
    out->c = true;
  }
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

static void shim_actor_depth_sort(Wram* w, const Rom* rom, const CosimRegs* in,
                                  CosimRegs* out) {
  (void)rom;
  uint16_t tail = actor_depth_sort(w);

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

static void shim_actor_cull(Wram* w, const Rom* rom, const CosimRegs* in,
                            CosimRegs* out) {
  (void)rom;
  actor_cull(w);

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

static void shim_oam_buffer_clear(Wram* w, const Rom* rom, const CosimRegs* in,
                                  CosimRegs* out) {
  (void)rom;
  (void)in;
  oam_buffer_clear(w);

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
  // *which* handler, and a count with no address is not a work list. Read the
  // same two words the dispatcher reads and record it.
  //
  // Only when the entry is a handler the port has never heard of. A decline
  // through `player_collide` or `enemy_collide` is a decline further down —
  // a jump-table entry, or an enemy that survived — and censusing the door it
  // came through would name the wrong routine. Those have coverage sites of
  // their own, and `player_collide`'s guard censuses its table entry below.
  uint32_t entry =
      ((uint32_t)(wram_r16(scratch, W_THREAD_HANDLER_BANK + in->x) & 0xff) << 16) |
      wram_r16(scratch, W_THREAD_HANDLER + in->x);
  if (entry != PLAYER_COLLIDE_ENTRY && entry != ENEMY_COLLIDE_ENTRY)
    cosim_census_note("handler", entry);
  return false;
}

static void shim_thread_call_handler(Wram* w, const Rom* rom,
                                     const CosimRegs* in, CosimRegs* out) {
  ThreadCallResult t;
  thread_call_handler(w, rom, in->x, in->y, in->c, &t);  // the guard allowed it
  handler_exit(in, &t, out);
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

static void shim_player_collide(Wram* w, const Rom* rom, const CosimRegs* in,
                                CosimRegs* out) {
  ActorHandlerRegs r = {.a = in->a, .x = in->x, .y = in->y};
  player_collide(w, rom, in->d, in->a, &r, NULL);  // the guard allowed it
  handler_regs(&r, out);
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
// $81:8888  enemy_collide — the same argument, on an enemy's page
// ---------------------------------------------------------------------------

// Carry is an input as well as an output, and only on one path: `enemy_die`
// hands whatever it arrived with to `score_add`, whose discard path passes it
// straight through to the `RTL`. Every other exit sets it outright.
static bool guard_enemy_collide(Wram* scratch, const Rom* rom,
                                const CosimRegs* in) {
  ActorHandlerRegs r = {.a = in->a, .x = in->x, .y = in->y, .c = in->c};
  return enemy_collide(scratch, rom, in->d, in->a, &r);
}

static void shim_enemy_collide(Wram* w, const Rom* rom, const CosimRegs* in,
                               CosimRegs* out) {
  ActorHandlerRegs r = {.a = in->a, .x = in->x, .y = in->y, .c = in->c};
  enemy_collide(w, rom, in->d, in->a, &r);  // the guard allowed it
  handler_regs(&r, out);
}

// ---------------------------------------------------------------------------
// $81:FE0E  shot_collide — the same argument again, on a weapon shot's page
// ---------------------------------------------------------------------------

// Carry is an input on exactly one of its three paths: id 0 reaches the `RTL`
// without executing a single `CMP`, so what leaves in carry is what arrived.
// The other two paths set it. No guard — the port has all of this routine, so
// there is no condition under which it could decline.
static void shim_shot_collide(Wram* w, const Rom* rom, const CosimRegs* in,
                              CosimRegs* out) {
  (void)rom;  // no table, no ROM read
  ActorHandlerRegs r = {.a = in->a, .x = in->x, .y = in->y, .c = in->c};
  shot_collide(w, in->d, in->a, &r);
  handler_regs(&r, out);
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

static void shim_actor_collide_notify(Wram* w, const Rom* rom,
                                      const CosimRegs* in, CosimRegs* out) {
  uint16_t a = notify_outer(w);
  ThreadCallResult tail = {.c = in->c};
  actor_collide_notify(w, rom, a, in->x, &tail);  // the guard allowed it

  // The routine's last instruction is the second `JSL $80:8480`, so everything
  // it returns is really the dispatcher's — including X and Y, which are the
  // arguments `$80:BEC0`/`$80:BEC2` set up and which the dispatcher hands back
  // untouched. So this defers to `handler_exit` rather than restating it, which
  // keeps one description of that tail rather than two.
  handler_exit(in, &tail, out);
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

static void shim_actor_overlap_pass(Wram* w, const Rom* rom, const CosimRegs* in,
                                    CosimRegs* out) {
  actor_overlap_pass(w, rom);  // the guard already established it will not decline

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
// $80:BD1F  sprite_build_oam — no arguments, and it may decline
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
  (void)in;
  return sprite_build_oam(scratch, rom);
}

static void shim_sprite_build_oam(Wram* w, const Rom* rom, const CosimRegs* in,
                                  CosimRegs* out) {
  (void)in;
  sprite_build_oam(w, rom);  // the guard already established it will not decline

  // The tail at `$80:BDD2` is what decides all of this, and it runs on every
  // path: `LDA $20 : AND #$0003 : TAX : LDA $BDE6,X : AND #$00FF : STA $1B64 :
  // SEC : RTL`.
  //
  //   * A is the table entry after the mask — the value just stored.
  //   * X is the tick's low two bits, from the `TAX`.
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
  out->x = (uint16_t)(wram_r16(w, W_SCHED_TICK) & 3);
  out->y = 0;
  out->n = (out->a & 0x8000) != 0;
  out->z = out->a == 0;
  out->c = true;
  out->flags = COSIM_FLAG_N | COSIM_FLAG_Z | COSIM_FLAG_C;
}

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
        // By far the most expensive routine substituted so far, and the most
        // variable: 5,864 when nothing is on screen, 66,412 when everything is.
        // A whole NTSC frame is about 57,000 master cycles, so this one pass is
        // most of the game's per-frame CPU budget.
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
};

const CosimRoutine* cosim_routines(int* count) {
  *count = (int)(sizeof ROUTINES / sizeof ROUTINES[0]);
  return ROUTINES;
}
