// A level loaded from its record -- see port/level_load.h.

#include "port/level_load.h"

#include "port/bodies.h"
#include "port/coverage.h"
#include "port/spawnlist.h"
#include "port/tile_rows.h"

// The record's words.
enum {
  RECORD_MAP = 0x04,
  RECORD_ATTRS = 0x08,
  RECORD_PICTURES = 0x0c,
  RECORD_PALETTE = 0x10,
  RECORD_SPRITE_PALETTE = 0x14,
  RECORD_THREAD = 0x18,
  RECORD_SPAWN_LIST = 0x1c,
  RECORD_VICTIMS = 0x1e,
  RECORD_OBJECTS = 0x20,
  RECORD_BLOCKS_ACROSS = 0x22,
  RECORD_BLOCKS_DOWN = 0x24,
  RECORD_PRIORITY_BELOW = 0x26,
  RECORD_LAST_TILE = 0x28,
  RECORD_START_X = 0x2a,
  RECORD_START_Y = 0x2c,
  RECORD_START_X_2 = 0x2e,
  RECORD_START_Y_2 = 0x30,
};

// An entry of the record's other threads.
enum {
  EXTRA_THREAD = 0,
  EXTRA_BANK = 2,
  EXTRA_ARG = 4,
  EXTRA_ARG_2 = 6,
};

#define OBJECT_SPAWNER_THREAD 0xc8f6u
#define HALF_SCREEN_ACROSS 0x0080u
#define HALF_SCREEN_DOWN 0x0084u

static uint16_t far_word(const Rom* rom, uint16_t at, int offset) {
  return rom_word(rom, ((uint32_t)LEVEL_RECORD_BANK << 16) + at + offset);
}

// `LDX $10`, which every stretch that reads the record begins with.
static uint16_t record_in_x(const Wram* w, PortCpu* c) {
  c->x = wram_r16(w, (uint16_t)(c->d + LEVEL_DP_RECORD));
  return c->x;
}

static void load_a(PortCpu* c, uint16_t v) {
  c->a = v;
  set_nz16(c, v);
}

static void load_y(PortCpu* c, uint16_t v) {
  c->y = v;
  set_nz16(c, v);
}

bool level_record_supported(const Wram* w, uint16_t page) {
  const uint16_t record = wram_r16(w, (uint16_t)(page + LEVEL_DP_RECORD));
  return record >= 0x8000u &&
         record <= 0xffffu - LEVEL_EXTRA_THREADS - LEVEL_EXTRA_THREAD_BYTES;
}

bool level_extra_supported(const Wram* w, uint16_t s) {
  const uint16_t entry = wram_r16(w, (uint16_t)(s + 1));
  return entry >= 0x8000u && entry <= 0xffffu - 2 * LEVEL_EXTRA_THREAD_BYTES;
}

void level_map_ask(Wram* w, const Rom* rom, PortCpu* c) {
  PORT_COVER(level_map_ask);
  wram_w16(w, W_BLOCK_LIBRARY, LEVEL_BLOCK_LIBRARY_AT);
  wram_w16(w, W_BLOCK_LIBRARY_BANK, LEVEL_BLOCK_LIBRARY_BANK);
  const uint16_t record = record_in_x(w, c);
  wram_w16(w, W_MAP_FIRST_ROW, far_word(rom, record, RECORD_MAP));
  wram_w16(w, W_MAP_FIRST_ROW + 2, far_word(rom, record, RECORD_MAP + 2));
  c->y = far_word(rom, record, RECORD_BLOCKS_DOWN);
  load_a(c, far_word(rom, record, RECORD_BLOCKS_ACROSS));
  c->x = c->a;
  c->pc = 0x8086c8u;
}

// A far pointer for a load: its address in Y and its bank in A.
static void far_pointer_ask(const Wram* w, const Rom* rom, PortCpu* c,
                            int field) {
  const uint16_t record = record_in_x(w, c);
  c->y = far_word(rom, record, field + 2);
  load_a(c, far_word(rom, record, field));
}

void level_attrs_ask(Wram* w, const Rom* rom, PortCpu* c) {
  PORT_COVER(level_attrs_ask);
  far_pointer_ask(w, rom, c, RECORD_ATTRS);
  c->pc = 0x8086dbu;
}

void level_tiles_ask(Wram* w, const Rom* rom, PortCpu* c) {
  PORT_COVER(level_tiles_ask);
  const uint16_t record = record_in_x(w, c);
  const uint16_t at = far_word(rom, record, RECORD_PICTURES);
  const uint16_t bank = far_word(rom, record, RECORD_PICTURES + 2);
  wram_w16(w, W_TILE_PICTURES, at);
  wram_w16(w, W_TILE_ANIM_BASE, at);
  wram_w16(w, W_TILE_PICTURES + 2, bank);
  wram_w16(w, W_TILE_ANIM_BASE + 2, bank);
  wram_w16(w, W_TILE_PRIORITY_BELOW,
           far_word(rom, record, RECORD_PRIORITY_BELOW));
  c->x = far_word(rom, record, RECORD_LAST_TILE);
  c->a = 0;
  load_y(c, LEVEL_TILES_VRAM);
  c->pc = 0x808707u;
}

// `CLC : ADC : LSR : SEC : SBC #`: halfway between two, less half a screen.
static uint16_t between_less(PortCpu* c, uint16_t one, uint16_t other,
                             uint16_t half_screen) {
  const uint16_t halfway = (uint16_t)((uint16_t)(one + other) >> 1);
  set_c(c, true);
  return sbc16(c, halfway, half_screen);
}

void level_camera_ask(Wram* w, const Rom* rom, PortCpu* c) {
  PORT_COVER(level_camera_ask);
  const uint16_t record = record_in_x(w, c);
  c->y = between_less(c, far_word(rom, record, RECORD_START_Y),
                      far_word(rom, record, RECORD_START_Y_2),
                      HALF_SCREEN_DOWN);
  c->a = between_less(c, far_word(rom, record, RECORD_START_X),
                      far_word(rom, record, RECORD_START_X_2),
                      HALF_SCREEN_ACROSS);
  c->x = c->a;
  c->pc = 0x80872bu;
}

void level_screen_ask(Wram* w, const Rom* rom, PortCpu* c) {
  (void)rom;
  PORT_COVER(level_screen_ask);
  wram_w16(w, W_TILEMAP_VRAM_BASE, LEVEL_MAP_VRAM);
  load_a(c, (uint16_t)(LEVEL_MAP_VRAM >> 8 | 1));  // `XBA : ORA #$0001`
  c->pc = 0x808739u;
}

// The two colour loads take their pointer the other way round.
static void colours_ask(const Wram* w, const Rom* rom, PortCpu* c, int field) {
  const uint16_t record = record_in_x(w, c);
  c->y = far_word(rom, record, field);
  load_a(c, far_word(rom, record, field + 2));
}

void level_palette_ask(Wram* w, const Rom* rom, PortCpu* c) {
  PORT_COVER(level_palette_ask);
  colours_ask(w, rom, c, RECORD_PALETTE);
  c->pc = 0x808748u;
}

void level_sprite_palette_ask(Wram* w, const Rom* rom, PortCpu* c) {
  PORT_COVER(level_sprite_palette_ask);
  colours_ask(w, rom, c, RECORD_SPRITE_PALETTE);
  c->pc = 0x808757u;
}

bool level_thread_ask(Wram* w, const Rom* rom, PortCpu* c) {
  const uint16_t record = record_in_x(w, c);
  const uint16_t thread = far_word(rom, record, RECORD_THREAD);
  const uint16_t bank = far_word(rom, record, RECORD_THREAD + 2);
  if ((thread | bank) == 0) {
    PORT_COVER(level_thread_none);
    load_a(c, 0);
    c->pc = LEVEL_THREAD_NONE_PC;
    return false;
  }
  PORT_COVER(level_thread_ask);
  c->y = bank;
  load_a(c, thread);
  c->pc = LEVEL_THREAD_SPAWN_PC;
  return true;
}

// A thread that gets one of the record's lists as a far pointer on its page.
static void list_thread_ask(Wram* w, const Rom* rom, PortCpu* c, int field,
                            uint16_t thread, uint16_t bank) {
  const uint16_t record = record_in_x(w, c);
  wram_w16(w, c->d, far_word(rom, record, field));
  wram_w16(w, (uint16_t)(c->d + 2), LEVEL_RECORD_BANK);
  c->a = thread;
  load_y(c, bank);
}

void level_spawner_ask(Wram* w, const Rom* rom, PortCpu* c) {
  PORT_COVER(level_spawner_ask);
  list_thread_ask(w, rom, c, RECORD_SPAWN_LIST,
                  (uint16_t)SPAWNLIST_BEGIN_PC, SPAWNLIST_BEGIN_PC >> 16);
  c->pc = 0x80878bu;
}

void level_victims_thread_ask(Wram* w, const Rom* rom, PortCpu* c) {
  PORT_COVER(level_victims_thread_ask);
  list_thread_ask(w, rom, c, RECORD_VICTIMS, (uint16_t)VICTIMS_START_PC,
                  VICTIMS_START_PC >> 16);
  c->pc = 0x8087a2u;
}

void level_victims_ask(Wram* w, const Rom* rom, PortCpu* c) {
  PORT_COVER(level_victims_ask);
  c->x = far_word(rom, record_in_x(w, c), RECORD_VICTIMS);
  load_a(c, LEVEL_RECORD_BANK);
  c->pc = 0x8087b0u;
}

void level_objects_ask(Wram* w, const Rom* rom, PortCpu* c) {
  PORT_COVER(level_objects_ask);
  list_thread_ask(w, rom, c, RECORD_OBJECTS, OBJECT_SPAWNER_THREAD, 0x0080u);
  c->pc = 0x8087c7u;
}

void level_extras_begin(Wram* w, const Rom* rom, PortCpu* c) {
  (void)rom;
  PORT_COVER(level_extras_begin);
  set_c(c, false);
  c->a = adc16(c, LEVEL_EXTRA_THREADS,
               wram_r16(w, (uint16_t)(c->d + LEVEL_DP_RECORD)));
  push16(w, c, c->a);
  c->pc = 0x8087d2u;
}

bool level_extra_next(Wram* w, const Rom* rom, PortCpu* c) {
  const uint16_t entry = pull16(w, c);
  c->x = entry;
  set_c(c, false);
  push16(w, c, adc16(c, entry, LEVEL_EXTRA_THREAD_BYTES));
  const uint16_t thread = far_word(rom, entry, EXTRA_THREAD);
  const uint16_t bank = far_word(rom, entry, EXTRA_BANK);
  if ((thread | bank) == 0) {
    PORT_COVER(level_extra_none);
    load_a(c, pull16(w, c));
    c->pc = LEVEL_LOAD_RTL_PC;
    return false;
  }
  PORT_COVER(level_extra_next);
  wram_w16(w, c->d, far_word(rom, entry, EXTRA_ARG));
  wram_w16(w, (uint16_t)(c->d + 2), far_word(rom, entry, EXTRA_ARG_2));
  c->y = bank;
  load_a(c, thread);
  c->pc = LEVEL_EXTRA_SPAWN_PC;
  return true;
}

void tiles_upload_begin(Wram* w, PortCpu* c) {
  PORT_COVER(tiles_upload_begin);
  push16(w, c, c->d);
  push16(w, c, 0);
  c->d = pull16(w, c);

  const uint16_t first = c->a;
  const uint16_t first_words = (uint16_t)(first << 4);
  wram_w16(w, UPLOAD_DP_FIRST, first);
  wram_w16(w, UPLOAD_DP_FIRST_WORDS, first_words);
  wram_w16(w, UPLOAD_DP_FROM,
           (uint16_t)(first * TILE_PICTURE_BYTES + wram_r16(w, W_TILE_PICTURES)));
  wram_w16(w, UPLOAD_DP_BANK, wram_r16(w, W_TILE_PICTURES + 2));
  wram_w16(w, UPLOAD_DP_VRAM, (uint16_t)(c->y + first_words));

  // `TXA : SEC : SBC $38 : INC`, and five shifts: 32 bytes a tile.
  set_c(c, true);
  uint16_t left = (uint16_t)(sbc16(c, c->x, first) + 1);
  for (int shift = 0; shift < 5; shift++) left = asl16(c, left);
  wram_w16(w, UPLOAD_DP_LEFT, left);

  c->a = TILES_UPLOAD_JOB;
  load_y(c, TILES_UPLOAD_JOB_BANK);
  c->pc = TILES_UPLOAD_QUEUE_PC;
}

void tiles_upload_end(const Wram* w, PortCpu* c) {
  PORT_COVER(tiles_upload_end);
  c->d = pull16(w, c);
  set_nz16(c, c->d);
  c->pc = TILES_UPLOAD_RTL_PC;
}

static CameraPlace kept_on_map(uint16_t at, uint16_t limit) {
  if (at & 0x8000u) return CAMERA_PLACE_BEFORE;
  return at < limit ? CAMERA_PLACE_ON : CAMERA_PLACE_PAST;
}

CameraPlaced camera_place(Wram* w, uint16_t x, uint16_t y) {
  const uint16_t limit_y = wram_r16(w, W_CAMERA_MAX_Y);
  const CameraPlaced did = {
      kept_on_map(x, wram_r16(w, W_CAMERA_MAX_X)),
      kept_on_map(y, limit_y),
  };
  if (did.x == CAMERA_PLACE_BEFORE) x = 0;
  if (did.x == CAMERA_PLACE_PAST) x = limit_y;  // the ROM's: see the header
  if (did.y == CAMERA_PLACE_BEFORE) y = 0;
  if (did.y == CAMERA_PLACE_PAST) y = limit_y;
  if (did.x == CAMERA_PLACE_ON && did.y == CAMERA_PLACE_ON) {
    PORT_COVER(camera_place_on);
  } else {
    PORT_COVER(camera_place_kept);
  }
  wram_w16(w, W_CAMERA_X, x);
  wram_w16(w, W_CAMERA_Y, y);
  return did;
}

void palette_job_ask(PortCpu* c) {
  PORT_COVER(palette_job_ask);
  c->a = PALETTE_JOB;
  load_y(c, PALETTE_JOB_BANK);
  c->pc = PALETTE_JOB_QUEUE_PC;
}
