// What a level's start copies into WRAM -- see port/loads.h.

#include "port/loads.h"

#include "port/coverage.h"
#include "port/cpu.h"  // bus_r16

bool loads_from_cartridge(uint8_t bank, uint16_t at, uint16_t bytes) {
  return bank >= 0x80 && bank <= 0xbf && at >= 0x8000u &&
         (uint32_t)at + bytes <= 0x10000u;
}

// `bytes` from `from` to WRAM at `to`, a word at a time from the last down,
// and to `also` as well when there is one. Returns the last word read, which
// is the first of the table.
static uint16_t copy_down(Wram* w, const Rom* rom, uint32_t from, uint32_t to,
                          uint32_t also, uint16_t bytes) {
  uint16_t word = 0;
  for (int at = bytes - 2; at >= 0; at -= 2) {
    word = bus_r16(w, rom, from + (uint32_t)at);
    wram_w16(w, to + (uint32_t)at, word);
    if (also) wram_w16(w, also + (uint32_t)at, word);
  }
  return word;
}

uint16_t tile_attrs_load(Wram* w, const Rom* rom, uint16_t at, uint16_t bank) {
  PORT_COVER(tile_attrs_load);
  wram_w16(w, LOADS_DP_ATTRS_LEVEL, at);
  wram_w16(w, LOADS_DP_ATTRS_LEVEL + 2, bank);
  wram_w16(w, LOADS_DP_ATTRS_COPY, LOADS_TILE_ATTRS_AT);
  wram_w16(w, LOADS_DP_ATTRS_COPY + 2, 0x007e);
  return copy_down(w, rom, ((uint32_t)(bank & 0xffu) << 16) | at,
                   LOADS_TILE_ATTRS_AT, 0, LOADS_TILE_ATTRS_BYTES);
}

static uint32_t palette_pointer(Wram* w, uint16_t bank, uint16_t at) {
  wram_w16(w, LOADS_DP_PALETTE + 2, bank);
  wram_w16(w, LOADS_DP_PALETTE, at);
  return ((uint32_t)(bank & 0xffu) << 16) | at;
}

uint16_t palette_load(Wram* w, const Rom* rom, uint16_t bank, uint16_t at) {
  PORT_COVER(palette_load);
  const uint32_t from = palette_pointer(w, bank, at);
  const uint16_t first = copy_down(w, rom, from, LOADS_PALETTE_AT,
                                   LOADS_PALETTE_SHOWN_AT, LOADS_PALETTE_BYTES);
  wram_w16(w, LOADS_PALETTE_SHOWN_AT + LOADS_PALETTE_BYTES, 0);
  return first;
}

uint16_t palette_sprites_load(Wram* w, const Rom* rom, uint16_t bank,
                              uint16_t at) {
  PORT_COVER(palette_sprites_load);
  const uint32_t from = palette_pointer(w, bank, at);
  return copy_down(w, rom, from, LOADS_PALETTE_SPRITES_AT, 0,
                   LOADS_PALETTE_BYTES);
}

void hud_reset(Wram* w, uint16_t page) {
  PORT_COVER(hud_reset);
  // Nothing it last drew is what it will be asked to draw.
  for (int i = 0; i < LOADS_HUD_DRAWN_WORDS; i++)
    wram_w16(w, LOADS_HUD_DRAWN_AT + 2u * (uint32_t)i, 0xffff);
  wram_w16(w, LOADS_HUD_CHANGED, 0);
  wram_w16(w, (uint16_t)(page + LOADS_HUD_DP_CLEARED), 0);
  for (uint32_t at = 0; at < LOADS_HUD_SHADOW_BYTES; at += 2)
    wram_w16(w, LOADS_HUD_SHADOW_AT + at, 0);
}
