// What the game forgets between one thing and the next: see `port/clears.h`.

#include "port/clears.h"

#include "port/coverage.h"

void clear_span(Wram* w, ClearSpan s) {
  PORT_COVER(span_cleared);
  for (uint32_t at = s.first; at <= s.last; at++) wram_w8(w, at, 0);
}

void threads_clear(Wram* w) {
  PORT_COVER(threads_cleared);
  clear_span(w, THREADS_CLEAR_PAGES);
  clear_span(w, THREADS_CLEAR_LAST);
}
