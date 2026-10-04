// The threads that animate a level's colours -- see port/palcycle.h.

#include "port/palcycle.h"

#include <stddef.h>

#include "port/coverage.h"
#include "port/dma.h"
#include "port/flags.h"
#include "port/thread.h"

const PalcycleTurn PALCYCLE_TURN_SIX = {0x80a0c1u, 0x80a0bdu, 0x0072, 6};
const PalcycleTurn PALCYCLE_TURN_FIVE = {0x80a236u, 0x80a232u, 0x00d0, 5};
const PalcycleTurn PALCYCLE_TURN_SEVEN = {0x80a27du, 0x80a279u, 0x00c2, 7};

#define GREEN 0x03e0u
#define GREEN_ONE 0x0020u
#define FIGURE_ROW 0x00e0u  // the background's eighth row, as a byte offset

static void ask_for_upload(Wram* w, PalcycleLog* log) {
  log->queue_slot = vbl_queue_a_add(w, PALCYCLE_JOB, PALCYCLE_JOB_BANK);
}

// --- $80:A0C1, $80:A236, $80:A27D  a run of colours turns ----------------------

// Where the counter goes next: down a place, and from below the first to the
// last.
static uint16_t turned(const Wram* w, const PalcycleTurn* turn, bool* wrapped) {
  const uint16_t at = (uint16_t)(wram_r16(w, W_PALCYCLE_TURN_AT) - 2);
  *wrapped = (at & 0x8000u) != 0;
  return *wrapped ? (uint16_t)(2 * turn->colours - 2) : at;
}

bool palcycle_turn_supported(const Wram* w, const PalcycleTurn* turn) {
  bool wrapped;
  const uint16_t at = turned(w, turn, &wrapped);
  return at % 2 == 0 && at < 2 * turn->colours;
}

void palcycle_turn(Wram* w, const PalcycleTurn* turn, PalcycleLog* log) {
  PalcycleLog scratch;
  if (log == NULL) log = &scratch;
  *log = (PalcycleLog){0};

  const uint16_t at = turned(w, turn, &log->wrapped);
  if (log->wrapped) {
    PORT_COVER(palcycle_turn_wrapped);
  } else {
    PORT_COVER(palcycle_turn_stepped);
  }
  wram_w16(w, W_PALCYCLE_TURN_AT, at);

  const int first = at / 2;
  for (int i = 0; i < turn->colours; i++) {
    const int from = (first + i) % turn->colours;
    wram_w16(w, PALETTE_SHOWN_AT + turn->first + 2 * i,
             wram_r16(w, PALETTE_AT + turn->first + 2 * from));
  }
  ask_for_upload(w, log);
}

uint16_t palcycle_turn_seven_ticks(Wram* w, bool carry, RngResult* draw) {
  rng_next(w, carry, draw);
  return (uint16_t)((draw->a & 7) + 1);
}

// --- $80:A106  a colour's green goes up and comes down -------------------------

void palcycle_pulse(Wram* w, uint16_t page, PalcycleLog* log) {
  PalcycleLog scratch;
  if (log == NULL) log = &scratch;
  *log = (PalcycleLog){0};

  const uint16_t now = wram_r16(w, (uint16_t)(page + PALCYCLE_PULSE_DP_NOW));
  const uint16_t floor = wram_r16(w, (uint16_t)(page + PALCYCLE_PULSE_DP_FLOOR));
  const uint16_t step = wram_r16(w, (uint16_t)(page + PALCYCLE_PULSE_DP_STEP));

  const uint32_t shown = PALETTE_SHOWN_AT + PALCYCLE_PULSE_COLOUR;
  wram_w16(w, shown, (uint16_t)((wram_r16(w, shown) & (uint16_t)~GREEN) | now));

  PortFlags flags = {0};
  const uint16_t next = flags_add(&flags, now, step);
  log->v = flags.v;
  wram_w16(w, (uint16_t)(page + PALCYCLE_PULSE_DP_NOW), next);

  if (next <= floor) {
    PORT_COVER(palcycle_pulse_floor);
    log->at_floor = next == floor;
    wram_w16(w, (uint16_t)(page + PALCYCLE_PULSE_DP_STEP), GREEN_ONE);
  } else {
    log->past_floor = true;
    if (next >= GREEN) {
      PORT_COVER(palcycle_pulse_top);
      log->at_top = true;
      wram_w16(w, (uint16_t)(page + PALCYCLE_PULSE_DP_STEP),
               (uint16_t)(0u - GREEN_ONE));
    } else {
      PORT_COVER(palcycle_pulse_between);
    }
  }
  ask_for_upload(w, log);
}

// --- $80:A180  the big figure's row --------------------------------------------

bool palcycle_figure_supported(const Wram* w, uint16_t page) {
  return wram_r16(w, (uint16_t)(page + PALCYCLE_FIGURE_DP_ROW)) <
         PALCYCLE_FIGURE_ROWS;
}

uint16_t palcycle_figure(Wram* w, const Rom* rom, uint16_t page,
                         PalcycleLog* log) {
  PalcycleLog scratch;
  if (log == NULL) log = &scratch;
  *log = (PalcycleLog){0};

  const uint16_t row = wram_r16(w, (uint16_t)(page + PALCYCLE_FIGURE_DP_ROW));
  const uint16_t row_at = (uint16_t)(row * 16);
  const uint32_t arrangement = PALCYCLE_FIGURE_TABLE + row_at;
  const uint16_t ticks = rom_word(rom, arrangement);

  // Last colour first, as the ROM goes.
  for (int i = PALCYCLE_FIGURE_COLOURS; i >= 1; i--) {
    const uint16_t from = rom_word(rom, arrangement + 2 * i);
    wram_w16(w, PALETTE_SHOWN_AT + FIGURE_ROW + 2 * i,
             wram_r16(w, PALETTE_AT + FIGURE_ROW + from));
  }

  uint16_t next = (uint16_t)(row + 1);
  if (next == PALCYCLE_FIGURE_ROWS) {
    PORT_COVER(palcycle_figure_restarted);
    log->wrapped = true;
    next = 0;
  } else {
    PORT_COVER(palcycle_figure_stepped);
  }
  wram_w16(w, (uint16_t)(page + PALCYCLE_FIGURE_DP_ROW), next);
  wram_w16(w, (uint16_t)(page + PALCYCLE_FIGURE_DP_TICKS), ticks);
  wram_w16(w, (uint16_t)(page + PALCYCLE_FIGURE_DP_ROW_AT), row_at);
  wram_w16(w, (uint16_t)(page + PALCYCLE_FIGURE_DP_COLOUR), 0);
  ask_for_upload(w, log);
  return ticks;
}
