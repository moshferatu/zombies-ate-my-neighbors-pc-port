// The video chip's share of a saved state.
//
// It is saved in the layout the emulated PPU has always saved it in, so that
// a state saved by either is loaded by either: the registers a field at a
// time in the PPU's order, then the memories, then the two rows of the last
// line's sprites as far as the console's 256 columns.
//
// Two things in it are nobody's to read after a load. The PPU saves where it
// had got to in a line of mode 7, which nothing here draws: zero is saved
// for it. And a sprite's priority is in its row only where a sprite is.
//
// The emulator's `StateHandler` is what a state is written with.

#ifndef ZAMN_VIDEO_STATE_H
#define ZAMN_VIDEO_STATE_H

#include <stdint.h>

#include "statehandler.h"
#include "video/registers.h"

// Where those two things are in what was saved.
typedef struct {
  int mode7_working;  // eight bytes
  int sprite_rows;    // 256 bytes of pixels, then 256 of priorities
} VideoStateParts;

// Save from, or load into, `r`, its memories and the two rows. `parts`, if
// given, is told where the two things are.
void video_state_handle(VideoRegisters* r, uint8_t* obj_pixel, uint8_t* obj_priority,
                        StateHandler* sh, VideoStateParts* parts);

// What of two saved states, `size` bytes each, is not the same, or NULL. The
// two things are left out.
const char* video_states_differ(const uint8_t* here, const uint8_t* theirs, int size,
                                const VideoStateParts* parts);

#endif
