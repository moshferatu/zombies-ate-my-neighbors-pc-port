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
// The registry
// ---------------------------------------------------------------------------

// `cycles` is the mean cost of the ROM's own instructions, as `zamn_cosim
// verify` measured it over movies/level1.zmv. It is what a substituted call
// burns in native mode so the rest of the machine still sees a call that took
// about as long as it used to. Re-measure and update these if a routine's ROM
// side is ever re-read; `verify` prints the range it saw alongside the mean.
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
        .cycles = 304,
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
        .cycles = 3057,
        .stack_bytes = 0,   // pushes nothing
    },
    {
        .name = "vbl_queue_a_add",
        .symbol = "$80:83AE",
        .entry = 0x8083ae,
        .ret_op = 0x8083d2,  // RTL
        .ret_kind = COSIM_RTL,
        .run = shim_vbl_queue_a_add,
        .cycles = 693,
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
};

const CosimRoutine* cosim_routines(int* count) {
  *count = (int)(sizeof ROUTINES / sizeof ROUTINES[0]);
  return ROUTINES;
}
