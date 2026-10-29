// $82:B267  a cursor a pad moves about a screen -- see port/cursor.h.

#include "port/cursor.h"

#include "port/coverage.h"
#include "port/frontend.h"  // the backdrop's slide
#include "port/oam.h"  // the display record's fields
#include "port/password_check.h"
#include "port/text.h"

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
    c->a = CURSOR_MOVE_SOUND;
    set_nz16(c, c->a);
    k->blocks[CU_SOUND]++;
    c->pc = CURSOR_MOVED_PC;
    return false;
  }
  PORT_COVER(cursor_let_go);
  k->blocks[CU_TAKEN]++;
  return true;
}

// What every turn ends with: one off the clock, and round again unless it
// has run out or the entry is over.
static void count_turn(Wram* w, PortCpu* c, CursorWork* k) {
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

void cursor_frame(Wram* w, const Rom* rom, PortCpu* c, CursorWork* k) {
  if (read_pad(w, rom, c, k)) count_turn(w, c, k);
}

void cursor_after(Wram* w, PortCpu* c, bool picked, PickWork* k) {
  if (picked) {
    PORT_COVER(cursor_after_pick);
    k->turn.blocks[CU_BRA]++;  // over the move
  } else {
    PORT_COVER(cursor_after_move);
  }
  wram_w16(w, W_CURSOR_TIME, CURSOR_TURNS);
  k->blocks[PK_CLOCK]++;
  count_turn(w, c, &k->turn);
}

// ---------------------------------------------------------------------------
// $82:B3F6  the character under the cursor
// ---------------------------------------------------------------------------

typedef struct {
  uint16_t last;    // the last place of the entry
  uint16_t grid;
  uint16_t column;
  uint16_t row;     // doubled: an index of words
  uint16_t at;      // the cursor's place in the grid
} Pick;

static bool pick_at(const Wram* w, const Rom* rom, uint16_t page, Pick* p) {
  const uint32_t at = screen(w, page);
  if ((at >> 16) < 0x80 || (at & 0xffffu) < 0x8000u || (at & 0xffffu) > 0xfffcu)
    return false;
  const uint16_t which = rom_word(rom, at);
  const uint16_t record = wram_r16(w, W_CURSOR_RECORD);
  if (which > 4 || (which & 1) || record >= 0x1f00) return false;
  p->last = (uint16_t)(rom_word(rom, CURSOR_LENGTHS + which) - 1);
  p->grid = rom_word(rom, CURSOR_GRIDS + which);
  p->column = (uint16_t)((uint16_t)(wram_r16(w, (uint16_t)(record + ACTOR_X)) -
                                    CURSOR_AT_LEFT) >>
                         CURSOR_COLUMN_SHIFT);
  p->row = (uint16_t)((uint16_t)(wram_r16(w, (uint16_t)(record + ACTOR_Y)) -
                                 CURSOR_AT_TOP) >>
                      CURSOR_ROW_SHIFT);
  if (p->column >= CURSOR_GRID_COLUMNS || (p->row & 1) ||
      p->row >= 2 * CURSOR_GRID_ROW_COUNT)
    return false;
  p->at = (uint16_t)(rom_word(rom, CURSOR_GRID_ROWS + p->row) + p->column);
  return p->grid >= 0x8000u && p->grid <= 0xffffu - p->at;
}

static uint8_t grid_char(const Wram* w, const Rom* rom, uint8_t db,
                         const Pick* p) {
  return bus_r8(w, rom, ((uint32_t)db << 16) | (uint16_t)(p->grid + p->at));
}

bool cursor_pick_supported(const Wram* w, const Rom* rom, uint8_t db,
                           uint16_t page) {
  Pick p;
  return pick_at(w, rom, page, &p) && p.last < CURSOR_ENTRY_MAX &&
         wram_r16(w, W_CURSOR_ENTRY_AT) <= p.last &&
         grid_char(w, rom, db, &p) != CURSOR_CHAR_BACK;
}

bool cursor_pick(Wram* w, const Rom* rom, PortCpu* c, PickWork* k) {
  Pick p;
  pick_at(w, rom, c->d, &p);
  wram_w16(w, W_CURSOR_ENTRY_LAST, p.last);
  wram_w16(w, (uint16_t)(c->d + CURSOR_DP_GRID), p.grid);
  wram_w16(w, W_CURSOR_BUTTON, CURSOR_PAD_BUTTONS);
  wram_w16(w, W_CURSOR_COLUMN, p.column);
  set_c(c, false);
  c->y = adc16(c, rom_word(rom, CURSOR_GRID_ROWS + p.row), p.column);
  uint8_t ch = grid_char(w, rom, c->db, &p);
  c->a = (uint16_t)((c->y & 0xff00u) | ch);
  set_c(c, ch >= CURSOR_CHAR_END);
  set_nz8(c, (uint8_t)(ch - CURSOR_CHAR_END));
  k->blocks[PK_HEAD]++;
  if (ch == CURSOR_CHAR_END) {
    PORT_COVER(cursor_pick_end);
    k->blocks[PK_TAKEN]++;
    const uint16_t start = (uint16_t)(wram_r16(w, W_CURSOR_START) - 1);
    wram_w16(w, W_CURSOR_START, start);
    set_nz16(c, start);
    k->blocks[PK_END]++;
    c->pc = CURSOR_PICK_RTS_PC;
    return true;
  }

  k->blocks[PK_SPACE_TEST]++;
  if (ch == CURSOR_CHAR_SPACE) {
    PORT_COVER(cursor_pick_space);
    ch = CURSOR_SPACE;
    k->blocks[PK_SPACE]++;
  } else {
    k->blocks[PK_TAKEN] += 2;  // that `BNE`, and the one of the next test
    k->blocks[PK_BACK_TEST]++;
  }

  c->x = wram_r16(w, W_CURSOR_ENTRY_AT);
  wram_w8(w, (uint16_t)(W_CURSOR_ENTRY + c->x), ch);
  cmp16(c, c->x, p.last);
  k->blocks[PK_STORE]++;
  if (c->x >= p.last) {
    PORT_COVER(cursor_pick_last);
    k->blocks[PK_TAKEN]++;
  } else {
    PORT_COVER(cursor_pick_letter);
    c->x++;
    wram_w16(w, W_CURSOR_ENTRY_AT, c->x);
    k->blocks[PK_ADVANCE]++;
  }
  c->a = CURSOR_PICK_SOUND;
  set_nz16(c, c->a);
  k->blocks[PK_SOUND]++;
  c->pc = CURSOR_PICK_SOUND_PC;
  return false;
}

// ---------------------------------------------------------------------------
// The rest of the screen, between its calls
// ---------------------------------------------------------------------------

#define TABLE_SCREENS 0x10u  // more than the tables have

static bool pointer_in_cartridge(uint32_t at) {
  return (at >> 16) >= 0x80 && (at & 0xffffu) >= 0x8000u &&
         (at & 0xffffu) <= 0xfff8u;
}

bool cursor_screen_known(const Wram* w, const Rom* rom, uint16_t page) {
  const uint32_t at = screen(w, page);
  if (!pointer_in_cartridge(at)) return false;
  const uint16_t which = rom_word(rom, at);
  // The second line is the player's, by the table's pad.
  if (which == CURSOR_SCREEN_TWO_LINES) {
    const uint16_t pad = rom_word(rom, at + 2);
    return pad == 0 || pad == 2;
  }
  return which < TABLE_SCREENS;
}

static uint32_t check_routine(const Wram* w, const Rom* rom, uint16_t page) {
  const uint32_t at = screen(w, page);
  return ((uint32_t)(rom_word(rom, at + 6) & 0xffu) << 16) |
         rom_word(rom, at + 4);
}

bool cursor_check_known(const Wram* w, const Rom* rom, uint16_t page) {
  return pointer_in_cartridge(screen(w, page)) &&
         (cursor_check_none(w, rom, page) ||
          check_routine(w, rom, page) == PASSWORD_CHECK_PC);
}

bool cursor_check_none(const Wram* w, const Rom* rom, uint16_t page) {
  return rom_word(rom, screen(w, page) + 4) == 0;
}

bool cursor_needs_met(CursorNeeds needs, const Wram* w, const Rom* rom,
                      const PortCpu* c) {
  switch (needs) {
    case CURSOR_NEEDS_NOTHING:
      return true;
    case CURSOR_NEEDS_PAD:
      return c->x == 0 || c->x == 2;
    case CURSOR_NEEDS_SCREEN:
      return cursor_screen_known(w, rom, c->d);
    case CURSOR_NEEDS_FLASH:
      return wram_r16(w, W_CURSOR_FLASH_SCREEN) < TABLE_SCREENS;
    case CURSOR_NEEDS_RECORD:
      return c->a < 0x1f00u;
  }
  return false;
}

static void load_a(PortCpu* c, uint16_t v) {
  c->a = v;
  set_nz16(c, v);
}

static void load_word(PortCpu* c, uint16_t v, uint32_t pc) {
  load_a(c, v);
  c->pc = pc;
}

static void load_from(const Wram* w, PortCpu* c, uint16_t at, uint32_t pc) {
  load_word(c, wram_r16(w, at), pc);
}

// A job and its bank, for the queue.
static void job_ask(PortCpu* c, uint16_t job, uint16_t bank, uint32_t pc) {
  c->a = job;
  c->y = bank;
  set_nz16(c, bank);
  c->pc = pc;
}

static void xba(PortCpu* c) {
  c->a = (uint16_t)((c->a << 8) | (c->a >> 8));
  set_nz8(c, (uint8_t)c->a);
}

// For a password: the table for the pad in X, with its bank.
void cursor_password_screen_ask(Wram* w, const Rom* rom, PortCpu* c) {
  (void)w;
  PORT_COVER(password_screen_ask);
  c->y = rom_word(rom, PASSWORD_SCREENS + c->x);
  load_word(c, CURSOR_BANK, 0x82affbu);
}

// The data bank kept and set to this one, and the table's address made a far
// pointer.
void cursor_screen_begin(Wram* w, const Rom* rom, PortCpu* c) {
  (void)rom;
  PORT_COVER(cursor_screen_begin);
  push8(w, c, c->db);
  push16(w, c, CURSOR_BANK);
  c->db = pull8(w, c);
  xba(c);
  wram_w16(w, (uint16_t)(c->d + CURSOR_DP_SCREEN + 1), c->a);
  wram_w16(w, (uint16_t)(c->d + CURSOR_DP_SCREEN), c->y);
  c->pc = 0x82b23eu;
}

void cursor_screen_dim(Wram* w, const Rom* rom, PortCpu* c) {
  (void)rom;
  PORT_COVER(cursor_screen_dim);
  load_a(c, wram_r16(w, W_BRIGHTNESS_SHADOW) | BRIGHTNESS_DARK);
  wram_w16(w, W_BRIGHTNESS_SHADOW, c->a);
  c->pc = 0x82b4c0u;
}

// `PLA`, the word the last send was given, and which of the backdrop's
// colours.
void cursor_screen_backdrop_ask(Wram* w, const Rom* rom, PortCpu* c) {
  (void)rom;
  PORT_COVER(cursor_screen_backdrop_ask);
  pull16(w, c);
  load_word(c, CURSOR_SCREEN_BACKDROP, 0x82b51cu);
}

void cursor_backdrop_colours_ask(Wram* w, const Rom* rom, PortCpu* c) {
  (void)w;
  (void)rom;
  PORT_COVER(backdrop_colours_ask);
  set_c(c, false);
  c->x = adc16(c, c->a, BACKDROP_COLOURS);
  c->a = BACKDROP_COLOURS_BANK;
  c->y = BACKDROP_COLOURS_BYTES;
  set_nz16(c, c->y);
  c->pc = 0x82b1b2u;
}

void cursor_backdrop_slide_ask(Wram* w, const Rom* rom, PortCpu* c) {
  (void)rom;
  PORT_COVER(backdrop_slide_ask);
  pull16(w, c);
  job_ask(c, BACKDROP_SLIDE_JOB, CURSOR_BANK, 0x82b1eeu);
}

// Where the queue put the job, from X.
void cursor_backdrop_slide_kept(Wram* w, const Rom* rom, PortCpu* c) {
  (void)rom;
  PORT_COVER(backdrop_slide_kept);
  wram_w16(w, W_BACKDROP_SLIDE_COUNT, 0);
  wram_w16(w, W_BACKDROP_SLIDE_JOB, c->x);
  c->pc = 0x82b1f8u;
}

void cursor_backdrop_slide_job_ask(Wram* w, const Rom* rom, PortCpu* c) {
  (void)rom;
  PORT_COVER(backdrop_slide_job_ask);
  load_from(w, c, W_BACKDROP_SLIDE_JOB, 0x82b22cu);
}

// What the printer of several lines is called with: the screen's words, and
// where its entry goes.
void cursor_screen_words_ask(Wram* w, const Rom* rom, PortCpu* c) {
  PORT_COVER(cursor_screen_words_ask);
  const uint16_t which = rom_word(rom, screen(w, c->d));
  c->x = rom_word(rom, CURSOR_SCREEN_WORDS + which);
  c->y = rom_word(rom, CURSOR_SCREEN_PLACES + which);
  load_word(c, CURSOR_BANK, 0x82b533u);
}

static void shown(Wram* w, PortCpu* c) {
  wram_w16(w, W_BRIGHTNESS_SHADOW, 0);
  c->pc = 0x82b558u;
}

// Not the screen with a second line, so shown: black, but no longer blank.
// That screen prints the player's line first: the words by the table's pad,
// where its entry goes.
bool cursor_screen_one_line(Wram* w, const Rom* rom, PortCpu* c) {
  const uint32_t at = screen(w, c->d);
  c->a = rom_word(rom, at);
  cmp16(c, c->a, CURSOR_SCREEN_TWO_LINES);
  if (c->a != CURSOR_SCREEN_TWO_LINES) {
    PORT_COVER(cursor_screen_one_line);
    shown(w, c);
    return false;
  }
  PORT_COVER(cursor_screen_two_lines);
  c->y = rom_word(rom, CURSOR_SCREEN_PLACES + c->a);
  c->x = rom_word(rom, CURSOR_SECOND_LINES + rom_word(rom, at + 2));
  load_word(c, CURSOR_BANK, CURSOR_SECOND_LINE_PC);
  return true;
}

void cursor_screen_shown(Wram* w, const Rom* rom, PortCpu* c) {
  (void)rom;
  PORT_COVER(cursor_screen_shown);
  shown(w, c);
}

void cursor_screen_undim(Wram* w, const Rom* rom, PortCpu* c) {
  (void)rom;
  PORT_COVER(cursor_screen_undim);
  load_a(c, wram_r16(w, W_BRIGHTNESS_SHADOW) & (BRIGHTNESS_DARK - 1u));
  wram_w16(w, W_BRIGHTNESS_SHADOW, c->a);
  c->pc = 0x82b562u;
}

void cursor_sprites_ask(Wram* w, const Rom* rom, PortCpu* c) {
  (void)w;
  (void)rom;
  PORT_COVER(cursor_sprites_ask);
  job_ask(c, CURSOR_SPRITES_JOB, CURSOR_SPRITES_JOB_BANK, 0x82b58eu);
}

void cursor_sprites_job_kept(Wram* w, const Rom* rom, PortCpu* c) {
  (void)rom;
  PORT_COVER(cursor_sprites_job_kept);
  wram_w16(w, W_CURSOR_SPRITES_JOB, c->x);
  load_word(c, 1, 0x82b59cu);
}

// The cursor's display record, in A: at the grid's first place, on the
// screen and not in the world, drawn over everything, and this thread's.
void cursor_record_begin(Wram* w, const Rom* rom, PortCpu* c) {
  (void)rom;
  PORT_COVER(cursor_record_begin);
  const uint16_t record = c->a;
  wram_w16(w, W_CURSOR_RECORD, record);
  c->y = record;
  wram_w16(w, (uint16_t)(record + ACTOR_X), CURSOR_AT_LEFT);
  wram_w16(w, (uint16_t)(record + ACTOR_Z), 0);
  wram_w16(w, (uint16_t)(record + ACTOR_Y), CURSOR_AT_TOP);
  wram_w16(w, (uint16_t)(record + ACTOR_META), CURSOR_SPRITE);
  wram_w16(w, (uint16_t)(record + ACTOR_META_BANK), CURSOR_SPRITE_BANK);
  wram_w16(w, (uint16_t)(record + ACTOR_THREAD),
           wram_r16(w, W_SCHED_CUR_TASK));
  wram_w16(w, (uint16_t)(record + ACTOR_COLLIDE_ID), 0);
  wram_w16(w, (uint16_t)(record + ACTOR_FLAGS),
           (uint16_t)(wram_r16(w, (uint16_t)(record + ACTOR_FLAGS)) |
                      ACTOR_DRAW | ACTOR_SCREEN_SPACE | ACTOR_PRIORITY_TOP));
  load_word(c, 1, 0x82b5deu);
}

// What each pad holds and which way, and Start.
void cursor_pads_forget(Wram* w, const Rom* rom, PortCpu* c) {
  (void)rom;
  PORT_COVER(cursor_pads_forget);
  for (int pad = 0; pad < 2; pad++) {
    wram_w16(w, (uint16_t)(W_CURSOR_PADS + 2 * pad), 0);
    wram_w16(w, (uint16_t)(W_CURSOR_DIRS + 2 * pad), 0);
  }
  wram_w16(w, W_CURSOR_START, 0);
  c->pc = 0x82b583u;
}

// Nothing entered, and the clock set. The loop leaves X two under zero.
void cursor_entry_clear(Wram* w, const Rom* rom, PortCpu* c) {
  (void)rom;
  PORT_COVER(cursor_entry_clear);
  for (int at = CURSOR_ENTRY_MAX; at >= 0; at -= 2) {
    wram_w16(w, (uint16_t)(W_CURSOR_ENTRY + at), 0);
  }
  c->x = 0xfffeu;
  wram_w16(w, W_CURSOR_ENTRY_AT, 0);
  wram_w16(w, W_CURSOR_BUTTON, 0);
  wram_w16(w, W_CURSOR_DIR, 0);
  wram_w16(w, W_CURSOR_TIME, CURSOR_TURNS);
  load_word(c, CURSOR_TURN_TICKS, CURSOR_SLEEP_PC);
}

void cursor_check_call(Wram* w, const Rom* rom, PortCpu* c) {
  const uint32_t at = screen(w, c->d);
  set_c(c, false);
  if (cursor_check_none(w, rom, c->d)) {
    // `LDY #4 : LDA [$E6],Y : BEQ`, to the `RTS`.
    PORT_COVER(cursor_check_none);
    c->y = 4;
    load_a(c, 0);
    c->pc = (uint32_t)CURSOR_BANK << 16 | (uint16_t)(pull16(w, c) + 1);
    return;
  }
  PORT_COVER(cursor_check_call);
  push16(w, c, rom_word(rom, at + 4));
  c->y = 6;
  c->a = rom_word(rom, at + 6);
  xba(c);
  wram_w16(w, W_CURSOR_CHECK + 1, c->a);
  load_a(c, pull16(w, c));
  wram_w16(w, W_CURSOR_CHECK, c->a);
  // What the check's `RTL` returns through: this bank, and the `RTS` after.
  push8(w, c, c->db);
  push16(w, c, CURSOR_CHECK_RETURN);
  c->pc = check_routine(w, rom, c->d);
}

bool cursor_checked(Wram* w, PortCpu* c) {
  (void)w;
  if (flag(c, PORT_P_C)) {
    PORT_COVER(cursor_checked_wrong);
    load_word(c, CURSOR_SOUND_WRONG, CURSOR_WRONG_PC);
    return false;
  }
  PORT_COVER(cursor_checked_right);
  load_word(c, CURSOR_SOUND_RIGHT, CURSOR_RIGHT_PC);
  return true;
}

// What the printer is called with for the entry: how it is printed in X,
// where and the words' bank in A, and the entry, which is in low memory, in
// Y.
static void entry_print_ask(Wram* w, const Rom* rom, PortCpu* c,
                            uint16_t which, uint32_t how, uint32_t pc) {
  c->x = rom_word(rom, how + which);
  const uint16_t place = rom_word(rom, CURSOR_SCREEN_PLACES + which);
  c->a = (uint16_t)((place << 8) | CURSOR_BANK);
  c->y = W_CURSOR_ENTRY;
  set_nz16(c, c->y);
  wram_w16(w, W_TEXT_STRING_BANK, 0);
  c->pc = pc;
}

// The entry flashes twice. Which screen is kept where the cursor's column
// was.
void cursor_flash_begin(Wram* w, const Rom* rom, PortCpu* c) {
  PORT_COVER(cursor_flash_begin);
  const uint16_t which = rom_word(rom, screen(w, c->d));
  wram_w16(w, W_CURSOR_FLASH_SCREEN, which);
  wram_w16(w, W_CURSOR_FLASHES, 1);
  entry_print_ask(w, rom, c, which, CURSOR_ENTRY_LIT, 0x82b3bau);
}

void cursor_flash_lit(Wram* w, const Rom* rom, PortCpu* c) {
  PORT_COVER(cursor_flash_lit);
  entry_print_ask(w, rom, c, wram_r16(w, W_CURSOR_FLASH_SCREEN),
                  CURSOR_ENTRY_LIT, 0x82b3bau);
}

void cursor_flash_lit_sleep(Wram* w, const Rom* rom, PortCpu* c) {
  (void)w;
  (void)rom;
  PORT_COVER(cursor_flash_lit_sleep);
  load_word(c, CURSOR_FLASH_TICKS, 0x82b3c5u);
}

void cursor_flash_plain(Wram* w, const Rom* rom, PortCpu* c) {
  PORT_COVER(cursor_flash_plain);
  entry_print_ask(w, rom, c, wram_r16(w, W_CURSOR_FLASH_SCREEN),
                  CURSOR_ENTRY_PLAIN, 0x82b3e1u);
}

void cursor_flash_plain_sleep(Wram* w, const Rom* rom, PortCpu* c) {
  (void)w;
  (void)rom;
  PORT_COVER(cursor_flash_plain_sleep);
  load_word(c, CURSOR_FLASH_TICKS, 0x82b3ecu);
}

bool cursor_flash_next(Wram* w, PortCpu* c) {
  const uint16_t left = (uint16_t)(wram_r16(w, W_CURSOR_FLASHES) - 1);
  wram_w16(w, W_CURSOR_FLASHES, left);
  set_nz16(c, left);
  if (left & 0x8000u) {
    PORT_COVER(cursor_flash_done);
    c->pc = CURSOR_FLASH_DONE_PC;
    return false;
  }
  PORT_COVER(cursor_flash_again);
  c->pc = CURSOR_FLASH_AGAIN_PC;
  return true;
}

void cursor_record_free_ask(Wram* w, const Rom* rom, PortCpu* c) {
  (void)rom;
  PORT_COVER(cursor_record_free_ask);
  load_from(w, c, W_CURSOR_RECORD, 0x82b2edu);
}

void cursor_screen_dark(Wram* w, const Rom* rom, PortCpu* c) {
  (void)rom;
  PORT_COVER(cursor_screen_dark);
  wram_w16(w, W_BRIGHTNESS_SHADOW, BRIGHTNESS_DARK);
  load_word(c, BRIGHTNESS_DARK, 0x82b303u);
}

void cursor_sprites_job_ask(Wram* w, const Rom* rom, PortCpu* c) {
  (void)rom;
  PORT_COVER(cursor_sprites_job_ask);
  load_from(w, c, W_CURSOR_SPRITES_JOB, 0x82b30au);
}

// What it returns is where the entry is. `PLB : PLB`: the byte it pushed to
// set its own data bank, and its caller's.
void cursor_screen_end(Wram* w, const Rom* rom, PortCpu* c) {
  (void)rom;
  PORT_COVER(cursor_screen_end);
  c->a = W_CURSOR_ENTRY;
  pull8(w, c);
  c->db = pull8(w, c);
  set_nz8(c, c->db);
  c->pc = 0x82b314u;
}

void cursor_wrong_sleep(Wram* w, const Rom* rom, PortCpu* c) {
  (void)w;
  (void)rom;
  PORT_COVER(cursor_wrong_sleep);
  load_word(c, CURSOR_WRONG_TICKS, 0x82b31fu);
}

void cursor_wrong_again(Wram* w, const Rom* rom, PortCpu* c) {
  (void)w;
  (void)rom;
  PORT_COVER(cursor_wrong_again);
  load_word(c, CURSOR_SOUND_WRONG, 0x82b326u);
}

void cursor_wrong_again_sleep(Wram* w, const Rom* rom, PortCpu* c) {
  (void)w;
  (void)rom;
  PORT_COVER(cursor_wrong_again_sleep);
  load_word(c, CURSOR_WRONG_TICKS, 0x82b32du);
}

// After a pick's sound: the entry printed again.
void cursor_entry_print_ask(Wram* w, const Rom* rom, PortCpu* c) {
  PORT_COVER(cursor_entry_print_ask);
  entry_print_ask(w, rom, c, rom_word(rom, screen(w, c->d)),
                  CURSOR_ENTRY_PLAIN, 0x82b4a6u);
}

// `REP #$30`, which a pick that was wide already ends on.
void cursor_pick_end(Wram* w, const Rom* rom, PortCpu* c) {
  (void)w;
  (void)rom;
  PORT_COVER(cursor_pick_wide);
  c->pc = 0x82b4b0u;
}
