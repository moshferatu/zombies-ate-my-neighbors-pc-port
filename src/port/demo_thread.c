// The demonstration, in the front end's thread -- see port/demo_thread.h.

#include "port/demo_thread.h"

#include "port/collide.h"  // W_PLAYER_CAPPED_COUNT, which is the lives
#include "port/coverage.h"
#include "port/demo.h"
#include "port/frontend.h"     // W_FRAME_COUNT
#include "port/level_scene.h"  // W_LEVEL_NUMBER
#include "port/rng.h"
#include "port/sched.h"  // W_RNG_HOLD, W_NMI_FLAGS

#define W_FRAME_COUNT_HIGH 0x0022u

static void lda(PortCpu* c, uint16_t v) {
  c->a = v;
  set_nz16(c, v);
}

static uint16_t page(const Wram* w, const PortCpu* c, uint8_t at) {
  return wram_r16(w, (uint16_t)(c->d + at));
}

bool demo_record_supported(const Wram* w, const PortCpu* c) {
  const uint16_t record = page(w, c, DEMO_DP_RECORD);
  return record >= 0x8000u && record <= 0xffffu - DEMO_RECORD_SONG - 3;
}

bool demo_return_supported(const Wram* w, const PortCpu* c) {
  const uint8_t p = wram_r8(w, (uint16_t)(c->s + 1));
  return (p & (PORT_P_M | PORT_P_X)) == 0 &&
         wram_r16(w, (uint16_t)(c->s + 2)) == DEMO_RETURN;
}

void demo_begin(Wram* w, const Rom* rom, PortCpu* c, DemoThreadWork* k) {
  wram_w16(w, W_RNG_HOLD, 1);
  wram_w16(w, W_DEMO_ENDED, 0);
  wram_w16(w, W_DEMO_AT, 0);
  wram_w16(w, W_DEMO_FRAME, 0);
  k->blocks[DM_HOLD]++;

  // All four played: all four again.
  const uint16_t played = wram_r16(w, W_DEMOS_PLAYED_COUNT);
  set_c(c, played >= DEMO_COUNT);
  k->blocks[DM_COUNT]++;
  if (played >= DEMO_COUNT) {
    PORT_COVER(demo_all_played);
    wram_w16(w, W_DEMOS_PLAYED_COUNT, 0);
    wram_w16(w, W_DEMOS_PLAYED, 0);
    wram_w16(w, W_DEMOS_PLAYED + 2, 0);
    k->blocks[DM_RESET]++;
  } else {
    k->blocks[DM_TAKEN]++;
  }

  // Drawn for until one not played yet comes up.
  uint16_t which;
  for (;;) {
    RngResult r;
    rng_next(w, flag(c, PORT_P_C), &r);
    set_c(c, r.c);
    set_v(c, r.v);
    k->blocks[DM_DRAW]++;
    if (r.v) k->draws_twice++;
    which = (uint16_t)(r.a & 0x0003u);
    k->blocks[DM_TEST]++;
    if (wram_r8(w, (uint16_t)(W_DEMOS_PLAYED + which)) == 0) break;
    PORT_COVER(demo_drawn_again);
    k->blocks[DM_TAKEN]++;
  }
  PORT_COVER(demo_drawn);
  wram_w8(w, (uint16_t)(W_DEMOS_PLAYED + which), 0xff);
  wram_w16(w, W_DEMOS_PLAYED_COUNT,
           (uint16_t)(wram_r16(w, W_DEMOS_PLAYED_COUNT) + 1));
  c->x = (uint16_t)(2 * which);
  c->y = (uint16_t)(4 * which);
  set_c(c, false);  // the second `ASL`'s
  wram_w16(w, W_DEMO_HUD, 1);
  wram_w16(w, W_SCORE_SLOT_SIDE, rom_word(rom, DEMO_SIDES + c->x));
  wram_w16(w, W_DEMO_RECORDING, rom_word(rom, DEMO_RECORDINGS + c->y));
  wram_w16(w, W_DEMO_RECORDING + 2, rom_word(rom, DEMO_RECORDINGS + 2 + c->y));
  lda(c, rom_word(rom, DEMO_LEVELS + c->x));
  wram_w16(w, W_LEVEL_NUMBER, c->a);
  k->blocks[DM_CHOSEN]++;
  k->blocks[c->db >= 0x80 ? DM_TABLES : DM_TABLES_SLOW]++;
  c->pc = DEMO_RECORD_ASK_PC;
}

// What the unpacking is given: the map's address, its bank on the stack.
void demo_unpack(Wram* w, const Rom* rom, PortCpu* c, DemoThreadWork* k) {
  PORT_COVER(demo_unpack);
  const uint16_t record = page(w, c, DEMO_DP_RECORD);
  push16(w, c, rom_word(rom, DEMO_RECORDS + record));
  c->a = rom_word(rom, DEMO_RECORDS + 2 + record);
  c->x = 0x007e;
  c->y = DEMO_UNPACK_TO;
  set_nz16(c, c->y);
  k->blocks[DM_UNPACK]++;
  c->pc = DEMO_UNPACK_CALL_PC;
}

void demo_unpacked(Wram* w, PortCpu* c, DemoThreadWork* k) {
  PORT_COVER(demo_unpacked);
  lda(c, pull16(w, c));
  k->blocks[DM_PULL]++;
  c->pc = DEMO_FADE_OUT_CALL_PC;
}

void demo_rest(PortCpu* c, DemoThreadWork* k) {
  PORT_COVER(demo_rest);
  lda(c, DEMO_REST_TICKS);
  k->blocks[DM_LDA_IMM]++;
  c->pc = DEMO_REST_CALL_PC;
}

void demo_scroll(Wram* w, PortCpu* c, DemoThreadWork* k) {
  PORT_COVER(demo_scroll);
  lda(c, page(w, c, DEMO_DP_SCROLL_JOB));
  k->blocks[DM_LDA_DP]++;
  c->pc = DEMO_SCROLL_CALL_PC;
}

// The song's two words, A and X.
void demo_song(Wram* w, const Rom* rom, PortCpu* c, DemoThreadWork* k) {
  PORT_COVER(demo_song);
  const uint32_t at = DEMO_RECORDS + DEMO_RECORD_SONG + page(w, c, DEMO_DP_RECORD);
  // `PHA : ... : TAX : PLA`.
  push16(w, c, rom_word(rom, at));
  c->x = rom_word(rom, at + 2);
  lda(c, pull16(w, c));
  k->blocks[DM_SONG]++;
  c->pc = DEMO_SONG_CALL_PC;
}

void demo_lives(Wram* w, PortCpu* c, DemoThreadWork* k) {
  PORT_COVER(demo_lives);
  wram_w16(w, W_PLAYER_CAPPED_COUNT, 0);
  k->blocks[DM_LIVES]++;
  c->pc = DEMO_LIVES_WAI_PC;
}

void demo_clear(Wram* w, PortCpu* c, DemoThreadWork* k) {
  PORT_COVER(demo_clear);
  set_c(c, false);
  wram_w16(w, W_RNG_STATE, 0);
  wram_w16(w, W_FRAME_COUNT, 0);
  wram_w16(w, W_FRAME_COUNT_HIGH, 0);
  wram_w16(w, W_NMI_FLAGS, 0);
  wram_w16(w, W_NMI_FRAME_COUNTER, 0);
  wram_w16(w, W_DEMO_AT, 0);
  lda(c, 0);
  k->blocks[DM_CLEAR]++;
  c->pc = DEMO_CLEAR_SAVE_PC;
}

void demo_job_ask(PortCpu* c, DemoThreadWork* k) {
  PORT_COVER(demo_job_ask);
  c->a = DEMO_JOB_PC & 0xffffu;
  c->y = DEMO_JOB_BANK;
  set_nz16(c, c->y);
  k->blocks[DM_JOB_ASK]++;
  c->pc = DEMO_JOB_ASK_CALL_PC;
}

// Carry set: it was ended.
void demo_played(PortCpu* c, DemoThreadWork* k) {
  k->blocks[DM_PLAYED]++;
  if (flag(c, PORT_P_C)) {
    PORT_COVER(demo_played_ended);
    c->pc = DEMO_PLAYED_ENDED_PC;
    return;
  }
  PORT_COVER(demo_played_out);
  k->blocks[DM_TAKEN]++;
  c->pc = DEMO_PLAYED_OVER_PC;
}

// The compare's flags are kept on the stack for the front end.
void demo_ended(Wram* w, PortCpu* c, DemoThreadWork* k) {
  const uint16_t ended = wram_r16(w, W_DEMO_ENDED);
  lda(c, ended);
  cmp16(c, ended, 1);
  push8(w, c, c->p);
  k->blocks[DM_ENDED]++;
  if (flag(c, PORT_P_C)) {
    PORT_COVER(demo_ended_by_pad);
    k->blocks[DM_TAKEN]++;
    c->pc = DEMO_ENDED_ON_PC;
    return;
  }
  PORT_COVER(demo_ended_scores);
  c->pc = DEMO_SCORES_CALL_PC;
}

void demo_over(Wram* w, PortCpu* c, DemoThreadWork* k) {
  PORT_COVER(demo_over);
  wram_w16(w, W_LEVEL_NUMBER, 1);
  wram_w16(w, W_SCORE_SLOT_SIDE, 0);
  lda(c, DEMO_OVER_TICKS);
  k->blocks[DM_OVER]++;
  c->pc = DEMO_OVER_CALL_PC;
}

void demo_return(Wram* w, PortCpu* c, DemoThreadWork* k) {
  c->p = pull8(w, c);
  c->pc = (c->pc & 0xff0000u) | (uint16_t)(pull16(w, c) + 1);
  k->blocks[DM_RETURN]++;
  k->blocks[DM_PLAYED]++;  // the front end's `BCC`
  if (flag(c, PORT_P_C)) {
    PORT_COVER(demo_back_title);
    c->pc = DEMO_BACK_TITLE_PC;
    return;
  }
  PORT_COVER(demo_back_start);
  k->blocks[DM_TAKEN]++;
  c->pc = DEMO_BACK_START_PC;
}
