#include "port/level_card.h"

#include "port/card.h"
#include "port/coverage.h"
#include "port/logo.h"

// The record's words that the screen reads.
enum {
  RECORD_BLOCKS = 0x00,  // packed, a far address
  RECORD_SONG = 0x32,    // two words, as `$80:CBD9` takes them
};

#define SETUP_PC 0x82acf0u        // where the colours and pictures are sent
#define SETUP_RETURN 0xac22u      // what the `JSL` to it pushes
#define DROP_PC 0x82ae6fu
#define FIRST_DROP_RETURN 0xac4au // ...and the two `JSR`s
#define SECOND_DROP_RETURN 0xac77u

static void load_a(PortCpu* c, uint16_t v) {
  c->a = v;
  set_nz16(c, v);
}

static void load_x(PortCpu* c, uint16_t v) {
  c->x = v;
  set_nz16(c, v);
}

static void load_y(PortCpu* c, uint16_t v) {
  c->y = v;
  set_nz16(c, v);
}

static uint16_t record_word(const Rom* rom, uint16_t record, int offset) {
  return rom_word(rom,
                  ((uint32_t)LEVEL_CARD_WORDS_BANK << 16) + record + offset);
}

void level_card_begin(Wram* w, PortCpu* c) {
  PORT_COVER(level_card_begin);
  // `PHB : PEA $0082 : PLB`, which leaves a spare byte.
  push8(w, c, c->db);
  push16(w, c, LEVEL_CARD_DATA_BANK);
  c->db = pull8(w, c);
  push16(w, c, c->x);
  push16(w, c, c->a);
  push16(w, c, c->y);
  load_a(c, (uint16_t)(wram_r16(w, W_LOGO_BRIGHTNESS) | LEVEL_CARD_SCREEN_OFF));
  wram_w16(w, W_LOGO_BRIGHTNESS, c->a);
  c->pc = LEVEL_CARD_BEGUN_PC;
}

void level_card_colours_ask(Wram* w, PortCpu* c) {
  PORT_COVER(level_card_colours_ask);
  push8(w, c, (uint8_t)(SETUP_PC >> 16));
  push16(w, c, SETUP_RETURN);
  c->a = LEVEL_CARD_COLOURS_BANK;
  c->x = LEVEL_CARD_COLOURS;
  load_y(c, LEVEL_CARD_COLOUR_BYTES);
  c->pc = 0x82acf9u;
}

void level_card_tiles_unpack_ask(Wram* w, PortCpu* c) {
  PORT_COVER(level_card_tiles_unpack_ask);
  push16(w, c, LEVEL_CARD_TILES_PACKED);
  c->a = LEVEL_CARD_TILES_BANK;
  c->x = LEVEL_CARD_SCRATCH_BANK;
  load_y(c, LEVEL_CARD_SCRATCH_AT);
  c->pc = 0x82ad0au;
}

// Y is how much the unpacking made, and is left as it is.
void level_card_tiles_send_ask(Wram* w, PortCpu* c) {
  PORT_COVER(level_card_tiles_send_ask);
  pull16(w, c);
  push16(w, c, LEVEL_CARD_SCRATCH_BANK);
  c->a = LEVEL_CARD_SCRATCH_AT;
  load_x(c, LEVEL_CARD_TILES_VRAM);
  c->pc = 0x82ad19u;
}

// `PLA : RTL`, back to the call that clears the map.
void level_card_map_clear_ask(Wram* w, PortCpu* c) {
  PORT_COVER(level_card_map_clear_ask);
  load_a(c, pull16(w, c));
  const uint16_t return_to = pull16(w, c);
  const uint8_t bank = pull8(w, c);
  c->pc = (uint32_t)bank << 16 | (uint16_t)(return_to + 1);
}

static void drop_begin(Wram* w, PortCpu* c, uint16_t return_to) {
  push16(w, c, return_to);
  load_a(c, LEVEL_CARD_DROP_FRAMES);
  wram_w16(w, W_CARD_FRAMES, c->a);
  c->pc = CARD_DROP_WAI_PC;
}

void level_card_first_drop_begin(Wram* w, PortCpu* c) {
  PORT_COVER(level_card_first_drop_begin);
  drop_begin(w, c, FIRST_DROP_RETURN);
}

void level_card_second_drop_begin(Wram* w, PortCpu* c) {
  PORT_COVER(level_card_second_drop_begin);
  drop_begin(w, c, SECOND_DROP_RETURN);
}

bool level_card_record_supported(uint16_t record) {
  return record >= 0x8000u && record <= 0xffffu - RECORD_SONG - 3;
}

void level_card_song_ask(Wram* w, const Rom* rom, PortCpu* c) {
  PORT_COVER(level_card_song_ask);
  wram_w16(w, W_NMI_FRAME_COUNTER, 0);
  const uint16_t record = pull16(w, c);
  wram_w16(w, W_LEVEL_CARD_RECORD, record);
  // The first word is pushed and pulled round the second's `TAX`.
  const uint16_t first = record_word(rom, record, RECORD_SONG);
  push16(w, c, first);
  c->x = record_word(rom, record, RECORD_SONG + 2);
  load_a(c, pull16(w, c));
  c->pc = LEVEL_CARD_SONG_ASKED_PC;
}

void level_card_second_words_ask(Wram* w, PortCpu* c) {
  PORT_COVER(level_card_second_words_ask);
  c->a = LEVEL_CARD_WORDS_BANK;
  load_x(c, pull16(w, c));
  c->pc = 0x82ac71u;
}

void level_card_blocks_unpack_ask(Wram* w, const Rom* rom, PortCpu* c) {
  PORT_COVER(level_card_blocks_unpack_ask);
  wram_w16(w, W_NMI_FRAME_COUNTER, 0);
  const uint16_t record = wram_r16(w, W_LEVEL_CARD_RECORD);
  push16(w, c, record_word(rom, record, RECORD_BLOCKS));
  c->a = record_word(rom, record, RECORD_BLOCKS + 2);
  c->x = LEVEL_CARD_SCRATCH_BANK;
  load_y(c, LEVEL_CARD_SCRATCH_AT);
  c->pc = LEVEL_CARD_BLOCKS_ASKED_PC;
}

void level_card_blocks_unpacked(Wram* w, PortCpu* c) {
  PORT_COVER(level_card_blocks_unpacked);
  load_a(c, pull16(w, c));
  c->pc = 0x82ac92u;
}

void level_card_blank(Wram* w, PortCpu* c) {
  PORT_COVER(level_card_blank);
  load_a(c, (uint16_t)(wram_r16(w, W_LOGO_BRIGHTNESS) | LEVEL_CARD_SCREEN_OFF));
  wram_w16(w, W_LOGO_BRIGHTNESS, c->a);
  c->pc = 0x82acafu;
}
