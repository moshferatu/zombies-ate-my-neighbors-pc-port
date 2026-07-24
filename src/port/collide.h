// The door into actor behaviour: the callback dispatcher, and the two handlers
// a collision reaches.
//
// `port/oam.h` ends at `actor_collide_notify` (`$80:BE8F`), which hands a
// touching pair to `$80:8480` twice — once per actor. Until now that was where
// the port stopped, and `zamn_cosim` measured the size of the hole rather than
// filling it: on `movies/level1-rescue.zmv` **1,226 of 1,226** collisions enter
// a handler, and a census found that 1,225 of them reach exactly **two**
// handlers. This is those two, plus the dispatcher that enters them.
//
//   $80:8480  thread_call_handler  build the frame, swap direct page, RTL in
//   $80:F7F7  player_collide       the player's; jump-tables on the other's id
//   $80:F950  player_collide_hurt  the entry 1,225 of 1,225 of them land on
//   $81:8888  enemy_collide        an enemy's; the mirror image of the player's
//
// ## Why this is where the direct page stops being pinned
//
// `docs/frame-skeleton.md` recorded that direct page is `$0000` for the whole
// game. That is true of the scheduler, NMI and every routine ported before this
// one — and false here. `$80:84A2  TCD` installs the *target thread's* page,
// read from a 24-entry table at `$80:82DE`, before entering its handler. So a
// handler's `LDA $70` is not `$7E:0070`; it is offset `$70` into that thread's
// own 128-byte page, somewhere in `$7E:0100-$7E:0CFF`.
//
// That is the answer to a question PROGRESS.md has been carrying since Phase 2:
// **an actor's state is its thread's direct page.** There is no separate actor
// slot table. The fields below are the ones these two handlers touch, and they
// are offsets from `D`, not WRAM addresses — which is why they are named here
// and not in `port/wram.h`, and why none of them is in `tools/symbols/zamn.sym`
// (a symbol file keyed by absolute address has nowhere to put them).
//
// ## What this port covers, and what it declines
//
// Both handlers are ported as far as the branch that *does nothing*, which on
// the movie that reaches them is almost all of it: the player's hit path
// (`$80:F950`) in full, and the enemy's "not my kind of collision" early-out.
// Taking damage — `$81:88A4` onwards, which subtracts from health and then
// either jumps into the death routine or `$81:8506` — is not ported, and neither
// is the sound effect `$80:F92D` plays, because it talks to the APU rather than
// to WRAM. Both decline, and `zamn_cosim` counts them.
//
// Port code: libc only.

#ifndef PORT_COLLIDE_H
#define PORT_COLLIDE_H

#include <stdbool.h>
#include <stdint.h>

#include "assets/rom.h"
#include "port/wram.h"

// --- A thread's direct page: the fields the collision handlers touch ---------
//
// Offsets from `D`, so an actual WRAM offset is `dp + ACTOR_DP_x`. Every name
// below is what *these routines* do with the field and no more; `$1E` in
// particular is health to an enemy and something else to the player, which is
// exactly what you would expect of a page each actor's own code lays out.

// The player's index, already doubled — 0 or 2. Used to index the per-player
// arrays in `$7E:1Cxx`. `$80:8874` writes those arrays with the same doubled
// index and `$80:D206`'s `LDA $006E,X` reads that player's joypad with it.
#define ACTOR_DP_PLAYER 0x0e
// Health, to anything that has any. `$81:88A5` subtracts a damage-table entry
// from it and runs the death path when the result goes negative.
#define ACTOR_DP_HEALTH 0x1e
// The argument `enemy_collide` parks before masking it. Scratch: it is written
// on entry and consumed by the branches below.
#define ACTOR_DP_HIT_ID 0x22
// A request word the actor's own per-frame code consumes and clears —
// `$80:D050  BIT $50 : STZ $50` is the other end of it. `$80:F950` posts
// `$8001` here to say "you were hit".
#define ACTOR_DP_EVENT 0x50
// Hit-recovery: `$80:F96C  BPL` ignores a hit unless this has gone negative,
// and taking one sets it back to $40. Classic invulnerability frames.
#define ACTOR_DP_HURT_TIMER 0x52
// The record this actor collided with, as `$80:F801` files it for the actor's
// own code to read later.
#define ACTOR_DP_COLLIDER 0x58
// Which branch of the actor's own state machine is running — `$80:D1EA  LDX $70
// : JMP ($D1EF,X)` is the jump it indexes. States 2 and 4 ignore collisions.
#define ACTOR_DP_STATE 0x70

// --- $80:8480 ---------------------------------------------------------------

// 24 x u16, in ROM: the direct page each scheduler slot runs on. They tile
// `$7E:0100-$7E:0CFF` at stride $80, and `$80:84A2` is what installs one.
// Indexed by a slot already doubled, like every other per-thread table.
#define THREAD_DP_TABLE 0x8082deu

// What a handler leaves behind.
//
// This is the one place port code names registers, and it is deliberate rather
// than a lapse: `$80:8480` *acts* on one of them — a handler that returns with
// carry set has its thread parked at `$80:84A8` — and hands the rest straight
// back to its caller. They are the routine's outputs, not the calling
// convention's, so modelling them here is what keeps `src/cosim/routines.c` a
// translator instead of a second implementation.
typedef struct {
  uint16_t a, x, y;
  bool n, z, c;
} ActorHandlerRegs;

// What the dispatcher did.
//
// `entered` is the `BEQ` at `$80:8486`: a slot with no handler registered makes
// the whole routine three instructions that write nothing. It is separate from
// the register fields because N and Z depend on it and on nothing else the port
// can see — they come from that `ORA` when no handler ran, and from the `PLB`
// that restores the caller's data bank when one did. Restoring a saved register
// is calling convention, so the shim finishes that half.
typedef struct {
  bool entered;
  uint16_t a, x, y;
  bool c;
} ThreadCallResult;

// Enter thread `slot`'s registered handler with one word of argument.
//
// `slot` is already doubled, as `ACTOR_THREAD` holds it and as every per-thread
// table is indexed. `carry_in` is the caller's carry, which the no-handler path
// passes through untouched.
//
// **False means the port does not have this handler**, and nothing has been
// written — the far address in `thread_handler`/`thread_handler_bank` is not one
// of the two below. That is the honest shape of "the door into actor behaviour":
// the door itself is transcribable, and what is behind it is a list that grows
// one routine at a time. `zamn_cosim` counts the declines by name.
bool thread_call_handler(Wram* w, const Rom* rom, uint16_t slot, uint16_t arg,
                         bool carry_in, ThreadCallResult* out);

// --- $80:F7F7 ---------------------------------------------------------------

#define PLAYER_COLLIDE_ENTRY 0x80f7f7u
// 57 entries of one word each, in ROM, indexed by the other actor's collision
// id already doubled. Every target is in bank $80, because `JSR ($F808,X)` is a
// same-bank indirect jump.
#define PLAYER_COLLIDE_TABLE 0x80f808u
#define PLAYER_COLLIDE_NOP 0xf87au   // a bare RTS: this id does nothing
#define PLAYER_COLLIDE_HURT 0xf950u  // the hit path — see collide.c

// Ids at or above this are the player's own side of a collision, and each
// handler ignores the ids that belong to the other. That is what makes exactly
// one side of every pair do real work.
#define COLLIDE_ID_PLAYER 0x005c

// The one `$7E:1CBC` value the hit path treats specially, and how long a hit
// locks the next one out for. Both are bare constants in `$80:F950`.
#define PLAYER_WEAPON_IMMUNE 0x0004
#define PLAYER_HURT_TIMER_RESET 0x0040

// The player thread's handler. `arg` is the *other* actor's collision id and
// `dp` is the player's direct page, which the dispatcher has already installed.
// `r` comes in holding the registers the handler was entered with.
//
// False if the id's jump-table entry is a routine the port does not have. Two
// of the 57 entries are ported: `$80:F87A`, which is a bare `RTS`, and
// `$80:F950`, the hit path.
bool player_collide(Wram* w, const Rom* rom, uint16_t dp, uint16_t arg,
                    ActorHandlerRegs* r);

// --- $81:8888 ---------------------------------------------------------------

#define ENEMY_COLLIDE_ENTRY 0x818888u

// An enemy thread's handler, and the mirror image of the player's: ids *below*
// `COLLIDE_ID_PLAYER` are somebody else's business and it returns having written
// nothing at all. That branch is 1,225 of the 1,226 dispatches
// `movies/level1-rescue.zmv` produces, and it is what this port serves.
//
// False for an id it would act on — `$81:889F` onwards subtracts a damage table
// entry from `ACTOR_DP_HEALTH` and leaves through one of three routines nobody
// has ported.
bool enemy_collide(Wram* w, uint16_t dp, uint16_t arg, ActorHandlerRegs* r);

#endif
