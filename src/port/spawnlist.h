// The thread that brings a level's creatures in from its list of places.
//
// A level record points at a list in the cartridge: for each place a rest
// time, and where it is. The thread, `$81:80EC`, looks at one place a frame.
// This is one frame of it as readable C, from where `thread_yield` returns to
// the next yield:
//
//   $81:810F  frame   one place looked at, or the nearest one started
//
// `port/bodies.h` has the same loop as three stretches, a register at a time.
// They are still registered, and take the frames this one declines.
//
// ## A pass down the list
//
// **Nothing happens while the board is full.** Each frame starts by asking
// `spawn_has_room`, and a no is the whole frame.
//
// **A place that is resting counts down** a frame, and is not looked at.
//
// **A place that is ready is measured** against the nearer player, and the
// nearest so far is remembered.
//
// **At the end of the list the nearest is started**, if it is nearer than
// 256, and set resting for its rest time. Then the pass begins again from the
// top. So what comes in is whatever is ready and nearest to a player, one
// creature a pass, and a pass takes a frame a place.
//
// The rest time of zero is what ends the list.
//
// ## Its contract with the ROM
//
// It writes WRAM exactly as the ROM does: the thread's page, the rest
// counters at `$7E:60DA`, and the two words `nearest_player_dist` leaves. A
// frame ends at the `JSL thread_yield` with one tick in A, or at the `JSR
// $807E` that starts the creature, which is the ROM's, with the place in X,
// its offset into the list in Y and its rest time in A.
//
// Port code: libc only.

#ifndef PORT_SPAWNLIST_H
#define PORT_SPAWNLIST_H

#include <stdbool.h>
#include <stdint.h>

#include "assets/rom.h"
#include "port/wram.h"

#define SPAWNLIST_FRAME_PC 0x81810fu
#define SPAWNLIST_YIELD_PC 0x81810bu  // `JSL thread_yield`, A already 1
#define SPAWNLIST_START_PC 0x818179u  // `JSR $807E`

// A place in the list: its rest time, a byte, and then where it is.
#define SPAWNLIST_PLACE_BYTES 10
#define SPAWNLIST_PLACE_X 1
#define SPAWNLIST_PLACE_Y 3
#define SPAWNLIST_PLACES_MAX 64
#define SPAWNLIST_NEAR 0x0100u
#define W_SPAWNLIST_REST 0x60dau  // a byte a place: frames of rest left

// Fields on the thread's page.
#define SPAWNLIST_DP_DOUBLE 0x0a   // working: a place's number, doubled
#define SPAWNLIST_DP_LIST 0x0c     // the list's address, in the data bank
#define SPAWNLIST_DP_PLACE 0x10    // the place this frame looks at
#define SPAWNLIST_DP_NEAREST 0x12  // the nearest ready place this pass
#define SPAWNLIST_DP_DISTANCE 0x14 // ...and how near

typedef enum {
  SPAWNLIST_SLEEPS,
  SPAWNLIST_STARTS,  // the nearest place's creature is to be started
} SpawnlistFate;

// For the harness, and only for it: which way the frame went, and the
// registers at the exit that starts a creature.
typedef struct {
  bool held;        // no room on the board
  bool rested;      // the place was resting
  bool measured;    // ...or ready, and measured
  bool nearer;      // ......and the nearest so far
  bool none_near;   // the end of the list, with nothing near enough
  bool c;           // carry as the frame leaves it
  uint16_t rest, place, place_at;
} SpawnlistLog;

// Can `spawnlist_frame` take this frame? Not a list outside the cartridge,
// nor a place past the sixty-four there are counters for, nor a player's
// record outside low WRAM. `bank` is the data bank. It only looks.
bool spawnlist_frame_supported(const Wram* w, uint16_t page, uint8_t bank);

// One frame, for the thread whose page is `page`. `log` may be NULL.
SpawnlistFate spawnlist_frame(Wram* w, const Rom* rom, uint16_t page,
                              uint8_t bank, SpawnlistLog* log);

#endif
