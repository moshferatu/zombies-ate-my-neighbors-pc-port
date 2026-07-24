#include "port/oam.h"

#include <stdbool.h>
#include <string.h>

#include "assets/sprite.h"
#include "port/coverage.h"
#include "port/sprite_cache.h"

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
  if ((fa ^ fb) & ACTOR_SORT_FIRST) {
    PORT_COVER(sort_key_first);
    return (fb & ACTOR_SORT_FIRST) != 0;
  }
  PORT_COVER(sort_key_y);
  return wram_r16(w, (uint32_t)a + ACTOR_Y) < wram_r16(w, (uint32_t)b + ACTOR_Y);
}

// Exchange `cur` with its successor `next`, given the record in front of `cur`.
// `prev` is 0 for the head, where the list head itself is what moves.
static void swap_with_next(Wram* w, uint16_t prev, uint16_t cur, uint16_t next) {
  PORT_COVER_IF(prev == 0, sort_swap_head, sort_swap_mid);
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
    if (!(flags & ACTOR_DRAW)) { PORT_COVER(cull_undrawn); continue; }
    if (!(flags & ACTOR_SCREEN_SPACE)) {
      if (!in_window((uint16_t)(wram_r16(w, (uint32_t)rec + ACTOR_X) - camera_x))) {
        PORT_COVER(cull_offscreen);
        continue;
      }
      if (!in_window((uint16_t)(wram_r16(w, (uint32_t)rec + ACTOR_Y) - camera_y))) {
        PORT_COVER(cull_offscreen);
        continue;
      }
    } else {
      PORT_COVER(cull_screen);
    }
    wram_w16(w, W_VISIBLE_ACTORS + count, rec);
    count += 2;
  }

  wram_w16(w, W_VISIBLE_ACTOR_COUNT, count);
}

// ---------------------------------------------------------------------------
// $80:BEC9  actor_overlap_pass
// ---------------------------------------------------------------------------

// The 16x16 box test, on one axis, exactly as `$80:BEE1` does it: subtract,
// re-bias by 8, and take it as one unsigned range. Signed or not, the two
// records are within 8 pixels of each other.
static bool within_8px(uint16_t a, uint16_t b) {
  return (uint16_t)(b - a + 8) < 0x0010;
}

bool actor_overlap_pass(Wram* w) {
  uint16_t count = wram_r16(w, W_VISIBLE_ACTOR_COUNT);
  if (count == 0) return true;  // `LDY $9C : BEQ` — not even $3C is written
  uint16_t outer = (uint16_t)(count - 2);
  if (outer == 0) return true;  // one record; nothing to pair it with

  // The ROM writes its four scratch words as it goes; the port accumulates them
  // and writes at the end. Only the values at the `RTL` are observable — the
  // harness diffs the routine, not its instructions — and holding them here is
  // what lets a decline leave WRAM untouched.
  bool tested = false;
  uint16_t id = 0, ox = 0, oy = 0;

  for (;;) {
    uint16_t a = wram_r16(w, W_VISIBLE_ACTORS + outer);
    outer -= 2;

    uint16_t a_id = wram_r16(w, (uint32_t)a + ACTOR_COLLIDE_ID);
    if (a_id == 0) PORT_COVER(overlap_no_id);
    if (a_id != 0) {
      tested = true;
      id = a_id;
      ox = wram_r16(w, (uint32_t)a + ACTOR_X);
      oy = wram_r16(w, (uint32_t)a + ACTOR_Y);

      // Everything before `a` in the list, so each unordered pair is tested
      // once. The ROM walks down with `DEY DEY : BPL`, which is why index 0 is
      // included and the walk ends by going negative.
      for (uint16_t inner = outer;; inner -= 2) {
        uint16_t b = wram_r16(w, W_VISIBLE_ACTORS + inner);
        uint16_t b_id = wram_r16(w, (uint32_t)b + ACTOR_COLLIDE_ID);
        if (b_id == id) PORT_COVER(overlap_same_id);
        // Split across the two axes only so each half is separately countable:
        // the X test firing is what says the 16-px threshold is exercised at
        // all, and the pair test firing is what says the dispatch below is.
        if (b_id != 0 && b_id != id &&
            within_8px(ox, wram_r16(w, (uint32_t)b + ACTOR_X))) {
          PORT_COVER(overlap_near_x);
          if (within_8px(oy, wram_r16(w, (uint32_t)b + ACTOR_Y))) {
            // `$80:BF0D  PHY : JSR $BE8F : PLY`. That is where the port stops:
            // `$80:BE8F` hands both records to `$80:8480`, which builds a call
            // frame from the actor's own thread slot and `RTL`s into its
            // handler — arbitrary game logic, none of it ported. Nothing has
            // been written yet, so the ROM can run this call from the top.
            PORT_COVER(overlap_hit);
            return false;
          }
        }
        if (inner == 0) break;
      }
    }

    if (outer == 0) break;
  }

  wram_w16(w, W_OVERLAP_CURSOR, 0);
  if (tested) {
    wram_w16(w, W_OVERLAP_ID, id);
    wram_w16(w, W_OVERLAP_X, ox);
    wram_w16(w, W_OVERLAP_Y, oy);
  }
  return true;
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

// ---------------------------------------------------------------------------
// $80:BD1F  sprite_build_oam
// ---------------------------------------------------------------------------

// The VRAM cache lookup, as `sprite_emit` wants it. This is the `JSR $80:B9D6`
// at `$80:BA98`, and routing it through the callback rather than calling it
// directly is what keeps `assets/sprite.c` free of runtime state.
static uint16_t frame_tile(uint16_t frame, void* ctx) {
  return sprite_frame_tile((Wram*)ctx, frame);
}

// Everything the pass reads out of one record before it can draw it. Returned
// rather than written straight to direct page so the "is this drawable at all"
// decision and the scratch writes stay in one place, in the order the ROM makes
// them.
typedef struct {
  uint16_t attr_or, attr_and;
  int16_t ox, oy;
  uint16_t ptr, bank;  // the metasprite pointer, split as the record holds it
  SpriteFlip flip;
} DrawArgs;

// How far the record got through `$80:BD46`..`$80:BD9F`: 0 if its metasprite
// pointer is not even in ROM (`$80:BD91`), 1 if it is but the bank is not $8F
// or $90 (`$80:BD9A`/`$BD9F`), 2 if the pass will draw it.
//
// Three answers rather than a bool because the pass commits its scratch as it
// goes, and a record can fail after some of it is already written: the
// attributes and the origin are set for *every* drawable record, `$8A` survives
// the first check, and `$8C` only the second. The diff compares those bytes, so
// where each one stops matters.
//
// The attribute pair is how an actor overrides what its frames were authored
// with. Priority always (bit 3 picks 3 over 2), and on top of that
// `ACTOR_ATTR_SET` ORs the record's own attribute word in *and* masks the
// piece's palette bits away, so one metasprite can be drawn in any palette.
static int draw_args(const Wram* w, uint16_t rec, DrawArgs* d) {
  uint16_t flags = flags_of(w, rec);

  if (flags & ACTOR_PRIORITY_TOP) PORT_COVER(draw_priority_top);
  d->attr_or = (flags & ACTOR_PRIORITY_TOP) ? 0x3000 : 0x2000;
  d->attr_and = 0xffff;
  if (flags & ACTOR_ATTR_SET) {
    PORT_COVER(draw_attr_set);
    d->attr_or |= wram_r16(w, (uint32_t)rec + ACTOR_ATTR);
    d->attr_and = 0xf1ff;
  }

  if (flags & ACTOR_SCREEN_SPACE) {
    PORT_COVER(draw_screen);
    d->ox = (int16_t)wram_r16(w, (uint32_t)rec + ACTOR_X);
    d->oy = (int16_t)wram_r16(w, (uint32_t)rec + ACTOR_Y);
  } else {
    d->ox = (int16_t)(wram_r16(w, (uint32_t)rec + ACTOR_X) - wram_r16(w, W_CAMERA_X));
    d->oy = (int16_t)(wram_r16(w, (uint32_t)rec + ACTOR_Y) -
                      wram_r16(w, (uint32_t)rec + ACTOR_Z) - wram_r16(w, W_CAMERA_Y));
  }
  d->flip = (SpriteFlip)(flags & ACTOR_FLIP);

  d->ptr = wram_r16(w, (uint32_t)rec + ACTOR_META);
  if (d->ptr < 0x8000) { PORT_COVER(draw_no_meta); return 0; }
  d->bank = wram_r16(w, (uint32_t)rec + ACTOR_META_BANK);
  if (d->bank < SPRITE_META_BANK_LO || d->bank > SPRITE_META_BANK_HI) {
    PORT_COVER(draw_bad_bank);
    return 1;
  }
  return 2;
}

bool sprite_build_oam(Wram* w, const Rom* rom) {
  actor_depth_sort(w);
  actor_cull(w);
  oam_buffer_clear(w);
  wram_w16(w, W_SPRITE_TICK, wram_r16(w, W_SCHED_TICK));

  uint16_t count = wram_r16(w, W_VISIBLE_ACTOR_COUNT);
  if (count != 0) {
    // The OAM buffer is worked on as a block and copied back once. That is not
    // a shortcut around the WRAM-layout rule — the bytes end up in the same
    // 544 at `$7E:13BE` — it is what lets the pass hand `sprite_emit` the
    // `SpriteOam` it already takes, so the composition Phase 2 proved against
    // 4,591 real emissions is the composition that runs here.
    SpriteOam oam;
    memcpy(oam.bytes, &w->bytes[W_OAM_BUFFER], SPRITE_OAM_BYTES);
    oam.index = 0;

    bool emitted_any = false;
    for (uint16_t cursor = 0;;) {
      wram_w16(w, W_OAM_PASS_CURSOR, cursor);
      uint16_t rec = wram_r16(w, W_VISIBLE_ACTORS + cursor);

      DrawArgs d;
      if (flags_of(w, rec) & ACTOR_DRAW) {
        int stage = draw_args(w, rec, &d);
        wram_w16(w, W_SPRITE_ATTR_OR, d.attr_or);
        wram_w16(w, W_SPRITE_ATTR_AND, d.attr_and);
        wram_w16(w, W_SPRITE_ORIGIN_X, (uint16_t)d.ox);
        wram_w16(w, W_SPRITE_ORIGIN_Y, (uint16_t)d.oy);
        if (stage >= 1) wram_w16(w, W_SPRITE_META_PTR, d.ptr);

        if (stage == 2) {
          wram_w16(w, W_SPRITE_META_BANK, d.bank);
          uint32_t addr = ((uint32_t)d.bank << 16) | d.ptr;

          SpriteMeta meta;
          if (sprite_meta_read(rom, addr, &meta) != SPRITE_OK) {
            // Only reachable if a metasprite's pieces run off the end of their
            // bank, which no shipped one does — `$80:BDA3` would read the next
            // bank's bytes and this declines rather than guess which.
            return false;
          }
          wram_w16(w, W_SPRITE_PIECES_LEFT, (uint16_t)meta.count);

          if (meta.count == 0) PORT_COVER(draw_empty_meta);
          if (meta.count != 0) {
            // `$80:BDAC  INC $8A` — the pointer the emitter walks starts at the
            // first piece, one past the count byte.
            uint16_t first = (uint16_t)(d.ptr + 1);
            wram_w16(w, W_SPRITE_META_PTR, first);

            SpriteEmitTrace t;
            sprite_emit(&oam, &meta, d.flip, d.ox, d.oy, d.attr_or, d.attr_and,
                        frame_tile, w, &t);
            wram_w16(w, W_SPRITE_PIECES_LEFT, (uint16_t)(meta.count - t.walked));
            wram_w16(w, W_SPRITE_META_PTR,
                     (uint16_t)(first + (uint32_t)t.walked * SPRITE_PIECE_BYTES));
            if (t.attr_valid) {
              wram_w16(w, W_SPRITE_ATTR_MASKED, t.attr);
              emitted_any = true;
            }
            // `$80:BDB7  CPX #$0200` — OAM is full, so the pass stops here and
            // does not write the terminator. Every other way of reaching that
            // test has a record offset in X, which can never be $0200.
            if (oam.index == SPRITE_OAM_LOW_BYTES) {
              PORT_COVER(draw_oam_full);
              break;
            }
          }
        }
      }

      cursor += 2;
      if (cursor >= count) {
        // `$80:BDC4`. The ROM compares with `BNE`, which would loop forever if
        // the cursor ever stepped past the count; it cannot, because both are
        // even, and `>=` costs nothing to be sure.
        sprite_oam_terminate(&oam);
        break;
      }
    }

    memcpy(&w->bytes[W_OAM_BUFFER], oam.bytes, SPRITE_OAM_BYTES);
    wram_w16(w, W_OAM_INDEX, oam.index);
    // `sprite_frame_tile` opens with `STX $38`, and the X it spills is the OAM
    // index of the piece being emitted. So the last lookup of the pass leaves
    // the index of the last piece drawn — four bytes back from where the buffer
    // now ends. See the exclude on this address in `src/cosim/routines.c` for
    // why the port writes it here rather than inside `sprite_frame_tile`.
    if (emitted_any)
      wram_w16(w, W_SPRITE_SCRATCH_X, (uint16_t)(oam.index - 4));
  }

  if (!actor_overlap_pass(w)) return false;

  // `$80:BDD2`. Four bytes indexed by the low two bits of the tick, and all
  // four are $80 in the shipped ROM — so this is a constant with a table's
  // shape. Read rather than assumed: a ROM hack that varies it by frame phase
  // would still work, and it costs one lookup a frame.
  uint16_t tick = wram_r16(w, W_SCHED_TICK);
  wram_w16(w, W_SPRITE_PASS_PHASE,
           (uint16_t)(rom_word(rom, SPRITE_PASS_PHASE_TABLE + (tick & 3)) & 0xff));
  return true;
}
