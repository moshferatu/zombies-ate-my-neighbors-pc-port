#include "port/oam.h"

#include <stdbool.h>

static uint16_t flags_of(const Wram* w, uint16_t rec) {
  return wram_r16(w, (uint32_t)rec + ACTOR_FLAGS);
}

static uint16_t next_of(const Wram* w, uint16_t rec) {
  return wram_r16(w, (uint32_t)rec + ACTOR_NEXT);
}

static void set_next(Wram* w, uint16_t rec, uint16_t to) {
  wram_w16(w, (uint32_t)rec + ACTOR_NEXT, to);
}

// ---------------------------------------------------------------------------
// $80:BC7F  actor_depth_sort
// ---------------------------------------------------------------------------

// Does the successor `b` belong in front of `a`?
//
// `ACTOR_SORT_FIRST` decides it whenever the two records disagree about that
// bit — the ROM tests `(fa ^ fb) & $20` and then looks only at `fb`. Otherwise
// the larger Y wins, compared unsigned exactly as `CMP` does it.
static bool swap_needed(const Wram* w, uint16_t a, uint16_t b) {
  uint16_t fa = flags_of(w, a), fb = flags_of(w, b);
  if ((fa ^ fb) & ACTOR_SORT_FIRST) return (fb & ACTOR_SORT_FIRST) != 0;
  return wram_r16(w, (uint32_t)a + ACTOR_Y) < wram_r16(w, (uint32_t)b + ACTOR_Y);
}

// Exchange `cur` with its successor `next`, given the record in front of `cur`.
// `prev` is 0 for the head, where the list head itself is what moves.
static void swap_with_next(Wram* w, uint16_t prev, uint16_t cur, uint16_t next) {
  set_next(w, cur, next_of(w, next));
  set_next(w, next, cur);
  if (prev == 0)
    wram_w16(w, W_ACTOR_LIST_HEAD, next);
  else
    set_next(w, prev, next);
}

uint16_t actor_depth_sort(Wram* w) {
  uint16_t cur = wram_r16(w, W_ACTOR_LIST_HEAD);
  if (cur == 0) return 0;
  uint16_t next = next_of(w, cur);
  if (next == 0) return cur;

  // The head has no predecessor to relink, so the list head itself moves.
  if (swap_needed(w, cur, next)) swap_with_next(w, 0, cur, next);

  // `$80:BCAD`, which both branches above fall into: the ROM advances with
  // `STX $38 : TYX` whether or not it swapped. After a swap that steps `cur`
  // onto the record which just moved to the front, so the same pair is compared
  // a second time — and the walk then has the *wrong* predecessor for one
  // iteration, since `$38` holds the record that moved back.
  //
  // Neither costs anything, because that second look can never swap: it is
  // looking at a pair the first look just put in order, and both keys are
  // strict. Transcribing it anyway keeps the port walking the list in exactly
  // the order the listing does, which is what the diff is checking.
  uint16_t prev = cur;
  cur = next;

  for (;;) {
    next = next_of(w, cur);
    if (next == 0) return cur;
    if (!swap_needed(w, cur, next)) {
      prev = cur;
      cur = next;
      continue;
    }
    swap_with_next(w, prev, cur, next);
    // `cur` stays where it is — it has moved one place back, so the record
    // after it is the one the walk has not seen yet.
    prev = next;
  }
}

// ---------------------------------------------------------------------------
// $80:BCE2  actor_cull
// ---------------------------------------------------------------------------

// The camera window on one axis, as the ROM tests it: `CMP #$FF80 : BCS in`
// then `CMP #$0180 : BCS out`. Unsigned, so it is one wrapped range — 128 px
// behind the origin through 383 px ahead of it.
static bool in_window(uint16_t delta) {
  return delta >= 0xff80 || delta < 0x0180;
}

void actor_cull(Wram* w) {
  uint16_t camera_x = wram_r16(w, W_CAMERA_X);
  uint16_t camera_y = wram_r16(w, W_CAMERA_Y);
  uint16_t count = 0;

  for (uint16_t rec = wram_r16(w, W_ACTOR_LIST_HEAD); rec != 0;
       rec = next_of(w, rec)) {
    uint16_t flags = flags_of(w, rec);
    if (!(flags & ACTOR_DRAW)) continue;
    if (!(flags & ACTOR_SCREEN_SPACE)) {
      if (!in_window((uint16_t)(wram_r16(w, (uint32_t)rec + ACTOR_X) - camera_x)))
        continue;
      if (!in_window((uint16_t)(wram_r16(w, (uint32_t)rec + ACTOR_Y) - camera_y)))
        continue;
    }
    wram_w16(w, W_VISIBLE_ACTORS + count, rec);
    count += 2;
  }

  wram_w16(w, W_VISIBLE_ACTOR_COUNT, count);
}

// ---------------------------------------------------------------------------
// $80:BC23  oam_buffer_clear
// ---------------------------------------------------------------------------

void oam_buffer_clear(Wram* w) {
  // The ROM does this with the direct page pointed at the buffer, 8-bit index
  // registers and sixteen `STY` per iteration, walking the page forward $40 at
  // a time. All of that is addressing: what it leaves behind is one byte per
  // entry.
  for (int entry = 0; entry < OAM_ENTRIES; entry++)
    wram_w8(w, W_OAM_BUFFER + (uint32_t)entry * 4 + 1, 0xe0);
  for (uint32_t i = 0; i < OAM_HIGH_BYTES; i++) wram_w8(w, W_OAM_HIGH + i, 0xaa);
}
