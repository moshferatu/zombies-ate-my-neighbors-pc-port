// What a level leaves on the ground, and the neighbours' list.
//
// A level's pickups and keys are a list in WRAM, four arrays of thirty-five
// words: see `port/wram.h`. One thread, `$80:C8F6`, gives each a display
// record while the camera is near it and takes the record away when the
// camera has gone. Its loop is `object_spawner_body` in `port/bodies.h`.
// These are the four routines that loop calls:
//
//   $80:C9A5  object_list_parse  the level's list copied into the arrays
//   $80:C9E3  object_spawn       an entry given a record
//   $80:CAA8  object_free        ...and its record taken away
//   $80:CABF  object_collect     the ones picked up retired for good
//
// **The list** is five bytes an entry: where across, where down and a type,
// ended by a zero where across. The thread's first two words point at it.
// The parse also names the thread's handler, `$80:CAEE`, which is how
// whatever picks a thing up tells this thread about it.
//
// **A record** has the entry's place, the type's picture and collide id from
// two tables, and this thread as its owner. Its offset is kept in the
// entry's state word, which is how the loop knows the entry has one.
//
// **Picked up**, an entry's record is on the thread's page: the handler
// counts them at `$12` and leaves each above it. `object_collect` finds the
// entry that has the record, marks it `$8000` so it never comes back, and
// frees the record.
//
// Each record made adds one to the level's load at `$00DE`, which every
// spawner in the game asks about, and each freed takes one away.
//
// ## The neighbours' list
//
//   $82:DB46  victim_list_parse   where each neighbour of the level is
//   $81:817E  victim_tables_clear nobody started, and no thread for anybody
//   $81:81A2  victim_start        a neighbour's thread started
//
// The neighbours are on the same plan and a different list, twelve bytes an
// entry in bank `$9F`: see `victims_body` in `port/bodies.h`. The parse keeps
// where each is, for the radar, and stops at the first entry whose gate is
// past `$7E:1D50`: a level has only as many neighbours as are still alive.
// `victim_list_parse` ends by writing the colour window's registers, and
// those writes are the ROM's: the port stops at `$82:DB96`, before them.
//
// `victim_start` holds an entry against the same gate, and starts the thread
// the entry names with the entry's place and parameter as its first words.
// One past the gate is the ROM's.
//
// ## Their contract with the ROM
//
// WRAM as the ROM leaves it, and the registers. Overflow is claimed only
// where no call comes after the last sum.
//
// Port code: libc only.

#ifndef PORT_OBJECTS_H
#define PORT_OBJECTS_H

#include <stdbool.h>
#include <stdint.h>

#include "assets/rom.h"
#include "port/cpu.h"
#include "port/oam.h"
#include "port/wram.h"

#define OBJECT_LIST_PARSE_PC 0x80c9a5u
#define OBJECT_LIST_PARSE_RTS_PC 0x80c9e2u
#define OBJECT_SPAWN_PC 0x80c9e3u
#define OBJECT_SPAWN_RTS_PC 0x80ca2fu
#define OBJECT_FREE_PC 0x80caa8u
#define OBJECT_FREE_RTS_PC 0x80cabeu
#define OBJECT_COLLECT_PC 0x80cabfu
#define OBJECT_COLLECT_RTS_PC 0x80caedu

#define OBJECT_HANDLER 0xcaeeu
#define OBJECT_HANDLER_BANK 0x0080
#define OBJECT_PICTURES 0xca6cu     // by type, in the thread's data bank
#define OBJECT_COLLIDE_IDS 0xca30u
#define OBJECT_TYPE_END 0x003c      // thirty types, doubled
#define OBJECT_META_BANK 0x008f
#define OBJECT_ENTRY_BYTES 5
#define OBJECT_ENDED 0xc000u        // the state word past the last entry
#define OBJECT_RETIRED 0x8000u

#define OBJECT_DP_LIST 0x00       // a far pointer
#define OBJECT_DP_ENTRY 0x0c      // doubled
#define OBJECT_DP_COLLECTED 0x12  // bytes of records above it

#define OBJECT_COLLECT_MAX 8

typedef struct {
  bool declined;
  int entries;      // parsed
  uint16_t record;  // made
  int collected;
  int scanned[OBJECT_COLLECT_MAX];  // entries passed over before each
  int place[OBJECT_COLLECT_MAX];    // where in the display list each was
} ObjectsWork;

void object_list_parse(Wram* w, const Rom* rom, PortCpu* c, ObjectsWork* k);
void object_spawn(Wram* w, const Rom* rom, PortCpu* c, ObjectsWork* k);
void object_free(Wram* w, PortCpu* c, ObjectsWork* k);
void object_collect(Wram* w, PortCpu* c, ObjectsWork* k);

#define VICTIM_LIST_PARSE_PC 0x82db46u
#define VICTIM_LIST_PARSE_DONE_PC 0x82db96u  // `SEP #$20`, and the registers
#define VICTIM_TABLES_CLEAR_PC 0x81817eu
#define VICTIM_TABLES_CLEAR_RTL_PC 0x818190u
#define VICTIM_START_PC 0x8181a2u
#define VICTIM_START_RTS_PC 0x8181eeu

#define VICTIM_ENTRY_BYTES 12
#define VICTIM_ENTRY_PARAM 4
#define VICTIM_ENTRY_GATE 6
#define VICTIM_ENTRY_THREAD 8
#define VICTIM_ENTRY_BANK 10
#define VICTIM_PLACES_MAX 15  // four bytes each, up to the count
#define VICTIM_TABLE_BYTES 64
#define W_VICTIM_GATE 0x1d50u
#define W_VICTIM_WORD_A 0x1f98u
#define W_VICTIM_WORD_B 0x1f9au

#define VICTIM_DP_LIST 0x28  // the parse's far pointer
#define VICTIM_DP_ARG_X 0x00
#define VICTIM_DP_ARG_Y 0x02
#define VICTIM_DP_ARG_PARAM 0x04
#define VICTIM_DP_ARG_ENTRY 0x06
#define VICTIM_DP_SCRATCH 0x0a
#define VICTIM_DP_START_LIST 0x0c
#define VICTIM_DP_ENTRY 0x10
#define VICTIM_DP_X 0x16
#define VICTIM_DP_Y 0x18

typedef struct {
  bool declined;
  int entries;      // kept by the parse
  int equal;        // ...of which this many were at the level's gate
  bool ended_zero;  // the list ended at a zero gate...
  bool ended_past;  // ...or at one past the level's
  bool ungated;     // the start's entry had a negative gate...
  bool at_gate;     // ...or one equal to the level's
  int slot;         // the thread started, doubled, or -1
} VictimsWork;

void victim_list_parse(Wram* w, const Rom* rom, PortCpu* c, VictimsWork* k);
void victim_tables_clear(Wram* w, PortCpu* c);
void victim_start(Wram* w, const Rom* rom, PortCpu* c, VictimsWork* k);

#endif
