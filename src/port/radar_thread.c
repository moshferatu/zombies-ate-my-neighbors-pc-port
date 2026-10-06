// $82:D8FD  the radar's thread, a frame of it -- see port/radar_thread.h.

#include "port/radar_thread.h"

#include "port/coverage.h"
#include "port/oam.h"  // the display record's fields

static uint16_t field(const Wram* w, uint16_t page, uint16_t at) {
  return wram_r16(w, (uint16_t)(page + at));
}

static void set_field(Wram* w, uint16_t page, uint16_t at, uint16_t v) {
  wram_w16(w, (uint16_t)(page + at), v);
}

static void lda(PortCpu* c, uint16_t v) {
  c->a = v;
  set_nz16(c, v);
}

bool radar_frame_supported(const Wram* w, uint16_t page) {
  const uint16_t player = field(w, page, RADAR_DP_PLAYER);
  if (player != 0 && player != 2) return false;
  return field(w, page, RADAR_DP_SQUARE) < 0x1f00 &&
         wram_r16(w, W_PLAYER_A_RECORD + player) < 0x1f00;
}

// How far apart on one axis, as the ROM has it: the distance, whether it had
// to be negated, and carry from the compare with the radar's reach.
static uint16_t apart(PortCpu* c, uint16_t player, uint16_t neighbour,
                      bool* negated, RadarWork* k) {
  set_c(c, true);
  uint16_t d = sbc16(c, player, neighbour);
  *negated = (d & 0x8000u) != 0;
  if (*negated) {
    d = (uint16_t)(0u - d);
    k->blocks[RD_NEGATE]++;
  } else {
    k->blocks[RD_TAKEN]++;
  }
  cmp16(c, d, RADAR_REACH);
  k->blocks[RD_FAR]++;
  return d;
}

// `LSR` four times: a sixteenth, and carry the last bit out.
static uint16_t sixteenth(PortCpu* c, uint16_t v) {
  set_c(c, (v & 0x0008u) != 0);
  return (uint16_t)(v >> 4);
}

// The square's place on one axis: the panel's middle, less the distance or
// plus it.
static uint16_t place(PortCpu* c, uint16_t middle, uint16_t by, bool plus,
                      RadarWork* k) {
  if (plus) {
    k->blocks[RD_TAKEN]++;
    k->blocks[RD_ADD]++;
    set_c(c, false);
    return adc16(c, middle, by);
  }
  k->blocks[RD_SUB]++;
  k->blocks[RD_TAKEN]++;  // its `BRA`
  set_c(c, true);
  return sbc16(c, middle, by);
}

static void doze(PortCpu* c, RadarWork* k) {
  lda(c, RADAR_YIELD_TICKS);
  k->blocks[RD_YIELD]++;
  c->pc = RADAR_YIELD_PC;
}

void radar_frame(Wram* w, PortCpu* c, RadarWork* k) {
  const uint16_t page = c->d;
  c->x = field(w, page, RADAR_DP_PLAYER);
  lda(c, wram_r16(w, (uint16_t)(W_RADAR_UP + c->x)));
  k->blocks[RD_HEAD]++;
  if (c->a == 0) {
    PORT_COVER(radar_went_down);
    k->blocks[RD_DOWN]++;
    c->pc = RADAR_DOWN_PC;
    return;
  }
  k->blocks[RD_TAKEN]++;

  lda(c, wram_r16(w, W_RADAR_NEIGHBOURS_LEFT));
  cmp16(c, c->a, field(w, page, RADAR_DP_LEFT));
  k->blocks[RD_SAME]++;
  if (c->a != field(w, page, RADAR_DP_LEFT)) {
    PORT_COVER(radar_recount);
    c->pc = RADAR_RECOUNT_PC;
    return;
  }
  k->blocks[RD_TAKEN]++;

  lda(c, wram_r16(w, W_VICTIM_COUNT));
  set_field(w, page, RADAR_DP_TRIES, c->a);
  k->blocks[RD_COUNT]++;

  for (;;) {
    const uint16_t tries = (uint16_t)(field(w, page, RADAR_DP_TRIES) - 1);
    set_field(w, page, RADAR_DP_TRIES, tries);
    set_nz16(c, tries);
    k->blocks[RD_TRY]++;
    if (tries & 0x8000u) {
      // Round the whole list and none near: off the screen with it.
      PORT_COVER(radar_none_near);
      c->y = field(w, page, RADAR_DP_SQUARE);
      lda(c, RADAR_NOWHERE);
      wram_w16(w, (uint16_t)(c->y + ACTOR_X), RADAR_NOWHERE);
      wram_w16(w, (uint16_t)(c->y + ACTOR_Y), RADAR_NOWHERE);
      k->blocks[RD_NONE]++;
      k->blocks[RD_TAKEN]++;  // its `BRA`
      break;
    }
    k->blocks[RD_TAKEN]++;

    // The next in the list, and round to the first after the last.
    uint16_t shown = (uint16_t)(field(w, page, RADAR_DP_SHOWN) + 1);
    const uint16_t count = wram_r16(w, W_VICTIM_COUNT);
    cmp16(c, shown, count);
    k->blocks[RD_PICK]++;
    if (shown == count) {
      k->blocks[RD_TAKEN]++;
    } else {
      k->blocks[RD_PICK_BCC]++;
      if (shown < count) {
        k->blocks[RD_TAKEN]++;
      } else {
        PORT_COVER(radar_wrapped);
        shown = 1;
        k->blocks[RD_PICK_WRAP]++;
      }
    }
    set_field(w, page, RADAR_DP_SHOWN, shown);
    c->x = (uint16_t)(shown - 1);
    lda(c, wram_r16(w, (uint32_t)W_VICTIM_SPAWNED + c->x) &
               RADAR_NEIGHBOUR_GONE);
    k->blocks[RD_FLAG]++;
    if (c->a != 0) {
      PORT_COVER(radar_skipped_gone);
      k->blocks[RD_TAKEN]++;
      continue;
    }

    c->x = (uint16_t)(c->x << 2);
    set_field(w, page, RADAR_DP_RIGHT, 0);
    set_field(w, page, RADAR_DP_BELOW, 0);
    c->y = wram_r16(w, (uint16_t)(W_PLAYER_A_RECORD +
                                  field(w, page, RADAR_DP_PLAYER)));
    bool right, below;
    k->blocks[RD_DX]++;
    c->a = apart(c, wram_r16(w, (uint16_t)(c->y + ACTOR_X)),
                 wram_r16(w, (uint32_t)W_VICTIM_X + c->x), &right, k);
    if (right) set_field(w, page, RADAR_DP_RIGHT, 1);
    if (c->a >= RADAR_REACH) {
      PORT_COVER(radar_skipped_far_x);
      k->blocks[RD_TAKEN]++;
      continue;
    }
    c->a = sixteenth(c, c->a);
    set_field(w, page, RADAR_DP_ACROSS, c->a);
    k->blocks[RD_DY]++;
    c->a = apart(c, wram_r16(w, (uint16_t)(c->y + ACTOR_Y)),
                 wram_r16(w, (uint32_t)W_VICTIM_Y + c->x), &below, k);
    if (below) set_field(w, page, RADAR_DP_BELOW, 1);
    if (c->a >= RADAR_REACH) {
      PORT_COVER(radar_skipped_far_y);
      k->blocks[RD_TAKEN]++;
      continue;
    }
    c->a = sixteenth(c, c->a);
    set_field(w, page, RADAR_DP_DOWN, c->a);
    k->blocks[RD_PUT_HEAD]++;

    PORT_COVER(radar_shown);
    c->a = place(c, field(w, page, RADAR_DP_MIDDLE_X),
                 field(w, page, RADAR_DP_ACROSS), right, k);
    c->y = field(w, page, RADAR_DP_SQUARE);
    wram_w16(w, (uint16_t)(c->y + ACTOR_X), c->a);
    k->blocks[RD_PUT_MID]++;
    c->a = place(c, field(w, page, RADAR_DP_MIDDLE_Y),
                 field(w, page, RADAR_DP_DOWN), below, k);
    wram_w16(w, (uint16_t)(c->y + ACTOR_Y), c->a);
    k->blocks[RD_PUT_TAIL]++;
    break;
  }
  doze(c, k);
}
