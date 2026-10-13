// The radar's thread, a frame of it.
//
// While a player's radar is up, a thread at `$82:D8DB` moves one square
// about its panel. Every second frame the square shows the next neighbour:
// the thread goes on through the level's list from where it was, past any
// that is saved or gone, to the first within `$180` pixels of the player on
// both axes. The square is put a sixteenth of that distance from the panel's
// middle. If the whole list has none, the square is put off the screen.
//
// `radar_frame` is the stretch from each wake to the next yield. It stops
// where two other things begin: the radar going down, at `$82:D9B4`, and
// the count of neighbours having changed since the panel was drawn, at
// `$82:D90E`.
//
// ## The panel
//
// The rest of the thread is the panel itself, and `RADAR_STRETCHES` is that:
// each from where the thread starts or a call comes back to the next call,
// yield or return.
//
// The panel is eight tiles a side on the third layer, at the column and row
// the thread was started with. `$82:DBC1` asks for a rectangle of tiles to
// be sent: A and the word under the return address point at a record of
// where the layer's map is in VRAM, how many tiles across and down, and
// where the tiles' words are. It leaves the job at `$82:DC0C` what it needs
// and queues it. The panel going up sends its picture, and going down a
// blank one.
//
// Two channels write the panel's window every line, one a player. Their
// bits are kept in `W_HDMA_CHANNELS`, and `$82:DC45` queues the job that
// gives the word to the hardware.
//
// The count of neighbours left is two tiles under the panel, sent by a job
// of the player's own: `$82:DAA0` or `$82:DADE`.
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
#define RADAR_DP_NUMBER 0x00    // the player's number
#define RADAR_DP_COLUMN 0x02    // the panel's corner, in tiles
#define RADAR_DP_ROW 0x04
#define RADAR_DP_SQUARE 0x08    // the square's display record
#define RADAR_DP_SHOWN 0x0a     // which neighbour it shows, counted from 1
#define RADAR_DP_MIDDLE_X 0x0c  // the panel's middle, on the screen
#define RADAR_DP_MIDDLE_Y 0x0e
#define RADAR_DP_ACROSS 0x10    // how far from it the square goes
#define RADAR_DP_DOWN 0x12
#define RADAR_DP_RIGHT 0x14     // nonzero: the neighbour is right of the player
#define RADAR_DP_BELOW 0x16     // ...and below
#define RADAR_DP_TRIES 0x18
#define RADAR_DP_RECORD 0x1a    // the player's display record
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
  RD_BEGIN,      // $D8DB-$D8EB
  RD_BEGIN_TICK, // $D8EC-$D8EE
  RD_CHANGED,    // $D90E-$D90F
  RD_LOWER,      // $D9B4-$D9BE
  RD_BLANK_ASK,  // $D9C3-$D9D0
  RD_GONE,       // $D9D5-$D9DB, with its JML
  RD_COUNT_HEAD, // $D9DC-$D9DF
  RD_COUNT_ASK,  // $D9E0-$D9E5, and $D9EB-$D9F0
  RD_PANEL_ASK,  // $D9F6-$DA0F
  RD_PLACE,      // $DA14-$DA41
  RD_SQUARE,     // $DA46-$DA84
  RD_SHOW,       // $DA89-$DA9A
  RD_TILES_ASK,  // $DBC1-$DC05
  RD_TILES_ASKED,  // $DC0A
  RD_CHANNELS_ASK,  // $DC45-$DC4E, with its JML
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

// What a stretch needs of where it starts.
typedef enum {
  RADAR_NEEDS_NOTHING,
  RADAR_NEEDS_NUMBER,  // the player's number on the page, 0 or 1
  RADAR_NEEDS_PLAYER,  // ...or twice it, at `RADAR_DP_PLAYER`
  RADAR_NEEDS_SQUARE,  // a display record in Y, in low WRAM
  RADAR_NEEDS_TILES,   // A and the word under the return address point at a
                       // record in the cartridge
  RADAR_NEEDS_FRAME,   // what `radar_frame` does
} RadarNeeds;

bool radar_needs_met(const Wram* w, RadarNeeds needs, const PortCpu* c);

#define RADAR_TILES_ASK_PC 0x82dbc1u
#define RADAR_TILES_QUEUE_PC 0x82dc06u  // its `JSL vbl_queue_a_add`
#define RADAR_CHANNELS_ASK_PC 0x82dc45u
#define RADAR_BEGIN_YIELD_PC 0x82d8efu
#define RADAR_DRAW_PC 0x82d8f3u         // `JSR $D9F6`
#define RADAR_COUNT_FIRST_PC 0x82d9e6u  // `JSL vbl_queue_a_add`, a player each
#define RADAR_COUNT_SECOND_PC 0x82d9f1u

// The panel's tables, in bank `$82`, which the stretches' data bank has to
// be.
#define RADAR_BANK 0x82u
#define RADAR_PICTURES 0x82db1cu  // a player: the panel's record, 24 bits in 32
#define RADAR_BLANK 0xdb38u       // the record that takes it down
#define RADAR_CHANNELS 0x82db42u  // a player: the channel's bit

// name, symbol, entry, what it needs.
#define RADAR_STRETCHES(X) \
  X(begin, "$82:D8DB", 0x82d8dbu, RADAR_NEEDS_NUMBER) \
  X(drawn, "$82:D8F6", 0x82d8f6u, RADAR_NEEDS_NOTHING) \
  X(count_changed, "$82:D90E", 0x82d90eu, RADAR_NEEDS_NOTHING) \
  X(recounted, "$82:D913", 0x82d913u, RADAR_NEEDS_FRAME) \
  X(lower, "$82:D9B4", 0x82d9b4u, RADAR_NEEDS_PLAYER) \
  X(blank_ask, "$82:D9C3", 0x82d9c3u, RADAR_NEEDS_NOTHING) \
  X(gone, "$82:D9D5", 0x82d9d5u, RADAR_NEEDS_NOTHING) \
  X(count_ask, "$82:D9DC", 0x82d9dcu, RADAR_NEEDS_NOTHING) \
  X(panel_ask, "$82:D9F6", 0x82d9f6u, RADAR_NEEDS_NUMBER) \
  X(panel_place, "$82:DA14", 0x82da14u, RADAR_NEEDS_PLAYER) \
  X(square_set, "$82:DA46", 0x82da46u, RADAR_NEEDS_SQUARE) \
  X(panel_show, "$82:DA89", 0x82da89u, RADAR_NEEDS_PLAYER) \
  X(tiles_ask, "$82:DBC1", RADAR_TILES_ASK_PC, RADAR_NEEDS_TILES) \
  X(tiles_asked, "$82:DC0A", 0x82dc0au, RADAR_NEEDS_NOTHING) \
  X(channels_ask, "$82:DC45", RADAR_CHANNELS_ASK_PC, RADAR_NEEDS_NOTHING)

#define X(name, sym, entry, needs) \
  void radar_##name(Wram* w, const Rom* rom, PortCpu* c, RadarWork* k);
RADAR_STRETCHES(X)
#undef X

#endif
