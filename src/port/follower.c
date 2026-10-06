// $82:F40B  a thing kept beside another -- see port/follower.h.

#include "port/follower.h"

#include "port/coverage.h"
#include "port/frontend.h"  // the frame count
#include "port/oam.h"       // the display record's fields

void follower_place(Wram* w, const Rom* rom, PortCpu* c, FollowerWork* k) {
  const uint16_t page = c->d;
  c->x = wram_r16(w, (uint16_t)(page + FOLLOWER_DP_PLACE));
  c->a = rom_word(rom, FOLLOWER_PLACES + c->x);
  set_nz16(c, c->a);
  k->blocks[FW_HEAD]++;
  if (c->a == 0) {
    PORT_COVER(follower_ended);
    k->blocks[FW_TAKEN]++;
    const uint16_t left =
        (uint16_t)(wram_r16(w, (uint16_t)(page + FOLLOWER_DP_LEFT)) - 1);
    wram_w16(w, (uint16_t)(page + FOLLOWER_DP_LEFT), left);
    set_nz16(c, left);
    k->blocks[FW_ENDED]++;
    c->pc = FOLLOWER_ENDED_RTS_PC;
    return;
  }

  c->y = wram_r16(w, (uint16_t)(page + FOLLOWER_DP_SIDE));
  k->blocks[FW_SIDE]++;
  if (c->y & 0x8000u) {
    PORT_COVER(follower_other_side);
    c->a = (uint16_t)(0 - c->a);
    k->blocks[FW_OTHER]++;
  } else {
    PORT_COVER(follower_this_side);
    k->blocks[FW_TAKEN]++;
  }

  set_c(c, false);
  const uint16_t x = adc16(c, c->a, wram_r16(w, W_FOLLOWED_X));
  c->y = wram_r16(w, (uint16_t)(page + FOLLOWER_DP_RECORD));
  wram_w16(w, (uint16_t)(c->y + ACTOR_X), x);
  wram_w16(w, (uint16_t)(page + FOLLOWER_DP_X), x);
  wram_w16(w, (uint16_t)(page + FOLLOWER_DP_TILE_X), (uint16_t)(x >> 3));

  set_c(c, false);
  const uint16_t y = adc16(c, rom_word(rom, FOLLOWER_PLACES + 2 + c->x),
                           wram_r16(w, W_FOLLOWED_Y));
  wram_w16(w, (uint16_t)(c->y + ACTOR_Y), y);
  wram_w16(w, (uint16_t)(page + FOLLOWER_DP_Y), y);
  wram_w16(w, (uint16_t)(page + FOLLOWER_DP_TILE_Y), (uint16_t)(y >> 3));
  set_c(c, (y & 0x0004u) != 0);  // the last of three `LSR`s

  c->a = (uint16_t)(wram_r16(w, W_FRAME_COUNT) & FOLLOWER_LOOK_FRAMES);
  set_nz16(c, c->a);
  k->blocks[FW_PLACE]++;
  if (c->a != 0) {
    k->blocks[FW_TAKEN]++;
    c->pc = FOLLOWER_RTS_PC;
    return;
  }
  PORT_COVER(follower_looks);
  c->a = 1;
  set_nz16(c, c->a);
  k->blocks[FW_LOOK]++;
  c->pc = FOLLOWER_SLEEP_PC;
}

// ---------------------------------------------------------------------------
// $82:F4B8  a frame of the thread
// ---------------------------------------------------------------------------

static uint16_t field(const Wram* w, const PortCpu* c, uint16_t at) {
  return wram_r16(w, (uint16_t)(c->d + at));
}

static void set_field(Wram* w, const PortCpu* c, uint16_t at, uint16_t v) {
  wram_w16(w, (uint16_t)(c->d + at), v);
}

// `$82:F3CD`, and `$82:F3B7` at the end of it.
static void picture(Wram* w, const Rom* rom, PortCpu* c,
                    FollowerFrameWork* k) {
  const uint16_t record = field(w, c, FOLLOWER_DP_RECORD);
  uint16_t left = (uint16_t)(field(w, c, FOLLOWER_DP_PICTURE_LEFT) - 1);
  set_field(w, c, FOLLOWER_DP_PICTURE_LEFT, left);
  k->blocks[FF_BACK]++;
  if (!(left & 0x8000u)) {
    k->blocks[FF_TAKEN]++;
  } else {
    PORT_COVER(follower_next_picture);
    left = FOLLOWER_PICTURE_FRAMES;
    set_field(w, c, FOLLOWER_DP_PICTURE_LEFT, left);
    const uint16_t which =
        (uint16_t)((field(w, c, FOLLOWER_DP_PICTURE) + 1) & 3u);
    set_field(w, c, FOLLOWER_DP_PICTURE, which);
    set_c(c, false);  // the `ASL`
    c->y = record;
    wram_w16(w, (uint16_t)(record + ACTOR_META),
             rom_word(rom, FOLLOWER_PICTURES + (which << 1)));
    k->blocks[FF_PICTURE]++;
  }

  k->blocks[FF_BLINK]++;
  if ((left & 1u) == 0) {
    k->blocks[FF_TAKEN]++;
    return;
  }
  uint16_t flags = wram_r16(w, record);
  k->blocks[FF_TURN]++;
  if (flags & ACTOR_DRAW) {
    PORT_COVER(follower_hidden);
    k->blocks[FF_TAKEN]++;
    flags &= (uint16_t)~ACTOR_DRAW;
    k->blocks[FF_HIDE]++;
  } else {
    PORT_COVER(follower_shown);
    flags |= ACTOR_DRAW;
    k->blocks[FF_SHOW]++;
  }
  wram_w16(w, record, flags);
  k->blocks[FF_TURNED]++;
}

void follower_frame(Wram* w, const Rom* rom, PortCpu* c,
                    FollowerFrameWork* k) {
  c->a = field(w, c, FOLLOWER_DP_LEFT);
  set_nz16(c, c->a);
  k->blocks[FF_HEAD]++;
  if (c->a != 0) {
    PORT_COVER(follower_frame_ended);
    k->blocks[FF_TAKEN]++;
    c->pc = FOLLOWER_FRAME_ENDED_PC;
    return;
  }

  const uint16_t left = (uint16_t)(field(w, c, FOLLOWER_DP_PLACE_LEFT) - 1);
  set_field(w, c, FOLLOWER_DP_PLACE_LEFT, left);
  k->blocks[FF_COUNT]++;
  if (left & 0x8000u) {
    PORT_COVER(follower_moved_on);
    k->blocks[FF_TAKEN]++;
    set_c(c, false);
    set_field(w, c, FOLLOWER_DP_PLACE,
              adc16(c, FOLLOWER_PLACE_BYTES, field(w, c, FOLLOWER_DP_PLACE)));
    set_field(w, c, FOLLOWER_DP_PLACE_LEFT, FOLLOWER_PLACE_FRAMES);
    k->blocks[FF_NEXT]++;
  }

  k->blocks[FF_CALL]++;
  follower_place(w, rom, c, &k->place);
  if (c->pc == FOLLOWER_SLEEP_PC) {
    PORT_COVER(follower_frame_looks);
    push16(w, c, FOLLOWER_PLACE_RETURN);
    return;
  }

  picture(w, rom, c, k);
  c->a = field(w, c, FOLLOWER_DP_HIT);
  set_nz16(c, c->a);
  k->blocks[FF_TAIL]++;
  if (c->a != 0) {
    PORT_COVER(follower_frame_hit);
    c->pc = FOLLOWER_FRAME_HIT_PC;
    return;
  }
  k->blocks[FF_TAKEN]++;
  c->a = 1;
  set_nz16(c, c->a);
  k->blocks[FF_AGAIN]++;
  c->pc = FOLLOWER_FRAME_SLEEP_PC;
}
