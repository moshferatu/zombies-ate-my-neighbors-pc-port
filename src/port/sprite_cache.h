// The 128-slot VRAM cache of 16x16 sprite frames.
//
// Phase 2 decoded everything about a sprite that is a *format* — the frames,
// the metasprites, the OAM composition — but stopped at one hole. `sprite_emit()`
// needs to know which VRAM tile a frame is currently loaded at, and that is not
// in the ROM: only 128 of the game's 4096 frames fit in the 16 KB sprite
// character area at once, so the game keeps an LRU cache and uploads on demand.
// `assets/sprite.h` left it as a `SpriteTileFn` callback for exactly this
// reason. This is that callback, ported.
//
// It is the natural first routine of Phase 3: it is called more than any other
// piece of game logic in a traced run (10,354 times in 2400 frames, second only
// to the LZSS and APU byte-pushers), it is a leaf that never yields to the
// scheduler, and every byte it touches is WRAM the harness can diff.
//
// State, all in `port/wram.h`:
//
//   frame_slot[4096]       W_FRAME_SLOT        slot x2 holding this frame, or <0
//   slot_frame[128]        W_SLOT_FRAME        the reverse map, used to evict
//   sprite_slot_tick[128]  W_SPRITE_SLOT_TICK  the tick a slot was last drawn at
//   sprite_lru_slot        W_SPRITE_LRU_SLOT   x2; where the eviction scan resumes
//   sprite_tick            W_SPRITE_TICK       this frame's tick
//   the upload queue       W_SPRITE_UPLOAD_*   drained in vblank by `$80:B947`
//
// Port code: libc only.

#ifndef PORT_SPRITE_CACHE_H
#define PORT_SPRITE_CACHE_H

#include <stdbool.h>
#include <stdint.h>

#include "port/wram.h"

// `$80:B9C7` — stamp every slot with `sched_tick - 1`.
//
// Called once per frame before the drawing pass, and it is what makes the LRU
// scan work: a slot is "in use this frame" exactly when its tick equals
// `sprite_tick`, so ageing every slot by one makes them all evictable again.
void sprite_cache_age(Wram* w);

// `$80:B9D6` — resolve a frame number to the OAM tile number it is loaded at,
// loading it into the cache first if it is not already resident.
//
// On a miss this evicts a slot, rewrites both halves of the frame<->slot map,
// and appends the frame to the vblank upload queue — so calling this has the
// side effect of scheduling a DMA, which is why it cannot be a pure lookup.
//
// The eviction scan starts one slot past `sprite_lru_slot` and takes the first
// slot not already drawn this frame, wrapping at 128. If *every* slot has been
// drawn this frame the ROM spins forever; see the note in the implementation.
//
// Returns the value the ROM leaves in A: the slot's OAM tile word.
uint16_t sprite_frame_tile(Wram* w, uint16_t frame);

// --- $80:C05A  sprite_cache_init --------------------------------------------
//
// Point the cache at a frame array and declare all 128 slots empty. Five call
// sites — `$80:85D9`, `$80:8644`, `$80:8BEB`, `$82:B58E`, `$82:BF09` — three of
// them in the loader in bank `$80` and two in the boss code in `$82`, and 31
// calls across the corpus, so it runs about three times a movie.
//
// **It is the largest single piece of work in the game that is not a loop over
// anything**: 16,900 instructions a call, all of it two stores repeated, and at
// 31 calls across the corpus that is still 524,241 instructions — a fifth of a
// per cent of everything the ROM executes, spent writing `$FFFF` 4,225 times.
// The cost is entirely the `frame_slot` direction: 4,096 frames, one word each,
// against 128 slots the other way.
//
// ## The two loops overlap by one word, and it does not matter
//
//     LDX #$2000 : LDA #$FFFF : STA $2128,X : DEX : DEX : BPL -
//
// `BPL` and not `BNE`, so the loop runs with X at zero as well and clears
// **`$2002` bytes, not `$2000`** — one word past the end of `frame_slot`, which
// is `slot_frame`'s first entry. The second loop writes it again four
// instructions later. Reproduced rather than tidied, because the port's job is
// the bytes and the bytes are the same either way; it is recorded because a
// reader who counted 4,096 entries and found 4,097 stores would otherwise have
// to work out which of us was wrong.
//
// ## `PLB : PLB`, and the flags a caller gets back
//
//     PHB : PEA $007E : PLB ... PLB : PLB : RTL
//
// `PEA` pushes two bytes and `PLB` pulls one, so there is a stray `$00` sitting
// under the saved bank for the whole routine and the exit has to pull twice.
// The consequence is the interesting part: the **last** `PLB` is the one that
// restores the caller's own data bank, and `PLB` sets N and Z from the byte it
// pulls, so what a caller reads back in N and Z is a fact about its own `DB`.
// The port keeps meeting routines whose flags describe the caller rather than
// the answer, one save-and-restore instruction at a time — `apu_play_sfx`
// through `PLD`, `sin_deg` and `terrain_point_bit2` through `PLX`,
// `thread_call_handler` and `$80:BE0C` through `PLB` — and, like all of them,
// nothing reads it.
#define SPRITE_CACHE_INIT_ENTRY 0x80c05au

// What an unmapped frame and an empty slot both look like. The lookup tests it
// with `BPL`/`BMI`, so any negative word would do; this is the one written.
#define SPRITE_CACHE_EMPTY 0xffff

// How many frames the forward map covers, which is the `LDX #$2000` above read
// as words. `assets/sprite.h` has the slot count; this is the other axis, and
// it lives here because it is a property of the WRAM table rather than of the
// format.
#define SPRITE_FRAME_COUNT 4096

typedef struct {
  uint16_t a, x, y;
  bool n, z;
} SpriteCacheInitRegs;

// `base`/`bank` are the A and Y the ROM stores straight into
// `W_SPRITE_FRAME_BASE` and `W_SPRITE_FRAME_BANK`; `caller_db` is the data bank
// the `JSL` arrived on, and exists only to answer for N and Z. `out` may be
// NULL.
void sprite_cache_init(Wram* w, uint16_t base, uint16_t bank, uint16_t caller_db,
                       SpriteCacheInitRegs* out);

#endif
