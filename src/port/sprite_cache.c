#include "port/sprite_cache.h"

#include "assets/sprite.h"

// The two 128-entry tables at `$80:B547` and `$80:B647` that this routine
// indexes are the slot geometry Phase 2 already proved: `sprite_slot_vram()`
// and `sprite_slot_tile()` reproduce all 128 entries of each exactly (see
// `verify-sprites`). Reusing them here rather than re-reading the ROM keeps one
// definition of where a slot lives.
//
// The third table it uses, `$80:B447`, is the eviction scan's "next slot":
// entry `i` is `(i * 2 + 2) & $FF`, so indexed by an already-doubled slot it
// yields the following slot, wrapping after 128. That is simple enough to
// inline as `(x + 2) & 0xff` below.

void sprite_cache_age(Wram* w) {
  uint16_t aged = (uint16_t)(wram_r16(w, W_SCHED_TICK) - 1);
  for (int slot = 0; slot < SPRITE_SLOTS; slot++)
    wram_w16(w, W_SPRITE_SLOT_TICK + slot * 2, aged);
}

uint16_t sprite_frame_tile(Wram* w, uint16_t frame) {
  uint16_t tick = wram_r16(w, W_SPRITE_TICK);
  uint16_t fx = (uint16_t)(frame * 2);  // the ROM's index: frame number x2

  // Already resident? `frame_slot` holds the slot x2, or a negative word.
  uint16_t entry = wram_r16(w, W_FRAME_SLOT + fx);
  if (!(entry & 0x8000)) {
    wram_w16(w, W_SPRITE_SLOT_TICK + entry, tick);
    return sprite_slot_tile(entry / 2);
  }

  // Miss. `$80:B9EC` parks the doubled frame index in scratch and it stays
  // there afterwards, so the port writes it too — the harness diffs WRAM.
  wram_w16(w, W_SPRITE_SCRATCH_F, fx);

  // Evict. Start one past where the last eviction stopped and take the first
  // slot that has not been drawn this frame.
  //
  // The ROM's loop has no bound: if every slot's tick equals `sprite_tick` it
  // scans forever. That cannot happen while fewer than 128 frames are on screen
  // in one pass, which the 128-sprite OAM makes impossible, so the bound here
  // is not a behaviour change — it is a hang the port declines to reproduce. If
  // it ever fired, the slot we settle on would diverge and the harness would
  // say so.
  uint16_t sx = (uint16_t)((wram_r16(w, W_SPRITE_LRU_SLOT) + 2) & 0xff);
  for (int guard = 0; guard < SPRITE_SLOTS; guard++) {
    if (wram_r16(w, W_SPRITE_SLOT_TICK + sx) != tick) break;
    sx = (uint16_t)((sx + 2) & 0xff);
  }
  wram_w16(w, W_SPRITE_LRU_SLOT, sx);
  wram_w16(w, W_SPRITE_SLOT_TICK + sx, tick);

  // Whatever was in the slot is no longer anywhere.
  uint16_t evicted = wram_r16(w, W_SLOT_FRAME + sx);
  if (!(evicted & 0x8000)) wram_w16(w, W_FRAME_SLOT + evicted, 0xffff);

  wram_w16(w, W_FRAME_SLOT + fx, sx);
  wram_w16(w, W_SLOT_FRAME + sx, fx);

  // Queue the 128-byte frame for the vblank DMA. The source address is built
  // the way `$80:BA29` builds it — from the frame array base in `$7E`/`$80`
  // rather than a constant, so a mod that relocates the array still works.
  //
  // `LSR : XBA : LSR` puts `(frame & $FF) * 128` in the low bits with the top
  // nibble of the frame number shifted down into the bottom; the `AND #$FF80`
  // afterwards is what discards it. The stray carry the second `LSR` feeds into
  // the `ADC` is discarded by the same mask.
  uint16_t swapped = (uint16_t)(((frame & 0xff) << 8) | (frame >> 8));
  uint16_t carry = (uint16_t)(swapped & 1);
  uint16_t src = (uint16_t)((swapped >> 1) + wram_r16(w, W_SPRITE_FRAME_BASE) + carry);
  src &= 0xff80;
  uint16_t bank = (uint16_t)((frame >> 8) + wram_r16(w, W_SPRITE_FRAME_BANK));

  uint16_t q = wram_r16(w, W_SPRITE_UPLOAD_COUNT);
  wram_w16(w, W_SPRITE_UPLOAD_SRC + q, src);
  wram_w16(w, W_SPRITE_UPLOAD_BANK + q, bank);
  wram_w16(w, W_SPRITE_UPLOAD_DEST + q, sprite_slot_vram(sx / 2));
  wram_w16(w, W_SPRITE_UPLOAD_COUNT, (uint16_t)(q + 2));

  return sprite_slot_tile(sx / 2);
}
