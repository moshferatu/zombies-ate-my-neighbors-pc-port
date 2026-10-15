// The game over's thread, between its calls -- see port/game_over.h.

#include "port/game_over.h"

#include "port/clears.h"  // W_TOP_SCORES_SECOND
#include "port/coverage.h"
#include "port/frontend.h"  // W_BG3_SCROLL_Y
#include "port/vblank.h"    // BRIGHTNESS_BLANKED

static void lda(PortCpu* c, uint16_t v) {
  c->a = v;
  set_nz16(c, v);
}

// What a job is asked for with: A the job, Y its bank.
static void job_words(PortCpu* c, uint16_t job, uint32_t exit) {
  c->a = job;
  c->y = GAME_OVER_JOB_BANK;
  set_nz16(c, c->y);
  c->pc = exit;
}

void game_over_scroll_set(Wram* w, PortCpu* c, GameOverWork* k) {
  PORT_COVER(game_over_scroll_set);
  // `LDA #0 : SEC : SBC #$20`.
  set_c(c, true);
  c->a = sbc16(c, 0, 0x0020);
  wram_w16(w, W_BG3_SCROLL_Y, c->a);
  job_words(c, GAME_OVER_TEXT_MAP_JOB, GAME_OVER_SCROLL_SET_EXIT);
  k->blocks[GO_SCROLL_SET]++;
}

void game_over_ask(PortCpu* c, uint16_t job, uint32_t exit, GameOverWork* k) {
  PORT_COVER(game_over_ask);
  job_words(c, job, exit);
  k->blocks[GO_ASK]++;
}

void game_over_sprite(Wram* w, PortCpu* c, uint16_t x, bool last,
                      GameOverWork* k) {
  c->x = x;
  set_nz16(c, x);
  if (last) {
    PORT_COVER(game_over_last_sprite);
    k->blocks[GO_LAST_SPRITE]++;
  } else {
    PORT_COVER(game_over_sprite);
    // The `JSR` is three bytes after the `LDX`.
    push16(w, c, (uint16_t)(c->pc + 5));
    k->blocks[GO_SPRITE]++;
  }
  c->pc = GAME_OVER_SPRITE_PC;
}

void game_over_rest(PortCpu* c, uint16_t ticks, uint32_t exit,
                    GameOverWork* k) {
  PORT_COVER(game_over_rest);
  lda(c, ticks);
  k->blocks[GO_REST]++;
  c->pc = exit;
}

void game_over_wait(Wram* w, PortCpu* c, GameOverWork* k) {
  const uint16_t brightness = wram_r16(w, W_BRIGHTNESS_SHADOW);
  lda(c, brightness);
  k->blocks[GO_FADE_TEST]++;
  if (brightness != 0) {
    PORT_COVER(game_over_fade);
    wram_w16(w, W_BRIGHTNESS_SHADOW, (uint16_t)(brightness - 1));
    set_nz16(c, (uint16_t)(brightness - 1));
    k->blocks[GO_FADE]++;
    c->pc = GAME_OVER_FADE_WAI_PC;
    return;
  }
  PORT_COVER(game_over_blank);
  k->blocks[GO_TAKEN]++;
  lda(c, BRIGHTNESS_BLANKED);
  wram_w16(w, W_BRIGHTNESS_SHADOW, c->a);
  k->blocks[GO_BLANK]++;
  c->pc = GAME_OVER_BLANK_WAI_PC;
}

void game_over_job_sent(Wram* w, PortCpu* c, GameOverWork* k) {
  PORT_COVER(game_over_job_sent);
  lda(c, pull16(w, c));
  set_c(c, false);
  k->blocks[GO_JOB_SENT]++;
  c->pc += 2;
}

bool top_scores_qualify(const Wram* w, int player) {
  const uint16_t at = (uint16_t)(W_PLAYER_SCORE + 4 * player);
  const uint32_t score = (uint32_t)wram_r16(w, at) |
                         (uint32_t)wram_r16(w, (uint16_t)(at + 2)) << 16;
  const uint16_t row = (uint16_t)(W_TOP_SCORES_SECOND + TOP_SCORES_LAST_ROW);
  const uint32_t tenth = (uint32_t)wram_r16(w, row) |
                         (uint32_t)wram_r16(w, (uint16_t)(row + 2)) << 16;
  return score >= tenth;
}

void top_scores_none(Wram* w) {
  PORT_COVER(top_scores_check);
  wram_w16(w, TOP_SCORES_COUNT, 0);
}
