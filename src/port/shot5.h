// Weapon 5's shot.
//
// `$81:EBE2` is a thread: the thing weapon 5 fires. Weapon 5 is the bazooka:
// the shot counts itself at `$1FCC`, and the level's end pays a bonus to a
// player whose count is nothing, under the words "NO BAZOOKA FIRED". Its
// start takes a record and puts it in front of the player, and then every
// frame it moves by its step and asks the ground about the point it has
// come to. All of it is here but two calls, each from where the thread
// starts or a call comes back to the next call:
//
//   $81:EBE2  enter   its weight in the level's load, and its sound
//   $81:ED1A  begin   its record, in front of whoever fired it, counted
//                     against their score slot, with its handler named
//   $81:EC79  aim     ...moved to the mouth of the weapon, with the picture
//                     for the way it goes, and the ground where it was fired
//                     from asked about
//   $81:EC03  frame   a frame of its flight, from where the sleep comes
//                     back to the next sleep, or to where it stops
//   $81:EC30  burst   stopped: its record put where it bursts, as far as
//                     the pictures it bursts in
//   $81:EC58  done    no handler, and on to its end, which is a row of
//                     `port/begin.h`'s
//   $81:ECAC  break   the tile it struck, as far as the block is put
//   $81:ECE0  broken  ...the block counted, and fifty points to whoever
//                     fired it
//
// The `RTS` the points come back to, `$81:ED12`, is a link of
// `port/links.h`'s, and it returns to burst.
//
// The two calls not here are `$82:DC57`, after a broken tile with bit 5
// when the shot was fired the way numbered two, and the end of the thread.
// The corpus never breaks such a tile. I have not read `$82:DC57`.
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
//   * a tile with bit 1 and any of bits 4, 5 and 6: it stops, and the
//     tile is changed (`$81:ECAC`, whose first part is `shot5_break`);
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
// **Where it is first asked about is where it was fired from**, not where
// it is put: the first test of the ground is at `$00` and `$02`, the
// player's place. Blocked there, it bursts before it has flown at all.
//
// Each ends where the ROM goes on: at the `JSL thread_yield` with the tick
// in A, at a stop's burst or break, at the `JSL` of a sound, of a list of
// pictures, of the points or of `$82:DC57`, or where the thread ends.
//
// Port code: libc only.

#ifndef PORT_SHOT5_H
#define PORT_SHOT5_H

#include <stdbool.h>
#include <stdint.h>

#include "assets/rom.h"
#include "port/cpu.h"
#include "port/terrain.h"
#include "port/tile_rows.h"
#include "port/wram.h"

#define SHOT5_BANK 0x81
#define SHOT5_FRAME_PC 0x81ec03u
#define SHOT5_YIELD_PC 0x81ebffu  // `JSL thread_yield`, A already 1
#define SHOT5_SPENT_PC 0x81ec58u  // gone without bursting
#define SHOT5_BREAK_RETURN 0xec2fu  // what `JSR $ECAC` leaves
#define SHOT5_BURST_PC 0x81ec30u
#define SHOT5_BURST_PLAY_PC 0x81ec54u  // `JSL pictures_play`
#define SHOT5_BREAK_PC 0x81ecacu       // the tile it struck changed
#define SHOT5_BREAK_PUT_PC 0x81ecdcu   // `JSL $80:AB5A`

#define SHOT5_ENTER_PC 0x81ebe2u
#define SHOT5_SOUND_CALL_PC 0x81ed16u  // `JSL apu_play_sfx`, the sound in A
#define SHOT5_ENTER_RETURN 0xebeeu     // what `JSR $ED13` leaves
#define SHOT5_LOAD 0x0006
#define SHOT5_SOUND 0x0006

#define SHOT5_BEGIN_PC 0x81ed1au
#define SHOT5_PICTURES_CALL_PC 0x81ec75u  // `JSL pictures_play`, the list in A
#define SHOT5_FIRST_PICTURES 0xee6bu
#define SHOT5_WAY_PICTURES 0xee93u  // what `SHOT5_DP_PICTURES` is set to
#define SHOT5_HANDLER 0xedaau
#define SHOT5_HANDLER_BANK 0x0081
#define W_BAZOOKA_FIRED 0x1fccu     // a word a score slot
#define SHOT5_AIM_PC 0x81ec79u
#define SHOT5_AIM_RETURN 0xebf1u    // what `JSR $EC72` leaves

#define SHOT5_DONE_PC SHOT5_SPENT_PC
#define SHOT5_END_PC 0x81ec60u      // `shot_5_end`

#define SHOT5_BROKEN_PC 0x81ece0u      // where the block's put comes back
#define SHOT5_HOLE_CALL_PC 0x81ed02u   // `JSL $82:DC57`
#define SHOT5_SCORE_CALL_PC 0x81ed0eu  // `JSL score_add`
#define SHOT5_HOLE_TILE 0x0020         // bit 5 of the tile struck
#define SHOT5_HOLE_WAY 0x0002          // ...and `SHOT5_DP_WAY_KEPT`
#define SHOT5_HOLE_ACROSS 0x000a       // where `$82:DC57` is asked about
#define SHOT5_HOLE_DOWN 0x0005
#define SHOT5_BREAK_POINTS 0x0050      // in decimal

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
  SHOT5_STOPS,  // ...to burst, or to break the tile first
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

// `$81:EBE2`.
void shot5_enter(Wram* w, PortCpu* c);

// For the harness, which prices the start from it.
typedef struct {
  uint16_t record;
  bool second_slot;  // whoever fired it is the second score slot
} Shot5BeginLog;

// False for a way or a side the tables do not have.
bool shot5_begin_supported(const Wram* w, uint16_t page);
// False with no record free, which is the ROM's: it only looks then.
bool shot5_begin(Wram* w, const Rom* rom, PortCpu* c, Shot5BeginLog* log);

typedef struct {
  bool clears;  // the table's word for its flags is one that clears
  TerrainRegs ground;
} Shot5AimLog;

bool shot5_aim_supported(const Wram* w, const Rom* rom, uint16_t page);
void shot5_aim(Wram* w, const Rom* rom, PortCpu* c, Shot5AimLog* log);

// `$81:ECAC`, as far as the `JSL` that puts the block: the block where the
// shot is swapped for its pair. See `port/tile_rows.h`. The tile's
// attributes stay on the stack for what the ROM does after.
bool shot5_break_supported(const Wram* w, const Rom* rom, uint16_t page);
void shot5_break(Wram* w, const Rom* rom, PortCpu* c, BlockSwapWork* k);

// False for a way the table does not have.
bool shot5_burst_supported(const Wram* w, uint16_t page);
void shot5_burst(Wram* w, const Rom* rom, PortCpu* c);

// `$81:EC58`.
void shot5_done(Wram* w, PortCpu* c);

// `$81:ECE0`, with the page and the tile's attributes on the stack.
typedef enum {
  SHOT5_BROKEN_PLAIN,  // without bit 5
  SHOT5_BROKEN_WAY,    // with it, fired another way
  SHOT5_BROKEN_HOLE,   // with it, fired the way numbered two: `$82:DC57`
} Shot5Broken;
Shot5Broken shot5_broken(Wram* w, PortCpu* c);

#endif
