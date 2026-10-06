// The radar's thread, a frame of it.
//
// While a player's radar is up, a thread at `$82:D8DB` moves one square
// about its panel. Every second frame the square shows the next neighbour:
// the thread goes on through the level's list from where it was, past any
// that is saved or gone, to the first within `$180` pixels of the player on
// both axes. The square is put a sixteenth of that distance from the panel's
// middle. If the whole list has none, the square is put off the screen.
//
// This is the stretch from each wake to the next yield. It leaves two things
// to the ROM, by stopping where they begin: the radar going down, at
// `$82:D9B4`, and the count of neighbours having changed since the panel was
// drawn, at `$82:D90E`.
//
// Port code: libc only.

#ifndef PORT_RADAR_THREAD_H
#define PORT_RADAR_THREAD_H

#include <stdbool.h>
#include <stdint.h>

#include "port/cpu.h"
#include "port/wram.h"

#define RADAR_FRAME_PC 0x82d8fdu    // after the yield
#define RADAR_YIELD_PC 0x82d8f9u    // `JSL thread_yield`, A already 2
#define RADAR_DOWN_PC 0x82d9b4u
#define RADAR_RECOUNT_PC 0x82d90eu
#define RADAR_YIELD_TICKS 2

// Fields on the thread's page.
#define RADAR_DP_SQUARE 0x08    // the square's display record
#define RADAR_DP_SHOWN 0x0a     // which neighbour it shows, counted from 1
#define RADAR_DP_MIDDLE_X 0x0c  // the panel's middle, on the screen
#define RADAR_DP_MIDDLE_Y 0x0e
#define RADAR_DP_ACROSS 0x10    // how far from it the square goes
#define RADAR_DP_DOWN 0x12
#define RADAR_DP_RIGHT 0x14     // nonzero: the neighbour is right of the player
#define RADAR_DP_BELOW 0x16     // ...and below
#define RADAR_DP_TRIES 0x18
#define RADAR_DP_PLAYER 0x1c    // twice the player's number
#define RADAR_DP_LEFT 0x1e      // neighbours left when the panel was drawn

// Nonzero while that player's radar is up: `W_PLAYER_FLAG` in
// `port/player.h`. And the neighbours still to be saved: `W_NEIGHBOURS_LEFT`
// in `port/bodies.h`.
#define W_RADAR_UP 0x1f98u
#define W_RADAR_NEIGHBOURS_LEFT 0x1d52u
#define RADAR_NEIGHBOUR_GONE 0x0080u
#define RADAR_REACH 0x0180u
#define RADAR_NOWHERE 0xffe0u

enum {
  RD_HEAD,       // $D8FD-$D903
  RD_DOWN,       // $D904-$D906
  RD_SAME,       // $D907-$D90D
  RD_COUNT,      // $D913-$D918
  RD_TRY,        // $D919-$D91C
  RD_NONE,       // $D91D-$D929
  RD_PICK,       // $D92A-$D932
  RD_PICK_BCC,   // $D933-$D934
  RD_PICK_WRAP,  // $D935-$D937
  RD_FLAG,       // $D938-$D944
  RD_DX,         // $D945-$D95C
  RD_NEGATE,     // $D95D-$D962, and the same at $D978-$D97D
  RD_FAR,        // $D963-$D967, and $D97E-$D982
  RD_DY,         // $D968-$D977
  RD_PUT_HEAD,   // $D983-$D98C
  RD_SUB,        // $D98D-$D993, and $D9A2-$D9A8
  RD_ADD,        // $D994-$D998, and $D9A9-$D9AD
  RD_PUT_MID,    // $D999-$D9A1
  RD_PUT_TAIL,   // $D9AE-$D9B3
  RD_YIELD,      // $D8F6-$D8F8
  RD_TAKEN,      // a branch taken
  RD_BLOCK_COUNT
};

typedef struct {
  uint16_t blocks[RD_BLOCK_COUNT];
} RadarWork;

// The player's number is one of the two, and the square's record and the
// player's are in low WRAM.
bool radar_frame_supported(const Wram* w, uint16_t page);

void radar_frame(Wram* w, PortCpu* c, RadarWork* k);

#endif
