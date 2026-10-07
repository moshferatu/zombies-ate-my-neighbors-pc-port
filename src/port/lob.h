// The thing thrown in an arc.
//
// `$81:F976` is a thread. It takes a display record, puts it thirty pixels
// up at the place it was given, and then every frame moves it two pixels
// one of eight ways, adds its rise to its height, and every ninth frame
// shows the next of four pictures. The rise starts at 3 and goes down one
// every fourth frame, so the thing goes up, over and down. When its height
// goes below zero it has landed, and the ROM does the rest.
//
// This is a frame of that: from the return of the yield at `$81:F98A` to
// the next yield, or to `$81:F99C` when it has landed.
//
// And the rest of the thread, either side of that loop:
//
//   $81:F976  begin    its record, thirty pixels up, to the first yield
//   $81:F99C  landed   on the ground, and off to be heard
//   $81:F9B2  burst    everything within forty pixels told, and a sleep
//   $81:F9BC  burst    ...and told again, and off to the rest of them
//   $81:F9C6  end      its weight given back, and off to free its record
//
// **Landing, it tells whatever is near, twice a tick apart.** The box is
// forty pixels each way from where it came down, and what it tells them is
// a collide id by the player it is from. Between the two it shows one list
// of pictures and after them another. By what the code does it goes off
// when it lands. I have not seen it on a screen.
//
// Each of the five ends where the ROM goes on: at a `JSL thread_yield`, at
// the `JSL` that plays the landing's sound with the sound in A, at a `JSL
// pictures_play` with the list in A, or at the `JSL` that frees the record
// with the record in A.
//
// Port code: libc only.

#ifndef PORT_LOB_H
#define PORT_LOB_H

#include <stdbool.h>
#include <stdint.h>

#include "assets/rom.h"
#include "port/cpu.h"
#include "port/oam.h"  // ActorNotifyWork
#include "port/wram.h"

#define LOB_PC 0x81f98au
#define LOB_YIELD_PC 0x81f986u   // `JSL thread_yield`, A already 1
#define LOB_LANDED_PC 0x81f99cu
#define LOB_BEGIN_PC 0x81f976u
#define LOB_LANDED_SOUND_PC 0x81f9a7u  // `JSL apu_play_sfx`
#define LOB_BURST_PC 0x81f9b2u
#define LOB_BURST_YIELD_PC 0x81f9b8u
#define LOB_BURST_AGAIN_PC 0x81f9bcu
#define LOB_BURST_PLAY_PC 0x81f9c2u   // `JSL pictures_play`
#define LOB_END_PC 0x81f9c6u
#define LOB_END_FREE_PC 0x81f9d4u     // `JSL actor_slot_free`

#define LOB_LOAD 0x0001            // what one adds to the level's load
#define LOB_START_HEIGHT 0x001e
#define LOB_START_PICTURE 0xd300u
#define LOB_START_FLAGS 0x800au
#define LOB_START_RISE 3
#define LOB_START_SLOW_MASK 3
#define LOB_SFX_LAND 0x002e
#define LOB_BURST_PICTURES 0xfab1u       // the list shown between the two
#define LOB_BURST_PICTURES_REST 0xfabfu  // ...and after them
#define LOB_BURST_REACH 0x0028
#define LOB_BURST_IDS 0xfacdu      // what it tells them, a word a player
#define LOB_DP_PLACE_X 0x00
#define LOB_DP_PLACE_Y 0x02
#define LOB_DP_PLAYER 0x06
#define LOB_DP_SPRITE 0x08         // the record again, for `pictures_play`

#define LOB_BANK 0x81
#define LOB_STEPS 0xfad1u          // a step across and a step down, a way
#define LOB_WAY_MAX 16             // the way is kept doubled
#define LOB_PICTURES_LOW 0xfaa1u   // four pictures
#define LOB_PICTURES_HIGH 0xfaa9u  // ...and four for `$44` up and over
#define LOB_HIGH 0x0044
#define LOB_PICTURE_FRAMES 8
#define LOB_PICTURE_BANK 0x008f
#define LOB_DP_WAY 0x04
#define LOB_DP_RECORD 0x0a
#define LOB_DP_PICTURE_FRAMES 0x18
#define LOB_DP_PICTURE 0x1a
#define LOB_DP_RISE 0x1e
#define LOB_DP_FRAMES 0x20
#define LOB_DP_SLOW_MASK 0x22  // the rise drops when the count has none of it
#define LOB_DP_PICTURES 0x3c

enum {
  LB_ARC,        // JSR, $FEEF-$FF01, RTS
  LB_ARC_SLOW,   // DEC $1E
  LB_MOVE,       // JSR, $FA55-$FA6F
  LB_PIC_COUNT,  // JSR, DEC $18 : BPL, RTS
  LB_PIC,        // $FA26-$FA37, $FA3B-$FA53
  LB_PIC_HIGH,   // LDA #$FAA9
  LB_TEST,       // $F993-$F999
  LB_AGAIN,      // BRA, LDA #$0001
  LB_TAKEN,
  LB_BLOCK_COUNT
};

typedef struct {
  uint16_t blocks[LB_BLOCK_COUNT];
} LobWork;

// False for a way the table does not have.
bool lob_frame_supported(const Wram* w, uint16_t page);
void lob_frame(Wram* w, const Rom* rom, PortCpu* c, LobWork* k);

// The thread's start. False with no record free, which is the ROM's.
bool lob_begin(Wram* w, PortCpu* c, uint16_t* record);
void lob_landed(Wram* w, PortCpu* c);
// False when something it told has a handler the port lacks: asked of a
// copy first. `again` is the second of the two.
bool lob_burst(Wram* w, const Rom* rom, PortCpu* c, bool again,
               ActorNotifyWork* told);
// False if the level's load would go below nothing, where the ROM stops.
bool lob_end(Wram* w, PortCpu* c);

#endif
