// The screens outside a level -- see port/frontend.h.

#include "port/frontend.h"

#include <stddef.h>

#include "port/coverage.h"
#include "port/flags.h"

#define REG_VMAIN 0x2115u
#define REG_VMADD 0x2116u
#define REG_VMDATA 0x2118u
#define REG_BG1_SCROLL_X 0x210du

#define REG_BG3_SCROLL_X 0x2111u
#define REG_BG3_SCROLL_Y 0x2112u
#define REG_CGADD 0x2121u
#define REG_CGDATA 0x2122u

#define W_SCROLL_SHADOW 0x1360u
#define SHADOWED_SCROLLS 4  // BG1 across and down, then BG2's

// A sprite's record: how far down the screen it is.
#define SPRITE_Y 0x06

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

// --- $80:953B  the backdrop's drift ------------------------------------------

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

// --- $82:B1F9  the backdrop's slide ------------------------------------------

// A scroll register takes its two bytes one after the other.
static void send_scroll(HwTrace* t, int first_run, uint16_t reg, uint16_t v) {
  hw_run(t, first_run);
  hw_w8(t, reg, (uint8_t)v);
  hw_run(t, IS_SCROLL);
  hw_w8(t, reg, (uint8_t)(v >> 8));
}

bool backdrop_slide_job(Wram* w, HwTrace* t) {
  const uint16_t count = (uint16_t)(wram_r16(w, W_BACKDROP_SLIDE_COUNT) + 1);
  wram_w16(w, W_BACKDROP_SLIDE_COUNT, count);
  hw_run(t, BS_HEAD);
  if (count & 3) {
    PORT_COVER(backdrop_slide_waited);
    hw_run(t, DMA_TAKEN);
    hw_run(t, FE_SEC_RTL);
    return false;
  }
  PORT_COVER(backdrop_slide_moved);
  const uint16_t x = (uint16_t)(wram_r16(w, W_BG3_SCROLL_X) + 1);
  const uint16_t y = (uint16_t)(wram_r16(w, W_BG3_SCROLL_Y) + 1);
  wram_w16(w, W_BG3_SCROLL_X, x);
  wram_w16(w, W_BG3_SCROLL_Y, y);
  hw_run(t, BS_STEP);
  send_scroll(t, FE_SEP_SCROLL, REG_BG3_SCROLL_X, x);
  send_scroll(t, IS_SCROLL, REG_BG3_SCROLL_Y, y);
  hw_run(t, IS_TAIL);
  return true;
}

// --- $80:9A1B  behind the portraits ------------------------------------------

bool portrait_scroll_job(Wram* w) {
  wram_w16(w, W_BG1_SCROLL_Y, (uint16_t)(wram_r16(w, W_BG1_SCROLL_Y) + 1));
  const uint16_t count = (uint16_t)(wram_r16(w, W_PORTRAIT_SCROLL_COUNT) + 1);
  wram_w16(w, W_PORTRAIT_SCROLL_COUNT, count);
  if (count < PORTRAIT_SCROLL_EVERY) {
    PORT_COVER(portrait_scroll_waited);
    return false;
  }
  PORT_COVER(portrait_scroll_fifth);
  wram_w16(w, W_BG3_SCROLL_X, (uint16_t)(wram_r16(w, W_BG3_SCROLL_X) + 1));
  wram_w16(w, W_BG3_SCROLL_Y, (uint16_t)(wram_r16(w, W_BG3_SCROLL_Y) + 1));
  wram_w16(w, W_PORTRAIT_SCROLL_COUNT, 0);
  return true;
}

// --- $80:8A00  the game over -------------------------------------------------

void game_over_scroll_job(const Wram* w, HwTrace* t) {
  PORT_COVER(game_over_scroll_job);
  send_scroll(t, FE_SEP_SCROLL, REG_BG3_SCROLL_Y, wram_r16(w, W_BG3_SCROLL_Y));
  hw_run(t, IS_TAIL);
}

uint8_t game_over_colours_job(HwTrace* t) {
  // Three colours, low byte first.
  static const uint8_t colours[] = {0x53, 0x59, 0x8a, 0x34, 0x26, 0x1c};
  PORT_COVER(game_over_colours_job);
  hw_run(t, IS_VMAIN);
  hw_w8(t, REG_CGADD, GAME_OVER_FIRST_COLOUR);
  for (size_t i = 0; i < sizeof colours; i++) {
    hw_run(t, DMA_IMM);
    hw_w8(t, REG_CGDATA, colours[i]);
  }
  hw_run(t, IS_TAIL);
  return colours[sizeof colours - 1];
}

bool game_over_fall_supported(const Wram* w, uint16_t d) {
  if (d > 0x1ff0u) return false;
  for (int i = 0; i < GAME_OVER_SPRITES; i++) {
    if (wram_r16(w, d + GAME_OVER_DP_SPRITES + 2 * i) > 0x1ff8u) return false;
  }
  return true;
}

void game_over_fall(Wram* w, uint16_t d, GameOverFallLog* log) {
  GameOverFallLog scratch;
  if (log == NULL) log = &scratch;
  *log = (GameOverFallLog){0};
  log->twice = (wram_r16(w, W_FRAME_COUNT) & 1) != 0;
  log->a = log->twice;
  PORT_COVER_IF(log->twice, game_over_fell_two, game_over_fell_one);
  for (int step = 0; step < (log->twice ? 2 : 1); step++) {
    for (int i = 0; i < GAME_OVER_SPRITES; i++) {
      const uint16_t sprite = wram_r16(w, d + GAME_OVER_DP_SPRITES + 2 * i);
      log->x = sprite;
      log->last = (uint16_t)(wram_r16(w, sprite + SPRITE_Y) + 1);
      wram_w16(w, sprite + SPRITE_Y, log->last);
    }
  }
}

// --- $80:8A11 and $80:8A30 ---------------------------------------------------

void game_over_frame(Wram* w, PortCpu* c, bool second, GameOverFrameLog* log) {
  PORT_COVER_IF(second, game_over_frame_2, game_over_frame);
  // `JSR $8A58`, whose return address is left under the stack.
  const uint16_t s = c->s;
  push16(w, c, second ? 0x8a32 : 0x8a16);
  c->s = s;

  uint16_t shadow = wram_r16(w, W_BG3_SCROLL_Y);
  if (!second) wram_w16(w, W_BG3_SCROLL_Y, --shadow);
  game_over_fall(w, c->d, &log->fall);
  c->x = log->fall.x;
  if (second) wram_w16(w, W_BG3_SCROLL_Y, --shadow);

  c->a = shadow;
  cmp16(c, shadow, second ? GAME_OVER_SECOND_UNTIL : GAME_OVER_FIRST_UNTIL);
  log->again = flag(c, PORT_P_C);
  if (!log->again) {
    PORT_COVER(game_over_frame_last);
    c->pc = second ? GAME_OVER_FRAME_2_END_PC : GAME_OVER_FRAME_END_PC;
    return;
  }
  c->a = 1;
  set_nz16(c, c->a);
  c->pc = second ? GAME_OVER_FRAME_2_YIELD_PC : GAME_OVER_FRAME_YIELD_PC;
}

// --- $80:9A52 ----------------------------------------------------------------

bool portrait_copy_supported(uint16_t x, uint16_t y) {
  return (x == 0 || x == 4) && (y == 0 || y == 2);
}

void portrait_copy(Wram* w, const Rom* rom, uint16_t page, uint8_t db,
                   uint16_t x, uint16_t y, PortraitCopyRegs* out) {
  PORT_COVER(portrait_copy);
  PortCpu c = {0};
  const uint32_t tables = (uint32_t)db << 16;
  // Each far pointer is made the same way: the bank stored a byte up, and
  // then the address over half of it.
  wram_w16(w, (uint16_t)(page + PORTRAIT_DP_TO + 1), 0x7e00);
  uint16_t to = adc16(&c, PORTRAIT_MAP_AT,
                      bus_r16(w, rom, tables + PORTRAIT_PLACES + y));
  wram_w16(w, (uint16_t)(page + PORTRAIT_DP_TO), to);
  const uint16_t bank = bus_r16(w, rom, tables + PORTRAIT_PICTURES + 2 + x);
  wram_w16(w, (uint16_t)(page + PORTRAIT_DP_FROM + 1),
           (uint16_t)(bank << 8 | bank >> 8));
  uint16_t from = bus_r16(w, rom, tables + PORTRAIT_PICTURES + x);
  const uint32_t from_bank =
      (uint32_t)wram_r8(w, (uint16_t)(page + PORTRAIT_DP_FROM + 2)) << 16;

  for (int row = 0; row < PORTRAIT_ROWS; row++) {
    for (uint16_t along = 0; along < PORTRAIT_ROW_BYTES; along += 2) {
      wram_w16(w, (uint32_t)to + along, bus_r16(w, rom, from_bank | from));
      from = (uint16_t)(from + 2);
    }
    set_c(&c, false);
    to = adc16(&c, to, PORTRAIT_MAP_ROW);
  }
  wram_w16(w, (uint16_t)(page + PORTRAIT_DP_FROM), from);
  wram_w16(w, (uint16_t)(page + PORTRAIT_DP_TO), to);
  out->a = to;
  out->c = flag(&c, PORT_P_C);
  out->v = flag(&c, PORT_P_V);
}

// --- $83:802D and $83:8241 ---------------------------------------------------

void intro_fill(Wram* w, const Rom* rom) {
  PORT_COVER(intro_fill);
  for (uint32_t at = 0; at < INTRO_FILL_HEAD_BYTES; at += 2)
    wram_w16(w, INTRO_FILL_AT + at, bus_r16(w, rom, INTRO_FILL_HEAD + at));
  for (uint32_t at = INTRO_FILL_HEAD_BYTES; at < INTRO_FILL_BLANK_END; at += 2)
    wram_w16(w, INTRO_FILL_AT + at, INTRO_BLANK_TILE);
  for (uint32_t at = INTRO_FILL_BLANK_END; at < INTRO_FILL_END; at++)
    wram_w8(w, INTRO_FILL_AT + at, 0);
}

bool intro_colours_copy_supported(uint16_t a, uint16_t x, uint16_t y) {
  return a >= 2 && (a & 1) == 0 && a <= 0x0200 && y >= 0x8000u &&
         (uint32_t)y + a <= 0x10000u && (uint32_t)x + a <= 0x0200u;
}

void intro_colours_copy(Wram* w, const Rom* rom, uint16_t page, uint16_t a,
                        uint16_t x, uint16_t y, IntroColoursRegs* out) {
  PORT_COVER(intro_colours_copy);
  uint16_t last = 0;
  for (uint16_t left = a >> 1; left != 0; left--) {
    last = bus_r16(w, rom, ((uint32_t)INTRO_BANK << 16) | y);
    wram_w16(w, (uint32_t)PALETTE_SHOWN_AT + x, last);
    y = (uint16_t)(y + 2);
    x = (uint16_t)(x + 2);
  }
  wram_w16(w, (uint16_t)(page + INTRO_DP_COUNT), 0);
  out->a = last;
  out->x = x;
  out->y = y;
}
