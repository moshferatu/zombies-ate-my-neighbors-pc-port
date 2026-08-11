#include "port/oam.h"

#include <stdbool.h>
#include <string.h>

#include "assets/sprite.h"
#include "port/collide.h"
#include "port/coverage.h"
#include "port/sprite_cache.h"
#include "port/thread.h"

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

// Does the successor `b` belong in front of `a`, and which key said so?
//
// `ACTOR_SORT_FIRST` decides it whenever the two records disagree about that
// bit — the ROM tests `(fa ^ fb) & $20` and then looks only at `fb`. Otherwise
// the larger Y wins, compared unsigned exactly as `CMP` does it.
//
// Which key answered is not a detail the sort cares about — it returns the same
// list either way — but the ROM reaches the two answers down straight lines of
// different lengths, so `ActorSortWork` counts them apart.
static ActorSortCmp compare_pair(const Wram* w, uint16_t a, uint16_t b) {
  uint16_t fa = flags_of(w, a), fb = flags_of(w, b);
  if ((fa ^ fb) & ACTOR_SORT_FIRST) {
    PORT_COVER(sort_key_first);
    return (fb & ACTOR_SORT_FIRST) ? ACTOR_SORT_CMP_FIRST_SWAP
                                   : ACTOR_SORT_CMP_FIRST_NOSWAP;
  }
  PORT_COVER(sort_key_y);
  return wram_r16(w, (uint32_t)a + ACTOR_Y) < wram_r16(w, (uint32_t)b + ACTOR_Y)
             ? ACTOR_SORT_CMP_Y_SWAP
             : ACTOR_SORT_CMP_Y_NOSWAP;
}

static bool cmp_swaps(ActorSortCmp cmp) {
  return cmp == ACTOR_SORT_CMP_FIRST_SWAP || cmp == ACTOR_SORT_CMP_Y_SWAP;
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
  ActorSortWork work;
  return actor_depth_sort_counted(w, &work);
}

uint16_t actor_depth_sort_counted(Wram* w, ActorSortWork* work) {
  memset(work, 0, sizeof *work);

  uint16_t cur = wram_r16(w, W_ACTOR_LIST_HEAD);
  if (cur == 0) {
    work->empty = true;
    return 0;
  }
  uint16_t next = next_of(w, cur);
  if (next == 0) {
    work->single = true;
    return cur;
  }

  // The head has no predecessor to relink, so the list head itself moves.
  ActorSortCmp cmp = compare_pair(w, cur, next);
  work->compares[cmp]++;
  if (cmp_swaps(cmp)) {
    work->swap_head = true;
    swap_with_next(w, 0, cur, next);
  }

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
    work->steps++;
    cmp = compare_pair(w, cur, next);
    work->compares[cmp]++;
    if (!cmp_swaps(cmp)) {
      prev = cur;
      cur = next;
      continue;
    }
    work->swap_mid++;
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
//
// Which of the two comparisons answered it is not something the cull cares
// about — the record is in or it is out — but the ROM reaches the answers down
// runs of different lengths, so this reports the comparison as well as its
// result. See `ActorCullBlock`.
typedef enum {
  CULL_WINDOW_HIGH,  // `CMP #$FF80` said so: behind the origin
  CULL_WINDOW_LOW,   // ...it did not, and `CMP #$0180` put it inside
  CULL_WINDOW_OUT,
} CullWindow;

static CullWindow window_of(uint16_t delta) {
  if (delta >= 0xff80) return CULL_WINDOW_HIGH;
  return delta < 0x0180 ? CULL_WINDOW_LOW : CULL_WINDOW_OUT;
}

void actor_cull_counted(Wram* w, ActorCullWork* work) {
  memset(work, 0, sizeof(*work));

  uint16_t camera_x = wram_r16(w, W_CAMERA_X);
  uint16_t camera_y = wram_r16(w, W_CAMERA_Y);
  uint16_t count = 0;

  uint16_t rec = wram_r16(w, W_ACTOR_LIST_HEAD);
  if (rec == 0) {
    // `$80:BCE8 BEQ $BD1C` — the count still gets written, and it is zero
    // because `LDY #$0000` ran before the list was ever looked at.
    work->blocks[CULL_BLK_EMPTY]++;
    wram_w16(w, W_VISIBLE_ACTOR_COUNT, 0);
    return;
  }
  work->blocks[CULL_BLK_PROLOGUE]++;

  for (;;) {
    uint16_t flags = flags_of(w, rec);
    if (!(flags & ACTOR_DRAW)) {
      work->blocks[CULL_BLK_UNDRAWN]++;
      PORT_COVER(cull_undrawn);
      goto advance;
    }
    work->blocks[CULL_BLK_DRAWN]++;

    if (flags & ACTOR_SCREEN_SPACE) {
      work->blocks[CULL_BLK_SCREEN]++;
      PORT_COVER(cull_screen);
    } else {
      work->blocks[CULL_BLK_WORLD]++;

      CullWindow wx =
          window_of((uint16_t)(wram_r16(w, (uint32_t)rec + ACTOR_X) - camera_x));
      work->blocks[wx == CULL_WINDOW_HIGH ? CULL_BLK_X_HIGH : CULL_BLK_X_TEST]++;
      if (wx != CULL_WINDOW_HIGH)
        work->blocks[wx == CULL_WINDOW_LOW ? CULL_BLK_X_IN : CULL_BLK_X_OUT]++;
      if (wx == CULL_WINDOW_OUT) {
        PORT_COVER(cull_offscreen);
        goto advance;
      }

      CullWindow wy =
          window_of((uint16_t)(wram_r16(w, (uint32_t)rec + ACTOR_Y) - camera_y));
      work->blocks[wy == CULL_WINDOW_HIGH ? CULL_BLK_Y_HIGH : CULL_BLK_Y_TEST]++;
      if (wy != CULL_WINDOW_HIGH)
        work->blocks[wy == CULL_WINDOW_LOW ? CULL_BLK_Y_IN : CULL_BLK_Y_OUT]++;
      if (wy == CULL_WINDOW_OUT) {
        PORT_COVER(cull_offscreen);
        goto advance;
      }
    }

    work->blocks[CULL_BLK_EMIT]++;
    wram_w16(w, W_VISIBLE_ACTORS + count, rec);
    count += 2;

  advance:
    // `$80:BD17 LDA $12,X : TAX : BNE $BCEA`. Every one of the three ways
    // through a record arrives here, which is why it is a label and not a loop
    // condition: the ROM's `continue` and its fall-through are the same address.
    rec = next_of(w, rec);
    work->blocks[rec != 0 ? CULL_BLK_ADVANCE : CULL_BLK_EXIT]++;
    if (rec == 0) break;
  }

  wram_w16(w, W_VISIBLE_ACTOR_COUNT, count);
}

void actor_cull(Wram* w) {
  ActorCullWork work;
  actor_cull_counted(w, &work);
}

// ---------------------------------------------------------------------------
// $80:BE8F  actor_collide_notify
// ---------------------------------------------------------------------------

bool actor_collide_notify(Wram* w, const Rom* rom, uint16_t a, uint16_t b,
                          ThreadCallResult* tail) {
  // `$80:BE8F`..`$80:BEA6`. Both records are read out in full before either
  // dispatch runs, which is what makes the second one see the pair as it was
  // rather than as the first handler left it.
  uint16_t id_b = wram_r16(w, (uint32_t)b + ACTOR_COLLIDE_ID);
  uint16_t thread_b = wram_r16(w, (uint32_t)b + ACTOR_THREAD);
  uint16_t id_a = wram_r16(w, (uint32_t)a + ACTOR_COLLIDE_ID);
  uint16_t thread_a = wram_r16(w, (uint32_t)a + ACTOR_THREAD);

  wram_w16(w, W_NOTIFY_REC_B, b);
  wram_w16(w, W_NOTIFY_ID_B, id_b);
  wram_w16(w, W_NOTIFY_THREAD_B, thread_b);
  wram_w16(w, W_NOTIFY_REC_A, a);
  wram_w16(w, W_NOTIFY_ID_A, id_a);
  wram_w16(w, W_NOTIFY_THREAD_A, thread_a);

  // The two `JSL $80:8480` at `$80:BEB4` and `$80:BEC4`, each preceded by the
  // pair being republished the other way round. The order matters now that the
  // handlers are ported and can read it: `player_collide` does `LDA $0076`, so
  // whichever way `$76` is pointing when its dispatch runs is what it files.
  wram_w16(w, W_HANDLER_SELF, b);
  wram_w16(w, W_HANDLER_OTHER, a);
  if (!thread_call_handler(w, rom, thread_b, id_a, tail->c, tail)) {
    PORT_COVER(collide_unported);
    return false;
  }
  bool entered = tail->entered;

  wram_w16(w, W_HANDLER_SELF, a);
  wram_w16(w, W_HANDLER_OTHER, b);
  if (!thread_call_handler(w, rom, thread_a, id_b, tail->c, tail)) {
    PORT_COVER(collide_unported);
    return false;
  }

  // Which of the two shapes this call had, for the coverage report. `verify`
  // measured the answer before either handler existed and it was flat: every
  // collision in ordinary play enters a handler, and `collide_none` has never
  // been reached by any input.
  PORT_COVER_IF(entered || tail->entered, collide_handler, collide_none);
  return true;
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

bool actor_overlap_pass_counted(Wram* w, const Rom* rom,
                                ActorOverlapWork* work) {
  memset(work, 0, sizeof(*work));

  uint16_t count = wram_r16(w, W_VISIBLE_ACTOR_COUNT);
  if (count == 0) {  // `LDY $9C : BEQ` — not even $3C is written
    work->blocks[OVL_BLK_EMPTY]++;
    return true;
  }
  uint16_t outer = (uint16_t)(count - 2);
  if (outer == 0) {  // one record; nothing to pair it with
    work->blocks[OVL_BLK_SINGLE]++;
    return true;
  }
  work->blocks[OVL_BLK_PROLOGUE]++;

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
    work->blocks[a_id == 0 ? OVL_BLK_OUTER_NOID : OVL_BLK_OUTER_ID]++;
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
        // The tests below read as one condition but the ROM reaches them down
        // four branches, each with its own length, so they are counted apart.
        // Splitting the two axes is also what makes each half separately
        // countable: the X test firing is what says the 16-px threshold is
        // exercised at all, and the pair test firing is what says the dispatch
        // below is.
        work->blocks[b_id == 0 ? OVL_BLK_INNER_NOID : OVL_BLK_INNER_ID]++;
        if (b_id != 0)
          work->blocks[b_id == id ? OVL_BLK_SAME_ID : OVL_BLK_DIFF_ID]++;
        if (b_id != 0 && b_id != id) {
          bool near_x = within_8px(ox, wram_r16(w, (uint32_t)b + ACTOR_X));
          work->blocks[near_x ? OVL_BLK_X_NEAR : OVL_BLK_X_FAR]++;
          if (near_x) {
            PORT_COVER(overlap_near_x);
            bool near_y = within_8px(oy, wram_r16(w, (uint32_t)b + ACTOR_Y));
            work->blocks[near_y ? OVL_BLK_Y_NEAR : OVL_BLK_Y_FAR]++;
            if (near_y) {
              // `$80:BF0D  PHY : JSR $BE8F : PLY`. Carry is clear here by
              // construction — the `BCS` at `$80:BF0B` is what falls through to
              // this call — and the no-handler path inside the dispatch is the
              // only thing that would pass it on.
              PORT_COVER(overlap_hit);
              work->hits++;
              ThreadCallResult tail = {.c = false};
              if (!actor_collide_notify(w, rom, a, b, &tail)) return false;
            }
          }
        }
        work->blocks[inner == 0 ? OVL_BLK_INNER_DONE : OVL_BLK_INNER_NEXT]++;
        if (inner == 0) break;
      }
    }

    work->blocks[outer == 0 ? OVL_BLK_OUTER_DONE : OVL_BLK_OUTER_NEXT]++;
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

bool actor_overlap_pass(Wram* w, const Rom* rom) {
  ActorOverlapWork work;
  return actor_overlap_pass_counted(w, rom, &work);
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

bool sprite_build_oam(Wram* w, const Rom* rom, uint16_t dp) {
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

  if (!actor_overlap_pass(w, rom)) return false;

  // `$80:BDD2`. Four bytes indexed by the low two bits of a word, and all four
  // are $80 in the shipped ROM — so this is a constant with a table's shape.
  // Read rather than assumed: a ROM hack that varies it by frame phase would
  // still work, and it costs one lookup a frame.
  //
  // The word is `$20` on the **caller's** page, not `W_SCHED_TICK`: `$80:BDD0
  // PLD` runs first. They are the same thing for the scheduler, whose page is
  // zero, and different for `$82:DE03`/`$82:DE4B` — see the header.
  uint16_t phase = wram_r16(w, (uint32_t)((dp + SPRITE_PASS_PHASE_DP) & 0xffff));
  wram_w16(w, W_SPRITE_PASS_PHASE,
           (uint16_t)(rom_word(rom, SPRITE_PASS_PHASE_TABLE + (phase & 3)) & 0xff));
  return true;
}

// ---------------------------------------------------------------------------
// $80:B123  actor_nearest
// ---------------------------------------------------------------------------

// `SEC : SBC : BCS : EOR #$FFFF : INC A` — the ROM's absolute value, and it
// keeps the signed difference as well because the store happens before the
// negation.
static uint16_t nearest_abs(uint16_t a, uint16_t b, uint16_t* raw) {
  uint16_t d = (uint16_t)(a - b);
  *raw = d;
  // The branch is on carry, which `SBC` leaves set when there was no borrow.
  return (a >= b) ? d : (uint16_t)(~d + 1);
}

uint16_t actor_nearest(Wram* w, uint16_t x, uint16_t y, uint16_t* dist) {
  wram_w16(w, NEAREST_DP_X, x);
  wram_w16(w, NEAREST_DP_Y, y);
  wram_w16(w, NEAREST_DP_BEST, 0xffffu);

  // $80:B131. From the top slot down, every slot, ending on the base itself.
  for (int i = ACTOR_SLOT_COUNT - 1; i >= 0; i--) {
    uint32_t rec = W_ACTOR_SLOTS + (uint32_t)i * ACTOR_SLOT_STRIDE;
    uint16_t flags = wram_r16(w, rec + ACTOR_FLAGS);
    if (!(flags & ACTOR_DRAW)) {
      PORT_COVER(nearest_undrawn);
      continue;
    }
    if (!(flags & ACTOR_ACTIVE)) {
      PORT_COVER(nearest_inactive);
      continue;
    }
    uint16_t id = wram_r16(w, rec + ACTOR_COLLIDE_ID);
    if (id != NEAREST_ID_PLAYER_A && id != NEAREST_ID_PLAYER_B &&
        id != NEAREST_ID_C && id != NEAREST_ID_D) {
      PORT_COVER(nearest_wrong_id);
      continue;
    }
    PORT_COVER(nearest_candidate);

    uint16_t raw;
    uint16_t d = nearest_abs(wram_r16(w, rec + ACTOR_X), x, &raw);
    wram_w16(w, NEAREST_DP_DX, raw);
    wram_w16(w, NEAREST_DP_DIST, d);

    uint16_t dy = nearest_abs(wram_r16(w, rec + ACTOR_Y), y, &raw);
    wram_w16(w, NEAREST_DP_DY, raw);
    d = (uint16_t)(d + dy);
    wram_w16(w, NEAREST_DP_DIST, d);

    // `CMP $38 : BCS` — strictly nearer wins, so on a tie the higher slot
    // keeps it, and the walk runs downwards.
    if (d < wram_r16(w, NEAREST_DP_BEST)) {
      PORT_COVER(nearest_closer);
      wram_w16(w, NEAREST_DP_BEST, d);
      wram_w16(w, NEAREST_DP_FOUND, (uint16_t)rec);
    }
  }

  // $80:B189. `$44` is not seeded, so when nothing matched this hands back
  // whatever the last search that did find something left there.
  *dist = wram_r16(w, NEAREST_DP_BEST);
  return wram_r16(w, NEAREST_DP_FOUND);
}

// ---------------------------------------------------------------------------
// $80:B379  actor_aligned
// ---------------------------------------------------------------------------

// `SEC : SBC : CLC : ADC #$0008 : CMP #$0010 : BCS` -- the same
// add-half-and-compare-unsigned window `actor_at_point` uses, so "aligned"
// means the difference is in `-8..+7`: one tile.
static bool aligned_within(uint16_t rec, uint16_t point) {
  uint16_t diff = (uint16_t)(rec - point);
  return (uint16_t)(diff + ALIGNED_HALF) < ALIGNED_WINDOW;
}

// `SEC : SBC` again, on the other axis, from scratch -- the ROM does not reuse
// the difference the window test just computed. `BMI` reads the sign, and the
// carry it leaves is what the direction exits hand back.
static bool aligned_negative(uint16_t rec, uint16_t point, bool* out_carry) {
  *out_carry = rec >= point;
  return (int16_t)(uint16_t)(rec - point) < 0;
}

void actor_aligned(Wram* w, uint16_t x, uint16_t y, ActorAlignedRegs* out) {
  wram_w16(w, ALIGNED_DP_X, x);
  wram_w16(w, ALIGNED_DP_Y, y);

  // $80:B382 seeds X with $1ACA, the *last* record, and the `CPX #$185E : BCS`
  // at the bottom is what stops it -- so all 32 are visited, top down, exactly
  // as `actor_nearest` visits them.
  for (int i = ACTOR_SLOT_COUNT - 1; i >= 0; i--) {
    uint32_t rec = W_ACTOR_SLOTS + (uint32_t)i * ACTOR_SLOT_STRIDE;
    uint16_t flags = wram_r16(w, rec + ACTOR_FLAGS);
    if (!(flags & ACTOR_DRAW)) {
      PORT_COVER(aligned_undrawn);
      continue;
    }
    if (!(flags & ACTOR_ACTIVE)) {
      PORT_COVER(aligned_inactive);
      continue;
    }
    uint16_t id = wram_r16(w, rec + ACTOR_COLLIDE_ID);
    if (id != ALIGNED_ID_PLAYER_A && id != ALIGNED_ID_PLAYER_B &&
        id != ALIGNED_ID_D) {
      PORT_COVER(aligned_wrong_id);
      continue;
    }

    bool carry;
    // X is tested first and returns, so a record inside the window on both
    // axes is reported as up or down and never as left or right.
    if (aligned_within(wram_r16(w, rec + ACTOR_X), x)) {
      bool up = aligned_negative(wram_r16(w, rec + ACTOR_Y), y, &carry);
      PORT_COVER_IF(up, aligned_up, aligned_down);
      out->a = up ? ALIGNED_UP : ALIGNED_DOWN;
      out->x = (uint16_t)rec;
      out->c = carry;
      return;
    }
    if (aligned_within(wram_r16(w, rec + ACTOR_Y), y)) {
      bool left = aligned_negative(wram_r16(w, rec + ACTOR_X), x, &carry);
      PORT_COVER_IF(left, aligned_left, aligned_right);
      out->a = left ? ALIGNED_LEFT : ALIGNED_RIGHT;
      out->x = (uint16_t)rec;
      out->c = carry;
      return;
    }
    PORT_COVER(aligned_off);
  }

  // $80:B3EC. X is the loop counter one stride past the bottom of the table,
  // which is a number rather than a record, and the carry is the `CPX` that
  // just failed.
  PORT_COVER(aligned_none);
  out->a = ALIGNED_NONE;
  out->x = (uint16_t)(W_ACTOR_SLOTS - ACTOR_SLOT_STRIDE);
  out->c = false;
}

// ---------------------------------------------------------------------------
// $80:B093  actor_gap
// ---------------------------------------------------------------------------

// `SEC : SBC : BPL : EOR #$FFFF : INC A`. The test is on the *sign* of the
// difference where `nearest_abs` above tests the borrow, so `$8000` comes back
// as itself here and as `$8000` there too -- the two agree everywhere, and they
// are still two different instructions and stay that way.
static uint16_t gap_abs(uint16_t a, uint16_t b) {
  uint16_t d = (uint16_t)(a - b);
  return (d & 0x8000u) ? (uint16_t)(~d + 1u) : d;
}

void actor_gap(Wram* w, uint16_t rec, ActorGapRegs* out) {
  // $80:B093 `TYA : BEQ`. No record, no distance, and nothing that writes
  // carry between here and the `RTS`.
  if (rec == 0) {
    PORT_COVER(gap_empty);
    out->a = 0xffffu;
    out->n = true;
    out->z = false;
    out->has_c = false;
    return;
  }
  out->has_c = true;

  const uint16_t x = wram_r16(w, GAP_DP_X);
  const uint16_t y = wram_r16(w, GAP_DP_Y);
  const uint16_t dx = gap_abs(wram_r16(w, (uint32_t)rec + ACTOR_X), x);
  wram_w16(w, GAP_DP_DX, dx);
  const uint16_t dy = gap_abs(wram_r16(w, (uint32_t)rec + ACTOR_Y), y);

  // `CMP $3C : BCS` -- the Y gap keeps the answer on a tie, and the flags the
  // caller sees are that comparison's rather than the distance's.
  if (dy >= dx) {
    PORT_COVER(gap_y_wider);
    const uint16_t diff = (uint16_t)(dy - dx);
    out->a = dy;
    out->n = (diff & 0x8000u) != 0;
    out->z = diff == 0;
    out->c = true;
    return;
  }
  // `LDA $3C`, which sets N and Z from the X gap and leaves the failed `CMP`'s
  // carry alone.
  PORT_COVER(gap_x_wider);
  out->a = dx;
  out->n = (dx & 0x8000u) != 0;
  out->z = dx == 0;
  out->c = false;
}

// ---------------------------------------------------------------------------
// $80:B18F  actor_nearest_id3
// ---------------------------------------------------------------------------

uint16_t actor_nearest_id3(Wram* w, uint16_t x, uint16_t y, uint16_t* dist) {
  wram_w16(w, NEAREST_DP_X, x);
  wram_w16(w, NEAREST_DP_Y, y);
  wram_w16(w, NEAREST_DP_BEST, 0xffffu);

  // $80:B19D, and the same walk `actor_nearest` runs: the top slot down to the
  // base, all 32, whatever the board holds.
  for (int i = ACTOR_SLOT_COUNT - 1; i >= 0; i--) {
    uint32_t rec = W_ACTOR_SLOTS + (uint32_t)i * ACTOR_SLOT_STRIDE;
    uint16_t flags = wram_r16(w, rec + ACTOR_FLAGS);
    if (!(flags & ACTOR_DRAW)) {
      PORT_COVER(nearest3_undrawn);
      continue;
    }
    if (!(flags & ACTOR_ACTIVE)) {
      PORT_COVER(nearest3_inactive);
      continue;
    }
    // The one line that is not `actor_nearest`: one id, tested with a `BNE`
    // rather than four tested with `BEQ`s.
    if (wram_r16(w, rec + ACTOR_COLLIDE_ID) != NEAREST3_ID) {
      PORT_COVER(nearest3_wrong_id);
      continue;
    }
    PORT_COVER(nearest3_candidate);

    uint16_t raw;
    uint16_t d = nearest_abs(wram_r16(w, rec + ACTOR_X), x, &raw);
    wram_w16(w, NEAREST_DP_DX, raw);
    wram_w16(w, NEAREST_DP_DIST, d);

    uint16_t dy = nearest_abs(wram_r16(w, rec + ACTOR_Y), y, &raw);
    wram_w16(w, NEAREST_DP_DY, raw);
    d = (uint16_t)(d + dy);
    wram_w16(w, NEAREST_DP_DIST, d);

    if (d < wram_r16(w, NEAREST_DP_BEST)) {
      PORT_COVER(nearest3_closer);
      wram_w16(w, NEAREST_DP_BEST, d);
      wram_w16(w, NEAREST_DP_FOUND, (uint16_t)rec);
    }
  }

  *dist = wram_r16(w, NEAREST_DP_BEST);
  return wram_r16(w, NEAREST_DP_FOUND);
}

// ---------------------------------------------------------------------------
// $80:B1EC, $80:B22A  actor_bearing_point, actor_bearing
// ---------------------------------------------------------------------------

// One axis of the table index. `CMP : BEQ` first, so "the same" is its own
// answer, and then `ADC #$0001` with the comparison's own carry underneath it,
// which makes 1 for *less than* and 2 for *greater*.
static int bearing_axis(uint16_t rec, uint16_t point) {
  if (rec == point) return 0;
  return rec > point ? 2 : 1;
}

// The half the two routines share: the point is already in `GAP_DP_X` and
// `GAP_DP_Y`, and everything from here down is common except which table is
// read and whether the vertical half of the index survives to reach it.
static void bearing_lookup(Wram* w, const Rom* rom, uint16_t rec, bool keep_v,
                           ActorBearingRegs* out) {
  const uint16_t x = wram_r16(w, GAP_DP_X);
  const uint16_t y = wram_r16(w, GAP_DP_Y);
  const uint16_t rx = wram_r16(w, (uint32_t)rec + ACTOR_X);
  const uint16_t ry = wram_r16(w, (uint32_t)rec + ACTOR_Y);

  const int v = bearing_axis(ry, y);
  const int h = bearing_axis(rx, x);
  if (v == 0) PORT_COVER(bearing_level);
  else PORT_COVER_IF(v == 1, bearing_above, bearing_below);
  if (h == 0) PORT_COVER(bearing_column);
  else PORT_COVER_IF(h == 1, bearing_left, bearing_right);

  const int index = keep_v ? 4 * v + h : h;
  out->x = (uint16_t)index;
  out->a = (uint16_t)(rom_word(rom, (keep_v ? BEARING_TABLE
                                            : BEARING_POINT_TABLE) +
                                        (uint32_t)index) &
                      0xffu);
  // **Not the horizontal `CMP`'s carry.** `ADC #$0001` sits between it and the
  // `RTL`, and an `ADC` writes carry whether anything asked it to or not — so
  // on every path where the two differ the comparison's answer is overwritten
  // by an addition of at most ten, which never carries. Only the `BEQ` path
  // skips the `ADC` and keeps the `CMP`'s carry, and that is the equal case, so
  // carry out means *the record shares the point's X* and nothing else.
  //
  // Modelled as `rx >= x` first, which is what the comparison says and what the
  // instruction after it throws away: 2,078 calls, 2,078 diverging, `flag C:
  // ROM 0, port 1`.
  out->c = rx == x;
}

void actor_bearing(Wram* w, const Rom* rom, uint16_t from_rec, uint16_t to_rec,
                   ActorBearingRegs* out) {
  // $80:B22F. The `from` record's position *is* the point, copied into the
  // same two words the whole family reads.
  wram_w16(w, GAP_DP_X, wram_r16(w, (uint32_t)from_rec + ACTOR_X));
  wram_w16(w, GAP_DP_Y, wram_r16(w, (uint32_t)from_rec + ACTOR_Y));
  bearing_lookup(w, rom, to_rec, true, out);
}

void actor_bearing_point(Wram* w, const Rom* rom, uint16_t rec, uint16_t x,
                         uint16_t y, ActorBearingRegs* out) {
  wram_w16(w, GAP_DP_X, x);
  wram_w16(w, GAP_DP_Y, y);
  // `keep_v` false is the bug, and it is the whole difference: $80:B208 is
  // `TXA` where $80:B249 is `TAX`, so the vertical half is computed, shifted
  // twice, and dropped on the floor. See the header.
  PORT_COVER_IF(wram_r16(w, (uint32_t)rec + ACTOR_Y) != y, bearing_v_dropped,
                bearing_v_zero);
  bearing_lookup(w, rom, rec, false, out);
}

// ---------------------------------------------------------------------------
// $80:B26B, $80:B2A5  player_in_range, player_bearing
// ---------------------------------------------------------------------------

// Which player, if either, is inside `limit` -- the twenty instructions
// `$80:B26B` and `$80:B2A5` have in common, down to the order the two `$D2`
// and `$D4` gaps are measured in and the two dead scratch words they leave
// behind.
//
// Returns the chosen record, or zero when neither is close enough, and reports
// the carry the exit it took was reached with.
static uint16_t player_pick(Wram* w, uint16_t limit, uint16_t* dist,
                            bool* carry) {
  ActorGapRegs g;

  const uint16_t rec_a = wram_r16(w, W_PLAYER_A_RECORD);
  actor_gap(w, rec_a, &g);
  const uint16_t da = g.a;
  wram_w16(w, PICK_DP_DIST_A, da);

  const uint16_t rec_b = wram_r16(w, W_PLAYER_B_RECORD);
  actor_gap(w, rec_b, &g);
  const uint16_t db = g.a;
  wram_w16(w, PICK_DP_DIST_B, db);

  // `CMP $42 : BCS` -- player B first, and out of range for B sends the whole
  // question to A rather than comparing the two.
  if (db < limit) {
    if (db < da) {
      PORT_COVER(pick_b_nearer);
      *dist = db;
      *carry = false;
      return rec_b;
    }
    // The `BRA`, reached with the failed `CMP $3E`'s carry still set. A is no
    // further than B and B is inside the limit, so A is inside it too and the
    // routine does not re-check.
    PORT_COVER(pick_a_nearer);
    *dist = da;
    *carry = true;
    return rec_a;
  }
  if (da < limit) {
    PORT_COVER(pick_a_only);
    *dist = da;
    *carry = false;
    return rec_a;
  }
  PORT_COVER(pick_neither);
  *dist = 0;
  *carry = true;
  return 0;
}

void player_in_range(Wram* w, uint16_t limit, uint16_t x, uint16_t y,
                     PlayerPickRegs* out) {
  wram_w16(w, PICK_DP_LIMIT, limit);
  wram_w16(w, GAP_DP_X, x);
  wram_w16(w, GAP_DP_Y, y);

  uint16_t dist = 0;
  bool carry = false;
  const uint16_t rec = player_pick(w, limit, &dist, &carry);

  out->a = rec;
  // `LDX $3E`/`LDX $40` on the two found exits; the zero exit never loads X at
  // all, so the caller's own argument is still in it.
  out->x = rec ? dist : x;
  // `LDY $D4` on the way in and nothing after it, whichever player won.
  out->y = wram_r16(w, W_PLAYER_B_RECORD);
  out->c = carry;
}

void player_bearing(Wram* w, const Rom* rom, uint16_t limit, uint16_t x,
                    uint16_t y, PlayerPickRegs* out) {
  wram_w16(w, PICK_DP_LIMIT, limit);
  wram_w16(w, GAP_DP_X, x);
  wram_w16(w, GAP_DP_Y, y);

  uint16_t dist = 0;
  bool carry = false;
  const uint16_t rec = player_pick(w, limit, &dist, &carry);
  if (rec == 0) {
    // $80:B2CE. A is zero, which is how every caller reads "nobody in range",
    // and X and Y are the leftovers the selection stopped on.
    out->a = BEARING_NONE;
    out->x = x;
    out->y = wram_r16(w, W_PLAYER_B_RECORD);
    out->c = carry;
    return;
  }

  const uint16_t rx = wram_r16(w, (uint32_t)rec + ACTOR_X);
  const uint16_t ry = wram_r16(w, (uint32_t)rec + ACTOR_Y);
  const int index = 4 * bearing_axis(ry, y) + bearing_axis(rx, x);
  PORT_COVER_IF(index == 0, player_bearing_same, player_bearing_off);

  // A word table, so the index is doubled -- and that `ASL` is also the last
  // thing on this path to write carry. Ten shifted left is not enough to shift
  // anything out, so carry is clear on every direction exit.
  out->a = rom_word(rom, PLAYER_BEARING_TABLE + (uint32_t)index * 2);
  out->x = dist;  // `PEI` before the lookup, `PLX` after it
  out->y = rec;   // ...and `PHY`/`PLY` around it
  out->c = false;
}

// ---------------------------------------------------------------------------
// $80:B3F1  actor_snap_to
// ---------------------------------------------------------------------------

// One axis. `SEC : SBC : BPL : EOR #$FFFF : INC A` is the ROM's absolute value,
// a two's-complement negate, so `$8000` comes back as itself exactly as it does
// there.
//
// `CMP #$0002` **does not write A** -- it sets the flags from a subtraction it
// throws away -- so on the no-snap path A is still the absolute difference
// while N and Z describe that difference minus two. The two have to be carried
// separately, which is why `flags_src` is not just `*out_a`.
static bool snap_axis(Wram* w, uint16_t rec, uint16_t onto, uint16_t field,
                      uint16_t* out_a, uint16_t* flags_src, bool* carry) {
  uint16_t mine = wram_r16(w, (uint32_t)rec + field);
  uint16_t theirs = wram_r16(w, (uint32_t)onto + field);
  uint16_t diff = (uint16_t)(mine - theirs);
  if (diff & 0x8000u) diff = (uint16_t)(~diff + 1u);

  if (diff >= SNAP_WINDOW) {
    *out_a = diff;                                   // untouched by the CMP
    *flags_src = (uint16_t)(diff - SNAP_WINDOW);     // ...which set N and Z
    *carry = true;
    return false;
  }
  wram_w16(w, (uint32_t)rec + field, theirs);
  *out_a = theirs;    // `LDA $0002,Y`; the `STA` under it sets nothing
  *flags_src = theirs;
  *carry = false;
  return true;
}

void actor_snap_to(Wram* w, uint16_t rec, uint16_t onto, ActorSnapRegs* out) {
  uint16_t a, flags;
  bool c;
  // X first, then Y, and only Y's registers and flags survive.
  bool x_snapped = snap_axis(w, rec, onto, ACTOR_X, &a, &flags, &c);
  PORT_COVER_IF(x_snapped, snap_x_took, snap_x_left);
  bool y_snapped = snap_axis(w, rec, onto, ACTOR_Y, &a, &flags, &c);
  PORT_COVER_IF(y_snapped, snap_y_took, snap_y_left);

  out->a = a;
  out->n = (flags & 0x8000u) != 0;
  out->z = flags == 0;
  out->c = c;
}

// ---------------------------------------------------------------------------
// $80:BF1B  actor_notify_box
// ---------------------------------------------------------------------------

bool actor_notify_box(Wram* w, const Rom* rom, uint16_t a_in, bool c_in,
                      ThreadCallResult* tail, ActorNotifyRegs* out) {
  // `LDX #$0006 : BIT $38,X : BPL : STZ $38,X`, downwards over four words.
  // Only a negative bound is touched, and it is zeroed rather than clamped to
  // anything the map knows about. X falls out of this loop at $FFFE, which is
  // what the two early exits below hand back.
  for (int i = NOTIFY_BOX_BOUNDS - 1; i >= 0; i--) {
    uint32_t at = NOTIFY_BOX_DP_X0 + (uint32_t)i * 2;
    if (wram_r16(w, at) & 0x8000u) {
      PORT_COVER(notify_bound_clamped);
      wram_w16(w, at, 0);
    } else {
      PORT_COVER(notify_bound_kept);
    }
  }

  out->a = a_in;
  out->x = 0xfffeu;
  out->y = 0;
  out->c = c_in;

  uint16_t count = wram_r16(w, W_VISIBLE_ACTOR_COUNT);
  if (count == 0) {
    PORT_COVER(notify_no_actors);
    return true;
  }
  // `DEY DEY : BEQ` — one visible record is refused as well as none, so a board
  // holding exactly one actor is never told anything. The walk would have run
  // from index 0 to index 0; the guard rejects it for being zero rather than
  // for being empty, and the port keeps that.
  uint16_t at = (uint16_t)(count - 2);
  if (at == 0) {
    PORT_COVER(notify_one_actor);
    return true;
  }

  uint16_t id = wram_r16(w, NOTIFY_BOX_DP_ID);
  uint16_t x0 = wram_r16(w, NOTIFY_BOX_DP_X0), x1 = wram_r16(w, NOTIFY_BOX_DP_X1);
  uint16_t y0 = wram_r16(w, NOTIFY_BOX_DP_Y0), y1 = wram_r16(w, NOTIFY_BOX_DP_Y1);

  // A, X and the carry are whatever the *last* record examined left behind, so
  // they are carried through the walk rather than reconstructed at the end.
  // Each `CMP` below writes the carry and not A; each `LDA` writes A and not
  // the carry; a record with no id writes neither.
  for (;;) {
    uint16_t rec = wram_r16(w, W_VISIBLE_ACTORS + at);
    out->x = rec;
    uint16_t rec_id = wram_r16(w, (uint32_t)rec + ACTOR_COLLIDE_ID);
    out->a = rec_id;

    if (rec_id == 0) {
      PORT_COVER(notify_no_id);            // `BEQ`, and the carry stands
    } else if (rec_id == id) {
      PORT_COVER(notify_self_id);
      out->c = true;                       // `CMP $40` equal, so C is set
    } else {
      out->c = rec_id >= id;
      uint16_t rx = wram_r16(w, (uint32_t)rec + ACTOR_X);
      out->a = rx;
      out->c = rx >= x0;
      if (!out->c) {
        PORT_COVER(notify_left_of);
      } else {
        out->c = rx >= x1;
        if (out->c) {
          PORT_COVER(notify_right_of);
        } else {
          uint16_t ry = wram_r16(w, (uint32_t)rec + ACTOR_Y);
          out->a = ry;
          out->c = ry >= y0;
          if (!out->c) {
            PORT_COVER(notify_above);
          } else {
            out->c = ry >= y1;
            if (out->c) {
              PORT_COVER(notify_below);
            } else {
              PORT_COVER(notify_hit);
              // `PHY : LDA $0C,X : STX $78 : TAX : LDY $40 : JSL $808480 :
              // PLY`. The record is published *before* the dispatch, as
              // everywhere else, so a handler reading `$78` sees the actor it
              // is being told about. `PHY`/`PLY` is why the walk survives it.
              wram_w16(w, W_HANDLER_SELF, rec);
              tail->c = out->c;
              if (!thread_call_handler(
                      w, rom, wram_r16(w, (uint32_t)rec + ACTOR_THREAD), id,
                      tail->c, tail)) {
                return false;
              }
              out->a = tail->a;
              out->x = tail->x;
              out->c = tail->c;
            }
          }
        }
      }
    }

    if (at == 0) break;
    at -= 2;
  }

  // `DEY DEY : BPL` off the end of index 0.
  out->y = 0xfffeu;
  return true;
}

// ---------------------------------------------------------------------------
// $80:BF67  actor_at_point
// ---------------------------------------------------------------------------

// `LDA $0002,Y : SEC : SBC $3A : CLC : ADC #$0006 : CMP #$000C : BCS`.
// Returns the value the ROM leaves in A as well as the verdict, because the
// skip paths exit with it still there.
static bool at_point_axis(uint16_t pos, uint16_t target, uint16_t* a) {
  *a = (uint16_t)(pos - target + AT_POINT_HALF_WINDOW);
  return *a < AT_POINT_WINDOW;
}

void actor_at_point(Wram* w, uint16_t self, uint16_t x, uint16_t y,
                    AtPointRegs* out) {
  wram_w16(w, AT_POINT_DP_SELF, self);
  wram_w16(w, AT_POINT_DP_X, x);
  wram_w16(w, AT_POINT_DP_Y, y);

  // A, X and Y all survive to the exit, so they are tracked rather than
  // returned: the ROM's registers hold whatever the last iteration left.
  out->a = self;  // `STA $38` does not disturb it
  out->y = y;
  out->found = false;

  uint16_t count = wram_r16(w, W_VISIBLE_ACTOR_COUNT);
  if (count == 0) {
    // $80:BF74. `LDX $9C : BEQ` — X is the zero it just loaded.
    PORT_COVER(at_point_empty);
    out->x = 0;
    return;
  }

  // $80:BF76. A byte index into a word array, walked downwards.
  for (int32_t i = (int32_t)count - 2; i >= 0; i -= 2) {
    out->x = (uint16_t)i;
    uint16_t rec = wram_r16(w, W_VISIBLE_ACTORS + (uint32_t)i);
    out->y = rec;

    if (rec == self) {
      PORT_COVER(at_point_self);
      continue;
    }
    uint16_t flags = wram_r16(w, rec + ACTOR_FLAGS);
    out->a = (uint16_t)(flags >> 1);  // `LSR A` leaves this behind
    if (!(flags & ACTOR_ACTIVE)) {
      PORT_COVER(at_point_inactive);
      continue;
    }

    uint16_t id = wram_r16(w, rec + ACTOR_COLLIDE_ID);
    out->a = id;
    // $80:BF88-BF9E. Three comparisons and four ways out of them: id 0, the
    // whole band $0C..$33, and $07 and $08 on their own.
    if (id == 0) {
      PORT_COVER(at_point_no_id);
      continue;
    }
    if (id >= AT_POINT_ID_RANGE_LO && id <= AT_POINT_ID_RANGE_HI) {
      PORT_COVER(at_point_id_band);
      continue;
    }
    if (id == AT_POINT_ID_SKIP_A || id == AT_POINT_ID_SKIP_B) {
      PORT_COVER(at_point_id_named);
      continue;
    }

    if (!at_point_axis(wram_r16(w, rec + ACTOR_X), x, &out->a)) {
      PORT_COVER(at_point_far_x);
      continue;
    }
    if (!at_point_axis(wram_r16(w, rec + ACTOR_Y), y, &out->a)) {
      PORT_COVER(at_point_far_y);
      continue;
    }

    // $80:BFBE. `PLD : SEC : RTL`, and X is left on the entry that matched.
    PORT_COVER(at_point_hit);
    out->found = true;
    return;
  }

  // $80:BFC3. The `BPL` fails on the first negative index, which is -2 because
  // the count is a byte count and so always even.
  PORT_COVER(at_point_none);
  out->x = 0xfffeu;
}

// ---------------------------------------------------------------------------
// $80:BFC8  actor_obstacle_at_point
// ---------------------------------------------------------------------------

// `$80:BFFE`-`$80:C020`, in the ROM's order, which is not sorted and is not
// worth sorting: the port compares against all seven either way, and keeping
// the order makes the listing and this array read the same.
//
// `$05` and `$06` are the two players — refused here by id as well as by
// record, above — and `$37` is the only one of the seven that an id above the
// band can be, since the six below it are all under `$0C`.
const uint16_t OBSTACLE_ID_SKIP[OBSTACLE_ID_SKIP_COUNT] = {
    0x0005, 0x0006, 0x0007, 0x0008, 0x0002, 0x0001, 0x0037,
};

void actor_obstacle_at_point(Wram* w, uint16_t a_in, uint16_t x, uint16_t y,
                             ObstacleRegs* out) {
  wram_w16(w, OBSTACLE_DP_X, x);
  wram_w16(w, OBSTACLE_DP_Y, y);

  // `$80:BFC8  PHD : PEA $0000 : PLD : STX $3A : STY $3C` — and no `STA`.
  // Nothing before the loop touches A, so a caller whose search never gets
  // going gets its own accumulator back. `$80:E4DD  LDA $08` loads one anyway,
  // which is the caller filing the thread slot for a routine that does not want
  // it; harmless, and the reason `a_in` has to be threaded through here.
  out->a = a_in;
  out->y = y;
  out->blocked = false;

  uint16_t count = wram_r16(w, W_VISIBLE_ACTOR_COUNT);
  if (count == 0) {
    // $80:BFD3. `LDX $9C : BEQ` — X is the zero it just loaded.
    PORT_COVER(obstacle_empty);
    out->x = 0;
    return;
  }

  // Read once: the ROM re-reads them every iteration, but nothing in the loop
  // writes WRAM, so the values cannot move underneath it.
  uint16_t player_a = wram_r16(w, W_PLAYER_A_RECORD);
  uint16_t player_b = wram_r16(w, W_PLAYER_B_RECORD);

  // $80:BFD5. Same backwards walk of the same byte-indexed array.
  for (int32_t i = (int32_t)count - 2; i >= 0; i -= 2) {
    out->x = (uint16_t)i;
    uint16_t rec = wram_r16(w, W_VISIBLE_ACTORS + (uint32_t)i);
    out->y = rec;

    // $80:BFDA. `CPY $D2` / `CPY $D4`, and in a one-player game `$D4` is zero,
    // so the second test is against a record address that cannot occur.
    if (rec == player_a) {
      PORT_COVER(obstacle_player_a);
      continue;
    }
    if (rec == player_b) {
      PORT_COVER(obstacle_player_b);
      continue;
    }

    uint16_t flags = wram_r16(w, rec + ACTOR_FLAGS);
    out->a = (uint16_t)(flags >> 1);  // `LSR A` leaves this behind
    if (!(flags & ACTOR_ACTIVE)) {
      PORT_COVER(obstacle_inactive);
      continue;
    }

    uint16_t id = wram_r16(w, rec + ACTOR_COLLIDE_ID);
    out->a = id;
    if (id == 0) {
      PORT_COVER(obstacle_no_id);
      continue;
    }
    // $80:BFED-BFFD. The high half, entered only when `CMP #$000C` says so, and
    // **it can fall out of the bottom into the chain below** rather than
    // deciding on its own — which is what makes `$37` reachable twice over.
    if (id >= OBSTACLE_ID_BAND_LO) {
      if (id <= OBSTACLE_ID_BAND_HI) {
        PORT_COVER(obstacle_id_band);
        continue;
      }
      if (id >= OBSTACLE_ID_CEILING) {
        PORT_COVER(obstacle_id_high);
        continue;
      }
      PORT_COVER(obstacle_id_above_band);
    } else {
      PORT_COVER(obstacle_id_below_band);
    }

    // $80:BFFE-C020. Seven `CMP : BEQ` in a row.
    bool named = false;
    for (int k = 0; k < OBSTACLE_ID_SKIP_COUNT; k++) {
      if (id == OBSTACLE_ID_SKIP[k]) {
        named = true;
        break;
      }
    }
    if (named) {
      PORT_COVER(obstacle_id_named);
      continue;
    }

    // The same six-pixel window as `actor_at_point`, down to sharing the
    // helper: `$80:C021`-`$80:C03E` is `$80:BFA0`-`$80:BFBD` byte for byte
    // except for the branch targets.
    if (!at_point_axis(wram_r16(w, rec + ACTOR_X), x, &out->a)) {
      PORT_COVER(obstacle_far_x);
      continue;
    }
    if (!at_point_axis(wram_r16(w, rec + ACTOR_Y), y, &out->a)) {
      PORT_COVER(obstacle_far_y);
      continue;
    }

    // $80:C03F. `PLD : SEC : RTL` — the step the caller was testing is blocked.
    PORT_COVER(obstacle_hit);
    out->blocked = true;
    return;
  }

  PORT_COVER(obstacle_none);
  out->x = 0xfffeu;
}

// ---------------------------------------------------------------------------
// $80:BE0C  actor_slot_alloc
// ---------------------------------------------------------------------------

void actor_slot_alloc(Wram* w, uint16_t caller_db, SlotAllocRegs* out) {
  uint16_t rec = ACTOR_SLOT_LAST;
  uint16_t tries = ACTOR_SLOT_COUNT;
  uint16_t flags = 0;

  for (;;) {
    // `LDA $0000,Y : LSR : BCC found` — the shift is the test, and the value
    // that survives it is what a failed scan hands back in A.
    flags = wram_r16(w, (uint16_t)(rec + ACTOR_FLAGS));
    if (!(flags & ACTOR_ACTIVE)) break;
    flags >>= 1;

    // `TYA : SBC #$0014 : TAY`, and the `LSR` that just set carry is what makes
    // the subtraction exactly $14 rather than $15.
    rec = (uint16_t)(rec - ACTOR_SLOT_STRIDE);
    if (--tries == 0) {
      PORT_COVER(slot_alloc_full);
      out->a = flags;
      out->x = 0;
      out->y = rec;
      out->n = (caller_db & 0x80u) != 0;
      out->z = (caller_db & 0xffu) == 0;
      out->c = true;  // the `SBC` that stepped Y, which never borrows here
      return;
    }
    PORT_COVER(slot_alloc_scan);
  }
  PORT_COVER(slot_alloc_took);

  // `LDA #$0001 : STA $0000,Y` — the whole flags word, so a slot arrives with
  // nothing but the allocation bit and its previous tenant's everything gone.
  wram_w16(w, (uint16_t)(rec + ACTOR_FLAGS), ACTOR_ACTIVE);
  wram_w16(w, (uint16_t)(rec + ACTOR_NEXT), wram_r16(w, W_ACTOR_LIST_HEAD));
  wram_w16(w, W_ACTOR_LIST_HEAD, rec);

  out->a = rec;  // `TYA`
  out->x = tries;
  out->y = rec;
  out->n = (caller_db & 0x80u) != 0;
  out->z = (caller_db & 0xffu) == 0;
  out->c = false;  // the `LSR` the `BCC` was taken on
}

// ---------------------------------------------------------------------------
// $80:BE41  actor_slot_free
// ---------------------------------------------------------------------------

void actor_slot_free(Wram* w, uint16_t rec, uint16_t caller_d, uint16_t in_x,
                     uint16_t in_y, SlotFreeRegs* out) {
  const uint16_t owner = wram_r16(w, W_SCHED_CUR_TASK);
  const uint16_t theirs = wram_r16(w, (uint16_t)(rec + ACTOR_THREAD));

  out->x = in_x;
  out->y = rec;  // `TAY`, before anything can decline

  // `LDA $0008 : CMP $000C,Y : BNE`. Someone else's record, so nothing happens
  // and the caller is not told — see the header.
  if (owner != theirs) {
    PORT_COVER(slot_free_not_mine);
    uint16_t r = (uint16_t)(owner - theirs);
    out->a = owner;
    out->n = (r & 0x8000u) != 0;
    out->z = false;
    out->c = owner >= theirs;
    return;
  }

  uint16_t flags = wram_r16(w, (uint16_t)(rec + ACTOR_FLAGS));
  if (!(flags & ACTOR_ACTIVE)) {
    PORT_COVER(slot_free_already);
    out->a = (uint16_t)(flags >> 1);
    out->n = false;  // an `LSR` cannot leave bit 15 set
    out->z = (flags >> 1) == 0;
    out->c = false;
    return;
  }
  PORT_COVER(slot_free_took);

  wram_w16(w, (uint16_t)(rec + ACTOR_FLAGS), 0);
  // `PHD : LDA #$0000 : TCD : STY $38`. From here the routine is on page zero
  // and `$38` is how the walk below recognises the record it is looking for.
  wram_w16(w, W_SLOT_FREE_SELF, rec);

  out->n = (caller_d & 0x8000u) != 0;  // `PLD`
  out->z = caller_d == 0;
  out->c = true;  // still the `LSR` that let the free through

  const uint16_t head = wram_r16(w, W_ACTOR_LIST_HEAD);
  if (rec == head) {
    PORT_COVER(slot_free_head);
    uint16_t next = wram_r16(w, (uint16_t)(rec + ACTOR_NEXT));
    wram_w16(w, W_ACTOR_LIST_HEAD, next);
    out->a = next;
    return;
  }

  // Walk from the head looking for the link that points at us. `X` trails one
  // record behind `Y`, which is the pair the unlink needs.
  uint16_t prev = head;
  for (;;) {
    uint16_t next = wram_r16(w, (uint16_t)(prev + ACTOR_NEXT));
    if (next == 0) {
      // Off the end without finding it: the record's flags are cleared and it
      // is still on the list. Nothing in the ROM stops that happening and
      // nothing here does either.
      PORT_COVER(slot_free_unlisted);
      out->a = 0;  // still the `LDA #$0000` from the flags store
      out->x = prev;
      out->y = 0;
      return;
    }
    if (next == rec) {
      PORT_COVER(slot_free_unlink);
      uint16_t after = wram_r16(w, (uint16_t)(rec + ACTOR_NEXT));
      wram_w16(w, (uint16_t)(prev + ACTOR_NEXT), after);
      out->a = after;
      out->x = prev;
      out->y = rec;
      return;
    }
    PORT_COVER(slot_free_walk);
    prev = next;
  }
}
