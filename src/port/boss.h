// The big figure's mover: how a boss too large for sprites walks into a wall,
// and the two things its thread does with the position afterwards.
//
//   $82:8F93  boss_step          0.4%   one step of the big figure, terrain
//   $82:9265  boss_place_parts   0.1%   the four sub-records dragged after it
//   $82:92D6  boss_stomp         0.1%   ...and what it does to anything under it
//
// `port/bossbg.h` draws the figure and `port/collide.h` lets it be shot. This
// is the third side of the same object: where it is allowed to be. The position
// it moves is not in an actor record at all -- big figures keep theirs in two
// fixed words, `$1E62` and `$1E64`, with the draw offsets `$82:892E` derives
// from them in the two words after -- and eight routines across banks `$82` and
// `$83` write that pair, so this slot belongs to whichever oversized thing the
// level has rather than to level 25's baby in particular.
//
// The other two arrived here rather than in a file of their own because they
// are the same object seen from the same two words. `boss_step` writes `$1E62`
// and `$1E64` and the mirror flag `$36`; `boss_place_parts` reads all three and
// `boss_stomp` reads the first two. One thread runs the three of them in that
// order, once a frame, and the loop is four instructions long:
//
//     $82:958C  JSR $9265 : JSR $92D6 : JSR $93C6
//     $82:9595  LDA #$0001 : JSL thread_yield
//
// That thread, and the states that decide which way it steps, are
// `port/boss_thread.h`.
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
#include "port/oam.h"  // ActorNotifyWork: `boss_stomp` is priced by the box's table
#include "port/terrain.h"  // TerrainWideWork: and `boss_step` by the probe's
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

// --- What a step costs ------------------------------------------------------
//
// **Almost none of this is the routine.** Sixty-odd instructions of table
// lookups and additions, and then between one and four calls to
// `terrain_blocked_wide`, which is the most expensive leaf in the registry.
// A step that runs both passes and finds all four probes clear spends 12,592
// of its 15,136 cycles inside the leaf -- 83% of the call in a routine that is
// not, itself, doing anything expensive.
//
// So this table is thirteen blocks of arithmetic plus a `TerrainWideWork` that
// the probes accumulate into, and the two are added at the end. The `JSR`s live
// here (in `PASS_HEAD` and `LEAD_CLEAR`) and the matching `RTS`s live in the
// leaf's own exit blocks, which is where they are already paid for.
typedef enum {
  BOSS_BLK_SINGLE,        // $82:8F98 BNE taken: A was not #$6969
  BOSS_BLK_DOUBLE,        // ...or it was, so the index moves $40 on and $2C
                          // is decremented -- a read-modify-write at 50
  BOSS_BLK_SETUP,         // $82:8FA2-$82:8FD4: four table words and the base
  BOSS_BLK_PASS_HEAD,     // $82:8FD6-$82:8FEC: the leading probe, and its JSR
  BOSS_BLK_LEAD_BLOCKED,  // $82:8FEF BCS taken: the axis is refused here
  BOSS_BLK_LEAD_CLEAR,    // ...or not, and the trailing probe follows
  BOSS_BLK_TRAIL_BLOCKED, // $82:9004 BCS taken: one corner fits, one does not
  BOSS_BLK_COMMIT_X,      // bit 3 of $0A set, so $1E62 is written
  BOSS_BLK_COMMIT_Y,      // ...or clear, and $1E64 is
  BOSS_BLK_LOOP_NEXT,     // $82:9021 BPL taken: a diagonal's second pass
  BOSS_BLK_LOOP_DONE,     // ...or the counter went negative
  BOSS_BLK_EXIT_X,        // $82:9028 BNE taken: X moved
  BOSS_BLK_EXIT_Y,        // $82:902F BNE taken: X held and Y moved
  BOSS_BLK_EXIT_STUCK,    // ...neither, which is the SEC the caller reads
  BOSS_STEP_BLOCK_COUNT,
} BossStepBlock;

// The arithmetic and the probes, kept apart because they are priced from two
// different listings and only added up at the seam.
//
//     SINGLE + DOUBLE == 1,  SETUP == 1,  LOOP_DONE == 1
//     PASS_HEAD == LOOP_NEXT + LOOP_DONE == the passes run (1 or 2)
//     LEAD_BLOCKED + LEAD_CLEAR == PASS_HEAD
//     TRAIL_BLOCKED + COMMIT_X + COMMIT_Y == LEAD_CLEAR
//     EXIT_X + EXIT_Y + EXIT_STUCK == 1
//     probes.blocks[WIDE_BLK_PROLOGUE] == PASS_HEAD + LEAD_CLEAR
//
// That last one is the only cross-check in this port that ties two separately
// derived tables together by a count rather than by a cost, and it is the one
// that would catch a `JSR` counted in the wrong block.
typedef struct {
  uint16_t blocks[BOSS_STEP_BLOCK_COUNT];
  TerrainWideWork probes;
} BossStepWork;

void boss_step_counted(Wram* w, const Rom* rom, uint16_t dp, uint16_t a_in,
                       BossStepRegs* out, BossStepWork* work);

// --- $82:9265  boss_place_parts ---------------------------------------------
//
// **A figure that has no sprite of its own still needs somewhere to be hit.**
//
// The big figure is drawn out of background tiles by `port/bossbg.h`, so there
// is no actor record under it and nothing for `actor_overlap_pass` to find. The
// thread's opening sequence buys four records instead --
//
//     $82:9531  JSR $94B4 : STX $24 : JSR $94B4 : STX $26
//     $82:953B  JSR $94B4 : STX $28 : JSR $94B4 : STX $2A
//
// -- and this routine is what keeps them under the drawing: once a frame it
// writes `ACTOR_X` and `ACTOR_Y` of all four from `W_BOSS_X`/`W_BOSS_Y` plus a
// fixed offset. Nothing else about the four is touched here; they are hitboxes
// on a string.
//
// ## The offsets are a diagonal, and only X is mirrored
//
// X comes from a table and Y is four constants written into the code:
//
//     part    dx (facing east)   dx (facing west)   dy
//     $24         -14                 +14           -6
//     $26          -4                  +4            0
//     $28         +10                 -10           +4
//     $2A         +20                 -20          +12
//
// So the four sit on a line running down and to the right of the figure's
// origin, and the mirror flips that line about the vertical without touching
// the heights -- which is what `boss_step` writing `$36` is for and the only
// thing this routine reads out of direct page besides the four record pointers.
// The X table holds both halves back to back and the selector is the index
// `LDY #$0000` or `LDY #$0008`, so **"mirrored" is a second table rather than a
// negation**. The eight entries do happen to be four exact negative pairs, and
// the port still reads all eight: a hack that gave the figure a lopsided reach
// by editing four of those bytes would keep working.
//
// **`W_BOSS_Y` is read four separate times**, once per part, and the port keeps
// all four rather than hoisting the load. Whether that could make a difference
// is not the question a transcription answers: `LDA $1E64` appears four times
// in the ROM, so it appears four times here, and the harness comparing 128 KB
// after every call is what settles whether it ever mattered.
#define BOSS_PLACE_PARTS_ENTRY 0x829265u

// Four words of X offset per facing, the two facings back to back. Bank `$82`,
// reached the same absolute-indexed way `BOSS_STEP_PROBES` is.
#define BOSS_PARTS_TABLE 0x92c6u
// `LDY #$0008` -- what `$36` selects when it is not zero.
#define BOSS_PARTS_MIRROR 0x0008u

// The four record pointers, on the boss thread's own direct page.
#define BOSS_PARTS_DP_FIRST 0x24u
#define BOSS_PARTS_DP_STRIDE 0x02u
#define BOSS_PARTS_COUNT 4

// A is the last Y written and N, Z and C are the `ADC` that produced it -- so
// they describe part `$2A`'s vertical position and nothing else. X is that same
// last record, left over from `LDX $2A`, and Y is the table selector. V is that
// `ADC`'s too and is not claimed; the single caller's next instruction is
// another `JSR`.
typedef struct {
  uint16_t a, x, y;
  bool n, z, c;
} BossPartsRegs;

// `$82:9265` -- drag the four hitboxes to where the figure now is.
void boss_place_parts(Wram* w, const Rom* rom, uint16_t dp, BossPartsRegs* out);

// --- $82:92D6  boss_stomp ---------------------------------------------------
//
// Five words and a `JSL`: the box under the big figure, handed to
// `actor_notify_box`. This is how a thing with no sprite hurts what it walks
// over, and it fires every frame the boss's thread runs -- not on contact, not
// on a timer, so anything standing inside the rectangle is told once a frame
// for as long as it stands there.
//
// ## The box is not centred on the figure
//
//     x0 = W_BOSS_X - 34        x1 = x0 + 56    ->  -34 .. +22
//     y0 = W_BOSS_Y -  8        y1 = y0 + 32    ->   -8 .. +24
//
// `boss_step` describes the origin as the top centre of a footprint 36 wide and
// about 20 tall, which puts the walked-on box six pixels left of the body it
// belongs to and standing eight pixels proud of the top of it. The port
// reproduces the numbers; why they are those numbers is not recorded anywhere
// and is not guessed at here.
//
// Both far edges are derived from the near ones rather than from the position:
// `SBC #$0022` and then `ADC #$0038` **to that result**, which is a width and
// not a second offset. Each pair clears the carry before the addition
// (`$82:92E0`, `$82:92F1`), so no borrow out of the subtraction reaches it.
//
// A figure standing within 34 pixels of `x = 0` therefore stores a near edge
// that has wrapped past `$FFFF`, and the far edge derived from it has not --
// `$FFF0` and `$0028`. What happens next is `actor_notify_box`'s business, and
// what it does is zero the near edge because bit 15 is set. The port makes no
// attempt to be tidier: same arithmetic, same width, same wrap.
#define BOSS_STOMP_ENTRY 0x8292d6u

#define BOSS_STOMP_LEFT 0x0022u    // `SBC #$0022` from W_BOSS_X
#define BOSS_STOMP_WIDTH 0x0038u   // `ADC #$0038` onto that
#define BOSS_STOMP_TOP 0x0008u     // `SBC #$0008` from W_BOSS_Y
#define BOSS_STOMP_HEIGHT 0x0020u  // `ADC #$0020` onto that
// `LDA #$000A : STA $0040` -- the id every victim's handler is entered with,
// and, per `actor_notify_box`, the one id inside the box that is skipped.
#define BOSS_STOMP_ID 0x000au

// Whatever `actor_notify_box` left. It is a `JSL` immediately before the `RTS`,
// so there is nothing of this routine's own in any of them.
typedef struct {
  uint16_t a, x, y;
  bool c;
} BossStompRegs;

// **Neither of `actor_notify_box`'s two register inputs is this routine's
// caller's.** Nothing arrives here in a register at all, and both of the values
// the `JSL` passes on are made three instructions earlier:
//
//   * A is `BOSS_STOMP_ID`, still in the accumulator from the `LDA #$000A` that
//     wrote `$40` -- so the id is handed over twice, once in memory and once in
//     a register, and the callee reads both;
//   * carry is the `CLC : ADC #$0020` that produced the bottom edge, which is
//     set only when that addition wrapped 16 bits.
//
// So a shim for this routine must not pass `in->a` and `in->c` through. Both
// are recomputed here, which is why neither function below takes them.
//
// True if every actor in the box has a handler the port has. Same decline as
// `actor_notify_box`, inherited whole; see `port/oam.h`.
bool boss_stomp_supported(Wram* scratch, const Rom* rom);

// `$82:92D6` -- set the box and tell everything in it. Returns false on the
// decline, having possibly told some of them, exactly as `actor_notify_box`
// does and for the same reason.
bool boss_stomp(Wram* w, const Rom* rom, BossStompRegs* out);

// ...and what it did, which is *entirely* `actor_notify_box`'s: five stores and
// a `JSL`, so there is no block table of this routine's own and no enum here.
// The five stores are a constant the cost model adds, and everything that
// varies call to call varies inside the callee.
//
// This is the second registry entry priced by a table it does not own -- the
// first being the three blocks of `wave_hdma_build` that pay for `sin_deg` --
// and it is not a duplicate for the same reason: when `$82:92D6` is
// substituted its `JSL` never happens, so `actor_notify_box`'s own shim is
// never entered on that call and its entry prices nothing.
bool boss_stomp_counted(Wram* w, const Rom* rom, BossStompRegs* out,
                        ActorNotifyWork* work);

#endif
