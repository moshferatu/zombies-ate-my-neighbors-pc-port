// $83:8102 to $83:8228  the logo screens, between their waits -- see
// port/logo.h.

#include "port/logo.h"

#include <stdbool.h>

#include "port/coverage.h"

static void lda(PortCpu* c, uint16_t v) {
  c->a = v;
  set_nz16(c, v);
}

static uint16_t colour(const Wram* w, uint16_t at) {
  return wram_r16(w, (uint32_t)LOGO_COLOURS + at);
}

static void set_colour(Wram* w, uint16_t at, uint16_t v) {
  wram_w16(w, (uint32_t)LOGO_COLOURS + at, v);
}

// A word of page zero taken down one, as `DEC` leaves the flags.
static uint16_t dec(Wram* w, PortCpu* c, uint16_t at) {
  const uint16_t v = (uint16_t)(wram_r16(w, at) - 1);
  wram_w16(w, at, v);
  set_nz16(c, v);
  return v;
}

// Each colour of a row takes the next one's value, in `rows` rows `$20`
// apart: X counts up by two from `first` until it is past `last`.
static void move_down(Wram* w, PortCpu* c, uint16_t first, uint16_t to,
                      uint16_t last, int rows, int block, LogoWork* k) {
  c->x = first;
  do {
    for (int row = 0; row < rows; row++) {
      const uint16_t at = (uint16_t)(to + 0x20 * row + c->x);
      lda(c, colour(w, (uint16_t)(at + 2)));
      set_colour(w, at, c->a);
    }
    c->x = (uint16_t)(c->x + 2);
    cmp16(c, c->x, last);
    k->blocks[block]++;
    if (c->x < last) k->blocks[LG_TAKEN]++;
  } while (c->x < last);
}

void logo_frame(Wram* w, PortCpu* c, LogoStage stage, LogoWork* k) {
  const uint16_t page = c->d;
  // `JSR $8254 : RTS`, whose return address is left under the stack.
  const uint16_t s = c->s;
  push16(w, c, (uint16_t)(c->pc + 2));
  c->s = s;
  k->blocks[LG_RTS]++;

  switch (stage) {
    case LOGO_SLIDE: {
      const uint16_t other =
          (uint16_t)(wram_r16(w, (uint16_t)(page + LOGO_DP_OTHER)) ^ 0xffffu);
      wram_w16(w, (uint16_t)(page + LOGO_DP_OTHER), other);
      k->blocks[LG_1A]++;
      if (other == 0) {
        k->blocks[LG_TAKEN]++;
      } else {
        uint16_t tile =
            (uint16_t)(wram_r16(w, (uint16_t)(page + LOGO_DP_TILE)) + 2);
        wram_w16(w, (uint16_t)(page + LOGO_DP_TILE), tile);
        k->blocks[LG_1B]++;
        if (tile < 0x000d) {
          PORT_COVER(logo_tile_stepped);
          k->blocks[LG_TAKEN]++;
        } else {
          PORT_COVER(logo_tile_wrapped);
          wram_w16(w, (uint16_t)(page + LOGO_DP_TILE), 6);
          k->blocks[LG_1C]++;
        }
      }
      set_c(c, true);
      lda(c, sbc16(c, wram_r16(w, W_LOGO_SCROLL_X), 8));
      wram_w16(w, W_LOGO_SCROLL_X, c->a);
      k->blocks[LG_1D]++;
      if (c->a != 0) {
        k->blocks[LG_TAKEN]++;
        c->pc = LOGO_SLIDE_WAI_PC;
        return;
      }
      PORT_COVER(logo_slid);
      wram_w16(w, W_LOGO_SCROLL_2_X, 0);
      wram_w16(w, (uint16_t)(page + LOGO_DP_FRAMES), 0);
      lda(c, 3);
      wram_w16(w, (uint16_t)(page + LOGO_DP_TIMES), 3);
      k->blocks[LG_1E]++;
      c->pc = LOGO_CYCLE_WAI_PC;
      return;
    }
    case LOGO_CYCLE: {
      const uint16_t frames =
          (uint16_t)(wram_r16(w, (uint16_t)(page + LOGO_DP_FRAMES)) + 1);
      wram_w16(w, (uint16_t)(page + LOGO_DP_FRAMES), frames);
      c->a = frames;
      cmp16(c, frames, 4);
      k->blocks[LG_2A]++;
      if (frames != 4) {
        k->blocks[LG_TAKEN]++;
        c->pc = LOGO_CYCLE_WAI_PC;
        return;
      }
      PORT_COVER(logo_cycled);
      wram_w16(w, (uint16_t)(page + LOGO_DP_FRAMES), 0);
      k->blocks[LG_2B]++;
      move_down(w, c, 2, 0x0060, 0x0021, 1, LG_2W, k);
      const uint16_t times = dec(w, c, (uint16_t)(page + LOGO_DP_TIMES));
      k->blocks[LG_2C]++;
      if (times != 0) {
        k->blocks[LG_TAKEN]++;
        c->pc = LOGO_CYCLE_WAI_PC;
      } else {
        c->pc = LOGO_RISE_WAI_PC;
      }
      return;
    }
    case LOGO_RISE: {
      lda(c, (uint16_t)(wram_r16(w, W_LOGO_SCROLL_Y) - 2));
      wram_w16(w, W_LOGO_SCROLL_Y, c->a);
      cmp16(c, c->a, 0x0300);
      k->blocks[LG_3A]++;
      if (c->a != 0x0300) {
        k->blocks[LG_TAKEN]++;
        c->pc = LOGO_RISE_WAI_PC;
        return;
      }
      PORT_COVER(logo_risen);
      lda(c, 0x0031);
      k->blocks[LG_3B]++;
      c->pc = LOGO_RISE_END_PC;
      return;
    }
    case LOGO_SWEEP: {
      // Each row's first colour is kept on the stack for its end.
      const uint16_t upper = colour(w, 0x002c);
      const uint16_t lower = colour(w, 0x004c);
      push16(w, c, upper);
      push16(w, c, lower);
      c->s = s;
      k->blocks[LG_4A]++;
      move_down(w, c, 0x000c, 0x0020, 0x001f, 2, LG_4W, k);
      set_colour(w, 0x005e, lower);
      set_colour(w, 0x003e, upper);
      c->a = upper;
      const uint16_t times = dec(w, c, (uint16_t)(page + LOGO_DP_TIMES));
      k->blocks[LG_4C]++;
      if (times != 0) {
        k->blocks[LG_TAKEN]++;
        c->pc = LOGO_SWEEP_WAI_PC;
      } else {
        PORT_COVER(logo_swept);
        c->pc = LOGO_SWEEP_END_PC;
      }
      return;
    }
    case LOGO_SWEEP_2: {
      const uint16_t first = colour(w, 0x000c);
      push16(w, c, first);
      c->s = s;
      k->blocks[LG_5A]++;
      move_down(w, c, 0x000c, 0x0000, 0x001f, 1, LG_5W, k);
      set_colour(w, 0x001e, first);
      c->a = first;
      const uint16_t times = dec(w, c, (uint16_t)(page + LOGO_DP_TIMES));
      k->blocks[LG_5C]++;
      if (times != 0) {
        k->blocks[LG_TAKEN]++;
        c->pc = LOGO_SWEEP_2_WAI_PC;
      } else {
        PORT_COVER(logo_swept_2);
        wram_w16(w, (uint16_t)(page + LOGO_DP_STEP), 0);
        k->blocks[LG_5D]++;
        c->pc = LOGO_FLASH_WAI_PC;
      }
      return;
    }
    case LOGO_FLASH: {
      // The step in each of a colour's three fives of bits. The ROM swaps
      // the step's bytes and shifts: two up for the top five, three down
      // for the middle.
      const uint16_t step =
          (uint16_t)(wram_r16(w, (uint16_t)(page + LOGO_DP_STEP)) + 1);
      wram_w16(w, (uint16_t)(page + LOGO_DP_STEP), step);
      const uint16_t swapped = (uint16_t)(step << 8 | step >> 8);
      const uint16_t top = (uint16_t)(swapped << 2);
      wram_w16(w, (uint16_t)(page + LOGO_DP_SCRATCH), top);
      set_colour(w, 0, (uint16_t)(swapped >> 3 | top | step));
      c->a = step;
      cmp16(c, step, 0x001f);
      k->blocks[LG_6A]++;
      if (step != 0x001f) {
        k->blocks[LG_TAKEN]++;
        c->pc = LOGO_FLASH_WAI_PC;
        return;
      }
      PORT_COVER(logo_flashed);
      c->x = 0x00ff;
      set_nz16(c, c->x);
      k->blocks[LG_6B]++;
      c->pc = LOGO_HOLD_WAI_PC;
      return;
    }
    case LOGO_HOLD: {
      c->x = (uint16_t)(c->x - 1);
      set_nz16(c, c->x);
      k->blocks[LG_7A]++;
      if (c->x != 0) {
        k->blocks[LG_TAKEN]++;
        c->pc = LOGO_HOLD_WAI_PC;
      } else {
        PORT_COVER(logo_held);
        c->pc = LOGO_FADE_WAI_PC;
      }
      return;
    }
    default: {
      lda(c, dec(w, c, W_LOGO_BRIGHTNESS));
      k->blocks[LG_8A]++;
      if (c->a != 0) {
        k->blocks[LG_TAKEN]++;
        c->pc = LOGO_FADE_WAI_PC;
      } else {
        PORT_COVER(logo_faded);
        c->pc = LOGO_FADE_END_PC;
      }
      return;
    }
  }
}
