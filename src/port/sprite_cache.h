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

#endif
