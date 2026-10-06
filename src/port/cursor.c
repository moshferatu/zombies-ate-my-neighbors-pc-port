// $82:B267  a cursor a pad moves about a screen -- see port/cursor.h.

#include "port/cursor.h"

#include "port/coverage.h"
#include "port/oam.h"  // the display record's fields

static uint32_t screen(const Wram* w, uint16_t page) {
  return ((uint32_t)wram_r8(w, (uint16_t)(page + CURSOR_DP_SCREEN + 2)) << 16) |
         wram_r16(w, (uint16_t)(page + CURSOR_DP_SCREEN));
}

bool cursor_frame_supported(const Wram* w, const Rom* rom, uint16_t page) {
  const uint32_t at = screen(w, page);
  if ((at >> 16) < 0x80 || (at & 0xffffu) < 0x8000u || (at & 0xffffu) > 0xfffcu)
    return false;
  const uint16_t pad = rom_word(rom, at + 2);
  return rom_word(rom, at) < 0x0100 && (pad == 0 || pad == 2) &&
         wram_r16(w, W_CURSOR_RECORD) < 0x1f00 &&
         wram_r16(w, (uint16_t)(W_CURSOR_DIRS + pad)) <= 0x0020;
}

// `$82:B333`: put the cursor where it wants to be, or in at the far edge
// from one it has gone past.
static void put(Wram* w, const Rom* rom, PortCpu* c, CursorWork* k) {
  c->y = rom_word(rom, screen(w, c->d));
  c->x = wram_r16(w, W_CURSOR_RECORD);
  c->a = wram_r16(w, W_CURSOR_WANT_X);
  set_nz16(c, c->a);
  k->blocks[CU_PUT]++;
  bool past_left = (c->a & 0x8000u) != 0;
  if (past_left) {
    k->blocks[CU_TAKEN]++;
  } else {
    cmp16(c, c->a, CURSOR_LEFT);
    k->blocks[CU_LEFT_EQ]++;
    if (c->a == CURSOR_LEFT) {
      past_left = true;
      k->blocks[CU_TAKEN]++;
    } else {
      k->blocks[CU_BCC]++;
      if (c->a < CURSOR_LEFT) {
        past_left = true;
        k->blocks[CU_TAKEN]++;
      }
    }
  }
  if (past_left) {
    PORT_COVER(cursor_past_left);
    c->a = CURSOR_AT_RIGHT;
    k->blocks[CU_TO_RIGHT]++;
  } else {
    cmp16(c, c->a, CURSOR_RIGHT);
    k->blocks[CU_RIGHT]++;
    if (c->a >= CURSOR_RIGHT) {
      PORT_COVER(cursor_past_right);
      k->blocks[CU_TAKEN]++;
      c->a = CURSOR_AT_LEFT;
      k->blocks[CU_TO_LEFT]++;
    } else {
      k->blocks[CU_BRA]++;
    }
  }
  wram_w16(w, (uint16_t)(c->x + ACTOR_X), c->a);

  c->a = wram_r16(w, W_CURSOR_WANT_Y);
  cmp16(c, c->a, CURSOR_TOP);
  k->blocks[CU_PUT_X]++;
  bool past_top = c->a == CURSOR_TOP;
  if (past_top) {
    k->blocks[CU_TAKEN]++;
  } else {
    k->blocks[CU_BCC]++;
    if (c->a < CURSOR_TOP) {
      past_top = true;
      k->blocks[CU_TAKEN]++;
    }
  }
  const uint16_t bottom = rom_word(rom, CURSOR_BOTTOMS + c->y);
  if (past_top) {
    PORT_COVER(cursor_past_top);
    set_c(c, true);
    c->a = sbc16(c, bottom, CURSOR_ROW);
    k->blocks[CU_TO_BOTTOM]++;
  } else {
    cmp16(c, c->a, bottom);
    k->blocks[CU_BOTTOM]++;
    if (c->a >= bottom) {
      PORT_COVER(cursor_past_bottom);
      k->blocks[CU_TAKEN]++;
      c->a = CURSOR_AT_TOP;
      k->blocks[CU_TO_TOP]++;
    } else {
      k->blocks[CU_BRA]++;
    }
  }
  wram_w16(w, (uint16_t)(c->x + ACTOR_Y), c->a);
  k->blocks[CU_PUT_Y]++;
}

// The pad, from the sleep's return. False where the ROM goes on, with the
// program counter set.
static bool read_pad(Wram* w, const Rom* rom, PortCpu* c, CursorWork* k) {
  c->x = wram_r16(w, W_CURSOR_RECORD);
  c->y = rom_word(rom, screen(w, c->d) + 2);
  c->a = wram_r16(w, (uint16_t)(W_CURSOR_PADS + c->y));
  set_nz16(c, c->a);
  c->p = (uint8_t)(c->p & ~PORT_P_Z);  // `BIT #`: Z and nothing else
  if ((c->a & CURSOR_PAD_START) == 0) c->p |= PORT_P_Z;
  k->blocks[CU_HEAD]++;
  if (c->a & CURSOR_PAD_START) {
    PORT_COVER(cursor_start);
    const uint16_t start = (uint16_t)(wram_r16(w, W_CURSOR_START) + 1);
    wram_w16(w, W_CURSOR_START, start);
    set_nz16(c, start);
    k->blocks[CU_START]++;
    return true;
  }
  k->blocks[CU_TAKEN]++;

  c->a &= CURSOR_PAD_BUTTONS;
  set_nz16(c, c->a);
  k->blocks[CU_BUTTONS]++;
  if (c->a != 0) {
    c->a = wram_r16(w, W_CURSOR_BUTTON);
    set_nz16(c, c->a);
    k->blocks[CU_HELD]++;
    if (c->a == 0) {
      PORT_COVER(cursor_button);
      c->pc = CURSOR_BUTTON_PC;
      return false;
    }
    PORT_COVER(cursor_button_held);
    k->blocks[CU_TAKEN]++;
    return true;
  }
  k->blocks[CU_TAKEN]++;

  wram_w16(w, W_CURSOR_BUTTON, 0);
  c->a = wram_r16(w, (uint16_t)(W_CURSOR_DIRS + c->y));
  const uint16_t last = wram_r16(w, W_CURSOR_DIR);
  cmp16(c, c->a, last);
  k->blocks[CU_DIR]++;
  if (c->a == last) {
    PORT_COVER(cursor_same);
    k->blocks[CU_TAKEN]++;
    return true;
  }

  wram_w16(w, W_CURSOR_DIR, c->a);
  c->y = asl16(c, c->a);
  set_c(c, false);
  wram_w16(w, W_CURSOR_WANT_X,
           adc16(c, rom_word(rom, CURSOR_STEPS + c->y),
                 wram_r16(w, (uint16_t)(c->x + ACTOR_X))));
  set_c(c, false);
  wram_w16(w, W_CURSOR_WANT_Y,
           adc16(c, rom_word(rom, CURSOR_STEPS + 2 + c->y),
                 wram_r16(w, (uint16_t)(c->x + ACTOR_Y))));
  k->blocks[CU_MOVE]++;
  put(w, rom, c, k);
  c->a = wram_r16(w, W_CURSOR_DIR);
  set_nz16(c, c->a);
  k->blocks[CU_MOVED]++;
  if (c->a != 0) {
    PORT_COVER(cursor_moved);
    c->pc = CURSOR_MOVED_PC;
    return false;
  }
  PORT_COVER(cursor_let_go);
  k->blocks[CU_TAKEN]++;
  return true;
}

void cursor_frame(Wram* w, const Rom* rom, PortCpu* c, CursorWork* k) {
  if (!read_pad(w, rom, c, k)) return;

  c->a = (uint16_t)(wram_r16(w, W_CURSOR_TIME) - 1);
  set_nz16(c, c->a);
  wram_w16(w, W_CURSOR_TIME, c->a);
  k->blocks[CU_TIME]++;
  if (c->a & 0x8000u) {
    PORT_COVER(cursor_time_up);
    k->blocks[CU_TAKEN]++;
    c->pc = CURSOR_DONE_PC;
    return;
  }
  c->a = wram_r16(w, W_CURSOR_START);
  set_nz16(c, c->a);
  k->blocks[CU_FLAG]++;
  if (c->a != 0) {
    k->blocks[CU_TAKEN]++;
    c->pc = CURSOR_DONE_PC;
    return;
  }
  c->a = 4;
  set_nz16(c, c->a);
  k->blocks[CU_AGAIN]++;
  c->pc = CURSOR_SLEEP_PC;
}
