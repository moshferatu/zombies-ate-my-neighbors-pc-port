// Where a mover wants to be next, and the one rule that is about the other
// player rather than about the board.
//
//   $80:E450  step_propose        direction + speed -> a candidate position
//   $80:A8B3  step_tether_blocked ...is that candidate too far from the other
//                                 player to be allowed?
//
// These are the two ends of `$80:E4C1`, the movement step validator. It opens
// with `JSR $E450` to work out where the mover is trying to go, then puts that
// candidate through four tests — `terrain_blocked`, this tether,
// `actor_obstacle_at_point` and `terrain_out_of_bounds` — and commits it only
// if all four agree (`$80:E4FF  LDA $34 : STA $30`). Then it does the whole
// thing again for the other axis, which is why a mover slides along a wall
// instead of stopping dead against it.
//
// With these two the pipeline is complete: `port/terrain.h` has two of the
// tests, `port/oam.h` has the third, this file has the fourth and the proposer.
// Only `$80:E4C1` itself — the sequencing — is still the ROM's.
//
// ## The direction tables
//
// `$80:E450` is table-driven and the tables are the interesting part. `$24` on
// the mover's page is a direction **already doubled**, so it indexes two
// nine-word tables of per-frame deltas directly:
//
//   | `$24` | dx | dy | |
//   | --- | --- | --- | --- |
//   | `$00` |  0 |  0 | not moving |
//   | `$02` |  0 | -1 | up |
//   | `$04` | +1 | -1 | up-right |
//   | `$06` | +1 |  0 | right |
//   | `$08` | +1 | +1 | down-right |
//   | `$0A` |  0 | +1 | down |
//   | `$0C` | -1 | +1 | down-left |
//   | `$0E` | -1 |  0 | left |
//   | `$10` | -1 | -1 | up-left |
//
// Clockwise from up, with zero meaning still. Every step is one pixel per axis,
// and speed is entirely a question of **how often the step is taken twice**.
//
// ## How speed is expressed
//
// `$80:E45C  LDA $E4AA,X : AND $0020` — a mask ANDed with the low word of
// `W_SCHED_TICK`. Non-zero means add the delta a second time this frame. The
// mask is chosen by `((dir & 2) << 2) + $76`, which is two rows of four:
//
//   |  `$76` | diagonal | cardinal | pixels per frame |
//   | --- | --- | --- | --- |
//   | `$00` | `$0001` | `$FFFF` | 1.5 diagonal, 2 cardinal |
//   | `$02` | `$0000` | `$0001` | 1, 1.5 |
//   | `$04` | `$0000` | `$0000` | 1, 1 |
//   | `$06` | `$0000` | `$0000` | 1, 1 |
//
// `dir & 2` is bit 0 of the undoubled direction, so the **odd** directions —
// up, right, down, left — take the second row. Diagonals are the slower row,
// and 1.5 against 2 is a 0.75 that stands in for the 0.707 a real
// normalisation would want: cheap, one table lookup, and 6% too fast on the
// diagonal.
//
// A mask of `$FFFF` is "always", except that it is not: it is ANDed with a
// counter, and the counter is zero one frame in 65,536. So about once every
// eighteen minutes of play the fastest thing in the game takes a single
// half-speed step. Nothing depends on it and nobody would see it, but it is
// there, and the port reproduces it because reproducing it is free.
//
// ## The tether
//
// `$80:A8B3` is the co-op leash, and it is the only movement test that reads
// state belonging to somebody other than the mover. `W_PLAYER_A_TASK` is the
// task id `$80:A8A4` recorded when player A's record was registered, so
// comparing it with `W_SCHED_CUR_TASK` is how the routine asks *which player am
// I* — and the answer selects **the other one's** record as the reference.
//
// A candidate is allowed if it is inside a window around that reference:
// `-$E0 <= dx < $E0` and `-$B0 <= dy < $B0`, so 224 by 176 pixels either way,
// a little under two screens. Outside the window there is a second chance, and
// it is the humane one: the candidate is allowed anyway if the **Manhattan
// distance from the mover to the reference** is less than the two players'
// current separation — that is, if the step is bringing them back together.
// You can always walk toward your partner; you can only walk away until the
// leash runs out.
//
// The `PLD` at both exits means N and Z are the caller's direct page, the trap
// `actor_nearest` cost four movies to learn. The carry is the answer, set
// meaning **blocked**, the same convention as the other three tests.
//
// Port code: libc only.

#ifndef PORT_STEP_H
#define PORT_STEP_H

#include <stdbool.h>
#include <stdint.h>

#include "assets/rom.h"
#include "port/wram.h"

// --- $80:E450 ---------------------------------------------------------------

#define STEP_PROPOSE_ENTRY 0x80e450u

// The two nine-word delta tables and the eight-word speed mask table, all read
// through the data bank, which is `$80` for the whole of the mover's thread.
#define STEP_DELTA_X 0x80e486u
#define STEP_DELTA_Y 0x80e498u
#define STEP_SPEED_MASKS 0x80e4aau
#define STEP_DIR_COUNT 9

// `$80:E453  AND #$0002` — bit 0 of the undoubled direction, which is the
// cardinals. `<< 2` turns it into the second row of four words.
#define STEP_DIR_CARDINAL 0x0002

// Fields on the mover's own direct page. `$30`/`$32` are where it is; `$34`/
// `$36` are where it would like to be, and `$80:E4C1` copies them back one axis
// at a time.
#define STEP_DP_DIR 0x24
#define STEP_DP_X 0x30
#define STEP_DP_Y 0x32
#define STEP_DP_NEXT_X 0x34
#define STEP_DP_NEXT_Y 0x36
// The speed class, already doubled — `$80:E459  ADC $76` adds it to a byte
// offset, so it is `$00`, `$02`, `$04` or `$06`.
#define STEP_DP_SPEED_CLASS 0x76

typedef struct {
  uint16_t a, x, y;
  bool n, z, c;
} StepProposeRegs;

// `$34`/`$36` = `$30`/`$32` plus this frame's delta. `dp` is the mover's page.
void step_propose(Wram* w, const Rom* rom, uint16_t dp, StepProposeRegs* out);

// --- $80:A8B3 ---------------------------------------------------------------

#define TETHER_ENTRY 0x80a8b3u

// The window around the other player, as the ROM writes it: bias, then an
// unsigned compare against the span. `$80:A8D2  ADC #$00E0 : CMP #$01C0`.
#define TETHER_BIAS_X 0x00e0
#define TETHER_SPAN_X 0x01c0
#define TETHER_BIAS_Y 0x00b0
#define TETHER_SPAN_Y 0x0160

// The routine pins its own direct page to zero and uses two scalars there. It
// leaves both behind, and what they hold depends on which exit it took, so the
// port has to write them in the same order for the same reasons.
#define TETHER_DP_Y 0x38  // ...and, on the far path, |Ax - Bx|
#define TETHER_DP_X 0x3a  // ...and, on the far path, the candidate's distance

typedef struct {
  uint16_t a, x, y;
  bool blocked;
} TetherRegs;

// `x`/`y` are the candidate position, passed in X and Y exactly as `$80:E4D3`
// loads them from `$34`/`$32`.
void step_tether_blocked(Wram* w, uint16_t x, uint16_t y, TetherRegs* out);

#endif
