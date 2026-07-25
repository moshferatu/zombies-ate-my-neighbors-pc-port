// Pushing a movie's per-frame state into the emulated controllers.
//
// This is the one place the movie meets the core. It is a separate header from
// `movie.h` because `movie.h` is emulator-free — `zamn_disasm` links the
// analysis library without ever seeing an SNES — and because eight tools do
// this identically. When the format grew a second controller, every one of them
// grew it at once; that is the point.

#ifndef MOVIE_APPLY_H
#define MOVIE_APPLY_H

#include "analysis/movie.h"
#include "snes.h"

static inline void movie_apply(Movie* m, Snes* snes, int frame) {
  MovieInput in = movie_state(m, frame);
  for (int p = 0; p < MOVIE_PORTS; p++) {
    for (int b = 0; b < 12; b++) {
      snes_setButtonState(snes, p + 1, b, (in.port[p] >> b) & 1);
    }
  }
}

#endif
