// A screen made darker or lighter, a step a frame.
//
// The screens of bank `$82` do not sleep through the scheduler when they
// fade. Each has a loop of its own around a `WAI`: wait for a frame, and
// move `$136C`, which the NMI puts in the brightness register, one step
// toward black or toward full.
//
// There are five of them, the same instructions each time:
//
//   $82:AC9A  darker   a level's name, when it has been shown
//   $82:B2F1  darker   the screen a cursor is moved about, when it is done
//   $82:BA36  darker   the top scores, when they are done
//   $82:B565  lighter  the cursor's screen, when it has been drawn
//   $82:BAE8  lighter  the top scores, when they have been drawn
//
// What each belongs to is from the code around it. I have not watched any
// of them.
//
// A frame of one is from the instruction after the `WAI` back to the `WAI`,
// or to the instruction after the loop when the brightness has got there.
//
// Port code: libc only.

#ifndef PORT_DIM_H
#define PORT_DIM_H

#include <stdbool.h>
#include <stdint.h>

#include "port/cpu.h"
#include "port/wram.h"

#define DIM_FULL 0x000f  // as light as the screen goes

typedef struct {
  uint32_t entry;  // the instruction after the `WAI`
  uint32_t wai;
  uint32_t done;
  bool lighter;
} DimLoop;

enum {
  DIM_NAME_OUT,
  DIM_CURSOR_OUT,
  DIM_SCORES_OUT,
  DIM_CURSOR_IN,
  DIM_SCORES_IN,
  DIM_LOOP_COUNT
};

extern const DimLoop DIM_LOOPS[DIM_LOOP_COUNT];

// True if that was the last: the brightness was already there.
bool dim_frame(Wram* w, PortCpu* c, const DimLoop* loop);

#endif
