// Weapon 5's shot.
//
// `$81:EBE2` is a thread: the thing weapon 5 fires. Its start takes a
// record and puts it in front of the player, and then every frame it moves
// by its step and asks the ground about the point it has come to. Two of
// its stretches are here, and two of its start:
//
//   $81:ED1A  begin   its record, in front of whoever fired it
//   $81:EC79  aim     ...moved to the mouth of the weapon, and the picture
//                     for the way it goes
//   $81:EC03  frame   a frame of its flight, from where the sleep comes
//                     back to the next sleep, or to where it stops
//   $81:EC30  burst   stopped: its record put where it bursts, as far as
//                     the pictures it bursts in
//
// The rest of the start, and what it does to the tile it stops at, are the
// ROM's.
//
// **Where it starts depends on whose it is.** The thread is handed a side
// in `$06`, 0 or 2 in `port/score.h`'s sense, and keeps it in `$48`. Its
// collide id, a height and a table of places are each read by it. The id's
// top bit is the side, which is how a kill finds whose score to add to. Why
// the height and the places differ by side I have not established. I take
// it to be that the two characters are drawn differently. I have not run it
// with two players.
//
// **What stops it.** `terrain_point_bit2` answers with the tile's
// attributes, and with carry for a point outside the level or a tile with
// bit 2. Then:
//
//   * carry: it stops, and the tile is left alone;
//   * a tile without bit 1: it flies on;
//   * a tile with bit 1 and any of bits 4, 5 and 6: it stops, and the ROM
//     goes on to change the tile (`$81:ECAC`);
//   * a tile with bit 1 and none of those: it flies on, for sixty such
//     frames in all, and then it is gone without bursting.
//
// What the bits are to the game I have not established. By what the code
// does, bit 1 with one of the three is something this shot breaks, which is
// what a bazooka does to a wall or a hedge. I have not seen it on a screen.
//
// **Its step is not the same every way.** Four pixels a frame up or left,
// three down or right, two and two on a slant, from the table at
// `$81:EDFF`. The thread's start reads it, not this.
//
// Each ends where the ROM goes on: at the `JSL thread_yield` with the tick
// in A, at the test of carry where it stops, at the thread's last
// instructions when it is spent, or at the `JSL pictures_play` with the
// list in A. `begin` ends at a `JSL` to `$80:9D6A` with the side in A,
// and `aim` at the `RTS` of the setter it jumps to, which the ROM makes.
//
// Port code: libc only.

#ifndef PORT_SHOT5_H
#define PORT_SHOT5_H

#include <stdbool.h>
#include <stdint.h>

#include "assets/rom.h"
#include "port/cpu.h"
#include "port/terrain.h"
#include "port/wram.h"

#define SHOT5_BANK 0x81
#define SHOT5_FRAME_PC 0x81ec03u
#define SHOT5_YIELD_PC 0x81ebffu  // `JSL thread_yield`, A already 1
#define SHOT5_STOP_PC 0x81ec2bu   // `BCS`: carry says leave the tile alone
#define SHOT5_SPENT_PC 0x81ec58u  // gone without bursting
#define SHOT5_BURST_PC 0x81ec30u
#define SHOT5_BURST_PLAY_PC 0x81ec54u  // `JSL pictures_play`

#define SHOT5_BEGIN_PC 0x81ed1au
#define SHOT5_BEGIN_OWNER_PC 0x81ed7cu  // `JSL $80:9D6A`, the side in A
#define SHOT5_AIM_PC 0x81ec79u
#define SHOT5_AIM_RTS_PC 0x81ff47u

#define SHOT5_DP_PLACE_X 0x00   // where whoever fired it is
#define SHOT5_DP_PLACE_Y 0x02
#define SHOT5_DP_FIRED_WAY 0x04 // the way, doubled
#define SHOT5_DP_SIDE 0x06      // 0 or 2: whose it is
#define SHOT5_DP_SPRITE 0x08    // the record again, for `pictures_play`
#define SHOT5_DP_PICTURES 0x14  // flags and a picture, a way
#define SHOT5_DP_WAY_KEPT 0x3c
#define SHOT5_DP_OWNER 0x48     // `$06` again
#define SHOT5_FIRED_WAY_MAX 0x10
#define SHOT5_STEPS 0xedffu       // across and down, a way
#define SHOT5_START_AT 0xeddbu    // where it starts from the player, a way
#define SHOT5_COLLIDE_IDS 0xedabu // a word a side
#define SHOT5_MOUTHS 0xedafu      // a table of places a side, a way each
#define SHOT5_HEIGHTS 0xedb3u     // ...and a height
#define SHOT5_START_PICTURE 0xa26bu
#define SHOT5_PICTURE_BANK 0x0090
#define SHOT5_LIFT 2              // it starts two pixels up
#define SHOT5_OVER_FRAMES 0x003c

#define SHOT5_DP_RECORD 0x0a
#define SHOT5_DP_X 0x0c
#define SHOT5_DP_Y 0x10
#define SHOT5_DP_WAY 0x3e     // the way it goes, doubled twice
#define SHOT5_DP_OVER 0x40    // frames it may still fly over a tile with bit 1
#define SHOT5_DP_STEP_X 0x42
#define SHOT5_DP_STEP_Y 0x44

#define SHOT5_WAY_MAX 0x20
#define SHOT5_TILE_OVER 0x0002         // bit 1
#define SHOT5_TILE_BREAKS_FIRST 0x0010 // bits 4, 5 and 6, tested in turn
#define SHOT5_BREAK_TESTS 3
#define SHOT5_BURST_NUDGE 0xedb7u      // across and down, a way
#define SHOT5_BURST_PICTURES 0xee7fu

typedef enum {
  SHOT5_FLIES,
  SHOT5_STOPS,
  SHOT5_SPENT,
} Shot5Fate;

// For the harness, which prices the frame from it.
typedef struct {
  TerrainRegs ground;
  bool over;     // the tile had bit 1
  int tests;     // ...and this many of the three bits were tested
  bool counted;  // ...none was there, so one frame of the sixty went
} Shot5Log;

Shot5Fate shot5_frame(Wram* w, PortCpu* c, Shot5Log* log);

// False for a way or a side the tables do not have.
bool shot5_begin_supported(const Wram* w, uint16_t page);
// False with no record free, which is the ROM's: it only looks then.
bool shot5_begin(Wram* w, const Rom* rom, PortCpu* c, uint16_t* record);

bool shot5_aim_supported(const Wram* w, const Rom* rom, uint16_t page);
// True if the table's word for its flags is one that clears.
bool shot5_aim(Wram* w, const Rom* rom, PortCpu* c);

// False for a way the table does not have.
bool shot5_burst_supported(const Wram* w, uint16_t page);
void shot5_burst(Wram* w, const Rom* rom, PortCpu* c);

#endif
