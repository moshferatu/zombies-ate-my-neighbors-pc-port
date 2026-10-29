// The game over's thread, between its calls -- see port/game_over.h.

#include "port/game_over.h"

#include "port/clears.h"  // W_TOP_SCORES_SECOND
#include "port/coverage.h"
#include "port/cursor.h"  // W_CURSOR_ENTRY
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

// --- The top scores ----------------------------------------------------------

#define W_FILE_AT 0x1e96u    // a count, then the score's address, then a name's
#define W_FILE_ROW 0x1e9au   // a digit seen, then the row it goes in
#define W_FILE_NAME 0x1e9cu  // the name's address in bank `$7E`

typedef struct {
  Wram* w;
  const Rom* rom;
  PortCpu* c;
  TopScoresWork* k;
} TopScores;

static void ran(TopScores* t, int run) { t->k->runs[run]++; }

// A branch: counted if it is taken.
static bool took(TopScores* t, bool taken) {
  if (taken) t->k->runs[TOPS_TAKEN]++;
  return taken;
}

static bool carry(const PortCpu* c) { return flag(c, PORT_P_C); }
static bool zero(const PortCpu* c) { return flag(c, PORT_P_Z); }

static void ldx(PortCpu* c, uint16_t v) {
  c->x = v;
  set_nz16(c, v);
}

static void ldy(PortCpu* c, uint16_t v) {
  c->y = v;
  set_nz16(c, v);
}

static void lsr(PortCpu* c) {
  set_c(c, (c->a & 1u) != 0);
  lda(c, (uint16_t)(c->a >> 1));
}

static void plb(TopScores* t) {
  t->c->db = pull8(t->w, t->c);
  set_nz8(t->c, t->c->db);
}

// `MVP` and `MVN` within bank `$7E`, or from low memory to it: A+1 bytes,
// down from X and Y or up from them. The data bank is left at `$7E`.
static void moves(TopScores* t, bool down) {
  PortCpu* c = t->c;
  do {
    wram_w8(t->w, c->y, wram_r8(t->w, c->x));
    c->x = (uint16_t)(down ? c->x - 1 : c->x + 1);
    c->y = (uint16_t)(down ? c->y - 1 : c->y + 1);
    ran(t, TOPS_MOVED);
  } while (c->a-- != 0);
  c->db = 0x7e;
}

static void asks(TopScores* t, int player);

// `$82:BC37`: carry set if either went in, the two banks pulled, and the
// `RTL` is the harness's.
static void ends(TopScores* t) {
  PortCpu* c = t->c;
  set_c(c, false);
  lda(c, wram_r16(t->w, TOP_SCORES_COUNT));
  ran(t, TOPS_TAIL);
  if (!took(t, c->a == 0)) {
    set_c(c, true);
    ran(t, TOPS_SEC);
  }
  plb(t);
  plb(t);
  ran(t, TOPS_END);
  c->pc = TOP_SCORES_RTL_PC;
}

// `$82:BBFC`, and `$82:BC19` for the second player: is this player's score
// at least the tenth's? The row's offset is under the stack.
static void asks(TopScores* t, int player) {
  PortCpu* c = t->c;
  Wram* w = t->w;
  const uint16_t at = (uint16_t)(W_PLAYER_SCORE + 4 * player);
  if (player == 1) ldx(c, pull16(w, c));
  lda(c, wram_r16(w, (uint16_t)(at + 2)));
  cmp16(c, c->a, wram_r16(w, (uint16_t)(W_TOP_SCORES_SECOND + 2 + c->x)));
  ran(t, player ? TOPS_SECOND : TOPS_FIRST);
  bool in = false;
  if (!took(t, !carry(c))) {
    ran(t, TOPS_EQUAL);
    if (took(t, !zero(c))) {
      in = true;
    } else {
      lda(c, wram_r16(w, at));
      cmp16(c, c->a, wram_r16(w, (uint16_t)(W_TOP_SCORES_SECOND + c->x)));
      ran(t, TOPS_LOW);
      in = !took(t, !carry(c));
    }
  }
  if (!in) {
    if (player == 0) {
      asks(t, 1);
    } else {
      PORT_COVER(top_scores_check);
      ends(t);
    }
    return;
  }
  // `JSR $BC41`, which calls the screen with the player's table.
  PORT_COVER(top_scores_goes_in);
  const uint16_t count = (uint16_t)(wram_r16(w, TOP_SCORES_COUNT) + 1);
  wram_w16(w, TOP_SCORES_COUNT, count);
  ldx(c, (uint16_t)(2 * player));
  push16(w, c, player ? TOP_SCORES_RET_SECOND : TOP_SCORES_RET_FIRST);
  ran(t, TOPS_QUALIFIES);
  push16(w, c, c->x);
  ldy(c, rom_word(t->rom, TOP_SCORES_SCREENS + c->x));
  lda(c, 0x0082);
  ran(t, TOPS_SCREEN);
  c->pc = TOP_SCORES_SCREEN_PC;
}

void top_scores_ask(Wram* w, const Rom* rom, PortCpu* c, TopScoresWork* k) {
  TopScores t = {w, rom, c, k};
  push8(w, c, c->db);
  push16(w, c, 0x0082);
  plb(&t);
  wram_w16(w, TOP_SCORES_COUNT, 0);
  lda(c, 0x0009);
  c->a = asl16(c, c->a);
  c->a = asl16(c, c->a);
  ldx(c, c->a);
  push16(w, c, c->x);
  ran(&t, TOPS_HEAD);
  asks(&t, 0);
}

// The first player's has the tenth's row under it, which the check pulls
// before it asks about the second.
bool top_scores_file_known(const Wram* w, uint16_t s) {
  const uint16_t player = wram_r16(w, (uint16_t)(s + 1));
  const uint16_t ret = wram_r16(w, (uint16_t)(s + 3));
  if (player == 2) return ret == TOP_SCORES_RET_SECOND;
  return player == 0 && ret == TOP_SCORES_RET_FIRST &&
         wram_r16(w, (uint16_t)(s + 5)) == TOP_SCORES_LAST_ROW;
}

// `$82:BD35`: a digit of the score, in A with Z its own, put in the entry at
// Y. A nought before any other is a space.
static void digit(TopScores* t, uint16_t ret) {
  PortCpu* c = t->c;
  Wram* w = t->w;
  push16(w, c, ret);
  const bool nought = zero(c);
  if (nought) bit16(c, wram_r16(w, W_FILE_ROW));
  if (nought && !flag(c, PORT_P_N)) {
    lda(c, TOP_SCORES_SPACE);
    ran(t, TOPS_DIGIT_SPACE);
  } else {
    set_c(c, false);
    c->a = adc16(c, c->a, TOP_SCORES_NOUGHT);
    // `SEC : ROR $1E9A`: a digit has been seen.
    const uint16_t seen = wram_r16(w, W_FILE_ROW);
    set_c(c, (seen & 1u) != 0);
    const uint16_t now = (uint16_t)(seen >> 1 | 0x8000u);
    wram_w16(w, W_FILE_ROW, now);
    set_nz16(c, now);
    ran(t, nought ? TOPS_DIGIT_NOUGHT : TOPS_DIGIT);
  }
  wram_w8(w, (uint16_t)(W_CURSOR_ENTRY + c->y), (uint8_t)c->a);
  ldy(c, (uint16_t)(c->y + 1));
  (void)pull16(w, c);
}

// The entry's blanks become spaces, and its places 6 to 13 the score.
static void writes_score(TopScores* t) {
  PortCpu* c = t->c;
  Wram* w = t->w;
  // `SEP #$30`: X and Y lose their high bytes.
  set_p(c, (uint8_t)(c->p | PORT_P_M | PORT_P_X));
  c->y = 0;
  set_nz8(c, 0);
  c->a = (uint16_t)((c->a & 0xff00u) | TOP_SCORES_SPACE);
  set_nz8(c, TOP_SCORES_SPACE);
  ran(t, TOPS_FILE_HEAD);
  do {
    c->x = wram_r8(w, (uint16_t)(W_CURSOR_ENTRY + c->y));
    set_nz8(c, (uint8_t)c->x);
    ran(t, TOPS_BLANK_TEST);
    if (!took(t, c->x != 0)) {
      wram_w8(w, (uint16_t)(W_CURSOR_ENTRY + c->y), TOP_SCORES_SPACE);
      ran(t, TOPS_BLANK);
    }
    c->y = (uint16_t)((c->y + 1) & 0xffu);
    set_c(c, c->y >= TOP_SCORES_BLANKS);
    set_nz8(c, (uint8_t)(c->y - TOP_SCORES_BLANKS));
    ran(t, TOPS_BLANK_NEXT);
  } while (took(t, c->y != TOP_SCORES_BLANKS));
  set_p(c, (uint8_t)(c->p & ~(PORT_P_M | PORT_P_X)));

  // The score's last byte, the highest: the player's offset, which the
  // screen's caller pushed, doubled.
  lda(c, pull16(w, c));
  c->a = asl16(c, c->a);
  set_c(c, false);
  c->a = adc16(c, c->a, (uint16_t)(W_PLAYER_SCORE + 3));
  ldx(c, c->a);
  lda(c, 0x0003);
  wram_w16(w, W_FILE_AT, c->a);
  wram_w16(w, W_FILE_ROW, 0);
  ldy(c, TOP_SCORES_DIGITS_AT);
  ran(t, TOPS_SCORE_AT);
  uint16_t left;
  do {
    lda(c, wram_r16(w, c->x));
    for (int i = 0; i < 4; i++) lsr(c);
    lda(c, c->a & 0x000fu);
    ran(t, TOPS_HIGH);
    digit(t, 0xbc80u);
    lda(c, wram_r16(w, c->x) & 0x000fu);
    ran(t, TOPS_LOW_NIBBLE);
    digit(t, 0xbc89u);
    ldx(c, (uint16_t)(c->x - 1));
    left = (uint16_t)(wram_r16(w, W_FILE_AT) - 1);
    wram_w16(w, W_FILE_AT, left);
    set_nz16(c, left);
    ran(t, TOPS_NEXT_BYTE);
  } while (took(t, (left & 0x8000u) == 0));
}

// Its row: up from the tenth while the score is at least the one above.
// The rows from there down move down one, the scores and the names, and
// the score and the entry go in.
static void files_it(TopScores* t) {
  PortCpu* c = t->c;
  Wram* w = t->w;
  ldx(c, (uint16_t)(c->x + 1));
  push8(w, c, c->db);
  push16(w, c, 0x007e);
  plb(t);
  lda(c, 0x0009);
  c->a = asl16(c, c->a);
  c->a = asl16(c, c->a);
  ldy(c, c->a);
  ran(t, TOPS_ROW_HEAD);
  for (;;) {
    lda(c, wram_r16(w, (uint16_t)(c->x + 2)));
    cmp16(c, c->a, wram_r16(w, (uint16_t)(W_TOP_SCORES_SECOND + 2 + c->y)));
    ran(t, TOPS_ROW_HIGH);
    bool up;
    if (took(t, zero(c))) {
      lda(c, wram_r16(w, c->x));
      cmp16(c, c->a, wram_r16(w, (uint16_t)(W_TOP_SCORES_SECOND + c->y)));
      ran(t, TOPS_ROW_LOW);
    } else {
      ran(t, TOPS_ROW_HIGHER);
    }
    up = took(t, carry(c));
    if (!up) {
      ran(t, TOPS_ROW_BRA);
      took(t, true);
      break;
    }
    lda(c, c->y);
    set_c(c, true);
    c->a = sbc16(c, c->a, 0x0004);
    ldy(c, c->a);
    ran(t, TOPS_ROW_UP);
    if (!took(t, (c->y & 0x8000u) == 0)) break;
  }

  wram_w16(w, W_FILE_AT, c->x);
  lda(c, c->y);
  set_c(c, false);
  c->a = adc16(c, c->a, 0x0004);
  wram_w16(w, W_FILE_ROW, c->a);
  const uint16_t row = c->a;
  // The ninth's last byte, and the tenth's.
  lda(c, 0x0008);
  c->a = asl16(c, c->a);
  c->a = asl16(c, c->a);
  c->a = adc16(c, c->a, W_TOP_SCORES_SECOND);
  c->a = adc16(c, c->a, 0x0003);
  ldx(c, c->a);
  set_c(c, false);
  c->a = adc16(c, c->a, 0x0004);
  ldy(c, c->a);
  lda(c, 0x0009);
  c->a = asl16(c, c->a);
  c->a = asl16(c, c->a);
  set_c(c, true);
  c->a = sbc16(c, c->a, row);
  ran(t, TOPS_ROWS);
  if (!took(t, c->a == 0)) {
    PORT_COVER(top_scores_rows_move);
    lda(c, (uint16_t)(c->a - 1));
    ran(t, TOPS_DEC);
    moves(t, true);
  }

  ldx(c, wram_r16(w, W_FILE_AT));
  ldy(c, wram_r16(w, W_FILE_ROW));
  wram_w16(w, (uint16_t)(W_TOP_SCORES_SECOND + c->y), wram_r16(w, c->x));
  wram_w16(w, (uint16_t)(W_TOP_SCORES_SECOND + 2 + c->y),
           wram_r16(w, (uint16_t)(c->x + 2)));
  lda(c, 0x0009);
  c->a = asl16(c, c->a);
  ldx(c, c->a);
  ran(t, TOPS_SCORE_IN);

  // The names: the ninth's last byte, the tenth's, and the row's own.
  lda(c, (uint16_t)(rom_word(t->rom, TOP_SCORES_NAME_ROWS + c->x) - 1));
  wram_w16(w, W_FILE_AT, c->a);
  lda(c, c->y);
  lsr(c);
  ldx(c, c->a);
  lda(c, rom_word(t->rom, TOP_SCORES_NAME_ROWS + c->x));
  wram_w16(w, W_FILE_NAME, c->a);
  ldx(c, wram_r16(w, W_FILE_AT));
  set_c(c, false);
  c->a = adc16(c, c->x, TOP_SCORES_NAME_BYTES);
  ldy(c, c->a);
  lda(c, wram_r16(w, W_FILE_AT));
  set_c(c, true);
  c->a = sbc16(c, c->a, wram_r16(w, W_FILE_NAME));
  cmp16(c, c->a, 0xffff);
  ran(t, TOPS_NAMES);
  if (!took(t, zero(c))) moves(t, true);

  ldx(c, W_CURSOR_ENTRY);
  ldy(c, wram_r16(w, W_FILE_NAME));
  lda(c, TOP_SCORES_NAME_BYTES - 1);
  ran(t, TOPS_NAME_IN);
  moves(t, false);
  plb(t);
  plb(t);
  ran(t, TOPS_FILE_END);
}

void top_scores_file(Wram* w, const Rom* rom, PortCpu* c, TopScoresWork* k) {
  TopScores t = {w, rom, c, k};
  PORT_COVER(top_scores_filed);
  writes_score(&t);
  files_it(&t);
  if (pull16(w, c) == TOP_SCORES_RET_FIRST) {
    asks(&t, 1);
  } else {
    ends(&t);
  }
}
