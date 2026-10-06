// $82:B84A and $82:B8FB  the text printer -- see port/text.h.

#include "port/text.h"

#include "port/coverage.h"
#include "port/cpu.h"  // bus_r16, bus_fast

typedef struct {
  Wram* w;
  const Rom* rom;
  uint16_t page;
  TextWork* k;
  uint16_t x, y;  // the index registers, as the ROM leaves them
} Text;

bool text_source_ok(uint8_t bank, uint16_t at) {
  if (bank == 0x7e || bank == 0x7f) return true;
  if (bank & 0x40) return false;
  // WRAM's first 8 KB, which every such bank shows, or the cartridge.
  return at < 0x2000u || at >= 0x8000u;
}

static uint8_t byte_at(const Text* t, uint16_t dp) {
  return wram_r8(t->w, (uint16_t)(t->page + dp));
}

static void set_byte(Text* t, uint16_t dp, uint8_t v) {
  wram_w8(t->w, (uint16_t)(t->page + dp), v);
}

static uint16_t word_at(const Text* t, uint16_t dp) {
  return wram_r16(t->w, (uint16_t)(t->page + dp));
}

static void set_word(Text* t, uint16_t dp, uint16_t v) {
  wram_w16(t->w, (uint16_t)(t->page + dp), v);
}

// The far pointer at `$E0` set to `bank:at`. The ROM stores the bank a byte
// up and then the address over half of it.
static void read_from(Text* t, uint8_t bank_high, uint8_t bank, uint16_t at) {
  set_byte(t, TEXT_DP_FROM + 1, bank_high);
  set_byte(t, TEXT_DP_FROM + 2, bank);
  set_word(t, TEXT_DP_FROM, at);
}

// `LDA [$E0]`: the word the pointer is at.
static uint16_t next_word(Text* t) {
  const uint32_t at =
      ((uint32_t)byte_at(t, TEXT_DP_FROM + 2) << 16) | word_at(t, TEXT_DP_FROM);
  if (!bus_fast(at)) t->k->slow_words++;
  return bus_r16(t->w, t->rom, at);
}

static void move_on(Text* t, uint16_t by) {
  set_word(t, TEXT_DP_FROM, (uint16_t)(word_at(t, TEXT_DP_FROM) + by));
}

// A place read, with `add` added to its first word: where in the map the
// next character goes, and what it is drawn with.
static void read_place(Text* t, uint16_t add) {
  const uint16_t place = (uint16_t)(next_word(t) + add);
  move_on(t, 2);
  const uint8_t column = (uint8_t)place;
  const uint8_t row = (uint8_t)(place >> 8);
  set_byte(t, TEXT_DP_TO, (uint8_t)(column << 1));
  set_byte(t, TEXT_DP_TO + 1, 0);
  set_word(t, TEXT_DP_TO,
           (uint16_t)((row << 6) + word_at(t, TEXT_DP_TO) + TEXT_MAP_AT));

  const uint16_t with = next_word(t);
  move_on(t, 2);
  wram_w8(t->w, W_TEXT_TILE_BASE + 1, (uint8_t)((with & 0xffu) << 2));
  wram_w8(t->w, W_TEXT_JOB_BYTE, (uint8_t)(with >> 8));
  t->k->places++;
}

// A word of the map, through the far pointer at `$E3`.
static void put_tile(Text* t, uint16_t along, uint16_t tile) {
  const uint32_t at = (((uint32_t)byte_at(t, TEXT_DP_TO + 2) << 16) |
                       word_at(t, TEXT_DP_TO)) + along;
  wram_w16(t->w, at - 0x7e0000u, tile);
}

// One character: two tiles, the row below's two, and on to the next place
// along. `rows_at` is the routine's own counter of the two rows.
static void draw(Text* t, uint8_t ch, uint16_t rows_at) {
  uint16_t glyph = (uint16_t)((uint16_t)(ch - TEXT_FIRST_CHAR) << 2);
  t->x = glyph;
  if (glyph & 0x8000u) {
    PORT_COVER(text_char_skipped);
    return;
  }
  PORT_COVER(text_char_drawn);
  t->k->glyphs++;
  t->y = 0;
  wram_w16(t->w, rows_at, 2);
  for (int row = 0; row < 2; row++) {
    for (uint16_t along = 0; along < 4; along += 2) {
      const uint16_t tile = (uint16_t)(
          bus_r16(t->w, t->rom, TEXT_FONT + glyph) +
          wram_r16(t->w, W_TEXT_TILE_BASE));
      put_tile(t, along, (uint16_t)(tile | TEXT_TILE_FLAG));
      glyph = (uint16_t)(glyph + 2);
    }
    set_word(t, TEXT_DP_TO, (uint16_t)(word_at(t, TEXT_DP_TO) + 0x0040));
    glyph = (uint16_t)(glyph + TEXT_FONT_ROW - 4);
    wram_w16(t->w, rows_at, (uint16_t)(wram_r16(t->w, rows_at) - 1));
  }
  // Two rows down and back, and two tiles along.
  set_word(t, TEXT_DP_TO, (uint16_t)(word_at(t, TEXT_DP_TO) - 0x007c));
  t->x = glyph;
}

// The next byte of the string.
static uint8_t next_char(Text* t) {
  const uint8_t ch = (uint8_t)next_word(t);
  move_on(t, 1);
  t->k->bytes++;
  return ch;
}

static void finish(const Text* t, TextRegs* out) {
  out->a = 0;
  out->x = t->x;
  out->y = t->y;
}

void text_print(Wram* w, const Rom* rom, uint16_t page, uint16_t a, uint16_t x,
                uint16_t y, TextRegs* out, TextWork* k) {
  Text t = {w, rom, page, k, x, y};
  PORT_COVER(text_print);
  read_from(&t, (uint8_t)(a >> 8), (uint8_t)a, x);
  set_word(&t, TEXT_DP_TO + 1, (uint16_t)(TEXT_MAP_BANK << 8));
  read_place(&t, a & 0xff00u);

  const uint16_t bank = wram_r16(w, W_TEXT_STRING_BANK);
  read_from(&t, (uint8_t)(bank >> 8), (uint8_t)bank, y);
  for (;;) {
    const uint8_t ch = next_char(&t);
    if (ch == 0) break;
    draw(&t, ch, W_TEXT_ROWS);
  }
  finish(&t, out);
}

void text_print_lines(Wram* w, const Rom* rom, uint16_t page, uint16_t a,
                      uint16_t x, uint16_t y, TextRegs* out, TextWork* k) {
  Text t = {w, rom, page, k, x, y};
  PORT_COVER(text_print_lines);
  read_from(&t, (uint8_t)(a >> 8), (uint8_t)a, x);
  wram_w16(w, W_TEXT_LINES_OFFSET, y);
  set_word(&t, TEXT_DP_TO + 1, (uint16_t)(TEXT_MAP_BANK << 8));
  read_place(&t, y);
  for (;;) {
    const uint8_t ch = next_char(&t);
    if (ch == 0) break;
    if (ch == TEXT_NEW_PLACE) {
      PORT_COVER(text_new_place);
      read_place(&t, wram_r16(w, W_TEXT_LINES_OFFSET));
    } else {
      draw(&t, ch, W_TEXT_LINES_ROWS);
    }
  }
  finish(&t, out);
}
