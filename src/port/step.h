// Where a mover wants to be next, the rules that are about the other player
// rather than about the board, and where a move ends up once it is allowed.
//
//   $80:E450  step_propose        direction + speed -> a candidate position
//   $80:A8B3  step_tether_blocked ...is that candidate too far from the other
//                                 player to be allowed?
//   $80:AFFB  partner_near        the same question with the leash taken off
//   $80:F327  actor_publish_pos   ...and the committed position, told to the
//                                 actor record so something can draw it
//   $81:8024  nearest_player_dist how far a point is from the nearer player
//   $81:9BF3  actor_step_bearing  ...and all of the above at once, for something
//                                 that is not a player
//
// The first two are the two ends of `$80:E4C1`, the movement step validator. It
// opens with `JSR $E450` to work out where the mover is trying to go, then puts
// that candidate through four tests — `terrain_blocked`, this tether,
// `actor_obstacle_at_point` and `terrain_out_of_bounds` — and commits it only
// if all four agree (`$80:E4FF  LDA $34 : STA $30`). Then it does the whole
// thing again for the other axis, which is why a mover slides along a wall
// instead of stopping dead against it.
//
// With those two the validator is complete: `port/terrain.h` has two of the
// tests, `port/oam.h` has the third, this file has the fourth and the proposer.
// Only `$80:E4C1` itself — the sequencing — is still the ROM's.
//
// And `$80:E4FF` commits to `$30`/`$32`, which are the *thread's* opinion and
// not anything the screen knows about. `actor_publish_pos` is what carries that
// into the actor record, so it is the far end of the same pipeline: propose,
// vet, commit, publish.
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
  bool v;  // from the last `ADC`, y's
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
  // Overflow, from the last `ADC` the path ran: `ADC #$00B0` inside the
  // window, `ADC $38` on the far path. The one-player exit runs none and
  // leaves the caller's, which `v_set` says.
  bool v_set, v;
} TetherRegs;

// `x`/`y` are the candidate position, passed in X and Y exactly as `$80:E4D3`
// loads them from `$34`/`$32`.
void step_tether_blocked(Wram* w, uint16_t x, uint16_t y, TetherRegs* out);

// --- $80:AFFB  partner_near — A = my record, X/Y = a point; carry = close ----
//
// **The other test about the other player**, and the second routine in the game
// to reach for `$38` as scratch for exactly this. It sits in the middle of the
// terrain-attribute routines in `port/terrain.h`, in the ROM and nowhere else:
// it is `$80:A8B3`'s shape with the leash taken off.
//
//   * the caller passes its **own record** in A rather than being identified by
//     its task id, so this works for anything, not only for a player thread;
//   * the window is a square `$80` on a side tested from the point, with no
//     bias and no second chance;
//   * carry **set** means near, which is the opposite of every other carry in
//     this file — there is no "blocked" here, only a question.
//
// ## One call site, and it is the other half of `terrain_tile_bit3`
//
// `$80:E056` is inside `$80:E035`, a state body that nothing installs except
// `$80:E021` — and `$80:E021` is what `$80:E861` jumps to when
// `terrain_tile_bit3` says the tile the mover just stepped onto carries bit 3.
// The two are the two ends of one mechanic, and the corpus measures the funnel
// between them: 426 calls to the tile test, and **three** to this. In order:
//
//   1. a mover commits a step, and `$80:E855` re-divides its new position and
//      asks `terrain_tile_bit3` about the tile it landed on;
//   2. bit 3 set, so `$80:E021` installs `$80:E035` with `$16 = 10` — ten
//      frames of nothing;
//   3. `$80:E035` runs, and asks **this** routine about a point `$E06C` puts
//      **100 pixels ahead in the facing direction**, which is a long way
//      further than a step;
//   4. near, and the mover enters state `$06` with the direction saved in
//      `$4A`; far, and it falls through to `$80:E090` as though nothing had
//      been asked.
//
// A hundred pixels forward, gated on the other player still being within
// eighty of where that lands: this is a jump the co-op leash gets to veto
// before it happens, rather than the leash catching a step after the fact.
// What the fiction calls it the port cannot say.
//
// **The veto is only real in two-player**, because with `$D4` zero the `BIT`
// below answers *near* and a lone player always goes — and that is not a
// theoretical remark. All three corpus calls take exactly that exit, eleven
// instructions in, measuring nothing. Six of this routine's seven coverage
// sites are untaken for that reason: the corpus has five two-player movies and
// not one of them has ever stood on a bit 3 tile. That is a gap with a name and
// a recipe, which is a better thing for the untaken list to be carrying than
// silence.
//
// ## `BIT $00D2` is not the test it looks like
//
// The routine opens by checking that both players exist, and writes it as
// `BIT $00D2 : BEQ` — which on the 65816 sets Z from **A AND memory**, not from
// memory. A is the caller's record pointer, so what is really being asked is
// whether the caller's record and player A's record have a bit in common.
//
// It works, and it works by an accident of where the records live.
// `W_ACTOR_SLOTS` is `$185E` and there are 32 slots of `$14` bytes, so every
// record pointer in the game lies in `$185E..$1ACA` — and every address in that
// range has bits 11 and 12 set. Any two of them therefore share at least
// `$1800`, and the `AND` can only come out zero when one side is the `$0000`
// that means *no such player*. Move the slot table to a page that does not
// straddle `$1800` and the check silently stops working; nothing in the ROM
// says so, and nothing in the game would notice until two players existed.
//
// The port reproduces the `AND`, not the intent.
#define PARTNER_NEAR_ENTRY 0x80affbu

// `CMP #$0080 : BCS` on each axis, unsigned, against an absolute difference.
#define PARTNER_NEAR_SPAN 0x0080

// `STY $0038` — the same scalar `TETHER_DP_Y` names, borrowed by the same trick
// of keeping the Y argument somewhere the accumulator can reach it.
#define PARTNER_DP_Y 0x38

// **No `PHD`**, so N and Z are not the caller's direct page: they come from
// whichever instruction decided, and there are two kinds. The two "no such
// player" exits leave the `BIT`'s flags — `Z` set by construction, `N` from bit
// 15 of the *record pointer in WRAM*, which is a record pointer and so always
// clear. The three distance exits leave a `CMP`'s.
typedef struct {
  uint16_t a, x, y;
  bool n, z;
  bool c;  // set means near — or that there is nobody to be near
} PartnerRegs;

// `rec` is A, the caller's own record; `x`/`y` are the point being asked about,
// which need not be where that record is.
void partner_near(Wram* w, uint16_t rec, uint16_t x, uint16_t y,
                  PartnerRegs* out);

// --- $80:F327  actor_publish_pos — the last thing an actor's frame does ------
//
// **The other end of the pipeline this file opens.** `step_propose` turns a
// direction into a candidate, four tests vet it, and `$80:E4FF  LDA $34 : STA
// $30` commits it to `$30`/`$32` — which are the *thread's* idea of where it
// is, on its own direct page, and are not what anything draws. This is the
// routine that copies them into the actor record, and until it runs the mover
// has moved only in its own opinion.
//
// It sits at the bottom of `$80:CDF4`, the generic actor body:
//
//     JSR $D13A : LDA #$0001 : JSL thread_yield : JSR $D1EA : JSR $D01B
//     <call the state handler in $28> : <and the one in $2A, if any>
//     JSR $F327
//
// — so the order of a frame is *sleep, think, publish*, and every actor built
// on that body publishes through here. 31,971 calls over eleven movies from a
// single `JSR`, which is the highest call count of any one-caller routine in
// the registry.
//
// ## Two records, and the second one is not a copy
//
// `$1E` non-zero means the actor is drawn as **two stacked records** — `$08` is
// the lower and `$0A` the upper — and the second is placed rather than
// duplicated:
//
//   | field | lower (`$08`) | upper (`$0A`) |
//   | --- | --- | --- |
//   | `ACTOR_X` | `$30` | `$30` |
//   | `ACTOR_Y` | `$32` | `$32 + 1` |
//   | `ACTOR_Z` | untouched | *lower's* `ACTOR_Z` − 1 |
//
// One pixel down and one Z in front. The Y is a nudge that closes the seam
// between two metasprites that ought to abut; the Z is what stops the depth
// sort from ever separating them, because a strictly smaller Z is a tie that
// cannot be broken the wrong way. And note where the upper record's Z comes
// from: it is **read back out of the lower record**, not out of the thread, so
// whatever else moved the lower one this frame is what the upper one follows.
//
// The single-record path uses `$08` as an *index* (`LDX $08 : STA $0002,X`) and
// the two-record path uses it as a *pointer* (`STA ($08),Y`). Same word, same
// address, two addressing modes — which is free on the 65816 because a record
// pointer is a bank-zero address either way, and is the reason the two paths do
// not share a line of code.
//
// ## The flags are two different instructions
//
// The single-record exit falls out after `LDA $32 : STA $0006,X`, and a store
// sets nothing, so N and Z describe **the Y that was just published**. The
// two-record exit falls out after `DEC A : STA ($0A),Y`, so they describe **the
// upper record's new Z**. Nothing reads either — the caller's next instruction
// is `LDA $1A` — and they are claimed anyway.
#define ACTOR_PUBLISH_POS_ENTRY 0x80f327u

// The actor's own record, and the second one when it has a second.
#define PUBLISH_DP_RECORD 0x08
#define PUBLISH_DP_SECOND 0x0a
// Non-zero when there are two. Only its zero-ness is read here; whatever else
// the value means belongs to whoever set it.
#define PUBLISH_DP_TWO_PART 0x1e

typedef struct {
  uint16_t a, x, y;
  bool n, z;
} PublishRegs;

// `dp` is the actor thread's direct page; everything else comes off it.
void actor_publish_pos(Wram* w, uint16_t dp, uint16_t in_x, uint16_t in_y,
                       PublishRegs* out);

// --- $81:8024  nearest_player_dist — Chebyshev, and it is a *search* key -----
//
// How far a point is from whichever player is closer, as
// `max(|dx|, |dy|)` — the Chebyshev metric, and the third distance in this file
// after `step_tether_blocked`'s Manhattan sum and `partner_near`'s pair of
// independent axis tests. Three routines, three metrics, none of them Euclidean
// and none of them agreeing with the others about which of two points is
// nearer.
//
// One caller, `$81:80EC actor_list_spawn`, 22,283 calls:
//
//     ...for each entry in the level's spawn list:
//        $16 = its X, $18 = its Y
//        JSR $8024 : CMP $14 : BCS skip
//        $14 = A, $12 = this entry
//
// — a linear minimum over every entry not currently on cooldown (the per-entry
// counter at `$7E:60DA`), so what the spawner picks is **the eligible spawn
// point closest to a player**. The list is a set of positions the level
// supplies; which of them is used is decided fresh on each spawn, by this
// routine, and never by the level. Whether that reads as enemies arriving from
// where you are looking is the fiction's business — the code says only
// *nearest*.
//
// The two scratch words are `$1E` and `$20`, one per player, and both are
// primed with `$FFFF`: an absent player is not skipped in the comparison, it is
// entered into it as the largest distance there is, which is why a one-player
// game falls straight through to A's answer without a branch anywhere. Neither
// word is cleaned up afterwards, so both survive the call and the port writes
// them for the same reason the ROM does.
//
// ## Where the flags come from, which is one instruction earlier than it looks
//
//     LDA $1E : CMP $20 : BCC out : LDA $20
//     out: RTS
//
// If the first player is nearer the routine exits on the `CMP`, so carry is
// *clear* and N and Z belong to a subtraction whose result was thrown away. If
// the second is nearer or they tie, the `LDA` overwrites N and Z but **not
// carry**, which the `CMP` left set. So carry is a real output — set means the
// answer came from player B's side of the compare, or from the tie — and no
// caller reads it.
#define NEAREST_PLAYER_ENTRY 0x818024u

// `LDA #$FFFF : STA $1E : STA $20` — an absent player's distance.
#define NEAREST_PLAYER_NONE 0xffff

// Prefixed to stay clear of `port/oam.h`'s `NEAREST_DP_*`, which belong to
// `$80:B123 actor_nearest` — a nearest-*actor* search, on direct page zero, and
// seeded with the same `$FFFF`. Two routines, the same idea, and neither knows
// about the other.
#define NEAREST_PLAYER_DP_X 0x16  // the point being asked about
#define NEAREST_PLAYER_DP_Y 0x18
#define NEAREST_PLAYER_DP_A 0x1e  // ...its distance to A, and then the answer
#define NEAREST_PLAYER_DP_B 0x20  // ...and to B

typedef struct {
  uint16_t a, x, y;
  bool n, z, c;
} NearestRegs;

// `x`/`y` are read off `dp` rather than passed, because the caller writes them
// there — this routine takes no register arguments at all.
void nearest_player_dist(Wram* w, uint16_t dp, uint16_t in_y, NearestRegs* out);

// --- $81:9BF3  actor_step_bearing — the whole validator, in one routine ------
//
// Move an actor one step in the direction A names, refusing each axis on its
// own, and publish the result to its record. It is `$80:E4C1` — the movement
// step validator this file's first two routines are the ends of — rewritten for
// something that is not a player: same shape, three tests where the player's
// has four, and the commit goes straight into the actor record instead of into
// `$30`/`$32`.
//
//     if (sched_tick & 3) == 0: return              ; a rest frame
//     $10 = $0C + dx ; $12 = $0E + dy               ; the candidate
//     if !blocked_enemy($10,$0E) && !out_of_bounds($10,$0E)
//                                && !at_point($10,$0E): $0C = $10
//     if !blocked_enemy($0C,$12) && !out_of_bounds($0C,$12)
//                                && !at_point($0C,$12): $0E = $12
//     record.X = $0C ; record.Y = $0E
//
// The second axis is tested against the **committed** `$0C`, not the original,
// so a step that gets its X is tested for its Y from where it now is. That is
// the same order `$80:E4C1` uses and it is what makes a diagonal into a corner
// slide along the wall it hits rather than stop dead in front of it.
//
// Three of the four things it calls are already the port's — `terrain_blocked_
// enemy`, `terrain_out_of_bounds`, `actor_at_point` — so what this adds is the
// sequencing, which was the last part of the pattern still running on the
// 65816 anywhere.
//
// ## Speed is a duty cycle, again, and the delta table is doubled
//
// `LDA $0020 : AND #$0003 : BEQ out` — one frame in four is a rest frame, on
// which nothing moves and nothing is even tested. The table's steps are **two**
// pixels per axis where `step_propose`'s are one, so this walks 2 px on three
// frames in four: 1.5 px a frame, and the same average as a player at speed
// class `$00` on a diagonal, reached from the other side.
//
// The table is `$81:9C62`, nine four-byte entries indexed by a direction
// already doubled — the same currency `step_propose`'s `$24` is in, and the
// same clockwise-from-up order:
//
//   | `A` | dx | dy | |
//   | --- | --- | --- | --- |
//   | `$00` |  0 |  0 | not moving |
//   | `$02` |  0 | -2 | up |
//   | `$04` | +2 | -2 | up-right |
//   | `$06` | +2 |  0 | right |
//   | `$08` | +2 | +2 | down-right |
//   | `$0A` |  0 | +2 | down |
//   | `$0C` | -2 | +2 | down-left |
//   | `$0E` | -2 |  0 | left |
//   | `$10` | -2 | -2 | up-left |
//
// `ASL : TAX` on entry doubles it again, because the entries are four bytes and
// the argument counts in twos. There is no range check: the tenth entry is the
// two words that follow the table and nothing prevents a caller reaching them.
// Both callers — `$81:9DAA` and `$81:9E42` — arrive from `actor_bearing`, which
// cannot return more than eight, so the guard is upstream and implicit.
//
// ## What comes back
//
// A is the committed Y, from the `LDA $0E` two instructions before the `RTS`,
// and Y is the record. **X is whichever of the three tests ended the second
// axis** and so is a different routine's leftover on each path — one of
// `terrain_blocked_enemy`'s two outputs, or the candidate itself when
// `terrain_out_of_bounds` (which touches neither index register) was the one
// that refused, or `actor_at_point`'s. It is claimed rather than skipped
// because all three are ported and all three model it, so chaining them costs
// nothing.
//
// **Carry is the second axis's verdict — except on a rest frame, where it is
// the routine's first instruction.** `ASL` writes carry from bit 15 of the
// doubled direction, which is always clear because a direction is at most
// eight, and nothing on the rest path touches carry afterwards. So the routine
// returns `C = 0` there rather than whatever the caller arrived with. Modelling
// that as a passthrough is wrong on one call in four, and the harness said so
// at call 564 of `level21-bubble` with A, X, Y, N and Z all matching.
#define STEP_BEARING_ENTRY 0x819bf3u

// Nine entries of `{i16 dx, i16 dy}`.
#define STEP_BEARING_TABLE 0x819c62u
// `AND #$0003` against the low word of `W_SCHED_TICK`.
#define STEP_BEARING_REST_MASK 0x0003

#define BEARING_DP_RECORD 0x08  // the actor record the result is published to
#define BEARING_DP_X 0x0c       // the thread's own position, and the commit
#define BEARING_DP_Y 0x0e
#define BEARING_DP_TRY_X 0x10  // the candidate, left behind for nobody
#define BEARING_DP_TRY_Y 0x12

typedef struct {
  uint16_t a, x, y;
  bool n, z, c;
} StepBearingRegs;

// `dir2` is A on entry: a direction already doubled. `in_y` answers for the
// rest-frame exit, which happens after the `ASL : TAX` and returns the caller's
// Y untouched. Nothing here takes the caller's carry, for the reason above.
void actor_step_bearing(Wram* w, const Rom* rom, uint16_t dp, uint16_t dir2,
                        uint16_t in_y, StepBearingRegs* out);

#endif
