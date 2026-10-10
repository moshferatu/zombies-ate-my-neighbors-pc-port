// $82:ABBD  the colour fade's frame -- see port/palfade.h.

#include "port/palfade.h"

#include <stddef.h>

#include "port/coverage.h"
#include "port/flags.h"
#include "port/thread.h"

// The colours the screen is sent, and the background's second copy.
#define SHADOW_BACKGROUND 0x5428u
#define SHADOW_SPRITES 0x5528u
#define SHADOW_BACKGROUND_COPY 0x5628u

#define BLUE 0x7c00u
#define GREEN 0x03e0u
#define RED 0x001fu
#define BLUE_ONE 0x0400u
#define GREEN_ONE 0x0020u
#define RED_ONE 0x0001u

// What the row routines leave on page zero, `$82:AB19` and `$82:AB5B` both.
#define ZP_TARGETS 0x28        // the palette being faded to, a far address
#define ZP_MOVED 0x38          // colours of the row that moved
#define ZP_ROW_END 0x3a
#define ZP_NEW_GREEN 0x3e      // the last colour that moved, taken apart
#define ZP_NEW_BLUE 0x40
#define ZP_TARGET_RED 0x42
#define ZP_TARGET_GREEN 0x44
#define ZP_TARGET_BLUE 0x46
#define ZP_NOW 0x48
#define ZP_TARGET 0x4a         // the last colour looked at

typedef struct {
  uint16_t at, bank;
} FarAddress;

// One waking of the thread, and the carry and overflow the ROM would leave.
typedef struct {
  Wram* w;
  const Rom* rom;
  uint16_t page;
  PalfadeLog* log;  // never NULL here
  PortFlags flags;
} Palfade;

static uint16_t field(const Palfade* f, uint16_t at) {
  return wram_r16(f->w, (uint16_t)(f->page + at));
}

static void set_field(Palfade* f, uint16_t at, uint16_t v) {
  wram_w16(f->w, (uint16_t)(f->page + at), v);
}

static FarAddress far_field(const Wram* w, uint16_t page, uint16_t at) {
  return (FarAddress){wram_r16(w, (uint16_t)(page + at)),
                      wram_r16(w, (uint16_t)(page + at + 2))};
}

// A palette in the cartridge, all eight rows of it.
static bool in_cartridge(FarAddress palette) {
  const uint16_t bank = palette.bank & 0x00ffu;
  return bank >= 0x80 && bank <= 0x9f && palette.at >= 0x8000u &&
         palette.at <= 0x10000u - PALFADE_ROWS_END;
}

// One channel a step nearer its target's. `one` is the channel's lowest bit.
static uint16_t channel_nearer(uint16_t now, uint16_t target, uint16_t one,
                               PalfadeChannelWork* work) {
  if (now == target) {
    work->kept++;
    return now;
  }
  if (now > target) {
    work->lowered++;
    return (uint16_t)(now - one);
  }
  work->raised++;
  return (uint16_t)(now + one);
}

// `$82:AAB7`: a colour a step nearer its target, each channel by itself.
// Only called for one that is not there yet.
static uint16_t colour_nearer(Wram* w, uint16_t now, uint16_t target,
                              PalfadeRowWork* work) {
  const uint16_t blue =
      channel_nearer(now & BLUE, target & BLUE, BLUE_ONE, &work->blue);
  const uint16_t green =
      channel_nearer(now & GREEN, target & GREEN, GREEN_ONE, &work->green);
  const uint16_t red =
      channel_nearer(now & RED, target & RED, RED_ONE, &work->red);

  wram_w16(w, ZP_NOW, now);
  wram_w16(w, ZP_TARGET_BLUE, target & BLUE);
  wram_w16(w, ZP_TARGET_GREEN, target & GREEN);
  wram_w16(w, ZP_TARGET_RED, target & RED);
  wram_w16(w, ZP_NEW_BLUE, blue);
  wram_w16(w, ZP_NEW_GREEN, green);
  return (uint16_t)(blue | green | red);
}

// `$82:AB19` and `$82:AB5B`: a row of sixteen colours a step nearer. True
// when any of them moved. `copy` is the second place a moved colour is
// written, or 0 for none.
static bool palfade_row(Palfade* f, FarAddress targets, uint16_t row, uint16_t shadow,
                     uint16_t copy, PalfadeRowWork* work) {
  Wram* w = f->w;
  work->ran = true;
  wram_w16(w, ZP_TARGETS, targets.at);
  wram_w16(w, ZP_TARGETS + 2, targets.bank);
  wram_w16(w, ZP_ROW_END, (uint16_t)(row + PALFADE_ROW_BYTES));

  int moved = 0;
  for (uint16_t at = row; at < row + PALFADE_ROW_BYTES; at += 2) {
    const uint16_t target = rom_word(
        f->rom, ((uint32_t)(targets.bank & 0x00ffu) << 16) + targets.at + at);
    const uint16_t now = wram_r16(w, shadow + at);
    wram_w16(w, ZP_TARGET, target);
    if (now == target) {
      work->settled++;
      continue;
    }
    const uint16_t next = colour_nearer(w, now, target, work);
    wram_w16(w, shadow + at, next);
    if (copy != 0) wram_w16(w, copy + at, next);
    moved++;
  }
  wram_w16(w, ZP_MOVED, (uint16_t)moved);
  if (moved != 0) PORT_COVER(palfade_row_moved);
  return moved != 0;
}

// The background's row. Its eighth is not faded: the routine notes where the
// targets are and goes back.
static bool palfade_background_row(Palfade* f, uint16_t row) {
  const FarAddress targets = far_field(f->w, f->page, PALFADE_DP_BACKGROUND);
  if (row >= PALFADE_BACKGROUND_END) {
    PORT_COVER(palfade_background_spared);
    wram_w16(f->w, ZP_TARGETS, targets.at);
    wram_w16(f->w, ZP_TARGETS + 2, targets.bank);
    return false;
  }
  return palfade_row(f, targets, row, SHADOW_BACKGROUND, SHADOW_BACKGROUND_COPY,
                  &f->log->background);
}

static bool palfade_sprite_row(Palfade* f, uint16_t row) {
  return palfade_row(f, far_field(f->w, f->page, PALFADE_DP_SPRITES), row,
                  SHADOW_SPRITES, 0, &f->log->sprites);
}

static void note_moved(Palfade* f) {
  set_field(f, PALFADE_DP_MOVED, (uint16_t)(field(f, PALFADE_DP_MOVED) + 1));
}

// Sleep a tick less each time, down to one.
static void sleep_sooner(Palfade* f) {
  const uint16_t ticks = field(f, PALFADE_DP_TICKS);
  if (!flags_at_least(&f->flags, ticks, 2)) return;
  PORT_COVER(palfade_slept_sooner);
  f->log->sooner = true;
  set_field(f, PALFADE_DP_TICKS, (uint16_t)(ticks - 1));
}

bool palfade_frame_supported(const Wram* w, uint16_t page) {
  const uint16_t row = wram_r16(w, (uint16_t)(page + PALFADE_DP_ROW));
  return row < PALFADE_ROWS_END && row % PALFADE_ROW_BYTES == 0 &&
         in_cartridge(far_field(w, page, PALFADE_DP_BACKGROUND)) &&
         in_cartridge(far_field(w, page, PALFADE_DP_SPRITES));
}

bool palfade_frame(Wram* w, const Rom* rom, uint16_t page, PalfadeLog* log) {
  PalfadeLog scratch;
  if (log == NULL) log = &scratch;
  *log = (PalfadeLog){0};
  Palfade f = {w, rom, page, log, {0}};

  const uint16_t row = field(&f, PALFADE_DP_ROW);
  if (palfade_background_row(&f, row)) note_moved(&f);
  if (palfade_sprite_row(&f, row)) note_moved(&f);

  uint16_t next = flags_add(&f.flags, row, PALFADE_ROW_BYTES);
  if (flags_at_least(&f.flags, next, PALFADE_ROWS_END)) {
    log->swept = true;
    if (field(&f, PALFADE_DP_MOVED) == 0) {
      PORT_COVER(palfade_over);
      log->over = true;
      log->ended = (uint16_t)(wram_r16(w, W_PALFADE_ENDED) + 1);
      wram_w16(w, W_PALFADE_ENDED, log->ended);
      log->c = f.flags.c;
      log->v = f.flags.v;
      return false;
    }
    PORT_COVER(palfade_swept);
    log->queue_slot = vbl_queue_a_add(w, PALFADE_UPLOAD_JOB, PALFADE_UPLOAD_BANK);
    set_field(&f, PALFADE_DP_MOVED, 0);
    next = 0;
  }
  set_field(&f, PALFADE_DP_ROW, next);
  sleep_sooner(&f);

  log->ticks = field(&f, PALFADE_DP_TICKS);
  log->c = f.flags.c;
  log->v = f.flags.v;
  return true;
}
