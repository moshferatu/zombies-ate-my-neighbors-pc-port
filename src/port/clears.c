// What the game forgets between one thing and the next: see `port/clears.h`.

#include "port/clears.h"

#include "port/coverage.h"
#include "port/oam.h"

void clear_span(Wram* w, ClearSpan s) {
  PORT_COVER(span_cleared);
  for (uint32_t at = s.first; at <= s.last; at++) wram_w8(w, at, 0);
}

void threads_clear(Wram* w) {
  PORT_COVER(threads_cleared);
  clear_span(w, THREADS_CLEAR_PAGES);
  clear_span(w, THREADS_CLEAR_LAST);
}

void actor_slots_clear(Wram* w) {
  PORT_COVER(actor_slots_cleared);
  for (int i = 0; i < ACTOR_SLOT_COUNT; i++)
    wram_w16(w, (uint16_t)(W_ACTOR_SLOTS + i * ACTOR_SLOT_STRIDE + ACTOR_FLAGS),
             0);
  wram_w16(w, W_ACTOR_LIST_HEAD, 0);
}
