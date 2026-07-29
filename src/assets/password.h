// The password system — how four letters name a level.
//
// ZAMN has no SRAM (`PLAN.md`), so a password is the whole of what the game
// remembers about you. There are 130 of them and every one is derived from four
// tables in bank $82, so nothing here is transcribed: change the tables in a ROM
// hack and this changes with them.
//
// **The format.** Four characters from a 21-letter alphabet — the consonants,
// `$82:B178` — chosen on a 7x5 grid that also offers the digits, a backspace and
// an enter. No password uses a digit. The four are stored in entry order and
// then *scrambled*: `$82:B02D` swaps characters 0 and 2, so the routine's two
// 16-bit comparands are `(c2, c1)` and `(c0, c3)`.
//
//   * `(c2, c1)` selects one of **13 groups** (`$82:B14A`, thirteen index pairs).
//     Group `i` is level `5 + 4i`, which is why the game gives you a password
//     every fourth level and why the reachable set is 5, 9, 13 … 53.
//   * `(c0, c3)` selects one of **10 variants** (`$82:B164`, ten index pairs,
//     offset per group by `$82:B18D`). Variant `j` means `j + 1`, and that goes
//     to `$7E:1D50`/`$7E:1D52` — the victim gate. So the second half of a
//     password is **how many neighbours are still out there**, and only the
//     tenth variant of each group is a level as it starts: `$82:B0BE  CMP
//     #$000A : BNE : LDA #$0010` turns ten into the sixteen a fresh level load
//     writes.
//
// The generator at `$82:B0DE` is the same tables read the other way, with the
// variant coming from `$1F9C + $1F9E - 1` in BCD — the two players' rescue
// counts added together.
//
// One password is not in the tables at all. `$82:B018` opens by comparing both
// words against `$4342`/`$4644` — "BCDF" as typed — and answers by storing
// **zero** into `$1E7C` rather than a level. Everything else stores 5 + 4i.
//
// Port code: libc only.

#ifndef ASSETS_PASSWORD_H
#define ASSETS_PASSWORD_H

#include <stdbool.h>
#include <stdint.h>

#include "assets/rom.h"

// `$82:B178`, and the alphabet a password is spelled in.
#define PASSWORD_ALPHABET 0x82b178u
#define PASSWORD_ALPHABET_SIZE 21
// `$82:B14A` — 13 pairs of alphabet indices, one per four levels.
#define PASSWORD_GROUP_TABLE 0x82b14au
#define PASSWORD_GROUPS 13
// `$82:B164` — 10 pairs, and `$82:B18D` — one pair of per-group offsets added to
// them before the alphabet lookup, which is what stops the ten variants of one
// group spelling the same four letters as another's.
#define PASSWORD_VARIANT_TABLE 0x82b164u
#define PASSWORD_VARIANTS 10
#define PASSWORD_GROUP_OFFSETS 0x82b18du

// `$82:B040  LDA #$0005` and the four `INC`s under it.
#define PASSWORD_FIRST_LEVEL 5
#define PASSWORD_LEVEL_STEP 4
// `$82:B0BE  CMP #$000A : BNE : LDA #$0010`.
#define PASSWORD_FULL_VARIANT 10
#define PASSWORD_FULL_VICTIMS 0x0010

// The one password that is code rather than data (`$82:B018`), and what it
// stores where a level number goes.
#define PASSWORD_CHEAT "BCDF"
#define PASSWORD_CHEAT_LEVEL 0

// Spell the password for group `group` (0..12) and variant `variant` (0..9)
// into `out`, which must hold five bytes. Returns false if either index is out
// of range or the tables point outside the ROM.
bool password_spell(const Rom* rom, int group, int variant, char out[5]);

// The level and victim gate that a group/variant pair means — the two things
// `$82:B018` writes when it accepts one.
int password_level(int group);
int password_victims(int variant);

// Read four characters back to a group and variant, the way `$82:B018` does.
// Returns false if they are not a password at all. `*group` is -1 and `*variant`
// is -1 for `PASSWORD_CHEAT`, which is neither.
bool password_read(const Rom* rom, const char in[4], int* group, int* variant);

#endif
