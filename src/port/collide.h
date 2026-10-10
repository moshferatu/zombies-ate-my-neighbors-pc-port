// The door into actor behaviour: the callback dispatcher, and every handler a
// collision on the current movies reaches.
//
// `port/oam.h` ends at `actor_collide_notify` (`$80:BE8F`), which hands a
// touching pair to `$80:8480` twice — once per actor. That was once where the
// port stopped, and `zamn_cosim` measured the size of the hole rather than
// filling it: on `movies/level1-rescue.zmv` **1,226 of 1,226** collisions enter
// a handler. The census then named the handlers one at a time, and the five
// below were the first of them:
//
//   $80:8480  thread_call_handler  build the frame, swap direct page, RTL in
//   $80:F7F7  player_collide       the player's; jump-tables on the other's id
//   $80:F950  player_collide_hurt  the entry 1,225 of 1,225 of them land on
//   $81:8888  enemy_collide        an enemy's; the mirror image of the player's
//   $81:FE0E  shot_collide         a weapon shot's; four ids stop it, the rest
//                                  it flies through
//   $83:A364  victim_collide       a victim's; eight ids, eight endings, latched
//
// **That list is a snapshot, not the inventory.** It was written when it was
// complete and the dispatch chain in `thread_call_handler` has grown past it
// several times since — there are twenty-five addresses in that chain now, and
// the `else if` ladder is the only place they are all written down. A round was
// planned off this comment once, on the strength of its saying "five"; read the
// code below it instead.
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
// Every handler is ported through every outcome any movie has reached: the
// player's hit path (`$80:F950`) in full, the enemy's "not my kind of collision"
// early-out, the enemy's acting branch as far as dying — which is the one
// outcome `movies/level1-rescue.zmv` produces — and all of `shot_collide` and
// `victim_collide`, both of which are small enough to be here whole.
//
// **The audio wall is down.** Two jump-table entries used to stop here because
// they open with `JSL apu_play_sfx` and nobody had decided how port code drives
// the APU. `port/apu.h` is that decision, and both are ported: `$80:F92D`,
// whose entire reaction is a noise, and `$80:F87B`, the player's side of a
// pickup — a BCD add into an inventory counter, capped at `$0999`, which is the
// arithmetic that was stuck behind the noise.
//
// `$80:F87B` also has a **tail call**: a player who picks something up while
// holding no weapon falls into `$80:EA63`, which selects one for them. That is
// ported too, in `port/player.h`, because following the census there is what
// turned up the fact that `B` cycles weapons.
//
// Two collision ids (`$81:83C6`, `$81:847E`) have routines of their own, and no
// input has produced either, so both decline by name and wait for a movie rather
// than for code. An enemy that *survives* a hit used to be on that list;
// `movies/level53.zmv` reached it and `$81:8506` is ported below.
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
// An enemy's death request, and the field that makes `$81:8727` worth porting
// rather than transcribing. The enemy's own thread body clears it before its
// main loop (`$81:882B  STZ $12`) and reads it once per pass (`$81:8842  LDA
// $12 : BEQ <loop>`): zero means carry on, anything else means leave, and
// `ENEMY_DEATH_REQUEST` is the value that also means "a player killed me", which
// `$81:8846` tests for before bumping the kill counter at `$7E:1F64`. Six live
// enemy pages in the trace all use it this way.
#define ACTOR_DP_DEATH_REQ 0x12
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
// Zeroed by an enemy's init (`$81:87F5`) and again on the way into the death
// path (`$81:88B9`), and read by nothing any trace has seen. Named for where it
// is rather than for what it means, because the evidence does not say — the
// same treatment `port/wram.h` gives `$38`.
#define ACTOR_DP_SCRATCH_7E 0x7e

// A victim's own display record — the *address* of it, and the same field the
// shot keeps at `$0A`. Two pages, two offsets, one meaning: each actor's code
// lays out its own page, so there is no reason for them to agree and they do
// not. `$83:A213  LDY #$0004 : LDA ($08),Y : INC A : STA ($08),Y` walks it as a
// pointer twenty times over on the way out of a rescue, which is `ACTOR_Z`
// stepping up — a rescued victim rises twenty pixels — and that indirection is
// what proves it holds an address rather than a slot index.
#define VICTIM_DP_RECORD 0x08
// Which side claimed this victim, in `score_add`'s convention: bit 15 and
// nothing else. `$83:A1EA  LDA $18 : JSL $80C7D9` is the reader — the rescue
// thread on this same page — and the award that goes with it is the `$1000`
// the diff handed over when `score.c` was written.
//
// Measured rather than inferred: on `movies/level1-rescue.zmv` the id-5 path
// below runs and `score_slot_0` is credited four times against `score_slot_1`'s
// zero. So id 5 is one player claiming a victim, and this is where the game
// writes down which player it was.
#define VICTIM_DP_CLAIMANT 0x18
// What happened to this victim, latched. Zero means nothing yet, and the whole
// routine is guarded on that — the *first* thing to touch a victim decides what
// became of it and everything after is ignored.
//
// This is the third meaning `$1E` has had. It is health to an enemy
// (`ACTOR_DP_HEALTH`) and something else again to the player, which is what a
// page each actor's own code lays out looks like from the outside.
#define VICTIM_DP_EVENT 0x1e
// Read on one path only, and only to decide whether to switch the victim's
// collision off. Named for where it is rather than for what it means — the same
// treatment `ACTOR_DP_SCRATCH_7E` gets — because one `BNE` is not evidence of a
// meaning, and no input has taken both sides of it yet.
#define VICTIM_DP_FLAG_26 0x26

// The shot's own display record — the *address* of it, not an index. `$81:FA48
// LDX $0A : STA $0008,X` writes the metasprite pointer through it and
// `$81:FE21  LDY $0A : STA $000E,Y` writes `ACTOR_COLLIDE_ID`, so the same field
// the sprite pass reads is the one a shot switches off when it stops flying.
#define ACTOR_DP_RECORD 0x0a
// Frames of life left, on a weapon shot's page. `$81:FDD1  LDA #$0014 : STA $42`
// sets it when the shot launches and `$81:FD1E  DEC $42 : BNE <loop>` is the
// shot thread's whole main loop, so writing 1 here means "end on the next pass".
#define ACTOR_DP_LIFE 0x42

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
  // Set, on a `false` return only, to the handler address the port does not
  // have — and left zero when the routine *does* have the handler and the
  // handler itself declined one level down.
  //
  // The two are different findings and the census has to tell them apart: a
  // dispatch to an address nobody has written is a routine to port, while
  // `enemy_collide` handing back id `$5D` is a decline that `enemy_collide`'s own
  // registry entry already names by its real address. This field replaced a list
  // of the handlers that can decline internally, which had four of them on it and
  // by then wanted eight.
  uint32_t unported;
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
#define PLAYER_COLLIDE_SFX 0xf92du   // one id, and its whole reaction is a noise
#define PLAYER_COLLIDE_PICKUP 0xf87bu  // ids $0C..$20: the player takes an item
#define PLAYER_COLLIDE_ITEM 0xf8d6u    // 13 more ids: the player takes an *item*
// Four ids that are one routine written four times: play a noise, copy a
// position onto the page, spawn `$82:E0B4` with a **kind** in `$04`, and then do
// one thing that differs. Ids $2D, $2E, $2F, $30.
#define PLAYER_COLLIDE_SPAWN_0 0xfa26u
#define PLAYER_COLLIDE_SPAWN_1 0xfa4au
#define PLAYER_COLLIDE_SPAWN_2 0xfa79u
#define PLAYER_COLLIDE_SPAWN_3 0xfaa4u
// And id $27, which is the only one of the seventeen that heals you.
#define PLAYER_COLLIDE_HEAL 0xfacfu
// The last entry any input has ever asked for and the only one the census still
// named: **one call, on `movies/level29-firstaid.zmv`, in the whole corpus.**
// `LDA $70 : CMP #$0002 : BEQ : CMP #$0004 : BEQ : JMP $DC09` — the states-2-and-4
// guard `ACTOR_DP_STATE` already documents, written out longhand, with a tail
// jump for every other state.
#define PLAYER_COLLIDE_STATE_GATE 0xf9aeu

// **Two more of the same shape**, named by the nine-level round's census. All
// four of `$80:F979`, `$80:F999`, `$80:F9AE` and `$80:F9BE` open by testing
// `ACTOR_DP_STATE` against the same small set and returning if it matches; what
// differs is what they do when it does not, and how many states are in the set.
// Read as a group they are one idea written four times, which is the third time
// this table has done that (`$80:FA26`'s four spawns, `$80:F87B`/`$80:F8D6`).
//
// `$80:F9BE` is id `$0A`, and it is `$80:F9AE` with the tail inlined: two state
// tests and then `LDA #$F9D0 : STA $28` — a queue into `PLAYER_DP_NEXT` rather
// than a `JMP` to a routine that queues. What it queues is a routine that costs
// the player a point of health (`$80:F9D2  LDA $1CB8,X : BEQ : DEC A : STA`),
// plays sound `$19`, and runs an animation for `$4B` ticks.
#define PLAYER_COLLIDE_QUEUE 0xf9beu
#define PLAYER_QUEUE_NEXT 0xf9d0u

// `$80:F979` is id `$0B`, and it is the one that is not just a guard. It tests
// `ACTOR_DP_HURT_TIMER` first — the same `BPL` `$80:DC09` and `$80:F96C` open
// with, so a hit inside the invulnerability window does nothing — then **three**
// states rather than two, and then posts a hit: `ACTOR_DP_EVENT` = `$C000` and
// the recovery timer back to `$30`.
//
// That makes it a sibling of `$80:F950`, the ordinary hit path, with two
// constants changed: that one posts `$8001` and re-arms `$40`. Two kinds of
// being hurt, told apart by the word `$80:D050` reads out of `ACTOR_DP_EVENT`.
#define PLAYER_COLLIDE_HURT_ALT 0xf979u
#define PLAYER_EVENT_HURT_ALT 0xc000
#define PLAYER_HURT_ALT_TIMER 0x0030
// The third state this one also returns on, on top of the two above.
#define PLAYER_STATE_IGNORE_C 0x000e

// The fourth of the group: id `$35`, three state tests and then
// `JMP $80:E331`. For a long time no input carried id `$35` to the player.
// The second demo does, nine times, so it is ported now.
//
// What it jumps to takes the player over: the pose becomes `$80:E33E`, with
// thirty frames on the pose timer, the hit event is cleared, and the thread
// stops taking hits at all (`$80:F36C`, which clears its handler).
#define PLAYER_COLLIDE_STATE_GATE_E331 0xf999u
#define PLAYER_STATE_IGNORE_D 0x000c
#define PLAYER_E331_NEXT 0xe33eu
#define PLAYER_E331_TIMER 0x001e
#define PLAYER_DP_POSE_TIMER 0x16

// Id `$37`, the door a finished level opens (`exit_door_collide`). All the
// player's side does is queue the pose that leaves by it, `$80:FAF6`.
#define PLAYER_COLLIDE_EXIT 0xfaf0u
#define PLAYER_EXIT_NEXT 0xfaf6u

// --- $80:DC09, the tail ------------------------------------------------------

// A `JMP`, not a `JSR`, so this routine's `RTS` is the one the jump table's
// caller gets — which is why it is ported here rather than declined: it is not a
// separate call, it is the rest of this one.
//
// Three guards and one store. `BPL` on `ACTOR_DP_HURT_TIMER` — still
// recovering, so nothing; `BNE` on `ACTOR_DP_STATE` — any state but zero, so
// nothing (which makes the caller's `CMP #$0002`/`CMP #$0004` redundant for
// every state except those two, and they are the two that return early anyway);
// and a compare of `$10` against a constant that reads like a sentinel.
#define PLAYER_STATE_TAIL 0x80dc09u
// What it queues into `PLAYER_DP_NEXT` when all three guards pass.
#define PLAYER_STATE_TAIL_NEXT 0xdc1eu
// `$80:DC13  CMP #$FD72`. A magic word compared against `$10` and nothing else
// in reach explains it, so it is named for the comparison rather than for a
// meaning nobody has established.
#define PLAYER_STATE_TAIL_SENTINEL 0xfd72u
// `$10` and `$28` on the player's page. `$28` is the "what I do next" pointer —
// the same role `$0E`, `$12` and `$16` play on the three enemy pages, at a fourth
// offset, because every actor lays out its own page.
#define PLAYER_DP_TAIL_WORD 0x10
#define PLAYER_DP_NEXT 0x28
// The two states `$80:F9AE` returns on without doing anything — the ones
// `ACTOR_DP_STATE` already describes as ignoring collisions.
#define PLAYER_STATE_IGNORE_A 0x0002
#define PLAYER_STATE_IGNORE_B 0x0004

// What all four spawn — `LDA #$E0B4 : LDY #$0082` — and the four values of `$04`
// that tell it apart. The thread body is unported; what the port owns is the
// three words handed to it, which `thread_spawn` copies onto its page.
//
// It is **the thing you just picked up, flying away.** `$82:E0B4` allocates a
// display record at the position it was handed, takes its metasprite from a
// four-word table at `$82:E147` indexed by the kind, gives it no collision id at
// all, picks one of four diagonals from `$80:9D39`'s random number
// (`AND #$0003`), drifts it eight pixels a tick for 21 ticks and frees the slot.
// The four metasprites are `$8F:DCAA`, `$DCB3`, `$DCBC` and `$DCC5` — which are
// the last four entries of `$80:CA6C`, the object-type table, so the sprite that
// flies off is the object's own. That is why the kind is worth passing: it is
// which of the four bonus objects this was.
#define PLAYER_SPAWN_BODY 0xe0b4
#define PLAYER_SPAWN_BANK 0x0082

// The two tails that are counters. `$80:FA26` bumps one per player with nothing
// stopping it; `$80:FA4A` bumps a different one and refuses past 5. Named for
// where they are: no trace has seen either read.
//
// (The second has since been read, for `--infinite-lives`: it is the **lives**.
// `$80:8882` starts it at two, `$80:CEC5  DEC $1D4C,X : BMI` is a death and the
// game over, and so `$80:FA4A` is an extra life, of which five can be held.
// The name is left, being in the coverage table. See `src/cheats.h`.)
#define W_PLAYER_SPAWN_COUNT 0x1ff0
#define W_PLAYER_CAPPED_COUNT 0x1d4c
#define PLAYER_CAPPED_MAX 0x0005

// The two tails that are points. `LDX #$0500` and `LDX #$1000`, both BCD, both
// handed to `score_add`.
#define PLAYER_SPAWN_AWARD_2 0x0500
#define PLAYER_SPAWN_AWARD_3 0x1000

// `$80:FACF`: three health, ceilinged at the same ten `$80:EB2F` refuses to
// spend a kit at, and a different sound from all the rest.
#define PLAYER_HEAL_AMOUNT 3
#define PLAYER_HEALTH_MAX 0x000a
#define PLAYER_SFX_HEAL 0x0005

// The two sound effects those two entries play. Bare `LDA` operands, and the
// only thing `$80:F92D` does at all. `$80:F8D6` plays the pickup one too, which
// is the first thing that says the two routines are a pair.
#define PLAYER_SFX_TOUCH 0x0009
#define PLAYER_SFX_PICKUP 0x000e

// The first collision id that is an item. `$80:F885  SEC : SBC #$0018` turns a
// *doubled* id into a slot, so the constant in the listing is twice this.
#define PICKUP_ID_FIRST 0x000c
// 21 words in ROM, one per item id, in the same units as the counter they are
// added to — BCD. The last seven are zero, which is what the ids past the end
// of a 14-slot inventory are worth.
#define PICKUP_AMOUNT_TABLE 0x80f8acu
// `$80:F895  CMP #$0999`. Three digits is what the HUD has room for.
#define PICKUP_MAX 0x0999

// The same three constants for `$80:F8D6`, and the differences are the routine.
// `$80:F8E0  SEC : SBC #$0042` makes id $21 the first item; the amounts sit in
// the 19 words between the routine and `$80:F92D`, which is the tightest packing
// in the jump table's neighbourhood; and `$80:F8F0  CMP #$0099` is *two* digits,
// because the HUD counts items in a corner box rather than on an ammo bar.
#define ITEM_ID_FIRST 0x0021
#define ITEM_AMOUNT_TABLE 0x80f907u
#define ITEM_MAX 0x0099

// The position the four spawning entries hand to the thread they start —
// `$80:FA51  LDA $30 : STA $00` and `LDA $32 : STA $02`. Two words of the
// player's page copied to the top of it, because `thread_spawn` passes
// arguments by copying the caller's first five words onto the new thread's page.
#define PLAYER_DP_SPAWN_X 0x30
#define PLAYER_DP_SPAWN_Y 0x32
// ...and where the kind goes, which is the only thing that differs between the
// four: 0, 1, 2, 3.
#define PLAYER_DP_SPAWN_ARG 0x04

// The base of this player's inventory array, on the player's own page. Two
// values only, and they are the two words at `$80:EAA4` that `$80:EA63` indexes
// with the doubled player number — so the field is a cached pointer rather than
// anything the player chose.
#define ACTOR_DP_INVENTORY 0x64

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
// Ten of the 57 jump-table entries are ported. Five are diffed: `$80:F87A`, a
// bare `RTS`; `$80:F950`, the hit path; `$80:F92D`, one sound effect and nothing
// else; `$80:F87B`, a pickup, which twenty-one item ids share; and `$80:F8D6`,
// the same routine over a second array, which thirteen more share.
//
// **Five more are transcribed and not yet diffed**, and it is worth being blunt
// about the difference. `$80:FA26`, `$80:FA4A`, `$80:FA79` and `$80:FAA4` are
// one routine written four times — sound, position, a kind, `thread_spawn`, and
// one tail apiece — and `$80:FACF` is the only entry in the table that gives
// health back. Every routine *under* them is diffed on thousands of calls:
// `apu_play_sfx`, `thread_spawn`, `score_add`. What is unchecked is their own
// half-dozen stores, because **no input reaches them**: the objects that carry
// their ids are in levels 9, 17, 21, 25, 29, 33, 37, 41, 45, 49 and 53, and
// every one of them so far is behind a wall a route has not been cut through.
// `player_spawn_0`..`player_heal_entry` in the coverage report are what say so,
// and they should be read as a work list of movies rather than of code.
//
// False if the port could not finish, which is now two different things — an
// entry nobody has written, or the pickup's auto-select tail. `unported` is set
// to the ROM address it gave up at, so the decline census names the routine
// that is actually missing rather than the one it was reached through. NULL if
// the caller does not care.
bool player_collide(Wram* w, const Rom* rom, uint16_t dp, uint16_t arg,
                    ActorHandlerRegs* r, uint32_t* unported);

// The five ways out of `$80:F950`, the entry nearly every collision in the game
// lands on.
//
// The two state exits are separate blocks although the port tests them with one
// `||`: `CMP #$0002 : BEQ` and `CMP #$0004 : BEQ` are a chain, so state 4 is
// reached one comparison further in and costs 30 more. That is the same reason
// `shot_collide` splits its four stop ids, and it is the kind of difference only
// a cost model cares about — every other observer sees one outcome.
typedef enum {
  HURT_BLK_STATE_A,       // $80:F955 BEQ taken: state 2, over in three
                          // instructions
  HURT_BLK_STATE_B,       // $80:F95A BEQ taken: state 4, one CMP further
  HURT_BLK_STATE_PASS,    // neither, so $80:F95C reads the weapon
  HURT_BLK_WEAPON_OTHER,  // $80:F964 BNE taken: not the weapon that guards
  HURT_BLK_WEAPON_MATCH,  // ...not taken, so $80:F966 LDA $1E runs
  HURT_BLK_HEALTH_SET,    // $80:F968 BNE taken: guarded, and over
  HURT_BLK_HEALTH_ZERO,   // ...not taken, and the hit goes on to the timer
  HURT_BLK_TIMER,         // $80:F96A LDA $52, which every surviving path reads
  HURT_BLK_IFRAMES,       // $80:F96C BPL taken: still recovering, so no hit
  HURT_BLK_TAKEN,         // ...not taken: the two stores that post the hit
  HURT_BLK_RTS,           // $80:F978, which all five exits share
  HURT_BLOCK_COUNT,
} PlayerHurtBlock;

// `$80:F7F7`'s own two exits. Everything past the dispatch is the jump-table
// target's, and which one that was is `target`.
typedef enum {
  PLAYER_BLK_IGNORE,    // $80:F7FA BCS taken: CLC : RTL, and 90 cycles is
                        // exactly the minimum `verify` measures here
  PLAYER_BLK_DISPATCH,  // ...not taken: file the other record and JSR through
                        // the table
  PLAYER_BLOCK_COUNT,
} PlayerCollideBlock;

typedef struct {
  uint16_t blocks[PLAYER_BLOCK_COUNT];
  // The address `$80:F803  JSR ($F808,X)` went to, or 0 when the id was out of
  // range and the table was never read. The cost model switches on it the same
  // way `ThreadCallWork::entry` does one level up, and for the same reason:
  // seventeen targets share this entry and they are seventeen routines.
  uint16_t target;
  // Valid only when `target` is `PLAYER_COLLIDE_HURT`.
  uint16_t hurt[HURT_BLOCK_COUNT];
} PlayerCollideWork;

bool player_collide_counted(Wram* w, const Rom* rom, uint16_t dp, uint16_t arg,
                            ActorHandlerRegs* r, uint32_t* unported,
                            PlayerCollideWork* work);

// --- $81:8888 ---------------------------------------------------------------

#define ENEMY_COLLIDE_ENTRY 0x818888u

// Bit 15 of a collision id is not part of the id. `$81:8891  AND #$7FFF` masks
// it off before anything is looked up, and `$80:C7D9` reads it as the side that
// earns the points — so the top bit says *which player's* weapon this was.
#define ENEMY_COLLIDE_ID_MASK 0x7fff

// Two ids the damage table does not describe: each has its own routine
// (`$81:83C6` and `$81:847E`) and neither is ported.
#define ENEMY_HIT_SPECIAL_A 0x005e
#define ENEMY_HIT_SPECIAL_B 0x005d

// 43 entries of one word each, in ROM, indexed by `(masked id - $5C) x 2` and
// subtracted from `ACTOR_DP_HEALTH`. Some entries are `$FFFF`, so a "hit" can
// add health as easily as remove it.
#define ENEMY_DAMAGE_TABLE 0x818561u

// --- $81:8727 ---------------------------------------------------------------

// What a kill is worth, as a BCD constant — `$81:8727  LDX #$0100`.
#define ENEMY_DEATH_AWARD 0x0100
// ...and what it posts to `ACTOR_DP_DEATH_REQ` to say so.
#define ENEMY_DEATH_REQUEST 0xf5f5

// An enemy thread's handler, and the mirror image of the player's: ids *below*
// `COLLIDE_ID_PLAYER` are somebody else's business and it returns having written
// nothing at all. That branch is 1,225 of the 1,226 dispatches
// `movies/level1-rescue.zmv` produces.
//
// The other one is a hit, and it is ported as far as the outcome the movie
// reaches. Park the id, mask it, look the damage up and subtract it from
// `ACTOR_DP_HEALTH`, and then:
//
//   * **the enemy died** (the difference went negative) — store it, clear
//     `ACTOR_DP_SCRATCH_7E`, and run `$81:8727`: award `ENEMY_DEATH_AWARD` to
//     whichever player bit 15 named, and post `ENEMY_DEATH_REQUEST` for the
//     enemy's own loop to find. It returns **carry set**, which is what makes
//     `thread_call_handler` park the thread — the only thing in the game that
//     does.
//   * **the damage was zero** — `$81:88AD  CMP $1E : BEQ` leaves through a bare
//     `CLC : RTL` having written only the id.
//   * **the enemy survived** — store the new health and leave through
//     `$81:8506`, the reaction below.
//
// False only for the two special ids above. `unported` is set to the address it
// gave up at, so the census names the routine rather than the id; NULL if the
// caller does not want one.
bool enemy_collide(Wram* w, const Rom* rom, uint16_t dp, uint16_t arg,
                   ActorHandlerRegs* r, uint32_t* unported);

// Its two flat exits, and a mark for the four that are not flat.
//
// Unlike the player's, this handler's interesting paths do not end in it: `$5E`
// and `$5D` are `JML`s, a death runs `enemy_die` into `score_add`, and a
// survival runs `enemy_survived_react` into the thread splice and sometimes
// `rng_next`. Each is a tree of its own, so the model prices what returns from
// inside `$81:8888` and declines the rest — the same bargain `player_collide`
// makes with its jump table.
typedef enum {
  ENEMY_BLK_IGNORE,     // $81:888B BCS not taken: not a player id, CLC : RTL
  ENEMY_BLK_NO_DAMAGE,  // $81:88AF BEQ taken: the subtraction took nothing off
  ENEMY_BLK_DEEP,       // freeze, bubble, death or survival: not priced here
  ENEMY_BLOCK_COUNT,
} EnemyCollideBlock;

typedef struct {
  uint16_t blocks[ENEMY_BLOCK_COUNT];
} EnemyCollideWork;

bool enemy_collide_counted(Wram* w, const Rom* rom, uint16_t dp, uint16_t arg,
                           ActorHandlerRegs* r, uint32_t* unported,
                           EnemyCollideWork* work);

// --- $81:8506 ---------------------------------------------------------------
//
// **What a surviving enemy does, and the only routine in the game that writes
// into a suspended thread's stack.** The scheduler parks a thread by saving its
// stack pointer at `W_THREAD_SP`; this moves that pointer down three bytes,
// slides the top three words down to meet it, and writes a 24-bit address into
// the gap — so that when the scheduler next resumes the thread, the `RTL` it
// resumes through returns into `$81:8542` instead, which runs, and *then*
// returns into whatever the thread was actually doing. **The game injects a
// call into code that is not running.**
//
// `docs/threads.md` has been carrying this as the open question it poses to the
// port's coroutines, and the answer turns out to be smaller than the question:
// the frame this splices is not the port's to write, because the thread it
// splices into is the *ROM's* — an enemy body nobody has ported. What the port
// has to get right is the twelve bytes of WRAM, and `verify` compares them.
//
// What gets injected is two ticks of `ACTOR_ATTR_SET` on the enemy's own
// display record (`$81:8542`), which is the flash you see when you shoot
// something that does not die. That is also the answer to a coverage site this
// project carried untaken for six rounds: `draw_attr_set` is the hurt flash.

#define ENEMY_SURVIVED_ENTRY 0x818506u
// The two ids that leave through routines of their own — `$81:8894  CMP #$005E :
// BEQ` and `$81:8899  CMP #$005D : BEQ`, both `JML`s. Still unreached.
//
// **These two were the wrong way round until `$81:B41C` was ported.** The `_A`
// and `_B` suffixes pair with `ENEMY_HIT_SPECIAL_A`/`_B` — `$5E` and `$5D` — but
// the addresses were written down in the order the `JML`s appear in the ROM,
// which is the opposite order: `$8897  BEQ` lands on `$88C0  JML $8183C6` and
// `$889C  BEQ` on `$88C4  JML $81847E`. Nothing could see it. Both ids decline
// either way, so the only thing the constant picks is the address the *census*
// prints, and neither id has ever been reached by any input — `enemy_hit_special`
// is one of the sites the coverage report has carried untaken from the start. It
// was caught by porting the third copy of this routine and reading the same two
// branches again from the bytes.
#define ENEMY_SPECIAL_A_ENTRY 0x8183c6u  /* id $5E */
#define ENEMY_SPECIAL_B_ENTRY 0x81847eu  /* id $5D */

// The far address `$81:8539  LDA #$8542 : DEC A` writes into the gap, and the
// bank `$81:8532  LDA #$0081 : XBA` writes above it. One less than the routine
// it wants, because what resumes the thread is an `RTL`.
#define ENEMY_REACT_RETURN 0x8541
// What the thread wakes into there: it flashes for two ticks. The record's
// bit is set, the thread sleeps, and the bit is cleared again. The `RTL`
// after that is the one the splice made room for: it goes back to wherever
// the thread was asleep.
#define ENEMY_FLASH_PC 0x818542u
#define ENEMY_FLASH_YIELD_PC 0x818550u    // `JSL thread_yield`, two ticks in A
#define ENEMY_FLASH_END_PC 0x818554u
#define ENEMY_FLASH_END_RTL_PC 0x818560u
#define ENEMY_FLASH_TICKS 2
// Each hands back the record, and the second the record's flags as it left
// them.
uint16_t enemy_flash_begin(Wram* w, uint16_t dp);
uint16_t enemy_flash_end(Wram* w, uint16_t dp, uint16_t* flags);
#define ENEMY_REACT_BANK 0x81
// How far the parked stack pointer moves, which is also how many bytes the
// three words below it slide.
#define ENEMY_REACT_FRAME 3

// --- $81:83C6  enemy_bubble_react -------------------------------------------
//
// **The routine every copy of `$81:8888` hands id `$5E` to**, and the last of
// the three splices in this family to be written.
//
// It is `enemy_survived_react` with the resume address changed, and nothing
// else: the same `LDY $08 : LDA $0000,Y : AND #$0010` guard (branch polarity
// flipped, same two exits), the same three-word frame slid into a suspended
// thread's stack, the same `SEC` that parks the thread. What the woken thread
// runs is `$81:8404`, which opens by pushing the creature's metasprite and
// collision id and then writes `$0036` over `ACTOR_COLLIDE_ID` — so the id `$5E`
// does not damage anything, it *replaces what the creature is* until something
// pops those words back.
//
// **The weapon is named off the ROM's text, and the chain is one link longer
// than `enemy_freeze`'s.** `$81:9BA2` — the nine instructions
// `enemy_9b6b_collide` runs on this same id before `JML`ing here — increments
// `D9B6B_FATAL_COUNT`, `$7E:1FDC`, and the end-of-level tally reads that word to
// decide whether to draw a string that says `MARTIAN/BUBBLED`. So `$5E` is the
// **bubble** weapon: `ENEMY_DAMAGE_TABLE`'s entry for it is zero, like `$5D`'s,
// because neither weapon damages anything. One freezes and one bubbles.
#define ENEMY_BUBBLE_ENTRY 0x8183c6u

// `$81:83FB  LDA #$8404 : DEC A` — the same bank and the same three-word frame as
// the other two splices, one less than the routine it wants because an `RTL`
// adds one.
#define BUBBLE_REACT_RETURN 0x8403

// The reaction. Always true: it has nothing left to decline.
bool enemy_bubble_react(Wram* w, uint16_t dp, ActorHandlerRegs* r);

// --- $81:847E  enemy_freeze -------------------------------------------------
//
// **The routine every copy of `$81:8888` hands id `$5D` to**, and the one that
// explains a store this file has apologised for six times.
//
// Each of the eight copies ends a death with `STZ $7E`, and each carries a
// comment saying the store is transcribed rather than diffed because the word is
// already zero on every call. It was already zero because **no input in the
// project had ever fired anything but the squirt gun.** `movies/level17-weapon.zmv`
// is the first one that does, and `$81:847E  INC $7E` is the only writer in the
// game that ever makes that word non-zero. The `STZ` is its reset.
//
// What it does with the count identifies the weapon, off the ROM's own text
// rather than off the screen: on the fifth hit it increments `$7E:1FE0`, and
// `$82:CA8C  LDA $1FE0 : CMP #$0028 : BCC` is the end-of-level tally deciding
// whether to draw the string at `$82:CABD`, which reads
// `MONSTER/FROZEN/....////BONUS?`. So `$5D` is the **ice** weapon, five hits
// freeze one thing, and forty freezes pay a bonus. Its `ENEMY_DAMAGE_TABLE`
// entry being **zero** is not an oversight either: it never damages anything.
#define ENEMY_FREEZE_ENTRY 0x81847eu

// `INC $7E : LDA $7E : CMP #$0005 : BCS`, on the *target's* page. Never reset by
// this routine, so once a creature is at five every later hit acts.
#define ACTOR_DP_FREEZE_HITS 0x7e  /* the same word as ACTOR_DP_SCRATCH_7E */
#define FREEZE_HITS_NEEDED 0x0005

// `INC $1FE0,X`, indexed by the score *slot* — not the side. `$81:849B  JSL
// $80:9D6A` converts one to the other, and it is `score_slot` with one
// comparison instead of two: `LDX #$0000 : CMP $1E84 : BEQ : INX : INX : RTL`.
// It cannot answer "nobody", so there is no discard path here.
#define W_MONSTERS_FROZEN 0x1fe0
#define FREEZE_BONUS_AT 0x0028  /* documentation: $82:CA8F reads it, not this */

// What goes into the gap: `$81:84D6` less the one an `RTL` adds. Same bank and
// same three-word frame as the other two splices.
#define FREEZE_REACT_RETURN 0x84d5

// The routine. Always true — it calls nothing the port does not have.
//
// `dp` is the *target's* page, because it is reached by `JML` from that
// creature's own handler, and `r->y` still holds the raw collision id the
// dispatcher put there: bit 15 of it is the player whose bonus this counts.
bool enemy_freeze(Wram* w, uint16_t dp, ActorHandlerRegs* r);

// `$81:8506`, on the enemy's own page and after its health has been stored.
// True always: there is no branch of it that is not here.
bool enemy_survived_react(Wram* w, uint16_t dp, ActorHandlerRegs* r);

// --- $81:FE0E ---------------------------------------------------------------

#define SHOT_COLLIDE_ENTRY 0x81fe0eu

// The four ids that end a shot. They are bare `CMP` operands in a chain, not a
// table, so there is nothing to look up and nothing to get wrong except the
// order — which matters only for the flags, and the flags are what the diff
// checks.
#define SHOT_STOP_ID_A 0x0000
#define SHOT_STOP_ID_B 0x0003
#define SHOT_STOP_ID_C 0x0004
#define SHOT_STOP_ID_D 0x0001

// What `$81:FE21` writes to `ACTOR_DP_LIFE`: one more pass, then the shot's own
// loop falls out of `DEC $42 : BNE` and runs its splash.
#define SHOT_LIFE_ENDING 0x0001

// A weapon shot's handler — twenty-one bytes, and 616 of the 618 dispatches
// `movies/level1-2p.zmv` could not serve before it existed.
//
// The thread at `$81:FCB2` registers it (`$81:FCCC  LDA #$FE0E : LDY #$0081 :
// JSL thread_set_handler`), lives twenty frames, and spends them flying. This is
// what happens when it touches something: if the id is one of four, switch the
// shot's collision id off so it cannot hit anything else, and set its life to 1
// so the next pass ends it. Every other id it flies straight through.
//
// It takes no `Rom*` — there is no table in it — and it never declines. The
// whole routine is reachable and all of it is here.
bool shot_collide(Wram* w, uint16_t dp, uint16_t arg, ActorHandlerRegs* r);

// Which of the five exits a call took, for `src/cosim/routines.c` to price.
//
// The four stop ids are separate blocks rather than one because the `CMP` chain
// is unrolled: each is reached through a different number of comparisons, so
// what an id costs here is *where it sits in the chain* and nothing else. Id 0
// is first because it is the `TAY`'s own Z flag, which is the same reason it is
// the one that leaves carry alone.
typedef enum {
  SHOT_BLK_STOP_A,  // $81:FE0F BEQ taken: id 0, reached with no CMP at all
  SHOT_BLK_STOP_B,  // $81:FE14 BEQ taken: id 3, one CMP in
  SHOT_BLK_STOP_C,  // $81:FE19 BEQ taken: id 4, two
  SHOT_BLK_STOP_D,  // $81:FE1E BEQ taken: id 1, three
  SHOT_BLK_TAIL,    // $81:FE21..$FE2E: the shared tail all four fall into
  SHOT_BLK_FLY,     // $81:FE20 RTL: every other id, straight through the chain
  SHOT_BLOCK_COUNT,
} ShotCollideBlock;

// Exactly one of the four `STOP` blocks or `FLY` is set on any call, and
// `TAIL` is set exactly when one of the four is.
typedef struct {
  uint16_t blocks[SHOT_BLOCK_COUNT];
} ShotCollideWork;

bool shot_collide_counted(Wram* w, uint16_t dp, uint16_t arg,
                          ActorHandlerRegs* r, ShotCollideWork* work);

// --- $83:A364 ---------------------------------------------------------------

#define VICTIM_COLLIDE_ENTRY 0x83a364u

// The eight ids a victim reacts to, as a `CMP` chain in this order. Nothing is
// looked up: each one branches straight to its own two or three instructions,
// so what an id *is* here is entirely the code behind it.
//
// The two below produce the same event and differ only in the word they latch
// into `VICTIM_DP_CLAIMANT` — and `score_add` reads bit 15 of that and nothing
// else, so this pair is the two players. `$83:A392  BRA` skips the `LDA #$8000`
// that the other one falls into, which means the first latches the id *itself*,
// still in A from the dispatcher. Three bytes saved, and the reason the field
// ends up holding `$0005` rather than a flag — confirmed by a perturbation,
// which failed at `$7E:0418` reading ROM `$05` against the port's `$00`.
//
// Only the first has ever run. `victim_claim_b` is untaken by every movie,
// including the two-player one, which rescues nobody: the input that takes it
// is the second player walking into a victim.
#define VICTIM_ID_CLAIM_A 0x0005
#define VICTIM_ID_CLAIM_B 0x0006
// One id of its own...
#define VICTIM_ID_EVENT_2 0x000b
// ...three that share an outcome, and it is the same code the victim's own
// thread writes when it gives up waiting (`$83:A23D  LDA #$0003 : STA $1E`
// after a 300-frame sleep). Whatever these three are, the game files them with
// "nobody came".
#define VICTIM_ID_EVENT_3_A 0x0003
#define VICTIM_ID_EVENT_3_B 0x0004
#define VICTIM_ID_EVENT_3_C 0x0009
// ...and two more, each with an event to itself and no other evidence about
// what it is. `$FF` is the only one whose code is not a small integer.
#define VICTIM_ID_EVENT_4 0x0034
#define VICTIM_ID_EVENT_FF 0x00ff

// The codes it latches. They are what `$83:A239  LDA $1E : BNE` wakes on, so
// each one is a different ending for the thread waiting underneath.
#define VICTIM_EVENT_CLAIMED 0x0001
#define VICTIM_EVENT_2 0x0002
#define VICTIM_EVENT_3 0x0003
#define VICTIM_EVENT_4 0x0004
#define VICTIM_EVENT_FF 0xffff

// A victim's handler — the last address the decline census named on any movie,
// and with it the census is empty of everything but the sound effect.
//
// It is the smallest kind of handler there is: a latch. Read
// `VICTIM_DP_EVENT`; if anything is already there, this victim's fate is
// settled and the whole routine is two instructions. Otherwise walk a chain of
// eight comparisons, and the one that matches writes a code into that field for
// the victim's own thread to find. Five of the eight also clear
// `ACTOR_COLLIDE_ID` in the display record, which is a victim switching its own
// collision off so nothing can claim it twice — exactly what `shot_collide`
// does to a spent shot, through a different field of a different page.
//
// The three movies find this handler on a page based at `$7E:0400` and the
// records it writes in the display list at `$7E:1A38`/`$7E:1A60`, so it is the
// second ported handler to reach outside its own direct page and the first to
// do it on a path that is not a shot ending.
//
// It takes no `Rom*` and it never declines: there is no table in it and every
// one of its exits is here.
bool victim_collide(Wram* w, uint16_t dp, uint16_t arg, ActorHandlerRegs* r);

// --- $80:CAEE ---------------------------------------------------------------

#define OBJECT_COLLIDE_ENTRY 0x80caeeu

// How many bytes of the queue below are in use, and the queue itself: record
// *addresses*, one word each, on the object manager's own page.
//
// `$80:C9E0  STZ $12` is the initialisation, and it is the last instruction of
// `object_list_parse` — the same routine `src/assets/actor.c` ports the static
// half of. So the parser that reads the placement list out of ROM and the
// handler that reacts to a pickup are two ends of one routine, which is how
// `$80:CAEE` was identified at all: the ROM installs it three instructions
// later (`$80:C9D6  LDA #$CAEE : LDY #$0080 : JSL thread_set_handler`).
//
// `INC $12` twice per entry, and `STA $14,X` indexes by it directly, so this is
// a byte cursor rather than a count. Nothing here bounds it — the object
// thread's own pass is what empties the queue.
#define OBJECT_DP_QUEUE_LEN 0x12
#define OBJECT_DP_QUEUE 0x14

// The three ids that take an object. Two of them are the ids `victim_collide`
// calls its claim pair, which is the second piece of evidence that **5 and 6
// are the two players**: the same two ids rescue a victim and pick up an item,
// and no third id does either.
//
// The third, `$0004`, is **the monster side, and it takes objects out from
// under you.** Its entry in the player's own jump table is `$80:F950`, the hit
// path, so a record carrying it is a thing that hurts you; and it is one of the
// three a victim files under "nobody came" (`VICTIM_ID_EVENT_3_B`). In level 45
// an id-$04 actor was watched standing on the same object as the player on the
// same frame and winning it, on three separate routes, which is not a tie-break
// the game decides on merit: `actor_overlap_pass` walks pairs from the end of the
// display list, so **whichever of the two the depth sort put later gets asked
// first**, and this handler clears the object's `ACTOR_COLLIDE_ID` before the
// loser's turn comes round. The loser's handler is then called with an id of
// zero, which for the player means `$80:F87A`, a bare `RTS`. That is what
// `object_spent` and `player_no_effect` count, three apiece, in the same run.
//
// They are tested against A — the *other* actor's collision id, as the
// dispatcher handed it in — while the `BEQ` above them tests X, which is the
// object's *own*. Two different records, three instructions apart.
//
// The object's own id is `$0C`, which the diff handed over rather than the
// listing: a perturbation that dropped the `STZ` below failed at `$7E:1A10`
// with ROM `$00` against the port's `$0C`, and `$7E:1A10` is `ACTOR_COLLIDE_ID`
// in the display record the pickup was reacting to.
#define OBJECT_ID_TAKE_A 0x0005
#define OBJECT_ID_TAKE_B 0x0006
#define OBJECT_ID_TAKE_C 0x0004

// The object manager's handler — the fifth, and the first that is not an actor
// reacting on its own behalf.
//
// Every object in the level shares one thread (`$80:C9D6` registers this on
// slot `$80` once, for all of them), so `dp` is the manager's page and not the
// object's. What the handler is handed is the display record of whichever
// object was touched, in `W_HANDLER_SELF`, and all it does is switch that
// record's collision off and write its address into a queue for the manager's
// own pass to drain. **The reaction is deferred, not computed here** — which is
// a different shape from the four handlers before it, and the reason this one
// is eleven instructions long.
//
// It takes no `Rom*` and it never declines: there is no table in it and all
// three of its exits are here.
bool object_collide(Wram* w, uint16_t dp, uint16_t arg, ActorHandlerRegs* r);

// ---------------------------------------------------------------------------
// $81:C4A6  monster_collide — a second, larger enemy's handler
// ---------------------------------------------------------------------------

// The census named this one, and it named it loudly: **2,039 declines across the
// four level-45 movies**, more than everything else on the list put together.
// `$81:C3B6` installs it (`LDA #$C4A6 : LDY #$0081 : JSL thread_set_handler`),
// and `$81:C3B6` is the body of level 46's type-`$14` actor — the giant spider,
// ten of that level's twenty placements.
//
// "The giant spider" here and below was a guess. It is the giant ant, the big
// red one level 45 is full of. The spiders are `$83:B299`, `spider_frame`.
//
// It is a **second copy of the enemy subsystem**, not a variant of the first.
// Same damage table at `$81:8561`, same "subtract, went negative means dead"
// shape, and each of the routines it leans on has a ported twin: `$81:BBEB` is
// `$81:8727` again with a bigger award, and `$81:BAB3` and `$81:BB05` are
// `$81:8506` again — the parked-stack splice. What it does *not* share is the
// page: health is at `$22` here and `$1E` there, which is the same lesson `$1E`
// itself taught, one page further out.
//
// **And it is the routine behind this round's findings.** The object branch is
// `LDA #$0003 : STA $000E,Y` into its own display record, which is exactly the
// `$04` -> `$03` transition `zamn_headless --records` caught at frames 3466 and
// 3790 when a monster took a bonus object out from under the player.
#define MONSTER_COLLIDE_ENTRY 0x81c4a6u

// The three ids the dispatch cuts on, besides `COLLIDE_ID_PLAYER`. Ids in
// `[MONSTER_OBJECT_ID_FIRST, MONSTER_OBJECT_ID_END)` are the object range —
// `$80:CA30`'s thirty entries run `$0C`..`$30` — and everything outside it and
// below `$5C` is ignored outright, in two separate `CLC : RTL`s.
#define MONSTER_OBJECT_ID_FIRST 0x000c
#define MONSTER_OBJECT_ID_END 0x0033

// The two ids with routines of their own, both `JML`s, neither ported: `$5D`
// goes to `$81:BB05` and `$5E` shares the death tail. Note the asymmetry with
// `enemy_collide`, where *both* are separate routines.
#define MONSTER_HIT_SPECIAL 0x005d
#define MONSTER_HIT_FATAL 0x005e

// Where a survivor goes, and where `$5D` goes. Both are stack splices in the
// `$81:8506` family. The first is ported below; the second declines by name.
#define MONSTER_SURVIVE_ENTRY 0x81bab3u
#define MONSTER_SPECIAL_ENTRY 0x81bb05u

// --- $81:BAB3 ---------------------------------------------------------------

// `$81:8506` again, three bytes at a time, and the differences are worth having
// in one place because they are all in the *edges* rather than the mechanism:
//
//   * **The guard reads a different field with a different test.** `$81:8506`
//     is `LDA $0000,Y : AND #$0010` — bit 4 of the record's flags,
//     `ACTOR_ATTR_SET`. This is `LDA $0010,Y : BNE` — the whole of `ACTOR_ATTR`,
//     the word that bit would have selected. Same question ("am I already
//     reacting?"), asked of the answer rather than of the permission.
//   * **What it returns on that path is therefore data, not a constant.** The
//     twin can hand back `ACTOR_ATTR_SET` because that is what the `AND` left;
//     this hands back whatever was in the field.
//   * The address spliced in is `$81:BAEC`, and what that does is write `$0C00`
//     into `ACTOR_ATTR`, sleep two ticks, and clear it — where the twin sets and
//     clears a bit. Same two ticks.
//
// Everything else — the three-byte gap, the three overlapping word moves lowest
// first, the two stores that lay down three bytes, `SEC` to park the thread — is
// the same routine, and `ENEMY_REACT_FRAME` is shared rather than re-spelled.
#define MONSTER_REACT_RETURN 0xbaeb  // `$81:BAEC` less the one an `RTL` adds
#define MONSTER_REACT_BANK 0x81

// --- this actor's own page --------------------------------------------------

// Health. `$22` here, where `enemy_collide`'s is `$1E` — see `ACTOR_DP_HEALTH`.
#define MONSTER_DP_HEALTH 0x22
// The raw hit id, parked sign bit and all, because `$81:BBEB` reads bit 15 of it
// to decide whose points these are. Same trick, same place in the routine.
#define MONSTER_DP_HIT_ID 0x20
// What became of this monster, latched — and a latch in the same sense
// `victim_collide`'s `$1E` is one: the object branch refuses outright if
// anything is already here, so the *first* thing to reach it decides.
#define MONSTER_DP_LATCH 0x26
// The state machine's next routine. `$81:C3DA  LDA $12 : DEC A : PHA : RTS` is
// the body dispatching through it, and `$81:C04A` — three instructions, inlined
// below — is how the object branch queues `$81:C050` up.
#define MONSTER_DP_NEXT 0x12
#define MONSTER_NEXT_TAKE_OBJECT 0xc050
// A countdown `$81:BBEB` steps on the way out of a death.
#define MONSTER_DP_COUNT 0x2a
// Cleared on the death path, the same way `ACTOR_DP_SCRATCH_7E` is.
#define MONSTER_DP_SCRATCH_7E 0x7e
// Its own display record — the address, at the same `$08` a victim keeps one at
// and a different offset from the `$0A` a shot uses. Named separately from
// `VICTIM_DP_RECORD` because sharing a number is not sharing a meaning: these
// pages are laid out by their own bodies and agree by accident.
#define MONSTER_DP_RECORD 0x08
// What it writes into that record's `ACTOR_COLLIDE_ID` on taking an object, and
// the single most useful constant in this file for reading a `--records` dump:
// a monster showing `$03` where it showed `$04` a frame ago has just eaten
// something.
#define MONSTER_TAKEN_ID 0x0003

// --- the two globals the object branch reads -------------------------------
//
// `AD 42 00` and `AD 46 00` are **absolute**, not direct page, so these are
// `$7E:0042` and `$7E:0046` rather than offsets into the monster's page. Worth
// the note: every other field this routine touches is direct page, and reading
// them as such would put the latch's value somewhere plausible and wrong.
#define W_MONSTER_LATCH_SRC 0x0042
#define W_MONSTER_LATCH_ALT 0x0046
#define MONSTER_LATCH_SRC_ALT 0x0004

// --- $81:BBEB ---------------------------------------------------------------

// `$81:8727` again, and worth three times as much: `LDX #$0300`. The rest is the
// same routine — `score_add` with bit 15 of the parked id as the side, then a
// per-side counter, then a countdown.
#define MONSTER_DEATH_AWARD 0x0300
// `INC $1FD4,X`, absolute again, indexed by the side already doubled — which is
// what `AND #$8000 : ASL A : ROL A : ROL A` computes from the parked id.
#define W_MONSTER_KILL_COUNT 0x1fd4

// The handler. `arg` is the other actor's collision id, `dp` this monster's own
// page.
//
// Four ways out of the dispatch and two of them write nothing: an id below
// `MONSTER_OBJECT_ID_FIRST`, and one at or above `MONSTER_OBJECT_ID_END` but
// below `COLLIDE_ID_PLAYER`. The object range takes the object. At or above
// `COLLIDE_ID_PLAYER` is a weapon shot, and that path is ported as far as the
// two outcomes that stay inside it — dead, and zero damage — while a survivor
// and id `$5D` decline by name, the way `shot_collide` declined `$81:8506`
// before a movie reached it.
//
// False only on those two. `unported` takes the address it gave up at.
bool monster_collide(Wram* w, const Rom* rom, uint16_t dp, uint16_t arg,
                     ActorHandlerRegs* r, uint32_t* unported);

// The five exits that end inside the routine, and a mark for the four that do
// not — the same bargain `EnemyCollideBlock` makes, on the same reasoning: a
// death runs `$81:BBEB` into `score_add`, a survivor leaves through a `JML`
// into the thread splice, and neither is a tree this file describes.
//
// **One table prices both copies.** `$81:C440` differs from `$81:C4A6` in three
// bytes: two `JML` targets, which are only on the deep paths that decline
// anyway, and the order of the `CMP #$005D`/`CMP #$005E` pair, which costs the
// same either way because both comparisons run and neither branch is taken on
// any path that reaches them. So there is no `MonsterCopy` in the cost model
// and there does not need to be — see `monster_collide_body`, which the two
// share for the same reason.
typedef enum {
  MON_BLK_IGNORE_LOW,   // $81:C4AE BCC taken: below the object range, CLC : RTL
  MON_BLK_IGNORE_HIGH,  // $81:C4B3 BCC not taken: above it, a different CLC : RTL
  MON_BLK_LATCHED,      // $81:C4EE BNE taken: something already happened to this one
  MON_BLK_TAKE,         // the theft, latching $42
  MON_BLK_TAKE_ALT,     // ...and the same latching $46, which costs one more load
  MON_BLK_NO_DAMAGE,    // $81:C4D7 BEQ taken: the subtraction took nothing off
  MON_BLK_DEEP,         // $5D, $5E, a death or a survival: not priced here
  MONSTER_BLOCK_COUNT,
} MonsterCollideBlock;

typedef struct {
  uint16_t blocks[MONSTER_BLOCK_COUNT];
} MonsterCollideWork;

bool monster_collide_counted(Wram* w, const Rom* rom, uint16_t dp, uint16_t arg,
                             ActorHandlerRegs* r, uint32_t* unported,
                             MonsterCollideWork* work);

// `$81:BAB3`, split out for the same reason `enemy_survived_react` is: it has
// two coverage sites of its own and one of them is an entry guard no diff can
// check. Always true — there is nothing in it to decline.
bool monster_survived_react(Wram* w, uint16_t dp, ActorHandlerRegs* r);

// ---------------------------------------------------------------------------
// $81:B41C  enemy_b41c_collide — the third copy of the enemy subsystem
// ---------------------------------------------------------------------------
//
// **The name is the address, and that is a claim about what is known rather
// than a failure of imagination.** `enemy_collide` and `monster_collide` are
// named for what they are; this one is not identified. Its body is entered
// through `$81:AEA6`, no actor in level 29's placement list names it — the
// creature is spawned rather than placed — and nothing in the disassembly ties
// it to a sprite. `VICTIM_DP_FLAG_26` is named for where it is rather than for
// what it means, for exactly this reason, and the same discipline applies to a
// routine. Rename it the day something proves what it is.
//
// What *is* established is its shape: it is `$81:8888` instruction for
// instruction, on a third page layout, with five differences. Three are
// relocations — the parked id, the health word, the display record. The other
// two are real, and both are below.
#define ENEMY_B41C_COLLIDE_ENTRY 0x81b41cu

// --- this actor's own page --------------------------------------------------

// Health. `$0C` here, against `enemy_collide`'s `$1E` and `monster_collide`'s
// `$22` — three copies of one routine, three different pages, and the same
// damage table at `ENEMY_DAMAGE_TABLE` indexed the same way.
#define B41C_DP_HEALTH 0x0c
// Where the raw id is parked on the way in, sign bit and all. `$5A` here.
// Nothing in this copy reads it back — the death path awards no score, so there
// is no `enemy_die` to ask whose shot it was — but the store still happens and
// the diff still checks it.
#define B41C_DP_HIT_ID 0x5a
// The display record, `LDY $08`, the offset `enemy_survived_react` already
// expects — which is why this copy can share it rather than re-spell it.
#define B41C_DP_RECORD 0x08
// **`INC $4C` — the first difference, and it is an outbound message.** The
// handler raises it on every hit that reaches the damage path, and the actor's
// own body consumes it on its next pass: `$81:AEC5  LDA $4C : BEQ : STZ $4C :
// JMP $AF4E`. It is `ACTOR_DP_DEATH_REQ`'s shape one rung down — that one says
// "take yourself apart", this one says "you were hit" — and `$81:B437` is its
// only writer in the whole bank.
#define B41C_DP_HIT_FLAG 0x4c
// **`DEC $0A` on the death path — the second difference, and the honest one.**
// Where `enemy_collide` calls `$81:8727` to pay out `ENEMY_DEATH_AWARD`, this
// copy awards nothing and decrements this word instead. The actor's body
// decrements it too, at `$81:AEE9`, when `$80:B26B` hands back no target. Two
// decrements, no initialiser and no reader anywhere in this actor's code, so
// what it counts is **not established** and the name says only where it lives.
#define B41C_DP_COUNTER_0A 0x0a
// Where `$81:B168` puts the routine below — the "what I do next" pointer on
// this page, the way `MONSTER_DP_NEXT` is `$12` on the spider's.
#define B41C_DP_NEXT 0x0e

// --- the third accepted id --------------------------------------------------

// `CMP #$0061 : BEQ`, a third comparison neither twin has. It is a weapon id
// like any other — `($61 - $5C) * 2` is a perfectly good `ENEMY_DAMAGE_TABLE`
// index, and the airborne path below uses it — so this is a specific weapon
// getting a specific answer out of a specific creature.
#define B41C_HIT_SPECIAL 0x0061

// ...and what decides the answer: `LDY $08 : LDX $0004,Y : BNE`. Record `+4` is
// `ACTOR_Z`, the height off the ground — so **the special case is only for one
// standing on the ground**, and one in the air falls through and takes the
// damage every other id takes. The actor's body tests the same field the same
// way at `$81:B217`.
#define B41C_Z_AIRBORNE_TO_DAMAGE 1  /* documentation, not a value */

// `$81:B168  LDA #$B16E : STA $0E : RTS` — three instructions, inlined for the
// reason `monster_collide` inlines `JSR $C04A`: a routine that only ever queues
// one constant is not a routine worth a registry entry. `$81:B16E` is what
// happens next — it swaps the record's metasprite out of the table at
// `$81:B199`, drops the handler, sleeps `$20` ticks and re-installs this one.
#define B41C_NEXT_ON_SPECIAL 0xb16e

// The handler. `arg` is the other actor's collision id, `dp` this actor's page.
//
// False on the two ids that `JML` elsewhere — `$5E` and `$5D`, the same two
// `enemy_collide` declines, to the same two addresses — with `unported` taking
// the address it gave up at.
bool enemy_b41c_collide(Wram* w, const Rom* rom, uint16_t dp, uint16_t arg,
                        ActorHandlerRegs* r, uint32_t* unported);

// The stretches of it a call runs, for the harness to price. Unlike
// `enemy_collide`'s, a survivor's is here too: `$81:8506` is a straight line
// either side of its one branch. Only the two ids that leave by a `JML` to
// somewhere else are not.
typedef enum {
  B41C_BLK_IGNORE,     // $81:B41F BCS not taken: CLC : RTL
  B41C_BLK_ACT,        // $81:B423 to the third BEQ, none taken
  B41C_BLK_SPECIAL,    // ...the third taken, and LDY : LDX : BNE
  B41C_BLK_GROUNDED,   // ......not taken: JSR $B168 : SEC : RTL
  B41C_BLK_AIRBORNE,   // ......taken, back to the damage
  B41C_BLK_HIT,        // $81:B437 to the BMI
  B41C_BLK_DIED,       // ...taken: DEC : STA : STZ : SEC : RTL
  B41C_BLK_NO_DAMAGE,  // ...not: CMP : BEQ taken, CLC : RTL
  B41C_BLK_SURVIVED,   // ......not: STA : JML $81:8506, to its BNE
  B41C_BLK_ALREADY,    // .........taken: flashing already, CLC : RTL
  B41C_BLK_SPLICED,    // .........not: the parked stack opened, SEC : RTL
  B41C_BLK_DEEP,       // id $5E or $5D: not priced here
  B41C_BLOCK_COUNT,
} B41cCollideBlock;

typedef struct {
  uint16_t blocks[B41C_BLOCK_COUNT];
} B41cCollideWork;

bool enemy_b41c_collide_counted(Wram* w, const Rom* rom, uint16_t dp,
                                uint16_t arg, ActorHandlerRegs* r,
                                uint32_t* unported, B41cCollideWork* work);

// ---------------------------------------------------------------------------
// $81:C440  monster_c440_collide — the same creature, one stage earlier
// ---------------------------------------------------------------------------
//
// **This is `$81:C4A6` again, 102 bytes before it, and for once the copy is not
// a guess.** The two share a page down to the last offset — health `$22`,
// parked id `$20`, latch `$26`, record `$08`, counter `$2A`, next-routine `$12`
// — and the same `JSR $81:BBEB` pays the same `MONSTER_DEATH_AWARD`. What makes
// it the *same creature* rather than a relative is the bodies: `$81:C321`
// installs this handler and then `$81:C326  JMP $C3B5` falls into `$81:C3B6`,
// which installs `$81:C4A6`. One thread, two handlers, in that order. So this
// is the giant spider before whatever `$81:BFA8` decides, and `monster_collide`
// is it afterwards.
//
// **Three bytes differ and two of them matter.**
#define MONSTER_C440_COLLIDE_ENTRY 0x81c440u

// A survivor goes to `enemy_survived_react` rather than to
// `monster_survived_react` — `JML $81:8506` against `JML $81:BAB3`. Both are
// ported, and they are not interchangeable: one guards on bit 4 of the record's
// flags and splices an address that *sets* that bit, the other guards on the
// whole of `ACTOR_ATTR` and splices one that writes `$0C00` into it. So the
// earlier stage flashes the way an ordinary enemy does and the later one does
// not.
#define MONSTER_C440_SURVIVE_ENTRY 0x818506u

// Id `$5D` goes to `ENEMY_SPECIAL_B_ENTRY` — `$81:847E`, the address
// `enemy_collide` declines to — rather than to `$81:BB05`. Unported either way,
// so the only thing this constant chooses is the address the census prints; it
// is written down because that is the whole value of the census, and because
// getting exactly this wrong once already cost a round (see the `_A`/`_B`
// mix-up recorded at `ENEMY_SPECIAL_A_ENTRY`).
#define MONSTER_C440_SPECIAL_ENTRY ENEMY_SPECIAL_B_ENTRY

// The third difference is not observable and is recorded so that nobody has to
// re-derive it: the two `CMP`s that pick out `$5D` and `$5E` are in the
// opposite order here. Both are equality tests against distinct constants and
// the fall-through immediately runs `SEC : SBC #$005C`, so neither which one
// matches nor what flags the pair leaves can differ.
#define MONSTER_C440_CMP_ORDER_IMMATERIAL 1  /* documentation, not a value */

// The handler, and it is `monster_collide`'s implementation with the two
// addresses above substituted — shared rather than re-spelled, for the reason
// `ENEMY_REACT_FRAME` is shared. False only on `$5D`, exactly as its twin.
bool monster_c440_collide(Wram* w, const Rom* rom, uint16_t dp, uint16_t arg,
                          ActorHandlerRegs* r, uint32_t* unported);

// ...and it counts into the same `MonsterCollideWork`, priced by the same
// table. See `MonsterCollideBlock` for why one table is enough for both.
bool monster_c440_collide_counted(Wram* w, const Rom* rom, uint16_t dp,
                                  uint16_t arg, ActorHandlerRegs* r,
                                  uint32_t* unported, MonsterCollideWork* work);

// ---------------------------------------------------------------------------
// $81:D7F6  enemy_d7f6_collide — level 17's, and the fifth copy of $81:8888
// ---------------------------------------------------------------------------
//
// Sixty-four bytes, `$81:8888`'s shape, and **not** near enough to any of the
// four existing copies to share their code: the health word, the parked id and
// the whole of what `$5E` means are all different. Written out, therefore, the
// way `enemy_b41c_collide` was — and, like it, named for its address, because
// nothing ties the creature to a sprite.
//
// | | `enemy_collide` | `enemy_b41c` | this |
// | --- | --- | --- | --- |
// | health | `$1E` | `$0C` | `$0C` |
// | parked id | `$22` | `$5A` | `$20` |
// | id `$5E` | `JML $81:83C6` | `JML $81:83C6` | **the death tail** |
// | id `$5D` | `JML $81:847E` | `JML $81:847E` | `JML $81:847E` |
// | a survivor | `JML $81:8506` | `JML $81:8506` | `JML $81:8506` |
// | a death | award `$0100` | `DEC $0A` | `DEC $0A` |
#define ENEMY_D7F6_COLLIDE_ENTRY 0x81d7f6u

// Health, and it is **one**: `$81:D6D2  LDA #$0001 : STA $0C`. Every entry in
// `ENEMY_DAMAGE_TABLE` except `$5D`'s and `$5E`'s is at least 1, so almost
// anything that hits this thing kills it.
#define D7F6_DP_HEALTH 0x0c
// Where the raw id is parked, sign bit and all — and here it is read twice
// rather than not at all: the body's death sequence uses it as `score_add`'s
// side *and* as the guard on whether to pay anything.
#define D7F6_DP_HIT_ID 0x20

// **`$0A` is a death flag, and this page is what proves the family meaning.**
// `enemy_b41c_collide` and `enemy_cdde_collide` both decrement a word at `$0A`
// and `B41C_DP_COUNTER_0A` says in as many words that what it counts is not
// established. Here it is: `$81:D6D8  STZ $0A` seeds it, the handler's
// `DEC $0A` is the only other writer in the routine's whole bank, and
// `$81:D72E  LDA $0A : BEQ <loop>` is the body's main loop deciding whether to
// go on living. One writer, one reader, no ambiguity.
//
// It is evidence about the twins rather than proof: `$81:B41C`'s copy has a
// *second* writer (`$81:AEE9`, when `$80:B26B` hands back no target) and this
// one does not, so `B41C_DP_COUNTER_0A` keeps its careful name.
#define D7F6_DP_DEAD 0x0a

// What the body does with that flag, recorded here because it is where the
// handler's two stores end up being read: `LDX #$0050 : LDA $20 : BEQ` — an
// award of `$0050`, the smallest in the game, and **skipped entirely when the
// parked id is zero**, which is `monster_death_award`'s guard again on another
// page. Then `INC $1F74`, a death animation, and `actor_slot_free`.
#define D7F6_DEATH_AWARD 0x0050  /* documentation: the body pays it, not this */

// The handler. False only on id `$5D`, which `JML`s to `ENEMY_SPECIAL_B_ENTRY`
// — the same address `enemy_collide` and `enemy_b41c_collide` decline to, and
// still unreached by any input.
bool enemy_d7f6_collide(Wram* w, const Rom* rom, uint16_t dp, uint16_t arg,
                        ActorHandlerRegs* r, uint32_t* unported);

// **`$82:9A6D` is the same sixty-four bytes, one bank over.** Byte for byte:
// every branch is relative and every jump out is a long one to bank `$81`, so
// nothing in the copy means anything different from the original, and the
// port serves both with one function. Its body is `$82:98DA`, a different
// creature, and records 0 and 51 place it. Until it was routed here it was the
// handler `thread_call_handler` declined most often — 2,482 times over one
// pass of every record — and each decline sent that frame's whole sprite pass
// back to the ROM.
//
// The coverage sites are `enemy_d7f6_collide`'s, so they do not say which of
// the two copies took a branch. Nothing needs them to: the two cannot differ.
#define ENEMY_9A6D_COLLIDE_ENTRY 0x829a6du

// ---------------------------------------------------------------------------
// $81:CDDE  enemy_cdde_collide — and this one is *not* the same routine again
// ---------------------------------------------------------------------------
//
// Named for its address for `enemy_b41c_collide`'s reason and by the same
// evidence: two install sites (`$81:CBDE`, `$81:CCAD`), both `LDA #$CDDE : LDY
// #$0081 : JSL thread_set_handler`, and nothing in level 29's placement list
// names either — this creature is spawned rather than placed.
//
// **What is worth saying is how little it has in common with the other three.**
// It opens `CMP #$005C` like all of them and then stops rhyming: there is no
// `ENEMY_DAMAGE_TABLE` lookup, no health word, no `$5E`, and no `JML` into the
// `$81:8506` family. Damage is a plain countdown — one hit is one `DEC` — and
// the reaction to a killing blow is a *different mechanism* again, below. Three
// copies of one routine had made the shape look universal; this is the
// counter-example, and it is the reason to read the bytes rather than assume.
#define ENEMY_CDDE_COLLIDE_ENTRY 0x81cddeu

// --- the three ids it answers to --------------------------------------------

// `CMP #$005D : BEQ` — the only id that costs it anything. `$5E`, which both
// twins treat specially, is not tested here at all and falls out as unmatched.
#define CDDE_HIT_DAMAGE 0x005d
// `CMP #$0064 : BEQ` and `CMP #$006F : BEQ`, two ids that share one exit: they
// increment `CDDE_DP_COUNTER_0A` and do nothing else. Two distinct ids reaching
// the same three instructions is why the coverage site is one site.
#define CDDE_HIT_COUNTED_A 0x0064
#define CDDE_HIT_COUNTED_B 0x006f

// --- this actor's own page --------------------------------------------------

// Where the raw id is parked — `$22`, the same offset `enemy_collide` uses.
// **And it is cleared rather than left**: `$81:CDF7  STZ $22` is where both the
// below-`$5C` branch and every unmatched id above it land, so an id this actor
// does not care about actively erases the last one it did.
#define CDDE_DP_HIT_ID 0x22
// The countdown that stands in for health. `DEC $0C` per damaging hit, negative
// means dead; `$81:CC0A` resets it to `CDDE_REACT_COUNTDOWN` and `$81:CC2F` to 4.
#define CDDE_DP_COUNTDOWN 0x0c
// Incremented by the two counted ids, and by a killing hit that arrives while
// the actor is already reacting. Same standing as `B41C_DP_COUNTER_0A`: named
// for where it is, because nothing in reach reads it.
#define CDDE_DP_COUNTER_0A 0x0a
// **The already-reacting guard, and this one is readable.** `$81:CC0A` sets it
// to `CDDE_REACT_TICKS` and `$81:CC2F` counts it down to zero, so non-zero means
// "mid-reaction" — which is `react_already`'s question asked of a page rather
// than of a display record's flags word.
#define CDDE_DP_REACT_TIMER 0x24
// The "what I do next" pointer on this page, and where the reaction parks the
// value it displaces so `$81:CC2F` can put it back.
#define CDDE_DP_NEXT 0x16
#define CDDE_DP_NEXT_SAVED 0x26
// The display record, at `$08` again.
#define CDDE_DP_RECORD 0x08

// --- $81:CC0A, the reaction -------------------------------------------------

// **A third way of reacting to a hit, and the simplest of the three.**
// `enemy_survived_react` splices a `JSL` frame into a suspended thread's own
// stack; `monster_survived_react` does the same one page over. This one just
// swaps its own next-routine pointer: save `CDDE_DP_NEXT`, install `$81:CC2F`,
// arm the timer, reset the countdown, and set the flash on the display record.
// `$81:CC2F` is the undo — count down, restore, clear. No stack, no splice, and
// nothing suspended.
#define CDDE_REACT_NEXT 0xcc2f
#define CDDE_REACT_TICKS 0x001e
#define CDDE_REACT_COUNTDOWN 0x0014
// What it writes into the record's `ACTOR_ATTR` — the same `$0C00`
// `monster_survived_react` writes, alongside the same `ACTOR_ATTR_SET` bit.
#define CDDE_REACT_ATTR 0x0c00

// The handler. Always true: every one of its ids is answered here, and the one
// routine it calls is `$81:CC0A`, which is ported below it. It is the first
// collision handler in the project with nothing to decline.
bool enemy_cdde_collide(Wram* w, uint16_t dp, uint16_t arg,
                        ActorHandlerRegs* r);

// `$81:CC0A`, split out for the reason the other two reactions are: it has a
// coverage site of its own and it is a mechanism worth naming.
void enemy_cdde_react_begin(Wram* w, uint16_t dp, ActorHandlerRegs* r);

// ---------------------------------------------------------------------------
// $81:B592  enemy_b592_collide — twenty-four bytes, and the last of level 29's
// ---------------------------------------------------------------------------
//
// The smallest collision handler in the game: two comparisons, a decrement, an
// increment, three `RTL`s. It is also the only one that does not test
// `COLLIDE_ID_PLAYER` at all — **the two ids it answers to are `$07` and `$08`,
// far below a weapon shot**, so whatever hurts this thing hurts it by touching
// it rather than by being fired at it.
//
// Named for its address for the reason the other two are, and with the same
// caveat about the creature. Its page does resemble `enemy_b41c_collide`'s —
// countdown at `$0C`, tally at `$0A`, next-routine at `$0E`, and its code sits
// a hundred bytes past `$81:B41C`'s in the same block — which makes "the same
// creature in a different state" the obvious guess. It is left as a guess:
// `$0A` runs *up* here and *down* there, and one shared offset is not a shared
// meaning.
//
// **It is the handler of the axe a doll throws**, which `port/axe.h` has
// since ported: the thread at `$81:B4EA` installs it. `$0C` is the one hit an
// axe has to spare, and `$0A`, counted up by the second, ends the thread. So
// the guess was near: not the doll in another state, but what the doll
// throws.
#define ENEMY_B592_COLLIDE_ENTRY 0x81b592u

// `CMP #$0007 : BEQ` and `CMP #$0008 : BEQ`, both to the same instruction.
#define B592_HIT_A 0x0007
#define B592_HIT_B 0x0008

// Where the id is parked — `$1E` on this page, which is `ACTOR_DP_HEALTH`'s
// offset on an enemy's. A reminder that these numbers are per-page and mean
// nothing across one.
#define B592_DP_HIT_ID 0x1e
// One touch, one decrement; negative is the end of it.
#define B592_DP_COUNTDOWN 0x0c
// ...and what that end does, which is to add one to a word nothing in reach
// reads. Same standing as `B41C_DP_COUNTER_0A` and `CDDE_DP_COUNTER_0A`.
#define B592_DP_COUNTER_0A 0x0a

// The handler. Always true — like `enemy_cdde_collide`, it answers every id it
// is given and calls nothing.
bool enemy_b592_collide(Wram* w, uint16_t dp, uint16_t arg, ActorHandlerRegs* r);

// ---------------------------------------------------------------------------
// $81:9B6B  enemy_9b6b_collide — level 21's, and the sixth copy of $81:8888
// ---------------------------------------------------------------------------
//
// The family's sixth member and the third with an award-free death. Nearest to
// `enemy_d7f6_collide` and not near enough to share: three offsets move, and id
// `$5E` goes back to being a routine of its own rather than the death tail.
//
// | | `enemy_d7f6` | this |
// | --- | --- | --- |
// | health | `$0C` | `$32` |
// | parked id | `$20` | `$30` |
// | death | `DEC $0A` | `DEC $26` |
// | id `$5E` | the death tail | `$81:9BA2`, then `JML $81:83C6` |
#define ENEMY_9B6B_COLLIDE_ENTRY 0x819b6bu
#define D9B6B_DP_HEALTH 0x32
#define D9B6B_DP_HIT_ID 0x30
// Decremented on the death path, and — like `B41C_DP_COUNTER_0A` and unlike
// `D7F6_DP_DEAD` — **named for where it is**. Nothing in reach reads it, so
// whether it is this creature's death flag or a tally is not established.
#define D9B6B_DP_COUNTER_26 0x26

// $81:9BA2  the nine instructions in front of `enemy_bubble_react`, and for four
// rounds the only reason this branch declined. Every other copy of `$81:8888`
// hands `$5E` straight to `$81:83C6`; this one counts it first.
//
// `TYA : ASL A : AND #$0000 : ROL A : ROL A` turns bit 15 of Y into 0 or 2 — the
// doubled side index `score_add` searches with and `monster_death_award` builds
// the same way — `JSL $80:9D6A` turns that into a score *slot*, and `INC
// $1FDC,X` counts one bubbled martian for the player who fired.
//
// **The counter is what names the weapon.** `$82:C9AE  LDA $1FDC : CMP #$000A :
// BCC` is the end-of-level tally deciding whether to draw `MARTIAN/BUBBLED`,
// exactly as `$82:CA8C  LDA $1FE0 : CMP #$0028 : BCC` decides `MONSTER/FROZEN`.
// The two counters are two words apart because they are the same screen's two
// lines — and **the thresholds are not the same**: ten bubbled martians earn the
// bonus and it takes forty frozen monsters, which is the ROM saying the bubble
// gun is the scarcer of the two. `$1FDE` is player 2's, at the same ten.
#define D9B6B_FATAL_COUNT 0x1fdc

// The handler. False on `$5D` only, now that `$5E` has somewhere to go.
bool enemy_9b6b_collide(Wram* w, const Rom* rom, uint16_t dp, uint16_t arg,
                        ActorHandlerRegs* r, uint32_t* unported);

// The tally, then the splice. Always true — `enemy_bubble_react` has nothing
// left to decline either.
bool enemy_9b6b_bubble(Wram* w, uint16_t dp, ActorHandlerRegs* r);

// ---------------------------------------------------------------------------
// $81:F534  actor_f534_collide — forty-two bytes, five ids, one store
// ---------------------------------------------------------------------------
//
// The second-smallest handler in the game after `enemy_b592_collide`, and the
// second with nothing to decline: it calls nothing, reads two words and writes
// one. Five ids latch themselves into `$3E` and everything else returns having
// done nothing — except that two of the five latch **conditionally**, on a word
// this page keeps at `$06`, and that is the whole of what makes it interesting.
#define ACTOR_F534_COLLIDE_ENTRY 0x81f534u

// `CMP #$0003`, `#$0004`, `#$0001`, all to the same `STA $3E`.
#define F534_LATCH_A 0x0003
#define F534_LATCH_B 0x0004
#define F534_LATCH_C 0x0001
// `CMP #$0005`, `#$0006`, both to the guarded one.
#define F534_LATCH_GUARDED_A 0x0005
#define F534_LATCH_GUARDED_B 0x0006

// Where the id goes. Named for where it is: one store and no reader in reach,
// the same standing as `VICTIM_DP_FLAG_26`.
#define F534_DP_LATCH 0x3e
// ...and the guard the last two ids pass through — `LDX $06 : CPX #$0004 : BNE`,
// so those two only count while this word holds exactly four. `$06` is
// `thread_count`'s offset in the scheduler's own page and means nothing of the
// sort here; every actor lays out its own.
#define F534_DP_GUARD 0x06
#define F534_GUARD_VALUE 0x0004

// The handler. Always true: every id is answered here and nothing is called.
bool actor_f534_collide(Wram* w, uint16_t dp, uint16_t arg, ActorHandlerRegs* r);

// ---------------------------------------------------------------------------
// $83:A264  victim_a264_collide — victim_collide's sibling, one page over
// ---------------------------------------------------------------------------
//
// Same bank, same idea, same two fields: `$18` is which side claimed it and
// `$1E` is what became of it, exactly as `VICTIM_DP_CLAIMANT` and
// `VICTIM_DP_EVENT` are at `$83:A364`. What differs is the id list and one
// mechanism — where `victim_collide` clears `ACTOR_COLLIDE_ID` in its display
// record to switch itself off, this one calls `$81:8191`, which sets a byte in
// a **flat array at `$7E:605A`** indexed by a word this page keeps at `$06`.
//
// That array is the only thing in the project so far that lives above
// `$7E:2000`, and the routine that writes it is nine bytes, so it is inlined
// rather than registered — `$81:8191  CMP #$FFFF : BEQ : TAX : SEP #$20 : LDA
// #$80 : STA $7E605A,X : REP #$30 : RTL`, a single byte and a guard.
#define VICTIM_A264_COLLIDE_ENTRY 0x83a264u

// The three ids that end it one way — `$FF`, `$03`, `$04` — and the two that
// end it the other, `$05` and `$06`, which are the two players exactly as
// `victim_claim_a`/`_b` are at `$83:A364`.
#define A264_EVENT_GIVE_UP 0x0003
#define A264_EVENT_CLAIMED 0x0001
#define A264_ID_GIVE_UP_FF 0x00ff
#define A264_ID_GIVE_UP_A 0x0003
#define A264_ID_GIVE_UP_B 0x0004
#define A264_ID_CLAIM_A 0x0005
#define A264_ID_CLAIM_B 0x0006
// Two ids that are ignored by name rather than by falling off the end: `$02`
// and `$5E`. The second is the enemy family's fatal id, which is a hint about
// what can reach this thing and not evidence of anything.
#define A264_ID_IGNORE_A 0x0002
#define A264_ID_IGNORE_B 0x005e

// The last comparison, and the one that makes this a victim rather than an
// enemy: **anything at or above a weapon shot clears the event word** rather
// than setting one — `AND #$7FFF : CMP #$005C : BCC : STZ $1E : SEC : RTL`.
#define A264_SHOT_CLEARS 1  /* documentation, not a value */

#define A264_DP_CLAIMANT 0x18
#define A264_DP_EVENT 0x1e
// The index `$81:8191` uses into the array below, out of this page.
#define A264_DP_ARRAY_INDEX 0x06
// `STA $7E605A,X`, one byte of `$80`, guarded on the index not being `$FFFF`.
#define W_A264_FLAG_ARRAY 0x605au
#define A264_FLAG_SET 0x80
#define A264_INDEX_NONE 0xffff

// The handler. Always true — every id is answered and the only routine it calls
// is inlined above.
bool victim_a264_collide(Wram* w, uint16_t dp, uint16_t arg,
                         ActorHandlerRegs* r);

// ---------------------------------------------------------------------------
// $81:9063  enemy_9063_collide — level 5's, and the seventh copy of $81:8888
// ---------------------------------------------------------------------------
//
// `enemy_9b6b_collide` with two offsets moved and its one flourish removed: the
// id-`$5E` branch is a bare `JML $81:83C6` here rather than nine instructions
// and then the same `JML`. Health `$22` — which is `ACTOR_DP_HIT_ID`'s offset on
// an enemy's page and `MONSTER_DP_HEALTH`'s on the spider's, a reminder that
// these numbers mean nothing across a page.
#define ENEMY_9063_COLLIDE_ENTRY 0x819063u
#define D9063_DP_HEALTH 0x22
#define D9063_DP_HIT_ID 0x30
// Decremented on death, and named for where it is: no reader in reach.
#define D9063_DP_COUNTER_2E 0x2e

// The handler. False on `$5E` and `$5D`, to the same two addresses as every
// other member of the family.
bool enemy_9063_collide(Wram* w, const Rom* rom, uint16_t dp, uint16_t arg,
                        ActorHandlerRegs* r, uint32_t* unported);

// ---------------------------------------------------------------------------
// $81:AC92  enemy_ac92_collide — level 49's, and the ninth copy of $81:8888
// ---------------------------------------------------------------------------
//
// Nine copies now, and this one is the plainest re-spelling of `enemy_9063` yet
// — three offsets moved and **one comparison added**, which is the only reason
// it is interesting.
//
// | | `enemy_9063` | this |
// | --- | --- | --- |
// | health | `$22` | `$3C` |
// | parked id | `$30` | `$3E` |
// | death | `DEC $2E` | `DEC $10` |
// | fatal id | — | **`$67`** |
#define ENEMY_AC92_COLLIDE_ENTRY 0x81ac92u
#define DAC92_DP_HEALTH 0x3c
#define DAC92_DP_HIT_ID 0x3e
// Decremented on death, and named for where it is: no reader in reach, exactly
// as with `B41C_DP_COUNTER_0A`, `D9B6B_DP_COUNTER_26` and `D9063_DP_COUNTER_2E`.
// Four copies of this family now end a death by stepping a word down and the
// port still cannot say what any of the four counts.
#define DAC92_DP_COUNTER_10 0x10

// `$81:ACA8  CMP #$0067 : BEQ` — an id that kills this creature outright,
// skipping the subtraction, and no other copy singles it out. It is
// `MONSTER_HIT_FATAL`'s mechanism at a different id: what gets stored into
// `DAC92_DP_HEALTH` is the masked id itself.
//
// **What it buys is legible from the damage table.** `ENEMY_DAMAGE_TABLE`'s
// entry for `$67` is 4 — a middling weapon, dearer than the basic shot's 1 and a
// fifth of `$61`'s 20 — so this is not a strong weapon being waved through, it is
// one specific thing being this creature's undoing whatever its health is.
#define AC92_HIT_FATAL 0x0067

// The handler. False on `$5E` alone: `$5D` is `enemy_freeze`, which the port
// has, and `$5E` is `JML $81:83C6`, which it does not.
bool enemy_ac92_collide(Wram* w, const Rom* rom, uint16_t dp, uint16_t arg,
                        ActorHandlerRegs* r, uint32_t* unported);

// ---------------------------------------------------------------------------
// $81:E6E4  enemy_e6e4_collide — the tenth copy, and the only one that can be
// switched off
// ---------------------------------------------------------------------------
//
// Found the way a routine is supposed to be found: it is the one address on the
// decline census, put there by `movies/level29-ice.zmv` **once**. One call names
// an address and nothing else, so the input came before the port —
// `movies/level37-e6e4.zmv` reaches it 367 times, and this is written against
// that.
//
// Its body is `$81:E413`, which seeds `$0C` and `$0E` to **3** and is entered
// from two behaviours rather than one — `$81:E481` and `$81:E51A`, both by a
// plain `JSR $E413`. Nine other copies of `$81:8888` each belong to a single
// behaviour; this one is shared, which is why it turns up on levels 15, 29, 31,
// 33, 35, 37, 38, 43 and 44 with nine different actor *types* in front of it.
//
// Two things make it not just a tenth re-spelling:
//
// **It opens on `enemy_b41c_collide`'s guard applied to everything.**
// `$81:E6E4  LDY $08 : LDX $0004,Y : BNE $E72A` is the same three instructions
// `$81:B462` uses — `$08` is this page's display record and `+$04` is `ACTOR_Z`,
// the height off the ground — but where `$81:B41C` lets height decide the answer
// to **one** id and takes the damage either way, this one sends *every*
// collision to a bare `CLC : RTL` while the creature is off the ground.
//
// That matters beyond this routine. `ACTOR_Z` is documented in `port/oam.h` as
// drawing-only: an actor that jumps or is thrown keeps its Y, "and so its depth
// sort order and its collision box", and only draws higher up. The overlap pass
// really does ignore height. So **height immunity is not a property of the
// collision system, it is a thing individual handlers opt into** — two of ten do,
// and they do it with the same three instructions and to different extents.
//
// **`$5E` and `$5D` go to different routines, and `$5E` is the bubble tail.**
// `$81:E722  JML $81:83C6` and `$81:E726  JML $81:847E` — which is
// `enemy_ac92_collide`'s pair rather than `enemy_d7f6_collide`'s, where the same
// `$5E` falls into the death tail without a subtraction. Ten copies of this
// subsystem and no two of them answer `$5E` the same way.
#define ENEMY_E6E4_COLLIDE_ENTRY 0x81e6e4u
#define E6E4_DP_HEALTH 0x0c
#define E6E4_DP_HIT_ID 0x22
// Decremented on death. The fifth counter in this family named for where it is
// rather than for what it counts, and the fifth with no reader in reach.
#define E6E4_DP_COUNTER_0A 0x0a

// Where the record lives on this page — `$81:E6E4  LDY $08`, the same `$08`
// `enemy_d7f6_collide` and three others keep theirs at.
#define E6E4_DP_RECORD 0x08

// The handler. Both of the ids that `JML` elsewhere are served — `$81:83C6` is
// `enemy_bubble_react` and `$81:847E` is `enemy_freeze`, and the port has had
// both since the level-49 round — so `unported` is carried for the family's
// signature and never written.
bool enemy_e6e4_collide(Wram* w, const Rom* rom, uint16_t dp, uint16_t arg,
                        ActorHandlerRegs* r, uint32_t* unported);

// ---------------------------------------------------------------------------
// $81:990B  enemy_990b_collide — the eleventh copy, and the only one that
// subtracts the same damage twice
// ---------------------------------------------------------------------------
//
// Found the same way `enemy_e6e4_collide` was: it is the one address on the
// decline census, put there by `movies/level29-item.zmv` 35 times, and it went
// on the census the moment that movie joined the corpus. The README claim that
// the ROM is never asked to run a routine the port does not have was true at
// forty-three movies and false at forty-four, and nothing noticed for two
// commits because nobody re-ran the census.
//
// **It is not in `analysis/bank_81.asm`.** Not one byte of it — the tracer never
// saw this creature hit, so all ninety-four bytes are `.db`, and this is the
// sixth time `tools/dis816.py` has had to go to the ROM for something four banks
// of grepping said did not exist.
//
// Two things make it the eleventh spelling rather than a duplicate:
//
// **There are two damage pools and one subtraction feeds both.** Every other
// copy takes the `ENEMY_DAMAGE_TABLE` entry out of one health word and is done.
// This one takes it out of `$2A`, and then — having already decided the creature
// lived — takes *the same entry* out of `$4A` as well:
//
//     $81:9934  STA $2A                 ; health, and it survived
//     $81:9936  LDA $4A : SEC
//     $81:9939  SBC $818561,X           ; the same damage again
//     $81:993D  STA $4A
//     $81:993F  BMI $9962               ; the second pool is what ran out
//
// So the creature has a health bar and a **stagger meter**, both draining at the
// same rate, and the meter is the shorter of the two: `$81:9652  LDA #$000F :
// STA $4A` refills it to 15 every time it empties. What emptying it buys is at
// `$81:9643` — `LDA #$96E4 : LDY #$0081 : JSL $80:8475` swaps in a *different*
// collision handler, `LDA #$0006 : STA $7E`, and then eight metasprite loads out
// of `$81:96F1` two `thread_yield`s apart. A spin, with someone else answering
// for it while it spins.
//
// **Its death path decrements nothing.** Seven copies of this family end a death
// with a `DEC` of some word on the page, and five of those words —
// `B41C_DP_COUNTER_0A`, `D9B6B_DP_COUNTER_26`, `D9063_DP_COUNTER_2E`,
// `DAC92_DP_COUNTER_10`, `E6E4_DP_COUNTER_0A` — are named for where they are
// because nothing in reach reads them. `$81:9945  STA $2A : STZ $7E : SEC : RTL`
// is three instructions and no counter at all, which makes it the shortest death
// in the family and settles nothing about what the other five words are for.
#define ENEMY_990B_COLLIDE_ENTRY 0x81990bu
#define D990B_DP_HEALTH 0x2a
#define D990B_DP_HIT_ID 0x2e

// The second pool, drained by the same damage as the health word above and
// refilled to `$000F` by `$81:9652` when it empties. Named for what the code
// does with it and no more: nothing in reach calls it a stagger meter, but
// nothing in reach reads it for any other purpose either.
#define D990B_DP_STAGGER 0x4a

// A **second display record's address**, kept on this page, and the `$5D` path
// is the only thing that touches it:
//
//     $81:994F  PHA
//     $81:9950  LDX $40 : CPX #$FFFF : BEQ +
//     $81:9957  LDA #$0000 : STA $000E,X
//     $81:995D  + PLA : JML $81:847E
//
// `$000E` off a record is `ACTOR_COLLIDE_ID` (`port/oam.h`), so freezing this
// creature switches something *else* off — and `$FFFF` is the page saying it has
// no companion right now, which is why the guard is there at all. No other copy
// of `$81:8888` reaches outside its own page on the way into `enemy_freeze`.
#define D990B_DP_PEER 0x40
#define D990B_PEER_NONE 0xffff

// $81:9633, the four instructions the emptied stagger meter runs.
//
// It is `enemy_survived_react`'s guard with the splice replaced by a store:
// `LDY $08 : LDA $0000,Y : AND #$0010 : BNE <RTS>` is `$81:8506`'s first four
// instructions verbatim — already flashing, so do nothing — and what stands
// where the stack surgery would be is `LDA #$9643 : STA $12`, an address parked
// in `ACTOR_DP_DEATH_REQ` for the enemy body's own `LDA $12 : BEQ <loop>` to
// find on its next pass. A request to leave the main loop, not a death.
#define ENEMY_990B_STAGGER_ENTRY 0x819633u
#define D990B_STAGGER_BODY 0x9643

// The handler. Both `JML` ids are served — `$5E` is `enemy_bubble_react` and
// `$5D` is `enemy_freeze` behind the peer store above — so `unported` is carried
// for the family's signature and never written.
bool enemy_990b_collide(Wram* w, const Rom* rom, uint16_t dp, uint16_t arg,
                        ActorHandlerRegs* r, uint32_t* unported);

// $81:9633. Always true: it reads two words and may write one.
void enemy_990b_stagger(Wram* w, uint16_t dp, ActorHandlerRegs* r);

// --- $81:96E4 ---------------------------------------------------------------
//
// **Seven instructions, and the shortest route from a census line to a port
// this project has had.** `$81:9643` — the body the stagger parks in `$12` —
// opens by swapping the collision handler out for this one and spinning; this
// is what answers for the creature while it spins, and `movies/level29-990b.zmv`
// made the ROM ask for it 430 times.
//
//     $81:96E4  CMP #$005C
//     $81:96E7  BCS $96EB
//     $81:96E9  CLC : RTL
//     $81:96EB  JML $818506
//
// That is the whole routine. The ignore half is `enemy_990b_collide`'s own first
// four bytes for the twelfth time; the other half is a bare `JML` into
// `enemy_survived_react` with **nothing in front of it** — no id parked at
// `$2E`, no mask, no damage table, no health. A creature in the spin takes no
// damage from anything and flashes at everything.
//
// So the two pools stop draining for the length of the spin, and the flash keeps
// arriving.
//
// **The last round guessed this routine was what `d990b_stagger_already` was
// waiting on, and that guess is now measured and wrong.** It is ported, it runs
// 1,918 times on `movies/level29-990b.zmv`, and the site is still zero. What the
// bytes around it say instead is that the site has a *fifteen-hit floor*:
//
//     $81:9652  LDA #$000F : STA $4A     ; at the top of the spin
//     ...
//     $81:96D2  LDA #$000F : STA $4A     ; and again at the bottom
//     $81:96D7  LDA #$990B : JSL $808475 ; two instructions later, the swap back
//
// The stagger meter is refilled at **both** ends of the spin, and the second
// refill is two instructions in front of the handler being swapped back — so the
// instant `$81:990B` is answering again, `$4A` is exactly `$000F`. A guard that
// wants the pool emptied inside a two-tick flash cannot be reached by the hit
// that follows a stagger, or the fourteen after it. It needs the fifteenth to
// land inside a flash that one of the fourteen before it installed, and over 121
// staggers no input has yet put one there.
//
// `$81:96EF  CLC : RTL` sits under the `JML` and nothing reaches it. Two dead
// bytes, of the same shape as the two live ones four instructions up, which is
// what a copied exit looks like after the copy stopped needing it.
#define ENEMY_990B_SPIN_ENTRY 0x8196e4u

// Always true, and it cannot decline: the far side of its one branch has been
// ported since the level-53 round.
bool enemy_990b_spin_collide(Wram* w, uint16_t dp, uint16_t arg,
                             ActorHandlerRegs* r);

// ---------------------------------------------------------------------------
// $81:845E  actor_845e_collide — thirty-two bytes and **no stores at all**
// ---------------------------------------------------------------------------
//
// `actor_deeb_collide` holds the record for the shortest handler in the game;
// this one holds a different record, and it is the more surprising of the two.
// It is four comparisons, a mask and two two-instruction exits — and it **writes
// nothing anywhere**. No latch, no counter, no health, no record. Every other
// handler in the project leaves at least one word behind. Carry is not the whole
// of this one's interface the way it is *most* of `$82:DEEB`'s; carry is
// literally all of it.
//
// What it decides is whether the thread parks. `thread_call_handler` parks on
// carry set, so:
//
//   * ids `$03`, `$05` and `$06` — tested **before** the mask, so by name and
//     not as weapons — park it;
//   * anything below `COLLIDE_ID_PLAYER` does not;
//   * `$5E` does not, and it is picked out one comparison later;
//   * every other id at or above `COLLIDE_ID_PLAYER` parks it.
//
// So the one thing that does *not* stop this actor is the id `$81:83C6` belongs
// to. That is the only place in the game where `$5E` is the exception rather
// than a branch of its own, and it is the second reading of that id this round
// after `AC92_HIT_FATAL`'s neighbour.
#define ACTOR_845E_COLLIDE_ENTRY 0x81845eu

// The three ids compared before the `AND #$7FFF`, so the raw argument is what
// they match — a shot from player two carries bit 15 and could not be one of
// these anyway, all three being far below a weapon.
#define D845E_PARK_A 0x0005
#define D845E_PARK_B 0x0006
#define D845E_PARK_C 0x0003
// ...and the one masked id that leaves it running.
#define D845E_PASS_ID 0x005e

// The handler. Always true, and it never touches `w` — which is why it does not
// take one.
bool actor_845e_collide(uint16_t arg, ActorHandlerRegs* r);

// ---------------------------------------------------------------------------
// $81:EDAA  shot_edaa_collide — one byte, and the smallest routine there can be
// ---------------------------------------------------------------------------
//
// `$81:EDAA  6B` — a bare `RTL`, and that is the whole routine. It is the
// collision handler the **heavy weapon's shot** installs: the four bytes
// immediately after it, `$81:EDAB  61 00 61 80`, are weapon `$61`'s two-player
// collision-id table, so this address and that table are one weapon's shot code
// laid out back to back.
//
// **What it means is that this shot does not react to anything it hits.** The
// squirt gun's `$81:FE0E` looks at the id, expires on some and passes through
// others; this one is told about every collision it has and answers none of
// them. The thing it hit still reacts — the enemy handler runs on its own side
// of the pair, which is how `boss_remap_61` gets taken — but the shot itself
// carries on, which is what a weapon that costs five shots a pickup should do.
//
// **Its interface is the strongest claim any shim in the project makes, and it
// is strong precisely because the routine is empty.** `RTL` sets no flag and
// touches no register, so A, X, Y, N, Z, C and V all come back exactly as they
// went in — every one of them claimed, and every one of them checked on every
// call. `$82:F1C2` is the cautionary opposite: no `CLC` and no `SEC` there
// either, but a `CMP` on every path had already decided carry, and reading "no
// carry instruction" as "carry passes through" failed on the second call. Here
// there is no instruction at all, which is a different fact and the only one
// that licenses this claim.
#define SHOT_EDAA_COLLIDE_ENTRY 0x81edaau

bool shot_edaa_collide(void);

// ---------------------------------------------------------------------------
// $81:F6A3  shot_f6a3_collide — one shot handler for four weapons
// ---------------------------------------------------------------------------
//
// ```
// $81:F6A3  CMP #$0003 : BEQ $F6B4
//           CMP #$0004 : BEQ $F6B4
//           CMP #$0001 : BEQ $F6B4
//           CLC : RTL
// $81:F6B4  STA $3E : CLC : RTL
// ```
//
// Seventeen bytes: three comparisons, one store and two exits that both clear
// carry, so — unlike `actor_845e_collide`, which is the same shape — **this one
// can never park its thread**. A shot that hits something keeps flying either
// way; what changes is whether the shot's own body is told.
//
// **It is identified the same way `$81:EDAA` was, off the bytes after it.**
// `$81:F6B8` begins `63 00 63 80 | 64 00 64 80 | 64 00 64 80 | 66 00 66 80 |
// 67 00 67 80` — five two-word collision-id tables in a row, and `$81:F6B8` is
// the address `docs/cosim.md`'s weapon table already gives for id `$63`. So this
// is not one weapon's shot code but **four weapons sharing one handler**, which
// is the first time any address in this project has been reached by more than
// one weapon.
//
// **`$3E` is the shot's "what I hit", and it has both its seed and its readers
// in the same bank.** `$81:F691  STZ $3E` clears it four instructions before
// `$81:F69B  LDA #$F6A3 : LDY #$0081 : JSL $80:8475` installs this very handler,
// and `$81:F3F6  LDA $3E : BNE` and `$81:F57B  LDA $3E : BEQ` are the shot body
// polling it on its next pass. So the store is not a latch the port has to guess
// at: the word is zero on every frame the shot has hit nothing, and the three
// ids below are the only things that can make it non-zero.
#define SHOT_F6A3_COLLIDE_ENTRY 0x81f6a3u

// The three ids it records. They are compared against the *unmasked* argument,
// like `actor_845e_collide`'s and for the same reason — all three are far below
// `COLLIDE_ID_PLAYER`, so no weapon and no second player's anything can be one.
#define SHOT_F6A3_RECORD_A 0x0003
#define SHOT_F6A3_RECORD_B 0x0004
#define SHOT_F6A3_RECORD_C 0x0001

// `STA $3E`, on the shot's own page.
#define SHOT_F6A3_DP_HIT_ID 0x3e

bool shot_f6a3_collide(Wram* w, uint16_t dp, uint16_t arg, ActorHandlerRegs* r);

// ---------------------------------------------------------------------------
// $82:F4EF  actor_f4ef_collide — sixteen bytes, and it only wants the players
// ---------------------------------------------------------------------------
//
// ```
// $82:F4EF  CMP #$0005 : BEQ $F4FB
//           CMP #$0006 : BEQ $F4FB
//           CLC : RTL
// $82:F4FB  STA $18 : CLC : RTL
// ```
//
// The same shape as `shot_f6a3_collide` one comparison shorter, and the first
// handler in the project that lives in **bank `$82`** rather than `$81` — the
// bank the level and UI code is in, not the actor bank.
//
// **What makes it different from the other latches is which ids it names.**
// `$05` and `$06` are the two players' own collision ids, the ones every other
// handler in the game sees at the bottom of its `CMP #$005C` and throws away as
// "below a shot's". This one throws away everything *else*: no weapon, no
// monster and no shot can make it store anything. It is an actor whose entire
// collision interface is "a player is standing on me".
//
// **`$18` has its seed and its reader in the same routine**, which is the
// pattern `$81:F6A3`'s `$3E` established last round and the reason neither store
// has to be guessed at. `$82:F3A7  STZ $18` is four instructions ahead of
// `$82:F3A9  LDA #$F4EF : LDY #$0082 : JSL $80:8475`, so the word is zero on
// every frame the actor has not been touched, and `$82:F4C6  LDA $18 : BEQ` is
// the body polling it — the branch back is `$F4AF`, so a zero means *keep
// waiting* and anything else falls through to `JSR $82:F3F4 : SEC`.
#define ACTOR_F4EF_COLLIDE_ENTRY 0x82f4efu

// The two ids it answers to, compared against the unmasked argument. Both are
// far below `COLLIDE_ID_PLAYER`, so the mask would change nothing.
#define F4EF_LATCH_P1 0x0005
#define F4EF_LATCH_P2 0x0006

// `STA $18`, on the actor's own page.
#define F4EF_DP_HIT_ID 0x18

bool actor_f4ef_collide(Wram* w, uint16_t dp, uint16_t arg, ActorHandlerRegs* r);

// ---------------------------------------------------------------------------
// $82:F958  exit_door_collide -- the door a finished level opens
// ---------------------------------------------------------------------------
//
// When the last neighbour is accounted for a door appears, and the level ends
// when a player walks onto it. This is how the door finds out. A player
// touching it is remembered twice: at `$14` on the door's page, and in
// `$1FBC`, which is the last player to have touched any door. The first touch
// by a player who is not that one counts the door's word at `$0A` down and
// comes back with carry set, which wakes the door's thread. The same player
// again, and anything that is not a player, changes nothing more.
#define EXIT_DOOR_COLLIDE_ENTRY 0x82f958u

#define EXIT_DOOR_ID_PLAYER_A 0x0005
#define EXIT_DOOR_ID_PLAYER_B 0x0006
#define EXIT_DOOR_DP_COUNT 0x0a    // counted down by each new player
#define EXIT_DOOR_DP_TOUCHED 0x14  // the player touching it
#define W_EXIT_DOOR_LAST 0x1fbc    // the last player to touch a door

// The handler. Always true.
bool exit_door_collide(Wram* w, uint16_t dp, uint16_t arg, ActorHandlerRegs* r);

// ---------------------------------------------------------------------------
// $81:F25D  actor_f25d_collide -- something that takes hits, and counts them
// ---------------------------------------------------------------------------
//
// Named for its address: the thread at `$81:F2B2` installs it, and no port
// has looked at that yet. A hit is id 3 or 4, or anything of the players'
// side: `$5C` and above, once bit 15 is masked off. Each takes one from the
// word at `$40`,
// and the hit that takes it below zero takes one from `$1C` as well. Id `$61`
// takes one from `$1C` whatever `$40` holds. All of those come back with
// carry set. Anything else is ignored, with carry clear.
#define ACTOR_F25D_COLLIDE_ENTRY 0x81f25du

#define F25D_ID_HIT_A 0x0003
#define F25D_ID_HIT_B 0x0004
#define F25D_ID_OUTRIGHT 0x0061
#define F25D_DP_STATE 0x1c  // counted down when the hits run out
#define F25D_DP_HITS 0x40   // hits it can still take

// The handler. Always true.
bool actor_f25d_collide(Wram* w, uint16_t dp, uint16_t arg, ActorHandlerRegs* r);

// ---------------------------------------------------------------------------
// $81:F8FC  actor_f8fc_collide -- remembers which of four it touched
// ---------------------------------------------------------------------------
//
// Named for its address: `$81:F8F1` installs it. Ids 1, 3, 4 and `$35` are
// stored at `$1C` on its page, for its thread to find, and everything else
// is ignored. Carry comes back clear either way, so it never wakes anything.
#define ACTOR_F8FC_COLLIDE_ENTRY 0x81f8fcu

#define F8FC_ID_A 0x0003
#define F8FC_ID_B 0x0004
#define F8FC_ID_C 0x0001
#define F8FC_ID_D 0x0035
#define F8FC_DP_TOUCHED 0x1c

// The handler. Always true.
bool actor_f8fc_collide(Wram* w, uint16_t dp, uint16_t arg, ActorHandlerRegs* r);

// ---------------------------------------------------------------------------
// $82:DEEB  actor_deeb_collide — seven bytes, and the smallest in the game
// ---------------------------------------------------------------------------
//
// `CMP #$00FF : BEQ : CLC : RTL` / `SEC : STA $12 : RTL`. One id, one store,
// two exits, and it takes the record from `enemy_b592_collide` as the shortest
// handler in the project by a factor of three.
//
// The carry is the whole of its interface: the id it answers to comes back with
// carry **set**, which parks its thread, and everything else clears it. So this
// is an actor whose entire collision behaviour is "when `$FF` touches me, stop
// and remember what it was".
#define ACTOR_DEEB_COLLIDE_ENTRY 0x82deebu
#define DEEB_ID_STOP 0x00ff
#define DEEB_DP_LATCH 0x12

// The handler. Always true.
bool actor_deeb_collide(Wram* w, uint16_t dp, uint16_t arg, ActorHandlerRegs* r);

// ---------------------------------------------------------------------------
// $82:F1C2  actor_f1c2_collide — level 37's, and a lesson about carry
// ---------------------------------------------------------------------------
//
// Thirty-six bytes and four ids. It contains **no `CLC` and no `SEC`**, which
// this header read as "carry comes back as the caller left it" — and `verify`
// said `flag C: ROM 1, port 0` on the second call it ever saw.
//
// `CMP` *is* a subtraction and it sets carry. Every path out of this routine has
// executed at least one, so carry is fully determined after all: **set** on all
// four acting paths (three of them by an equal comparison, the fourth by the
// `BCS` that got there) and **clear** on the ignore path, where `CMP #$005C`
// borrowed. The absence of a carry instruction is not the absence of a carry
// output, and this is the one routine in the registry where the difference was
// load-bearing enough to fail.
#define ACTOR_F1C2_COLLIDE_ENTRY 0x82f1c2u

// `CMP #$0001`, `#$0005`, `#$0006`, plus anything at or above a weapon shot
// once masked — four ways to reach one pair of stores.
#define F1C2_ID_A 0x0001
#define F1C2_ID_B 0x0005
#define F1C2_ID_C 0x0006

// What those stores are: clear its own record's `ACTOR_COLLIDE_ID` — the same
// switching-off `shot_collide` and `victim_collide` do, through the record
// pointer this page keeps at `$08` — and decrement a word at `$14`.
#define F1C2_DP_RECORD 0x08
#define F1C2_DP_COUNTER_14 0x14

// The handler. Always true.
bool actor_f1c2_collide(Wram* w, uint16_t dp, uint16_t arg, ActorHandlerRegs* r);

// ---------------------------------------------------------------------------
// $82:9660  boss_9660_collide — level 25's, and the first handler in bank $82
// ---------------------------------------------------------------------------
//
// **The creature is not identified and the routine is named for its address**,
// the same choice `enemy_b41c_collide` makes. What *is* established is that it
// is a boss rather than an enemy, and the evidence is all in the code its
// handler shares a page with:
//
// * its thread allocates **four** display records (`$82:94B4`, called four
//   times into `$24`/`$26`/`$28`/`$2A`), where every other actor in the project
//   has one;
// * it starts on **70 health** (`$82:955E  LDA #$0046 : STA $3C`) against an
//   enemy's 0 and level 33's 4;
// * dying pays `$2000` (`$82:95AC  LDX #$2000 : LDA $42 : BEQ : JSL score_add`)
//   — **twice a victim's `$1000` and the largest award in the game**;
// * and the death itself is a set piece rather than a slot being freed: sixteen
//   passes of a mosaic ramp queued into vblank (`$82:95E3`), `INC $1D52`, and a
//   `thread_spawn` of `$83:9776` where it stood.
//
// It is also the first ported handler that **mixes absolute and direct-page
// addressing**, and reading that correctly is the whole of getting it right.
// `LDY $0078` and `LDA $0020` are three-byte absolute operands — the globals
// `W_HANDLER_SELF` and `W_SCHED_TICK` — while `$3A`, `$3C`, `$3E`, `$40`, `$42`
// and `$44` are two-byte direct-page ones on this thread's own page. The two
// coincide only if `D` is zero and it is not: `$82:948F` writes absolute
// `$003C` as a *coordinate* in the same routine that seeds direct `$3C` to 70.
#define BOSS_9660_COLLIDE_ENTRY 0x829660u

// --- the two guards, and both are states rather than ids --------------------

// `LDY $0078 : LDX $000E,Y : CPX #$0009 : BEQ`. Its own record's
// `ACTOR_COLLIDE_ID`, not the other one's — the boss refusing to be hit while
// it is wearing a particular id. `$82:94B4` builds all four records with `$03`,
// so `$09` is a state something else in its body installs.
#define BOSS_9660_ID_INVULNERABLE 0x0009

// `LDX $40 : BNE`. The flash timer, and the second guard. `$82:8F6A` is the
// other half and the reason this one is worth a name: on a pass where `$40` is
// zero and `BOSS_9660_DP_HIT_FLAG` has gone negative, the body clears the flag,
// sets this to 3 and swaps the palette; three passes later it swaps it back.
// **So a hit while the boss is flashing is not a hit at all** — this is
// `react_already`'s guard with a consequence, like `enemy_cdde_collide`'s, but
// tested at the door rather than at the kill.
#define BOSS_9660_DP_FLASH 0x40

// --- this actor's own page --------------------------------------------------

// Health, seeded to 70 at `$82:9561`.
//
// **And that number is why `boss_died` is untaken**, though not for the reason
// first written here. The original note priced a kill off the basic shot — id
// `$5C`, `ENEMY_DAMAGE_TABLE` entry 1, so seventy clean hits — and concluded
// that what closed the site was "a weapon or a route". The weapon exists: `$67`
// costs 4 and is not one of the four ids rewritten below, so eighteen hits do
// it, and level 25 places forty shots of it. **The route exists too, and it is
// what the arithmetic kept missing.** A shot fired from the row
// `movies/level25-heavy.zmv` fights on dies six pixels out against a wall; the
// corridor the boss actually crosses is twenty-seven pixels lower, 408 px long,
// and `zamn_assets route --reach <rom> 26` draws it. Fighting in it takes the
// same forty shots from 16 damage to 26.
//
// What is left is arithmetic rather than mystery, and it is short. Of the six
// weapons level 25 places, three cannot be picked up from the boss's own side
// of the map — `$5F`'s 300 shots and `$64` sit in scenery, and `$62` is in a
// sealed pocket — so the reachable arsenal is 150 `$5C`, 40 `$67` and 20 `$61`
// (rewritten to `$60`), which is 390 damage of ammunition against 70 of health
// at an accuracy no input has yet got above about a fifth. See `docs/cosim.md`,
// "The stream of fire that was a wall".
#define BOSS_9660_DP_HEALTH 0x3c
// Where the raw id is parked, sign bit and all — and unlike every other copy of
// this idea, something *reads* it: the death sequence hands it to `score_add`,
// whose bit 15 is which player gets the `$2000`. The main loop clears it at the
// top of every pass (`$82:9579  STZ $42`), so what the award sees is the id of
// whatever landed the killing blow on that pass and nothing older.
#define BOSS_9660_DP_HIT_ID 0x42
// **The outbound "you were hit" message**, and it is `B41C_DP_HIT_FLAG` upside
// down: that one counts up from zero and its body tests `BNE`, this one counts
// *down* from zero and its body tests `BIT $3E : BPL` — a sign bit rather than
// a count. `$82:8F81  STZ $3E` is the consumer.
#define BOSS_9660_DP_HIT_FLAG 0x3e
// Hits until it changes its mind. Seeded to 6 (`$82:9566`), decremented on
// every hit that reaches the damage path, and read at two decision points in
// the body (`$82:8C79`, `$82:8CDF`, both `LDA $44 : BMI`) which put it back to
// 6 on the way past. So six hits is a phase.
#define BOSS_9660_DP_PHASE_COUNT 0x44
// The death flag, and the thing that ends the main loop: `$82:959C  LDA $3A :
// BEQ <loop>`. The handler decrements it from zero rather than storing a
// constant, which is the same spelling `$3E` uses one word up.
#define BOSS_9660_DP_DEAD 0x3a

// --- the four ids that are answered as some other id ------------------------
//
// No other handler in the game does this. Four ids are rewritten before the
// `SBC #$005C` that turns an id into an `ENEMY_DAMAGE_TABLE` index, so what a
// weapon costs this boss is not what the table says under that weapon's own id.
// Two of the four are rewritten **on a coin toss taken from the scheduler
// clock** — `LDA $0020 : AND #$0001` and `AND #$0003` — so the same weapon does
// two different amounts of damage depending on the tick the hit landed on. That
// is the first use of `W_SCHED_TICK` as a random source in ported code, and it
// is not `$80:9D39`: it is the low bits of a counter, read straight.
// Reading `ENEMY_DAMAGE_TABLE` says what the rewrites buy, and it is not small:
// `$61` costs 20 and `$60` costs 4, so that remap is this creature taking a
// fifth of what that weapon does to anything else; `$5C` costs 1 and **`$5D`
// costs 0**, so the coin toss is a weapon that does one damage half the time
// and nothing the other half. `boss_no_damage` is therefore not a defensive
// branch — it is the ordinary outcome of two of the four rewritten ids, and it
// is untaken only because no input has fired those weapons at this boss.
#define BOSS_9660_ID_ALT_HALF 0x0062   // 1 tick in 2 answers as the dearer id
#define BOSS_9660_ID_ALT_QUARTER 0x0070  // 1 in 4
#define BOSS_9660_ID_REMAP_61 0x0061   // always answers as $60
#define BOSS_9660_ID_REMAP_6F 0x006f   // always answers as $63
#define BOSS_9660_ID_CHEAP 0x005c      // what the coin toss answers on a miss
#define BOSS_9660_ID_DEAR 0x005d       // ...and on a hit
#define BOSS_9660_ID_61_AS 0x0060
#define BOSS_9660_ID_6F_AS 0x0063

// The handler. Always true: there is no `JML` out of it, no id it hands back
// and nothing under it that is not ported, so it declares no guard — the third
// handler in the registry that does not, after `enemy_cdde` and `enemy_b592`.
bool boss_9660_collide(Wram* w, const Rom* rom, uint16_t dp, uint16_t arg,
                       ActorHandlerRegs* r);

// Which path a call took, for `src/cosim/routines.c` to price.
//
// Three groups, and a call increments exactly one block of the first three, or
// `HIT` plus one remap plus one damage exit. That is the routine's own shape:
// everything before `$82:9678` is a guard, everything between there and
// `$82:96BA` is the id being rewritten, and everything after is arithmetic on
// health.
//
// **The two coin-toss ids are four blocks rather than two.** `$62` and `$70`
// end at the same two `LDA #$005C`/`LDA #$005D`, but `$70` is reached one
// comparison and one taken branch further down the chain, so the two cost
// different amounts to arrive at the same answer — `shot_collide`'s four stop
// ids are separate blocks for exactly this reason and no other.
typedef enum {
  BOSS_BLK_INVULN,          // $82:9669 BEQ taken: wearing id $09, out at $9674
  BOSS_BLK_FLASHING,        // $82:966D BNE taken: $40 still running, same exit
  BOSS_BLK_IGNORE,          // $82:9672 BCS not taken: below $5C, same exit
  BOSS_BLK_HIT,             // ...taken: $9678 STA $42 : AND #$7FFF
  BOSS_BLK_REMAP_62_CHEAP,  // id $62 and the tick is even: answers as $5C
  BOSS_BLK_REMAP_62_DEAR,   // ...or odd, and it answers as $5D
  BOSS_BLK_REMAP_70_CHEAP,  // id $70, two comparisons further in
  BOSS_BLK_REMAP_70_DEAR,   // ...and the dear half of the same toss
  BOSS_BLK_REMAP_61,        // always $60
  BOSS_BLK_REMAP_6F,        // always $63
  BOSS_BLK_REMAP_NONE,      // the chain ran out and the id stood
  BOSS_BLK_DIED,            // $82:96CB BMI taken: DEC $3A, health not stored
  BOSS_BLK_NO_DAMAGE,       // $82:96CF BEQ taken: the table entry was zero
  BOSS_BLK_SURVIVED,        // ...not taken, so $82:96D1 STA $3C
  BOSS_BLOCK_COUNT,
} BossCollideBlock;

typedef struct {
  uint16_t blocks[BOSS_BLOCK_COUNT];
} BossCollideWork;

bool boss_9660_collide_counted(Wram* w, const Rom* rom, uint16_t dp,
                               uint16_t arg, ActorHandlerRegs* r,
                               BossCollideWork* work);

// ---------------------------------------------------------------------------
// $82:AA2E  boss_aa2e_collide — records 20, 40 and 47, and `$82:9660`'s sibling
// ---------------------------------------------------------------------------
//
// Named for its address, like `boss_9660_collide`, and a boss for the same
// reasons: its body at `$82:A87E` draws through the `$1E6A`/`$1E6C` boss draw
// words, seeds 75 health, and installs this with `LDA #$AA2E : LDY #$0082 :
// JSL thread_set_handler`. It was the second most frequent decline in
// `thread_call_handler`, 1,888 over one pass of every record.
//
// The shape is `$82:9660`'s, and the details all differ:
//
// | | `boss_9660` | this |
// | --- | --- | --- |
// | its own record's id refuses a hit at | `$09` | `$00` or `$09` |
// | flash guard | `LDX $40` | `LDX $4C` |
// | parked id | `$42`, cleared on every refusal | `$4E`, never cleared |
// | id refused after parking | none | `$60` |
// | rewrites | `$62`, `$70`, `$61`, `$6F` | `$70`, `$5F`, `$6F`, `$61`, `$62` |
// | counted on every hit | `DEC $3E : DEC $44` | `DEC $4A` |
// | health | `$3C`, 70 | `$48`, 75 |
// | dead flag | `DEC $3A` | `DEC $46` |
//
// There is no `JML`, no `JSR` and no id handed back, so it declares no guard.
#define BOSS_AA2E_COLLIDE_ENTRY 0x82aa2eu

// `LDY $0078 : LDX $000E,Y` and two `CPX`: its own record's
// `ACTOR_COLLIDE_ID`, which the boss clears or sets to `$09` while it will not
// be hit.
#define BOSS_AA2E_ID_OFF 0x0000
#define BOSS_AA2E_ID_INVULNERABLE 0x0009

// This thread's own page. `$82:A89A  STZ $4C`, `STZ $4A`, `STZ $46` and
// `LDA #$004B : STA $48` seed all four at the top of the body.
#define BOSS_AA2E_DP_FLASH 0x4c      // `LDX $4C : BNE` refuses the hit
#define BOSS_AA2E_DP_HIT_ID 0x4e     // the raw id, bit 15 and all
#define BOSS_AA2E_DP_HEALTH 0x48
#define BOSS_AA2E_DP_HIT_COUNT 0x4a  // `DEC $4A` on every hit that is scored
#define BOSS_AA2E_DP_DEAD 0x46       // `DEC $46` when health would go negative

// The id it parks and then refuses, `CMP #$0060 : BEQ` into the `CLC : RTL`.
#define BOSS_AA2E_ID_IMMUNE 0x0060

// The rewrites, applied before the id becomes an `ENEMY_DAMAGE_TABLE` index.
// `$70` and `$5F` are decided by `LDA $0020 : BIT #$0003`, the low bits of the
// scheduler tick: three ticks in four answer as `$5D` and one as `$5C`.
#define BOSS_AA2E_ID_TOSS_A 0x0070
#define BOSS_AA2E_ID_TOSS_B 0x005f
#define BOSS_AA2E_ID_AS_5C_A 0x006f  // always answers as `$5C`
#define BOSS_AA2E_ID_AS_5C_B 0x0062  // ...and so does this one
#define BOSS_AA2E_ID_REMAP_61 0x0061 // always answers as `$66`
#define BOSS_AA2E_ID_61_AS 0x0066
#define BOSS_AA2E_ID_CHEAP 0x005c
#define BOSS_AA2E_ID_DEAR 0x005d

// The handler. Always true.
bool boss_aa2e_collide(Wram* w, const Rom* rom, uint16_t dp, uint16_t arg,
                       ActorHandlerRegs* r);

// ---------------------------------------------------------------------------
// The last six handlers `thread_call_handler` declined on, one pass of every
// record
// ---------------------------------------------------------------------------
//
// Each is small. Every routine any of them reaches is either already ported
// (`$81:8506`, `$81:83C6`, `$81:847E`) or short enough to inline here
// (`$81:C6EC`, `$81:C6A7`, `$80:9D6A`), so none declares a guard. With these
// the dispatcher has an answer for every handler the 56 records install in
// the sweep.

// --- $82:F330  actor_f330_collide — records 31 and 36 -------------------------
//
// `STA $16 : AND #$7FFF`, then four ids that `DEC $10` and set carry, and
// `$FF`, which sets carry without the `DEC`. Anything else is `CLC : RTL`.
#define ACTOR_F330_COLLIDE_ENTRY 0x82f330u
#define F330_DP_HIT_ID 0x16
#define F330_DP_COUNT 0x10
#define F330_ID_A 0x005d
#define F330_ID_B 0x0062
#define F330_ID_C 0x005c
#define F330_ID_D 0x0065
#define F330_ID_PARK 0x00ff
bool actor_f330_collide(Wram* w, uint16_t dp, uint16_t arg, ActorHandlerRegs* r);

// --- $81:A638  actor_a638_collide — record 48 -------------------------------
//
// Eleven bytes: `$FF` is `DEC $2A : SEC : RTL`, anything else `CLC : RTL`.
#define ACTOR_A638_COLLIDE_ENTRY 0x81a638u
#define A638_DP_COUNT 0x2a
#define A638_ID_PARK 0x00ff
bool actor_a638_collide(Wram* w, uint16_t dp, uint16_t arg, ActorHandlerRegs* r);

// --- $82:84AC  actor_84ac_collide — record 12 --------------------------------
//
// Only id `$62` does anything. The raw id is parked at `$34` and cleared again
// for every other shot. For `$62`, a negative `$32` is reset to 4 and `$30` is
// decremented, with carry set.
#define ACTOR_84AC_COLLIDE_ENTRY 0x8284acu
#define A84AC_DP_HIT_ID 0x34
#define A84AC_DP_TIMER 0x32
#define A84AC_DP_COUNT 0x30
#define A84AC_ID 0x0062
#define A84AC_TIMER_RESET 0x0004
bool actor_84ac_collide(Wram* w, uint16_t dp, uint16_t arg, ActorHandlerRegs* r);

// --- $81:B95F  enemy_b95f_collide — record 36, and another `$81:8888` -------
//
// `enemy_d7f6_collide` with its id `$5E` test replaced by `$5D`: health at
// `$0C`, the raw id parked at `$5A`, `$5D` straight into the death tail, and
// every other shot through `ENEMY_DAMAGE_TABLE`. There is no `STZ $7E` in the
// death tail, and `$5E` is just another row of the table.
//
// It is the handler of what a destroyed doll can leave, the thread at
// `$81:B664`: see `port/flame.h`.
#define ENEMY_B95F_COLLIDE_ENTRY 0x81b95fu
#define B95F_DP_HEALTH 0x0c
#define B95F_DP_HIT_ID 0x5a
#define B95F_DP_DEAD 0x0a
bool enemy_b95f_collide(Wram* w, const Rom* rom, uint16_t dp, uint16_t arg,
                        ActorHandlerRegs* r);

// --- $82:EFF0  enemy_eff0_collide — record 39 -------------------------------
//
// The family again, with the health at `$32` and the parked id at `$34`. It has
// **no death test at all**: `SBC $818561,X : STA $32 : JML $81:8506`, so the
// health is stored however far below zero it goes, and the body decides. `$5E`
// and `$5D` go to the bubble and the freeze. `$70` is a coin toss on bit 0 of
// the scheduler tick, clear answering as `$5D` and set as `$5C`.
#define ENEMY_EFF0_COLLIDE_ENTRY 0x82eff0u
#define EFF0_DP_HEALTH 0x32
#define EFF0_DP_HIT_ID 0x34
#define EFF0_ID_TOSS 0x0070
bool enemy_eff0_collide(Wram* w, const Rom* rom, uint16_t dp, uint16_t arg,
                        ActorHandlerRegs* r);

// --- $81:C8C3  actor_c8c3_collide — records 20 and 47 ------------------------
//
// It answers the two players by raw id, `$05` and `$06`, and every shot from
// `$5C` up by the same `JSR $C6EC`, which is `LDA #$C6F2 : STA $0A : RTS`:
// the address of the body's next routine. `$5E` and `$5D` go to the bubble
// and the freeze, `$61` is `DEC $22`, and `$68` counts a tally at `$1FC4` for
// the shooter's score slot and runs `$81:C6A7`, which turns it to face the
// shot: bit 1 of its record's flags, `ACTOR_FLIP`'s mirror in X, is set when
// the other record's X is left of `$0C` and cleared when it is not.
#define ACTOR_C8C3_COLLIDE_ENTRY 0x81c8c3u
#define C8C3_ID_P1 0x0005
#define C8C3_ID_P2 0x0006
#define C8C3_ID_61 0x0061
#define C8C3_ID_68 0x0068
#define C8C3_DP_NEXT 0x0a      // `$81:C6EC` and `$81:C6A7` both store here
#define C8C3_DP_COUNT_22 0x22  // `DEC $22` on id `$61`
#define C8C3_DP_X 0x0c         // what `$81:C6A7` compares the other's X with
#define C8C3_NEXT_TOUCHED 0xc6f2
#define C8C3_NEXT_68 0xc6ca
#define W_C8C3_TALLY 0x1fc4    // two words, indexed by score slot
#define C8C3_FLAG_MIRROR 0x0002
bool actor_c8c3_collide(Wram* w, uint16_t dp, uint16_t arg, ActorHandlerRegs* r);

// ---------------------------------------------------------------------------
// $81:D301  enemy_d301_collide — level 9's, and the copy whose counter has a
//                               reader
// ---------------------------------------------------------------------------
//
// The eighth copy of `$81:8888`, and the one that answers the question four of
// the others left open.
//
// `$81:B41C`, `$81:D7F6`, `$81:9B6B` and `$81:9063` all end a death with a bare
// decrement of some word on their own page instead of paying an award, and every
// one of those words was named for where it lives — `B41C_DP_COUNTER_0A`,
// `D9B6B_DP_COUNTER_26`, `D9063_DP_COUNTER_2E` — because nothing in reach reads
// them. **Here the reader is eleven instructions away and unambiguous**, and what
// it turns out to be is not a count at all:
//
//     $81:D2AD  LDA $0C : BEQ <loop top>    ; zero: keep running
//               BPL $D2CE                   ; positive: leave, quietly
//               ; negative: LDX #$0200 : LDA $36 : BEQ +$14 : JSL score_add
//               ;           INC $1F84 : ... : JSL $81:8191
//     $81:D2CE  ; ...both endings free the slot
//
// So `$0C` is a **three-way verdict word** that the handler writes and the body
// reads on its next pass: nought means carry on, positive means stop, negative
// means die — and the award this family "does not pay" is paid, `$0200` of it,
// by the actor's own main loop out of the id the handler parked. The death is
// still a `DEC` in the handler; what the `DEC` is *for* is a message.
//
// **That is evidence about the other four and not proof**, and the distinction is
// the same one `enemy_d7f6_collide` drew about `$0A`: those are different words on
// different pages with different writers, and one of them (`$81:B41C`'s) has a
// second writer this one does not. What has changed is that the shape now has one
// worked example instead of none.
//
// Two more things are worth knowing before reading the code.
//
// **It is immune to the ordinary weapon.** After masking, `CMP #$005C : BEQ` sends
// the player's basic shot — the id that is *every* hit in the corpus — straight to
// a `CLC : RTL`. No other copy does that; in all seven of the others `$5C` is the
// index-zero entry of `ENEMY_DAMAGE_TABLE` and costs a point. It parks the id at
// `$36` on the way past, so the shot is noticed and then ignored.
//
// **And `$FF` is how it is told to stop.** `CMP #$00FF : BEQ` is the *first*
// instruction, ahead of the family's `CMP #$005C`, and what it does is `INC $0C`
// — the positive verdict, the quiet exit. That is the same id `actor_deeb_collide`
// latches on and the same one `victim_a264_collide` calls `A264_ID_GIVE_UP_FF`, so
// three unrelated actors read `$FF` as "you are done here". **All three calls any
// input in the corpus makes to this handler are that one id**, which is the honest
// counterweight to everything above: one branch of eight is diffed and seven are
// transcribed.
#define ENEMY_D301_COLLIDE_ENTRY 0x81d301u

// The id that means "stop", tested ahead of everything else and answered with
// the positive verdict. `DEEB_ID_STOP` and `A264_ID_GIVE_UP_FF` are the same
// number reached by two other actors, which is three readings of `$FF` that agree
// and no routine anywhere that produces it yet.
#define D301_ID_STOP 0x00ff

// The verdict word above. Seeded to zero at `$81:D269`, and the handler is its
// only other writer in the bank.
#define D301_DP_VERDICT 0x0c
// Health, seeded to 25 at `$81:D262` — the same order as the boss's 70 and an
// order above an enemy's 0.
#define D301_DP_HEALTH 0x0e
// Where the raw id is parked, sign bit and all, and it *is* read: `$81:D2B6  LDA
// $36 : BEQ` skips the award when it is zero, exactly as `$81:BBEB` and the boss
// do. The main loop clears it every pass (`$81:D29C  STZ $36`).
#define D301_DP_HIT_ID 0x36
// Its own position, and the source of the four pairs the reseed below writes.
#define D301_DP_X 0x10
#define D301_DP_Y 0x12
// The four (x, y) pairs `$81:D210` seeds from that position when the actor is
// built, and that the reseed puts back. `$81:D028` walks such slots through
// `$18`, which `$81:D0F6` steps by four and wraps at `$20` — eight of them,
// `$1A` to `$36`.
//
// **The eighth is `D301_DP_HIT_ID`**, and this header does not claim to know
// whether that is deliberate. What is certain is that both are written: the main
// loop clears `$36` every pass and the handler parks an id there, while
// `$81:D041  STA $1A,X` writes it as a trail slot on the pass where `$18` has
// come round to `$1C`. Only the first four are named here, because only the first
// four are what this routine touches.
#define D301_DP_TRAIL 0x1a
#define D301_DP_TRAIL_STRIDE 4
#define D301_DP_TRAIL_RESEED 4  /* how many of the eight the handler rewrites */

// --- the survivor's tail, which is the only place a handler draws a number ----
//
// A hit it lives through leaves through `enemy_survived_react` — by `JSL` here,
// where every other copy uses `JML`, so there is a routine left to run when it
// comes back — and then rolls `rng_next` and acts on a 25-in-256 chance:
//
//     JSL $80:9D39 : CMP #$0019 : BCS <SEC : RTL>
//     JSR $81:D142                       ; $14 = $D148, its next routine
//     LDA $10 : STA $1A : STA $1E : STA $22 : STA $26
//     LDA $12 : STA $1C : STA $20 : STA $24 : STA $28
//
// `$81:D142` is three instructions and is inlined below for `$81:B168`'s reason.
// What it installs is the routine the body calls through `$14` on its next pass,
// and `$81:D148` opens with **the same `CMP #$0019` against a fresh draw** — so
// being hurt puts this creature into a state that keeps re-rolling.
//
// The draw is the reason `rng_next` had to be ported to port this handler at all,
// and the reason its carry input is not a detail: the two ways out of
// `enemy_survived_react` return *different* carry — set from the splice, clear
// from the already-flashing guard — and that flag is the bit the generator's `ROL`
// shifts in.
#define D301_NEXT_ROUTINE 0x0014   /* where `$81:D142` writes... */
#define D301_NEXT_ROUTINE_HURT 0xd148  /* ...this */
#define D301_RESEED_CHANCE 0x0019  /* `CMP #$0019 : BCS` — 25 draws in 256 */

// The handler. False on id `$5D`, which `JML`s to `$81:847E` like the rest of the
// family; every other id is answered here.
bool enemy_d301_collide(Wram* w, const Rom* rom, uint16_t dp, uint16_t arg,
                        ActorHandlerRegs* r, uint32_t* unported);

// --- Pricing the dispatch ----------------------------------------------------
//
// `$80:8480` again, and it is at the end of the header rather than up with its
// own section because what it costs is its own four blocks *plus whatever the
// handler cost*, so the struct cannot be written until every handler's work
// struct exists. That is the same reason `SpriteBuildWork` sits below the parts
// it aggregates in `port/oam.h`.
//
// The dispatcher is the one routine in the project that cannot be priced whole:
// it enters twenty-five different handlers and the general case is every
// behaviour in the game. So the model prices the frame — which is constant, and
// whose no-handler path is exactly the 140 cycles `verify` measures as this
// routine's minimum — and adds the handler's own cost when the handler is one
// that has a table. It declines otherwise, and the set it declines on shrinks by
// a handler at a time.
typedef enum {
  THREAD_CALL_BLK_NONE,    // $80:8486 BEQ taken: no handler registered
  THREAD_CALL_BLK_ENTER,   // ...not taken: build the frame, swap D, RTL in
  THREAD_CALL_BLK_RESUME,  // $80:84A6 BCC taken: the handler left carry clear
  THREAD_CALL_BLK_PARK,    // ...not taken, so $80:84A8 parks the thread
  THREAD_CALL_BLOCK_COUNT,
} ThreadCallBlock;

typedef struct {
  uint16_t blocks[THREAD_CALL_BLOCK_COUNT];
  // The handler `$80:849E` dispatched to, or 0 when none was registered. The
  // cost model switches on it, and an address it does not know is what makes
  // the call unpriceable rather than priced short.
  uint32_t entry;
  // The page `$80:84A2  TCD` installed. Its low byte is the whole reason
  // `CosimRun` has a third column: the table at `$80:82DE` tiles
  // `$7E:0100-$7E:0CFF` at stride `$80`, so twelve of the twenty-four threads
  // run their handler with `D` unaligned and pay an idle per direct-page
  // instruction.
  uint16_t dp;
  // Valid only when `entry` names the matching handler; the rest are untouched.
  ShotCollideWork shot;
  PlayerCollideWork player;
  EnemyCollideWork enemy;
  BossCollideWork boss;
  // Both copies of `$81:C4A6` count into this one, because both are priced by
  // one table — `MonsterCollideBlock` says why.
  MonsterCollideWork monster;
  B41cCollideWork b41c;
} ThreadCallWork;

bool thread_call_handler_counted(Wram* w, const Rom* rom, uint16_t slot,
                                 uint16_t arg, bool carry_in,
                                 ThreadCallResult* out, ThreadCallWork* work);

#endif
