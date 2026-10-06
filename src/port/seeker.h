// Two leaves of the thing in level 37 that comes at a player.
//
// Its thread at `$82:E858` asks where the nearest player is and which of
// eight ways that is, and then calls these two.
//
// `$82:E7C7` takes the way in A and moves the thing a pixel along it. Its
// place is kept on its page, at `$0E` and `$10`, and copied to its display
// record. The table at `$82:E7E3` has a step across and a step down for
// each of nine ways, the first of which is none.
//
// `$82:E807` changes its picture: every fifth call the record gets the
// other of two.
//
// Port code: libc only.

#ifndef PORT_SEEKER_H
#define PORT_SEEKER_H

#include <stdbool.h>
#include <stdint.h>

#include "assets/rom.h"
#include "port/cpu.h"
#include "port/wram.h"

#define SEEKER_STEP_PC 0x82e7c7u
#define SEEKER_STEP_RTS_PC 0x82e7e2u
#define SEEKER_FLAP_PC 0x82e807u
#define SEEKER_FLAP_RTS_PC 0x82e822u

#define SEEKER_BANK 0x82
#define SEEKER_STEPS 0x82e7e3u
#define SEEKER_WAYS 9
#define SEEKER_PICTURES 0x82e823u
#define SEEKER_FLAP_FRAMES 4  // counted down past zero
#define SEEKER_DP_RECORD 0x08
#define SEEKER_DP_X 0x0e
#define SEEKER_DP_Y 0x10
#define SEEKER_DP_PICTURE 0x18
#define SEEKER_DP_FRAMES 0x1a

// A is the way. False for one the table does not have.
bool seeker_step_supported(uint16_t way);
void seeker_step(Wram* w, const Rom* rom, PortCpu* c);

// True if the picture changed.
bool seeker_flap(Wram* w, const Rom* rom, PortCpu* c);

#endif
