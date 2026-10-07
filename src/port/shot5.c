// $81:EC03, $81:EC30  weapon 5's shot -- see port/shot5.h.

#include "port/shot5.h"

#include "port/coverage.h"
#include "port/oam.h"  // the display record's fields
#include "port/thread.h"  // W_SCHED_CUR_TASK

static uint16_t field(const Wram* w, uint16_t page, uint16_t at) {
  return wram_r16(w, (uint16_t)(page + at));
}

static void set_field(Wram* w, uint16_t page, uint16_t at, uint16_t v) {
  wram_w16(w, (uint16_t)(page + at), v);
}

static uint16_t table(const Rom* rom, uint16_t at) {
  return rom_word(rom, ((uint32_t)SHOT5_BANK << 16) | at);
}

static bool is_side(uint16_t v) { return (v & ~2u) == 0; }

// ---------------------------------------------------------------------------
// The start
// ---------------------------------------------------------------------------

bool shot5_begin_supported(const Wram* w, uint16_t page) {
  const uint16_t way = field(w, page, SHOT5_DP_FIRED_WAY);
  return way <= SHOT5_FIRED_WAY_MAX && (way & 1) == 0 &&
         is_side(field(w, page, SHOT5_DP_SIDE));
}

bool shot5_begin(Wram* w, const Rom* rom, PortCpu* c, uint16_t* record_out) {
  const uint16_t page = c->d;
  SlotAllocRegs slot;
  actor_slot_alloc(w, c->db, &slot);
  if (slot.c) return false;
  PORT_COVER(shot5_began);
  const uint16_t record = slot.a;
  *record_out = record;
  set_field(w, page, SHOT5_DP_RECORD, record);
  set_field(w, page, SHOT5_DP_SPRITE, record);

  const uint16_t fired = field(w, page, SHOT5_DP_FIRED_WAY);
  const uint16_t way = (uint16_t)(fired << 1);
  const uint16_t side = field(w, page, SHOT5_DP_SIDE);
  set_field(w, page, SHOT5_DP_WAY_KEPT, fired);
  set_field(w, page, SHOT5_DP_WAY, way);
  set_field(w, page, SHOT5_DP_STEP_X, table(rom, (uint16_t)(SHOT5_STEPS + way)));
  set_field(w, page, SHOT5_DP_STEP_Y,
            table(rom, (uint16_t)(SHOT5_STEPS + 2 + way)));
  set_field(w, page, SHOT5_DP_OWNER, side);

  const uint16_t x = field(w, page, SHOT5_DP_PLACE_X);
  const uint16_t y = (uint16_t)(field(w, page, SHOT5_DP_PLACE_Y) - SHOT5_LIFT);
  set_field(w, page, SHOT5_DP_X, x);
  set_field(w, page, SHOT5_DP_Y, y);
  set_c(c, false);
  wram_w16(w, (uint16_t)(record + ACTOR_X),
           adc16(c, x, table(rom, (uint16_t)(SHOT5_START_AT + way))));
  wram_w16(w, (uint16_t)(record + ACTOR_Z), 0);
  set_c(c, false);
  wram_w16(w, (uint16_t)(record + ACTOR_Y),
           adc16(c, y, table(rom, (uint16_t)(SHOT5_START_AT + 2 + way))));
  wram_w16(w, (uint16_t)(record + ACTOR_META), SHOT5_START_PICTURE);
  wram_w16(w, (uint16_t)(record + ACTOR_META_BANK), SHOT5_PICTURE_BANK);
  wram_w16(w, (uint16_t)(record + ACTOR_THREAD),
           wram_r16(w, W_SCHED_CUR_TASK));
  wram_w16(w, (uint16_t)(record + ACTOR_COLLIDE_ID),
           table(rom, (uint16_t)(SHOT5_COLLIDE_IDS + side)));
  wram_w16(w, (uint16_t)(record + ACTOR_FLAGS),
           (uint16_t)(wram_r16(w, (uint16_t)(record + ACTOR_FLAGS)) |
                      ACTOR_DRAW));

  c->x = side;
  c->y = record;
  c->a = side;
  set_nz16(c, c->a);
  c->pc = SHOT5_BEGIN_OWNER_PC;
  return true;
}

// The table of places for its side: a word across and a word down, a way.
static uint16_t mouths(const Wram* w, const Rom* rom, uint16_t page) {
  return table(rom, (uint16_t)(SHOT5_MOUTHS + field(w, page, SHOT5_DP_OWNER)));
}

bool shot5_aim_supported(const Wram* w, const Rom* rom, uint16_t page) {
  if (!is_side(field(w, page, SHOT5_DP_OWNER))) return false;
  const uint16_t fired = field(w, page, SHOT5_DP_FIRED_WAY);
  const uint16_t pictures = field(w, page, SHOT5_DP_PICTURES);
  return fired <= SHOT5_FIRED_WAY_MAX && (fired & 1) == 0 &&
         field(w, page, SHOT5_DP_WAY) <= SHOT5_WAY_MAX &&
         pictures >= 0x8000u && pictures <= 0xffffu - 2 * SHOT5_WAY_MAX &&
         mouths(w, rom, page) >= 0x8000u &&
         mouths(w, rom, page) <= 0xffffu - 2 * SHOT5_WAY_MAX;
}

bool shot5_aim(Wram* w, const Rom* rom, PortCpu* c) {
  PORT_COVER(shot5_aimed);
  const uint16_t page = c->d;
  const uint16_t record = field(w, page, SHOT5_DP_RECORD);
  const uint16_t owner = field(w, page, SHOT5_DP_OWNER);
  wram_w16(w, (uint16_t)(record + ACTOR_Z),
           (uint16_t)(table(rom, (uint16_t)(SHOT5_HEIGHTS + owner)) -
                      SHOT5_LIFT));

  set_c(c, false);
  const uint16_t mouth =
      adc16(c, mouths(w, rom, page), field(w, page, SHOT5_DP_WAY));
  set_c(c, false);
  const uint16_t x = adc16(c, table(rom, mouth), field(w, page, SHOT5_DP_X));
  set_field(w, page, SHOT5_DP_X, x);
  wram_w16(w, (uint16_t)(record + ACTOR_X), x);
  set_c(c, false);
  const uint16_t y =
      adc16(c, table(rom, (uint16_t)(mouth + 2)), field(w, page, SHOT5_DP_Y));
  set_field(w, page, SHOT5_DP_Y, y);
  wram_w16(w, (uint16_t)(record + ACTOR_Y), y);
  set_field(w, page, SHOT5_DP_OVER, SHOT5_OVER_FRAMES);

  // `$81:FF2A`: the flags and the picture for the way it goes. A word with
  // its top bit set is one to clear with.
  const uint16_t fired = field(w, page, SHOT5_DP_FIRED_WAY);
  const uint16_t at =
      (uint16_t)(field(w, page, SHOT5_DP_PICTURES) + (fired << 1));
  set_c(c, (fired & 0x8000u) != 0);
  const uint16_t word = table(rom, at);
  const uint16_t flags = wram_r16(w, (uint16_t)(record + ACTOR_FLAGS));
  const bool clears = (word & 0x8000u) != 0;
  wram_w16(w, (uint16_t)(record + ACTOR_FLAGS),
           clears ? (uint16_t)(flags & word) : (uint16_t)(flags | word));
  const uint16_t picture = table(rom, (uint16_t)(at + 2));
  wram_w16(w, (uint16_t)(record + ACTOR_META), picture);

  c->x = record;
  c->y = (uint16_t)((fired << 1) + 2);
  c->a = picture;
  set_nz16(c, c->a);
  c->pc = SHOT5_AIM_RTS_PC;
  return clears;
}

// ---------------------------------------------------------------------------
// Its flight
// ---------------------------------------------------------------------------

// `LDA #$0001`, and off to sleep.
static Shot5Fate flies(PortCpu* c) {
  c->a = 1;
  set_nz16(c, c->a);
  c->pc = SHOT5_YIELD_PC;
  return SHOT5_FLIES;
}

Shot5Fate shot5_frame(Wram* w, PortCpu* c, Shot5Log* log) {
  const uint16_t page = c->d;
  const uint16_t record = field(w, page, SHOT5_DP_RECORD);

  // `$81:ED93`: along by its step, and the record with it.
  set_c(c, false);
  const uint16_t x = adc16(c, field(w, page, SHOT5_DP_X),
                           field(w, page, SHOT5_DP_STEP_X));
  set_field(w, page, SHOT5_DP_X, x);
  wram_w16(w, (uint16_t)(record + ACTOR_X), x);
  set_c(c, false);
  const uint16_t y = adc16(c, field(w, page, SHOT5_DP_Y),
                           field(w, page, SHOT5_DP_STEP_Y));
  set_field(w, page, SHOT5_DP_Y, y);
  wram_w16(w, (uint16_t)(record + ACTOR_Y), y);

  // The test puts the page back as it leaves, which is the last thing to
  // write N and Z.
  terrain_point_bit2(w, x, y, &log->ground);
  c->a = log->ground.a;
  c->x = log->ground.x;
  c->y = log->ground.y;
  set_nz16(c, page);
  set_c(c, log->ground.blocked);
  c->pc = SHOT5_STOP_PC;
  if (log->ground.blocked) {
    PORT_COVER(shot5_blocked);
    return SHOT5_STOPS;
  }

  if ((c->a & SHOT5_TILE_OVER) == 0) {
    PORT_COVER(shot5_flew);
    return flies(c);
  }
  log->over = true;
  for (int i = 0; i < SHOT5_BREAK_TESTS; i++) {
    log->tests++;
    if (c->a & (SHOT5_TILE_BREAKS_FIRST << i)) {
      PORT_COVER(shot5_struck);
      c->p = (uint8_t)(c->p & ~PORT_P_Z);
      return SHOT5_STOPS;
    }
  }

  log->counted = true;
  const uint16_t left = (uint16_t)(field(w, page, SHOT5_DP_OVER) - 1);
  set_field(w, page, SHOT5_DP_OVER, left);
  if (left != 0) {
    PORT_COVER(shot5_flew_over);
    return flies(c);
  }
  PORT_COVER(shot5_spent);
  set_nz16(c, left);
  c->pc = SHOT5_SPENT_PC;
  return SHOT5_SPENT;
}

bool shot5_burst_supported(const Wram* w, uint16_t page) {
  const uint16_t way = field(w, page, SHOT5_DP_WAY);
  return way <= SHOT5_WAY_MAX && (way & 3) == 0;
}

void shot5_burst(Wram* w, const Rom* rom, PortCpu* c) {
  PORT_COVER(shot5_burst);
  const uint16_t page = c->d;
  const uint16_t way = field(w, page, SHOT5_DP_WAY);
  const uint16_t record = field(w, page, SHOT5_DP_RECORD);

  set_c(c, false);
  wram_w16(w, (uint16_t)(record + ACTOR_X),
           adc16(c, wram_r16(w, (uint16_t)(record + ACTOR_X)),
                 table(rom, (uint16_t)(SHOT5_BURST_NUDGE + way))));
  set_c(c, false);
  wram_w16(w, (uint16_t)(record + ACTOR_Y),
           adc16(c, wram_r16(w, (uint16_t)(record + ACTOR_Y)),
                 table(rom, (uint16_t)(SHOT5_BURST_NUDGE + 2 + way))));
  wram_w16(w, (uint16_t)(record + ACTOR_FLAGS),
           (uint16_t)(wram_r16(w, (uint16_t)(record + ACTOR_FLAGS)) |
                      ACTOR_PRIORITY_TOP));

  c->x = way;
  c->y = record;
  c->a = SHOT5_BURST_PICTURES;
  set_nz16(c, c->a);
  c->pc = SHOT5_BURST_PLAY_PC;
}
