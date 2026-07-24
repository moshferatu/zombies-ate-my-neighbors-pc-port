// $80:C7D9 — crediting a player with points.
//
// The collision path arrives here through a death: `$81:8727` puts the award in
// X and the collision id in A, and an enemy that has just run out of health
// calls it. It is not only the collision path's, though, and that is why it is
// registered as a routine in its own right rather than left inside
// `enemy_collide` — `$83:A1D5`, the victim-rescue thread, calls it too, from a
// completely unrelated part of the game. A routine checked on one of its two
// call sites is checked half as hard as one checked on both.
//
// Three things about it are worth knowing before reading the code.
//
// **The score is BCD.** `SED` is on for the whole addition, so the port has to
// do decimal arithmetic rather than binary. `score_bcd_add16()` below is the
// 65816's own algorithm written out, not a decode-add-reencode: the two agree on
// valid BCD and disagree on everything else, and the harness compares the answer
// byte for byte.
//
// **Which player gets the points is a lookup, not an index.** The caller does
// not name a player; it hands over a *side*, which is 0 or 2 depending on the
// sign bit of the collision id — so bit 15 of an enemy's collision argument says
// which player's weapon caused it, and bits 0-14 say what the weapon was. The
// side is then searched for in the two words at `$7E:1E84`, which say which side
// owns each of the two score slots. `$80:925D` seeds them 0 and 2, the identity;
// finding a side in neither is a real answer and means the points are dropped.
//
// **The addition is 32-bit but reached through a jump table.** The two score
// slots are laid out identically four bytes apart, but the ROM branches to two
// separate copies of the code rather than indexing, and the table it branches
// through is in ROM at `$80:C819`. This reads that table rather than
// transcribing its targets, for the reason `player_collide` does: a ROM hack
// that repoints it works, and an entry the port does not have declines by
// address instead of being silently mishandled.
//
// Port code: libc only.

#ifndef PORT_SCORE_H
#define PORT_SCORE_H

#include <stdbool.h>
#include <stdint.h>

#include "assets/rom.h"
#include "port/wram.h"

#define SCORE_ADD_ENTRY 0x80c7d9u

// 3 x u16 in ROM, indexed by the slot `$80:C7C2` answered with — already
// doubled, like every other index in this game. Every target is in bank $80,
// because `JMP ($C819,X)` is a same-bank indirect jump.
#define SCORE_ADD_TABLE 0x80c819u
#define SCORE_ADD_SLOT0 0xc7ebu    // add to the first score slot
#define SCORE_ADD_SLOT1 0xc801u    // ...and the second
#define SCORE_ADD_DISCARD 0xc817u  // `PLA : RTL`: nobody owns this side

// What `$80:C7C2` answers when the side belongs to neither slot. It is a valid
// index into the table above, which is the whole trick — "nobody" is an entry
// rather than a special case.
#define SCORE_SLOT_NONE 0x0004

// What the routine leaves behind. Y is not here because nothing between
// `$80:C7D9` and any of its three `RTL`s mentions it.
typedef struct {
  uint16_t a, x;
  bool n, z, c;
} ScoreResult;

// Credit `amount` — a BCD constant, `$0100` for a kill — to whichever player
// owns `side`.
//
// `second_side` is the sign bit of the collision id, which is what the `BMI` at
// the entry tests; the caller's N flag is genuinely an input here. `carry_in` is
// the caller's carry, which only the discard path passes through.
//
// False if the jump-table entry for the slot is not one of the three above,
// which a stock ROM cannot produce — see score.c.
bool score_add(Wram* w, const Rom* rom, bool second_side, uint16_t amount,
               bool carry_in, ScoreResult* out);

#endif
