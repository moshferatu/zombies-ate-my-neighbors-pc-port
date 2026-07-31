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
#include "port/collide.h"
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
void actor_cull(Wram* w);

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

#endif
