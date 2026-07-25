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
//   0     -              ; nothing held from frame 0
//   120   Start          ; Start held from frame 120
//   122   -              ; released at 122
//   400   Right+B        ; run right, firing
//
// A line's button set is *absolute*: it replaces whatever was held before, and
// stays in effect until the next line.
//
// Two controllers
// ---------------
// A frame may carry a `2:` prefix, which aims the line at controller 2:
//
//   130   Start          ; player 1 starts the game
//   2:130 Start          ; player 2 joins
//   2:132 -
//   400   Right+B        ; player 1 runs right...
//   2:400 Left+Y         ; ...while player 2 runs left
//
// The two ports are independent event streams, each with its own absolute
// semantics, so a stretch where one player does nothing costs no lines. Frames
// must ascend *within* a port; the two streams may interleave freely. A movie
// with no `2:` lines holds nothing on controller 2 for its whole length, which
// is what every one-player movie written before this did implicitly.
//
// Two players is not cosmetic: it is the only way to reach the parts of the
// game that ask *which* player did something (the per-player score slots at
// $80:C7C2, a player's weapon striking the other player). See PROGRESS.md.

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

#define MOVIE_PORTS 2

typedef struct {
  int frame;
  uint16_t buttons;
} MovieEvent;

typedef struct {
  MovieEvent* events;
  int count;
  int next;
  uint16_t current;
} MovieTrack;

typedef struct {
  MovieTrack track[MOVIE_PORTS];
} Movie;

// What both controllers hold on one frame.
typedef struct {
  uint16_t port[MOVIE_PORTS];
} MovieInput;

bool movie_load(Movie* m, const char* path);
void movie_free(Movie* m);

// Advance to `frame` and return what each controller should hold. Frames must
// be requested in ascending order.
MovieInput movie_state(Movie* m, int frame);

// True if the movie ever presses anything on controller `port` (0-based). Lets
// a tool report "two-player" without the caller re-reading the file.
bool movie_uses_port(const Movie* m, int port);

// Human-readable button mask, e.g. "Right+B". Returns `out`.
const char* movie_format(uint16_t buttons, char* out, int out_size);

#endif
