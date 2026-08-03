#include "port/camera.h"

#include "port/coverage.h"
#include "port/oam.h"
#include "port/terrain.h"

// The map lives in bank $7F and the destination the callers set up is in $7E,
// so both ends of the copy are WRAM and neither needs the bus dispatch
// `port/lzss.c` has to do. `$56` is still *read* rather than hardcoded, because
// it belongs to the caller and a routine that quietly substituted its own idea
// of the bank would agree with the ROM until the day a caller changed its mind.
//
// What is assumed is narrower and worth naming: that the bank is $7E or $7F.
// Any other one and this maps it into WRAM anyway instead of going to the
// cartridge. That is not guarded because `verify` is the guard — it compares
// all 128 KB after every call, so the first caller ever to pass something else
// diverges immediately rather than quietly writing to the wrong place.
static uint32_t wram_off(uint16_t bank, uint16_t addr) {
  return ((uint32_t)(bank & 1u) << 16) | addr;
}

void tilemap_copy_column(Wram* w, uint16_t count, uint16_t col, uint16_t row,
                         TilemapCopyRegs* out) {
  // `JSL $80AD1C : STA $50` — where the top tile of the strip lives.
  TilemapAddrRegs addr;
  tilemap_tile_addr(w, col, row, &addr);
  uint16_t src = addr.a;
  wram_w16(w, CAM_DP_SRC, src);
  wram_w16(w, CAM_DP_SRC_BANK, TILEMAP_SRC_BANK);

  uint16_t dst = wram_r16(w, CAM_DP_DST);
  uint16_t dst_bank = wram_r16(w, CAM_DP_DST_BANK);
  uint16_t stride = wram_r16(w, W_TILEMAP_ROW_BYTES);
  uint16_t threshold = wram_r16(w, W_TILE_PRIORITY_BELOW);

  // `DEX : BNE` — a do-while, so zero means 65,536. See the header.
  uint16_t x = count;
  uint16_t y = 0;
  do {
    uint16_t tile = wram_r16(w, wram_off(TILEMAP_SRC_BANK, src));
    uint32_t at = wram_off(dst_bank, (uint16_t)(dst + y));
    wram_w16(w, at, tile);
    if ((tile & TILEMAP_COPY_MASK) < threshold) {
      PORT_COVER(column_priority);
      wram_w16(w, at, (uint16_t)(tile | TILEMAP_PRIORITY_BIT));
    } else {
      PORT_COVER(column_plain);
    }
    src = (uint16_t)(src + stride);
    wram_w16(w, CAM_DP_SRC, src);
    y = (uint16_t)(y + 2);
  } while (--x != 0);

  // `PLA : ASL A : CLC : ADC $54 : STA $54` — the count back off the stack,
  // doubled into bytes, added to the destination. The flags are this add's.
  uint32_t sum = (uint32_t)(uint16_t)(count << 1) + dst;
  wram_w16(w, CAM_DP_DST, (uint16_t)sum);

  out->a = (uint16_t)sum;
  out->x = 0;  // where `DEX : BNE` leaves it
  out->y = y;
  out->n = (sum & 0x8000u) != 0;
  out->z = (uint16_t)sum == 0;
  out->c = sum > 0xffffu;
}

// --- $80:A401  tilemap_buffer_alloc ----------------------------------------

bool tilemap_buffer_alloc_supported(const Wram* w, uint16_t size) {
  // `LDA $CC : SEC : SBC $01,S : BCC retry` — carry clear means a borrow, and a
  // borrow is the spin. Ten traces and it has never happened once.
  return wram_r16(w, W_TILEMAP_ARENA_LEFT) >= size;
}

void tilemap_buffer_alloc(Wram* w, uint16_t size, TilemapAllocRegs* out) {
  uint16_t left = wram_r16(w, W_TILEMAP_ARENA_LEFT);
  wram_w16(w, W_TILEMAP_ARENA_LEFT, (uint16_t)(left - size));  // the guard
                                                               // proved this
                                                               // does not
                                                               // borrow
  uint16_t next = wram_r16(w, W_TILEMAP_ARENA_NEXT);
  uint32_t bumped = (uint32_t)next + size;
  wram_w16(w, W_TILEMAP_ARENA_NEXT, (uint16_t)bumped);

  // `TAY` before the add and `TYA` after it: what comes back is the address the
  // caller may use, which is where the arena stood *before* the bump.
  out->a = next;
  out->y = next;
  out->x = size;  // `PLX` pulls the argument back
  // N and Z are the `TYA`'s and describe that address; carry is still the
  // `ADC`'s, two instructions earlier, and nothing since has touched it.
  out->n = (next & 0x8000u) != 0;
  out->z = next == 0;
  out->c = bumped > 0xffffu;
}

// --- $80:A61D  tilemap_copy_row --------------------------------------------

bool tilemap_copy_row_supported(const Wram* w) {
  return tilemap_buffer_alloc_supported(w, TILEMAP_ROW_ALLOC);
}

void tilemap_copy_row(Wram* w, uint16_t col, uint16_t row,
                      TilemapCopyRegs* out) {
  TilemapAddrRegs addr;
  tilemap_tile_addr(w, col, row, &addr);
  wram_w16(w, CAM_DP_SRC, addr.a);
  wram_w16(w, CAM_DP_SRC_BANK, TILEMAP_SRC_BANK);

  // `LDA #$0042 : JSR $A401 : STA $54` — it buys its own strip. The guard
  // above has already established this cannot be the call that spins.
  TilemapAllocRegs got;
  tilemap_buffer_alloc(w, TILEMAP_ROW_ALLOC, &got);
  wram_w16(w, CAM_DP_DST, got.a);
  wram_w16(w, CAM_DP_DST_BANK, TILEMAP_DST_BANK);

  uint16_t src = addr.a;
  uint16_t threshold = wram_r16(w, W_TILE_PRIORITY_BELOW);
  uint16_t tile = 0;
  bool ge = false;

  // `LDY #$0040 ... DEY : DEY : BPL` — 33 tiles, and both pointers index by the
  // same Y because a row is contiguous at both ends.
  uint16_t y = TILEMAP_ROW_FIRST_Y;
  for (;;) {
    tile = wram_r16(w, wram_off(TILEMAP_SRC_BANK, (uint16_t)(src + y)));
    uint32_t at = wram_off(TILEMAP_DST_BANK, (uint16_t)(got.a + y));
    wram_w16(w, at, tile);
    ge = (tile & TILEMAP_COPY_MASK) >= threshold;
    if (ge) {
      PORT_COVER(row_plain);
    } else {
      PORT_COVER(row_priority);
      wram_w16(w, at, (uint16_t)(tile | TILEMAP_PRIORITY_BIT));
    }
    if (y == 0) break;
    y = (uint16_t)(y - 2);
  }

  // A is the last tile the loop touched, after its `AND` and — on the priority
  // path — its `ORA`. Carry is that same tile's `CMP $DC`. Neither is a summary
  // of the copy; both are whatever the thirty-third tile happened to be.
  // On the plain path A is still the `AND`'s result. On the priority path it is
  // *not*: `LDA #$2000 : ORA [$54],Y` ORs against the word already stored, which
  // is the whole tile, so the mask is gone from A again by the time it returns.
  out->a = ge ? (uint16_t)(tile & TILEMAP_COPY_MASK)
              : (uint16_t)(tile | TILEMAP_PRIORITY_BIT);
  out->c = ge;
  // X is the allocator's `PLX`, so it is the size argument and nothing else.
  out->x = TILEMAP_ROW_ALLOC;
  // `DEY : DEY` runs one more time than the copy does, so Y falls out at -2 and
  // N and Z describe that rather than anything the routine computed.
  out->y = 0xfffeu;
  out->n = true;
  out->z = false;
}

// --- $80:A54D  camera_window_update ----------------------------------------

void camera_window_update(Wram* w, CameraWindowRegs* out) {
  uint16_t tx = (uint16_t)(wram_r16(w, W_CAMERA_X) >> 3);
  wram_w16(w, W_CAMERA_TILE_X, tx);
  wram_w16(w, W_CAMERA_TILE_X_END, (uint16_t)(tx + CAMERA_WINDOW_TILES_X));

  uint16_t ty = (uint16_t)(wram_r16(w, W_CAMERA_Y) >> 3);
  wram_w16(w, W_CAMERA_TILE_Y, ty);
  wram_w16(w, W_CAMERA_TILE_Y_END, (uint16_t)(ty + CAMERA_WINDOW_TILES_Y));

  uint16_t cx = wram_r16(w, W_TILEMAP_CURSOR_X);
  wram_w16(w, W_TILEMAP_CURSOR_X_END,
           (uint16_t)((cx + CAMERA_WINDOW_TILES_X) & TILEMAP_CURSOR_MASK_X));

  // The last two instructions decide three of the four outputs between them,
  // and they disagree about which: `ADC` sets carry and the `AND` after it sets
  // N and Z. Both are published because neither can be derived from the other.
  uint16_t cy = wram_r16(w, W_TILEMAP_CURSOR_Y);
  uint32_t sum = (uint32_t)cy + CAMERA_WINDOW_TILES_Y;
  uint16_t masked = (uint16_t)(sum & TILEMAP_CURSOR_MASK_Y);
  wram_w16(w, W_TILEMAP_CURSOR_Y_END, masked);

  out->a = masked;
  out->n = (masked & 0x8000u) != 0;
  out->z = masked == 0;
  out->c = sum > 0xffffu;
}

// --- $80:A588  camera_split_y ----------------------------------------------

void camera_split_y(Wram* w, CameraSplitRegs* out) {
  uint16_t below =
      (uint16_t)(wram_r16(w, W_TILEMAP_CURSOR_Y) & TILEMAP_CURSOR_MASK_Y);
  wram_w16(w, CAM_DP_RUN_A_DST, below);
  uint32_t diff = (uint32_t)CAMERA_SPLIT_ROWS - below;
  uint16_t above = (uint16_t)diff;
  wram_w16(w, CAM_DP_RUN_A_SRC, above);

  out->a = above;
  out->n = (above & 0x8000u) != 0;
  out->z = above == 0;
  // `SEC : SBC` — carry set means no borrow, which here means the cursor was
  // never above 32 in the first place. It cannot be, given the mask above.
  out->c = CAMERA_SPLIT_ROWS >= below;
}

// --- $80:A599  camera_split_x ----------------------------------------------

void camera_split_x(Wram* w, CameraSplitRegs* out) {
  uint16_t cx = wram_r16(w, W_TILEMAP_CURSOR_X);
  uint16_t measured;
  if (cx < CAMERA_SPLIT_COLUMNS) {
    // The cursor is in the left screen, so the run that starts at it is the one
    // whose length has to be worked out and the overflow lands at word `$400`.
    PORT_COVER(split_x_left);
    measured = (uint16_t)((CAMERA_SPLIT_COLUMNS - cx) << 1);
    wram_w16(w, CAM_DP_RUN_A_SRC, 0);
    wram_w16(w, CAM_DP_RUN_A_DST, cx);
    wram_w16(w, CAM_DP_RUN_A_LEN, measured);
    wram_w16(w, CAM_DP_RUN_B_SRC, measured);
    wram_w16(w, CAM_DP_RUN_B_DST, TILEMAP_SECOND_SCREEN);
    wram_w16(w, CAM_DP_RUN_B_LEN,
             (uint16_t)(TILEMAP_ROW_ALLOC - measured));
  } else {
    // ...and here it is in the right screen, so the same two runs come out in
    // the other order: B starts at the cursor and A picks up the wrap at 0.
    PORT_COVER(split_x_right);
    measured = (uint16_t)((TILEMAP_COLUMNS - cx) << 1);
    wram_w16(w, CAM_DP_RUN_B_SRC, 0);
    wram_w16(w, CAM_DP_RUN_B_DST,
             (uint16_t)((cx & TILEMAP_SCREEN_MASK) | TILEMAP_SECOND_SCREEN));
    wram_w16(w, CAM_DP_RUN_B_LEN, measured);
    wram_w16(w, CAM_DP_RUN_A_SRC, measured);
    wram_w16(w, CAM_DP_RUN_A_DST, 0);
    wram_w16(w, CAM_DP_RUN_A_LEN,
             (uint16_t)(TILEMAP_ROW_ALLOC - measured));
  }

  // Both branches close on `LDA #$0042 : SEC : SBC <the run just measured>`,
  // so the four outputs are one expression written twice.
  uint16_t rest = (uint16_t)(TILEMAP_ROW_ALLOC - measured);
  out->a = rest;
  out->n = (rest & 0x8000u) != 0;
  out->z = rest == 0;
  out->c = TILEMAP_ROW_ALLOC >= measured;
}

// --- $80:9E6D  vram_queue_request ------------------------------------------

void vram_queue_request(Wram* w, uint16_t a, VramRequestRegs* out) {
  uint16_t flags = wram_r16(w, W_RENDER_FLAGS);
  if (flags & RENDER_FLAG_TRANSFER) {
    // `BIT $26 : BMI`. N is bit 15 of *memory*; Z is `A & memory`, so what this
    // exit returns in Z is a fact about the caller's accumulator.
    PORT_COVER(request_pending);
    out->a = a;
    out->n = true;
    out->z = (a & flags) == 0;
    return;
  }
  uint16_t queued = wram_r16(w, W_VRAM_QUEUE_COUNT);
  if (queued == 0) {
    // No coverage site, and that is a decision rather than an oversight. All
    // four call sites store a *nonzero* `$CE` in the instruction immediately
    // before the `JSR` -- `$80:A540` an `ADC #$0004`, the other three an
    // `INX : INX : STX $CE` -- so this exit cannot be reached from anywhere the
    // game calls it. It was a site for one corpus run, came back the only
    // untaken one of the five added, and `coverage.h` says in as many words
    // that a site no corpus can reach dilutes the number it exists to keep.
    // The code stays because it is three correct lines and the ROM has them.
    out->a = 0;
    out->n = false;
    out->z = true;
    return;
  }
  PORT_COVER(request_made);
  wram_w16(w, W_RENDER_FLAGS, RENDER_FLAG_TRANSFER);
  out->a = RENDER_FLAG_TRANSFER;
  out->n = true;
  out->z = false;
}

// --- $80:A68B / $A70A / $A789 / $A816  the four scroll routines --------------

// The six differences between a routine and its mirror. See `port/camera.h`.
typedef struct {
  bool forward;         // towards the far edge: `INC A`, and a limit to check
  uint16_t limit_at;    // `$B8` / `$B6`, and read only when `forward`
  uint16_t camera_at;   // `$1B6A` / `$1B6C`
  uint16_t sub_at;      // `$1B66` / `$1B68`, the sub-tile remainder
  uint16_t cursor_at;   // `$1B76` / `$1B7A`
  uint16_t cursor_mask;
  uint16_t edge_at;     // the tile-window word the new strip is read from
  uint16_t dest_at;     // the cursor word its VRAM address is looked up with
} CameraScrollAxis;

static const CameraScrollAxis SCROLL_LEFT = {
    .forward = false,
    .camera_at = W_CAMERA_X,
    .sub_at = W_CAMERA_SUB_X,
    .cursor_at = W_TILEMAP_CURSOR_X,
    .cursor_mask = TILEMAP_CURSOR_MASK_X,
    .edge_at = W_CAMERA_TILE_X,
    .dest_at = W_TILEMAP_CURSOR_X,
};

static const CameraScrollAxis SCROLL_RIGHT = {
    .forward = true,
    .limit_at = W_CAMERA_MAX_X,
    .camera_at = W_CAMERA_X,
    .sub_at = W_CAMERA_SUB_X,
    .cursor_at = W_TILEMAP_CURSOR_X,
    .cursor_mask = TILEMAP_CURSOR_MASK_X,
    .edge_at = W_CAMERA_TILE_X_END,
    .dest_at = W_TILEMAP_CURSOR_X_END,
};

static const CameraScrollAxis SCROLL_DOWN = {
    .forward = true,
    .limit_at = W_CAMERA_MAX_Y,
    .camera_at = W_CAMERA_Y,
    .sub_at = W_CAMERA_SUB_Y,
    .cursor_at = W_TILEMAP_CURSOR_Y,
    .cursor_mask = TILEMAP_CURSOR_MASK_Y,
    .edge_at = W_CAMERA_TILE_Y_END,
    .dest_at = W_TILEMAP_CURSOR_Y_END,
};

static const CameraScrollAxis SCROLL_UP = {
    .forward = false,
    .camera_at = W_CAMERA_Y,
    .sub_at = W_CAMERA_SUB_Y,
    .cursor_at = W_TILEMAP_CURSOR_Y,
    .cursor_mask = TILEMAP_CURSOR_MASK_Y,
    .edge_at = W_CAMERA_TILE_Y,
    .dest_at = W_TILEMAP_CURSOR_Y,
};

// Will this call get as far as allocating? Both guards below are this question
// and then one arena test, because a call that stops at an early exit cannot
// reach `$80:A401` and there is nothing for a guard to refuse.
static bool camera_scroll_allocates(const Wram* w, const CameraScrollAxis* ax) {
  uint16_t cam = wram_r16(w, ax->camera_at);
  if (ax->forward) {
    if (cam == wram_r16(w, ax->limit_at)) return false;
    return (((uint16_t)(cam + 1)) & 7u) == 0;
  }
  if (cam == 0) return false;
  return (((uint16_t)(cam - 1)) & 7u) == 7u;
}

typedef enum {
  SCROLL_PINNED,    // against the edge of the map; nothing happened at all
  SCROLL_MID_TILE,  // the camera moved, but not across a tile boundary
  SCROLL_STRIP,     // ...and it did, so a strip of map has to appear
} ScrollStep;

// Parts 1 and 2, and the cursor step that opens part 3. Fills `out` completely
// on the two exits that end here.
static ScrollStep camera_scroll_step(Wram* w, const CameraScrollAxis* ax,
                                     const CameraScrollIn* in,
                                     CameraScrollRegs* out) {
  uint16_t cam = wram_r16(w, ax->camera_at);
  bool carry;

  out->x = in->x;  // neither index is touched before the strip work
  out->y = in->y;

  if (ax->forward) {
    // `LDA $1B6A : CMP $B8 : BNE +` — the camera is as far as the map goes.
    uint16_t limit = wram_r16(w, ax->limit_at);
    carry = cam >= limit;
    if (cam == limit) {
      PORT_COVER(scroll_at_limit);
      out->a = cam;
      out->n = false;  // `cam - limit` is zero on this path, so it cannot be
      out->z = true;
      out->c = carry;
      return SCROLL_PINNED;
    }
    cam = (uint16_t)(cam + 1);
    wram_w16(w, ax->camera_at, cam);
    wram_w16(w, ax->sub_at, (uint16_t)(wram_r16(w, ax->sub_at) + 1));
  } else {
    // `LDA $1B6A : BEQ out` — the same question at the other end, asked with no
    // `CMP`, which is why this is the one exit that returns a carry it never
    // set. The caller's goes straight back out.
    if (cam == 0) {
      PORT_COVER(scroll_at_zero);
      out->a = 0;
      out->n = false;
      out->z = true;
      out->c = in->c;
      return SCROLL_PINNED;
    }
    carry = in->c;
    cam = (uint16_t)(cam - 1);
    wram_w16(w, ax->camera_at, cam);
    wram_w16(w, ax->sub_at, (uint16_t)(wram_r16(w, ax->sub_at) - 1));
  }

  // `AND #$0007`, and going backwards a `CMP #$0007` after it. One question —
  // did this pixel cross a tile boundary — but the answer is a remainder of 0
  // going forwards and 7 going backwards, and the flags the two publish on the
  // way out have nothing in common.
  uint16_t rem = (uint16_t)(cam & 7u);
  bool crossed = ax->forward ? rem == 0 : rem == 7;
  if (!crossed) {
    out->a = rem;
    out->z = false;
    if (ax->forward) {
      PORT_COVER(scroll_mid_tile_fwd);
      out->n = false;  // the `AND`'s, and A is at most 7
      out->c = carry;  // untouched since the `CMP $B8` above
    } else {
      PORT_COVER(scroll_mid_tile_back);
      out->n = ((uint16_t)(rem - 7u) & 0x8000u) != 0;
      out->c = rem >= 7u;
    }
    return SCROLL_MID_TILE;
  }

  // `LDA $1B76 : INC/DEC A : AND #mask : STA $1B76` — the writing cursor
  // follows the camera around the tilemap's own wrap rather than the map's.
  uint16_t cursor = wram_r16(w, ax->cursor_at);
  cursor = ax->forward ? (uint16_t)(cursor + 1) : (uint16_t)(cursor - 1);
  wram_w16(w, ax->cursor_at, (uint16_t)(cursor & ax->cursor_mask));
  return SCROLL_STRIP;
}

static void camera_scroll_x(Wram* w, const Rom* rom,
                            const CameraScrollAxis* ax,
                            const CameraScrollIn* in, CameraScrollRegs* out) {
  if (camera_scroll_step(w, ax, in, out) != SCROLL_STRIP) return;

  // `LDA #$0040 : JSR $A401 : STA $60 : STA $54` — one column of the tilemap,
  // bought before anything is read. `$60` keeps the address; `$54` is the
  // cursor `$80:A5E5` advances as it fills.
  TilemapAllocRegs got;
  tilemap_buffer_alloc(w, TILEMAP_COLUMN_ALLOC, &got);
  wram_w16(w, CAM_DP_RUN_A_LEN, got.a);
  wram_w16(w, CAM_DP_DST, got.a);
  wram_w16(w, CAM_DP_DST_BANK, TILEMAP_DST_BANK);

  CameraWindowRegs win;
  camera_window_update(w, &win);
  CameraSplitRegs split;
  camera_split_y(w, &split);

  uint16_t col = wram_r16(w, ax->edge_at);
  uint16_t row = wram_r16(w, W_CAMERA_TILE_Y);
  uint16_t above = wram_r16(w, CAM_DP_RUN_A_SRC);
  uint16_t below = wram_r16(w, CAM_DP_RUN_A_DST);

  TilemapCopyRegs copy;
  if (below != 0) {
    // The column wraps somewhere inside the tilemap, so its two halves come
    // out of two different places in the map.
    PORT_COVER(scroll_x_split);
    tilemap_copy_column(w, below, col, (uint16_t)(row + above), &copy);
  } else {
    PORT_COVER(scroll_x_whole);
  }
  // `32 - (cursor & 31)` is 1..32, never 0, so this one needs no `BEQ` in front
  // of it and the ROM does not have one.
  tilemap_copy_column(w, above, col, row, &copy);

  uint16_t at = wram_r16(w, W_VRAM_QUEUE_COUNT);
  wram_w16(w, (uint32_t)W_VRAM_QUEUE_SRC + at, wram_r16(w, CAM_DP_RUN_A_LEN));
  wram_w16(w, (uint32_t)W_VRAM_QUEUE_BANK + at, TILEMAP_DST_BANK);
  uint16_t index = (uint16_t)(wram_r16(w, ax->dest_at) << 1);
  uint32_t dest = (uint32_t)rom_word(rom, TILEMAP_COLUMN_DEST_TABLE + index) +
                  wram_r16(w, W_TILEMAP_VRAM_BASE);
  wram_w16(w, (uint32_t)W_VRAM_QUEUE_DEST + at, (uint16_t)dest);
  wram_w16(w, (uint32_t)W_VRAM_QUEUE_VMAIN + at, VMAIN_STEP_COLUMN);
  wram_w16(w, (uint32_t)W_VRAM_QUEUE_SIZE + at, TILEMAP_COLUMN_ALLOC);
  at = (uint16_t)(at + 2);
  wram_w16(w, W_VRAM_QUEUE_COUNT, at);

  // A is still the `LDA #$0040` two stores ago when the request is made, which
  // is what decides the Z it comes back with on the already-pending path.
  VramRequestRegs req;
  vram_queue_request(w, TILEMAP_COLUMN_ALLOC, &req);

  out->a = req.a;
  out->x = at;
  out->y = index;
  out->n = req.n;
  out->z = req.z;
  out->c = dest > 0xffffu;
}

static void camera_scroll_y(Wram* w, const Rom* rom,
                            const CameraScrollAxis* ax,
                            const CameraScrollIn* in, CameraScrollRegs* out) {
  if (camera_scroll_step(w, ax, in, out) != SCROLL_STRIP) return;

  CameraWindowRegs win;
  camera_window_update(w, &win);
  CameraSplitRegs split;
  camera_split_x(w, &split);

  // `LDX $1B6E : LDY $1B74 : JSR $A61D` — no allocation here, because the row
  // copier buys its own strip and leaves the address in `$54`.
  TilemapCopyRegs copy;
  tilemap_copy_row(w, wram_r16(w, W_CAMERA_TILE_X), wram_r16(w, ax->edge_at),
                   &copy);
  uint16_t strip = wram_r16(w, CAM_DP_DST);

  uint16_t index = (uint16_t)(wram_r16(w, ax->dest_at) << 1);
  uint16_t table = rom_word(rom, TILEMAP_ROW_DEST_TABLE + index);
  uint16_t base = wram_r16(w, W_TILEMAP_VRAM_BASE);

  uint16_t at = wram_r16(w, W_VRAM_QUEUE_COUNT);
  uint32_t dest = 0;
  uint16_t length = 0;
  for (int run = 0; run < 2; run++) {
    uint16_t src_at = run ? CAM_DP_RUN_B_SRC : CAM_DP_RUN_A_SRC;
    uint16_t dst_at = run ? CAM_DP_RUN_B_DST : CAM_DP_RUN_A_DST;
    uint16_t len_at = run ? CAM_DP_RUN_B_LEN : CAM_DP_RUN_A_LEN;

    wram_w16(w, (uint32_t)W_VRAM_QUEUE_SRC + at,
             (uint16_t)(wram_r16(w, src_at) + strip));
    wram_w16(w, (uint32_t)W_VRAM_QUEUE_BANK + at, TILEMAP_DST_BANK);
    // `CLC : ADC $5E : ADC $1B7E`, and there is no second `CLC`: the base add
    // carries the offset add's out. Neither can overflow with a `$7800` base
    // and a table that stops at `$3E0`, but the ROM chains them and so does
    // this, because the day one of them can is the day it matters.
    uint32_t sum = (uint32_t)table + wram_r16(w, dst_at);
    dest = (uint32_t)(uint16_t)sum + base + (sum > 0xffffu ? 1u : 0u);
    wram_w16(w, (uint32_t)W_VRAM_QUEUE_DEST + at, (uint16_t)dest);
    wram_w16(w, (uint32_t)W_VRAM_QUEUE_VMAIN + at, VMAIN_STEP_ROW);
    length = wram_r16(w, len_at);
    wram_w16(w, (uint32_t)W_VRAM_QUEUE_SIZE + at, length);
    at = (uint16_t)(at + 2);
  }
  wram_w16(w, W_VRAM_QUEUE_COUNT, at);

  VramRequestRegs req;
  vram_queue_request(w, length, &req);

  out->a = req.a;
  out->x = at;
  out->y = index;
  out->n = req.n;
  out->z = req.z;
  out->c = dest > 0xffffu;
}

bool camera_scroll_left_supported(const Wram* w) {
  return !camera_scroll_allocates(w, &SCROLL_LEFT) ||
         tilemap_buffer_alloc_supported(w, TILEMAP_COLUMN_ALLOC);
}

bool camera_scroll_right_supported(const Wram* w) {
  return !camera_scroll_allocates(w, &SCROLL_RIGHT) ||
         tilemap_buffer_alloc_supported(w, TILEMAP_COLUMN_ALLOC);
}

bool camera_scroll_down_supported(const Wram* w) {
  return !camera_scroll_allocates(w, &SCROLL_DOWN) ||
         tilemap_copy_row_supported(w);
}

bool camera_scroll_up_supported(const Wram* w) {
  return !camera_scroll_allocates(w, &SCROLL_UP) || tilemap_copy_row_supported(w);
}

void camera_scroll_left(Wram* w, const Rom* rom, const CameraScrollIn* in,
                        CameraScrollRegs* out) {
  camera_scroll_x(w, rom, &SCROLL_LEFT, in, out);
}

void camera_scroll_right(Wram* w, const Rom* rom, const CameraScrollIn* in,
                         CameraScrollRegs* out) {
  camera_scroll_x(w, rom, &SCROLL_RIGHT, in, out);
}

void camera_scroll_down(Wram* w, const Rom* rom, const CameraScrollIn* in,
                        CameraScrollRegs* out) {
  camera_scroll_y(w, rom, &SCROLL_DOWN, in, out);
}

void camera_scroll_up(Wram* w, const Rom* rom, const CameraScrollIn* in,
                      CameraScrollRegs* out) {
  camera_scroll_y(w, rom, &SCROLL_UP, in, out);
}

// --- $80:A93F  camera_follow -------------------------------------------------

// Where the view wants to be centred, or false if nothing is on the board to
// centre it on. Shared by the routine and its guard, which have to agree about
// which scroll routines a call would reach.
static bool camera_follow_target(const Wram* w, uint16_t* tx, uint16_t* ty,
                                 bool cover) {
  if (wram_r16(w, W_RENDER_FLAGS) & RENDER_FLAG_CAMERA_HELD) {
    if (cover) PORT_COVER(follow_held);
    return false;
  }
  uint16_t a = wram_r16(w, W_PLAYER_A_RECORD);
  uint16_t b = wram_r16(w, W_PLAYER_B_RECORD);

  if (a != 0 && b != 0) {
    // `LDA ($D2),Y : CLC : ADC ($D4),Y : LSR A` — and the `LSR` is of A alone,
    // so a sum over `$FFFF` loses its carry rather than shifting it back in.
    if (cover) PORT_COVER(follow_midpoint);
    *tx = (uint16_t)((uint16_t)(wram_r16(w, (uint32_t)a + ACTOR_X) +
                                wram_r16(w, (uint32_t)b + ACTOR_X)) >> 1);
    *ty = (uint16_t)((uint16_t)(wram_r16(w, (uint32_t)a + ACTOR_Y) +
                                wram_r16(w, (uint32_t)b + ACTOR_Y)) >> 1);
    return true;
  }
  if (a != 0) {
    if (cover) PORT_COVER(follow_player_a);
    *tx = wram_r16(w, (uint32_t)a + ACTOR_X);
    *ty = wram_r16(w, (uint32_t)a + ACTOR_Y);
    return true;
  }
  if (b != 0) {
    if (cover) PORT_COVER(follow_player_b);
    *tx = wram_r16(w, (uint32_t)b + ACTOR_X);
    *ty = wram_r16(w, (uint32_t)b + ACTOR_Y);
    return true;
  }
  if (cover) PORT_COVER(follow_no_players);
  return false;
}

// `SEC : SBC #$0080 : SEC : SBC $1B6A`, and the second `SEC` is why this is a
// function: the first subtract can borrow, and the ROM sets carry again rather
// than letting it.
static uint16_t camera_delta(const Wram* w, uint16_t target, uint16_t centre,
                             uint16_t camera_at) {
  return (uint16_t)(target - centre - wram_r16(w, camera_at));
}

bool camera_follow_supported(const Wram* w) {
  uint16_t tx = 0, ty = 0;
  if (!camera_follow_target(w, &tx, &ty, false)) return true;

  // Up to two strips in one call, and the second is bought out of what the
  // first left, so the arena has to answer for both at once.
  uint16_t need = 0;
  uint16_t dx = camera_delta(w, tx, CAMERA_CENTRE_X, W_CAMERA_X);
  if (dx != 0) {
    const CameraScrollAxis* ax = (dx & 0x8000u) ? &SCROLL_LEFT : &SCROLL_RIGHT;
    if (camera_scroll_allocates(w, ax)) need = (uint16_t)(need + TILEMAP_COLUMN_ALLOC);
  }
  uint16_t dy = camera_delta(w, ty, CAMERA_CENTRE_Y, W_CAMERA_Y);
  if (dy != 0) {
    const CameraScrollAxis* ax = (dy & 0x8000u) ? &SCROLL_UP : &SCROLL_DOWN;
    if (camera_scroll_allocates(w, ax)) need = (uint16_t)(need + TILEMAP_ROW_ALLOC);
  }
  return need == 0 || tilemap_buffer_alloc_supported(w, need);
}

void camera_follow(Wram* w, const Rom* rom, uint16_t x, uint16_t y,
                   CameraFollowRegs* out) {
  uint16_t tx = 0, ty = 0;
  if (!camera_follow_target(w, &tx, &ty, true)) {
    // Both exits leave A at zero: one is the `LDA #$0000` that set the direct
    // page and never got touched again, the other the `LDA $D4` that has just
    // read a zero. Neither index is mentioned on either path.
    out->a = 0;
    out->x = x;
    out->y = y;
    return;
  }
  wram_w16(w, W_CAMERA_TARGET_X, tx);
  wram_w16(w, W_CAMERA_TARGET_Y, ty);

  // `LDY #$0006` is the last index all three record paths load, and nothing
  // reloads Y afterwards — so this is what a scroll routine is handed, and what
  // comes back if none of them runs.
  uint16_t ry = ACTOR_Y;

  uint16_t dx = camera_delta(w, tx, CAMERA_CENTRE_X, W_CAMERA_X);
  uint16_t acc = dx;
  uint16_t rx = dx;  // `TAX`, and the `BEQ` under it is testing A
  if (dx != 0) {
    // `ASL A : BCC` — the shift is only ever read for its carry, which is bit
    // 15. One pixel, whatever the distance.
    bool behind = (dx & 0x8000u) != 0;
    CameraScrollIn args = {rx, ry, behind};
    CameraScrollRegs r;
    if (behind) {
      PORT_COVER(follow_left);
      camera_scroll_left(w, rom, &args, &r);
    } else {
      PORT_COVER(follow_right);
      camera_scroll_right(w, rom, &args, &r);
    }
    acc = r.a;
    rx = r.x;
    ry = r.y;
  } else {
    PORT_COVER(follow_x_still);
  }

  uint16_t dy = camera_delta(w, ty, CAMERA_CENTRE_Y, W_CAMERA_Y);
  acc = dy;
  rx = dy;
  if (dy != 0) {
    bool behind = (dy & 0x8000u) != 0;
    CameraScrollIn args = {rx, ry, behind};
    CameraScrollRegs r;
    if (behind) {
      PORT_COVER(follow_up);
      camera_scroll_up(w, rom, &args, &r);
    } else {
      PORT_COVER(follow_down);
      camera_scroll_down(w, rom, &args, &r);
    }
    acc = r.a;
    rx = r.x;
    ry = r.y;
  } else {
    PORT_COVER(follow_y_still);
  }

  out->a = acc;
  out->x = rx;
  out->y = ry;
}
