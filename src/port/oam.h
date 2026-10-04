// The per-frame sprite pass: the display list, and the OAM buffer it fills.
//
// `sprite_build_oam` (`$80:BD1F`) is what the scheduler runs once per pass, and
// it is the routine the whole sprite path hangs off. It opens with three list
// routines, walks whatever survived them, hands each survivor to one of the four
// emitters Phase 2 already ported (`src/assets/sprite.c`), and finishes with a
// pairwise overlap test. All five are here:
//
//   $80:BC7F  actor_depth_sort    one bubble pass over the display list
//   $80:BCE2  actor_cull          list -> visible_actors[], by camera window
//   $80:BC23  oam_buffer_clear    park all 128 OAM entries off screen
//   $80:BEC9  actor_overlap_pass  tell touching pairs about each other
//   $80:BD1F  sprite_build_oam    the pass itself
//
// ...and, at the end of the file, the two routines that put records on the list
// and take them off — `$80:BE0C actor_slot_alloc` and `$80:BE41 actor_slot_free`,
// 143 call sites between them.
//
// Between them they read and write nothing but WRAM and the metasprites in ROM,
// and none of them yields — the pass runs from `scheduler_idle`'s housekeeping,
// not from a thread. Each runs 1,016 times in `movies/level1.zmv`.
//
// One thing here is deliberately unfinished, and it is marked as such rather
// than hidden: when the overlap pass finds a pair it dispatches into the two
// actors' handlers, which is game logic nobody has ported. See
// `actor_overlap_pass` below.
//
// ## The display list
//
// The game keeps **32 records of 20 bytes at `$7E:185E`** — allocated by
// `$80:BE0C`, freed by `$80:BE41`, all 32 cleared by `$80:BDF3` — and chains the
// live ones into a singly-linked list whose head is `$7E:1B5E`. A record is
// addressed by its own WRAM offset: `$12,X` is the link to the next one, and 0
// terminates. Every routine here walks that list.
//
// Only the fields this pass reads are named below. The rest of a record belongs
// to the actor logic, which is not ported yet.
//
// Port code: libc only.

#ifndef PORT_OAM_H
#define PORT_OAM_H

#include <stdbool.h>
#include <stdint.h>

#include "assets/rom.h"
#include "assets/sprite.h"
#include "port/collide.h"
#include "port/sprite_cache.h"
#include "port/wram.h"

// --- A display-list record --------------------------------------------------

#define ACTOR_SLOT_COUNT 32
#define ACTOR_SLOT_STRIDE 0x14

#define ACTOR_FLAGS 0x00  // see the bits below
#define ACTOR_X 0x02      // world X, or screen X when ACTOR_SCREEN_SPACE
// Height off the ground, subtracted from Y on the way to the screen ($80:BD84).
// That is the whole of ZAMN's third dimension: an actor that jumps or is thrown
// keeps its Y — and so its depth-sort order and its collision box — and only
// draws higher up.
#define ACTOR_Z 0x04
#define ACTOR_Y 0x06      // world Y, likewise
#define ACTOR_META 0x08       // metasprite pointer, low 16 bits
#define ACTOR_META_BANK 0x0a  // ...and its bank, which must be $8F or $90
// OAM attribute bits this actor forces on, when ACTOR_ATTR_SET says so.
#define ACTOR_ATTR 0x10
// The scheduler slot (already doubled) whose handler is told when this record
// collides. Only `$80:BE8F` reads it — it is how a display record, which is a
// drawing concern, finds the thread that is running the actor behind it.
#define ACTOR_THREAD 0x0c
// Zero for a record that does not collide. Two records only collide when both
// are non-zero *and* they differ, so this is what an actor is to whatever runs
// into it — a side, not an identity.
#define ACTOR_COLLIDE_ID 0x0e
#define ACTOR_NEXT 0x12   // offset of the next record, or 0

// Bit 15: draw this record at all. Both the cull and the draw pass test it
// first, with a bare `BPL`.
#define ACTOR_DRAW 0x8000
// Bit 14: the position is already in screen coordinates. The cull lets one of
// these through without looking at where it is, and the draw pass skips the
// camera subtraction — this is how the HUD rides on the same list.
#define ACTOR_SCREEN_SPACE 0x4000
// Bit 5: the sort's primary key. A record with it set is ordered ahead of one
// without, whatever their positions. Nothing here needs to know what it means.
#define ACTOR_SORT_FIRST 0x0020
// Bit 0: **the allocator's mark, and the name is now earned.** `$80:BE0C` sets
// it with `LDA #$0001 : STA $0000,Y` — the whole flags word, so a record starts
// its life as exactly `$0001` and everything else about it, `ACTOR_DRAW`
// included, is written afterwards by whoever asked for it. `$80:BE41` clears it
// with `LDA #$0000` to the same word, and those two are the only writers.
//
// That answers a question this comment carried for four rounds. The flags seen
// on live records are `$8000`, `$8001`, `$8003`, `$8005`, `$8009`; the two
// readers that test it during a *walk*, `$80:B123` and `$80:B379`, do so right
// after `ACTOR_DRAW` as a second gate, and both of the port's coverage sites for
// that branch are untaken by all 43 movies. They are untaken because a record on
// the display list has been allocated by definition — the walk is testing an
// invariant, not a state.
#define ACTOR_ACTIVE 0x0001
// Bit 4: take the OAM palette from `ACTOR_ATTR` instead of the metasprite's.
// The pass ORs the field in and masks the piece's own palette bits away.
#define ACTOR_ATTR_SET 0x0010
// Bit 3: sprite priority 3 instead of 2 — in front of all four backgrounds.
#define ACTOR_PRIORITY_TOP 0x0008
// Bits 2-1: which of the four emitters draws it, already scaled by 2 so the
// ROM can use it as an index straight into the table at `$80:BDEA`.
#define ACTOR_FLIP 0x0006

// --- $80:BC7F ---------------------------------------------------------------

// One bubble pass over the display list, ordering it back-to-front.
//
// A *pass*, not a sort: it walks the list once, swapping adjacent records that
// are out of order, so the list only becomes fully sorted over several frames.
// That is the ROM's behaviour and it is presumably deliberate — a frame's worth
// of work is bounded by the list length either way.
//
// The key is `ACTOR_SORT_FIRST` first and descending Y second, so records lower
// down the screen are emitted earlier and, in OAM, draw in front.
//
// Returns the record the walk ended on — the tail — or 0 if the list is empty.
// That is the value the ROM leaves in X, and it is the only reason this returns
// anything.
uint16_t actor_depth_sort(Wram* w);

// Which key decided a pair, and which way.
//
// The sort does not care — it relinks or it does not — but the ROM reaches the
// four answers down straight lines of four different lengths, and something has
// to count them apart. See `ActorSortWork`.
typedef enum {
  ACTOR_SORT_CMP_FIRST_SWAP,    // $80:BC98/$80:BCC4 taken: ACTOR_SORT_FIRST set
  ACTOR_SORT_CMP_FIRST_NOSWAP,  // ...clear, so the pair is already in order
  ACTOR_SORT_CMP_Y_SWAP,        // $80:BCA1/$80:BCCD not taken: the successor is lower
  ACTOR_SORT_CMP_Y_NOSWAP,
  ACTOR_SORT_CMP_COUNT,
} ActorSortCmp;

// What one pass actually did, counted by the branch the ROM would have taken.
//
// **This is the routine's cost, in the only terms the port can honestly know
// it.** `$80:BC7F` costs 92 cycles on an empty display list and 7,524 on a full
// one, and substituting it against the mean of 1,605 moves the machine's clock
// enough to part a framebuffer — see `cosim_cost` for the whole account. Nothing
// here is about cycles: these are counts of branch outcomes, the same kind of
// fact `PORT_COVER` records, and `src/cosim/routines.c` is where they get
// multiplied by what the corresponding run of 65816 instructions costs.
//
// The fields are not independent, and the redundancy is the check: the compares
// always number `steps + 1`, and `$80:BCAD` runs `1 + steps - swap_mid` times.
typedef struct {
  bool empty;   // the list head was 0: the first of the two early returns
  bool single;  // one record, so there was no pair to compare
  // Compares by outcome, indexed by `ActorSortCmp`. Head plus loop.
  uint16_t compares[ACTOR_SORT_CMP_COUNT];
  bool swap_head;      // the head itself moved, which relinks nothing behind it
  uint16_t swap_mid;   // relinks that needed the predecessor
  uint16_t steps;      // times the walk re-entered `$80:BCB0` and found a successor
} ActorSortWork;

// The same pass, reporting what it did. `actor_depth_sort` is this with the
// counts thrown away, and is what the rest of the port calls.
uint16_t actor_depth_sort_counted(Wram* w, ActorSortWork* work);

// --- $80:BCE2 ---------------------------------------------------------------

// Collect the records worth drawing into `visible_actors`, and write the count
// (in bytes) to `visible_actor_count`.
//
// A record is kept when it has `ACTOR_DRAW` set and either has
// `ACTOR_SCREEN_SPACE` set or lies inside the camera window on both axes. The
// window is 128 px behind the camera origin and 384 px ahead of it, which the
// ROM tests as two unsigned comparisons against `$FF80` and `$0180`.
//
// Nothing bounds the output, and nothing needs to: 32 records at two bytes each
// is 64 bytes, and `visible_actors` at `$7E:137E` is followed immediately by
// `oam_buffer` at `$7E:13BE`. The array is exactly as long as the list can be.
void actor_cull(Wram* w, const Rom* rom);

// The straight-line runs of `$80:BCE2`, one per branch outcome.
//
// The routine is a walk with five decisions in it, and every one of them is a
// branch the ROM either takes or does not. Each entry below is the run of
// instructions between one such fork and the next, named for the outcome that
// reaches it, so that a tally of these is a complete account of what the call
// executed — see `ActorCullWork`.
//
// The pairs read as a transcript of the listing: `..._UNDRAWN` is the branch
// taken and `..._DRAWN` the same branch falling through, and so on down.
typedef enum {
  CULL_BLK_EMPTY,     // $80:BCE8 BEQ taken: no list, straight to the RTS
  CULL_BLK_PROLOGUE,  // ...not taken, so there is a first record
  CULL_BLK_UNDRAWN,   // $80:BCEC BPL taken: ACTOR_DRAW clear, skip it
  CULL_BLK_DRAWN,     // ...not taken
  CULL_BLK_SCREEN,    // $80:BCEF BMI taken: ACTOR_SCREEN_SPACE, no window test
  CULL_BLK_WORLD,     // ...not taken, so both axes get tested
  // The window is one wrapped unsigned range, so each axis is up to two
  // comparisons: `CMP #$FF80` catches the 128 px behind the origin and, when it
  // does not, `CMP #$0180` decides between the 384 ahead and off-screen.
  CULL_BLK_X_HIGH,  // $80:BCFA BCS taken: behind the origin, in without a second test
  CULL_BLK_X_TEST,  // ...not taken: ahead of it, and how far is still open
  CULL_BLK_X_IN,    // $80:BCFF BCS not taken: inside the 384
  CULL_BLK_X_OUT,   // ...taken: off-screen, and the record is dropped
  CULL_BLK_Y_HIGH,  // $80:BD0A, the same four for the other axis
  CULL_BLK_Y_TEST,
  CULL_BLK_Y_IN,
  CULL_BLK_Y_OUT,
  CULL_BLK_EMIT,     // $80:BD11 TXA : STA $137E,Y : INY : INY
  CULL_BLK_ADVANCE,  // $80:BD1A BNE taken: another record follows
  CULL_BLK_EXIT,     // ...not taken: the link was 0, so STY $9C : RTS
  CULL_BLOCK_COUNT,
} ActorCullBlock;

// What one cull actually did, counted by the branch the ROM would have taken.
//
// Same arrangement as `ActorSortWork` and `HudWork`, and for the same reason:
// `$80:BCE2` costs 138 cycles on an empty display list and 8,688 on a full one,
// and a routine declared at one number cannot be substituted for one whose cost
// is a function of its input. Nothing here is about cycles — `src/cosim/routines.c`
// multiplies these counts by what each run costs. See `cosim_cost`.
//
// The counts are not independent, which is what makes the model checkable:
// `EMPTY + PROLOGUE` is 1, `UNDRAWN + DRAWN` is the number of records walked,
// `EMIT` is `SCREEN + Y_HIGH + Y_IN`, and `EXIT` is 1 whenever `PROLOGUE` is.
typedef struct {
  uint16_t blocks[CULL_BLOCK_COUNT];
} ActorCullWork;

// The same pass, reporting what it did. `actor_cull` is this with the counts
// thrown away, and is what the rest of the port calls.
void actor_cull_counted(Wram* w, const Rom* rom, ActorCullWork* work);

// --- $80:BE8F ---------------------------------------------------------------

// Tell each of a touching pair about the other.
//
// `a` is the record the overlap walk was holding and `b` the one it ran into.
// The routine reads both records' `ACTOR_COLLIDE_ID` and `ACTOR_THREAD` into
// the direct-page words above, and then dispatches **twice** — once per actor,
// each told the other's collision id — through `$80:8480`, which enters that
// actor's own handler.
//
// That is the seam between the sprite pass and actor behaviour, and the
// handlers behind it now live in `port/collide.h`. Two of them exist, which on
// `movies/level1-rescue.zmv` is 1,225 of the 1,226 pairs; a dispatch into
// anything else **returns false** and the harness gives the call back to the
// ROM. A decline may leave `w` partly written, because the dispatch is the last
// thing the routine does and the scratch words are published before it — see
// `sprite_build_oam` for the same convention.
//
// `tail` is in-out, and it is the routine's whole result: its `c` on the way in
// is the caller's carry, and on the way out it holds what the *second* dispatch
// left — which is the routine's contract, because that `JSL` is its last
// instruction.
bool actor_collide_notify(Wram* w, const Rom* rom, uint16_t a, uint16_t b,
                          ThreadCallResult* tail);

// What the two dispatches did, for `src/cosim/routines.c` to price.
//
// The routine itself has no branches at all: twenty-six instructions of
// straight line with a `JSL $80:8480` at `$80:BEB4` and another at `$80:BEC4`.
// So there is no block table here — its own cost is one constant — and the only
// thing that varies between calls is what the two dispatches cost, which is why
// this carries a `ThreadCallWork` apiece rather than a count of anything.
typedef struct {
  ThreadCallWork call[2];
} CollideNotifyWork;

bool actor_collide_notify_counted(Wram* w, const Rom* rom, uint16_t a,
                                  uint16_t b, ThreadCallResult* tail,
                                  CollideNotifyWork* work);

// --- $80:BEC9 ---------------------------------------------------------------

// Test every pair of visible records for a 16x16 overlap, and tell the two that
// touch about each other.
//
// It is the last thing `sprite_build_oam` does, and it rides on the sprite pass
// for a reason: `visible_actors` has just been narrowed to the records on
// screen, so the collision system gets a camera-sized broad phase for free. The
// walk is O(n^2) over at most 32 entries, and a pair only counts when both have
// a non-zero `ACTOR_COLLIDE_ID` and the two differ — an actor does not collide
// with its own side.
//
// **This port is partial, and it says so rather than pretending.** The walk is
// ported, the dispatch plumbing is (`actor_collide_notify` above), and so are
// the two actor handlers a collision in ordinary play reaches
// (`port/collide.h`) — but the list of ported handlers is two long, and a
// collision that reaches any other one makes this **return false**, which hands
// the call back to the ROM (`CosimGuard` in `src/cosim/cosim.h`). A false is
// not a failure and not an approximation: it is the port declining a call it
// cannot serve, counted and printed as such.
//
// True means the pass ran to the end — with any hits along the way fully
// dispatched — and WRAM now holds everything the ROM's version would have left:
// the walk cursor back at zero and the last tested record's id and position in
// the scratch words above.
bool actor_overlap_pass(Wram* w, const Rom* rom);

// **A longer reach for the pairs a player wants to touch.** The ROM's box is 8
// pixels either way on both axes for every pair alike, which is tight for
// walking over a first aid kit and for a shot that visibly clips a zombie.
// `actor_overlap_reach` is that 8 for the pairs below and the ROM's 8 for every
// other; at `OVERLAP_REACH_STOCK`, which is how it starts and how every tool
// but the frontend leaves it, this pass is the ROM's to the byte.
//
//   * a player (`$05`, `$06`) and something to pick up -- the thirty ids of
//     `$80:CA30`, the table that turns an object's type into its collision id;
//   * a player and a neighbour (`$01`, `$02`): a rescue;
//   * a player's weapon (`$5C` and up under the mask, `COLLIDE_ID_PLAYER`) and
//     a creature, which is whatever is none of the above.
//
// Not a creature and a player, nor a creature and a neighbour: a box widened
// for every pair (which is what patching the two constants at `$80:BEF8` and
// `$80:BF06` does) also lets a zombie touch the player from half as far again.
// Nor a player carried off by a spider (`$38`), who picks nothing up.
//
// A weapon finds a creature a second way, and mostly that way: `$80:BF1B`,
// "who is in this box", which a shot asks about the 16x16 around itself every
// tick (`$80:D413`) and a weapon held in the hand about a box in front of the
// player (`$80:F055`). There the box is grown by the same number of pixels on every
// side -- `actor_overlap_reach - 8` -- for a creature and for nobody else, when
// the asker's id is a weapon's. (Of the first kill in `level1-rescue.zmv`, not
// one tick went through the overlap pass.)
//
// The ROM's own routines are not changed, so a call the port declines is run
// by the ROM at 8 for that frame. Set by the frontend before the machine runs.
#define OVERLAP_REACH_STOCK 8
#define OVERLAP_REACH_MAX 16
#define OVERLAP_PICKUP_IDS 0x80ca30u
#define OVERLAP_PICKUP_COUNT 30
extern int actor_overlap_reach;

// The straight-line runs of `$80:BEC9`, one per branch outcome, as
// `ActorCullBlock` is for the cull. Two nested walks and five decisions.
typedef enum {
  OVL_BLK_EMPTY,       // $80:BECB BEQ taken: `$9C` is zero, straight to the RTL
  OVL_BLK_SINGLE,      // $80:BECF BEQ taken: one record, nothing to pair it with
  OVL_BLK_PROLOGUE,    // ...neither, so there is at least one pair
  OVL_BLK_OUTER_NOID,  // $80:BEDA BEQ taken: the outer record has no collision id
  OVL_BLK_OUTER_ID,    // ...it has one, which is published to $4A/$38/$3A
  OVL_BLK_INNER_NOID,  // $80:BEEB BEQ taken: the inner record has no id
  OVL_BLK_INNER_ID,
  OVL_BLK_SAME_ID,  // $80:BEEF BEQ taken: same id, so the two are on one side
  OVL_BLK_DIFF_ID,
  OVL_BLK_X_FAR,   // $80:BEFD BCS taken: more than 8 px apart on X
  OVL_BLK_X_NEAR,  // ...within 8, so the other axis is worth testing
  OVL_BLK_Y_FAR,   // $80:BF0B BCS taken
  OVL_BLK_Y_NEAR,  // ...a real overlap: `PHY : JSR $BE8F : PLY`
  OVL_BLK_INNER_NEXT,  // $80:BF14 BPL taken: another record below this one
  OVL_BLK_INNER_DONE,  // ...the inner walk ran off the bottom
  OVL_BLK_OUTER_NEXT,  // $80:BF18 BNE taken
  OVL_BLK_OUTER_DONE,  // ...and the RTL
  OVL_BLOCK_COUNT,
} ActorOverlapBlock;

// How many dispatches one pass can record the shape of. A pass with more hits
// than this is not priced — not because the cost is unknowable but because
// there is nowhere to put it, so the bound is set well above anything the
// corpus produces and the overflow is treated as a decline like any other.
#define OVL_MAX_PRICED_HITS 16

// What one pass did, and whether its cost can be known at all.
//
// `hits` is the count of `OVL_BLK_Y_NEAR`. It used to be a veto: `$80:BF0E JSR
// $BE8F` is a dispatch into two actor handlers, and until those handlers had
// cost models a pass with any hit in it reported no cost at all. Now each hit
// records the two dispatches it made, and the veto has moved down a level — a
// pass is priced when every handler it entered is one `src/cosim/routines.c`
// has a table for, and declines otherwise. The count is still kept because a
// pass with more hits than `notify` can hold has to decline for a different
// reason, and the two should not be confused.
typedef struct {
  uint16_t blocks[OVL_BLOCK_COUNT];
  uint16_t hits;
  CollideNotifyWork notify[OVL_MAX_PRICED_HITS];
} ActorOverlapWork;

// The same pass, reporting what it did. `actor_overlap_pass` is this with the
// counts thrown away.
//
// A pass that **declines** leaves `work` holding whatever it had counted when it
// gave up, which is not a cost and must not be used as one: the `false` return
// hands the whole call back to the ROM.
bool actor_overlap_pass_counted(Wram* w, const Rom* rom,
                                ActorOverlapWork* work);

// --- $80:BC23 ---------------------------------------------------------------

#define OAM_ENTRIES 128
#define W_OAM_HIGH (W_OAM_BUFFER + OAM_ENTRIES * 4)  // the 2-bits-per-sprite table
#define OAM_HIGH_BYTES 32

// Blank the OAM buffer, ready for the pass to fill it front to back.
//
// "Blank" is per entry: only the Y byte is touched, set to $E0 — 224, below the
// bottom of a 224-line screen — so an entry nothing overwrites this frame is
// simply not visible. The high table is filled with $AA, which is the pair
// `%10` for all 128: large size, X's ninth bit clear.
void oam_buffer_clear(Wram* w);

// --- $80:BD1F ---------------------------------------------------------------

// The four-byte table at `$80:BDE6`, indexed by the low two bits of the word at
// `$20` **on the caller's direct page**. All four entries are $80.
#define SPRITE_PASS_PHASE_TABLE 0x80bde6u

// The tail's `$80:BDD2  LDA $20` is a direct-page read, and the `$80:BDD0  PLD`
// two instructions earlier has already put the *caller's* page back. So the word
// it indexes the table with is `dp + $20`, which is `W_SCHED_TICK` only when the
// caller's page is zero — see the note on `sprite_build_oam`.
#define SPRITE_PASS_PHASE_DP 0x20

// The whole per-frame sprite pass: the routine `scheduler_idle` calls once a
// frame, and the one everything above is a part of.
//
//   sort the display list back to front            $80:BC7F
//   narrow it to what the camera can see           $80:BCE2
//   park all 128 OAM entries off screen            $80:BC23
//   for each survivor, emit its metasprite         $80:BA51 and its three twins
//   test every visible pair for an overlap         $80:BEC9
//
// It is the point where Phase 2 and Phase 3 meet. The emitters are Phase 2's,
// verified byte-exact against 4,591 real emissions before any of this existed;
// what was missing was the caller — which records to draw, in what order, at
// what screen position, and with which attributes. That is what this is.
//
// The pass never yields, and it is a leaf in the sense that matters here — but
// it is **not** only the scheduler's. `$80:837C` is one of three `JSL $80BD1F`
// in the ROM; the other two are `$82:DE03` and `$82:DE4B`, inside the alert
// `$82:DDA7` runs when the player comes near the actor whose handler is
// `actor_deeb_collide`. Those two are called from a *thread*, so the caller's
// direct page is that thread's own page rather than zero — which is why `dp` is
// a parameter. Everything the pass itself does is inside `$80:BD21  PEA $0000 :
// PLD`, so `dp` reaches exactly one instruction: the tail's `LDA $20`.
//
// It went unnoticed for forty-one routines because the table that read indexes
// is `$80,$80,$80,$80`: the index changes nothing the pass writes, so 128 KB of
// WRAM agrees either way and the wrong value escapes only through **X**. See
// `docs/cosim.md` → *The pass that is not only the scheduler's*.
//
// Returns false if it could not serve the call: `actor_overlap_pass` found a
// pair to dispatch (see its note above), or — in a case no shipped metasprite
// reaches — a metasprite ran off the end of its bank. Unlike that routine, this
// one has usually written a great deal of `w` by the time it says so, because
// the pass it declines on comes last. A false therefore means "throw `w` away",
// which is exactly what the harness does: it runs this on a private copy first
// and only keeps the result if it comes back true.
bool sprite_build_oam(Wram* w, const Rom* rom, uint16_t dp);

// Which record each OAM entry came from, and where that record was drawn.
//
// The pass knows this and the buffer does not: an OAM entry is four bytes of
// position, tile and attribute, and nothing in it says which actor it is a
// piece of. The frontend's between-tick pictures (`src/layers.h`) need exactly
// that -- a sprite is moved a fraction of the way from where its *actor* was a
// tick ago, so every piece of an actor moves together and two zombies standing
// close do not trade pieces. So the pass leaves it here, beside the buffer it
// belongs to. `rec` is the record's WRAM offset, or -1 for an entry the pass
// parked; `ox`/`oy` are the origin the record's pieces were placed from, the
// game's `$8E`/`$90`, camera already out.
//
// **The table is a tick ahead of the picture.** A frame of the core ends as
// vblank begins, and the buffer this pass fills is DMA'd to OAM *in* that
// vblank, so the picture drawn during a frame shows the pass before this one.
// A reader pairing this table with the PPU's OAM has to hold it for a tick
// first; `src/main_sdl.c` and `tools/test_layers.c` both do.
//
// `serial` steps once per pass that ran to completion, so a reader can tell a
// table written this tick from one left over: a pass the ROM ran instead (a
// declined call, or `--stock`) leaves the serial where it was. Written only
// when the pass returns true, for the same reason the harness only keeps `w`
// then -- a pass that declined has been run on a scratch copy and its table
// describes nothing that is on screen.
//
// Port code: libc only. It is a global because the pass has no caller-supplied
// context to put it in and the harness calls it through a fixed signature.
typedef struct {
  uint32_t serial;
  int16_t rec[SPRITE_OAM_SPRITES];
  int16_t ox[SPRITE_OAM_SPRITES];
  int16_t oy[SPRITE_OAM_SPRITES];
} SpriteOamOwners;
extern SpriteOamOwners sprite_oam_owners;

// The last few passes, each with the low table it wrote -- for a reader that
// has the PPU's OAM in front of it and needs to know *which* pass that is.
// "Hold the table for a tick" answers that for a reader at the end of a frame.
// It does not for one at the top of a frame (`src/widescreen.h`): the game
// starts its next tick inside vblank, straight after the NMI that DMA'd the
// buffer, and whether this pass has run again by line 0 depends on how much
// the tick had to do first. So the table there is sometimes the one on screen
// and usually the one after it, and the only thing that says which is the
// bytes. `sprite_oam_history[serial % SPRITE_OAM_HISTORY]` is the pass with
// that serial, for as long as it is one of the last SPRITE_OAM_HISTORY.
// Eight: the pass runs twice a tick in a level, and a slow tick is two frames.
#define SPRITE_OAM_HISTORY 8
typedef struct {
  SpriteOamOwners owners;
  uint8_t low[SPRITE_OAM_LOW_BYTES];
} SpriteOamPass;
extern SpriteOamPass sprite_oam_history[SPRITE_OAM_HISTORY];

// The pass's own walk, `$80:BD30`..`$80:BDCB`, counted for `cosim_cost`.
//
// This is the last of the five parts, and the one that finally makes the other
// four worth having: the sort, the cull, the buffer clear and the overlap pass
// are all reached from here, so their models were correct and unreachable until
// this one existed. Nothing outside `sprite_build_oam` calls any of them.
typedef enum {
  BUILD_BLK_EMPTY,       // $80:BD36 BEQ taken: nothing visible, straight out
  BUILD_BLK_NONEMPTY,    // ...not taken, so the walk starts
  BUILD_BLK_UNDRAWN,     // $80:BD44 BPL taken: ACTOR_DRAW clear
  BUILD_BLK_DRAWN,       // ...not taken
  BUILD_BLK_PRIO_PLAIN,  // $80:BD4C BEQ taken: attributes OR $2000
  BUILD_BLK_PRIO_TOP,    // ...not taken: ACTOR_PRIORITY_TOP, so $3000
  BUILD_BLK_ATTR_PLAIN,  // $80:BD5B BEQ taken: the frames' own palette stands
  BUILD_BLK_ATTR_SET,    // ...not taken: ACTOR_ATTR_SET overrides it
  BUILD_BLK_SCREEN,      // $80:BD6B BPL not taken: screen space, no camera
  BUILD_BLK_WORLD,       // ...taken: three subtractions instead
  BUILD_BLK_NO_META,     // $80:BD91 BCC taken: the pointer is not in ROM
  BUILD_BLK_BANK_LOW,    // $80:BD9A BCC taken: bank below $8F
  BUILD_BLK_BANK_HIGH,   // $80:BD9F BCS taken: bank above $90
  BUILD_BLK_EMPTY_META,  // $80:BDAA BEQ taken: a metasprite with no pieces
  BUILD_BLK_DRAW,        // ...not taken: the emitter runs
  BUILD_BLK_FULL,        // $80:BDBA BEQ taken: OAM filled, the walk stops
  BUILD_BLK_NOT_FULL,    // ...not taken
  BUILD_BLK_NEXT,        // $80:BDC2 BNE taken: another record
  BUILD_BLK_LAST,        // ...not taken, so park the rest of OAM off screen
  BUILD_BLK_EPILOGUE,    // $80:BDCC..$80:BDE2, and every exit reaches it
  BUILD_BLOCK_COUNT,
} SpriteBuildBlock;

// Everything one pass did, at every level it did it at.
//
// The three sub-walks price themselves through their own `_counted` entry
// points and are not repeated here. The two things that *are* repeated are the
// two that run per piece rather than per record: the emitter, which runs once
// per drawable record and is summed because a pass draws many, and the VRAM
// cache lookup, which runs once per emitted piece. Summing them loses which
// record was expensive, which is exactly what a cost model does not need.
typedef struct {
  uint16_t blocks[BUILD_BLOCK_COUNT];
  ActorSortWork sort;
  ActorCullWork cull;
  ActorOverlapWork overlap;
  // Per flip variant, because the four emitters do not cost the same. Indexed
  // by `SpriteFlip` as it comes out of the actor's flag bits, so entries 1, 3,
  // 5 and 7 stay zero — a four-entry array indexed by `flip / 2` would be
  // tidier and would put a division between the ROM's own value and the table.
  uint16_t emit[8][EMIT_BLOCK_COUNT];
  SpriteTileWork tile;
} SpriteBuildWork;

bool sprite_build_oam_counted(Wram* w, const Rom* rom, uint16_t dp,
                              SpriteBuildWork* work);

// --- $80:B123 ---------------------------------------------------------------

// **Which of the things on the board is nearest to a point**, by Manhattan
// distance, out of the four collision ids it will look at.
//
// This is the most-executed routine in bank `$80` that the port did not have:
// 44,248 calls and 5.2% of every instruction the game executes, over ten movies
// one per level. Its callers are all inside enemy bodies — `$81:86B7`,
// `$81:870A`, `$81:8ADC`, `$81:8B3A` — which together with the ids it accepts
// is what it is: **an enemy choosing who to go after.** `$05` and `$06` are the
// two players, claimed by those same two numbers in `victim_a264_collide`.
//
// It walks all 32 slots from the top down rather than following
// `ACTOR_NEXT`, so it sees records the linked list has dropped, and it costs
// the same 32 iterations whatever the board looks like.
#define ACTOR_NEAREST_ENTRY 0x80b123u

// `LDA $000E,X` against four ids, in this order, and anything else is skipped.
#define NEAREST_ID_PLAYER_A 0x0005
#define NEAREST_ID_PLAYER_B 0x0006
#define NEAREST_ID_C 0x0038
#define NEAREST_ID_D 0x0001

// Its scratch, all on direct page zero — `LDA #$0000 : TCD` at `$80:B124` is
// what makes these absolute rather than relative to the calling thread's page.
//
// Three of them are left holding the *last candidate examined* rather than the
// best one, which is dead storage the routine never reads back. It is written
// anyway, so the port writes it too.
#define NEAREST_DP_BEST 0x38    // best distance so far; seeded $FFFF
#define NEAREST_DP_X 0x3a       // the point being searched from
#define NEAREST_DP_Y 0x3c
#define NEAREST_DP_DX 0x3e      // raw, signed, of the last candidate
#define NEAREST_DP_DY 0x40      // ...and its Y
#define NEAREST_DP_DIST 0x42    // ...and its distance
#define NEAREST_DP_FOUND 0x44   // the winning record, untouched if none matched

// Returns the winning record's address, which the ROM leaves in X, and puts the
// distance — `$FFFF` when nothing matched — in `*dist`, which it leaves in A.
uint16_t actor_nearest(Wram* w, uint16_t x, uint16_t y, uint16_t* dist);

// --- $80:B379 ---------------------------------------------------------------

// **Is anything lined up with this point, and which way is it?**
//
// `actor_nearest`'s sibling, and near enough its twin to be worth reading as
// one: the same 32 slots walked from the top down, the same `ACTOR_DRAW` and
// flag-bit-0 gate, the same collision ids — `$05`, `$06` and `$01`, which is
// `actor_nearest`'s four without `$38`. What differs is the question. That one
// measures every candidate and keeps the closest; this one takes the **first**
// candidate that lines up and returns immediately, so it answers with whichever
// matching record sits in the highest slot rather than with the nearest.
//
// Its two callers, `$81:9D34` and `$81:9DD4`, are one enemy body, and the line
// under the first of them is `JSL $80:B123` on a `$3C`-frame timer. So the same
// creature asks two different questions at two different rates: *who should I
// be walking towards*, once a second, and *can I shoot right now*, every frame.
// This is the second one.
#define ACTOR_ALIGNED_ENTRY 0x80b379u

#define ALIGNED_ID_PLAYER_A 0x0005
#define ALIGNED_ID_PLAYER_B 0x0006
#define ALIGNED_ID_D 0x0001

// Direct page zero, forced by the routine's own `LDA #$0000 : TCD`, and the
// point it was asked about. Note these are *not* `actor_nearest`'s slots:
// `$38` is the X here and the best distance there.
#define ALIGNED_DP_X 0x38
#define ALIGNED_DP_Y 0x3a

// `SBC : CLC : ADC #$0008 : CMP #$0010 : BCS` — the same add-half-and-compare
// -unsigned trick `actor_at_point` uses, so the window is one tile: the record
// is aligned when the difference is in `-8..+7`.
#define ALIGNED_HALF 0x0008
#define ALIGNED_WINDOW 0x0010

// What it answers with: a direction index already doubled, in the game's usual
// 1-8 clockwise-from-up ordering, and zero for nothing found. The ROM's caller
// does `TAX : BEQ` and then indexes with it.
//
// **X is tested first and wins ties.** A record diagonally within one tile on
// both axes is reported as up or down, never left or right.
#define ALIGNED_NONE 0x0000
#define ALIGNED_UP 0x0002
#define ALIGNED_RIGHT 0x0006
#define ALIGNED_DOWN 0x000a
#define ALIGNED_LEFT 0x000e

// A is the direction. X is the record that matched, or `$184A` — one stride
// below the table — when the walk ran off the bottom without a match, which is
// the loop counter's final value and not a pointer to anything. Y is the `y`
// argument, untouched from first instruction to last.
//
// Carry is the leftover of whichever `SBC` decided the direction, so it says
// `record >= point` on that axis and is never read by anything; it is clear on
// the no-match exit, from the `CPX` that ended the loop. N and Z belong to the
// caller's direct page, off the closing `PLD` — the caller re-derives Z from A
// with its own `TAX`.
//
// Overflow is the same `SBC`'s on a match. On the no-match exit it is clear:
// the last arithmetic was the loop counter's `SBC #$0014`, which cannot
// overflow.
//
// The counts are for the harness, which prices the call by its path: how
// many slots were dismissed at each test, how many candidates wore each of
// the three ids, the one that matched among them, and how many of those were
// lined up on neither axis.
typedef struct {
  uint16_t a, x;
  bool c, v;
  int undrawn, inactive, wrong_id, off;
  int by_id[3];  // `ALIGNED_ID_PLAYER_A`, `ALIGNED_ID_PLAYER_B`, `ALIGNED_ID_D`
} ActorAlignedRegs;

void actor_aligned(Wram* w, uint16_t x, uint16_t y, ActorAlignedRegs* out);

// --- $80:B093, $80:B18F, $80:B1EC, $80:B22A, $80:B26B, $80:B2A5 -------------

// **The rest of the enemy's targeting arithmetic**: six routines packed into
// the 530 bytes between `actor_nearest` and `actor_aligned`, sharing one leaf
// and one table.
//
// They are here as a family because they are one: `actor_nearest` above
// answers *which record*, and every one of these answers a follow-on question
// about it — *how far*, *which way*, *is it close enough*. Four of them read
// the two player-record pointers `$D2`/`$D4` directly instead of walking the
// slot table, which is what makes them the cheap question an enemy can afford
// every frame where `actor_nearest`'s 32 slots is the expensive one it asks
// once a second.
//
//   $80:B093  actor_gap             the larger of the two axis gaps
//   $80:B18F  actor_nearest_id3     actor_nearest, for collision id $03
//   $80:B1EC  actor_bearing_point   which way is a record from a point
//   $80:B22A  actor_bearing         ...and from another record
//   $80:B26B  player_in_range       the nearer player, if either is close
//   $80:B2A5  player_bearing        ...and which way that is
//
// **Three of them were being counted as already ported and are not.**
// `tools/native_share.py` attributes an instruction to the nearest preceding
// subroutine entry, and with nothing declared between `$80:B123` and `$80:B2A5`
// the whole of `$80:B1EC`, `$80:B22A` and `$80:B26B` fell inside
// `actor_nearest`'s block. The ranking's own caveat — "a routine with a
// suspiciously large share is worth checking against the disassembly before it
// is believed" — earned its keep here: `actor_nearest` was the third-largest
// native routine in the report and part of what it was credited with was code
// nobody had written.

// The 65816 gets its direct page to zero and then reads these as absolute
// addresses, so they are the same six bytes for every caller and every thread —
// exactly as `actor_nearest`'s and `actor_aligned`'s scratch is, and note that
// `$38` and `$3A` mean something different in all three.
#define GAP_DP_X 0x38   // the point, written by the caller and not by $80:B093
#define GAP_DP_Y 0x3a
#define GAP_DP_DX 0x3c  // the X gap, kept only so the Y gap can be compared

// A is the gap. X is untouched; Y is the record it was asked about, likewise.
//
// **Carry is the caller's on the empty-record exit and the routine's on every
// other**, which is why it is reported separately rather than defaulted:
// `TYA : BEQ` reaches `LDA #$FFFF : RTS` without executing anything that writes
// carry, so what the caller sees there is what it came in with. Nothing reads
// it — both callers overwrite it with a `CMP` two instructions later — and a
// shim that claimed it would be asserting the caller's leftovers.
typedef struct {
  uint16_t a;
  bool n, z;
  bool c;
  bool has_c;
  // For the harness, which prices the call by its path: which of the two
  // differences were negative, and so went through `EOR #$FFFF : INC`.
  bool dx_negative, dy_negative;
} ActorGapRegs;

// **How far is this record from the point, measured as the wider axis?**
//
// Chebyshev distance: `max(|rec.x - x|, |rec.y - y|)`, and `$FFFF` when `rec`
// is zero, which is what `$D4` reads in one-player mode. Nine hundred calls a
// second between the two routines below.
//
// It takes the point out of `GAP_DP_X`/`GAP_DP_Y` rather than as arguments
// because that is where it reads them from: it is a `JSR` leaf with no calling
// convention of its own beyond the record in Y, and both of its callers set
// those two words up before entering it.
//
// **The absolute value here is `BPL`, not the `BCS` `actor_nearest` uses.**
// The two are the same answer for every input either routine can be given and
// they are not the same instruction: `BPL` tests the sign of the difference and
// `BCS` tests whether the subtraction borrowed, which differ once the gap
// exceeds `$8000`. Transcribed as written, on the principle that a port is not
// entitled to decide that a distinction cannot be reached.
void actor_gap(Wram* w, uint16_t rec, ActorGapRegs* out);

// `actor_nearest`'s four collision ids, and this one's single `CMP #$0003`.
#define NEAREST3_ID 0x0003

// **`actor_nearest` with one id instead of four.** Byte for byte the same
// routine otherwise: the same 32 slots top-down, the same `ACTOR_DRAW` and
// flag-bit-0 gate, the same `|dx| + |dy|` sum into the same seven scratch
// words, the same strictly-nearer-wins comparison, the same three registers
// out. Both of its callers are in bank $83, the boss bank, and between them
// they account for 54 calls in the whole corpus — this is transcribed because
// it sits inside the family rather than because the ranking asked for it.
//
// Returns the winning record and puts the distance in `*dist`, exactly as
// `actor_nearest` does, including leaving `$44` alone when nothing matched so
// that a search that finds nothing hands back the last one that did.
uint16_t actor_nearest_id3(Wram* w, uint16_t x, uint16_t y, uint16_t* dist);

// --- What a search costs ----------------------------------------------------

// One block table prices both searches, because "byte for byte the same routine
// otherwise" above is not a figure of speech: `$80:B123` and `$80:B18F` differ
// only in the four `CMP`/`BEQ` pairs one uses where the other has a single
// `CMP #$0003 : BNE`. Everything else — the prologue, the two flag gates, the
// two absolute values, the sum, the strictly-nearer test, the loop tail and the
// epilogue — is the same instructions at the same costs, so the two id chains
// get their own blocks and the other twelve are shared.
//
// The walk is a fixed 32 slots whatever the board holds, which makes almost all
// of this self-checking. On every call:
//
//   UNDRAWN + INACTIVE + WRONG_ID + (the id blocks)  ==  32
//   ID_A + ID_B + ID_C + ID_D + ID3_MATCH  ==  DX_POS + DX_NEG
//                                          ==  DY_POS + DY_NEG
//                                          ==  KEPT + CLOSER
//   LOOP_NEXT == 31,  LOOP_DONE == 1,  FIXED == 1
//
// Splitting a loop that always runs the same number of times is the point
// rather than an oversight: a fixed count folded into the prologue is the
// cheapest place in a model to hide an arithmetic error.
typedef enum {
  NEAR_BLK_FIXED,      // the prologue and the epilogue: both run exactly once
  NEAR_BLK_UNDRAWN,    // $80:B137 BPL taken: no ACTOR_DRAW
  NEAR_BLK_INACTIVE,   // $80:B13A BCC taken: drawn, but flag bit 0 clear
  NEAR_BLK_WRONG_ID,   // $80:B151 BNE taken: none of the four ids
  NEAR_BLK_ID_A,       // $80:B142 BEQ taken: id $05, player A
  NEAR_BLK_ID_B,       // $80:B147 BEQ taken: id $06, player B
  NEAR_BLK_ID_C,       // $80:B14C BEQ taken: id $38
  NEAR_BLK_ID_D,       // $80:B151 BNE *not* taken: id $01, matched by falling
                       // through, which is why it is cheaper than $38's
  NEAR_BLK_ID3_MATCH,  // $80:B1AE BNE not taken: the sibling's single id $03
  NEAR_BLK_ID3_MISS,   // ...taken
  NEAR_BLK_DX_POS,     // $80:B15B BCS taken: X difference needed no negating
  NEAR_BLK_DX_NEG,     // ...not taken, so `EOR #$FFFF : INC A` ran
  NEAR_BLK_DY_POS,     // $80:B16B BCS taken, and the same on Y
  NEAR_BLK_DY_NEG,     // ...not taken
  NEAR_BLK_KEPT,       // $80:B178 BCS taken: not strictly nearer, so no store
  NEAR_BLK_CLOSER,     // ...not taken: a new winner, `STA $38 : STX $44`
  NEAR_BLK_LOOP_NEXT,  // $80:B187 BCS taken: another slot below this one
  NEAR_BLK_LOOP_DONE,  // ...not taken, which happens once
  NEAREST_BLOCK_COUNT,
} ActorNearestBlock;

typedef struct {
  uint16_t blocks[NEAREST_BLOCK_COUNT];
} ActorNearestWork;

uint16_t actor_nearest_counted(Wram* w, uint16_t x, uint16_t y, uint16_t* dist,
                               ActorNearestWork* work);
uint16_t actor_nearest_id3_counted(Wram* w, uint16_t x, uint16_t y,
                                   uint16_t* dist, ActorNearestWork* work);

// The three direction tables, all twelve entries, all indexed `4 * v + h` where
// each half is 0 for "the same", 1 for "less than" and 2 for "greater than".
// Every fourth entry is the unreachable `h == 3` slot and every one of them is
// zero.
//
// Read from ROM rather than transcribed: they are ordinary data in an ordinary
// bank, and a hack that retunes an enemy's facing by editing twelve bytes would
// still work.
#define BEARING_POINT_TABLE 0x80b21eu  // $80:B1EC's, bytes, mirrored
#define BEARING_TABLE 0x80b25fu        // $80:B22A's, bytes
#define PLAYER_BEARING_TABLE 0x80b302u // $80:B2A5's, words

// The compass every one of them answers in, clockwise from up, and the same one
// `ALIGNED_UP` and friends use undoubled.
#define BEARING_NONE 0
#define BEARING_UP 1
#define BEARING_UP_RIGHT 2
#define BEARING_RIGHT 3
#define BEARING_DOWN_RIGHT 4
#define BEARING_DOWN 5
#define BEARING_DOWN_LEFT 6
#define BEARING_LEFT 7
#define BEARING_UP_LEFT 8

// A is the direction. X is the table index it came from — a number, not a
// pointer, and left in X only because `TAX` is how the ROM indexes. Y is the
// record, untouched.
//
// N and Z are the closing `PLD`'s, so they describe the caller's direct page
// and not the answer; the callers re-derive what they need from A.
//
// **Carry is not the horizontal comparison's**, which is the obvious reading
// and the wrong one. `ADC #$0001` sits between that `CMP` and the `RTL`, and an
// `ADC` writes carry whether or not the code wanted an answer from it — so on
// every path where the record and the point differ on X, the comparison's carry
// is replaced by that of an addition of at most ten, which never carries. Only
// the equal case branches over the `ADC` and keeps it. Carry out therefore
// means *the record shares the point's X*, and nothing reads it.
typedef struct {
  uint16_t a, x;
  bool c;
} ActorBearingRegs;

// **Which way is one record from another?** `from` supplies the point — its
// `ACTOR_X` and `ACTOR_Y` are copied into the same `$38`/`$3A` the whole family
// uses — and `to` is measured against it. 13,671 calls over fifteen sites in
// three banks, which makes it the busiest routine in this group.
void actor_bearing(Wram* w, const Rom* rom, uint16_t from_rec, uint16_t to_rec,
                   ActorBearingRegs* out);

// **The same question with a bare point for the `from` — and it does not work.**
//
// `$80:B1EC` is `actor_bearing` with the point passed in X and Y instead of
// read out of a record, and its table is the mirror image of the other's:
// where that one answers *up* this one answers *down*, because it reports which
// way the point is from the record rather than the other way about.
//
// It computes the vertical half of the index, shifts it into place, and then
// **throws it away**: the instruction that should fold it into the index is
// `TXA` where its sibling twenty-two bytes further on has `TAX`, so X is still
// the zero it was seeded with and only the horizontal half ever reaches the
// table. Eight of that table's twelve bytes are unreachable, and every answer
// is `BEARING_NONE`, `BEARING_RIGHT` or `BEARING_LEFT` — an enemy asking this
// one which way to face can never be told to go up or down.
//
// **The port reproduces it.** It is not a transcription slip to be tidied on
// the way past: it is what the game does, three call sites reach it, and
// `movies/level17-weapon.zmv` among others is playing against the version with
// the bug in it.
void actor_bearing_point(Wram* w, const Rom* rom, uint16_t rec, uint16_t x,
                         uint16_t y, ActorBearingRegs* out);

// $80:B26B's and $80:B2A5's shared scratch: the range limit and the two
// distances, on top of `GAP_DP_X`/`GAP_DP_Y` which they set for the leaf.
#define PICK_DP_LIMIT 0x42
#define PICK_DP_DIST_A 0x3e
#define PICK_DP_DIST_B 0x40

// What the two of them hand back. A is the answer — a record pointer for
// `player_in_range`, a direction for `player_bearing`, zero from either when
// neither player is close enough. X and Y differ per routine and are documented
// at each.
//
// N and Z are the closing `PLD`'s in both, as everywhere in this family.
typedef enum {
  PICK_B_NEARER,  // B within the limit, and nearer than A
  PICK_A_NEARER,  // B within it, and A no further
  PICK_A_ONLY,    // B outside it, A within
  PICK_NEITHER,
} PlayerPickExit;

typedef struct {
  uint16_t a, x, y;
  bool c;
  // For the harness, which prices the call by its path: the two gaps as
  // measured, player A's first, and the exit. `player_bearing` adds whether
  // the chosen player was level with the point on each axis.
  ActorGapRegs gap[2];
  PlayerPickExit exit;
  bool same_x, same_y;
} PlayerPickRegs;

// **Which player is nearer, if either is within `limit`?**
//
// A is that player's record pointer, or zero. X is its distance on the two
// found exits and the caller's own `x` on the third, because the zero exit
// never loads it. Y is `$D4` — player B's record — on all three, including
// when the answer is player A: it is left over from the second `actor_gap` call
// and the routine never tidies it.
//
// Carry is the leftover of whichever comparison chose the exit, and it is not
// the same one on the two paths that both return player A: `1` when B was in
// range but no nearer, `0` when B was out of range and A was in it. Nothing
// reads it. It is claimed anyway, because an unclaimed output is an unchecked
// one.
void player_in_range(Wram* w, uint16_t limit, uint16_t x, uint16_t y,
                     PlayerPickRegs* out);

// **Which way is the nearer player, if either is within `limit`?**
//
// The same selection as `player_in_range` — the two routines are the same
// twenty instructions up to the point where they answer — and then the same
// direction lookup as `actor_bearing`, from a word-wide table.
//
// A is the direction and zero means *neither player is close enough*, which is
// exactly how its callers read it: `JSL : TAX : BNE`. That is why its table
// differs from `actor_bearing`'s in its first entry and only there — **a player
// standing exactly on top of the enemy answers `BEARING_UP` rather than zero**,
// because zero is already spoken for and an enemy told "nowhere" would stop
// chasing something it is touching.
//
// X is the chosen player's distance, pushed with `PEI` before the lookup and
// pulled back into X after it, and on the zero exit it is the caller's own `x`.
// Y is the chosen player's record on the direction exit — `PHY`/`PLY` around
// the lookup — and `$D4` on the zero exit.
//
// Carry is 0 on every direction exit: the last thing to write it is the `ASL`
// that doubles the table index, and an index of at most ten shifts nothing out.
// On the zero exit it is 1, from the comparison that rejected player A.
void player_bearing(Wram* w, const Rom* rom, uint16_t limit, uint16_t x,
                    uint16_t y, PlayerPickRegs* out);

// --- $80:B3F1 ---------------------------------------------------------------

// **Snap one record onto another when they are nearly lined up.**
//
// Twenty-one instructions, and the other half of what `actor_aligned` is for.
// That routine answers *is something roughly lined up with me*, with a tile of
// slack; this one closes the last pixel of it. For each axis independently: if
// `|rec.pos - onto.pos| < 2`, write `onto`'s coordinate into `rec`. Otherwise
// leave it alone.
//
// Without it, an enemy that walks towards a target one pixel at a time can step
// straight past the alignment it was aiming for and shoot down an empty
// corridor. With it, the last pixel is a snap rather than a step, so lining up
// always succeeds exactly.
//
// **This is below the threshold the ranking is for**: 199,679 instructions over
// ten profiles, 0.06%, where the rows around it are ten times that. It went in
// because it was already read, sits in the same file as its partner, is a leaf
// that calls nothing, and has thirteen callers across banks `$81` and `$82` —
// which is the sort of thing that turns other routines into fully-subsumed ones
// later. It is not an argument for porting the next 0.06% row.
#define ACTOR_SNAP_TO_ENTRY 0x80b3f1u

// `CMP #$0002 : BCS` on the absolute difference, so the snap happens at a
// distance of 0 or 1 and never at 2.
#define SNAP_WINDOW 0x0002

// A is whatever the *Y* axis last put there — `onto`'s Y when that axis
// snapped, and the absolute difference when it did not — because the X axis
// runs first and everything it left is overwritten. X and Y are the two record
// addresses, unchanged; the routine indexes with them and never writes them.
//
// Carry is the `CMP` that decided the Y axis: **set means it did not snap**.
// N and Z come from that same `CMP`, and on the no-snap path they therefore
// describe the difference **minus two** while A still holds the difference
// itself — `CMP` sets flags from a subtraction it does not store, and reading
// it as an `SBC` is worth exactly two, which is what the harness reported.
// There is no `PHD` here and no `PLD`, so unlike its neighbours these flags are
// the routine's own.
typedef struct {
  uint16_t a;
  bool n, z, c;
  // For the harness, which prices the call by its path: on X and then Y, was
  // the difference negative, and did the axis snap.
  bool negative[2], snapped[2];
} ActorSnapRegs;

void actor_snap_to(Wram* w, uint16_t rec, uint16_t onto, ActorSnapRegs* out);

// --- $80:BF1B ---------------------------------------------------------------

// **Tell everything inside a rectangle.** The blast radius, as a routine.
//
// `actor_overlap_pass` is the game asking *who is touching whom*, pair by pair,
// once a frame. This is one actor asking *who is inside this box* and telling
// all of them at once, and it is the mechanism behind every attack in the game
// that is not a contact hit: fifteen call sites across four banks reach it.
//
// The caller sets up five direct-page words and calls. The rectangle is
// half-open — `>= min` and `< max` on both axes, from `CMP : BCC` and
// `CMP : BCS` — and `$40` is the caller's own collision id, which is both the
// argument each handler is entered with and the id that is skipped, so nothing
// blasts itself.
//
// It walks `visible_actors` **backwards**, like `actor_at_point` and unlike
// `actor_nearest`, so it sees only what this frame's cull kept.
#define ACTOR_NOTIFY_BOX_ENTRY 0x80bf1bu

#define NOTIFY_BOX_DP_X0 0x38  // left, inclusive
#define NOTIFY_BOX_DP_X1 0x3a  // right, exclusive
#define NOTIFY_BOX_DP_Y0 0x3c  // top, inclusive
#define NOTIFY_BOX_DP_Y1 0x3e  // bottom, exclusive
#define NOTIFY_BOX_DP_ID 0x40  // the caller's id: passed on, and skipped

// **The four bounds are clamped to zero if negative, and only if negative.**
// `LDX #$0006 : BIT $38,X : BPL + : STZ $38,X : + DEX DEX : BPL` is a four-word
// loop testing bit 15, so a box that hangs off the left or top of the map is
// clipped to the edge — but a box off the right or bottom is not clipped at
// all, because there is no map size here to clip it against. The asymmetry is
// the ROM's and the port keeps it.
#define NOTIFY_BOX_BOUNDS 4

// The dispatch is `thread_call_handler`, so this routine inherits its decline:
// **false means some actor in the box has a handler the port does not have**,
// and the harness gives the whole call back to the ROM. As with
// `actor_collide_notify`, a decline may leave `w` partly written — earlier
// actors in the box have already been told — because the ROM has already told
// them by then too.
//
// `tail` is in-out and carries the last dispatch's registers, exactly as it does
// for the overlap pass.
//
// The three register outputs are all leftovers of the *last record examined*,
// and they are claimed rather than written off because there are fifteen call
// sites and four of the five looked at return immediately, which puts the
// values one frame further from anywhere they could be shown to be dead. So:
// A is the last value loaded — the id that read zero, or the X or Y that failed
// a bound, or the dispatch's A. X is that record, or the dispatch's X. Carry is
// whichever `CMP` decided the last record's fate.
//
// Y is the walk index run off the end: `$FFFE` when the walk happened, and 0 on
// both early exits, where `LDY $9C` or `DEY DEY` left it there. X on those two
// is `$FFFE` — the bound-clamping loop's counter, gone negative.
typedef struct {
  uint16_t a, x, y;
  bool c;
} ActorNotifyRegs;

bool actor_notify_box(Wram* w, const Rom* rom, uint16_t a_in, bool c_in,
                      ThreadCallResult* tail, ActorNotifyRegs* out);

// What a run of `$80:BF1B` did, block by block.
//
// The routine is one screen of listing and every branch in it is an outcome the
// port already computes, so this is the shape the `_counted` pattern was made
// for. Two things are worth saying about the layout:
//
// **The bound loop is four blocks, not one.** `LDX #$0006 : BIT $38,X : BPL :
// STZ $38,X : DEX DEX : BPL` always runs exactly four times, so its cost could
// have been a constant — but the `STZ` only happens on a negative bound, and
// the loop-back `BPL` is taken three times and not taken once. Counting all
// four independently makes the fixed part self-checking: `BOUND_NEXT` must come
// out at three times `BOUND_DONE` on every call, and the two bound blocks must
// sum to four times it.
//
// **The floor is the check, and it is exact.** All four bounds kept, then the
// one-record refusal at `$80:BF33`, is
// `114 + 4x76 + 3x18 + 12 + 158`, which is **642** — and 642 is the minimum
// `verify` measures over the 5,245 calls `movies/level25-boss.zmv` makes. On
// that movie `notify_bound_clamped` and `notify_no_actors` are both untaken, so
// the cheapest call available *is* that path, and it prices to the cycle.
typedef enum {
  NOTIFY_BLK_PROLOGUE,       // $80:BF1B PHD : PEA $0000 : PLD : LDX #$0006
  NOTIFY_BLK_BOUND_KEPT,     // $80:BF25 BPL taken: bit 15 clear, no STZ
  NOTIFY_BLK_BOUND_CLAMPED,  // ...not taken, so the bound is zeroed
  NOTIFY_BLK_BOUND_NEXT,     // $80:BF2B BPL taken: three of the four
  NOTIFY_BLK_BOUND_DONE,     // ...not taken, which is the fourth
  NOTIFY_BLK_NO_ACTORS,      // $80:BF2F BEQ taken: nothing visible at all
  NOTIFY_BLK_ONE_ACTOR,      // $80:BF33 BEQ taken: exactly one, which is refused
  NOTIFY_BLK_WALK,           // ...neither, so the walk runs
  NOTIFY_BLK_NO_ID,          // $80:BF3A BEQ taken: the record has no collision id
  NOTIFY_BLK_SELF_ID,        // $80:BF3E BEQ taken: it is the caller's own
  NOTIFY_BLK_LEFT_OF,        // $80:BF44 BCC taken: x below the left edge
  NOTIFY_BLK_RIGHT_OF,       // $80:BF48 BCS taken: x at or past the right one
  NOTIFY_BLK_ABOVE,          // $80:BF4E BCC taken: y above the top edge
  NOTIFY_BLK_BELOW,          // $80:BF52 BCS taken: y at or past the bottom one
  NOTIFY_BLK_HIT,            // inside the box: $80:BF54 PHY .. PLY, the JSL included
  NOTIFY_BLK_LOOP_NEXT,      // $80:BF63 BPL taken: another record
  NOTIFY_BLK_LOOP_DONE,      // ...not taken, and the shared PLD : RTL
  NOTIFY_BLOCK_COUNT,
} ActorNotifyBlock;

// The walk covers `visible_actors`, which the cull holds to 32 records, so 32
// is the most hits a call can produce and the array never has to refuse one for
// being too long. It is still guarded — a bound that is right by construction
// is worth asserting anyway, and `OVL_MAX_PRICED_HITS` is guarded for the same
// reason.
#define NOTIFY_BOX_MAX_PRICED_HITS 32

typedef struct {
  uint16_t blocks[NOTIFY_BLOCK_COUNT];
  // One per `NOTIFY_BLK_HIT`, in the order the walk made them, capped above.
  uint16_t hits;
  ThreadCallWork call[NOTIFY_BOX_MAX_PRICED_HITS];
} ActorNotifyWork;

bool actor_notify_box_counted(Wram* w, const Rom* rom, uint16_t a_in, bool c_in,
                              ThreadCallResult* tail, ActorNotifyRegs* out,
                              ActorNotifyWork* work);

// --- $80:BF67 ---------------------------------------------------------------

// **Is anything standing within six pixels of this point?**
//
// The second routine the profiler picked: 1.8% of every instruction the game
// executes over 43,605 calls, and its callers are the same enemy bodies that
// call `actor_nearest` — `$81:85E3`, `$81:8627`, `$81:89E6`, `$81:8A15`.
//
// It walks `visible_actors` **backwards** rather than the record table, so it
// sees only what this frame's cull kept, and it costs whatever the board is
// wide rather than a fixed 32.
//
// The window is the `CLC : ADC #$0006 : CMP #$000C : BCS` trick the collision
// box uses: adding half the width and testing unsigned against the width tests
// **-6 <= d <= +5** in two instructions, so the box is a pixel wider to the
// left than to the right. `actor_overlap_pass` does the same thing at 8 and 16.
#define ACTOR_AT_POINT_ENTRY 0x80bf67u

// The ids it will stop for, as the ROM's three comparisons leave them:
// `$00` never, `$07` and `$08` never, everything from `$0C` to `$33` never, and
// anything else — `$01`-`$06`, `$09`-`$0B`, and everything above `$33` — yes.
#define AT_POINT_ID_RANGE_LO 0x000c
#define AT_POINT_ID_RANGE_HI 0x0033
#define AT_POINT_ID_SKIP_A 0x0007
#define AT_POINT_ID_SKIP_B 0x0008
#define AT_POINT_HALF_WINDOW 0x0006
#define AT_POINT_WINDOW 0x000c

// Its scratch, on direct page zero like `actor_nearest`'s.
#define AT_POINT_DP_SELF 0x38  // the record the caller wants ignored
#define AT_POINT_DP_X 0x3a
#define AT_POINT_DP_Y 0x3c

// What the ROM leaves in A, X and Y, which is all three of them and none of it
// tidy — see the shim for why each one is what it is.
typedef struct {
  uint16_t a, x, y;
  bool found;  // the carry: set by `SEC` at $80:BFBF, cleared at $80:BFC6
  // Overflow, from the last `ADC #$0006` of the window test, if any entry got
  // that far. Otherwise nothing wrote it and the caller's stands.
  bool v_set, v;
} AtPointRegs;

void actor_at_point(Wram* w, uint16_t self, uint16_t x, uint16_t y,
                    AtPointRegs* out);

// What a search of the visible list costs, block by block.
//
// The id chain is the only part that is not a straight prefix walk, because
// `$80:BF8D  CMP #$000C : BCC $BF96` sends the *low* ids forward to the named
// tests and the band test then drops the *high* ones into the same place. So
// the two arrivals are their own blocks and the named tests are counted one at
// a time on top of whichever arrived — additive rather than nested, which is
// what stops two id ranges times two named tests becoming four blocks.
//
// **`ID_BAND` and `ID_33` are one branch in the port and two here.** The ROM
// leaves `$33` on a `BEQ` and `$0C`..`$32` on the `BCC` under it, and a taken
// branch reached one instruction earlier is twelve cycles cheaper, so the id
// the coverage table calls the top of the band is the cheapest way out of it.
//
// Per call: `PROLOGUE + EMPTY == 1`, `LOOP_DONE + HIT + EMPTY == 1`, and
// `NEAR_X == FAR_Y + NEAR_Y`, `NEAR_Y == HIT + (the walk ending on a hit)`.
typedef enum {
  AT_BLK_PROLOGUE,     // $80:BF67..$BF77, including the first `DEX DEX`
  AT_BLK_EMPTY,        // $80:BF74 BEQ taken: `$9C` was zero, and the `CLC : RTL`
  AT_BLK_SELF,         // $80:BF7D BEQ taken: the record the caller named
  AT_BLK_INACTIVE,     // $80:BF83 BCC taken: flag bit 0 clear
  AT_BLK_NO_ID,        // $80:BF88 BEQ taken: collision id zero
  AT_BLK_ID_33,        // $80:BF92 BEQ taken: id exactly $33
  AT_BLK_ID_BAND,      // $80:BF94 BCC taken: $0C..$32
  AT_BLK_ARRIVE_LOW,   // $80:BF8D BCC taken: under $0C, on to the named tests
  AT_BLK_ARRIVE_HIGH,  // ...over $33, which falls into the same tests
  AT_BLK_NAME_MISS,    // one `CMP : BEQ` that did not match
  AT_BLK_NAME_HIT,     // ...and one that did, which is six dearer
  AT_BLK_FAR_X,        // $80:BFAD BCS taken: outside the six-pixel window
  AT_BLK_NEAR_X,       // ...inside it
  AT_BLK_FAR_Y,        // $80:BFBC BCS taken
  AT_BLK_NEAR_Y,       // ...inside, which is the answer
  AT_BLK_HIT,          // $80:BFBE PLD : SEC : RTL
  AT_BLK_LOOP_NEXT,    // $80:BFC3 BPL taken: another entry below this one
  AT_BLK_LOOP_DONE,    // ...not taken, and the shared `PLD : CLC : RTL`
  AT_POINT_BLOCK_COUNT,
} AtPointBlock;

typedef struct {
  uint16_t blocks[AT_POINT_BLOCK_COUNT];
} AtPointWork;

void actor_at_point_counted(Wram* w, uint16_t self, uint16_t x, uint16_t y,
                            AtPointRegs* out, AtPointWork* work);

// --- $80:BFC8 ---------------------------------------------------------------

// **Is something a walker would bump into standing at this point?** — the same
// search as `actor_at_point`, eighty bytes further down the bank, asking a
// different question about the same board.
//
// Third on the profiler's list at 2.2% of every instruction the game executes
// over 41,232 calls, and unlike the two above it this one is not an enemy's:
// both callers are in `$80:E4C1`, the **movement step validator**. That routine
// computes where a walker wants to be next, puts it through `$80:AE14`,
// `$80:A8B3`, this, and `$80:B422` in turn, and only if all four agree does it
// commit the candidate — `$80:E4FF  LDA $34 : STA $30`. So a set carry here
// means *the step is blocked*, and the routine is a collision test rather than
// a search: nothing about which record it found is used.
//
// Structurally it is `actor_at_point` line for line — the same backwards walk
// of `visible_actors`, the same `ACTOR_ACTIVE` test, the same six-pixel window
// on both axes, and the same `PLD` then explicit `SEC`/`CLC` at both exits. The
// two differences are the whole of the semantics:
//
//   1. **It takes no self argument.** Where `actor_at_point` skips one record
//      the caller names in A, this skips `W_PLAYER_A_RECORD` and
//      `W_PLAYER_B_RECORD` — *both players, always.* A walker may walk through
//      a player; something else has to decide what that costs. A is therefore
//      untouched on the way in, and a caller that hits the empty board gets its
//      own A back.
//   2. **A much narrower accept set**, below.
#define ACTOR_OBSTACLE_AT_POINT_ENTRY 0x80bfc8u

// The id filter, which is written down nowhere but the disassembly and is
// seven comparisons arranged so that two ranges and eight singletons fall out
// of them. `$80:BFED  CMP #$000C : BCC` splits the low ids off to the named
// chain; the band between `$0C` and `$33` goes out on `BEQ`+`BCC` against
// `$0033`; `$005C` and above goes out on `BCS`; and everything left over —
// which is `$34`..`$5B` — *falls through into the named chain as well*, so the
// `CMP #$0037` in it is live for exactly one id above the band.
//
// What survives: `$03`, `$04`, `$09`, `$0A`, `$0B`, and `$34`..`$5B` except
// `$37`. Note `$05` and `$06` in the named chain — the two players again, by id
// this time, so a player is refused twice over.
#define OBSTACLE_ID_BAND_LO 0x000c   // `$0C`..`$33` never
#define OBSTACLE_ID_BAND_HI 0x0033
#define OBSTACLE_ID_CEILING 0x005c   // `$5C` and up never
// The named chain, in the ROM's own order, which is not sorted:
#define OBSTACLE_ID_SKIP_COUNT 7
extern const uint16_t OBSTACLE_ID_SKIP[OBSTACLE_ID_SKIP_COUNT];

// Its scratch. Two words, not three: there is no self to file.
#define OBSTACLE_DP_X 0x3a
#define OBSTACLE_DP_Y 0x3c

// A, X and Y again, and again none of them tidy — same reasoning as
// `AtPointRegs`, except that `a_in` is what comes back when the board is empty,
// because nothing on that path writes A at all.
typedef struct {
  uint16_t a, x, y;
  bool blocked;  // the carry: `SEC` at $80:C040, `CLC` at $80:C047
  // Overflow, from the last `ADC #$0006` of the box test, if any record got
  // that far. Otherwise nothing wrote it and the caller's stands.
  bool v_set, v;
} ObstacleRegs;

void actor_obstacle_at_point(Wram* w, uint16_t a_in, uint16_t x, uint16_t y,
                             ObstacleRegs* out);

// `AtPointBlock` again, with the differences the routine has: two player tests
// where that one has a self test, a ceiling above the band, and seven named
// comparisons where that one has two. Everything from the window tests down is
// the same instructions — `$80:C021`-`$80:C03E` is `$80:BFA0`-`$80:BFBD` byte
// for byte — so **the last seven entries of `OBSTACLE_COST` must equal the last
// seven of `AT_POINT_COST`**, and two tables disagreeing there is a
// transcription error in one of them and nothing else.
//
// A separate table rather than a shared one, unlike `NEAREST_COST` above: there
// the ROM really does hold one routine twice over, here it holds two routines
// that were written from the same sketch.
typedef enum {
  OBST_BLK_PROLOGUE,     // $80:BFC8..$BFD6, including the first `DEX DEX`
  OBST_BLK_EMPTY,        // $80:BFD3 BEQ taken, and the `CLC : RTL`
  OBST_BLK_PLAYER_A,     // $80:BFDC BEQ taken: `CPY $D2`
  OBST_BLK_PLAYER_B,     // $80:BFE0 BEQ taken: `CPY $D4`
  OBST_BLK_INACTIVE,     // $80:BFE6 BCC taken
  OBST_BLK_NO_ID,        // $80:BFEB BEQ taken
  OBST_BLK_ID_33,        // $80:BFF5 BEQ taken: id exactly $33
  OBST_BLK_ID_BAND,      // $80:BFF7 BCC taken: $0C..$32
  OBST_BLK_ID_HIGH,      // $80:BFFC BCS taken: $5C and up
  OBST_BLK_ARRIVE_LOW,   // $80:BFF0 BCC taken: under $0C
  OBST_BLK_ARRIVE_HIGH,  // ...$34..$5B, which falls through into the same chain
  OBST_BLK_NAME_MISS,    // one of the seven `CMP : BEQ` that did not match
  OBST_BLK_NAME_HIT,     // ...and the one that did
  OBST_BLK_FAR_X,        // $80:C02E BCS taken
  OBST_BLK_NEAR_X,
  OBST_BLK_FAR_Y,        // $80:C03D BCS taken
  OBST_BLK_NEAR_Y,
  OBST_BLK_HIT,          // $80:C03F PLD : SEC : RTL — the step is refused
  OBST_BLK_LOOP_NEXT,    // $80:C044 BPL taken
  OBST_BLK_LOOP_DONE,    // ...not taken, and the shared `PLD : CLC : RTL`
  OBSTACLE_BLOCK_COUNT,
} ObstacleBlock;

typedef struct {
  uint16_t blocks[OBSTACLE_BLOCK_COUNT];
} ObstacleWork;

void actor_obstacle_at_point_counted(Wram* w, uint16_t a_in, uint16_t x,
                                     uint16_t y, ObstacleRegs* out,
                                     ObstacleWork* work);

// --- $80:BE0C, $80:BE41 — the two ends of a record's life -------------------
//
// Everything else in this file walks the display list. These two are what puts
// records on it and takes them off, and between them they have **143 call sites
// in the cartridge** — 80 for the allocator and 63 for the free — which is more
// than any other pair the port has taken and is the reason a fourteen- and a
// twenty-instruction routine are worth their own registry slots.
//
// The list is a stack, not a queue: the allocator pushes onto the head at
// `$7E:1B5E` through `ACTOR_NEXT`, so the newest record is the first one every
// walk in this file sees. And the *slot* it pushes is found by scanning the 32
// records **downwards** from the last one, `$7E:1ACA`, taking the first with
// `ACTOR_ACTIVE` clear. Two orders, opposite directions, and neither is the
// other's inverse — which is why the list's order says nothing about the array's.
//
// ## `ACTOR_ACTIVE` is the allocation bit
//
// `port/oam.h` has carried a note for several rounds saying that bit 0 of
// `ACTOR_FLAGS` is set on every live record ever sampled and that *what clears
// it had not been established*. `$80:BE41` clears it: `LDA #$0000 : STA
// $0000,Y`, the whole word, as the first thing it does once it has decided the
// free is allowed. And `$80:BE0C` is the only thing that sets it, with `LDA
// #$0001` — a record's flags start at exactly `$0001` and everything else about
// it, including `ACTOR_DRAW`, is written afterwards by whoever asked for it.
//
// So the bit is not a property of a drawn record at all. It is the allocator's
// free-list mark, read by `LSR : BCC` in both routines and by nothing else, and
// the two coverage sites that test it during a *walk* are untaken by all 43
// movies because a record on the list always has it.
//
// ## A free needs the caller's permission slip
//
//     TAY : LDA $0008 : CMP $000C,Y : BNE out
//
// `$0008` is `W_SCHED_CUR_TASK` and `$0C` is `ACTOR_THREAD` — so a record can
// only be freed by the thread that owns it, and every one of the 63 call sites
// is inside the actor whose record it is. Passing another actor's record does
// nothing at all, silently. That is the only ownership check anywhere in the
// port so far.
//
// ## Three exits, three different sources of N and Z
//
// `$80:BE41` is the clearest example yet of the thing this codebase keeps
// running into. Its "not yours" exit leaves the `CMP`'s flags; its "already
// free" exit leaves the `LSR`'s; and its working exit ends `PLD : RTL`, so N
// and Z come off **the caller's own direct page** — the same shape
// `apu_play_sfx` has, and the same instruction.
// The `PHD` is there because the unlink walk uses direct-page addressing with
// `D` forced to zero (`LDA #$0000 : TCD`), which is how a `JSL` from any page
// reaches `$7E:0038` and `$7E:0012,X` without knowing where it came from.
//
// `$80:BE0C` does the same trick one register over: `PHB : PEA $007E : PLB`
// costs a stray byte and the exit pulls twice, so its N and Z are the caller's
// **data bank**. See `port/sprite_cache.h`, which pays for the same idiom.
#define ACTOR_SLOT_ALLOC_ENTRY 0x80be0cu
#define ACTOR_SLOT_FREE_ENTRY 0x80be41u

// `LDY #$1ACA` — the last of the 32, where the scan starts.
#define ACTOR_SLOT_LAST (W_ACTOR_SLOTS + (ACTOR_SLOT_COUNT - 1) * ACTOR_SLOT_STRIDE)

typedef struct {
  uint16_t a, x, y;
  bool n, z, c;
} SlotAllocRegs;

// Take a free record, mark it live and push it onto the head of the list.
// Returns its offset in A, and **zero is not the failure value** — a full board
// comes back with A holding the last record's flags word shifted right one, and
// with carry set where a success clears it. `caller_db` answers for N and Z.
void actor_slot_alloc(Wram* w, uint16_t caller_db, SlotAllocRegs* out);

typedef struct {
  uint16_t a, x, y;
  bool n, z, c;
} SlotFreeRegs;

// Clear a record and unlink it, if the calling thread owns it. `rec` is A on
// entry; `caller_d` is the direct page the `JSL` arrived on, which the routine
// parks and restores and which decides N and Z on the one path that gets that
// far. `in_x`/`in_y` answer for the two paths that decline.
void actor_slot_free(Wram* w, uint16_t rec, uint16_t caller_d, uint16_t in_x,
                     uint16_t in_y, SlotFreeRegs* out);

#endif  // PORT_OAM_H
