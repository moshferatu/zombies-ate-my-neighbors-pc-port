// $82:D8FD  the radar's thread, a frame of it -- see port/radar_thread.h.

#include "port/radar_thread.h"

#include "port/coverage.h"
#include "port/dma.h"  // what the jobs are left
#include "port/oam.h"  // the display record's fields
#include "port/thread.h"

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

static void search(Wram* w, PortCpu* c, RadarWork* k);

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

  search(w, c, k);
}

// `$82:D913`: the next neighbour near enough, and the square put there.
static void search(Wram* w, PortCpu* c, RadarWork* k) {
  const uint16_t page = c->d;
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

// ---------------------------------------------------------------------------
// The panel
// ---------------------------------------------------------------------------

// A word of one of the panel's tables.
static uint16_t table(const Wram* w, const Rom* rom, uint32_t at) {
  return bus_r16(w, rom, at);
}

bool radar_needs_met(const Wram* w, RadarNeeds needs, const PortCpu* c) {
  const uint16_t page = c->d;
  switch (needs) {
    case RADAR_NEEDS_NOTHING:
      return true;
    case RADAR_NEEDS_NUMBER:
      return field(w, page, RADAR_DP_NUMBER) <= 1;
    case RADAR_NEEDS_PLAYER:
      return field(w, page, RADAR_DP_PLAYER) == 0 ||
             field(w, page, RADAR_DP_PLAYER) == 2;
    case RADAR_NEEDS_SQUARE:
      return c->y >= 0x0100 && c->y < 0x1f00;
    case RADAR_NEEDS_TILES: {
      const uint8_t bank = wram_r8(w, (uint16_t)(c->s + 4));
      return bank >= 0x80 && c->a >= 0x8000 &&
             c->a <= 0x10000u - TILE_RECT_RECORD_BYTES;
    }
    case RADAR_NEEDS_FRAME:
      return radar_frame_supported(w, page);
  }
  return false;
}

// `$82:D8DB`: the thread's first instruction. The two players' radars take
// turns, a tick each, so the second to go up waits one if it has to.
void radar_begin(Wram* w, const Rom* rom, PortCpu* c, RadarWork* k) {
  (void)rom;
  const uint16_t page = c->d;
  const uint16_t number = field(w, page, RADAR_DP_NUMBER);
  c->a = asl16(c, number);
  c->x = c->a;
  const uint16_t up = (uint16_t)(wram_r16(w, (uint16_t)(W_RADAR_UP + c->x)) + 1);
  wram_w16(w, (uint16_t)(W_RADAR_UP + c->x), up);
  lda(c, wram_r16(w, W_SCHED_TICK) & 0x0001u);
  cmp16(c, c->a, number);
  k->blocks[RD_BEGIN]++;
  if (c->a == number) {
    PORT_COVER(radar_begin_in_step);
    k->blocks[RD_TAKEN]++;
    c->pc = RADAR_DRAW_PC;
    return;
  }
  PORT_COVER(radar_begin_waited);
  lda(c, 0x0001);
  k->blocks[RD_BEGIN_TICK]++;
  c->pc = RADAR_BEGIN_YIELD_PC;
}

// `$82:D8F6`: the panel is up, and two ticks to the first frame.
void radar_drawn(Wram* w, const Rom* rom, PortCpu* c, RadarWork* k) {
  (void)w;
  (void)rom;
  doze(c, k);
}

// `$82:D90E`: the count the panel shows from now on, which A already is.
void radar_count_changed(Wram* w, const Rom* rom, PortCpu* c, RadarWork* k) {
  (void)rom;
  set_field(w, c->d, RADAR_DP_LEFT, c->a);
  k->blocks[RD_CHANGED]++;
  c->pc = 0x82d910u;
}

// `$82:D913`: the rest of the frame, once the count's job is asked for.
void radar_recounted(Wram* w, const Rom* rom, PortCpu* c, RadarWork* k) {
  (void)rom;
  search(w, c, k);
}

// `$82:D9B4`: the radar is down. The player's channel is turned off first.
void radar_lower(Wram* w, const Rom* rom, PortCpu* c, RadarWork* k) {
  c->x = field(w, c->d, RADAR_DP_PLAYER);
  lda(c, table(w, rom, RADAR_CHANNELS + c->x) ^
             wram_r16(w, W_HDMA_CHANNELS));
  wram_w16(w, W_HDMA_CHANNELS, c->a);
  k->blocks[RD_LOWER]++;
  c->pc = 0x82d9bfu;
}

// `$82:D9C3`: ...then the blank picture where the panel was...
void radar_blank_ask(Wram* w, const Rom* rom, PortCpu* c, RadarWork* k) {
  (void)rom;
  c->a = asl16(c, c->x);
  push16(w, c, RADAR_BANK);
  lda(c, RADAR_BLANK);
  c->x = field(w, c->d, RADAR_DP_COLUMN);
  c->y = field(w, c->d, RADAR_DP_ROW);
  set_nz16(c, c->y);
  k->blocks[RD_BLANK_ASK]++;
  c->pc = 0x82d9d1u;
}

// `$82:D9D5`: ...and the square's record is given back, by a jump to
// `actor_slot_free` whose return ends the thread.
void radar_gone(Wram* w, const Rom* rom, PortCpu* c, RadarWork* k) {
  (void)rom;
  pull16(w, c);
  lda(c, field(w, c->d, RADAR_DP_SQUARE));
  k->blocks[RD_GONE]++;
  c->pc = ACTOR_SLOT_FREE_ENTRY;
}

// `$82:D9DC`: the player's own job for the count under the panel.
void radar_count_ask(Wram* w, const Rom* rom, PortCpu* c, RadarWork* k) {
  (void)rom;
  lda(c, field(w, c->d, RADAR_DP_NUMBER));
  k->blocks[RD_COUNT_HEAD]++;
  if (c->a != 0) {
    PORT_COVER(radar_count_second);
    k->blocks[RD_TAKEN]++;
    lda(c, (uint16_t)RADAR_COUNT_JOB_B_PC);
    c->pc = RADAR_COUNT_SECOND_PC;
  } else {
    PORT_COVER(radar_count_first);
    lda(c, (uint16_t)RADAR_COUNT_JOB_PC);
    c->pc = RADAR_COUNT_FIRST_PC;
  }
  c->y = RADAR_BANK;
  set_nz16(c, c->y);
  k->blocks[RD_COUNT_ASK]++;
}

// `$82:D9F6`: the panel going up. The player's record is kept, and the
// panel's picture asked for at the thread's column and row.
void radar_panel_ask(Wram* w, const Rom* rom, PortCpu* c, RadarWork* k) {
  const uint16_t page = c->d;
  c->a = asl16(c, field(w, page, RADAR_DP_NUMBER));
  set_field(w, page, RADAR_DP_PLAYER, c->a);
  c->x = c->a;
  set_field(w, page, RADAR_DP_RECORD,
            wram_r16(w, (uint16_t)(W_PLAYER_A_RECORD + c->x)));
  c->a = asl16(c, c->x);
  push16(w, c, c->a);
  c->y = c->a;
  push16(w, c, table(w, rom, RADAR_PICTURES + 2 + c->y));
  lda(c, table(w, rom, RADAR_PICTURES + c->y));
  c->x = field(w, page, RADAR_DP_COLUMN);
  c->y = field(w, page, RADAR_DP_ROW);
  set_nz16(c, c->y);
  k->blocks[RD_PANEL_ASK]++;
  c->pc = 0x82da10u;
}

// `ASL` three times: tiles to pixels.
static uint16_t times_eight(PortCpu* c, uint16_t v) {
  for (int i = 0; i < 3; i++) v = asl16(c, v);
  return v;
}

// `$82:DA14`: where the panel's middle is on the screen, for the square,
// and where in VRAM the count goes: seven rows under the panel's top.
#define RADAR_MIDDLE 0x001fu
#define RADAR_COUNT_ROWS_DOWN 0x0007u
#define RADAR_COUNT_BASE 0x6404u

void radar_panel_place(Wram* w, const Rom* rom, PortCpu* c, RadarWork* k) {
  (void)rom;
  const uint16_t page = c->d;
  pull16(w, c);
  c->y = pull16(w, c);
  c->a = times_eight(c, field(w, page, RADAR_DP_COLUMN));
  set_c(c, false);
  c->a = adc16(c, c->a, RADAR_MIDDLE);
  set_field(w, page, RADAR_DP_MIDDLE_X, c->a);
  // No `CLC` here: the carry is the last shift's.
  c->a = times_eight(c, field(w, page, RADAR_DP_ROW));
  c->a = adc16(c, c->a, RADAR_MIDDLE);
  set_field(w, page, RADAR_DP_MIDDLE_Y, c->a);

  set_c(c, false);
  c->a = adc16(c, field(w, page, RADAR_DP_ROW), RADAR_COUNT_ROWS_DOWN);
  for (int i = 0; i < 5; i++) c->a = asl16(c, c->a);
  set_c(c, false);
  c->a = adc16(c, c->a, field(w, page, RADAR_DP_COLUMN));
  set_c(c, false);
  c->a = adc16(c, c->a, RADAR_COUNT_BASE);
  c->x = field(w, page, RADAR_DP_PLAYER);
  set_nz16(c, c->x);
  wram_w16(w, (uint16_t)(W_RADAR_COUNT_AT + c->x), c->a);
  k->blocks[RD_PLACE]++;
  c->pc = 0x82da42u;
}

// `$82:DA46`: the square's display record, which `actor_slot_alloc` has
// just left in A and in Y. It starts off the screen.
#define RADAR_SQUARE_PICTURE 0xa33bu
#define RADAR_SQUARE_PICTURE_BANK 0x0090u
#define RADAR_SQUARE_ATTR 0x0400u
#define RADAR_SQUARE_FLAGS 0xc028u
#define W_RADAR_SQUARE_THREAD 0x0008u  // through the data bank, not the page

void radar_square_set(Wram* w, const Rom* rom, PortCpu* c, RadarWork* k) {
  (void)rom;
  const uint16_t square = c->y;
  set_field(w, c->d, RADAR_DP_SQUARE, c->a);
  wram_w16(w, (uint16_t)(square + ACTOR_X), RADAR_NOWHERE);
  wram_w16(w, (uint16_t)(square + ACTOR_Z), 0);
  wram_w16(w, (uint16_t)(square + ACTOR_Y), RADAR_NOWHERE);
  wram_w16(w, (uint16_t)(square + ACTOR_META_BANK), RADAR_SQUARE_PICTURE_BANK);
  wram_w16(w, (uint16_t)(square + ACTOR_META), RADAR_SQUARE_PICTURE);
  wram_w16(w, (uint16_t)(square + ACTOR_THREAD),
           wram_r16(w, W_RADAR_SQUARE_THREAD));
  wram_w16(w, (uint16_t)(square + ACTOR_COLLIDE_ID), 0);
  wram_w16(w, (uint16_t)(square + ACTOR_ATTR), RADAR_SQUARE_ATTR);
  wram_w16(w, (uint16_t)(square + ACTOR_FLAGS),
           wram_r16(w, (uint16_t)(square + ACTOR_FLAGS)) | RADAR_SQUARE_FLAGS);
  lda(c, 0x0000);
  c->y = 0x0000;
  k->blocks[RD_SQUARE]++;
  c->pc = 0x82da85u;
}

// `$82:DA89`: no neighbour shown yet and a count the first frame will find
// changed, and the player's channel is turned on.
#define RADAR_COUNT_UNKNOWN 0xffffu

void radar_panel_show(Wram* w, const Rom* rom, PortCpu* c, RadarWork* k) {
  const uint16_t page = c->d;
  set_field(w, page, RADAR_DP_SHOWN, 0);
  set_field(w, page, RADAR_DP_LEFT, RADAR_COUNT_UNKNOWN);
  c->x = field(w, page, RADAR_DP_PLAYER);
  lda(c, table(w, rom, RADAR_CHANNELS + c->x) | wram_r16(w, W_HDMA_CHANNELS));
  wram_w16(w, W_HDMA_CHANNELS, c->a);
  k->blocks[RD_SHOW]++;
  c->pc = 0x82da9bu;
}

// `$82:DBC1`: a rectangle of tiles asked for, X across and Y down from the
// record's corner. It does its work on page zero and leaves the caller's
// page on the stack for `$82:DC0A` to pull.
void radar_tiles_ask(Wram* w, const Rom* rom, PortCpu* c, RadarWork* k) {
  wram_w16(w, W_TILE_RECT_RECORD, c->a);
  wram_w16(w, W_TILE_RECT_RECORD + 2, wram_r16(w, (uint16_t)(c->s + 4)));
  push16(w, c, c->d);
  push16(w, c, 0x0000);
  c->d = pull16(w, c);
  const uint32_t record = wram_r16(w, W_TILE_RECT_RECORD) |
                          (uint32_t)wram_r8(w, W_TILE_RECT_RECORD + 2) << 16;

  // Thirty-two words a row, and no `CLC`: the carry is the last shift's.
  c->a = c->y;
  for (int i = 0; i < 5; i++) c->a = asl16(c, c->a);
  c->a = adc16(c, c->a, table(w, rom, record + TILE_RECT_CORNER));
  wram_w16(w, W_TILE_RECT_AT, c->a);
  set_c(c, false);
  c->a = adc16(c, c->x, wram_r16(w, W_TILE_RECT_AT));
  wram_w16(w, W_TILE_RECT_AT, c->a);

  c->a = asl16(c, table(w, rom, record + TILE_RECT_ACROSS));
  wram_w16(w, W_TILE_RECT_ROW_BYTES, c->a);
  wram_w16(w, W_TILE_RECT_ROWS, table(w, rom, record + TILE_RECT_DOWN));
  wram_w16(w, W_TILE_RECT_SOURCE, table(w, rom, record + TILE_RECT_WORDS));
  wram_w16(w, W_TILE_RECT_SOURCE + 2,
           table(w, rom, record + TILE_RECT_WORDS + 2));
  lda(c, (uint16_t)TILE_RECT_JOB_PC);
  c->y = RADAR_BANK;
  set_nz16(c, c->y);
  k->blocks[RD_TILES_ASK]++;
  c->pc = RADAR_TILES_QUEUE_PC;
}

// `$82:DC0A  PLD`, and its `RTL` is the harness's.
void radar_tiles_asked(Wram* w, const Rom* rom, PortCpu* c, RadarWork* k) {
  (void)rom;
  c->d = pull16(w, c);
  set_nz16(c, c->d);
  k->blocks[RD_TILES_ASKED]++;
  c->pc = 0x82dc0bu;
}

// `$82:DC45`: the job that gives the hardware its channels, by a jump to
// `vbl_queue_a_add`.
void radar_channels_ask(Wram* w, const Rom* rom, PortCpu* c, RadarWork* k) {
  (void)w;
  (void)rom;
  lda(c, (uint16_t)HDMA_CHANNELS_JOB_PC);
  c->y = RADAR_BANK;
  set_nz16(c, c->y);
  k->blocks[RD_CHANNELS_ASK]++;
  c->pc = VBL_QUEUE_A_ADD_PC;
}
