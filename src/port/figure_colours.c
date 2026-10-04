// $82:8138  the big figure's colours -- see port/figure_colours.h.

#include "port/figure_colours.h"

#include "port/coverage.h"
#include "port/thread.h"

bool figure_colours_supported(uint16_t at, uint8_t bank) {
  return bank >= 0x80 && bank <= 0x9f && at >= 0x8000u &&
         at <= 0x10000u - 2 * FIGURE_COLOURS_COUNT;
}

int figure_colours_set(Wram* w, const Rom* rom, uint16_t at, uint8_t bank) {
  PORT_COVER(figure_colours_set);
  for (uint16_t i = 0; i < 2 * FIGURE_COLOURS_COUNT; i += 2) {
    const uint16_t colour = rom_word(rom, ((uint32_t)bank << 16) + at + i);
    wram_w16(w, FIGURE_COLOURS_ROW + i, colour);
    wram_w16(w, FIGURE_COLOURS_ROW_COPY + i, colour);
  }
  return vbl_queue_b_add(w, FIGURE_COLOURS_JOB, FIGURE_COLOURS_JOB_BANK);
}
