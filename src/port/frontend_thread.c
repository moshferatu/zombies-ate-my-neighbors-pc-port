#include "port/frontend_thread.h"

#include "port/coverage.h"
#include "port/dma.h"
#include "port/frontend.h"

static void load_a(PortCpu* c, uint16_t v) {
  c->a = v;
  set_nz16(c, v);
}

static void load_y(PortCpu* c, uint16_t v) {
  c->y = v;
  set_nz16(c, v);
}

// `LDA dp : BNE over : INC dp`. True when the word was zero.
static bool first_time(Wram* w, PortCpu* c, uint8_t dp) {
  const uint16_t at = (uint16_t)(c->d + dp);
  load_a(c, wram_r16(w, at));
  if (c->a) return false;
  wram_w16(w, at, 1);
  set_nz16(c, 1);
  return true;
}

static bool g_opening_skipped;

void frontend_opening_skip(bool skip) { g_opening_skipped = skip; }

bool frontend_first_pass(Wram* w, PortCpu* c) {
  if (g_opening_skipped) {
    // `LDA $7C : BRA`.
    PORT_COVER(frontend_opening_skipped);
    load_a(c, wram_r16(w, (uint16_t)(c->d + FRONTEND_DP_OPENING_SHOWN)));
    c->pc = FRONTEND_TITLE_PC;
    return false;
  }
  const bool first = first_time(w, c, FRONTEND_DP_OPENING_SHOWN);
  if (first) {
    PORT_COVER(frontend_first_pass);
  } else {
    PORT_COVER(frontend_later_pass);
  }
  c->pc = first ? FRONTEND_OPENING_PC : FRONTEND_TITLE_PC;
  return first;
}

bool title_first_pass(Wram* w, PortCpu* c) {
  const bool first = first_time(w, c, TITLE_DP_WAVE_SHOWN);
  if (first) {
    PORT_COVER(title_first_pass);
  } else {
    PORT_COVER(title_later_pass);
  }
  c->pc = first ? TITLE_WAVE_BEGIN_PC : TITLE_WAVE_OVER_PC;
  return first;
}

bool frontend_title_left(PortCpu* c) {
  const bool on = flag(c, PORT_P_C);
  if (on) {
    PORT_COVER(frontend_title_left_on);
  } else {
    PORT_COVER(frontend_title_left_again);
  }
  c->pc = on ? FRONTEND_PORTRAITS_PC : FRONTEND_TITLE_AGAIN_PC;
  return on;
}

bool frontend_players_left(PortCpu* c) {
  const bool game = !flag(c, PORT_P_C);
  if (game) {
    PORT_COVER(frontend_players_left_game);
  } else {
    PORT_COVER(frontend_players_left_demo);
  }
  c->pc = game ? FRONTEND_GAME_PC : FRONTEND_DEMO_PC;
  return game;
}

bool frontend_region_check_supported(uint16_t a) {
  return (a & REGION_FIFTY_HERTZ) == 0;
}

// `REP #$20 : AND #$0010 : BEQ`, taken.
void frontend_region_check(PortCpu* c) {
  PORT_COVER(frontend_region_check);
  c->p = (uint8_t)(c->p & ~PORT_P_M);
  load_a(c, c->a & REGION_FIFTY_HERTZ);
  c->pc = FRONTEND_REGION_RTL_PC;
}

// `SEP #$20 : LDA $213F`, of a console at sixty frames a second: bit 4
// clear. What else the byte holds the `AND` drops, and the port says none.
#define REGION_SIXTY_HERTZ_STAT78 0x00u

void frontend_region_read(PortCpu* c) {
  PORT_COVER(frontend_region_read);
  c->p = (uint8_t)(c->p | PORT_P_M);
  c->a = (uint16_t)((c->a & 0xff00u) | REGION_SIXTY_HERTZ_STAT78);
  set_nz8(c, (uint8_t)c->a);
  frontend_region_check(c);
}

// What `vbl_queue_a_add` is called with: the job in A and its bank in Y.
static void job_ask(PortCpu* c, uint16_t job) {
  c->a = job;
  load_y(c, FRONTEND_JOB_BANK);
}

void frontend_scroll_job_ask(Wram* w, PortCpu* c) {
  (void)w;
  PORT_COVER(frontend_scroll_job_ask);
  job_ask(c, FRONTEND_SCROLL_JOB);
  c->pc = 0x809bf5u;
}

void frontend_scroll_job_asked(Wram* w, PortCpu* c) {
  PORT_COVER(frontend_scroll_job_asked);
  wram_w16(w, (uint16_t)(c->d + FRONTEND_DP_SCROLL_JOB), c->x);
  c->pc = 0x809bfbu;
}

// What `vbl_queue_a_remove` is called with.
void frontend_scroll_job_stop(Wram* w, PortCpu* c) {
  PORT_COVER(frontend_scroll_job_stop);
  load_a(c, wram_r16(w, (uint16_t)(c->d + FRONTEND_DP_SCROLL_JOB)));
  c->pc = 0x809c4cu;
}

void frontend_fade_in_ask(Wram* w, PortCpu* c) {
  PORT_COVER(frontend_fade_in_ask);
  wram_w16(w, W_BRIGHTNESS_SHADOW, 0);
  job_ask(c, BRIGHTNESS_UP_JOB);
  c->pc = 0x809c5eu;
}

void frontend_fade_out_ask(Wram* w, PortCpu* c) {
  (void)w;
  PORT_COVER(frontend_fade_out_ask);
  job_ask(c, BRIGHTNESS_DOWN_JOB);
  c->pc = 0x809c78u;
}

void frontend_show_sleep(Wram* w, PortCpu* c) {
  (void)w;
  PORT_COVER(frontend_show_sleep);
  load_a(c, SHOW_SLEEP_FRAMES);
  c->pc = 0x809245u;
}

void frontend_title_begin(Wram* w, PortCpu* c) {
  PORT_COVER(frontend_title_begin);
  wram_w16(w, W_BG3_SCROLL_X, TITLE_BACKDROP_X);
  wram_w16(w, W_BG3_SCROLL_Y, TITLE_BACKDROP_Y);
  wram_w16(w, W_TITLE_0072, 0);
  wram_w16(w, (uint16_t)(c->d + TITLE_DP_WAVE_LINES), TITLE_WAVE_LINES);
  wram_w16(w, (uint16_t)(c->d + TITLE_DP_WAVE_HOLD), TITLE_WAVE_HOLD);
  wram_w16(w, (uint16_t)(c->d + TITLE_DP_74), 1);
  wram_w16(w, W_BACKDROP_DRIFT_AT, 0);
  job_ask(c, BACKDROP_DRIFT_JOB);
  c->pc = 0x8094e5u;
}

// X is where the drift's job went in the queue.
void frontend_title_song_ask(Wram* w, PortCpu* c) {
  PORT_COVER(frontend_title_song_ask);
  wram_w16(w, (uint16_t)(c->d + TITLE_DP_DRIFT_JOB), c->x);
  c->a = TITLE_SONG;
  c->x = TITLE_SOUNDS;
  set_nz16(c, c->x);
  c->pc = 0x8094f1u;
}

void frontend_vram_clear_ask(Wram* w, PortCpu* c) {
  PORT_COVER(frontend_vram_clear_ask);
  wram_w16(w, W_VRAM_CLEAR_AT, 0);
  job_ask(c, VRAM_CLEAR_JOB);
  c->pc = 0x809fa6u;
}
