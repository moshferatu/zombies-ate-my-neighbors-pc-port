// The demo that plays when the title screen is left alone: a recording of a
// pad, fed to the game in place of the player's.
//
//   $80:9CB2  demo_job   a vblank job, once a frame while the demo runs
//
// The NMI has already put the real pads in `$6E` and `$70` when this runs.
// It overwrites the first with the recording's.
//
// ## The recording
//
// Pairs of words in the cartridge: what the pad held, and for how many
// frames. The job keeps the pair it is on and counts its frames down, and
// reads the next when they run out.
//
// ## What else it does
//
// **Any button on a real pad ends the demo.** Both players' pads are
// cleared, so the press that ended it is not also the title screen's, and a
// flag is raised for the thread that is waiting on it.
//
// **It makes the demo repeatable.** The game's random numbers are stirred by
// the NMI each frame, so a recording alone would not play back the same
// way. Each frame the job counts up and writes its count over the random
// state, low byte and counter both.
//
// ## Its contract with the ROM
//
// WRAM is written as the ROM writes it, the pointer it leaves on page zero
// included. It stays queued: carry set. X is untouched. A and Y and the two
// other flags are in `DemoLog`.
//
// Port code: libc only.

#ifndef PORT_DEMO_H
#define PORT_DEMO_H

#include <stdbool.h>
#include <stdint.h>

#include "assets/rom.h"
#include "port/wram.h"

#define DEMO_JOB_PC 0x809cb2u
#define DEMO_JOB_RTL_PC 0x809d0bu  // the played path's; the ended path has its own

#define W_DEMO_FRAME 0x1ec2u       // frames played, which is the random state
#define W_DEMO_RECORDING 0x1ebau   // a 24-bit pointer, its bank at `$1EBC`
#define W_DEMO_AT 0x1ebeu          // how far into it, in bytes
#define W_DEMO_PAD 0x1eb8u         // what the pad holds now
#define W_DEMO_FRAMES_LEFT 0x1ec0u // ...and for how much longer
#define W_DEMO_ENDED 0x1eb6u       // counted up when a real pad ends it
#define W_DEMO_HUD 0x1e88u         // two words cleared with it
#define W_DEMO_HUD_2 0x1e8au

// Sixteen bytes, a direction for each way the four arrows can be held. The
// NMI has a copy of its own.
#define DEMO_DIRECTIONS 0x809d0cu

#define DEMO_DP_POINTER 0x38       // the recording's pointer, as the job leaves it

typedef struct {
  bool ended;      // a real pad was pressed
  bool read_pair;  // the pair ran out and the next was read
  uint16_t a, y;
  bool n, z;
} DemoLog;

// Can `demo_job` take this frame? Not a recording outside the cartridge.
// `joy1` and `joy2` are the pads as the hardware latched them.
bool demo_job_supported(const Wram* w, uint16_t joy1, uint16_t joy2);

// One frame of it. `y` is Y as the job found it. `log` may be NULL.
void demo_job(Wram* w, const Rom* rom, uint16_t joy1, uint16_t joy2,
              uint16_t y, DemoLog* log);

#endif
