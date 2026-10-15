// $82:8138  the big figure's colours -- see port/figure_colours.h.

#include "port/figure_colours.h"

#include "port/coverage.h"
#include "port/thread.h"

bool figure_colours_supported(uint16_t at, uint8_t bank) {
  return bank >= 0x80 && bank <= 0x9f && at >= 0x8000u &&
         at <= 0x10000u - 2 * FIGURE_COLOURS_COUNT;
}

// Sixteen colours from `bank:at` into a row and its copy.
static void copy_row(Wram* w, const Rom* rom, uint16_t at, uint8_t bank,
                     uint16_t row, uint16_t copy) {
  for (uint16_t i = 0; i < 2 * FIGURE_COLOURS_COUNT; i += 2) {
    const uint16_t colour = rom_word(rom, ((uint32_t)bank << 16) + at + i);
    wram_w16(w, (uint16_t)(row + i), colour);
    wram_w16(w, (uint16_t)(copy + i), colour);
  }
}

int figure_colours_set(Wram* w, const Rom* rom, uint16_t at, uint8_t bank) {
  PORT_COVER(figure_colours_set);
  copy_row(w, rom, at, bank, FIGURE_COLOURS_ROW, FIGURE_COLOURS_ROW_COPY);
  return vbl_queue_b_add(w, FIGURE_COLOURS_JOB, FIGURE_COLOURS_JOB_BANK);
}

int figure_sprite_colours_set(Wram* w, const Rom* rom, uint16_t at,
                              uint8_t bank) {
  PORT_COVER(figure_sprite_colours_set);
  copy_row(w, rom, at, bank, FIGURE_SPRITE_COLOURS_ROW,
           FIGURE_SPRITE_COLOURS_ROW_COPY);
  return vbl_queue_a_add(w, FIGURE_SPRITE_COLOURS_JOB,
                         FIGURE_SPRITE_COLOURS_JOB_BANK);
}
