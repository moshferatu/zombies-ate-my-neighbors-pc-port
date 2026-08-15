// The per-level placement lists — where the enemies, victims and objects of a
// level are put, and what code drives each one.
//
// Three of the level record's bank-$9F pointers (`docs/asset-formats.md`) name
// these lists; the loader at `$80:86A2` hands each one to a dedicated thread:
//
//   * `+$1C` -> `$81:80EC`  the **actor list**  (enemies and the like)
//   * `+$1E` -> `$82:DB46`  the **victim list** (the ten people you rescue)
//   * `+$20` -> `$80:C9A5`  the **object list** (doors, warps, item spawns)
//
// Each list is an array of fixed-size records terminated by a zero key field,
// and each record carries a spawn position plus — for actors and victims — a
// far pointer to the routine that runs it. The record layouts here were read
// straight out of those three parsers; see `docs/asset-formats.md` for the
// byte-level tables and how `zamn_assets verify-actors` checks them.
//
// Port code: libc only.

#ifndef ASSETS_ACTOR_H
#define ASSETS_ACTOR_H

#include <stdbool.h>
#include <stdint.h>

#include "assets/level.h"
#include "assets/rom.h"

// The lists live between the level records in bank $9F. No level comes close to
// these counts; they only bound the parse so a malformed record can't run away.
#define ACTOR_LIST_MAX 128
#define VICTIM_LIST_MAX 32
#define SPAWN_LIST_MAX 32
#define OBJECT_LIST_MAX 128

// A 10-byte actor record, as `$81:80EC` reads it (stride 10, `LDA ($0C),Y`):
// +0 a type byte (0 ends the list), +1 spawn x, +3 spawn y, +5 a flags byte,
// +6/+8 a far pointer to the actor's behavior routine, +9 padding.
//
// **+0 is not a collision id**, which this file used to call it and
// `zamn_assets actors` used to print as one. An object's list byte *is* an index
// that yields a collision id, through `$80:CA30`, so the assumption was cheap to
// make and it is wrong for actors: `zamn_headless --records` shows the record at
// level 29's (290,1302) carrying `ACTOR_COLLIDE_ID` **$00** where the list says
// `$2D`, and level 46's monsters carrying `$03` and `$04`, neither of which
// appears anywhere in that level's actor list. An actor's collision id is
// written by its own body, not by its placement.
//
// The correction cost something: fourteen placements across the game carry type
// `$2D`, and for an afternoon they looked like the way to reach `$80:FA26` —
// the one jump-table entry with no input. They are not.
typedef struct {
  uint8_t type;       // +0  actor type; 0 terminates the list
  uint16_t x, y;      // +1/+3  spawn position, in level pixels
  uint8_t flags;      // +5  per-placement flags (meaning not yet established)
  uint32_t behavior;  // +6/+8  24-bit far pointer to the behavior routine
} ActorPlacement;

// The victim parser (`$82:DB46`) stops at the first record whose index is 0 or
// exceeds `$1D50`. **`$1D50` is not a constant**, which this file used to say by
// pointing at `$80:85E7`; that store sits beside `STA $1E7C = 1` and is new-game
// init, run once. Every level after the first is seeded at `$80:866F`, on all
// three of the game loop's level-exit paths:
//
//     $80:867F  LDA $1F9C : SED : ADC $1F9E : STA $1D50 : STA $1D52 : CLD
//
// — the two players' rescue counts for the level just finished, added in BCD.
// Per level, because `$80:8947` clears `$1F8A`..`$1FFB` from the top of the game
// loop (`$84C9`) one call before the load. A
// level places as many neighbours as you saved on the one before, which is why
// the index field is **BCD** and why every level's ten run 1..9 and then `$10`
// rather than `$0A`. A password writes the same pair outright (`$82:B0C6`), so
// its second half is a neighbour count: `VXBB` starts level 21 with `$0001`, and
// `$82:DB46` keeps exactly one victim of the ten in the record.
//
// So the list runs while `0 < index <= $1D50`, and the shipped indices are
// chosen so that a full ten (`$0010`) keeps all of them.
//
// **The record just past the last victim is not padding**, which this file used
// to say. The loader hands `+$1E` to *two* readers, and they stop on different
// fields: `$80:87A8` gives it to `$82:DB46`, which gates on +6 as above, and
// `$80:8791` gives it to `$81:81F6`, which walks the same twelve-byte stride
// (`$81:8223  TXA : ASL : ASL : STA $0A : ASL : CLC : ADC $0A`) and reads +0
// instead. `$81:81F6` is the one that spawns +$8. So everything between the two
// terminators is a placement that spawns and is not a neighbour, and the
// terminator `$82:DB46` sees is the end of the count, not the end of the list.
//
// **`$81:81F6` does not stop at its terminator, it wraps at it.** `$81:822D
// LDA ($0C),Y : BEQ $81FD` branches to `$81:81FD  STZ $10`, which resets the
// index and scans the list again, three frames at a time, forever. A zero +0 is
// the end of the array; it is not the end of the walk. What decides whether an
// entry spawns on a given pass is the camera — `$81:8221` skips it unless the
// screen centre is within `$A0` of it in both axes — and `$7E:605A,X`, one state
// byte per entry: `$00` idle, `$01` live with the thread handle at `$7E:609A,X`,
// `$80` retired. Leaving the box kills the thread and puts the entry back to
// `$00` (`$81:826B`), so **an entry the level does not retire respawns every
// time you walk back to it**. It is also where a victim above the gate goes: the
// walker reads `+$6` too (`$81:81C4  CMP $001D50 : BCS $81EF`) and strikes an
// out-of-gate placement off instead of spawning it, which is how a level entered
// with three neighbours left disposes of the other seven — one at a time, as the
// camera reaches each. Retiring is the body's own doing: `$81:8191` sets
// `$80`, and sixteen routines call it — the neighbours of bank `$83` when they
// are rescued or eaten, and `$81:983A` at `$81:9854`, before it has even
// installed its handler. The `$81:990B` creature is one encounter per level
// load. This is why the tail is worth decoding rather than skipping: it is where
// the levels put the population that comes back.
//
// The record layout is the same either way, which is what makes one loop over
// both halves correct; only the terminator each reader honours differs.
//
// Every one of the 56 records holds exactly ten victims; 28 of them carry a tail
// as well, 135 placements over ten bodies. `$81:983A` — the `$81:990B` creature
// — is thirty of those and appears in no other list, which is how
// `docs/cosim.md` came to ask this question.
#define VICTIM_INDEX_MAX 0x10

// A 12-byte victim record, as `$82:DB46` reads it (stride 12): +0 x, +2 y,
// +4 a word that is always zero so far, +6 the victim index (0 ends the list),
// +8/+10 a far pointer to the victim's routine, +11 padding. The parser only
// copies +0/+2 into its working arrays and gates the list on +6.
typedef struct {
  uint16_t x, y;      // +0/+2  spawn position
  uint16_t field4;    // +4  always $0000 in the shipped data
  uint16_t index;     // +6  1..N; 0 terminates the list
  uint32_t behavior;  // +8/+10  24-bit far pointer to the victim routine
} VictimPlacement;

// The tail of the `+$1E` list: same twelve bytes, read by `$81:81F6` alone. +6
// is zero — that is what ended the count — so only the position and the far
// pointer mean anything, and the pointer is an actor body, not a victim routine.
typedef struct {
  uint16_t x, y;      // +0/+2  spawn position
  uint16_t field4;    // +4  always $0000 in the shipped data
  uint32_t behavior;  // +8/+10  24-bit far pointer to the actor body
} SpawnPlacement;

// A 5-byte object record, as `$80:C9A5` reads it: +0 x, +2 y, +4 a type byte.
// +0 == 0 terminates the list. No behavior pointer — the type byte selects it.
typedef struct {
  uint16_t x, y;  // +0/+2  position
  uint8_t type;   // +4  object type
} ObjectPlacement;

typedef struct {
  ActorPlacement actors[ACTOR_LIST_MAX];
  int actor_count;
  VictimPlacement victims[VICTIM_LIST_MAX];
  int victim_count;
  SpawnPlacement spawns[SPAWN_LIST_MAX];
  int spawn_count;
  ObjectPlacement objects[OBJECT_LIST_MAX];
  int object_count;
} ActorLists;

typedef enum {
  ACTOR_OK = 0,
  ACTOR_ERR_ADDRESS = -1,  // a list pointer does not point at readable ROM
  ACTOR_ERR_OVERFLOW = -2,  // a list ran past its cap without terminating
} ActorStatus;

// Parse all three of `h`'s placement lists out of the ROM. Every list is
// optional: a level with a null (`$0000`) pointer for one simply gets a count
// of zero for it. Returns ACTOR_OK, or the first error encountered.
int actors_read(const Rom* rom, const LevelHeader* h, ActorLists* out);

#endif
