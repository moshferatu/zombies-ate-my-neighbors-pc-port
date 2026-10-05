// The text layer's tile map: see `port/textmap.h`.

#include "port/textmap.h"

#include "port/coverage.h"

void text_map_clear(Wram* w) {
  PORT_COVER(text_map_cleared);
  for (uint32_t at = 0; at < TEXT_MAP_BYTES; at++)
    wram_w8(w, W_TEXT_MAP + at, 0);
}
