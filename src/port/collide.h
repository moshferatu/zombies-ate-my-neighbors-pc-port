// The door into actor behaviour: the callback dispatcher, and every handler a
// collision on the current movies reaches.
//
// `port/oam.h` ends at `actor_collide_notify` (`$80:BE8F`), which hands a
// touching pair to `$80:8480` twice — once per actor. That was once where the
// port stopped, and `zamn_cosim` measured the size of the hole rather than
// filling it: on `movies/level1-rescue.zmv` **1,226 of 1,226** collisions enter
// a handler. The census then named the handlers one at a time, and this is all
// of them, plus the dispatcher that enters them.
//
//   $80:8480  thread_call_handler  build the frame, swap direct page, RTL in
//   $80:F7F7  player_collide       the player's; jump-tables on the other's id
//   $80:F950  player_collide_hurt  the entry 1,225 of 1,225 of them land on
//   $81:8888  enemy_collide        an enemy's; the mirror image of the player's
//   $81:FE0E  shot_collide         a weapon shot's; four ids stop it, the rest
//                                  it flies through
//   $83:A364  victim_collide       a victim's; eight ids, eight endings, latched
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
#define ENEMY_SPECIAL_A_ENTRY 0x81847eu
#define ENEMY_SPECIAL_B_ENTRY 0x8183c6u

// The far address `$81:8539  LDA #$8542 : DEC A` writes into the gap, and the
// bank `$81:8532  LDA #$0081 : XBA` writes above it. One less than the routine
// it wants, because what resumes the thread is an `RTL`.
#define ENEMY_REACT_RETURN 0x8541
#define ENEMY_REACT_BANK 0x81
// How far the parked stack pointer moves, which is also how many bytes the
// three words below it slide.
#define ENEMY_REACT_FRAME 3

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

// `$81:BAB3`, split out for the same reason `enemy_survived_react` is: it has
// two coverage sites of its own and one of them is an entry guard no diff can
// check. Always true — there is nothing in it to decline.
bool monster_survived_react(Wram* w, uint16_t dp, ActorHandlerRegs* r);

#endif
