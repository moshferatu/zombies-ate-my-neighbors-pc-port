// The player's weapon selection — `$80:EA63` and the small routine it ends
// with.
//
// This is the first ported code that is not on the collision path and not
// per-frame housekeeping: it is a player *doing* something. It arrived here by
// the census, which is the point of having one. `$80:F87B` — a pickup — ends
// with a tail call into `$80:EA63` when the player is holding nothing, that
// decline was the last address left on the list, and following it turned up a
// routine with a second caller that has nothing to do with pickups at all.
//
// **That second caller is a correction to something PROGRESS.md records.** An
// earlier round established that `Y` is the fire button and that `B` "does
// nothing", because holding B for 120 frames moved no counter. `B` does do
// something: `$80:D259  LDA $1A : AND #$8000 : ... : JSR $EA63` is the player's
// input handler cycling to the next weapon. It looked like nothing because a
// player carrying one weapon cycles to the one they are already holding, and
// the routine's first exit is `CMP $1CBC,X : BEQ` — no store, no sound.
//
// The inventory it searches is `W_PLAYER_INVENTORY`, fourteen BCD counters per
// player, and the same array `$80:F87B` adds to. So the two halves of "picking
// something up" — the counter and the selection — are both here and both
// diffed.
//
// Port code: libc only.

#ifndef PORT_PLAYER_H
#define PORT_PLAYER_H

#include <stdbool.h>
#include <stdint.h>

#include "assets/rom.h"
#include "port/wram.h"

// --- $80:EA63 ---------------------------------------------------------------

#define WEAPON_SELECT_ENTRY 0x80ea63u

// Two words in ROM — `$1CCC` and `$1CEC` — indexed by a player number already
// doubled. This is the evidence for `W_PLAYER_INVENTORY`'s base and stride, and
// for what `ACTOR_DP_INVENTORY` holds on the player's page.
#define PLAYER_INVENTORY_BASES 0x80eaa4u

// `$80:EA7E  CPY #$001C` — the walk wraps here, so the inventory is 14 words.
#define WEAPON_SCAN_WRAP 0x001c
// `$80:EA63  LDA #$000F`. Fifteen tries at fourteen slots, which is what lets
// the search start anywhere and still come all the way back round.
#define WEAPON_SCAN_TRIES 0x000f
// What `$80:EA8A` settles on when every slot is empty, and what
// `W_PLAYER_WEAPON` holds when the player has nothing selected. Negative, which
// is what both `$80:EA72  BMI` and `$80:F8A6  BPL` test for.
#define WEAPON_NONE 0xffff
// `$80:EA9C  LDA #$0012` — the sound a weapon change makes.
#define WEAPON_SFX_SWITCH 0x0012

// --- $80:EAA8 ---------------------------------------------------------------
//
// The *item* selector, and `$80:EA63` written out a second time with three
// numbers changed. Where the weapon search walks fourteen slots with fifteen
// tries and ends by looking the new weapon's data up and redrawing the player,
// this one walks twelve with thirteen and ends at the store — an item has no
// data table and does not change what you look like. It plays the same sound.
//
// Four callers, which is why it is registered in its own right: `$80:D278` (the
// player's input handler, on **A**), `$80:EB60`, `$80:EE7E`, and `$80:F903`,
// which is `item_pickup`'s tail call — the same shape as `$80:F8A8`'s into the
// weapon selector.

#define ITEM_SELECT_ENTRY 0x80eaa8u

// `$80:EABE  CPY #$0018` — twelve words, two fewer than the weapons.
#define ITEM_SCAN_WRAP 0x0018
// `$80:EAA8  LDA #$000D`. Thirteen tries at twelve slots, one spare, exactly as
// the weapon search has one spare at fourteen.
#define ITEM_SCAN_TRIES 0x000d
// What `$80:EACA` settles on when every slot is empty, and what `W_PLAYER_ITEM`
// holds when nothing is selected. `$80:EAB2  BMI` and `$80:F901  BPL` are the
// two tests for it.
#define ITEM_NONE 0xffff
// `$80:EAD9  LDA #$0012` — and it is the same sound a weapon change makes.
#define ITEM_SFX_SWITCH 0x0012

// --- $80:EA4B ---------------------------------------------------------------

// Two words in ROM, one per player, each pointing at a per-weapon table that
// `$80:EA4B` reads one entry out of. Both targets are in bank $80 above $8000,
// so they are ROM — unlike the inventory pointers above, which are the WRAM
// mirror, and the two are read through the identical `LDA ($2C),Y`.
#define WEAPON_DATA_TABLES 0x80fd9cu
// `$80:EA5C  LDA #$0090 : STA $000A,Y` — the bank half of the player's
// metasprite pointer, in the display record. Changing weapon changes what the
// player is drawn as, and this is the half of that the routine writes.
#define WEAPON_META_BANK 0x0090

// --- The player's direct page ------------------------------------------------
//
// Offsets from `D`, like the fields in `port/collide.h`, and on the same page
// those describe. `ACTOR_DP_PLAYER`, `ACTOR_DP_RECORD` and
// `ACTOR_DP_INVENTORY` are named there; these three are the rest of what these
// two routines touch.

// Scratch, and scratch in the strictest sense: `$80:EA6A` parks the inventory
// base here and `$80:EA51` overwrites it with a weapon-data pointer a few
// instructions later, in the same call. Both are consumed by the very next
// `LDA ($2C),Y` and neither survives the routine. It is named because the diff
// compares it, not because anything reads it.
#define PLAYER_DP_PTR 0x2c
// The base of this player's `W_PLAYER_ITEMS` array, and the exact counterpart of
// `ACTOR_DP_INVENTORY`: `$80:D1C6`/`$80:D1CB` fill the two from two tables one
// after the other. `$80:F8E5  ADC $66` uses it as an address and `$80:EAB8  LDA
// ($66),Y` uses the same word as a pointer, which is the pair of instructions
// that says it is a base rather than an index.
#define PLAYER_DP_ITEMS 0x66
// The search's countdown, `$80:EA86  DEC $2E`. Left at whatever the walk
// stopped on, which is how many tries were unused.
#define PLAYER_DP_SCAN 0x2e
// The index `$80:EA4F  LDX $0C` reads `WEAPON_DATA_TABLES` with. It has to be a
// player number already doubled for the table to have two entries, and the
// player's number is also at `ACTOR_DP_PLAYER` — so either the page holds it
// twice or one of the two is something else that happens to agree. No input has
// produced a disagreement, so this is named for where it is rather than for
// what it means, the same treatment `ACTOR_DP_SCRATCH_7E` gets.
#define PLAYER_DP_TABLE_INDEX 0x0c
// What `$80:EA58` files for the newly selected weapon. A word out of that
// player's weapon-data table, written and not read again by anything traced —
// the player's own per-frame code is what picks it up. On an *enemy's* page the
// same offset is `ACTOR_DP_DEATH_REQ`, which is the third time two pages have
// disagreed about an offset and will not be the last.
#define PLAYER_DP_WEAPON_DATA 0x12

// What the routine leaves in the caller's registers, in the same shape
// `port/collide.h` uses.
typedef struct {
  uint16_t a, x, y;
  bool n, z, c;
} WeaponSelectRegs;

// Select the player's next non-empty inventory slot.
//
// `dp` is the player's direct page. Two exits, and they could hardly be less
// alike: the search settling on the weapon already held is a `CMP` and an
// `RTS`, and anything else stores the new weapon, looks its data up and plays a
// sound — so what comes back in the registers is `apu_play_sfx`'s, flags
// included.
//
// Never declines. There is no branch of either routine that is not here.
void weapon_select_next(Wram* w, const Rom* rom, uint16_t dp,
                        WeaponSelectRegs* out);

// Select the player's next non-empty *item* slot. Same shape as the weapon
// search above and the same two exits, minus the weapon-data lookup — so the
// changed exit's registers are `apu_play_sfx`'s and the unchanged one's are the
// `CMP`'s.
//
// Never declines.
void item_select_next(Wram* w, const Rom* rom, uint16_t dp,
                      WeaponSelectRegs* out);

#endif
