// Scripted controller input ("movie") for headless runs.
//
// Deterministic input is what turns the tracer into a repeatable instrument:
// the same movie always produces the same CDL, the same WRAM profile, and the
// same trace. The Phase 3 co-simulation harness drives both the reference ROM
// and the native C code from these same files.
//
// Format — one state change per line, in ascending frame order:
//
//   # comment
//   0    -              ; nothing held from frame 0
//   120  Start          ; Start held from frame 120
//   122  -              ; released at 122
//   400  Right+B        ; run right, firing
//
// A line's button set is *absolute*: it replaces whatever was held before, and
// stays in effect until the next line.

#ifndef MOVIE_H
#define MOVIE_H

#include <stdbool.h>
#include <stdint.h>

// Bit numbers match the core's snes_setButtonState() button index.
enum {
  BTN_B = 0, BTN_Y = 1, BTN_SELECT = 2, BTN_START = 3,
  BTN_UP = 4, BTN_DOWN = 5, BTN_LEFT = 6, BTN_RIGHT = 7,
  BTN_A = 8, BTN_X = 9, BTN_L = 10, BTN_R = 11,
};

typedef struct {
  int frame;
  uint16_t buttons;
} MovieEvent;

typedef struct {
  MovieEvent* events;
  int count;
  int next;
  uint16_t current;
} Movie;

bool movie_load(Movie* m, const char* path);
void movie_free(Movie* m);

// Advance to `frame` and return the button mask that should be held. Frames
// must be requested in ascending order.
uint16_t movie_state(Movie* m, int frame);

// Human-readable button mask, e.g. "Right+B". Returns `out`.
const char* movie_format(uint16_t buttons, char* out, int out_size);

#endif
