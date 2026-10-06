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
