// The threads that animate a level's colours: water that runs, lights that
// pulse, the big figure's shimmer.
//
// A level lists one or more of these among its threads. Each sleeps a few
// ticks, changes some colours of the background's second copy, the one that
// is shown (`port/dma.h`), and asks for that copy to be sent. This is one
// waking of each as readable C, from where `thread_yield` returns to the next
// yield:
//
//   $80:A0C1  turn_six     colours 57-62 turn round one place, every 5 ticks
//   $80:A236  turn_five    colours 104-108, the same
//   $80:A27D  turn_seven   colours 97-103, after 1 to 8 ticks drawn at random
//   $80:A106  pulse        colour 90's green goes up and comes down, every 4
//   $80:A180  figure       the big figure's row steps through ten arrangements
//
// What is not here is each thread's setup, which runs once.
//
// ## Turning
//
// The three that turn share one counter, `$7E:5728`, which says where in the
// run the first shown colour comes from. Each waking takes it down a place and
// copies the run from the level's own colours, wrapping at the run's end. The
// counter is one word for all three because a level lists only one of them.
//
// ## The pulse
//
// The thread's page holds the colour's red and blue as the level has them,
// the same with the green it is showing, and a step. Each waking writes the
// second into the shown colour and adds the step. The step turns down when
// the sum passes `$03E0`, a full green over no red, and up again when it is
// back at or below the first.
//
// **The test is of the whole word, red and blue included**, so a colour with
// any blue is over `$03E0` after one step and flickers between two greens,
// and one with red tops out early by that much.
//
// ## The figure
//
// A table in the cartridge, `$80:A182`, of ten rows of eight words: how long
// to sleep, and then for seven of the row's colours which of the level's
// sixteen to show there. The first colour of the row is never written.
//
// ## Its contract with the ROM
//
// Each writes WRAM exactly as the ROM does, the pulse's and the figure's
// working on their thread's page included, and ends at the thread's `JSL
// thread_yield` with the tick count in A.
//
// Port code: libc only.

#ifndef PORT_PALCYCLE_H
#define PORT_PALCYCLE_H

#include <stdbool.h>
#include <stdint.h>

#include "assets/rom.h"
#include "port/rng.h"
#include "port/wram.h"

// One of the three that turn a run of colours.
typedef struct {
  uint32_t frame_pc;
  uint32_t yield_pc;  // `JSL thread_yield`, the ticks already in A
  uint16_t first;     // the run's first colour, as a byte offset into a palette
  int colours;
} PalcycleTurn;

extern const PalcycleTurn PALCYCLE_TURN_SIX;
extern const PalcycleTurn PALCYCLE_TURN_FIVE;
extern const PalcycleTurn PALCYCLE_TURN_SEVEN;

#define PALCYCLE_TURN_TICKS 5
#define W_PALCYCLE_TURN_AT 0x5728u  // where the first shown colour comes from

#define PALCYCLE_PULSE_PC 0x80a106u
#define PALCYCLE_PULSE_YIELD_PC 0x80a102u
#define PALCYCLE_PULSE_TICKS 4
#define PALCYCLE_PULSE_COLOUR 0x00b4u  // colour 90, as a byte offset
#define PALCYCLE_PULSE_DP_NOW 0x00     // red, blue, and the green being shown
#define PALCYCLE_PULSE_DP_FLOOR 0x02   // red and blue alone
#define PALCYCLE_PULSE_DP_STEP 0x04

#define PALCYCLE_FIGURE_PC 0x80a180u
#define PALCYCLE_FIGURE_YIELD_PC 0x80a17cu
#define PALCYCLE_FIGURE_TABLE 0x80a182u
#define PALCYCLE_FIGURE_ROWS 10
#define PALCYCLE_FIGURE_COLOURS 7     // of the row's sixteen, from the second
#define PALCYCLE_FIGURE_DP_ROW 0x00
#define PALCYCLE_FIGURE_DP_TICKS 0x02
#define PALCYCLE_FIGURE_DP_ROW_AT 0x04  // the row's place in the table
#define PALCYCLE_FIGURE_DP_COLOUR 0x06  // the colour being written

// The job every one of them asks for, `$80:A09E`, on queue A.
#define PALCYCLE_JOB 0xa09eu
#define PALCYCLE_JOB_BANK 0x0080u

// For the harness, and only for it.
typedef struct {
  int queue_slot;       // where the job went in the queue, -1 for nowhere
  bool wrapped;         // a turn: the counter started over
  bool at_floor;        // the pulse: back down, so the step turns up
  bool past_floor;      // ...or above it
  bool at_top;          // ......and past the top, so it turns down
  bool v;               // overflow, where the frame sets it
} PalcycleLog;

// Can `palcycle_turn` take this waking? Not a counter that is off the run.
bool palcycle_turn_supported(const Wram* w, const PalcycleTurn* turn);

// One waking of each. `log` may be NULL.
void palcycle_turn(Wram* w, const PalcycleTurn* turn, PalcycleLog* log);
// How long the seven sleep: a draw, which takes the carry the frame left.
uint16_t palcycle_turn_seven_ticks(Wram* w, bool carry, RngResult* draw);
void palcycle_pulse(Wram* w, uint16_t page, PalcycleLog* log);

// Not a row past the table's ten.
bool palcycle_figure_supported(const Wram* w, uint16_t page);
// Returns how many ticks the thread sleeps.
uint16_t palcycle_figure(Wram* w, const Rom* rom, uint16_t page,
                         PalcycleLog* log);

#endif
