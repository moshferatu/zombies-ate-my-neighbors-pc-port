// The big figure's mover: how a boss too large for sprites walks into a wall.
//
//   $82:8F93  boss_step   0.4%   one step of the big figure, terrain and all
//
// `port/bossbg.h` draws the figure and `port/collide.h` lets it be shot. This
// is the third side of the same object: where it is allowed to be. The position
// it moves is not in an actor record at all -- big figures keep theirs in two
// fixed words, `$1E62` and `$1E64`, with the draw offsets `$82:892E` derives
// from them in the two words after -- and eight routines across banks `$82` and
// `$83` write that pair, so this slot belongs to whichever oversized thing the
// level has rather than to level 25's baby in particular.
//
// ## Not `step_propose`
//
// `$80:E450` proposes a destination and leaves somebody else to accept it. This
// routine proposes, tests and **commits**, all three, and it commits the two
// axes separately -- which is the whole point of it.
//
// A direction in `$16` picks an eight-byte record from `$82:906F`: the delta,
// the facing flag to leave in `$36`, and a loop count that is `$0008` for the
// four diagonals and `$0000` for the four cardinals. The loop runs once per
// axis the direction actually uses, and each pass tests one edge of the
// figure's footprint and commits that axis alone if the edge is clear. So a
// boss walking north-east into a north wall still goes east. **Sliding along a
// wall is not a special case here; it is what falling out of one of two
// independent passes looks like.**
//
// ## The footprint is two probes, not a rectangle
//
// The second table, at `$82:9035`, holds four signed offsets per entry: two
// points, tested with `$82:90F7 terrain_blocked_wide`, and both must be clear.
//
//     north   (-18,  0) (18,  0)      the top corners
//     east    ( 18,  4) (18, 20)      the right edge
//     south   (-18, 18) (18, 18)      the bottom corners
//     west    (-18,  4) (-18, 20)     the left edge
//
// The origin is the top centre of something 36 wide and about 20 tall. Note
// that the vertical pairs span y 0..18 and the horizontal pairs span y 4..20 --
// the box the game tests is not quite the same box in both directions, and
// nothing rounds it off. Four probes would have been a rectangle; two are a
// leading edge, which is all a mover needs and half the terrain lookups.
//
// The table has **five** entries for four directions, and the fifth repeats the
// first. That is not padding: the index is a base from `$82:905D` plus the loop
// counter, and north-west's base is the last one, so its diagonal pass has to
// find north at the end of the table rather than by wrapping to the start.
//
// And the axis a pass commits is **bit 3 of that same index** -- `$0A AND
// #$0008` -- which works only because the entries are eight bytes and alternate
// vertical, horizontal, vertical, horizontal. One table, indexed once, decides
// both which two points to test and which coordinate to write.
//
// ## `#$6969` in A means double speed
//
// The caller's argument is a magic word, not a flag: `CMP #$6969` and nothing
// else. When it matches, `$40` is added to the record index, which selects the
// second half of the same table -- the same eight directions with the deltas
// doubled -- and `$2C` is decremented.
//
// **Nothing in the boss's own thread reads that counter back.** Bank `$82` has
// five instructions that read direct-page `$2C`: two inside the blitters in
// `port/bossbg.h`, which store to it before every use, two at `$82:A494` and
// `$82:A517` that are seeded by a store four instructions earlier, and one at
// `$82:EF49` -- `LDA $2C : JSL $80:8353`, a yield count -- in a routine nowhere
// near the boss and with a direct page this routine never sets.
//
// So the write is *probably* dead and the port does not claim it is. It
// reproduces it, because the direct page is the caller's and the harness
// compares 128 KB of WRAM after every call, which settles the question without
// anyone having to answer it.
//
// ## Carry means it is stuck
//
// The routine ends by comparing both coordinates against the copies it saved on
// the way in and setting the carry only if **neither** moved. Six of its eight
// call sites branch on that, and three of them -- `$82:8A60`, `$8A68`, `$8A70`
// -- are the same double step written out three times in a row, stopping at the
// first that gets nowhere. When it does get nowhere the caller reaches for the
// RNG and picks a different direction, so this carry is the entire reason a
// cornered boss does not simply stand there grinding against a wall.
//
// ## Index zero is the ROM's own guard
//
// `$16` is a direction times eight and both tables are sized exactly for
// `$00..$40`, with nothing between the last entry and `terrain_blocked_wide`'s
// first instruction. A direction out of range would read code as coordinates.
// The port does not add a check, because entry zero of the delta table is four
// zero words -- no movement, no facing change, one pass, carry set -- which is
// a working "stay put" and reads like the intended floor rather than an
// accident. Anything above `$40` the port reads exactly where the ROM would.
//
// Port code: libc only.

#ifndef PORT_BOSS_H
#define PORT_BOSS_H

#include <stdbool.h>
#include <stdint.h>

#include "assets/rom.h"
#include "port/wram.h"

#define BOSS_STEP_ENTRY 0x828f93u

// The big figure's position is `W_BOSS_X`/`W_BOSS_Y` in `port/wram.h`. The
// routine reaches it as absolute `$1E62`, not direct page, through a data bank
// of `$82` whose low 8 KB is the WRAM mirror.

// Direct page.
#define BOSS_STEP_DP_PROBE_BASE 0x08u  // the direction's entry in $82:9035
#define BOSS_STEP_DP_PROBE 0x0au       // ...plus the pass counter: this pass's
#define BOSS_STEP_DP_TRY_X 0x0cu       // where the step would land
#define BOSS_STEP_DP_TRY_Y 0x0eu
#define BOSS_STEP_DP_WAS_X 0x10u  // where it started, kept for the final compare
#define BOSS_STEP_DP_WAS_Y 0x12u
#define BOSS_STEP_DP_DIR 0x16u     // direction * 8, in $00..$40
#define BOSS_STEP_DP_PASS 0x18u    // $0008 for a diagonal, $0000 for a cardinal
#define BOSS_STEP_DP_TICK 0x2cu    // decremented on a double step; see above
#define BOSS_STEP_DP_FACING 0x36u  // the mirror flag `$82:892E` reads

// The word in A that asks for a double step, and what it adds to the index.
#define BOSS_STEP_FAST 0x6969u
#define BOSS_STEP_FAST_BIAS 0x0040u

// The three tables, all in bank $82 and all contiguous: probes, then the bases
// that index them, then the deltas, then `terrain_blocked_wide` itself.
#define BOSS_STEP_PROBES 0x9035u  // 5 entries x (dx1,dy1,dx2,dy2)
#define BOSS_STEP_BASES 0x905du   // 9 words, indexed by $16 >> 2
#define BOSS_STEP_DELTAS 0x82906fu  // 17 records x (dx,dy,facing,passes)
#define BOSS_STEP_TABLE_BANK 0x820000u

// Bit 3 of the probe index: set means this pass commits X, clear means Y.
#define BOSS_STEP_AXIS_X 0x0008u

// A is the coordinate the final compare loaded, and X and Y are whatever
// `terrain_blocked_wide` left on the last probe the routine ran -- there is
// always at least one, because the pass loop tests its counter at the bottom.
typedef struct {
  uint16_t a, x, y;
  bool n, z, c;  // c set: neither coordinate moved
} BossStepRegs;

// `$82:8F93` -- move the big figure one step in direction `$16`, or find out
// that it cannot. `a_in` is `BOSS_STEP_FAST` for a double step and anything
// else for a single one.
void boss_step(Wram* w, const Rom* rom, uint16_t dp, uint16_t a_in,
               BossStepRegs* out);

#endif
