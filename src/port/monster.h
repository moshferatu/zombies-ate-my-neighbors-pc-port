// The big monster's walk cycle, and what it does with what it is carrying.
//
//   $81:C16B  monster_anim          advance the walk, pick a frame, set the flip
//   $81:C00B  monster_place_carried put the held record where the facing says
//   $81:BB75  monster_seek          is anything worth chasing, and is anyone left
//   $81:BBA4  monster_deliver       am I home yet, and is anything worth chasing
//
// `port/collide.h` already has this creature's two collision handlers —
// `$81:C4A6 monster_collide` and `$81:C440 monster_c440_collide`, the same
// thing one stage apart — and calls it "the monster side, the one that takes
// objects out from under the player". These two are the other half of it: the
// per-frame work its thread does between yields, which is drawing and carrying
// rather than colliding.
//
// The thread is at `$81:C201`, and it opens by charging **28** to `W_SPAWN_LOAD`
// — the fifth-heaviest weight of the twenty-one in that census — installing
// `$81:C440` as its collision handler and then looping on
// `thread_yield : JSR $BB75 : JSR $C16B`. So one of these two runs every frame
// the creature is alive, 11,862 times over the corpus, which is why a routine
// this small is worth a registry slot.
//
// ## Facing is a doubled direction and it is used at three different scales
//
// `$14` is the facing already doubled, the currency `step_propose`'s `$24` and
// `actor_step_bearing`'s argument are both in: `$00` still, then clockwise from
// up in twos to `$10`. This file multiplies it by two again into `$2C` and by
// four again into the frame index, and the three scales are what the two tables
// are indexed at:
//
//   * `$2C = $14 * 2` indexes `$81:C026`, four bytes of `{i16 dx, i16 dy}` per
//     direction — where the carried record is held.
//   * `(($14 * 2) | phase) * 2` indexes `$81:C1B1`, eight bytes of four
//     metasprite pointers per direction — the walk cycle.
//
// The `ORA` is a concatenation only because `$14 * 2` has its low two bits
// clear, which is exactly the statement that `$14` is already doubled. Written
// as an `ORA` rather than an `ADC` it is also the ROM asserting that: an `ORA`
// that overlapped would silently pick another direction's frames.
//
// ## Nine directions, four sprite sets, and the mirror
//
// The frame table has nine groups and they are not nine drawings. Up has its
// own; down and "not moving" share one; the six diagonals and the two
// horizontals share a third — and the three west-facing groups are the *same
// pointers* as the three east-facing ones. What tells them apart is the flip:
//
//     CPX #$0030 : BCS + : LDA $0000,Y : AND #$FFFD : STA $0000,Y
//     + : LDA $0000,Y : ORA #$0002 : STA $0000,Y
//
// `$30` is the sixth group, so directions `$0C`, `$0E` and `$10` — down-left,
// left and up-left — get `ACTOR_FLIP`'s bit 1 set and the other six get it
// cleared. Four sprite sets and a mirror is the whole of an eight-way walk.
//
// ## The asymmetry at the two exits
//
// The mirrored path **returns without calling `$81:C00B`**, and the unmirrored
// one falls into it. So on the frame a west-facing monster advances its walk
// cycle, whatever it is carrying is not repositioned — it catches up on the
// next frame, when the timer has not expired and the `BPL` takes the same
// shortcut into `$C1A1` with `$2C` still holding the facing from before. The
// carried record therefore trails the monster's facing by up to one frame, in
// one half of the compass and not the other. That is what the ROM does; why it
// is written that way is not recorded anywhere and is not guessed at here.
//
// Port code: libc only.

#ifndef PORT_MONSTER_H
#define PORT_MONSTER_H

#include <stdbool.h>
#include <stdint.h>

#include "assets/rom.h"
#include "port/wram.h"

// --- Direct page: the monster thread's own page ---
#define MONSTER_DP_STATE 0x12   // the next state body, less one; see below
#define MONSTER_DP_RECORD 0x08  // its display record
#define MONSTER_DP_X 0x0a       // ...and where it is, which the record follows
#define MONSTER_DP_Y 0x0c
#define MONSTER_DP_FACING 0x14  // a direction, already doubled
#define MONSTER_DP_PHASE 0x1c   // 0..3, the leg the walk cycle is on
#define MONSTER_DP_TIMER 0x1e   // frames until the next leg
#define MONSTER_DP_TARGET 0x24  // what it has decided to chase, or zero
#define MONSTER_DP_HELD_KIND 0x26  // the collision id of what it grabbed
#define MONSTER_DP_CARRIED 0x28  // the record it is holding, or $FFFF
#define MONSTER_DP_LEAVE 0x2a    // nonzero ends the thread; see below
#define MONSTER_DP_FRAME_INDEX 0x2c  // `$14 * 2`, parked for `$81:C00B`
#define MONSTER_DP_HOME_X 0x2e  // where it was spawned, and where it goes back
#define MONSTER_DP_HOME_Y 0x30

// --- $81:C00B  monster_place_carried ----------------------------------------
//
// Put the held record at the monster's own position plus the offset its facing
// names: 24 pixels to whichever side it is facing and 14 up, or 24 up when it
// faces north, or 8 down when it faces south. The one direction with no offset
// is `$00`, standing still, and the entry for it is a pair of zeroes rather
// than a special case.
//
// `$FFFF` in `$28` means it is holding nothing, and the guard is a `CPY` before
// the `TAX` — so on that path the index register the caller passed in A is
// never even moved to X. Nothing else in the routine is conditional.
//
// The flags on the working path are the **second** `ADC`'s, the Y one, and
// carry is a real arithmetic carry out of a coordinate addition rather than a
// verdict about anything. V is not claimed: it is that `ADC`'s on one path and
// the caller's own on the other, and the single caller reads neither.
#define MONSTER_PLACE_CARRIED_ENTRY 0x81c00bu

// Nine four-byte `{i16 dx, i16 dy}` entries.
#define MONSTER_CARRY_TABLE 0x81c026u
// `CPY #$FFFF` — holding nothing.
#define MONSTER_CARRY_NONE 0xffff

typedef struct {
  uint16_t a, x, y;
  bool n, z, c;
} MonsterCarryRegs;

// `index` is A on entry, which is `$2C` — the facing doubled twice. It is also
// what comes back in A when the guard fires, because the `TAX` is on the other
// side of it; `in_x` is there for the same reason, and is the index register
// the caller still has.
void monster_place_carried(Wram* w, const Rom* rom, uint16_t dp, uint16_t index,
                           uint16_t in_x, MonsterCarryRegs* out);

// --- $81:C16B  monster_anim -------------------------------------------------
//
// One frame of the walk: count the timer down, and on the frame it goes
// negative take the next leg of the cycle, write the metasprite into the record
// and set the flip bit from the facing. Then, unless the facing was a mirrored
// one, place whatever is being carried.
//
// `DEC $1E : BPL` reloads with 2, so the timer runs 2, 1, 0, -1 and a leg lasts
// **three frames**; four legs is a twelve-frame stride.
//
// The metasprite bank is a constant, `LDA #$0090`, and it is written on every
// advancing frame even though it never changes — which is `port/oam.h`'s
// `SPRITE_META_BANK_HI` and so the top of the two banks the draw pass will
// accept.
#define MONSTER_ANIM_ENTRY 0x81c16bu

// Nine groups of four `u16` metasprite pointers.
#define MONSTER_FRAME_TABLE 0x81c1b1u
#define MONSTER_META_BANK 0x0090
// `LDA #$0002 : STA $1E` — three frames a leg, counted 2, 1, 0.
#define MONSTER_ANIM_PERIOD 0x0002
#define MONSTER_PHASE_MASK 0x0003
// `CPX #$0030` — the first frame-table index that draws mirrored.
#define MONSTER_FLIP_FROM 0x0030
// Bit 1 of `ACTOR_FLIP`: the emitter that draws the metasprite reversed in X.
#define MONSTER_FLIP_X 0x0002

// Both exits are shared with `monster_place_carried`, so the outputs are its
// on every path but the mirrored one.
typedef MonsterCarryRegs MonsterAnimRegs;

// A on entry is dead — the first instruction is `DEC $1E` — so only `in_x` is
// wanted, and only for the frame where the timer has not expired and the
// routine reaches `$81:C00B` without having run its own `TAX`.
void monster_anim(Wram* w, const Rom* rom, uint16_t dp, uint16_t in_x,
                  MonsterAnimRegs* out);

// --- $81:BB75 and $81:BBA4  the two states that decide where it goes ---------
//
// The creature's thread is a small state machine, and `monster_anim` above is
// the half of each state that draws. These two are the other half. Four of its
// state bodies open with a pair of `JSR`s and nothing else:
//
//     $81:C226  JSR $BB75 : JSR $C16B    the wander
//     $81:C2BB  JSR $BBA4 : JSR $C16B    ...carrying somebody home
//     $81:C355  JSR $BB75 : JSR $C16B
//     $81:C3DF  JSR $BB75 : JSR $C16B
//
// so three of the four decide where to go with `$81:BB75` and the carrying one
// uses `$81:BBA4`. The chase itself, at `$81:BEE3`, is a fifth body and is not
// built this way. It is in readable C in `port/chase.h`.
//
// ## The state word, and the `JMP` that is really a `JSR`'s tail
//
// A state body is entered by a computed `RTS`, not a call:
//
//     $81:C21E  PEA $C225 : LDA $12 : DEC A : PHA : RTS
//
// so `$12` holds the address of the next body and the `DEC` is the off-by-one
// an `RTS` needs. Both routines here **end by writing that word and returning**,
// via a two-instruction stub they share:
//
//     $81:BEDA  JMP $BEDD
//     $81:BEDD  LDA #$BEE3 : STA $12 : RTS
//
// It is reached by `JMP` and not `JSR`, so the `RTS` at `$81:BEE2` is the one
// that returns to whoever called `$81:BB75` — the stub is a tail, and a routine
// with three exits therefore has its `RTS` in three different places. The
// harness only needs one of them (`CosimRoutine::ret_op` is where a substituted
// call is *sent*, and the return is detected by address and stack pointer), so
// this costs the port nothing but is why the entry names an address inside the
// routine rather than the obvious last instruction.
//
// **A is `$BEE3` on that path**, which is a real output: the caller does not
// read it, but it is the only exit of the four that leaves the high bit set.
#define MONSTER_STATE_STUB 0x81bedau
// `LDA #$BEE3` — the chase, at `$81:BEE3`; see `port/chase.h`.
#define MONSTER_STATE_CHASE 0xbee3u

// --- $81:BB75  monster_seek -------------------------------------------------
//
// Two questions asked of the same 32-slot scan, at two ranges.
//
//     dist = actor_nearest(x, y)
//     dist <  $B4 and not carrying  ->  chase it
//     dist <  $B4 and carrying      ->  do nothing
//     dist in [$B4, $D0)            ->  do nothing
//     dist >= $D0                   ->  is *either player* within $D0?
//                                       if not, INC $2A and the thread ends
//
// The gap between `$B4` and `$D0` is not hysteresis — nothing here is a state
// with an exit condition, and the routine is re-entered from scratch every
// frame. It is a dead band: **something 180 to 207 pixels away neither starts a
// chase nor counts as the board being empty**, so the monster stands there.
//
// ## The two ranges are asking about different populations
//
// `actor_nearest` looks at four collision ids — the two players, `$38` and
// `$01` — and `player_in_range` looks at exactly the two players. So the near
// test can be satisfied by something that is not a player at all, and the far
// test cannot. That asymmetry is the whole behaviour: the creature will walk
// towards any of the four, and it gives up and leaves only when **both players**
// are more than `$D0` away. A board with a player at `$D1` and a `$38` at `$40`
// takes the near exit and never reaches the give-up test at all.
//
// `$2A` is incremented rather than set, and the thread reads it as
// `LDA $2A : BEQ` at the bottom of its loop — so one frame with nobody in range
// is enough. It is also the same word a `BPL` later decides the *manner* of the
// exit by: positive, as an increment from zero leaves it, takes the quiet path
// that skips the drop and the sound effect. Something else writes it negative;
// that writer is not in this file.
#define MONSTER_SEEK_ENTRY 0x81bb75u
// `CMP #$00B4` — inside this, start chasing.
#define MONSTER_SEEK_NEAR 0x00b4u
// `CMP #$00D0` — outside this, ask whether either player is still about, and
// the same word is the range that question is asked at.
#define MONSTER_SEEK_FAR 0x00d0u

typedef struct {
  uint16_t a, x, y;
  bool n, z, c;
} MonsterSeekRegs;

// `dp` is the thread's direct page. Nothing arrives in a register: the entry
// instruction is `STZ $24` and the point comes out of `$0A`/`$0C`.
void monster_seek(Wram* w, uint16_t dp, MonsterSeekRegs* out);

// --- $81:BBA4  monster_deliver ----------------------------------------------
//
// **Am I standing where I started?** `$81:B9F9`, the thread's own setup, writes
// the spawn point into `$2E`/`$30` at the same time as into `$0A`/`$0C`:
//
//     $81:B9FD  LDA $00 : STA $0A : STA $2E
//     $81:BA03  LDA $02 : STA $0C : STA $30
//
// and this routine measures back to it. Within sixteen pixels on **both** axes
// — a square, tested as two independent absolute differences and not a radius —
// whatever is being carried is freed, `$28` goes back to `$FFFF` and `$26` to
// zero, which is the monster reaching its lair and dropping the victim in it.
//
// The check is `|dx| < $10 && |dy| < $10 && $28 != $FFFF`, in that order, and
// all three failures land on the same instruction. There is no coverage site
// distinguishing "not home" from "home but empty", because there is no
// behavioural difference: both fall through to the second half.
//
// ## The second half is `monster_seek`'s tail, one test short
//
// From `$81:BBD6` the two routines are the same four instructions — clear the
// target, scan, chase anything inside `$B4` — with two differences, and both
// are tests this one leaves out.
//
// It **does not test `$26`**. `monster_seek` refuses to start a chase while the
// creature is holding something; this one does not ask, so a monster still
// carrying a victim across the level will drop into the chase state the moment
// anything comes within `$B4` — with `$28` still pointing at whoever it is
// holding. Nothing here undoes that, and the drop-at-home test it just failed
// is the only thing that would have.
//
// It **has no far test** either, so `$2A` is never incremented from this state
// and a monster carrying a victim never gives up and leaves. Both omissions are
// what the two routines' shapes differ by; why they are omitted is not recorded
// anywhere and is not guessed at here.
#define MONSTER_DELIVER_ENTRY 0x81bba4u
// `CMP #$0010` on each axis, against the absolute difference.
#define MONSTER_LAIR_RADIUS 0x0010u

typedef MonsterSeekRegs MonsterDeliverRegs;

// `dp` is the thread's direct page; nothing arrives in a register here either.
void monster_deliver(Wram* w, uint16_t dp, MonsterDeliverRegs* out);

#endif
