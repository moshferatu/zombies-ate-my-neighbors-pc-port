#include "port/camera.h"

#include "port/coverage.h"
#include "port/terrain.h"

// The map lives in bank $7F and the destination the callers set up is in $7E,
// so both ends of the copy are WRAM and neither needs the bus dispatch
// `port/lzss.c` has to do. `$56` is still *read* rather than hardcoded, because
// it belongs to the caller and a routine that quietly substituted its own idea
// of the bank would agree with the ROM until the day a caller changed its mind.
//
// What is assumed is narrower and worth naming: that the bank is $7E or $7F.
// Any other one and this maps it into WRAM anyway instead of going to the
// cartridge. That is not guarded because `verify` is the guard — it compares
// all 128 KB after every call, so the first caller ever to pass something else
// diverges immediately rather than quietly writing to the wrong place.
static uint32_t wram_off(uint16_t bank, uint16_t addr) {
  return ((uint32_t)(bank & 1u) << 16) | addr;
}

void tilemap_copy_column(Wram* w, uint16_t count, uint16_t col, uint16_t row,
                         TilemapCopyRegs* out) {
  // `JSL $80AD1C : STA $50` — where the top tile of the strip lives.
  TilemapAddrRegs addr;
  tilemap_tile_addr(w, col, row, &addr);
  uint16_t src = addr.a;
  wram_w16(w, CAM_DP_SRC, src);
  wram_w16(w, CAM_DP_SRC_BANK, TILEMAP_SRC_BANK);

  uint16_t dst = wram_r16(w, CAM_DP_DST);
  uint16_t dst_bank = wram_r16(w, CAM_DP_DST_BANK);
  uint16_t stride = wram_r16(w, W_TILEMAP_ROW_BYTES);
  uint16_t threshold = wram_r16(w, W_TILE_PRIORITY_BELOW);

  // `DEX : BNE` — a do-while, so zero means 65,536. See the header.
  uint16_t x = count;
  uint16_t y = 0;
  do {
    uint16_t tile = wram_r16(w, wram_off(TILEMAP_SRC_BANK, src));
    uint32_t at = wram_off(dst_bank, (uint16_t)(dst + y));
    wram_w16(w, at, tile);
    if ((tile & TILEMAP_COPY_MASK) < threshold) {
      PORT_COVER(column_priority);
      wram_w16(w, at, (uint16_t)(tile | TILEMAP_PRIORITY_BIT));
    } else {
      PORT_COVER(column_plain);
    }
    src = (uint16_t)(src + stride);
    wram_w16(w, CAM_DP_SRC, src);
    y = (uint16_t)(y + 2);
  } while (--x != 0);

  // `PLA : ASL A : CLC : ADC $54 : STA $54` — the count back off the stack,
  // doubled into bytes, added to the destination. The flags are this add's.
  uint32_t sum = (uint32_t)(uint16_t)(count << 1) + dst;
  wram_w16(w, CAM_DP_DST, (uint16_t)sum);

  out->a = (uint16_t)sum;
  out->x = 0;  // where `DEX : BNE` leaves it
  out->y = y;
  out->n = (sum & 0x8000u) != 0;
  out->z = (uint16_t)sum == 0;
  out->c = sum > 0xffffu;
}

// --- $80:A401  tilemap_buffer_alloc ----------------------------------------

bool tilemap_buffer_alloc_supported(const Wram* w, uint16_t size) {
  // `LDA $CC : SEC : SBC $01,S : BCC retry` — carry clear means a borrow, and a
  // borrow is the spin. Ten traces and it has never happened once.
  return wram_r16(w, W_TILEMAP_ARENA_LEFT) >= size;
}

void tilemap_buffer_alloc(Wram* w, uint16_t size, TilemapAllocRegs* out) {
  uint16_t left = wram_r16(w, W_TILEMAP_ARENA_LEFT);
  wram_w16(w, W_TILEMAP_ARENA_LEFT, (uint16_t)(left - size));  // the guard
                                                               // proved this
                                                               // does not
                                                               // borrow
  uint16_t next = wram_r16(w, W_TILEMAP_ARENA_NEXT);
  uint32_t bumped = (uint32_t)next + size;
  wram_w16(w, W_TILEMAP_ARENA_NEXT, (uint16_t)bumped);

  // `TAY` before the add and `TYA` after it: what comes back is the address the
  // caller may use, which is where the arena stood *before* the bump.
  out->a = next;
  out->y = next;
  out->x = size;  // `PLX` pulls the argument back
  // N and Z are the `TYA`'s and describe that address; carry is still the
  // `ADC`'s, two instructions earlier, and nothing since has touched it.
  out->n = (next & 0x8000u) != 0;
  out->z = next == 0;
  out->c = bumped > 0xffffu;
}
