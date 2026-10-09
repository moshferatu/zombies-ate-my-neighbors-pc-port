// What the game forgets between one thing and the next: see `port/clears.h`.

#include "port/clears.h"

#include <stddef.h>

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

void sched_tables_clear(Wram* w) {
  PORT_COVER(sched_tables_cleared);
  for (int i = 0; i < SCHED_THREADS; i++) {
    wram_w16(w, (uint16_t)(W_THREAD_WAIT + 2 * i), 0);
    wram_w16(w, (uint16_t)(W_THREAD_HANDLER + 2 * i), 0);
    wram_w16(w, (uint16_t)(W_THREAD_HANDLER_BANK + 2 * i), 0);
  }
  wram_w16(w, W_THREAD_COUNT, 1);
  wram_w16(w, (uint16_t)(W_THREAD_WAIT + wram_r16(w, W_SCHED_CUR_TASK)),
           THREAD_LIVE);
  for (int i = 0; i < SCHED_QUEUE_WORDS; i++) {
    wram_w16(w, (uint16_t)(W_VBL_QUEUE_A + 2 * i), 0);
    wram_w16(w, (uint16_t)(W_VBL_QUEUE_BETWEEN + 2 * i), 0);
    wram_w16(w, (uint16_t)(W_VBL_QUEUE_B + 2 * i), 0);
  }
  wram_w16(w, W_VBL_QUEUE_A_COUNT, 0);
  wram_w16(w, W_VBL_QUEUE_B_COUNT, 0);
}

void top_scores_default(Wram* w, const Rom* rom) {
  PORT_COVER(top_scores_defaulted);
  uint32_t avail = 0;
  const uint8_t* first = rom_ptr(rom, TOP_SCORES_ROM, &avail);
  if (first == NULL || avail < TOP_SCORES_FIRST_BYTES) return;
  for (uint32_t i = 0; i < TOP_SCORES_FIRST_BYTES; i++)
    wram_w8(w, W_TOP_SCORES + i, first[i]);
  const uint8_t* second = rom_ptr(rom, TOP_SCORES_ROM_SECOND, &avail);
  if (second == NULL || avail < TOP_SCORES_SECOND_BYTES) return;
  for (uint32_t i = 0; i < TOP_SCORES_SECOND_BYTES; i++)
    wram_w8(w, W_TOP_SCORES_SECOND + i, second[i]);
}

void actor_slots_clear(Wram* w) {
  PORT_COVER(actor_slots_cleared);
  for (int i = 0; i < ACTOR_SLOT_COUNT; i++)
    wram_w16(w, (uint16_t)(W_ACTOR_SLOTS + i * ACTOR_SLOT_STRIDE + ACTOR_FLAGS),
             0);
  wram_w16(w, W_ACTOR_LIST_HEAD, 0);
}
