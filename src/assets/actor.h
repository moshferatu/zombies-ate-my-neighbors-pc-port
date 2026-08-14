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
// exceeds `$1D50`, and `$1D50` is a fixed `$0010` set at every level load
// (`$80:85E7`). So the list runs while `0 < index <= 16`.
//
// **The record just past the last victim is not padding**, which this file used
// to say. The loader hands `+$1E` to *two* readers, and they stop on different
// fields: `$80:87A8` gives it to `$82:DB46`, which gates on +6 as above, and
// `$80:8791` gives it to `$81:81F6`, which walks the same twelve-byte stride
// (`$81:8223  TXA : ASL : ASL : STA $0A : ASL : CLC : ADC $0A`) and quits only
// when +0 is zero (`$81:822D  LDA ($0C),Y : BEQ $81FD`). `$81:81F6` is the one
// that spawns +$8. So everything between the two terminators is a placement that
// spawns and is not a neighbour, and the terminator `$82:DB46` sees is the end
// of the count, not the end of the list.
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
