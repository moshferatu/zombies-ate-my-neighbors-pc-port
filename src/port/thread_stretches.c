// Stretches of four threads, between their calls -- see
// port/thread_stretches.h.

#include "port/thread_stretches.h"

#include "port/bcd.h"
#include "port/bodies.h"            // W_NEIGHBOURS_LEFT
#include "port/coverage.h"
#include "port/follower.h"
#include "port/frontend.h"            // the frame count
#include "port/level_scene.h"       // W_LEVEL_NUMBER
#include "port/martian.h"
#include "port/neighbour_thread.h"  // W_SAVED_COUNT_A
#include "port/oam.h"               // the display record's fields
#include "port/player_frame.h"
#include "port/apu.h"               // W_APU_SOUNDS
#include "port/neighbours.h"        // W_TOURISTS_TURN
#include "port/werewolf.h"
#include "port/objects.h"           // W_VICTIM_GATE
#include "port/weeds.h"

const ThreadArgument THREAD_ARGUMENTS[THREAD_ARGUMENT_COUNT] = {
    {0x81d183u, WEED_SEED_TICKS_APART},
    {0x81d1adu, WEED_SEED_TICKS_APART},
    {0x81d1d7u, WEED_SEED_TICKS_APART},
    {0x82f4b1u, 0x0001},  // the follower's first sleep
    {0x808516u, 0x000a},  // ten ticks before a level fades in
    {0x8199c2u, 0x000c},  // what a martian's shot waits
    {0x80d069u, 0xd0e0},  // the pictures of a player hurt
    {0x81ec72u, 0xee6b},  // two lists of pictures I have not read the use of
    {0x81f30eu, 0xf368},
};

const ThreadSetter THREAD_SETTERS[THREAD_SETTER_COUNT] = {
    {0x819215u, 0x921b, 0x12},  // two states of the thread at `$81:92C2`
    {0x819250u, 0x9256, 0x12},
};

const ThreadPair THREAD_PAIRS[THREAD_PAIR_COUNT] = {
    {0x8085d3u, 0x8000, 0x0084},  // what the layers are reset with
    {0x819b30u, 0x9b6b, 0x0081},  // the martian's handler
    {0x80c2deu, 0xc2ab, 0x0080},  // the thread of the display's top
};

const WeedThrow WEED_THROWS[WEED_THROW_COUNT] = {
    {0x81d168u, 0x0000, -8, 0x81d17fu, TS_WEED_THROW_A},
    {0x81d18au, 0xa1de, 8, 0x81d1a9u, TS_WEED_THROW_B},
    {0x81d1b4u, 0xa19d, -2, 0x81d1d3u, TS_WEED_THROW_C},
};

const WeedKind WEED_KINDS[WEED_KIND_COUNT] = {
    {0x81d2f1u, 0x1400},
    {0x81d2f9u, 0x0800},
};

static uint16_t page(const Wram* w, const PortCpu* c, uint8_t at) {
  return wram_r16(w, (uint16_t)(c->d + at));
}

static void set_page(Wram* w, const PortCpu* c, uint8_t at, uint16_t v) {
  wram_w16(w, (uint16_t)(c->d + at), v);
}

static void lda(PortCpu* c, uint16_t v) {
  c->a = v;
  set_nz16(c, v);
}

static void ldx(PortCpu* c, uint16_t v) {
  c->x = v;
  set_nz16(c, v);
}

static void ldy(PortCpu* c, uint16_t v) {
  c->y = v;
  set_nz16(c, v);
}

static void ran(ThreadStretchWork* k, int block) { k->blocks[block]++; }

// A branch on a flag, the first thing in a stretch.
static bool branches(const PortCpu* c, uint8_t bit, ThreadStretchWork* k) {
  ran(k, TS_BRANCH);
  if (!flag(c, bit)) return false;
  ran(k, TS_TAKEN);
  return true;
}

// The level's load up by what a thing weighs.
static void load_up(Wram* w, PortCpu* c, uint16_t load) {
  set_c(c, false);
  lda(c, adc16(c, wram_r16(w, W_SPAWN_LOAD), load));
  wram_w16(w, W_SPAWN_LOAD, c->a);
}

// `JSR` to a routine that is done here: its return is left on the stack as
// the ROM leaves it.
static void jsr_and_back(Wram* w, PortCpu* c, uint16_t jsr_at,
                         ThreadStretchWork* k) {
  push16(w, c, (uint16_t)(jsr_at + 2));
  pull16(w, c);
  ran(k, TS_JSR);
}

bool thread_stretch_needs_met(const Wram* w, ThreadStretchNeeds needs,
                              const PortCpu* c) {
  switch (needs) {
    case TS_NEEDS_NOTHING:
      return true;
    case TS_NEEDS_RECORD:
      return page(w, c, WEED_DP_RECORD) < 0x1f00;
    case TS_NEEDS_A:
      return c->a < 0x1f00;
    case TS_NEEDS_LEVEL: {
      // The two words are read from the cartridge.
      const uint32_t at = (LEVEL_RECORD_WORDS & 0xffffu) + page(w, c, MAIN_DP_LEVEL);
      return at >= 0x8000u && at + 3 <= 0xffffu;
    }
    case TS_NEEDS_COUNTS:
      // Small enough that the decimal sum cannot overflow.
      return wram_r16(w, W_SAVED_COUNT_A) < 0x4000 &&
             wram_r16(w, W_SAVED_COUNT_B) < 0x4000;
    case TS_NEEDS_PLACE:
      return wram_r16(w, (uint16_t)(c->s + 1)) == MARTIAN_PLACE_RETURN;
    case TS_NEEDS_WOLF: {
      const uint16_t to = wram_r16(w, (uint16_t)(c->s + 1));
      return page(w, c, WEREWOLF_DP_RECORD) < 0x1f00 &&
             (to == WEREWOLF_LOOP_RETURN || to == WEREWOLF_MAKE_RETURN);
    }
  }
  return false;
}

// --- an argument or two, and a call ------------------------------------------

void thread_argument(Wram* w, const Rom* rom, PortCpu* c,
                     ThreadStretchWork* k) {
  (void)w;
  (void)rom;
  for (int i = 0; i < THREAD_ARGUMENT_COUNT; i++) {
    if (THREAD_ARGUMENTS[i].pc != c->pc) continue;
    lda(c, THREAD_ARGUMENTS[i].value);
    ran(k, TS_LDA_IMM);
    c->pc += 3;
    return;
  }
}

void thread_pair(Wram* w, const Rom* rom, PortCpu* c, ThreadStretchWork* k) {
  (void)w;
  (void)rom;
  for (int i = 0; i < THREAD_PAIR_COUNT; i++) {
    if (THREAD_PAIRS[i].pc != c->pc) continue;
    c->a = THREAD_PAIRS[i].a;
    ldy(c, THREAD_PAIRS[i].y);
    ran(k, TS_LDA_LDY_IMM);
    c->pc += 6;
    return;
  }
}

// --- the weed ----------------------------------------------------------------

void weed_snap_aim(Wram* w, const Rom* rom, PortCpu* c, ThreadStretchWork* k) {
  (void)rom;
  c->a = WEED_ANYBODY;
  c->x = page(w, c, WEED_DP_ROOT_X);
  ldy(c, page(w, c, WEED_DP_ROOT_Y));
  ran(k, TS_WEED_AIM);
  c->pc = WEED_SNAP_ASK_PC;
}

void weed_snap_show(Wram* w, const Rom* rom, PortCpu* c, ThreadStretchWork* k) {
  (void)rom;
  set_page(w, c, WEED_DP_TARGET, c->a);
  lda(c, WEED_SNAP_PICTURES);
  ran(k, TS_WEED_SHOW);
  c->pc = WEED_SNAP_SHOW_PC;
}

// A seed is handed where it begins, a pixel above the root and so far
// across, and who it is thrown at.
void weed_throw(Wram* w, const Rom* rom, PortCpu* c, ThreadStretchWork* k) {
  (void)rom;
  for (int i = 0; i < WEED_THROW_COUNT; i++) {
    const WeedThrow* t = &WEED_THROWS[i];
    if (t->pc != c->pc) continue;
    if (t->picture) {
      PORT_COVER(weed_throw_shows);
      c->y = page(w, c, WEED_DP_RECORD);
      wram_w16(w, (uint16_t)(c->y + ACTOR_META), t->picture);
    } else {
      PORT_COVER(weed_throw_first);
    }
    const uint16_t root_x = page(w, c, WEED_DP_ROOT_X);
    uint16_t x;
    if (t->across < 0) {
      set_c(c, true);
      x = sbc16(c, root_x, (uint16_t)-t->across);
    } else {
      set_c(c, false);
      x = adc16(c, root_x, (uint16_t)t->across);
    }
    set_page(w, c, WEED_DP_MARK_X, x);
    set_page(w, c, WEED_DP_MARK_Y, (uint16_t)(page(w, c, WEED_DP_ROOT_Y) - 1));
    set_page(w, c, WEED_SEED_DP_PLACED_AT, page(w, c, WEED_DP_TARGET));
    c->a = WEED_SEED_THREAD;
    ldy(c, WEED_SEED_THREAD_BANK);
    ran(k, t->block);
    c->pc = t->spawn_pc;
    return;
  }
}

void weed_snap_rest(Wram* w, const Rom* rom, PortCpu* c, ThreadStretchWork* k) {
  (void)rom;
  c->y = page(w, c, WEED_DP_RECORD);
  wram_w16(w, (uint16_t)(c->y + ACTOR_META), WEED_PICTURE);
  // The state's first pass is its last instructions here: one counted off.
  set_page(w, c, WEED_DP_REST_LEFT, WEED_REST_PASSES - 1);
  set_page(w, c, WEED_DP_STATE, WEED_STATE_REST);
  c->a = WEED_STATE_REST;
  set_nz16(c, WEED_REST_PASSES - 1);
  ran(k, TS_WEED_REST);
  c->pc = WEED_SNAP_RTS_PC;
}

// `STZ $36 : LDA #$0002` and the loop's sleep.
static void weed_sleeps(Wram* w, PortCpu* c, ThreadStretchWork* k) {
  set_page(w, c, WEED_DP_CLEARED, 0);
  lda(c, WEED_YIELD_TICKS);
  ran(k, TS_WEED_AGAIN);
  c->pc = WEED_YIELD_PC;
}

void weed_pass_end(Wram* w, const Rom* rom, PortCpu* c, ThreadStretchWork* k) {
  (void)rom;
  lda(c, page(w, c, WEED_DP_FATE));
  ran(k, TS_WEED_FATE);
  if (c->a != 0) {
    PORT_COVER(weed_pass_fated);
    c->pc = WEED_FATE_PC;
    return;
  }
  PORT_COVER(weed_pass_sleeps);
  ran(k, TS_TAKEN);
  weed_sleeps(w, c, k);
}

void weed_fate(Wram* w, const Rom* rom, PortCpu* c, ThreadStretchWork* k) {
  (void)w;
  (void)rom;
  ran(k, TS_BRANCH);
  if (flag(c, PORT_P_N)) {
    PORT_COVER(weed_fate_killed);
    c->pc = WEED_KILLED_PC;
    return;
  }
  PORT_COVER(weed_fate_ends);
  ran(k, TS_TAKEN);
  c->pc = WEED_END_PC;
}

void weed_died(Wram* w, const Rom* rom, PortCpu* c, ThreadStretchWork* k) {
  (void)rom;
  lda(c, page(w, c, WEED_DP_ENTRY));
  ran(k, TS_LDA_DP);
  c->pc = WEED_MARK_PC;
}

void weed_enter(Wram* w, const Rom* rom, PortCpu* c, ThreadStretchWork* k) {
  (void)rom;
  for (int i = 0; i < WEED_KIND_COUNT; i++) {
    if (WEED_KINDS[i].pc != c->pc) continue;
    set_page(w, c, WEED_DP_TILE_BITS, WEED_KINDS[i].tile_bits);
    ran(k, TS_WEED_KIND);
    load_up(w, c, WEED_LOAD);
    ran(k, TS_WEED_ENTER);
    c->pc = WEED_ALLOC_CALL_PC;
    return;
  }
}

// The record it was given, and its page: all four tips at the root, which is
// where the thread that began it said.
void weed_made(Wram* w, const Rom* rom, PortCpu* c, ThreadStretchWork* k) {
  (void)rom;
  const uint16_t record = c->a;
  set_page(w, c, WEED_DP_RECORD, record);
  const uint16_t x = page(w, c, WEED_DP_MARK_X);
  const uint16_t y = page(w, c, WEED_DP_MARK_Y);
  set_page(w, c, WEED_DP_ROOT_X, x);
  set_page(w, c, WEED_DP_ROOT_Y, y);
  for (uint8_t tip = 0; tip < 4; tip++) {
    set_page(w, c, (uint8_t)(WEED_DP_TIPS + 4 * tip), x);
    set_page(w, c, (uint8_t)(WEED_DP_TIPS + 4 * tip + 2), y);
  }
  wram_w16(w, (uint16_t)(record + ACTOR_X), x);
  wram_w16(w, (uint16_t)(record + ACTOR_Z), 0);
  wram_w16(w, (uint16_t)(record + ACTOR_Y), y);
  wram_w16(w, (uint16_t)(record + ACTOR_META), WEED_PICTURE);
  wram_w16(w, (uint16_t)(record + ACTOR_META_BANK), WEED_PICTURE_BANK);
  wram_w16(w, (uint16_t)(record + ACTOR_THREAD), wram_r16(w, W_SCHED_CUR_TASK));
  wram_w16(w, (uint16_t)(record + ACTOR_COLLIDE_ID), WEED_COLLIDE_ID);
  wram_w16(w, (uint16_t)(record + ACTOR_ATTR), WEED_ATTR);
  wram_w16(w, record, (uint16_t)(wram_r16(w, record) | ACTOR_DRAW));
  set_page(w, c, WEED_DP_HEALTH, WEED_HEALTH);
  set_page(w, c, WEED_DP_REST_LEFT, 0);
  set_page(w, c, WEED_DP_FATE, 0);
  set_page(w, c, WEED_DP_CLEARED, 0);
  set_page(w, c, WEED_DP_FREEZE_HITS, 0);
  set_page(w, c, WEED_DP_ARM, 0);
  c->a = WEED_HANDLER;
  ldy(c, WEED_BANK);
  ran(k, TS_WEED_MADE);
  c->pc = WEED_HANDLER_PC;
}

void weed_begun(Wram* w, const Rom* rom, PortCpu* c, ThreadStretchWork* k) {
  (void)rom;
  jsr_and_back(w, c, (uint16_t)(WEED_BEGUN_PC & 0xffffu), k);
  set_page(w, c, WEED_DP_STATE, WEED_STATE_GROW);
  ran(k, TS_WEED_GROWS);
  weed_sleeps(w, c, k);
}

// --- the follower ------------------------------------------------------------

static void follower_where(const Wram* w, PortCpu* c, ThreadStretchWork* k) {
  c->x = page(w, c, FOLLOWER_DP_X);
  ldy(c, page(w, c, FOLLOWER_DP_Y));
  ran(k, TS_LDX_LDY_DP);
}

void follower_look(Wram* w, const Rom* rom, PortCpu* c, ThreadStretchWork* k) {
  (void)rom;
  follower_where(w, c, k);
  c->pc = FOLLOWER_EDGE_PC;
}

void follower_on_level(Wram* w, const Rom* rom, PortCpu* c,
                       ThreadStretchWork* k) {
  (void)rom;
  if (branches(c, PORT_P_C, k)) {
    PORT_COVER(follower_look_off_level);
    c->pc = FOLLOWER_RTS_PC;
    return;
  }
  PORT_COVER(follower_look_on_level);
  follower_where(w, c, k);
  c->pc = FOLLOWER_TILE_ASK_PC;
}

void follower_tile(Wram* w, const Rom* rom, PortCpu* c, ThreadStretchWork* k) {
  (void)w;
  (void)rom;
  lda(c, (uint16_t)(c->a & FOLLOWER_GROUND));
  ran(k, TS_FOLLOWER_TILE);
  if (c->a == 0) {
    PORT_COVER(follower_tile_plain);
    ran(k, TS_TAKEN);
    c->pc = FOLLOWER_RTS_PC;
    return;
  }
  PORT_COVER(follower_tile_ground);
  lda(c, 0x0001);
  ran(k, TS_LDA_IMM);
  c->pc = FOLLOWER_MARK_SLEEP_PC;
}

void follower_mark(Wram* w, const Rom* rom, PortCpu* c, ThreadStretchWork* k) {
  (void)rom;
  c->a = FOLLOWER_TILE;
  c->x = page(w, c, FOLLOWER_DP_TILE_X);
  ldy(c, page(w, c, FOLLOWER_DP_TILE_Y));
  ran(k, TS_FOLLOWER_MARK);
  c->pc = FOLLOWER_PUT_PC;
}

void follower_room(Wram* w, const Rom* rom, PortCpu* c, ThreadStretchWork* k) {
  (void)rom;
  if (branches(c, PORT_P_C, k)) {
    PORT_COVER(follower_no_room);
    c->pc = FOLLOWER_NO_ROOM_PC;
    return;
  }
  PORT_COVER(follower_has_room);
  load_up(w, c, FOLLOWER_LOAD);
  ran(k, TS_FOLLOWER_LOAD);
  c->pc = FOLLOWER_MAKE_PC;
}

// The record it was given, and its page. The word at `$00` says which side
// of what it follows it is put, and it is drawn facing that way.
void follower_made(Wram* w, const Rom* rom, PortCpu* c, ThreadStretchWork* k) {
  (void)rom;
  const uint16_t record = c->a;
  set_page(w, c, FOLLOWER_DP_RECORD, record);
  uint16_t flags = wram_r16(w, record);
  ran(k, TS_FOLLOWER_SIDE);
  if (page(w, c, FOLLOWER_DP_SIDE_GIVEN) & 0x8000u) {
    PORT_COVER(follower_made_left);
    flags &= (uint16_t)~FOLLOWER_FLIP;
    c->x = 0xffff;
    ran(k, TS_FOLLOWER_LEFT);
  } else {
    PORT_COVER(follower_made_right);
    ran(k, TS_TAKEN);
    flags |= FOLLOWER_FLIP;
    c->x = 0x0000;
    ran(k, TS_FOLLOWER_RIGHT);
  }
  set_page(w, c, FOLLOWER_DP_SIDE, c->x);
  set_page(w, c, FOLLOWER_DP_PLACE, 0);
  wram_w16(w, (uint16_t)(record + ACTOR_Z), 0);
  set_page(w, c, FOLLOWER_DP_PLACE_LEFT, FOLLOWER_PLACE_FRAMES);
  wram_w16(w, (uint16_t)(record + ACTOR_META), FOLLOWER_PICTURE);
  wram_w16(w, (uint16_t)(record + ACTOR_META_BANK), FOLLOWER_PICTURE_BANK);
  wram_w16(w, (uint16_t)(record + ACTOR_THREAD), wram_r16(w, W_SCHED_CUR_TASK));
  wram_w16(w, (uint16_t)(record + ACTOR_COLLIDE_ID), FOLLOWER_COLLIDE_ID);
  set_page(w, c, FOLLOWER_DP_PICTURE_LEFT, 0);
  set_page(w, c, FOLLOWER_DP_PICTURE, 0);
  set_page(w, c, FOLLOWER_DP_LEFT, 0);
  wram_w16(w, record, (uint16_t)(flags | ACTOR_DRAW | ACTOR_PRIORITY_TOP));
  set_page(w, c, FOLLOWER_DP_HIT, 0);
  c->a = FOLLOWER_HANDLER;
  ldy(c, FOLLOWER_BANK);
  ran(k, TS_FOLLOWER_MADE);
  c->pc = FOLLOWER_HANDLER_PC;
}

// Something touched it: drawn, with its last picture, for two frames.
void follower_hit(Wram* w, const Rom* rom, PortCpu* c, ThreadStretchWork* k) {
  (void)rom;
  c->y = page(w, c, FOLLOWER_DP_RECORD);
  wram_w16(w, c->y, (uint16_t)(wram_r16(w, c->y) | ACTOR_DRAW));
  wram_w16(w, (uint16_t)(c->y + ACTOR_META), FOLLOWER_HIT_PICTURE);
  lda(c, FOLLOWER_HIT_FRAMES);
  ran(k, TS_FOLLOWER_HIT);
  c->pc = FOLLOWER_HIT_SLEEP_PC;
}

// --- the main thread ---------------------------------------------------------

// `LDA $18 : BNE`: has the sound's bank been sent?
static void main_sound(Wram* w, PortCpu* c, ThreadStretchWork* k) {
  lda(c, page(w, c, MAIN_DP_SOUND_SET));
  ran(k, TS_MAIN_SOUND_TEST);
  if (c->a != 0) {
    PORT_COVER(main_sound_was_set);
    ran(k, TS_TAKEN);
    c->pc = MAIN_FIRST_RTL_PC;
    return;
  }
  PORT_COVER(main_sound_unset);
  lda(c, 0x0001);
  wram_w16(w, W_MAIN_1E7E, c->a);
  ran(k, TS_MAIN_SOUND);
  c->pc = MAIN_SOUND_ASK_PC;
}

void main_reset(Wram* w, const Rom* rom, PortCpu* c, ThreadStretchWork* k) {
  wram_w16(w, W_MAIN_1B80, 0x2000);
  wram_w16(w, W_NEIGHBOURS_LEFT, MAIN_NEIGHBOURS);
  wram_w16(w, W_VICTIM_GATE, MAIN_NEIGHBOURS);
  wram_w16(w, W_LEVEL_NUMBER, rom_word(rom, MAIN_FIRST_LEVEL_OPERAND));
  lda(c, wram_r16(w, W_SCORES_SET));
  ran(k, TS_MAIN_RESET);
  if (c->a == 0) {
    PORT_COVER(main_scores_unset);
    c->pc = MAIN_SCORES_ASK_PC;
    return;
  }
  PORT_COVER(main_scores_were_set);
  ran(k, TS_TAKEN);
  main_sound(w, c, k);
}

void main_scores_set(Wram* w, const Rom* rom, PortCpu* c,
                     ThreadStretchWork* k) {
  (void)rom;
  wram_w16(w, W_SCORES_SET, 0x0001);
  ran(k, TS_MAIN_SCORES_SET);
  main_sound(w, c, k);
}

void main_sound_set(Wram* w, const Rom* rom, PortCpu* c, ThreadStretchWork* k) {
  (void)rom;
  const uint16_t set = (uint16_t)(page(w, c, MAIN_DP_SOUND_SET) + 1);
  set_page(w, c, MAIN_DP_SOUND_SET, set);
  set_nz16(c, set);
  ran(k, TS_MAIN_SOUND_SET);
  c->pc = MAIN_FIRST_RTL_PC;
}

void level_record_ask(Wram* w, const Rom* rom, PortCpu* c,
                      ThreadStretchWork* k) {
  const uint16_t level = page(w, c, MAIN_DP_LEVEL);
  c->y = bus_r16(w, rom, LEVEL_RECORD_WORDS + level);
  c->x = bus_r16(w, rom, LEVEL_RECORD_WORDS + 2 + level);
  lda(c, level);
  ran(k, TS_LEVEL_RECORD);
  c->pc = LEVEL_RECORD_PC;
}

void level_faded(Wram* w, const Rom* rom, PortCpu* c, ThreadStretchWork* k) {
  (void)rom;
  wram_w16(w, W_LEVELS_FADED, (uint16_t)(wram_r16(w, W_LEVELS_FADED) + 1));
  lda(c, 0x0002);
  ran(k, TS_LEVEL_FADED);
  c->pc = MAIN_FRAME_SLEEP_PC;
}

// A level begins with as many neighbours as the two players have saved.
void level_count(Wram* w, const Rom* rom, PortCpu* c, ThreadStretchWork* k) {
  (void)rom;
  wram_w16(w, W_SPAWN_LOAD, 0);
  bool carry = false, adjusted = false;
  const uint16_t saved = bcd_add16(wram_r16(w, W_SAVED_COUNT_A),
                                   wram_r16(w, W_SAVED_COUNT_B), &carry,
                                   &adjusted);
  set_c(c, carry);
  set_v(c, false);
  wram_w16(w, W_VICTIM_GATE, saved);
  wram_w16(w, W_NEIGHBOURS_LEFT, saved);
  c->a = 0x0000;
  ldx(c, 0x0000);
  ran(k, TS_LEVEL_COUNT);
  c->pc = LEVEL_COUNT_ASK_PC;
}

void level_count_asked(Wram* w, const Rom* rom, PortCpu* c,
                       ThreadStretchWork* k) {
  (void)w;
  (void)rom;
  c->a = 0x0000;
  ldx(c, 0x0002);
  ran(k, TS_LDA_LDX_IMM);
  c->pc = LEVEL_COUNT_ASK_B_PC;
}

void cache_start(Wram* w, const Rom* rom, PortCpu* c, ThreadStretchWork* k) {
  (void)rom;
  wram_w16(w, W_VISIBLE_ACTOR_COUNT, 0);
  c->a = CACHE_THREAD;
  ldy(c, CACHE_THREAD_BANK);
  ran(k, TS_CACHE_START);
  c->pc = THREAD_START_PC;
}

// --- three that are not the main thread's -----------------------------------

// `REP #$30`, which the thread that comes here has done already.
void frontend_enter(Wram* w, const Rom* rom, PortCpu* c, ThreadStretchWork* k) {
  (void)w;
  (void)rom;
  c->p &= (uint8_t)~(PORT_P_M | PORT_P_X);
  ran(k, TS_REP);
  c->pc = FRONTEND_LAYERS_PC;
}

void player_keep(Wram* w, const Rom* rom, PortCpu* c, ThreadStretchWork* k) {
  (void)rom;
  set_page(w, c, PLAYER_DP_KEPT, c->a);
  ran(k, TS_STA_DP);
  c->pc = PLAYER_ALLOC_PC;
}

static void spawner_sleeps(Wram* w, PortCpu* c, ThreadStretchWork* k) {
  set_page(w, c, SPAWNER_DP_CLEARED, 0);
  lda(c, 0x0001);
  ran(k, TS_SPAWNER_WAIT);
  c->pc = SPAWNER_SLEEP_PC;
}

// It starts on an odd frame: on an even one it sleeps a frame first.
void spawner_tick(Wram* w, const Rom* rom, PortCpu* c, ThreadStretchWork* k) {
  (void)rom;
  lda(c, (uint16_t)(wram_r16(w, W_FRAME_COUNT) & 0x0001));
  ran(k, TS_SPAWNER_TICK);
  if (c->a == 0) {
    PORT_COVER(spawner_tick_even);
    lda(c, 0x0001);
    ran(k, TS_LDA_IMM);
    c->pc = SPAWNER_TICK_SLEEP_PC;
    return;
  }
  PORT_COVER(spawner_tick_odd);
  ran(k, TS_TAKEN);
  spawner_sleeps(w, c, k);
}

void spawner_wait(Wram* w, const Rom* rom, PortCpu* c, ThreadStretchWork* k) {
  (void)rom;
  spawner_sleeps(w, c, k);
}

// --- the martian -------------------------------------------------------------

void martian_enter(Wram* w, const Rom* rom, PortCpu* c, ThreadStretchWork* k) {
  (void)rom;
  load_up(w, c, MARTIAN_LOAD);
  ran(k, TS_MARTIAN_ENTER);
  c->pc = MARTIAN_BEGIN_CALL_PC;
}

void martian_begun(Wram* w, const Rom* rom, PortCpu* c, ThreadStretchWork* k) {
  (void)rom;
  jsr_and_back(w, c, (uint16_t)(MARTIAN_BEGUN_PC & 0xffffu), k);
  set_page(w, c, MARTIAN_DP_STATE, MARTIAN_STATE_WALK);
  ran(k, TS_MARTIAN_WALKS);
  lda(c, MARTIAN_YIELD_TICKS);
  ran(k, TS_LDA_IMM);
  c->pc = MARTIAN_FIRST_SLEEP_PC;
}

// --- the werewolf ------------------------------------------------------------

// Its record is asked for, by a `JSR` whose return stays on the stack.
static void werewolf_make(Wram* w, PortCpu* c, ThreadStretchWork* k) {
  push16(w, c, WEREWOLF_MAKE_RETURN);
  ran(k, TS_JSR);
  c->pc = WEREWOLF_MAKE_PC;
}

void werewolf_enter(Wram* w, const Rom* rom, PortCpu* c,
                    ThreadStretchWork* k) {
  (void)rom;
  lda(c, wram_r16(w, W_TOURISTS_TURN));
  ran(k, TS_WOLF_ASK);
  if (c->a == 0) {
    PORT_COVER(werewolf_not_due);
    c->pc = WEREWOLF_NONE_PC;
    return;
  }
  PORT_COVER(werewolf_entered);
  ran(k, TS_TAKEN);
  load_up(w, c, WEREWOLF_LOAD);
  ran(k, TS_MARTIAN_ENTER);  // the same four instructions
  c->pc = WEREWOLF_HOWL_CHECK_PC;
}

// `$81:AC05`: see the header for why this begins where it does.
void werewolf_howl_check(Wram* w, const Rom* rom, PortCpu* c,
                         ThreadStretchWork* k) {
  (void)rom;
  lda(c, wram_r16(w, W_APU_SOUNDS));
  cmp16(c, c->a, WEREWOLF_SAMPLE_SET);
  ran(k, TS_WOLF_HEARD);
  if (c->a == WEREWOLF_SAMPLE_SET) {
    PORT_COVER(werewolf_howled);
    lda(c, WEREWOLF_HOWL);
    ran(k, TS_LDA_IMM);
    c->pc = WEREWOLF_HOWL_CALL_PC;
    return;
  }
  PORT_COVER(werewolf_unheard);
  ran(k, TS_TAKEN);
  werewolf_make(w, c, k);
}

void werewolf_howled(Wram* w, const Rom* rom, PortCpu* c,
                     ThreadStretchWork* k) {
  (void)rom;
  werewolf_make(w, c, k);
}

void werewolf_made(Wram* w, const Rom* rom, PortCpu* c, ThreadStretchWork* k) {
  (void)rom;
  PORT_COVER(werewolf_dressed);
  set_page(w, c, WEREWOLF_DP_X, page(w, c, WEREWOLF_DP_PLACED_X));
  set_page(w, c, WEREWOLF_DP_Y, page(w, c, WEREWOLF_DP_PLACED_Y));
  ldy(c, page(w, c, WEREWOLF_DP_RECORD));
  const uint16_t record = c->y;
  wram_w16(w, (uint16_t)(record + ACTOR_META), WEREWOLF_FIRST_PICTURE);
  wram_w16(w, (uint16_t)(record + ACTOR_META_BANK), 0x0090);
  wram_w16(w, (uint16_t)(record + ACTOR_ATTR), WEREWOLF_START_ATTR);
  wram_w16(w, (uint16_t)(record + ACTOR_FLAGS),
           (uint16_t)(wram_r16(w, (uint16_t)(record + ACTOR_FLAGS)) |
                      ACTOR_DRAW));
  wram_w16(w, (uint16_t)(record + ACTOR_COLLIDE_ID), WEREWOLF_COLLIDE_ID);
  set_page(w, c, WEREWOLF_DP_PICTURE, 0);
  set_page(w, c, WEREWOLF_DP_PICTURE_WAIT, 0);
  set_page(w, c, WEREWOLF_DP_BLOW, 0xffff);
  set_page(w, c, WEREWOLF_DP_QUARRY, 0xffff);
  set_page(w, c, WEREWOLF_DP_7E, 0);
  set_page(w, c, WEREWOLF_DP_KILLED, 0);
  set_page(w, c, WEREWOLF_DP_FATE, 0);
  lda(c, WEREWOLF_FIRST_SLEEP);
  ran(k, TS_WOLF_MADE);
  c->pc = WEREWOLF_FIRST_SLEEP_PC;
}

// `$81:ABB6`: what is told of it comes to its handler.
static void werewolf_handler(PortCpu* c, ThreadStretchWork* k) {
  c->a = WEREWOLF_HANDLER;
  ldy(c, WEREWOLF_BANK);
  ran(k, TS_LDA_LDY_IMM);
  c->pc = WEREWOLF_HANDLER_CALL_PC;
}

void werewolf_woke(Wram* w, const Rom* rom, PortCpu* c, ThreadStretchWork* k) {
  (void)rom;
  lda(c, WEREWOLF_HEALTH);
  set_page(w, c, WEREWOLF_DP_HURT, c->a);
  set_page(w, c, WEREWOLF_DP_HURT_SEEN, c->a);
  ran(k, TS_WOLF_HEALTH);
  werewolf_handler(c, k);
}

// `$81:AB02`: a strike is over, and the blow's record has gone back.
void werewolf_struck(Wram* w, const Rom* rom, PortCpu* c,
                     ThreadStretchWork* k) {
  (void)rom;
  lda(c, 0xffff);
  set_page(w, c, WEREWOLF_DP_BLOW, c->a);
  ran(k, TS_WOLF_UNBLOW);
  werewolf_handler(c, k);
}

// `$81:AA9B`: it has rested where it landed.
void werewolf_rested(Wram* w, const Rom* rom, PortCpu* c,
                     ThreadStretchWork* k) {
  (void)w;
  (void)rom;
  ran(k, TS_JMP);
  werewolf_handler(c, k);
}

// A body's `RTS`, back in the loop: a tick's sleep, unless its fate says it
// is over. The thread's start comes back past that test.
static void werewolf_body_returns(Wram* w, PortCpu* c, ThreadStretchWork* k) {
  if (pull16(w, c) == WEREWOLF_LOOP_RETURN) {
    lda(c, page(w, c, WEREWOLF_DP_FATE));
    ran(k, TS_WEED_FATE);
    if (c->a != 0) {
      c->pc = WEREWOLF_FATE_PC;
      return;
    }
    ran(k, TS_TAKEN);
  }
  lda(c, WEREWOLF_YIELD_TICKS);
  ran(k, TS_LDA_IMM);
  c->pc = WEREWOLF_YIELD_PC;
}

// `$81:ABC0`: it runs from now.
void werewolf_runs(Wram* w, const Rom* rom, PortCpu* c, ThreadStretchWork* k) {
  (void)rom;
  set_page(w, c, WEREWOLF_DP_STATE, WEREWOLF_STATE_RUN);
  ran(k, TS_MARTIAN_WALKS);
  werewolf_body_returns(w, c, k);
}

// `$81:A9AF`: the crouch's pictures have been shown, and it is in the air.
void werewolf_crouched(Wram* w, const Rom* rom, PortCpu* c,
                       ThreadStretchWork* k) {
  (void)rom;
  PORT_COVER(werewolf_leapt);
  ran(k, TS_JMP);
  set_page(w, c, WEREWOLF_DP_STATE, WEREWOLF_STATE_FLIGHT);
  ldy(c, page(w, c, WEREWOLF_DP_RECORD));
  const uint16_t record = c->y;
  wram_w16(w, (uint16_t)(record + ACTOR_META), WEREWOLF_LEAP_PICTURE);
  wram_w16(w, (uint16_t)(record + ACTOR_FLAGS),
           (uint16_t)(wram_r16(w, (uint16_t)(record + ACTOR_FLAGS)) |
                      WEREWOLF_LEAP_FLAGS));
  wram_w16(w, (uint16_t)(record + ACTOR_COLLIDE_ID), 0);
  ran(k, TS_WOLF_LEAP);
  werewolf_body_returns(w, c, k);
}

// --- one instruction or two --------------------------------------------------

void thread_setter(Wram* w, const Rom* rom, PortCpu* c, ThreadStretchWork* k) {
  (void)rom;
  for (int i = 0; i < THREAD_SETTER_COUNT; i++) {
    if (THREAD_SETTERS[i].pc != c->pc) continue;
    lda(c, THREAD_SETTERS[i].value);
    set_page(w, c, THREAD_SETTERS[i].at, c->a);
    ran(k, TS_LDA_IMM);
    ran(k, TS_STA_DP);
    c->pc += 5;
    return;
  }
}

void page_clear(Wram* w, const Rom* rom, PortCpu* c, ThreadStretchWork* k) {
  (void)rom;
  set_page(w, c, PAGE_CLEARED, 0);
  ran(k, TS_STZ_DP);
  c->pc = PAGE_CLEAR_RTS_PC;
}

void page_pull(Wram* w, const Rom* rom, PortCpu* c, ThreadStretchWork* k) {
  (void)rom;
  c->d = pull16(w, c);
  set_nz16(c, c->d);
  ran(k, TS_PLD);
  c->pc = PAGE_PULL_RTL_PC;
}

void carry_set(Wram* w, const Rom* rom, PortCpu* c, ThreadStretchWork* k) {
  (void)w;
  (void)rom;
  set_c(c, true);
  ran(k, TS_SEC);
  c->pc = CARRY_SET_RTS_PC;
}

void victim_gone(Wram* w, const Rom* rom, PortCpu* c, ThreadStretchWork* k) {
  (void)rom;
  lda(c, page(w, c, VICTIM_DP_GONE));
  ran(k, TS_LDA_DP);
  c->pc = VICTIM_GONE_CALL_PC;
}

// --- a player's flashing ended -----------------------------------------------

void flashing_end(Wram* w, const Rom* rom, PortCpu* c, ThreadStretchWork* k) {
  (void)rom;
  PORT_COVER(player_flashing_ended);
  set_page(w, c, FLASHING_DP_FRAMES_LEFT, 0);
  set_page(w, c, FLASHING_DP_ENDS_BY, 0);
  ldy(c, page(w, c, PLAYER_DP_RECORD));
  wram_w16(w, (uint16_t)(c->y + ACTOR_FLAGS),
           (uint16_t)(wram_r16(w, (uint16_t)(c->y + ACTOR_FLAGS)) | ACTOR_DRAW));
  lda(c, PLAYER_STATE_NORMAL);
  c->x = page(w, c, FLASHING_DP_UNDER);
  cmp16(c, c->x, FLASHING_UNDER_TURNING);
  ran(k, TS_FLASHING_END);
  if (c->x == FLASHING_UNDER_TURNING) {
    lda(c, PLAYER_STATE_TURNING_B);
    ran(k, TS_LDA_IMM);
  } else {
    ran(k, TS_TAKEN);
  }
  set_page(w, c, PLAYER_DP_STATE, c->a);
  ran(k, TS_STA_DP);
  c->pc = FLASHING_END_CALL_PC;
}

// --- a player shown hurt -----------------------------------------------------

// `$80:D056`: the picture they had goes on the stack, bank first.
void player_hit_show(Wram* w, const Rom* rom, PortCpu* c,
                     ThreadStretchWork* k) {
  (void)rom;
  PORT_COVER(player_hit_shown);
  ldy(c, page(w, c, PLAYER_DP_RECORD));
  push16(w, c, wram_r16(w, (uint16_t)(c->y + ACTOR_META_BANK)));
  push16(w, c, wram_r16(w, (uint16_t)(c->y + ACTOR_META)));
  lda(c, PLAYER_HURT_PICTURE_BANK);
  wram_w16(w, (uint16_t)(c->y + ACTOR_META_BANK), c->a);
  ran(k, TS_PLAYER_HIT_KEEP);
  c->pc = PLAYER_HIT_SHOW_CALL_PC;
}

// `$80:D070`: the list has played, and it comes off again.
void player_hit_shown(Wram* w, const Rom* rom, PortCpu* c,
                      ThreadStretchWork* k) {
  (void)rom;
  PORT_COVER(player_hit_back);
  ldy(c, page(w, c, PLAYER_DP_RECORD));
  wram_w16(w, (uint16_t)(c->y + ACTOR_META), pull16(w, c));
  lda(c, pull16(w, c));
  wram_w16(w, (uint16_t)(c->y + ACTOR_META_BANK), c->a);
  set_page(w, c, PLAYER_DP_16, 0);
  set_page(w, c, PLAYER_DP_50, 0);
  ran(k, TS_PLAYER_HIT_BACK);
  c->pc = PLAYER_HIT_SHOWN_CALL_PC;
}

// `$81:9A3E`: one that comes in over the top. Its place is just below the
// top of the screen, and the ground and the level's edges are asked about it
// before anything is made.
void martian_arrival_enter(Wram* w, const Rom* rom, PortCpu* c,
                           ThreadStretchWork* k) {
  (void)rom;
  PORT_COVER(martian_arrival_entered);
  push16(w, c, MARTIAN_PLACE_RETURN);
  ran(k, TS_JSR);
  set_c(c, false);
  lda(c, adc16(c, wram_r16(w, W_CAMERA_Y), MARTIAN_ARRIVAL_BELOW_TOP));
  set_page(w, c, MARTIAN_DP_SHOT_Y, c->a);
  ldy(c, c->a);
  ldx(c, page(w, c, MARTIAN_DP_SHOT_X));
  ran(k, TS_ARRIVAL_PLACE);
  c->pc = MARTIAN_PLACE_GROUND_PC;
}

// The place is no good, and the thread ends with nothing made.
static void arrival_refused(Wram* w, PortCpu* c, ThreadStretchWork* k) {
  PORT_COVER(martian_arrival_blocked);
  pull16(w, c);
  ran(k, TS_RTS);
  ran(k, TS_BRANCH);
  ran(k, TS_TAKEN);
  c->pc = MARTIAN_ARRIVAL_NONE_PC;
}

void martian_arrival_grounded(Wram* w, const Rom* rom, PortCpu* c,
                              ThreadStretchWork* k) {
  (void)rom;
  if (branches(c, PORT_P_C, k)) {
    arrival_refused(w, c, k);
    return;
  }
  ldx(c, page(w, c, MARTIAN_DP_SHOT_X));
  ldy(c, page(w, c, MARTIAN_DP_SHOT_Y));
  ran(k, TS_LDX_LDY_DP);
  c->pc = MARTIAN_PLACE_EDGE_PC;
}

void martian_arrival_placed(Wram* w, const Rom* rom, PortCpu* c,
                            ThreadStretchWork* k) {
  (void)rom;
  if (branches(c, PORT_P_C, k)) {
    arrival_refused(w, c, k);
    return;
  }
  PORT_COVER(martian_arrival_free);
  pull16(w, c);
  ran(k, TS_ARRIVAL_FREE);
  ran(k, TS_BRANCH);
  load_up(w, c, MARTIAN_LOAD);
  ran(k, TS_MARTIAN_ENTER);
  c->pc = MARTIAN_ARRIVAL_BEGIN_CALL_PC;
}

void martian_arrival_begun(Wram* w, const Rom* rom, PortCpu* c,
                           ThreadStretchWork* k) {
  (void)rom;
  jsr_and_back(w, c, (uint16_t)(MARTIAN_ARRIVAL_BEGUN_PC & 0xffffu), k);
  set_page(w, c, MARTIAN_DP_STATE, MARTIAN_STATE_ARRIVE);
  set_page(w, c, MARTIAN_DP_SIDE_TIMER, 0);
  set_page(w, c, MARTIAN_DP_SIDE, 0);
  ran(k, TS_ARRIVAL_STATE);
  lda(c, MARTIAN_YIELD_TICKS);
  ran(k, TS_LDA_IMM);
  c->pc = MARTIAN_ARRIVAL_YIELD_PC;
}
