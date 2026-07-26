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
// Two more are unreached rather than unported: an enemy that *survives* a hit
// leaves through `$81:8506`, and two collision ids (`$81:83C6`, `$81:847E`) have
// routines of their own. No input has produced either, so both decline by name
// and wait for a movie rather than for code.
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

// The two sound effects those two entries play. Bare `LDA` operands, and the
// only thing `$80:F92D` does at all.
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
// Four of the 57 jump-table entries are ported: `$80:F87A`, a bare `RTS`;
// `$80:F950`, the hit path; `$80:F92D`, one sound effect and nothing else; and
// `$80:F87B`, a pickup, which twenty-one item ids share.
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
//     `$81:8506`, which is not ported. Declines.
//
// False for that last one, and for the two special ids above.
bool enemy_collide(Wram* w, const Rom* rom, uint16_t dp, uint16_t arg,
                   ActorHandlerRegs* r);

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
// and no third id does either. The third, `$0004`, is also one of the three a
// victim files under "nobody came" (`VICTIM_ID_EVENT_3_B`), so what it is is
// still open.
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

#endif
