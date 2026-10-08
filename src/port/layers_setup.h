// The picture's layers set up as a level has them.
//
// `$80:AC7E` writes six registers: mode 1 with the third layer in front,
// and where each of the three layers has its map and its tiles in VRAM.
//
//     the first layer   tiles at `$5000`   a map at `$6800`, 64 by 64
//     the second        tiles at `$2000`   a map at `$7000`, 32 by 32
//     the third         tiles at `$4000`   a map at `$6400`, 32 by 32
//
// `$80:88A9` does the same after it has put every layer at its corner: the
// six words of the scroll's shadow cleared, each scroll register written
// zero twice, and the three layers and the sprites on the main screen.
//
// Neither writes the machine. Each records its writes as `port/hw.h` has
// them, and the harness makes them on the ROM's cycles.
//
// Both leave `$44` in A's low byte, and X and Y with their high bytes
// cleared by the `SEP #$30` they begin their writes with.
//
// Port code: libc only.

#ifndef PORT_LAYERS_SETUP_H
#define PORT_LAYERS_SETUP_H

#include <stdint.h>

#include "port/hw.h"
#include "port/wram.h"

#define LAYERS_SETUP_PC 0x80ac7eu
#define LAYERS_SETUP_RTL_PC 0x80aca0u
#define LAYERS_RESET_PC 0x8088a9u
#define LAYERS_RESET_RTL_PC 0x808908u

#define LAYERS_SCROLL_WORDS 6  // across and down for each of three layers
#define LAYERS_LEFT_IN_A 0x44u

void layers_setup(HwTrace* t);
void layers_reset(Wram* w, HwTrace* t);

#endif
