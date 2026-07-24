// The per-frame sprite pass: the display list, and the OAM buffer it fills.
//
// `sprite_build_oam` (`$80:BD1F`) is what the scheduler runs once per pass, and
// it is the routine the whole sprite path hangs off. It opens with three list
// routines, walks whatever survived them, and hands each survivor to one of the
// four emitters Phase 2 already ported (`src/assets/sprite.c`). Those three
// openers are what live here:
//
//   $80:BC7F  actor_depth_sort   one bubble pass over the display list
//   $80:BCE2  actor_cull         list -> visible_actors[], by camera window
//   $80:BC23  oam_buffer_clear   park all 128 OAM entries off screen
//
// They are the shortest step into real per-frame game logic: each is a leaf that
// never yields, each runs 1,016 times in `movies/level1.zmv`, and between them
// they read and write nothing but WRAM. `sprite_build_oam` itself, and the
// overlap pass at `$80:BEC9` it ends with, are not ported yet.
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

#include <stdint.h>

#include "port/wram.h"

// --- A display-list record --------------------------------------------------

#define ACTOR_SLOT_COUNT 32
#define ACTOR_SLOT_STRIDE 0x14

#define ACTOR_FLAGS 0x00  // see the three bits below
#define ACTOR_X 0x02      // world X, or screen X when ACTOR_SCREEN_SPACE
#define ACTOR_Y 0x06      // world Y, likewise
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

#endif
