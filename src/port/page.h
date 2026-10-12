// A player's page, begun.
//
// `$80:D13A` is called once by a player's thread, before its loop. It sets
// the page up: who the player is, their two records, where they stand, the
// way they face. This is its first stretch, as far as the `JSL` at
// `$80:D16A` that asks for the first record:
//
//   * a player's weight, `$1C`, onto the census at `$7E:00DE`;
//   * the page cleared from `$0A` up, which is nearly all the stretch costs;
//   * `$0C`, the side that has the slot at `$04`, and that slot's half of
//     the status panel turned on;
//   * `$0E`, the word at `$06` doubled, and Y the same.
//
// ## The two records
//
// The second stretch is from `$80:D174`, where the second record it asked
// for comes back in A, to the `JSR` at `$80:D1D8` that begins the standing
// pose:
//
//   * the record is the player's, `$08`, and the one asked for first is the
//     hand weapon's, `$0A`. `$80:A8A4` files the player's at `$7E:00D2`, and
//     for the first player the thread too;
//   * the player's stands where the page was told, `$00` and `$02`, which
//     are kept at `$30` and `$32`, and it is drawn. Both records are the
//     thread's, and each has the collide id of its side or its player from
//     `$80:FE64`. `$80:EA42` then takes the weapon's away again;
//   * the player faces down, `$26`, and `$64` and `$66` are their weapons
//     and their items;
//   * `$80:EA4B`: with a weapon chosen, `$12` is its pictures for this
//     side, and the weapon's record is in bank `$90`.
//
// What is between the two stretches is the second `JSL $80BE0C` and the
// store of what the first answered. After the second the pose begins and
// the thread says what handles it, each an entry of its own.
//
// Port code: libc only.

#ifndef PORT_PAGE_H
#define PORT_PAGE_H

#include <stdbool.h>
#include <stdint.h>

#include "assets/rom.h"
#include "port/cpu.h"
#include "port/oam.h"
#include "port/wram.h"

#define PAGE_BEGIN_PC 0x80d13au
#define PAGE_BEGIN_ALLOC_PC 0x80d16au  // `JSL $80BE0C`

#define PAGE_PLAYER_WEIGHT 0x001c
#define PAGE_CLEAR_FROM 0x0a
#define PAGE_CLEAR_TO 0x7e  // the last word cleared
#define PAGE_DP_SLOT 0x04
#define PAGE_DP_PLAYER_IN 0x06
#define PAGE_DP_SIDE 0x0c
#define PAGE_DP_PLAYER 0x0e
#define PAGE_SLOT_COUNT 2

enum {
  PG_HEAD,   // $D13A-$D146
  PG_CLEAR,  // STZ $00,X : DEX : DEX : CPX #$0009 : BCS, once a word
  PG_TAIL,   // $D150-$D169
  PG_TAKEN,
  PG_BLOCK_COUNT
};

typedef struct {
  uint16_t blocks[PG_BLOCK_COUNT];
} PageWork;

void player_page_begin(Wram* w, PortCpu* c, PageWork* k);

#define PAGE_BANK 0x80u
#define PAGE_RECORDS_PC 0x80d174u
#define PAGE_RECORDS_STAND_PC 0x80d1d8u  // `JSR $D4F4`
#define PAGE_FILE_AT 0xd178u             // its `JSL $80A8A4`
#define PAGE_UNARM_RET 0xd1cfu           // what `JSR $EA42` pushes
#define PAGE_ARM_RET 0xd1d7u             // ...and `JSR $EA4B`

#define PAGE_COLLIDE_IDS 0x80fe64u      // by side, and by player
#define PAGE_INVENTORIES 0x80d1e2u      // by player
#define PAGE_ITEMS 0x80d1e6u
#define PAGE_WEAPON_PICTURES 0x80fd9cu  // by side: a table by weapon

#define PAGE_DP_GIVEN_X 0x00
#define PAGE_DP_GIVEN_Y 0x02
#define PAGE_DP_RECORD 0x08
#define PAGE_DP_WEAPON_RECORD 0x0a
#define PAGE_DP_WEAPON_PICTURES 0x12
#define PAGE_DP_FACING 0x26
#define PAGE_DP_PICTURES_AT 0x2c  // scratch: the side's table
#define PAGE_DP_X 0x30
#define PAGE_DP_Y 0x32
#define PAGE_DP_INVENTORY 0x64
#define PAGE_DP_ITEMS 0x66
#define PAGE_DP_76 0x76
#define PAGE_FACING_DOWN 0x000a
#define PAGE_WEAPON_PICTURE_BANK 0x0090

typedef struct {
  bool first_player;  // the thread was filed too
  bool armed;         // a weapon is chosen
} PageRecords;

// False for a page the port will not follow: a side, a player or a record
// that is not one, or a weapon with no row in the table.
bool player_page_records_supported(const Wram* w, uint16_t page,
                                   uint16_t record);
// Entered with the player's record in A. It stops at
// `PAGE_RECORDS_STAND_PC`.
void player_page_records(Wram* w, const Rom* rom, PortCpu* c,
                         PageRecords* did);

#endif
