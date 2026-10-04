// Two vblank jobs of the screens outside a level -- see port/frontend.h.

#include "port/frontend.h"

#include <stddef.h>

#include "port/coverage.h"
#include "port/flags.h"

#define REG_VMAIN 0x2115u
#define REG_VMADD 0x2116u
#define REG_VMDATA 0x2118u
#define REG_BG1_SCROLL_X 0x210du

#define W_SCROLL_SHADOW 0x1360u
#define SHADOWED_SCROLLS 4  // BG1 across and down, then BG2's

void intro_screen_job(const Wram* w, HwTrace* t) {
  PORT_COVER(intro_screen_job);
  hw_run(t, PJ_HEAD);
  dma_to_cgram(t, PALETTE_BANK, PALETTE_SHOWN_AT, INTRO_SCREEN_COLOUR_BYTES);

  hw_run(t, IS_VMAIN);
  hw_w8(t, REG_VMAIN, 0x80);
  // Each scroll register takes its two bytes one after the other.
  for (int i = 0; i < SHADOWED_SCROLLS; i++) {
    for (int byte = 0; byte < 2; byte++) {
      hw_run(t, IS_SCROLL);
      hw_w8(t, (uint16_t)(REG_BG1_SCROLL_X + i),
            wram_r8(w, W_SCROLL_SHADOW + 2 * i + byte));
    }
  }
  hw_run(t, IS_VADDR);
  hw_w16(t, REG_VMADD, INTRO_SCREEN_TILE_AT);
  hw_run(t, IS_TILE);
  hw_w8(t, REG_VMDATA, wram_r8(w, INTRO_SCREEN_DP_TILE));
  hw_run(t, IS_TAIL);
}

// --- $80:953B  the backdrop's drift -------------------------------------------

bool backdrop_drift_job_supported(const Wram* w) {
  const uint16_t at = wram_r16(w, W_BACKDROP_DRIFT_AT);
  return at < BACKDROP_DRIFT_PATH_BYTES && at % 4 == 0;
}

void backdrop_drift_job(Wram* w, const Rom* rom, uint16_t x,
                        BackdropDriftLog* log) {
  BackdropDriftLog scratch;
  if (log == NULL) log = &scratch;
  *log = (BackdropDriftLog){0};
  log->x = x;

  const uint16_t phase = wram_r16(w, W_FRAME_COUNT) & 3;
  if (phase != 3) {
    PORT_COVER(backdrop_drift_waited);
    log->a = phase;
    log->n = true;  // the compare with 3, from below it
    return;
  }
  log->moved = true;

  const uint16_t at = wram_r16(w, W_BACKDROP_DRIFT_AT);
  PortFlags flags = {0};
  wram_w16(w, W_BG3_SCROLL_X,
           flags_add(&flags, rom_word(rom, BACKDROP_DRIFT_PATH + at),
                     wram_r16(w, W_BG3_SCROLL_X)));
  const uint16_t y =
      flags_add(&flags, rom_word(rom, BACKDROP_DRIFT_PATH + at + 2),
                wram_r16(w, W_BG3_SCROLL_Y));
  wram_w16(w, W_BG3_SCROLL_Y, y);
  log->a = y;
  log->v = flags.v;

  uint16_t next = (uint16_t)(at + 4);
  if (next >= BACKDROP_DRIFT_PATH_BYTES) {
    PORT_COVER(backdrop_drift_wrapped);
    log->wrapped = true;
    next = 0;
    log->z = true;  // the `LDX #$0000`
  } else {
    PORT_COVER(backdrop_drift_stepped);
    log->n = true;  // the compare with the path's length, from below it
  }
  wram_w16(w, W_BACKDROP_DRIFT_AT, next);
  log->x = next;
}
