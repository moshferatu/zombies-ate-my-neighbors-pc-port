// Stretches of four threads, between their calls -- see
// port/thread_stretches.h.

#include "port/thread_stretches.h"

#include "port/bcd.h"
#include "port/bodies.h"            // W_NEIGHBOURS_LEFT
#include "port/bolt.h"
#include "port/fishman.h"
#include "port/walk.h"              // W_WALK_SQUARES
#include "port/mainloop.h"         // W_PLAYERS_OUT
#include "port/boss_thread.h"       // the figure's page
#include "port/bossbg.h"            // W_BOSS_BG_ONE_SRC
#include "port/bystander.h"
#include "port/chainsaw.h"
#include "port/apu.h"               // W_APU_SOUNDS
#include "port/cheat.h"
#include "port/coverage.h"
#include "port/decoy.h"
#include "port/waits.h"
#include "port/follower.h"
#include "port/football.h"         // FOOTBALLER_STATE_SENT_OFF
#include "port/frontend.h"            // the frame count
#include "port/level_scene.h"       // W_LEVEL_NUMBER
#include "port/martian.h"
#include "port/neighbour_thread.h"  // W_SAVED_COUNT_A
#include "port/oam.h"               // the display record's fields
#include "port/thread.h"            // thread_set_handler
#include "port/player_frame.h"
#include "port/player.h"            // W_PLAYER_FLAG
#include "port/player_small.h"      // player_hands_swap
#include "port/rng.h"
#include "port/spider.h"
#include "port/walker.h"
#include "port/apu.h"               // W_APU_SOUNDS
#include "port/seeker.h"
#include "port/tracker.h"
#include "port/neighbours.h"        // W_TOURISTS_TURN
#include "port/werewolf.h"
#include "port/objects.h"           // W_VICTIM_GATE
#include "port/palcycle.h"
#include "port/saucer.h"
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
    {0x82e0b4u, 0x0001},  // a bonus object's first sleep
    {0x81e8c1u, 0x0001},  // the swipe's first sleep, after its sound
    {0x82df0au, 0x000a},  // a locked door's two buzzes, and a sleep after each
    {0x82df11u, DOOR_BUZZ_SFX},
    {0x82df18u, 0x0014},
    {0x82ec5au, 0x0020},  // the seeker's sleep after its puff begins
    {0x82950au, 0x0001},  // the figure's sleeps while its tiles go up
    {0x82a871u, 0x0001},  // the same after `$82:A86D`'s upload
    {0x8085bbu, 0x0007},  // sound 7, as both players reach the way out
    {0x8085c2u, 0x0090},  // ...and 144 ticks
    {0x80ea16u, 0x000a},  // ten ticks
    {0x80ef7bu, 0x0023},  // a sound
    {0x80ef88u, 0x0004},  // four ticks
    {0x80efa3u, 0x0004},  // four ticks
    {0x80efb2u, 0x0004},  // four ticks
    {0x81843bu, 0x012c},  // five seconds
    {0x81a471u, 0x0002},  // two ticks
    {0x81c6d2u, 0xc6dc},  // a footballer sent off: its pictures
    {0x81e500u, 0xe5b8},  // a fishman's pictures
    {0x81e593u, 0xe5b8},  // ...the same
    {0x81e8bau, 0x0014},  // the swipe's sound
    {0x81f1e6u, 0xf296},  // a decoy's pictures
    {0x81f9abu, 0xfab1},  // the lob's pictures as it bursts
    {0x82ef0bu, 0xef20},  // a seeker's pictures
    {0x82f279u, 0x0002},  // two ticks
    {0x82f606u, 0xf623},  // a square let go: its pictures
    {0x82f760u, 0xf77a},  // the exit door's last pictures
    {0x82f918u, 0xf934},  // ...and its first
    {0x81cd05u, 0xcd1f},  // a slime killed: its last pictures
    {0x80857eu, 0x0002},  // the level's ending: the second loop's first sleep
    {0x808a00u, 0x001e},  // the game over's first sleep, half a second
    {0x80d9acu, 0x0005},  // the monster potion's sound
    {0x82de4fu, 0x000a},  // the bystander's ten frames, a `WAI` each
};

const uint16_t FIGURE_PART_RETURNS[FIGURE_PARTS] = {0x9533, 0x9538, 0x953d,
                                                    0x9542};

const uint16_t DEATH_ENDS[DEATH_ENDS_COUNT] = {
    0xd97f,  // the flashing's, `$80:D96C`
    0xda54, 0xdb08, 0xdbda, 0xdc70, 0xf3a5,
};

const ThreadSetter THREAD_SETTERS[THREAD_SETTER_COUNT] = {
    {0x819215u, 0x921b, 0x12},  // two states of the thread at `$81:92C2`
    {0x819250u, 0x9256, 0x12},
};

const ThreadPair THREAD_PAIRS[THREAD_PAIR_COUNT] = {
    {0x8085d3u, 0x8000, 0x0084},  // what the layers are reset with
    {0x819b30u, 0x9b6b, 0x0081},  // the martian's handler
    {0x80c2deu, 0xc2ab, 0x0080},  // the thread of the display's top
    {0x80815fu, 0x84b1, 0x0080},  // the main thread, begun at the reset
    {0x81f252u, 0xf25d, 0x0081},  // a decoy's handler
    {0x82f91fu, 0xf958, 0x0082},  // the exit door's handler
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

// A seeker's state with the loop's return under it, and its record in low
// WRAM.
static bool seeker_returns(const Wram* w, const PortCpu* c) {
  return wram_r16(w, (uint16_t)(c->s + 1)) == SEEKER_FRAME_RETURN &&
         page(w, c, SEEKER_DP_RECORD) < 0x1f00;
}

// `$80:CEE6`: does a player out end, rather than stay for good? With
// neighbours still to save, or some saved, and the two players' words not
// the same.
static bool player_out_unloads(const Wram* w) {
  const bool saving =
      wram_r16(w, W_NEIGHBOURS_LEFT) != 0 ||
      (wram_r16(w, W_RESCUED) | wram_r16(w, W_RESCUED + 2)) != 0;
  return saving && wram_r16(w, W_FRONTEND_PLAYERS) !=
                       wram_r16(w, W_FRONTEND_PLAYERS + 2);
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
    case TS_NEEDS_SQUARE:
      return page(w, c, SQUARE_DP_HANDLER) == SQUARE_HANDLER &&
             wram_r16(w, W_SQUARE_PLAYERS) < 0x1f00 &&
             wram_r16(w, W_SQUARE_PLAYERS + 2) < 0x1f00;
    case TS_NEEDS_FLYOFF_RECORD:
      return c->y < 0x1f00 && page(w, c, FLYOFF_DP_KIND) < 0x0100;
    case TS_NEEDS_FLYOFF:
      return page(w, c, FLYOFF_DP_RECORD) < 0x1f00 &&
             page(w, c, FLYOFF_DP_STEP_AT) < 4 * FLYOFF_DIAGONALS;
    case TS_NEEDS_BYSTANDER_RECORD:
      return c->y < 0x1f00 && page(w, c, BYSTANDER_DP_KIND) < 0x0100;
    case TS_NEEDS_BYSTANDER_SET:
      return wram_r16(w, (uint16_t)(c->s + 1)) == BYSTANDER_SETUP_RETURN;
    case TS_NEEDS_BYSTANDER_KIND:
      return page(w, c, BYSTANDER_DP_KIND) < 0x0100;
    case TS_NEEDS_CUT: {
      const uint16_t side = page(w, c, CHAINSAW_DP_CUT_SIDE);
      return side >= 1 && side <= 8 &&
             page(w, c, CHAINSAW_DP_RECORD) < 0x1f00;
    }
    case TS_NEEDS_CUT_PLACED:
      return thread_stretch_needs_met(w, TS_NEEDS_CUT, c) &&
             wram_r16(w, (uint16_t)(c->s + 1)) == CUT_AIM_RETURN;
    case TS_NEEDS_CUT_MADE:
      return thread_stretch_needs_met(w, TS_NEEDS_CUT, c) &&
             c->a < 0x1f00 && page(w, c, CUT_DP_LIST) >= 0x8000u &&
             wram_r16(w, (uint16_t)(c->s + 1)) == CUT_MAKE_RETURN;
    case TS_NEEDS_CUT_LIST:
      return page(w, c, CHAINSAW_DP_SAW) < 0x1f00 &&
             page(w, c, CHAINSAW_DP_RECORD) < 0x1f00 &&
             page(w, c, CUT_DP_LIST) >= 0x8000u;
    case TS_NEEDS_CUT_TILED: {
      PortCpu under = *c;
      under.d = wram_r16(w, (uint16_t)(c->s + 1));
      const uint16_t side = page(w, &under, CHAINSAW_DP_CUT_SIDE);
      return under.d >= 0x0100 && under.d < 0x1f00 &&
             (under.d & 0x00ffu) == 0 && side >= 1 && side <= 8;
    }
    case TS_NEEDS_CUT_FREED:
      return wram_r16(w, (uint16_t)(c->s + 1)) == CUT_SECOND_RETURN;
    case TS_NEEDS_DEATH_ENDS: {
      const uint16_t ends_by = page(w, c, DEATH_DP_ENDS_BY);
      if (ends_by == 0) return true;
      for (int i = 0; i < DEATH_ENDS_COUNT; i++)
        if (DEATH_ENDS[i] == ends_by) return true;
      return false;
    }
    case TS_NEEDS_DEATH: {
      const uint16_t who = page(w, c, DEATH_DP_WHO);
      const uint16_t player = page(w, c, DEATH_DP_PLAYER);
      return c->db == 0x80 && who < 4 && (who & 1) == 0 && player < 4 &&
             (player & 1) == 0 && page(w, c, DEATH_DP_RECORD) < 0x1f00;
    }
    case TS_NEEDS_SLIMED:
      return page(w, c, SLIMED_DP_RECORD) < 0x1f00 &&
             page(w, c, SLIMED_DP_COVER) < 0x1f00 &&
             page(w, c, SLIMED_DP_PLAYER) < 4;
    case TS_NEEDS_SLIMED_RESTED:
      return wram_r16(w, (uint16_t)(c->s + 1)) == SLIMED_REST_RETURN &&
             page(w, c, SLIMED_DP_PLAYER) < 4;
    case TS_NEEDS_SPIDER_MADE:
      return c->a < 0x1f00 &&
             wram_r16(w, (uint16_t)(c->s + 1)) == SPIDER_MADE_RETURN;
    case TS_NEEDS_WALKER_MADE:
      return c->a < 0x1f00 &&
             wram_r16(w, (uint16_t)(c->s + 1)) == WALKER_MADE_RETURN;
    case TS_NEEDS_WALKER_SHOWN:
      return wram_r16(w, (uint16_t)(c->s + 1)) == WALKER_MADE_RETURN;
    case TS_NEEDS_SQUARE_MADE:
      return c->a < 0x1f00 &&
             wram_r16(w, (uint16_t)(c->s + 1)) == SQUARE_MADE_RETURN;
    case TS_NEEDS_SQUARE_CELL: {
      // The block map is in the cartridge, which is what its price is.
      const uint8_t bank = (uint8_t)wram_r16(w, W_SQUARE_CELL + 2);
      return c->d == 0 && bank >= 0x80 && bank < 0xc0 &&
             wram_r16(w, W_SQUARE_CELL) >= 0x8000u &&
             wram_r16(w, (uint16_t)(c->s + 3)) == SQUARE_CELL_RETURN &&
             wram_r16(w, (uint16_t)(c->s + 1)) >= 0x0100;
    }
    case TS_NEEDS_SQUARE_TILED: {
      const uint16_t page_at = wram_r16(w, (uint16_t)(c->s + 1));
      return page_at >= 0x0100 &&
             wram_r16(w, (uint16_t)(c->s + 3)) == SQUARE_CELL_RETURN &&
             wram_r16(w, (uint16_t)(page_at + SQUARE_DP_RECORD)) < 0x1f00;
    }
    case TS_NEEDS_SQUARE_FREED:
      return wram_r16(w, (uint16_t)(c->s + 1)) == SQUARE_PASS_RETURN &&
             page(w, c, SQUARE_DP_DONE) != 0xffffu &&
             page(w, c, SQUARE_DP_SLOT_KEPT) <= WALK_SQUARES_LAST;
    case TS_NEEDS_FISHMAN_ENDED:
      return page(w, c, FISHMAN_DP_RECORD) < 0x1f00 &&
             (wram_r16(w, W_SPAWN_LOAD) & 0x8000u) == 0 &&
             wram_r16(w, W_SPAWN_LOAD) >= FISHMAN_LOAD;
    case TS_NEEDS_EXIT_DOOR_FIND:
      return wram_r16(w, (uint16_t)(c->s + 1)) == EXIT_DOOR_FIND_RETURN &&
             page(w, c, EXIT_DOOR_DP_PLAYER_RECORD) < 0x1f00 &&
             (page(w, c, EXIT_DOOR_DP_PLAYER) & ~2u) == 0;
    case TS_NEEDS_EXIT_DOOR_MADE:
      return c->a == c->y && c->y < 0x1f00 &&
             wram_r16(w, (uint16_t)(c->s + 1)) == EXIT_DOOR_MAKE_RETURN;
    case TS_NEEDS_EXIT_DOOR_HANDLED:
      return page(w, c, EXIT_DOOR_DP_RECORD) < 0x1f00 &&
             wram_r16(w, (uint16_t)(c->s + 1)) == EXIT_DOOR_MAKE_RETURN;
    case TS_NEEDS_EXIT_DOOR:
      return page(w, c, EXIT_DOOR_DP_RECORD) < 0x1f00;
    case TS_NEEDS_PLAYER:
      return page(w, c, PLAYER_OUT_DP_PLAYER) < 4;
    case TS_NEEDS_REST_ENDED:
      return page(w, c, PLAYER_OUT_DP_PLAYER) < 4 &&
             wram_r16(w, (uint16_t)(c->s + 1)) != SLIMED_REST_RETURN;
    case TS_NEEDS_PLAYER_OUT:
      return page(w, c, PLAYER_OUT_DP_PLAYER) < 4 &&
             (!player_out_unloads(w) ||
              ((uint16_t)(wram_r16(w, W_SPAWN_LOAD) - PLAYER_OUT_LOAD) &
               0x8000u) == 0);
    case TS_NEEDS_PLAYER_DROPS: {
      const uint16_t at = page(w, c, PLAYER_OUT_DP_INVENTORY);
      return at < 0x1f00 && (c->db < 0x40 || (c->db >= 0x80 && c->db < 0xc0));
    }
    case TS_NEEDS_ZOMBIE_FREED:
      // Less than its weight, and the ROM goes round a `BMI` for good.
      return ((uint16_t)(wram_r16(w, W_SPAWN_LOAD) - ZOMBIE_8ED5_LOAD) & 0x8000u) == 0;
    case TS_NEEDS_SEEKER_MADE:
      return c->a < 0x1f00 &&
             wram_r16(w, (uint16_t)(c->s + 1)) == SEEKER_MADE_RETURN;
    case TS_NEEDS_SEEKER_SOUNDED:
      return wram_r16(w, (uint16_t)(c->s + 1)) == SEEKER_MADE_RETURN &&
             page(w, c, SEEKER_DP_RECORD) < 0x1f00;
    case TS_NEEDS_BREAKABLE_MADE:
      return c->a < 0x1f00 &&
             wram_r16(w, (uint16_t)(c->s + 1)) == BREAKABLE_MADE_RETURN;
    case TS_NEEDS_WATCHER_BEGUN:
      return page(w, c, IDLE_WAIT_DP_RECORD) < 0x1f00 &&
             wram_r16(w, (uint16_t)(c->s + 1)) == WATCHER_BEGUN_RETURN;
    case TS_NEEDS_DECOY_MADE:
      return c->a < 0x1f00 && c->y == c->a &&
             wram_r16(w, (uint16_t)(c->s + 1)) == DECOY_MADE_RETURN;
    case TS_NEEDS_BOLT_MADE:
      return c->a < 0x1f00 && c->y == c->a &&
             wram_r16(w, (uint16_t)(c->s + 1)) == BOLT_MADE_RETURN &&
             page(w, c, BOLT_DP_WAY) <= BOLT_WAY_MAX &&
             page(w, c, BOLT_DP_PLAYER) < 8;
    case TS_NEEDS_STUNG:
      return page(w, c, STUNG_DP_PLAYER) < 4 &&
             page(w, c, STUNG_DP_RECORD) < 0x1f00;
    case TS_NEEDS_STUNG_SHOWN:
      return page(w, c, STUNG_DP_RECORD) < 0x1f00 && c->db == 0x80 &&
             page(w, c, STUNG_DP_WAY) <= 2;
    case TS_NEEDS_CHAINSAW_MADE:
      return page(w, c, CHAINSAW_DP_RECORD) < 0x1f00 &&
             wram_r16(w, (uint16_t)(c->s + 1)) == CHAINSAW_MADE_RETURN;
    case TS_NEEDS_CHAINSAW_SHOWN:
      return page(w, c, CHAINSAW_DP_RECORD) < 0x1f00 &&
             wram_r16(w, (uint16_t)(c->s + 1)) == CHAINSAW_SHOWN_RETURN;
    case TS_NEEDS_SEEKER:
      return seeker_returns(w, c);
    case TS_NEEDS_SEEKER_HIDDEN:
      return seeker_returns(w, c) && page(w, c, SEEKER_DP_TARGET) < 0x1f00;
    case TS_NEEDS_SEEKER_NEXT: {
      const uint16_t after = page(w, c, SEEKER_DP_AFTER);
      return seeker_returns(w, c) && after <= SEEKER_AFTER_DART &&
             (after & 1) == 0;
    }
    case TS_NEEDS_FIGURE_PART: {
      const uint16_t back = wram_r16(w, (uint16_t)(c->s + 1));
      for (int i = 0; i < FIGURE_PARTS - 1; i++)
        if (back == FIGURE_PART_RETURNS[i]) return c->a < 0x1f00;
      return back == FIGURE_PART_RETURNS[FIGURE_PARTS - 1] &&
             wram_r16(w, (uint16_t)(c->s + 3)) == FIGURE_START_RETURN &&
             c->a < 0x1f00;
    }
    case TS_NEEDS_PLAYER_LOAD:
      // `SBC #$1C : BMI` to itself, below nought.
      return ((uint16_t)(wram_r16(w, W_SPAWN_LOAD) - PLAYER_LOAD) & 0x8000u) ==
             0;
    case TS_NEEDS_SAUCER_AIMED:
      return wram_r16(w, (uint16_t)(c->s + 1)) == SAUCER_START_RETURN &&
             c->x < 0x1f00;
    case TS_NEEDS_SLIME_BEGUN:
      return wram_r16(w, (uint16_t)(c->s + 1)) == SLIME_BEGIN_RETURN;
    case TS_NEEDS_SLIME_WAY:
      return wram_r16(w, (uint16_t)(c->s + 1)) == SLIME_WAY_RETURN;
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

// `LDA $00D2 : STA $04 : LDA #player : STA $06 : LDA #$F70B : LDY #$0082`,
// or `$00D4` for the second player, to the `JSL thread_spawn`.
static void way_out_begun(Wram* w, PortCpu* c, ThreadStretchWork* k,
                          uint16_t player) {
  lda(c, wram_r16(w, (uint16_t)(W_PLAYER_RECORDS + player)));
  set_page(w, c, LEVEL_ENDING_DP_RECORD, c->a);
  set_page(w, c, LEVEL_ENDING_DP_PLAYER, player);
  c->a = LEVEL_ENDING_THREAD;
  ldy(c, LEVEL_ENDING_THREAD_BANK);
  ran(k, TS_LEVEL_ENDING_SPAWN);
}

void level_ending(Wram* w, const Rom* rom, PortCpu* c, ThreadStretchWork* k) {
  lda(c, (uint16_t)(wram_r16(w, W_PLAYERS_RESCUED) |
                    wram_r16(w, W_PLAYERS_RESCUED + 2)));
  ran(k, TS_MAIN_PLAYERS);  // `LDA : ORA : BEQ` too
  if (c->a == 0) {
    PORT_COVER(level_ending_none_rescued);
    ran(k, TS_TAKEN);
    c->pc = GAME_LOST_PC;
    return;
  }
  lda(c, wram_r16(w, W_HUD_PANEL_ON));
  ran(k, TS_MAIN_NEIGHBOURS);  // `LDA abs` and a branch
  if (c->a == 0) {
    PORT_COVER(level_ending_first_out);
    ran(k, TS_TAKEN);
    level_ending_second(w, rom, c, k);
    return;
  }
  PORT_COVER(level_ending_first_leaves);
  way_out_begun(w, c, k, 0);
  c->pc = LEVEL_ENDING_FIRST_CALL_PC;
}

void level_ending_second(Wram* w, const Rom* rom, PortCpu* c,
                         ThreadStretchWork* k) {
  lda(c, wram_r16(w, W_HUD_PANEL_ON + 2));
  ran(k, TS_MAIN_NEIGHBOURS);
  if (c->a == 0) {
    PORT_COVER(level_ending_second_out);
    ran(k, TS_TAKEN);
    c->pc = LEVEL_ENDING_SLEEP_PC;
    thread_argument(w, rom, c, k);
    return;
  }
  PORT_COVER(level_ending_second_leaves);
  way_out_begun(w, c, k, 2);
  c->pc = LEVEL_ENDING_SECOND_CALL_PC;
}

void main_back(Wram* w, const Rom* rom, PortCpu* c, ThreadStretchWork* k) {
  (void)w;
  (void)rom;
  ran(k, TS_BRANCH);
  if (flag(c, PORT_P_C)) {
    PORT_COVER(main_back_over);
    ran(k, TS_TAKEN);
    c->pc = MAIN_BACK_OVER_PC;
    return;
  }
  PORT_COVER(main_back_next);
  c->pc = MAIN_BACK_NEXT_PC;
}

void game_lost(Wram* w, const Rom* rom, PortCpu* c, ThreadStretchWork* k) {
  (void)w;
  (void)rom;
  PORT_COVER(game_lost);
  set_c(c, true);
  ran(k, TS_SEC);
  c->pc = GAME_LOST_RTL_PC;
}

void stuck_freed(Wram* w, const Rom* rom, PortCpu* c, ThreadStretchWork* k) {
  (void)rom;
  PORT_COVER(stuck_state_ended);
  set_page(w, c, PLAYER_DP_STATE, PLAYER_STATE_NORMAL);
  ran(k, TS_STZ_DP);
  c->pc = STUCK_FREED_RTS_PC;
}

// The `BNE` reads the zero the caller's `LDX` left: the first player is X
// zero.
void panel_side(Wram* w, const Rom* rom, PortCpu* c, ThreadStretchWork* k) {
  (void)w;
  (void)rom;
  ran(k, TS_BRANCH);
  if (flag(c, PORT_P_Z)) {
    PORT_COVER(panel_side_first);
    c->pc = PANEL_SIDE_1_CALL_PC;
    return;
  }
  PORT_COVER(panel_side_second);
  ran(k, TS_TAKEN);
  c->pc = PANEL_SIDE_2_CALL_PC;
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

// --- the walk's square -------------------------------------------------------

// `$82:F6EB`: the band is set from the square, the handler named, and the
// first sleep is six ticks.
void square_enter(Wram* w, const Rom* rom, PortCpu* c, ThreadStretchWork* k) {
  (void)rom;
  push16(w, c, (uint16_t)(SQUARE_BAND_CALL_PC + 2));
  const uint16_t left = (uint16_t)(page(w, c, SQUARE_DP_X) & ~(SQUARE_SIZE - 1));
  set_page(w, c, SQUARE_DP_LEFT, left);
  set_c(c, false);
  const uint16_t middle_x = adc16(c, left, SQUARE_SIZE / 2);
  set_page(w, c, SQUARE_DP_MIDDLE_X, middle_x);
  set_c(c, false);
  set_page(w, c, SQUARE_DP_RIGHT, adc16(c, middle_x, SQUARE_SIZE / 2));
  set_c(c, true);
  const uint16_t top =
      sbc16(c, (uint16_t)(page(w, c, SQUARE_DP_Y) & ~(SQUARE_SIZE - 1)),
            SQUARE_ABOVE);
  set_page(w, c, SQUARE_DP_TOP, top);
  set_c(c, false);
  const uint16_t middle_y = adc16(c, top, SQUARE_TO_MIDDLE);
  set_page(w, c, SQUARE_DP_MIDDLE_Y, middle_y);
  set_c(c, false);
  set_page(w, c, SQUARE_DP_BOTTOM, adc16(c, middle_y, SQUARE_BELOW_MIDDLE));
  lda(c, page(w, c, SQUARE_DP_SLOT));
  set_page(w, c, SQUARE_DP_SLOT_KEPT, c->a);
  set_page(w, c, SQUARE_DP_DONE, 0);
  pull16(w, c);
  ran(k, TS_JSR);
  ran(k, TS_SQUARE_BAND);

  push16(w, c, (uint16_t)(SQUARE_HANDLER_CALL_PC + 2));
  lda(c, SQUARE_HANDLER);
  set_page(w, c, SQUARE_DP_HANDLER, c->a);
  pull16(w, c);
  ran(k, TS_JSR);
  ran(k, TS_SQUARE_HANDLER);

  lda(c, SQUARE_TICKS);
  ran(k, TS_LDA_IMM);
  c->pc = SQUARE_YIELD_PC;
}

// Is either player on this side of an edge of the band? Below the top is
// carry set, and above the bottom carry clear. Each is asked in turn, and
// a player not in the game is passed over.
static bool either_player(const Wram* w, PortCpu* c, ThreadStretchWork* k,
                          uint16_t edge, bool carry_wanted) {
  for (int i = 0; i < 2; i++) {
    ldy(c, wram_r16(w, (uint16_t)(W_SQUARE_PLAYERS + 2 * i)));
    ran(k, TS_SQUARE_WHO);
    if (c->y == 0) {
      ran(k, TS_TAKEN);
      continue;
    }
    lda(c, wram_r16(w, (uint16_t)(c->y + ACTOR_Y)));
    cmp16(c, c->a, edge);
    ran(k, TS_SQUARE_ROW);
    if (flag(c, PORT_P_C) == carry_wanted) {
      ran(k, TS_TAKEN);
      return true;
    }
    if (i == 1) {
      ran(k, TS_SQUARE_BRA);
      ran(k, TS_TAKEN);
    }
  }
  return false;
}

// `$82:F6F8`: the handler is called by `PEA : LDA $10 : DEC : PHA : RTS`,
// and asks `$82:F593` whether the band is still held.
void square_pass(Wram* w, const Rom* rom, PortCpu* c, ThreadStretchWork* k) {
  (void)rom;
  push16(w, c, SQUARE_PASS_RETURN);
  lda(c, (uint16_t)(page(w, c, SQUARE_DP_HANDLER) - 1));
  push16(w, c, c->a);
  pull16(w, c);
  ran(k, TS_SQUARE_CALL);

  push16(w, c, (uint16_t)(SQUARE_ASK_CALL_PC + 2));
  ran(k, TS_JSR);
  const bool held =
      either_player(w, c, k, page(w, c, SQUARE_DP_TOP), true) &&
      either_player(w, c, k, page(w, c, SQUARE_DP_BOTTOM), false);
  set_c(c, !held);
  pull16(w, c);
  ran(k, held ? TS_SQUARE_HELD : TS_SQUARE_LEFT);
  ran(k, TS_BRANCH);  // BCC
  if (!held) {
    PORT_COVER(square_left);
    ran(k, TS_JMP);
    c->pc = SQUARE_LEFT_PC;
    return;
  }
  ran(k, TS_TAKEN);
  pull16(w, c);
  ran(k, TS_RTS);

  lda(c, page(w, c, SQUARE_DP_DONE));
  ran(k, TS_SQUARE_DONE);
  if (c->a != 0) {
    PORT_COVER(square_done);
    c->pc = SQUARE_DONE_PC;
    return;
  }
  PORT_COVER(square_held);
  ran(k, TS_TAKEN);
  lda(c, SQUARE_TICKS);
  ran(k, TS_LDA_IMM);
  c->pc = SQUARE_YIELD_PC;
}

// --- a bonus object flying off -----------------------------------------------

// `$82:E0BF`, with the record in A and in Y. The table is read through the
// data bank, which is the thread's.
void flyoff_dress(Wram* w, const Rom* rom, PortCpu* c, ThreadStretchWork* k) {
  set_page(w, c, FLYOFF_DP_RECORD, c->a);
  const uint16_t record = c->y;
  wram_w16(w, (uint16_t)(record + ACTOR_X), page(w, c, FLYOFF_DP_X));
  wram_w16(w, (uint16_t)(record + ACTOR_Z), 0);
  wram_w16(w, (uint16_t)(record + ACTOR_Y), page(w, c, FLYOFF_DP_Y));
  c->x = asl16(c, page(w, c, FLYOFF_DP_KIND));
  wram_w16(w, (uint16_t)(record + ACTOR_META),
           rom_word(rom, FLYOFF_PICTURES + c->x));
  wram_w16(w, (uint16_t)(record + ACTOR_META_BANK), FLYOFF_PICTURE_BANK);
  wram_w16(w, (uint16_t)(record + ACTOR_THREAD),
           wram_r16(w, W_SCHED_CUR_TASK));
  wram_w16(w, (uint16_t)(record + ACTOR_COLLIDE_ID), 0);
  wram_w16(w, (uint16_t)(record + ACTOR_FLAGS),
           (uint16_t)(wram_r16(w, (uint16_t)(record + ACTOR_FLAGS)) |
                      FLYOFF_FLAGS));
  // No handler: `LDA #$0000 : TAY`.
  c->a = 0;
  ldy(c, 0);
  ran(k, TS_FLYOFF_DRESS);
  c->pc = FLYOFF_HANDLER_PC;
}

// `$82:E102`, with the draw in A.
void flyoff_way(Wram* w, const Rom* rom, PortCpu* c, ThreadStretchWork* k) {
  (void)rom;
  c->a = (uint16_t)(c->a & (FLYOFF_DIAGONALS - 1));
  c->a = asl16(c, asl16(c, c->a));
  set_page(w, c, FLYOFF_DP_STEP_AT, c->a);
  set_page(w, c, FLYOFF_DP_TICKS_LEFT, FLYOFF_TICKS);
  ran(k, TS_FLYOFF_WAY);
  lda(c, 1);
  ran(k, TS_LDA_IMM);
  c->pc = FLYOFF_YIELD_PC;
}

// `$82:E115`: a step along the diagonal, and the count.
void flyoff_pass(Wram* w, const Rom* rom, PortCpu* c, ThreadStretchWork* k) {
  c->y = page(w, c, FLYOFF_DP_RECORD);
  c->x = page(w, c, FLYOFF_DP_STEP_AT);
  set_c(c, false);
  wram_w16(w, (uint16_t)(c->y + ACTOR_X),
           adc16(c, wram_r16(w, (uint16_t)(c->y + ACTOR_X)),
                 rom_word(rom, FLYOFF_STEPS + c->x)));
  set_c(c, false);
  wram_w16(w, (uint16_t)(c->y + ACTOR_Y),
           adc16(c, wram_r16(w, (uint16_t)(c->y + ACTOR_Y)),
                 rom_word(rom, FLYOFF_STEPS + c->x + 2)));
  const uint16_t left = (uint16_t)(page(w, c, FLYOFF_DP_TICKS_LEFT) - 1);
  set_page(w, c, FLYOFF_DP_TICKS_LEFT, left);
  ran(k, TS_FLYOFF_STEP);
  if (left & 0x8000u) {
    PORT_COVER(flyoff_gone);
    lda(c, page(w, c, FLYOFF_DP_RECORD));
    ran(k, TS_FLYOFF_END);
    c->pc = FLYOFF_FREE_PC;
    return;
  }
  PORT_COVER(flyoff_on);
  ran(k, TS_TAKEN);
  lda(c, 1);
  ran(k, TS_LDA_IMM);
  c->pc = FLYOFF_YIELD_PC;
}

// --- a frozen creature -------------------------------------------------------

// `$81:84D6`.
void freeze_begin(Wram* w, const Rom* rom, PortCpu* c, ThreadStretchWork* k) {
  (void)rom;
  c->y = page(w, c, FREEZE_DP_RECORD);
  wram_w16(w, (uint16_t)(c->y + ACTOR_FLAGS),
           (uint16_t)(wram_r16(w, (uint16_t)(c->y + ACTOR_FLAGS)) |
                      ACTOR_ATTR_SET));
  push16(w, c, wram_r16(w, (uint16_t)(c->y + ACTOR_COLLIDE_ID)));
  wram_w16(w, (uint16_t)(c->y + ACTOR_COLLIDE_ID), FREEZE_COLLIDE_ID);
  ran(k, TS_FREEZE_BEGIN);
  lda(c, FREEZE_TICKS);
  ran(k, TS_LDA_IMM);
  c->pc = FREEZE_YIELD_PC;
}

// `$81:84F2`.
void freeze_pass(Wram* w, const Rom* rom, PortCpu* c, ThreadStretchWork* k) {
  (void)rom;
  const uint16_t left = (uint16_t)(page(w, c, FREEZE_DP_HITS) - 1);
  set_page(w, c, FREEZE_DP_HITS, left);
  ran(k, TS_FREEZE_COUNT);
  if ((left & 0x8000u) == 0) {
    PORT_COVER(freeze_held);
    ran(k, TS_TAKEN);
    lda(c, FREEZE_TICKS);
    ran(k, TS_LDA_IMM);
    c->pc = FREEZE_YIELD_PC;
    return;
  }
  PORT_COVER(freeze_over);
  c->y = page(w, c, FREEZE_DP_RECORD);
  wram_w16(w, (uint16_t)(c->y + ACTOR_FLAGS),
           (uint16_t)(wram_r16(w, (uint16_t)(c->y + ACTOR_FLAGS)) &
                      ~ACTOR_ATTR_SET));
  lda(c, pull16(w, c));
  wram_w16(w, (uint16_t)(c->y + ACTOR_COLLIDE_ID), c->a);
  ran(k, TS_FREEZE_END);
  c->pc = FREEZE_OVER_PC;
}

// --- the chainsaw maniac's cut -----------------------------------------------

// `$81:92D6`: the cut is called, and its first instruction is watched.
void cut_enter(Wram* w, const Rom* rom, PortCpu* c, ThreadStretchWork* k) {
  (void)rom;
  push16(w, c, CUT_RETURN);
  ran(k, TS_JSR);
  c->pc = CUT_HEARD_PC;
}

// `$81:93E2`: `JSR ($945D,X)` by the side, to the draw that begins either
// way of aiming.
void cut_aim(Wram* w, const Rom* rom, PortCpu* c, ThreadStretchWork* k) {
  c->a = asl16(c, page(w, c, CHAINSAW_DP_CUT_SIDE));
  c->x = c->a;
  push16(w, c, CUT_AIM_RETURN);
  ran(k, TS_CUT_AIM);
  c->pc = (CUT_AIMS & 0xff0000u) | rom_word(rom, CUT_AIMS + c->x);
}

// `$81:93D3`.
void cut_heard(Wram* w, const Rom* rom, PortCpu* c, ThreadStretchWork* k) {
  lda(c, wram_r16(w, W_APU_SOUNDS));
  cmp16(c, c->a, 0);
  ran(k, TS_CUT_HEARD);
  if (c->a == 0) {
    PORT_COVER(cut_heard);
    lda(c, CUT_SOUND);
    ran(k, TS_LDA_IMM);
    c->pc = CUT_SOUND_PC;
    return;
  }
  PORT_COVER(cut_unheard);
  ran(k, TS_TAKEN);
  cut_aim(w, rom, c, k);
}

static bool cut_across(const PortCpu* c) { return c->pc < CUT_DOWN_PC; }

// `$81:9326` or `$81:9362`, with the draw in A.
void cut_drawn(Wram* w, const Rom* rom, PortCpu* c, ThreadStretchWork* k) {
  (void)rom;
  const bool across = cut_across(c);
  const uint16_t spread =
      asl16(c, asl16(c, (uint16_t)((c->a & 3u) + 1)));
  set_page(w, c, CUT_DP_SPREAD, spread);
  const uint16_t from =
      page(w, c, across ? CHAINSAW_DP_X : CHAINSAW_DP_Y) & ~(CUT_SQUARE - 1);
  set_c(c, false);
  uint16_t at = adc16(c, from, across ? CUT_ACROSS_INTO : CUT_DOWN_INTO);
  at = adc16(c, at, spread);
  set_page(w, c, across ? CHAINSAW_DP_TRY_X : CHAINSAW_DP_TRY_Y, at);
  c->y = page(w, c, CHAINSAW_DP_RECORD);
  wram_w16(w, (uint16_t)(c->y + (across ? ACTOR_X : ACTOR_Y)), at);
  ran(k, TS_CUT_DRAWN);
  lda(c, 1);
  ran(k, TS_LDA_IMM);
  c->pc = across ? CUT_ACROSS_SLEEP_PC : CUT_DOWN_SLEEP_PC;
}

// `$81:9347` or `$81:9383`: who is at the place tried?
void cut_woke(Wram* w, const Rom* rom, PortCpu* c, ThreadStretchWork* k) {
  (void)rom;
  const bool across = cut_across(c);
  lda(c, page(w, c, CHAINSAW_DP_RECORD));
  ldx(c, page(w, c, across ? CHAINSAW_DP_TRY_X : CHAINSAW_DP_X));
  ldy(c, page(w, c, across ? CHAINSAW_DP_Y : CHAINSAW_DP_TRY_Y));
  ran(k, TS_CUT_WOKE);
  c->pc = across ? CUT_ACROSS_ASK_PC : CUT_DOWN_ASK_PC;
}

// `$81:9351` or `$81:938D`, with carry set if somebody is there.
void cut_placed(Wram* w, const Rom* rom, PortCpu* c, ThreadStretchWork* k) {
  const bool across = cut_across(c);
  ran(k, TS_BRANCH);  // BCC
  if (flag(c, PORT_P_C)) {
    PORT_COVER(cut_in_the_way);
    ran(k, TS_BRANCH);  // BRA
    ran(k, TS_TAKEN);
    c->pc = across ? CUT_ACROSS_PC : CUT_DOWN_PC;
    return;
  }
  PORT_COVER(cut_placed);
  ran(k, TS_TAKEN);
  lda(c, page(w, c, across ? CHAINSAW_DP_TRY_X : CHAINSAW_DP_TRY_Y));
  set_page(w, c, across ? CHAINSAW_DP_X : CHAINSAW_DP_Y, c->a);
  c->a = asl16(c, asl16(c, c->a));
  set_page(w, c, across ? CHAINSAW_DP_FINE_X : CHAINSAW_DP_FINE_Y, c->a);
  pull16(w, c);
  ran(k, TS_CUT_KEEP);

  // `$81:93E9`: the maniac put there, and the chips asked for beside it.
  c->x = page(w, c, CHAINSAW_DP_RECORD);
  const uint16_t x = page(w, c, CHAINSAW_DP_X);
  const uint16_t y = page(w, c, CHAINSAW_DP_Y);
  wram_w16(w, (uint16_t)(c->x + ACTOR_X), x);
  wram_w16(w, (uint16_t)(c->x + ACTOR_Y), y);
  c->y = asl16(c, asl16(c, page(w, c, CHAINSAW_DP_CUT_SIDE)));
  set_page(w, c, CUT_DP_LIST, rom_word(rom, CUT_FIRST_LISTS + c->y));
  set_page(w, c, CUT_DP_CHIPS_X, x);
  set_c(c, false);
  set_page(w, c, CUT_DP_CHIPS_Y,
           adc16(c, y, rom_word(rom, CUT_FIRST_LISTS + c->y + 2)));
  lda(c, 0);
  set_page(w, c, CUT_DP_CHIPS_Z, 0);
  push16(w, c, CUT_MAKE_RETURN);
  ran(k, TS_CUT_PUT);
  ran(k, TS_JSR);
  c->pc = CUT_MAKE_PC;
}

// The next picture of a list of the chips', through the pointer at `$48`:
// shown, and a sleep, or at the list's end what follows either list.
static void cut_list_next(Wram* w, const Rom* rom, PortCpu* c,
                          ThreadStretchWork* k, bool first) {
  lda(c, bus_r16(w, rom, (((uint32_t)c->db << 16) +
                          page(w, c, CUT_DP_LIST) + c->y) & 0xffffffu));
  ran(k, TS_CUT_NEXT);
  if (c->a != 0) {
    c->x = page(w, c, CHAINSAW_DP_SAW);
    wram_w16(w, (uint16_t)(c->x + ACTOR_META), c->a);
    c->y = (uint16_t)(c->y + 2);
    push16(w, c, c->y);
    lda(c, first ? CUT_FIRST_TICKS : CUT_SECOND_TICKS);
    ran(k, TS_CUT_PICTURE);
    c->pc = first ? CUT_SLEEP_PC : CUT_SECOND_SLEEP_PC;
    return;
  }
  ran(k, TS_TAKEN);
  if (first) {
    PORT_COVER(cut_first_list_over);
    c->y = page(w, c, CHAINSAW_DP_RECORD);
    lda(c, (uint16_t)(ACTOR_DRAW |
                      wram_r16(w, (uint16_t)(c->y + ACTOR_FLAGS))));
    wram_w16(w, (uint16_t)(c->y + ACTOR_FLAGS), c->a);
    ran(k, TS_CUT_LISTED);
    c->pc = CUT_LISTED_PC;
  } else {
    PORT_COVER(cut_second_list_over);
    c->y = page(w, c, CHAINSAW_DP_SAW);
    lda(c, (uint16_t)(wram_r16(w, (uint16_t)(c->y + ACTOR_FLAGS)) &
                      ~ACTOR_PRIORITY_TOP));
    wram_w16(w, (uint16_t)(c->y + ACTOR_FLAGS), c->a);
    lda(c, page(w, c, CHAINSAW_DP_SAW));
    ran(k, TS_CUT_UNMARK);
    c->pc = CUT_FREE_PC;
  }
}

// `$81:939E`, with the chips' record in A.
void cut_made(Wram* w, const Rom* rom, PortCpu* c, ThreadStretchWork* k) {
  push16(w, c, c->a);
  c->y = c->a;
  const uint16_t chips = c->y;
  wram_w16(w, (uint16_t)(chips + ACTOR_X), page(w, c, CUT_DP_CHIPS_X));
  wram_w16(w, (uint16_t)(chips + ACTOR_Z), 0);
  wram_w16(w, (uint16_t)(chips + ACTOR_Y), page(w, c, CUT_DP_CHIPS_Y));
  wram_w16(w, (uint16_t)(chips + ACTOR_META), CUT_CHIPS);
  wram_w16(w, (uint16_t)(chips + ACTOR_META_BANK), CUT_CHIPS_BANK);
  wram_w16(w, (uint16_t)(chips + ACTOR_THREAD),
           wram_r16(w, W_SCHED_CUR_TASK));
  wram_w16(w, (uint16_t)(chips + ACTOR_COLLIDE_ID), 0);
  wram_w16(w, (uint16_t)(chips + ACTOR_FLAGS),
           (uint16_t)(wram_r16(w, (uint16_t)(chips + ACTOR_FLAGS)) |
                      ACTOR_DRAW));
  lda(c, pull16(w, c));
  pull16(w, c);
  ran(k, TS_CUT_MADE);

  set_page(w, c, CHAINSAW_DP_SAW, c->a);
  c->y = c->a;
  uint16_t flags = (uint16_t)(wram_r16(w, (uint16_t)(c->y + ACTOR_FLAGS)) |
                              ACTOR_PRIORITY_TOP);
  wram_w16(w, (uint16_t)(c->y + ACTOR_FLAGS), flags);
  c->x = page(w, c, CHAINSAW_DP_CUT_SIDE);
  cmp16(c, c->x, CUT_SIDE_TURNED);
  ran(k, TS_CUT_MARKED);
  if (c->x == CUT_SIDE_TURNED) {
    PORT_COVER(cut_chips_turned);
    flags |= CUT_TURNED;
    set_nz16(c, flags);
    wram_w16(w, (uint16_t)(c->y + ACTOR_FLAGS), flags);
    ran(k, TS_CUT_SEVEN);
  } else {
    ran(k, TS_TAKEN);
  }
  c->x = page(w, c, CHAINSAW_DP_RECORD);
  wram_w16(w, (uint16_t)(c->x + ACTOR_FLAGS),
           (uint16_t)(wram_r16(w, (uint16_t)(c->x + ACTOR_FLAGS)) &
                      ~ACTOR_DRAW));
  ldy(c, 0);
  ran(k, TS_CUT_SHOWN);
  cut_list_next(w, rom, c, k, true);
}

// `$81:944E` or `$81:94DA`, where a picture's sleep comes back.
void cut_list_again(Wram* w, const Rom* rom, PortCpu* c,
                    ThreadStretchWork* k) {
  const bool first = c->pc == CUT_SHOWN_PC;
  ldy(c, pull16(w, c));
  ran(k, TS_CUT_AGAIN);
  ran(k, TS_TAKEN);
  cut_list_next(w, rom, c, k, first);
}

// `$81:92D9`: back from the first list, the hedge's block for its pair.
bool cut_swap_supported(const Wram* w, const Rom* rom, uint16_t page_at) {
  return tile_block_swap_supported(
      w, rom, wram_r16(w, (uint16_t)(page_at + CHAINSAW_DP_CUT_X)),
      wram_r16(w, (uint16_t)(page_at + CHAINSAW_DP_CUT_Y)));
}

void cut_swap(Wram* w, const Rom* rom, PortCpu* c, BlockSwapWork* k) {
  PORT_COVER(cut_swapped);
  tile_block_swap_ask(w, rom, c, page(w, c, CHAINSAW_DP_CUT_X),
                      page(w, c, CHAINSAW_DP_CUT_Y), k);
  c->pc = CUT_SWAP_PUT_PC;
}

// `$81:930C`: counted, the maniac's page back, and the second list.
void cut_tiled(Wram* w, const Rom* rom, PortCpu* c, ThreadStretchWork* k) {
  wram_w16(w, W_HEDGES_CUT, (uint16_t)(wram_r16(w, W_HEDGES_CUT) + 1));
  c->d = pull16(w, c);
  push16(w, c, CUT_SECOND_RETURN);
  ran(k, TS_CUT_TILED);
  c->a = page(w, c, CHAINSAW_DP_CUT_SIDE);
  c->y = asl16(c, c->a);
  set_page(w, c, CUT_DP_LIST, rom_word(rom, CUT_SECOND_LISTS + c->y));
  ldy(c, 0);
  ran(k, TS_CUT_SECOND);
  cut_list_next(w, rom, c, k, false);
}

// `$81:94EE`: the chips gone, and the maniac's own handler named.
void cut_freed(Wram* w, const Rom* rom, PortCpu* c, ThreadStretchWork* k) {
  (void)rom;
  lda(c, 0xffff);
  set_page(w, c, CHAINSAW_DP_SAW, c->a);
  pull16(w, c);
  ran(k, TS_CUT_FREED);
  c->a = CUT_HANDLER;
  ldy(c, CUT_HANDLER_BANK);
  ran(k, TS_CUT_HANDLER);
  c->pc = CUT_HANDLER_CALL_PC;
}

// `$81:931D`.
void cut_done(Wram* w, const Rom* rom, PortCpu* c, ThreadStretchWork* k) {
  (void)rom;
  set_page(w, c, CHAINSAW_DP_COUNT, 0);
  ran(k, TS_CUT_DONE);
  c->pc = CUT_SETTER_PC;
}

// --- a player's death --------------------------------------------------------

// `$80:CE81`: a state with an end due is ended, by `PEA : DEC : PHA : RTS`
// to the routine at `$72`.
void death_sounded(Wram* w, const Rom* rom, PortCpu* c, ThreadStretchWork* k) {
  (void)rom;
  const uint16_t ends_by = page(w, c, DEATH_DP_ENDS_BY);
  lda(c, ends_by);
  ran(k, TS_DEATH_FLASHING);
  if (ends_by == 0) {
    PORT_COVER(death_no_end_due);
    ran(k, TS_TAKEN);
    c->pc = DEATH_HANDS_PC;
    return;
  }
  PORT_COVER(death_state_ended);
  push16(w, c, DEATH_ENDS_RETURN);
  lda(c, (uint16_t)(ends_by - 1));
  push16(w, c, c->a);
  pull16(w, c);
  ran(k, TS_DEATH_ENDS);
  c->pc = (DEATH_SOUNDED_PC & 0xff0000u) | ends_by;
}

// `$80:CE94`.
void death_shown(Wram* w, const Rom* rom, PortCpu* c, ThreadStretchWork* k) {
  lda(c, page(w, c, DEATH_DP_PICTURES));
  cmp16(c, c->a, DEATH_MONSTER_PICTURES);
  ran(k, TS_DEATH_WHICH);
  c->y = page(w, c, DEATH_DP_RECORD);
  if (c->a == DEATH_MONSTER_PICTURES) {
    PORT_COVER(death_of_the_monster);
    wram_w16(w, (uint16_t)(c->y + ACTOR_META), DEATH_MONSTER_PICTURE);
    lda(c, DEATH_MONSTER_TICKS);
    ran(k, TS_DEATH_MONSTER);
    c->pc = DEATH_SLEEP_PC;
    return;
  }
  PORT_COVER(death_shown_a_list);
  ran(k, TS_TAKEN);
  lda(c, wram_r16(w, (uint16_t)(c->y + ACTOR_META)));
  set_page(w, c, DEATH_DP_PICTURE_KEPT, c->a);
  ldx(c, page(w, c, DEATH_DP_WHO));
  lda(c, rom_word(rom, DEATH_LISTS + c->x));
  ran(k, TS_DEATH_LIST);
  c->pc = DEATH_PLAY_PC;
}

// `$80:CEC3`: a life gone, unless the cheat says not, and with one left
// `$80:D95F`.
static void death_count(Wram* w, PortCpu* c, ThreadStretchWork* k) {
  ldx(c, page(w, c, DEATH_DP_PLAYER));
  if (port_cheats.lives) {
    ran(k, TS_DEATH_LIFE_KEPT);
  } else {
    const uint16_t lives =
        (uint16_t)(wram_r16(w, (uint16_t)(W_PLAYER_LIVES + c->x)) - 1);
    wram_w16(w, (uint16_t)(W_PLAYER_LIVES + c->x), lives);
    set_nz16(c, lives);
    ran(k, TS_DEATH_LIFE);
  }
  if (flag(c, PORT_P_N)) {
    PORT_COVER(death_none_left);
    ran(k, TS_TAKEN);
    c->pc = DEATH_NONE_LEFT_PC;
    return;
  }
  PORT_COVER(death_lives_left);
  c->y = page(w, c, DEATH_DP_RECORD);
  lda(c, page(w, c, DEATH_DP_PICTURE_KEPT));
  wram_w16(w, (uint16_t)(c->y + ACTOR_META), c->a);
  ran(k, TS_DEATH_SHOW);

  push16(w, c, DEATH_AGAIN_RETURN);
  ran(k, TS_JSR);
  ldx(c, page(w, c, DEATH_DP_PLAYER));
  wram_w16(w, (uint16_t)(W_PLAYER_HEALTH + c->x), DEATH_HEALTH);
  set_page(w, c, DEATH_DP_STATE, PLAYER_STATE_FLASHING);
  set_page(w, c, DEATH_DP_ENDS_BY, FLASHING_END_PC & 0xffffu);
  set_page(w, c, DEATH_DP_FRAMES_LEFT, DEATH_FLASHING_FRAMES);
  c->y = page(w, c, DEATH_DP_RECORD);
  lda(c, DEATH_COLLIDE_ID);
  wram_w16(w, (uint16_t)(c->y + ACTOR_COLLIDE_ID), c->a);
  pull16(w, c);
  ran(k, TS_DEATH_AGAIN);
  c->pc = DEATH_BACK_PC;
}

// `$80:CEAA`: the monster's picture after its thirty ticks.
void death_still(Wram* w, const Rom* rom, PortCpu* c, ThreadStretchWork* k) {
  ldx(c, page(w, c, DEATH_DP_WHO));
  lda(c, rom_word(rom, DEATH_PICTURES_AFTER + c->x));
  set_page(w, c, DEATH_DP_PICTURE_KEPT, c->a);
  ran(k, TS_DEATH_STILL);
  ran(k, TS_TAKEN);
  death_count(w, c, k);
}

void death_counted(Wram* w, const Rom* rom, PortCpu* c, ThreadStretchWork* k) {
  (void)rom;
  death_count(w, c, k);
}

// --- the bystander -----------------------------------------------------------

// `$82:DD52`: its setup is called, and asks for a record at once.
void bystander_enter(Wram* w, const Rom* rom, PortCpu* c,
                     ThreadStretchWork* k) {
  (void)rom;
  push16(w, c, BYSTANDER_SETUP_RETURN);
  ran(k, TS_JSR);
  c->pc = BYSTANDER_ALLOC_PC;
}

// `$82:DE87`, with the record in A and in Y. Its picture is read through a
// pointer on the page, from the data bank, which is the thread's.
void bystander_dress(Wram* w, const Rom* rom, PortCpu* c,
                     ThreadStretchWork* k) {
  set_page(w, c, BYSTANDER_DP_SLOT, c->a);
  const uint16_t record = c->y;
  const uint16_t x = page(w, c, BYSTANDER_DP_X);
  wram_w16(w, (uint16_t)(record + ACTOR_X), x);
  const uint16_t left = (uint16_t)(x - BYSTANDER_BOX_LEFT);
  set_page(w, c, BYSTANDER_DP_LEFT, left);
  set_page(w, c, BYSTANDER_DP_RIGHT,
           (uint16_t)(left + BYSTANDER_BOX_WIDTH));
  wram_w16(w, (uint16_t)(record + ACTOR_Z), 0);
  const uint16_t y = page(w, c, BYSTANDER_DP_Y);
  wram_w16(w, (uint16_t)(record + ACTOR_Y), y);
  const uint16_t top = (uint16_t)(y - BYSTANDER_BOX_ABOVE);
  set_page(w, c, BYSTANDER_DP_TOP, top);
  set_page(w, c, BYSTANDER_DP_BOTTOM,
           (uint16_t)(top + BYSTANDER_BOX_HEIGHT));
  set_c(c, false);
  const uint16_t pointer =
      adc16(c, (uint16_t)(page(w, c, BYSTANDER_DP_KIND) << 2),
            BYSTANDER_PICTURES);
  set_page(w, c, BYSTANDER_DP_PICTURE, pointer);
  c->x = 0;
  wram_w16(w, (uint16_t)(record + ACTOR_META),
           bus_r16(w, rom, ((uint32_t)c->db << 16) | pointer));
  wram_w16(w, (uint16_t)(record + ACTOR_META_BANK), BYSTANDER_PICTURE_BANK);
  wram_w16(w, (uint16_t)(record + ACTOR_THREAD),
           wram_r16(w, W_SCHED_CUR_TASK));
  wram_w16(w, (uint16_t)(record + ACTOR_COLLIDE_ID), 0);
  wram_w16(w, (uint16_t)(record + ACTOR_FLAGS),
           (uint16_t)(wram_r16(w, (uint16_t)(record + ACTOR_FLAGS)) |
                      BYSTANDER_FLAGS));
  c->a = BYSTANDER_HANDLER;
  ldy(c, BYSTANDER_HANDLER_BANK);
  ran(k, TS_BYSTANDER_DRESS);
  c->pc = BYSTANDER_HANDLER_CALL_PC;
}

// `$82:DEE8`, and back in the thread for its first sleep.
void bystander_set(Wram* w, const Rom* rom, PortCpu* c, ThreadStretchWork* k) {
  (void)rom;
  set_page(w, c, BYSTANDER_DP_END, 0);
  pull16(w, c);
  ran(k, TS_BYSTANDER_SET);
  lda(c, BYSTANDER_YIELD_TICKS);
  ran(k, TS_LDA_IMM);
  c->pc = BYSTANDER_YIELD_PC;
}

// `JSL rng_next`, made here: the byte in A, with its carry and overflow.
static void draw(Wram* w, PortCpu* c, ThreadStretchWork* k) {
  RngResult r;
  rng_next(w, flag(c, PORT_P_C), &r);
  c->a = r.a;
  set_nz16(c, r.a);
  set_c(c, r.c);
  set_v(c, r.v);
  ran(k, r.v ? TS_RNG_TWICE : TS_RNG_ONCE);
}

// The record in A, as the spider and the walker each put theirs: where the
// thread was begun (`$00` and `$02`, kept at `x_at` and `y_at`), on the
// ground, drawn, and the thread's own.
static void creeper_record(Wram* w, PortCpu* c, uint8_t x_at, uint8_t y_at,
                           uint16_t picture, uint16_t bank) {
  const uint16_t record = c->a;
  set_page(w, c, SPIDER_DP_RECORD, record);
  c->y = record;
  set_page(w, c, x_at, page(w, c, 0x00));
  set_page(w, c, y_at, page(w, c, 0x02));
  wram_w16(w, (uint16_t)(record + ACTOR_X), page(w, c, 0x00));
  wram_w16(w, (uint16_t)(record + ACTOR_Z), 0);
  wram_w16(w, (uint16_t)(record + ACTOR_Y), page(w, c, 0x02));
  wram_w16(w, (uint16_t)(record + ACTOR_META), picture);
  wram_w16(w, (uint16_t)(record + ACTOR_META_BANK), bank);
  wram_w16(w, (uint16_t)(record + ACTOR_THREAD), wram_r16(w, W_SCHED_CUR_TASK));
  wram_w16(w, (uint16_t)(record + ACTOR_COLLIDE_ID), CREEPER_COLLIDE_ID);
  wram_w16(w, (uint16_t)(record + ACTOR_FLAGS),
           (uint16_t)(wram_r16(w, (uint16_t)(record + ACTOR_FLAGS)) |
                      ACTOR_DRAW));
  wram_w16(w, (uint16_t)(record + ACTOR_ATTR), CREEPER_ATTR);
}

// `$83:B27B`: with room, its weight and a record asked for.
void spider_enter(Wram* w, const Rom* rom, PortCpu* c, ThreadStretchWork* k) {
  (void)rom;
  ran(k, TS_BRANCH);
  if (flag(c, PORT_P_C)) {
    PORT_COVER(spider_no_room);
    ran(k, TS_JMP);
    c->pc = SPIDER_NO_ROOM_PC;
    return;
  }
  PORT_COVER(spider_entered);
  ran(k, TS_TAKEN);
  load_up(w, c, SPIDER_LOAD);
  push16(w, c, SPIDER_MADE_RETURN);
  ran(k, TS_SPIDER_ENTER);
  c->pc = SPIDER_ALLOC_PC;
}

// `$83:B34A`: its record, its handler, the way it first wanders, and the
// first sleep.
void spider_made(Wram* w, const Rom* rom, PortCpu* c, ThreadStretchWork* k) {
  (void)rom;
  PORT_COVER(spider_made);
  creeper_record(w, c, SPIDER_DP_X, SPIDER_DP_Y, SPIDER_PICTURE,
                 SPIDER_PICTURE_BANK);
  set_page(w, c, SPIDER_DP_22, 0);
  set_page(w, c, SPIDER_DP_CYCLE, 0);
  set_page(w, c, SPIDER_DP_PICTURE_TIMER, 0);
  set_page(w, c, SPIDER_DP_FATE, 0);
  set_page(w, c, SPIDER_DP_HIT_BY, 0);
  set_page(w, c, 0x7e, 0);
  c->a = SPIDER_HANDLER;
  ldy(c, SPIDER_HANDLER_BANK);
  ran(k, TS_SPIDER_MADE);
  thread_set_handler(w, c);
  ran(k, TS_SET_HANDLER);
  pull16(w, c);
  ran(k, TS_RTS);
  push16(w, c, SPIDER_DRAW_RETURN);
  ran(k, TS_JSR);
  ran(k, TS_SPIDER_DRAW);
  draw(w, c, k);
  // One of the eight ways, doubled.
  lda(c, (uint16_t)((c->a & 0x0007u) + 1));
  lda(c, asl16(c, c->a));
  set_page(w, c, SPIDER_DP_DIRECTION, c->a);
  lda(c, SPIDER_STATE_WANDER);
  set_page(w, c, SPIDER_DP_STATE, c->a);
  pull16(w, c);
  ran(k, TS_SPIDER_WAY);
  set_page(w, c, SPIDER_DP_HIT_BY, 0);
  lda(c, SPIDER_YIELD_TICKS);
  ran(k, TS_SPIDER_FIRST);
  c->pc = SPIDER_YIELD_PC;
}

// --- the square ----------------------------------------------------------------
//
// From where nobody is level with it: a record at the middle of its square,
// heard, its pictures, the tile under it one less, everything round it told,
// its last pictures, and its record freed. Then its place among the five is
// free again, and it ends.

// `$82:F643`: its record, and back to keep it.
void square_made(Wram* w, const Rom* rom, PortCpu* c, ThreadStretchWork* k) {
  (void)rom;
  PORT_COVER(square_made);
  const uint16_t record = c->a;
  ldy(c, record);
  wram_w16(w, (uint16_t)(record + ACTOR_X), page(w, c, SQUARE_DP_MIDDLE_X));
  wram_w16(w, (uint16_t)(record + ACTOR_Z), 0);
  wram_w16(w, (uint16_t)(record + ACTOR_Y), page(w, c, SQUARE_DP_MIDDLE_Y));
  wram_w16(w, (uint16_t)(record + ACTOR_META), SQUARE_PICTURE);
  wram_w16(w, (uint16_t)(record + ACTOR_META_BANK), SQUARE_PICTURE_BANK);
  wram_w16(w, (uint16_t)(record + ACTOR_THREAD), wram_r16(w, W_SCHED_CUR_TASK));
  wram_w16(w, (uint16_t)(record + ACTOR_COLLIDE_ID), SQUARE_COLLIDE_ID);
  wram_w16(w, (uint16_t)(record + ACTOR_FLAGS),
           (uint16_t)(wram_r16(w, (uint16_t)(record + ACTOR_FLAGS)) |
                      ACTOR_DRAW));
  lda(c, record);  // `PLA`
  pull16(w, c);
  ran(k, TS_SQUARE_MADE);
  set_page(w, c, SQUARE_DP_RECORD, c->a);
  lda(c, SQUARE_SOUND);
  ran(k, TS_SQUARE_KEPT);
  c->pc = SQUARE_SOUND_CALL_PC;
}

// `LSR` six times: a pixel to a tile of the block map.
static uint16_t to_tile(PortCpu* c, uint16_t pixel) {
  set_c(c, (pixel & 0x0020u) != 0);
  lda(c, (uint16_t)(pixel >> 6));
  return c->a;
}

// `$82:F60D`: `JSR $F678`, which asks where the cell under it is, with the
// direct page at zero.
void square_shown(Wram* w, const Rom* rom, PortCpu* c, ThreadStretchWork* k) {
  (void)rom;
  push16(w, c, SQUARE_CELL_RETURN);
  ran(k, TS_JSR);
  push16(w, c, c->d);
  wram_w16(w, W_SQUARE_COLUMN, to_tile(c, page(w, c, SQUARE_DP_MIDDLE_X)));
  wram_w16(w, W_SQUARE_ROW, to_tile(c, page(w, c, SQUARE_DP_MIDDLE_Y)));
  lda(c, 0);
  c->d = c->a;
  ldx(c, wram_r16(w, W_SQUARE_COLUMN));
  ldy(c, wram_r16(w, W_SQUARE_ROW));
  ran(k, TS_SQUARE_CELL_AT);
  c->pc = SQUARE_CELL_CALL_PC;
}

// `$82:F69B`: the tile there, less one.
void square_cell(Wram* w, const Rom* rom, PortCpu* c, ThreadStretchWork* k) {
  ldy(c, 0);
  const uint32_t cell = (uint32_t)(wram_r16(w, W_SQUARE_CELL + 2) & 0xffu) << 16 |
                        wram_r16(w, W_SQUARE_CELL);
  lda(c, (uint16_t)(bus_r16(w, rom, cell) - 1));
  ldx(c, wram_r16(w, W_SQUARE_COLUMN));
  ldy(c, wram_r16(w, W_SQUARE_ROW));
  ran(k, TS_SQUARE_TILE);
  c->pc = SQUARE_TILE_CALL_PC;
}

// `$82:F6A9`: back on its own page, and `JSR $F5CB`: everything in a box
// round it is told `$6F`.
void square_tiled(Wram* w, const Rom* rom, PortCpu* c, ThreadStretchWork* k) {
  (void)rom;
  c->d = pull16(w, c);
  set_nz16(c, c->d);
  pull16(w, c);
  ran(k, TS_SQUARE_BACK);
  push16(w, c, 0xf612u);
  ran(k, TS_JSR);
  const uint16_t record = page(w, c, SQUARE_DP_RECORD);
  ldy(c, record);
  set_c(c, true);
  lda(c, sbc16(c, wram_r16(w, (uint16_t)(record + ACTOR_X)), 0x0020));
  wram_w16(w, W_SQUARE_COLUMN, c->a);
  set_c(c, false);
  lda(c, adc16(c, c->a, 0x0040));
  wram_w16(w, W_SQUARE_ROW, c->a);
  set_c(c, true);
  lda(c, sbc16(c, wram_r16(w, (uint16_t)(record + ACTOR_Y)), 0x0038));
  wram_w16(w, W_SQUARE_BOX_TOP, c->a);
  set_c(c, false);
  lda(c, adc16(c, c->a, 0x0040));
  wram_w16(w, W_SQUARE_BOX_BOTTOM, c->a);
  lda(c, SQUARE_BOX_ID);
  wram_w16(w, W_SQUARE_BOX_ID, c->a);
  ran(k, TS_SQUARE_BOX);
  c->pc = SQUARE_BOX_CALL_PC;
}

// `$82:F613`: its last pictures.
void square_told(Wram* w, const Rom* rom, PortCpu* c, ThreadStretchWork* k) {
  (void)w;
  (void)rom;
  PORT_COVER(square_told);
  lda(c, SQUARE_LAST_PICTURES);
  ran(k, TS_LDA_IMM);
  c->pc = SQUARE_PICTURES_CALL_PC;
}

// `$82:F61A`: its record freed.
void square_hidden(Wram* w, const Rom* rom, PortCpu* c, ThreadStretchWork* k) {
  (void)rom;
  lda(c, page(w, c, SQUARE_DP_RECORD));
  ran(k, TS_LDA_DP);
  c->pc = SQUARE_FREE_CALL_PC;
}

// `$82:F620`: done, and back to the pass, which gives its place among the
// five back (`$82:F561`) and ends.
void square_freed(Wram* w, const Rom* rom, PortCpu* c, ThreadStretchWork* k) {
  (void)rom;
  PORT_COVER(square_freed);
  set_page(w, c, SQUARE_DP_DONE, (uint16_t)(page(w, c, SQUARE_DP_DONE) + 1));
  pull16(w, c);
  ran(k, TS_SQUARE_FREED);
  ran(k, TS_SQUARE_DONE);
  ran(k, TS_SQUARE_SLOT_GIVEN);
  lda(c, page(w, c, SQUARE_DP_SLOT_KEPT));
  ldx(c, c->a);
  wram_w16(w, (uint32_t)(W_WALK_SQUARES + c->x), 0xffffu);
  lda(c, 0xffffu);
  // `PLB`, last: the bank is the thread's own.
  set_nz16(c, (uint16_t)(c->db << 8));
  ran(k, TS_SQUARE_SLOT_FREE);
  c->pc = SQUARE_END_PC;
}

// --- the exit door ---------------------------------------------------------

// `$82:F79E`: a tick's sleep, and the search again.
static void exit_door_sleep(PortCpu* c, ThreadStretchWork* k) {
  lda(c, 0x0001);
  ran(k, TS_LDA_IMM);
  c->pc = EXIT_DOOR_SLEEP_PC;
}

// `$82:F7DD`: the place one way from the player, and is it solid?
static void exit_door_try(Wram* w, const Rom* rom, PortCpu* c,
                     ThreadStretchWork* k) {
  const uint16_t record = page(w, c, EXIT_DOOR_DP_PLAYER_RECORD);
  ldy(c, record);
  ldx(c, page(w, c, EXIT_DOOR_DP_WAY));
  set_c(c, false);
  lda(c, adc16(c, wram_r16(w, (uint16_t)(record + ACTOR_X)),
               rom_word(rom, EXIT_DOOR_WAYS + c->x)));
  set_page(w, c, EXIT_DOOR_DP_X, c->a);
  set_c(c, false);
  lda(c, adc16(c, wram_r16(w, (uint16_t)(record + ACTOR_Y)),
               rom_word(rom, EXIT_DOOR_WAYS + 2 + c->x)));
  set_page(w, c, EXIT_DOOR_DP_Y, c->a);
  ldy(c, c->a);
  ldx(c, page(w, c, EXIT_DOOR_DP_X));
  ran(k, TS_EXIT_DOOR_TRY);
  c->pc = EXIT_DOOR_TRY_CALL_PC;
}

// `$82:F895`: the next way round, and after eight a tick's sleep.
static void exit_door_next_way(Wram* w, const Rom* rom, PortCpu* c,
                          ThreadStretchWork* k) {
  lda(c, (uint16_t)(page(w, c, EXIT_DOOR_DP_WAY) + 4));  // `INC A` four times
  cmp16(c, c->a, EXIT_DOOR_WAYS_END);
  ran(k, TS_EXIT_DOOR_NEXT);
  if (c->a != EXIT_DOOR_WAYS_END) {
    ran(k, TS_TAKEN);
  } else {
    lda(c, EXIT_DOOR_FIRST_WAY);
    ran(k, TS_LDA_IMM);
  }
  set_page(w, c, EXIT_DOOR_DP_WAY, c->a);
  const uint16_t tries = (uint16_t)(page(w, c, EXIT_DOOR_DP_TRIES) - 1);
  set_page(w, c, EXIT_DOOR_DP_TRIES, tries);
  set_nz16(c, tries);
  ran(k, TS_EXIT_DOOR_COUNT);
  ran(k, TS_JMP);
  if (tries & 0x8000u) {
    PORT_COVER(exit_door_ways_out);
    ran(k, TS_TAKEN);
    exit_door_sleep(c, k);
    return;
  }
  PORT_COVER(exit_door_way_next);
  exit_door_try(w, rom, c, k);
}

// The `BCS` each answer about a way begins with: true when the way is no
// good, and the next has been tried.
static bool exit_door_way_refused(Wram* w, const Rom* rom, PortCpu* c,
                             ThreadStretchWork* k, bool by_bra) {
  if (!branches(c, PORT_P_C, k)) return false;
  if (by_bra) ran(k, TS_EXIT_DOOR_BRA);  // `BRA $F895`
  exit_door_next_way(w, rom, c, k);
  return true;
}

// `LDX $00 : LDY $02`, for the next question about the same place.
static void exit_door_place(Wram* w, PortCpu* c) {
  ldx(c, page(w, c, EXIT_DOOR_DP_X));
  ldy(c, page(w, c, EXIT_DOOR_DP_Y));
}

// The ground some way above or below the place.
static void exit_door_ground(Wram* w, PortCpu* c, ThreadStretchWork* k,
                        int16_t down, uint32_t call) {
  const uint16_t y = page(w, c, EXIT_DOOR_DP_Y);
  set_c(c, down > 0 ? false : true);
  lda(c, down > 0 ? adc16(c, y, (uint16_t)down)
                  : sbc16(c, y, (uint16_t)-down));
  ldy(c, c->a);
  ldx(c, page(w, c, EXIT_DOOR_DP_X));
  ran(k, TS_EXIT_DOOR_ABOVE);
  c->pc = call;
}

// `$82:F70B`: the search, a tick from now.
void exit_door_enter(Wram* w, const Rom* rom, PortCpu* c, ThreadStretchWork* k) {
  (void)rom;
  PORT_COVER(exit_door_begun);
  push16(w, c, EXIT_DOOR_FIND_RETURN);
  ran(k, TS_JSR);
  exit_door_sleep(c, k);
}

// `$82:F7A5`: where the player is, and is it solid?
void exit_door_look(Wram* w, const Rom* rom, PortCpu* c, ThreadStretchWork* k) {
  (void)rom;
  const uint16_t record = page(w, c, EXIT_DOOR_DP_PLAYER_RECORD);
  ldy(c, record);
  lda(c, wram_r16(w, (uint16_t)(record + ACTOR_X)));
  set_page(w, c, EXIT_DOOR_DP_X, c->a);
  ldx(c, c->a);
  lda(c, wram_r16(w, (uint16_t)(record + ACTOR_Y)));
  set_page(w, c, EXIT_DOOR_DP_Y, c->a);
  ldy(c, c->a);
  ran(k, TS_EXIT_DOOR_LOOK);
  c->pc = EXIT_DOOR_LOOK_CALL_PC;
}

// `$82:F7B7`: a player on solid ground, and the first way to try.
void exit_door_looked(Wram* w, const Rom* rom, PortCpu* c, ThreadStretchWork* k) {
  if (branches(c, PORT_P_C, k)) {
    PORT_COVER(exit_door_player_unsteady);
    exit_door_sleep(c, k);
    return;
  }
  ldx(c, page(w, c, EXIT_DOOR_DP_PLAYER));
  lda(c, wram_r16(w, (uint16_t)(W_FRONTEND_PLAYERS + c->x)));
  ran(k, TS_EXIT_DOOR_ALIVE);
  if (c->a == 0) {
    // `SEC : RTS`, and the `BCS` after the search to the thread's end.
    PORT_COVER(exit_door_nobody);
    set_c(c, true);
    pull16(w, c);
    ran(k, TS_EXIT_DOOR_NOBODY);
    ran(k, TS_BRANCH);
    ran(k, TS_TAKEN);
    c->pc = EXIT_DOOR_END_PC;
    return;
  }
  ran(k, TS_TAKEN);
  lda(c, wram_r16(w, (uint16_t)(W_PLAYERS_OUT + c->x)));
  ran(k, TS_EXIT_DOOR_OUT);
  if (c->a != 0) {
    PORT_COVER(exit_door_other_player);
    lda(c, rom_word(rom, EXIT_DOOR_OTHER_PLAYER + c->x));
    set_page(w, c, EXIT_DOOR_DP_PLAYER, c->a);
    ldx(c, c->a);
    lda(c, wram_r16(w, (uint16_t)(W_PLAYER_A_RECORD + c->x)));
    set_page(w, c, EXIT_DOOR_DP_PLAYER_RECORD, c->a);
    ran(k, TS_EXIT_DOOR_OTHER);
  } else {
    ran(k, TS_TAKEN);
  }
  lda(c, asl16(c, wram_r16(w, (uint16_t)(W_JOY_DIR + c->x))));
  set_page(w, c, EXIT_DOOR_DP_WAY, c->a);
  lda(c, EXIT_DOOR_TRIES);
  set_page(w, c, EXIT_DOOR_DP_TRIES, c->a);
  ran(k, TS_EXIT_DOOR_WAYS);
  exit_door_try(w, rom, c, k);
}

// `$82:F7FA`, `$82:F809` and `$82:F818`: the place itself, 16 above it and
// 32 above it, each solid or not.
void exit_door_tried(Wram* w, const Rom* rom, PortCpu* c, ThreadStretchWork* k) {
  if (exit_door_way_refused(w, rom, c, k, true)) return;
  exit_door_ground(w, c, k, -0x10, EXIT_DOOR_ABOVE_CALL_PC);
}

void exit_door_above(Wram* w, const Rom* rom, PortCpu* c, ThreadStretchWork* k) {
  if (exit_door_way_refused(w, rom, c, k, true)) return;
  exit_door_ground(w, c, k, -0x20, EXIT_DOOR_HIGHER_CALL_PC);
}

void exit_door_higher(Wram* w, const Rom* rom, PortCpu* c, ThreadStretchWork* k) {
  if (exit_door_way_refused(w, rom, c, k, false)) return;
  exit_door_ground(w, c, k, 0x10, EXIT_DOOR_BELOW_CALL_PC);
}

// `$82:F827`: 16 below, and then the map's edge.
void exit_door_below(Wram* w, const Rom* rom, PortCpu* c, ThreadStretchWork* k) {
  if (exit_door_way_refused(w, rom, c, k, false)) return;
  exit_door_place(w, c);
  ran(k, TS_EXIT_DOOR_XY);
  c->pc = EXIT_DOOR_EDGE_CALL_PC;
}

// `$82:F831`: anyone at the place.
void exit_door_edge(Wram* w, const Rom* rom, PortCpu* c, ThreadStretchWork* k) {
  if (exit_door_way_refused(w, rom, c, k, false)) return;
  lda(c, 0);
  exit_door_place(w, c);
  ran(k, TS_EXIT_DOOR_ZERO_XY);
  c->pc = EXIT_DOOR_SOMEONE_CALL_PC;
}

// `$82:F83E`: how far the other player would be.
void exit_door_someone(Wram* w, const Rom* rom, PortCpu* c, ThreadStretchWork* k) {
  if (exit_door_way_refused(w, rom, c, k, false)) return;
  exit_door_place(w, c);
  ran(k, TS_EXIT_DOOR_XY);
  c->pc = EXIT_DOOR_TETHER_CALL_PC;
}

// `LSR` three times: a pixel to a tile.
static uint16_t exit_door_to_tile(PortCpu* c, uint16_t pixel) {
  set_c(c, (pixel & 0x0004u) != 0);
  const uint16_t tile = (uint16_t)(pixel >> 3);
  set_nz16(c, tile);
  return tile;
}

// `$82:F848`: the tile at the place.
void exit_door_tether(Wram* w, const Rom* rom, PortCpu* c, ThreadStretchWork* k) {
  if (exit_door_way_refused(w, rom, c, k, false)) return;
  ran(k, TS_EXIT_DOOR_BRA);
  ldx(c, exit_door_to_tile(c, page(w, c, EXIT_DOOR_DP_X)));
  set_page(w, c, EXIT_DOOR_DP_COLUMN, c->x);
  ldy(c, exit_door_to_tile(c, page(w, c, EXIT_DOOR_DP_Y)));
  set_page(w, c, EXIT_DOOR_DP_ROW, c->y);
  lda(c, c->y);  // `LDA $02` went before the shifts; A is the row's
  ran(k, TS_EXIT_DOOR_TILES);
  c->pc = EXIT_DOOR_TILE_CALL_PC;
}

// `AND #$01FF : CMP $00DC`: is the tile one drawn over sprites? Carry set
// if so.
static void exit_door_tile_test(Wram* w, PortCpu* c, ThreadStretchWork* k) {
  lda(c, (uint16_t)(c->a & 0x01ffu));
  cmp16(c, c->a, wram_r16(w, W_TILE_PRIORITY_BELOW));
  ran(k, TS_EXIT_DOOR_TILE_TEST);
}

// `$82:F862` and `$82:F875`: and the tile three up, and six up.
static void exit_door_tile_above(Wram* w, const Rom* rom, PortCpu* c,
                            ThreadStretchWork* k, uint16_t up, int block,
                            uint32_t call) {
  exit_door_tile_test(w, c, k);
  if (!flag(c, PORT_P_C)) {
    ran(k, TS_TAKEN);
    exit_door_next_way(w, rom, c, k);
    return;
  }
  ldx(c, page(w, c, EXIT_DOOR_DP_COLUMN));
  ldy(c, (uint16_t)(page(w, c, EXIT_DOOR_DP_ROW) - up));  // `DEY`, that many
  ran(k, block);
  c->pc = call;
}

void exit_door_tile(Wram* w, const Rom* rom, PortCpu* c, ThreadStretchWork* k) {
  exit_door_tile_above(w, rom, c, k, 3, TS_EXIT_DOOR_UP_3, EXIT_DOOR_TILE_UP_CALL_PC);
}

void exit_door_tile_up(Wram* w, const Rom* rom, PortCpu* c, ThreadStretchWork* k) {
  exit_door_tile_above(w, rom, c, k, 6, TS_EXIT_DOOR_UP_6, EXIT_DOOR_TILE_TOP_CALL_PC);
}

// `$82:F88B`: the last tile has to be past the line, not on it. Then the
// place is found: its weight, and `JSR $F8D9` for its record.
void exit_door_tile_top(Wram* w, const Rom* rom, PortCpu* c, ThreadStretchWork* k) {
  exit_door_tile_test(w, c, k);
  if (flag(c, PORT_P_Z)) {
    ran(k, TS_TAKEN);
    exit_door_next_way(w, rom, c, k);
    return;
  }
  if (!branches(c, PORT_P_C, k)) {
    exit_door_next_way(w, rom, c, k);
    return;
  }
  PORT_COVER(exit_door_found);
  set_c(c, false);
  pull16(w, c);
  ran(k, TS_EXIT_DOOR_FOUND);
  ran(k, TS_BRANCH);  // the search's `BCS`
  load_up(w, c, EXIT_DOOR_LOAD);
  ran(k, TS_EXIT_DOOR_LOAD);
  push16(w, c, EXIT_DOOR_MAKE_RETURN);
  ran(k, TS_JSR);
  c->pc = EXIT_DOOR_ALLOC_PC;
}

// `$82:F8DD`: its record, at the place, with its picture and nothing to
// touch it yet. Then its handler cleared.
void exit_door_made(Wram* w, const Rom* rom, PortCpu* c, ThreadStretchWork* k) {
  (void)rom;
  PORT_COVER(exit_door_made);
  const uint16_t record = c->y;
  set_page(w, c, EXIT_DOOR_DP_RECORD, c->a);
  wram_w16(w, (uint16_t)(record + ACTOR_X), page(w, c, EXIT_DOOR_DP_X));
  wram_w16(w, (uint16_t)(record + ACTOR_Z), 0);
  wram_w16(w, (uint16_t)(record + ACTOR_Y), page(w, c, EXIT_DOOR_DP_Y));
  wram_w16(w, (uint16_t)(record + ACTOR_META), EXIT_DOOR_PICTURE);
  wram_w16(w, (uint16_t)(record + ACTOR_META_BANK), EXIT_DOOR_PICTURE_BANK);
  wram_w16(w, (uint16_t)(record + ACTOR_THREAD),
           wram_r16(w, W_SCHED_CUR_TASK));
  wram_w16(w, (uint16_t)(record + ACTOR_COLLIDE_ID), 0);
  wram_w16(w, (uint16_t)(record + ACTOR_FLAGS),
           (uint16_t)(wram_r16(w, (uint16_t)(record + ACTOR_FLAGS)) |
                      ACTOR_DRAW));
  lda(c, 0);
  ldy(c, 0);
  ran(k, TS_EXIT_DOOR_MADE);
  c->pc = EXIT_DOOR_UNHANDLED_CALL_PC;
}

// `$82:F929`: it can be touched, and the first two ticks' sleep.
void exit_door_handled(Wram* w, const Rom* rom, PortCpu* c, ThreadStretchWork* k) {
  (void)rom;
  ldy(c, page(w, c, EXIT_DOOR_DP_RECORD));
  lda(c, EXIT_DOOR_ID);
  wram_w16(w, (uint16_t)(c->y + ACTOR_COLLIDE_ID), c->a);
  set_page(w, c, EXIT_DOOR_DP_COUNT, 0);
  pull16(w, c);
  ran(k, TS_EXIT_DOOR_TOUCHABLE);
  lda(c, 0x0002);
  ran(k, TS_LDA_IMM);
  c->pc = EXIT_DOOR_WAIT_CALL_PC;
}

// `$82:F758`: no handler, on the way out.
static void exit_door_goes(PortCpu* c, ThreadStretchWork* k) {
  lda(c, 0);
  ldy(c, 0);
  ran(k, TS_EXIT_DOOR_UNHANDLED);
  c->pc = EXIT_DOOR_GONE_CALL_PC;
}

static void exit_door_waits_on(PortCpu* c, ThreadStretchWork* k) {
  lda(c, 0x0002);
  ran(k, TS_LDA_IMM);
  c->pc = EXIT_DOOR_WAIT_CALL_PC;
}

static void exit_door_pauses(PortCpu* c, ThreadStretchWork* k) {
  lda(c, 0x000a);
  ran(k, TS_LDA_IMM);
  c->pc = EXIT_DOOR_PAUSE_CALL_PC;
}

// `$82:F724`: every other tick.
void exit_door_wait(Wram* w, const Rom* rom, PortCpu* c, ThreadStretchWork* k) {
  (void)rom;
  lda(c, page(w, c, EXIT_DOOR_DP_COUNT));
  ran(k, TS_EXIT_DOOR_TOUCHED);
  if (c->a & 0x8000u) {
    PORT_COVER(exit_door_touched);
    ran(k, TS_TAKEN);
    exit_door_pauses(c, k);
    return;
  }
  lda(c, (uint16_t)(wram_r16(w, W_FRONTEND_PLAYERS) |
                    wram_r16(w, W_FRONTEND_PLAYERS + 2)));
  ran(k, TS_EXIT_DOOR_PLAYERS);
  if (c->a == 0) {
    PORT_COVER(exit_door_no_players);
    ran(k, TS_TAKEN);
    exit_door_goes(c, k);
    return;
  }
  lda(c, (uint16_t)(wram_r16(w, W_RESCUED) | wram_r16(w, W_RESCUED + 2)));
  ran(k, TS_EXIT_DOOR_PLAYERS);
  if (c->a != 0) {
    PORT_COVER(exit_door_waiting);
    ran(k, TS_TAKEN);
    exit_door_waits_on(c, k);
    return;
  }
  PORT_COVER(exit_door_untouchable);
  ldx(c, page(w, c, EXIT_DOOR_DP_RECORD));
  wram_w16(w, (uint16_t)(c->x + ACTOR_COLLIDE_ID), 0);
  ran(k, TS_EXIT_DOOR_UNTOUCHABLE);
  exit_door_pauses(c, k);
}

// `$82:F744`: after the ten ticks. The player who touched it, by
// `$80:9D6A`'s side; out, and the door goes.
void exit_door_paused(Wram* w, const Rom* rom, PortCpu* c, ThreadStretchWork* k) {
  (void)rom;
  set_page(w, c, EXIT_DOOR_DP_COUNT, 0);
  lda(c, (uint16_t)(page(w, c, EXIT_DOOR_DP_TOUCHED) & 0xfffeu));
  set_c(c, true);
  lda(c, sbc16(c, c->a, 0x0004));
  ran(k, TS_EXIT_DOOR_SIDE);
  ldx(c, 0);
  cmp16(c, c->a, wram_r16(w, W_SCORE_SLOT_SIDE));
  ran(k, TS_KILLER_SIDE);
  if (c->a == wram_r16(w, W_SCORE_SLOT_SIDE)) {
    ran(k, TS_TAKEN);
  } else {
    ldx(c, 2);
    ran(k, TS_KILLER_OTHER);
  }
  ran(k, TS_RTL);
  lda(c, wram_r16(w, (uint16_t)(W_PLAYERS_OUT + c->x)));
  ran(k, TS_EXIT_DOOR_LEFT);
  if (c->a == 0) {
    PORT_COVER(exit_door_toucher_in);
    ran(k, TS_TAKEN);
    exit_door_waits_on(c, k);
    return;
  }
  PORT_COVER(exit_door_toucher_out);
  exit_door_goes(c, k);
}

// --- a player out ---------------------------------------------------------

// `$80:F354`: the player's flag cleared, and a tick's sleep.
void rest_begun(Wram* w, const Rom* rom, PortCpu* c, ThreadStretchWork* k) {
  (void)rom;
  PORT_COVER(rest_begun);
  ldx(c, page(w, c, PLAYER_OUT_DP_PLAYER));
  wram_w16(w, (uint16_t)(W_PLAYER_FLAG + c->x), 0);
  lda(c, 0x0001);
  ran(k, TS_SLIMED_REST);
  c->pc = REST_SLEEP_PC;
}

// `$80:F360`, for a caller other than the slime: cleared again, to its
// `RTS`.
void rest_ended(Wram* w, const Rom* rom, PortCpu* c, ThreadStretchWork* k) {
  (void)rom;
  PORT_COVER(rest_ended);
  ldx(c, page(w, c, PLAYER_OUT_DP_PLAYER));
  wram_w16(w, (uint16_t)(W_PLAYER_FLAG + c->x), 0);
  ran(k, TS_REST_DONE);
  c->pc = REST_RTS_PC;
}

// `$80:CED8`: out of the game, and none of its rescues count.
void player_out_noted(Wram* w, const Rom* rom, PortCpu* c,
                      ThreadStretchWork* k) {
  (void)rom;
  ldx(c, page(w, c, PLAYER_OUT_DP_PLAYER));
  wram_w16(w, (uint16_t)(W_FRONTEND_PLAYERS + c->x), 0);
  wram_w16(w, (uint16_t)(W_RESCUED + c->x), 0);
  lda(c, page(w, c, PLAYER_OUT_DP_PLAYER));
  ran(k, TS_OUT_NOTED);
  c->pc = PLAYER_OUT_LINK_PC;
}

// `LDA #$7FFF`, to the sleep for good.
static void player_out_sleeps(PortCpu* c, ThreadStretchWork* k) {
  lda(c, PLAYER_OUT_FOREVER);
  ran(k, TS_LDA_IMM);
  c->pc = PLAYER_OUT_SLEEP_PC;
}

// `$80:CEE6`: does it end, or stay for good?
void player_out(Wram* w, const Rom* rom, PortCpu* c, ThreadStretchWork* k) {
  (void)rom;
  lda(c, wram_r16(w, W_NEIGHBOURS_LEFT));
  ran(k, TS_OUT_LEFT);
  bool compare = c->a != 0;
  if (compare) {
    ran(k, TS_TAKEN);
  } else {
    lda(c, (uint16_t)(wram_r16(w, W_RESCUED) | wram_r16(w, W_RESCUED + 2)));
    ran(k, TS_OUT_TWO);
    if (c->a == 0)
      ran(k, TS_TAKEN);
    else
      compare = true;
  }
  if (compare) {
    lda(c, wram_r16(w, W_FRONTEND_PLAYERS));
    cmp16(c, c->a, wram_r16(w, W_FRONTEND_PLAYERS + 2));
    ran(k, TS_OUT_TWO);
    if (c->a != wram_r16(w, W_FRONTEND_PLAYERS + 2)) {
      // `$80:CF16`: its weight given back, and its record filed as none.
      PORT_COVER(player_out_unloads);
      ran(k, TS_TAKEN);
      set_c(c, true);
      lda(c, sbc16(c, wram_r16(w, W_SPAWN_LOAD), PLAYER_OUT_LOAD));
      wram_w16(w, W_SPAWN_LOAD, c->a);
      lda(c, 0);
      ldx(c, page(w, c, PLAYER_OUT_DP_PLAYER));
      ran(k, TS_OUT_UNLOAD);
      c->pc = PLAYER_OUT_FILE_CALL_PC;
      return;
    }
  }
  lda(c, page(w, c, PLAYER_OUT_DP_SENTINEL));
  cmp16(c, c->a, PLAYER_OUT_SENTINEL);
  ran(k, TS_OUT_SENTINEL);
  if (c->a != PLAYER_OUT_SENTINEL) {
    ran(k, TS_TAKEN);
    c->pc = PLAYER_OUT_SHOW_PC;
    return;
  }
  PORT_COVER(player_out_forever);
  player_out_sleeps(c, k);
}

// `$80:CF09`: `BRA`, and the sleep for good again.
void player_out_slept(Wram* w, const Rom* rom, PortCpu* c,
                      ThreadStretchWork* k) {
  (void)w;
  (void)rom;
  ran(k, TS_BRANCH);
  ran(k, TS_TAKEN);
  player_out_sleeps(c, k);
}

// `$80:CF2B` and `$80:CF31`: its two records freed.
void player_out_filed(Wram* w, const Rom* rom, PortCpu* c,
                      ThreadStretchWork* k) {
  (void)rom;
  lda(c, page(w, c, PLAYER_OUT_DP_RECORD));
  ran(k, TS_LDA_DP);
  c->pc = PLAYER_OUT_FREE_CALL_PC;
}

void player_out_freed(Wram* w, const Rom* rom, PortCpu* c,
                      ThreadStretchWork* k) {
  (void)rom;
  lda(c, page(w, c, PLAYER_OUT_DP_SECOND));
  ran(k, TS_LDA_DP);
  c->pc = PLAYER_OUT_FREE_2_CALL_PC;
}

// `PLA`, and the thread's `RTL`.
static void player_out_ends(Wram* w, PortCpu* c, ThreadStretchWork* k) {
  lda(c, pull16(w, c));
  ran(k, TS_PLA);
  c->pc = PLAYER_OUT_RTL_PC;
}

// `$80:CF37`: its first weapon and its tenth item, read through the
// inventory's pointer. Any of either is dropped where it was.
void player_out_freed_2(Wram* w, const Rom* rom, PortCpu* c,
                        ThreadStretchWork* k) {
  const uint32_t at = ((uint32_t)c->db << 16) | page(w, c, PLAYER_OUT_DP_INVENTORY);
  const uint16_t weapon = bus_r16(w, rom, at);
  const uint16_t item = bus_r16(w, rom, at + PLAYER_OUT_ITEM);
  ldx(c, 0);
  ldy(c, PLAYER_OUT_ITEM);
  set_c(c, false);
  lda(c, adc16(c, weapon, item));
  ran(k, TS_OUT_DROPS);
  if (c->a == 0) {
    ran(k, TS_TAKEN);
    player_out_ends(w, c, k);
    return;
  }
  PORT_COVER(player_out_drops);
  wram_w16(w, W_DROPPED_WEAPON, weapon);
  wram_w16(w, W_DROPPED_ITEM, item);
  lda(c, PLAYER_OUT_DROP_RANGE);
  ldx(c, page(w, c, PLAYER_OUT_DP_X));
  ldy(c, page(w, c, PLAYER_OUT_DP_Y));
  ran(k, TS_OUT_DROPPED);
  c->pc = PLAYER_OUT_DROP_CALL_PC;
}

// `$80:CF59`: back from the drop, and the end.
void player_out_dropped(Wram* w, const Rom* rom, PortCpu* c,
                        ThreadStretchWork* k) {
  (void)rom;
  player_out_ends(w, c, k);
}

// --- five ends ---------------------------------------------------------------

// A word of WRAM one more, or one less, and N and Z its.
static void count(Wram* w, PortCpu* c, uint16_t at, int by) {
  const uint16_t n = (uint16_t)(wram_r16(w, at) + by);
  wram_w16(w, at, n);
  set_nz16(c, n);
}

// `$81:CCF7`: a fate that is not zero, in A. Negative is nothing to pay.
void slime_fate(Wram* w, const Rom* rom, PortCpu* c, ThreadStretchWork* k) {
  (void)rom;
  if (branches(c, PORT_P_N, k)) {
    PORT_COVER(slime_died_unpaid);
    c->pc = SLIME_RECORD_END_PC;
    return;
  }
  PORT_COVER(slime_died_paid);
  count(w, c, W_SLIMES_KILLED, 1);
  ldx(c, SLIME_WORTH);
  lda(c, page(w, c, SLIME_DP_KILLER));
  ran(k, TS_SLIME_SCORED);
  c->pc = SLIME_SCORED_CALL_PC;
}

// `LDA #$0000 : TAY`, to `JSL thread_set_handler`: no handler.
static void unhandled(PortCpu* c, ThreadStretchWork* k, uint32_t call) {
  lda(c, 0);
  ldy(c, c->a);
  ran(k, TS_UNHANDLED);
  c->pc = call;
}

// `$81:F1DB`: one fewer decoy out, and no handler.
void decoy_ended(Wram* w, const Rom* rom, PortCpu* c, ThreadStretchWork* k) {
  (void)rom;
  PORT_COVER(decoy_ended);
  count(w, c, W_DECOYS_OUT, -1);
  ran(k, TS_DECOY_ENDED);
  unhandled(c, k, DECOY_UNHANDLED_CALL_PC);
}

// `$81:AC2A` and `$81:9887`: dead, and no handler.
void creature_unhandled(Wram* w, const Rom* rom, PortCpu* c,
                        ThreadStretchWork* k) {
  (void)w;
  (void)rom;
  PORT_COVER(creature_unhandled);
  unhandled(c, k,
            c->pc == WEREWOLF_DEAD_PC ? WEREWOLF_UNHANDLED_CALL_PC
                                      : CHAINSAW_UNHANDLED_CALL_PC);
}

// `$81:AC32`: the werewolf's second record freed, if it has one, and on to
// being paid.
void werewolf_unhandled(Wram* w, const Rom* rom, PortCpu* c,
                        ThreadStretchWork* k) {
  (void)rom;
  lda(c, page(w, c, WEREWOLF_DP_SECOND));
  cmp16(c, c->a, 0xffffu);
  ran(k, TS_WOLF_SECOND);
  if (c->a == 0xffffu) {
    ran(k, TS_TAKEN);
    c->pc = WEREWOLF_SCORED_PC;
    return;
  }
  PORT_COVER(werewolf_second_freed);
  c->pc = WEREWOLF_SECOND_FREE_PC;
}

// `$81:988F`: one more chainsaw maniac killed, and the killer paid.
void chainsaw_unhandled(Wram* w, const Rom* rom, PortCpu* c,
                        ThreadStretchWork* k) {
  (void)rom;
  count(w, c, W_CHAINSAWS_KILLED, 1);
  ldx(c, CHAINSAW_WORTH);
  lda(c, page(w, c, CHAINSAW_DP_KILLER));
  ran(k, TS_CHAINSAW_SCORED);
  c->pc = CHAINSAW_SCORED_CALL_PC;
}

// `$81:989B`: counted by the side that killed it, as `$80:9D6A` finds the
// side; nothing can touch it, and its last pictures.
void chainsaw_scored(Wram* w, const Rom* rom, PortCpu* c,
                     ThreadStretchWork* k) {
  (void)rom;
  PORT_COVER(chainsaw_counted);
  // `AND #$8000 : ASL A : ROL A : ROL A`: bit 15, doubled.
  const bool side_b = (page(w, c, CHAINSAW_DP_KILLER) & 0x8000u) != 0;
  set_c(c, false);
  lda(c, side_b ? 2 : 0);
  ran(k, TS_CHAINSAW_SIDE);
  ldx(c, 0);
  cmp16(c, c->a, wram_r16(w, W_SCORE_SLOT_SIDE));
  ran(k, TS_KILLER_SIDE);
  if (c->a == wram_r16(w, W_SCORE_SLOT_SIDE)) {
    ran(k, TS_TAKEN);
  } else {
    ldx(c, 2);
    ran(k, TS_KILLER_OTHER);
  }
  ran(k, TS_RTL);
  const uint16_t kills = (uint16_t)(W_CHAINSAW_KILLS_BY_SIDE + c->x);
  count(w, c, kills, 1);
  const uint16_t record = page(w, c, CHAINSAW_DP_RECORD);
  ldy(c, record);
  lda(c, 0);
  wram_w16(w, (uint16_t)(record + ACTOR_COLLIDE_ID), c->a);
  lda(c, CHAINSAW_PICTURE_BANK);
  wram_w16(w, (uint16_t)(record + ACTOR_META_BANK), c->a);
  lda(c, CHAINSAW_LAST_PICTURES);
  ran(k, TS_CHAINSAW_COUNTED);
  c->pc = CHAINSAW_LAST_PICTURES_CALL_PC;
}

// `$81:8EF0`: a zombie nobody killed, or one paid for: its record freed.
void zombie_unscored(Wram* w, const Rom* rom, PortCpu* c,
                     ThreadStretchWork* k) {
  (void)rom;
  lda(c, page(w, c, ZOMBIE_DP_RECORD));
  ran(k, TS_LDA_DP);
  c->pc = ZOMBIE_FREE_CALL_PC;
}

// `$81:8EF6`: and its weight given back, to the thread's `RTL`.
void zombie_freed(Wram* w, const Rom* rom, PortCpu* c, ThreadStretchWork* k) {
  (void)rom;
  PORT_COVER(zombie_freed);
  set_c(c, true);
  lda(c, sbc16(c, wram_r16(w, W_SPAWN_LOAD), ZOMBIE_8ED5_LOAD));
  wram_w16(w, W_SPAWN_LOAD, c->a);
  ran(k, TS_ZOMBIE_FREED);
  c->pc = ZOMBIE_RTL_PC;
}

// `$81:E4C1`: dead, and told of nothing more.
void fishman_fate(Wram* w, const Rom* rom, PortCpu* c, ThreadStretchWork* k) {
  (void)w;
  (void)rom;
  PORT_COVER(fishman_died);
  lda(c, 0);
  ldy(c, c->a);
  ran(k, TS_FISH_FATE);
  c->pc = FISHMAN_UNHANDLED_CALL_PC;
}

// `LDA $42 : CMP #$FFFF : BEQ`: is there a second record? True if so.
static bool second_record(Wram* w, PortCpu* c, ThreadStretchWork* k) {
  lda(c, page(w, c, FISHMAN_DP_SECOND));
  cmp16(c, c->a, 0xffffu);
  ran(k, TS_FISH_SECOND);
  if (c->a != 0xffffu) return true;
  ran(k, TS_TAKEN);
  return false;
}

// `$81:E4C9`: the second record freed, and on to being paid.
void fishman_unhandled(Wram* w, const Rom* rom, PortCpu* c,
                       ThreadStretchWork* k) {
  (void)rom;
  if (second_record(w, c, k)) {
    PORT_COVER(fishman_second_freed);
    c->pc = FISHMAN_SECOND_FREE_PC;
    return;
  }
  c->pc = FISHMAN_SCORED_PC;
}

// `$81:E4DF` and `$81:E572`: counted, by the side that killed it, and
// heard. `$80:9D6A` finds the side: 0 for the one in `$1E84`, 2 for the
// other.
void fishman_counted(Wram* w, const Rom* rom, PortCpu* c,
                     ThreadStretchWork* k) {
  (void)rom;
  PORT_COVER(fishman_counted);
  wram_w16(w, W_FISHMEN_KILLED, (uint16_t)(wram_r16(w, W_FISHMEN_KILLED) + 1));
  // `AND #$8000 : ASL : ROL : ROL`: bit 15 of who killed it, doubled.
  const uint16_t side = (page(w, c, FISHMAN_DP_KILLER) & 0x8000u) ? 2 : 0;
  set_c(c, false);
  lda(c, side);
  ran(k, TS_FISH_COUNTED);
  ldx(c, 0);
  cmp16(c, c->a, wram_r16(w, W_SCORE_SLOT_SIDE));
  ran(k, TS_KILLER_SIDE);
  if (c->a == wram_r16(w, W_SCORE_SLOT_SIDE)) {
    ran(k, TS_TAKEN);
  } else {
    ldx(c, 2);
    ran(k, TS_KILLER_OTHER);
  }
  ran(k, TS_RTL);
  const uint16_t kills = (uint16_t)(W_KILLS_BY_SIDE + c->x);
  wram_w16(w, kills, (uint16_t)(wram_r16(w, kills) + 1));
  ldy(c, page(w, c, FISHMAN_DP_RECORD));
  lda(c, 0);
  wram_w16(w, (uint16_t)(c->y + ACTOR_COLLIDE_ID), c->a);
  lda(c, FISHMAN_SOUND);
  ran(k, TS_FISH_UNTOUCHABLE);
  c->pc = c->pc == FISHMAN_COUNTED_PC ? FISHMAN_SOUND_CALL_PC
                                      : FISHMAN_PATROL_SOUND_CALL_PC;
}

// `$81:E59A`: the patrol's weight given back, and its record freed.
void fishman_patrol_ended(Wram* w, const Rom* rom, PortCpu* c,
                          ThreadStretchWork* k) {
  (void)rom;
  PORT_COVER(fishman_patrol_ended);
  set_c(c, true);
  lda(c, sbc16(c, wram_r16(w, W_SPAWN_LOAD), FISHMAN_LOAD));
  wram_w16(w, W_SPAWN_LOAD, c->a);
  lda(c, page(w, c, FISHMAN_DP_RECORD));
  ran(k, TS_FISH_ENDED);
  c->pc = FISHMAN_PATROL_FREE_PC;
}

// `$81:E5AC`: and the second record, by `JML`, or the thread's `RTL`.
void fishman_patrol_freed(Wram* w, const Rom* rom, PortCpu* c,
                          ThreadStretchWork* k) {
  (void)rom;
  if (second_record(w, c, k)) {
    PORT_COVER(fishman_patrol_second_freed);
    c->pc = FISHMAN_PATROL_SECOND_FREE_PC;
    return;
  }
  c->pc = FISHMAN_PATROL_RTL_PC;
}

// `$81:F808`: its weight, and its sound.
void bolt_enter(Wram* w, const Rom* rom, PortCpu* c, ThreadStretchWork* k) {
  (void)rom;
  PORT_COVER(bolt_entered);
  load_up(w, c, BOLT_BUDGET);
  lda(c, BOLT_SOUND);
  ran(k, TS_BOLT_ENTER);
  c->pc = BOLT_SOUND_CALL_PC;
}

// `$81:F819`: `JSR $F89D`, to its record.
void bolt_heard(Wram* w, const Rom* rom, PortCpu* c, ThreadStretchWork* k) {
  (void)rom;
  push16(w, c, BOLT_MADE_RETURN);
  ran(k, TS_JSR);
  c->pc = BOLT_ALLOC_PC;
}

// `$81:F8A1`: its record, its handler, and back to its first sleep.
void bolt_made(Wram* w, const Rom* rom, PortCpu* c, ThreadStretchWork* k) {
  PORT_COVER(bolt_made);
  const uint16_t record = c->a;
  set_page(w, c, BOLT_DP_RECORD, record);
  set_page(w, c, BOLT_DP_SPRITE, record);
  const uint16_t way = page(w, c, BOLT_DP_WAY);
  wram_w16(w, (uint16_t)(record + ACTOR_X), page(w, c, BOLT_DP_X));
  wram_w16(w, (uint16_t)(record + ACTOR_Z), BOLT_START_HEIGHT);
  wram_w16(w, (uint16_t)(record + ACTOR_Y), page(w, c, BOLT_DP_Y));
  wram_w16(w, (uint16_t)(record + ACTOR_FLAGS),
           (uint16_t)(wram_r16(w, (uint16_t)(record + ACTOR_FLAGS)) |
                      rom_word(rom, BOLT_FLAGS + way)));
  wram_w16(w, (uint16_t)(record + ACTOR_META),
           rom_word(rom, BOLT_PICTURES + 2u * way));
  wram_w16(w, (uint16_t)(record + ACTOR_META_BANK), BOLT_PICTURE_BANK);
  wram_w16(w, (uint16_t)(record + ACTOR_THREAD), wram_r16(w, W_SCHED_CUR_TASK));
  ldx(c, page(w, c, BOLT_DP_PLAYER));
  wram_w16(w, (uint16_t)(record + ACTOR_COLLIDE_ID),
           rom_word(rom, BOLT_IDS + c->x));
  wram_w16(w, (uint16_t)(record + ACTOR_FLAGS),
           (uint16_t)(wram_r16(w, (uint16_t)(record + ACTOR_FLAGS)) |
                      BOLT_DRAWN));
  set_page(w, c, BOLT_DP_TOLD, 0);
  set_page(w, c, BOLT_DP_WHICH, 0);
  set_page(w, c, BOLT_DP_PICTURE_LEFT, 0);
  set_page(w, c, BOLT_DP_FRAMES_LEFT, BOLT_FRAMES);
  c->a = BOLT_HANDLER;
  ldy(c, BOLT_HANDLER_BANK);
  ran(k, TS_BOLT_MADE);
  thread_set_handler(w, c);
  ran(k, TS_SET_HANDLER);
  pull16(w, c);
  ran(k, TS_RTS);
  lda(c, 0x0001);
  ran(k, TS_LDA_IMM);
  c->pc = BOLT_SLEEP_PC;
}

// `$80:89C2`: the screen dimmed, and on to the first wait.
void pause_dim(Wram* w, const Rom* rom, PortCpu* c, ThreadStretchWork* k) {
  (void)rom;
  PORT_COVER(pause_dimmed);
  lda(c, PAUSE_DIM);
  wram_w16(w, W_BRIGHTNESS_SHADOW, c->a);
  ran(k, TS_PAUSE_DIM);
  c->pc = PAUSE_HELD_PC;
}

// `$80:89E9`: the volume back up.
void pause_over(Wram* w, const Rom* rom, PortCpu* c, ThreadStretchWork* k) {
  (void)w;
  (void)rom;
  PORT_COVER(pause_over);
  lda(c, PAUSE_VOLUME_UP);
  ran(k, TS_LDA_IMM);
  c->pc = PAUSE_VOLUME_UP_CALL_PC;
}

// `$80:89F0`: the screen bright again, and the `RTL`.
void pause_resumed(Wram* w, const Rom* rom, PortCpu* c, ThreadStretchWork* k) {
  (void)rom;
  PORT_COVER(pause_resumed);
  lda(c, PAUSE_BRIGHT);
  wram_w16(w, W_BRIGHTNESS_SHADOW, c->a);
  ran(k, TS_PAUSE_BRIGHT);
  c->pc = PAUSE_RTL_PC;
}

// `$80:8537`: the main loop's two questions, after the HUD's refresh.
void main_frame_rest(Wram* w, const Rom* rom, PortCpu* c,
                     ThreadStretchWork* k) {
  (void)rom;
  lda(c, (uint16_t)(wram_r16(w, W_HUD_PANEL_ON) |
                    wram_r16(w, W_HUD_PANEL_ON + 2)));
  ran(k, TS_MAIN_PLAYERS);
  if (c->a == 0) {
    PORT_COVER(main_frame_rest_nobody);
    ran(k, TS_TAKEN);
    c->pc = MAINLOOP_NO_PLAYERS_PC;
    return;
  }
  lda(c, wram_r16(w, W_NEIGHBOURS_LEFT));
  ran(k, TS_MAIN_NEIGHBOURS);
  if (c->a == 0) {
    PORT_COVER(main_frame_rest_saved);
    c->pc = MAINLOOP_NO_NEIGHBOURS_PC;
    return;
  }
  PORT_COVER(main_frame_rest);
  ran(k, TS_TAKEN);
  lda(c, MAINLOOP_YIELD_TICKS);
  ran(k, TS_LDA_IMM);
  c->pc = MAINLOOP_YIELD_PC;
}

// `$80:F9D0`: a point of health, if they have one.
void stung(Wram* w, const Rom* rom, PortCpu* c, ThreadStretchWork* k) {
  (void)rom;
  ldx(c, page(w, c, STUNG_DP_PLAYER));
  const uint16_t health = wram_r16(w, (uint16_t)(W_PLAYER_HEALTH + c->x));
  lda(c, health);
  ran(k, TS_STUNG);
  if (health == 0) {
    PORT_COVER(stung_spared);
    ran(k, TS_TAKEN);
    c->pc = STUNG_SPARED_PC;
    return;
  }
  PORT_COVER(stung);
  wram_w16(w, (uint16_t)(W_PLAYER_HEALTH + c->x), (uint16_t)(health - 1));
  lda(c, STUNG_SOUND);
  ran(k, TS_STUNG_HURT);
  c->pc = STUNG_SOUND_CALL_PC;
}

// `$80:F9E2`: the picture, and `JSR player_unhandled`.
void stung_heard(Wram* w, const Rom* rom, PortCpu* c, ThreadStretchWork* k) {
  PORT_COVER(stung_heard);
  c->x = page(w, c, STUNG_DP_WAY);
  c->y = page(w, c, STUNG_DP_RECORD);
  lda(c, rom_word(rom, STUNG_PICTURES + c->x));
  wram_w16(w, (uint16_t)(c->y + ACTOR_META), c->a);
  push16(w, c, STUNG_HEARD_RETURN);
  ran(k, TS_STUNG_SHOWN);
  c->pc = STUNG_UNHANDLED_PC;
}

// `$80:F9F2`: 75 ticks.
void stung_held(Wram* w, const Rom* rom, PortCpu* c, ThreadStretchWork* k) {
  (void)w;
  (void)rom;
  PORT_COVER(stung_held);
  lda(c, STUNG_TICKS);
  ran(k, TS_LDA_IMM);
  c->pc = STUNG_YIELD_PC;
}

// `$81:C6D9`: `JMP $C6EC`, and the state for its next pass.
void footballer_off_pictured(Wram* w, const Rom* rom, PortCpu* c,
                             ThreadStretchWork* k) {
  (void)rom;
  PORT_COVER(footballer_off_pictured);
  ran(k, TS_JMP);
  lda(c, FOOTBALLER_STATE_SENT_OFF);
  set_page(w, c, FOOTBALLER_DP_STATE, c->a);
  ran(k, TS_FOOTBALLER_OFF);
  c->pc = FOOTBALLER_OFF_RTS_PC;
}

// `$81:BAEC`.
void ant_flash(Wram* w, const Rom* rom, PortCpu* c, ThreadStretchWork* k) {
  (void)rom;
  PORT_COVER(ant_flash);
  c->y = page(w, c, ANT_DP_RECORD);
  wram_w16(w, (uint16_t)(c->y + ACTOR_ATTR), ANT_FLASH_ATTR);
  lda(c, ANT_FLASH_TICKS);
  ran(k, TS_ANT_FLASH);
  c->pc = ANT_FLASH_YIELD_PC;
}

// `$81:BAFB`: carry clear, back to the thread.
void ant_flashed(Wram* w, const Rom* rom, PortCpu* c, ThreadStretchWork* k) {
  (void)rom;
  PORT_COVER(ant_flashed);
  c->y = page(w, c, ANT_DP_RECORD);
  lda(c, 0);
  wram_w16(w, (uint16_t)(c->y + ACTOR_ATTR), 0);
  set_c(c, false);
  ran(k, TS_ANT_FLASHED);
  c->pc = ANT_FLASHED_RTL_PC;
}

// `$81:983A`: its weight, seven passes to its first picture, and a record.
void chainsaw_enter(Wram* w, const Rom* rom, PortCpu* c, ThreadStretchWork* k) {
  (void)rom;
  PORT_COVER(chainsaw_entered);
  load_up(w, c, CHAINSAW_LOAD);
  lda(c, CHAINSAW_FIRST_PICTURE_WAIT);
  set_page(w, c, CHAINSAW_DP_PICTURE_WAIT, c->a);
  push16(w, c, CHAINSAW_MADE_RETURN);
  ran(k, TS_CHAINSAW_ENTER);
  c->pc = CHAINSAW_RECORD_CALL_PC;
}

// `$81:9733`: where it is, its record, and its first state.
void chainsaw_made(Wram* w, const Rom* rom, PortCpu* c, ThreadStretchWork* k) {
  (void)rom;
  PORT_COVER(chainsaw_made);
  const uint16_t record = page(w, c, CHAINSAW_DP_RECORD);
  const uint16_t x = page(w, c, 0x00);
  const uint16_t y = page(w, c, 0x02);
  set_page(w, c, CHAINSAW_DP_X, x);
  set_page(w, c, CHAINSAW_DP_FINE_X, (uint16_t)(x << 2));
  set_page(w, c, CHAINSAW_DP_Y, y);
  set_page(w, c, CHAINSAW_DP_FINE_Y, (uint16_t)(y << 2));
  set_page(w, c, CHAINSAW_DP_TURN, CHAINSAW_START_TURN);
  set_page(w, c, CHAINSAW_DP_WAY, 0);
  wram_w16(w, (uint16_t)(record + ACTOR_META), CHAINSAW_PICTURE);
  wram_w16(w, (uint16_t)(record + ACTOR_META_BANK), CHAINSAW_START_PICTURE_BANK);
  wram_w16(w, (uint16_t)(record + ACTOR_FLAGS),
           (uint16_t)(wram_r16(w, (uint16_t)(record + ACTOR_FLAGS)) |
                      ACTOR_DRAW));
  c->y = record;
  wram_w16(w, (uint16_t)(record + ACTOR_ATTR), CHAINSAW_ATTR);
  set_page(w, c, CHAINSAW_DP_7E, 0);
  set_page(w, c, CHAINSAW_DP_KILLER, 0);
  set_page(w, c, CHAINSAW_DP_HEALTH, CHAINSAW_HEALTH);
  set_page(w, c, CHAINSAW_DP_SAW, 0xffffu);
  lda(c, CHAINSAW_4A);
  set_page(w, c, CHAINSAW_DP_4A, c->a);
  // The second `ASL` of the place down leaves the carry.
  set_c(c, (y & 0x4000u) != 0);
  pull16(w, c);
  ran(k, TS_CHAINSAW_MADE);
  // Back in the start: its way, and the state that wanders.
  lda(c, CHAINSAW_START_WAY);
  set_page(w, c, CHAINSAW_DP_WAY, c->a);
  push16(w, c, (uint16_t)(CHAINSAW_SET_PC - 1));
  ran(k, TS_CHAINSAW_WAYS);
  c->pc = CHAINSAW_WANDER_CALL_PC;
}

// `$81:9854`: its entry in the level's list.
void chainsaw_set(Wram* w, const Rom* rom, PortCpu* c, ThreadStretchWork* k) {
  (void)rom;
  PORT_COVER(chainsaw_set);
  lda(c, page(w, c, CHAINSAW_DP_LIST));
  ran(k, TS_LDA_DP);
  c->pc = CHAINSAW_MARK_CALL_PC;
}

// `$81:985A`: `JSR $9811`, which sounds it on a level with no samples of
// its own, and asks for its first pictures.
void chainsaw_marked(Wram* w, const Rom* rom, PortCpu* c,
                     ThreadStretchWork* k) {
  (void)rom;
  push16(w, c, (uint16_t)(CHAINSAW_SHOWN_RETURN));
  ran(k, TS_JSR);
  const uint16_t sounds = wram_r16(w, W_APU_SOUNDS);
  lda(c, sounds);
  cmp16(c, sounds, 0);
  ran(k, TS_CHAINSAW_SOUND_TEST);
  if (sounds == 0) {
    PORT_COVER(chainsaw_sounded);
    lda(c, CHAINSAW_SOUND);
    ran(k, TS_LDA_IMM);
    c->pc = CHAINSAW_SOUND_CALL_PC;
    return;
  }
  PORT_COVER(chainsaw_unheard);
  ran(k, TS_TAKEN);
  lda(c, CHAINSAW_FIRST_PICTURES);
  ran(k, TS_LDA_IMM);
  c->pc = CHAINSAW_PICTURES_CALL_PC;
}

// `$81:9820`: back from the sound.
void chainsaw_heard(Wram* w, const Rom* rom, PortCpu* c, ThreadStretchWork* k) {
  (void)w;
  (void)rom;
  PORT_COVER(chainsaw_heard);
  lda(c, CHAINSAW_FIRST_PICTURES);
  ran(k, TS_LDA_IMM);
  c->pc = CHAINSAW_PICTURES_CALL_PC;
}

// `$81:9827`: back in the start, what can touch it and its handler.
void chainsaw_shown(Wram* w, const Rom* rom, PortCpu* c, ThreadStretchWork* k) {
  (void)rom;
  PORT_COVER(chainsaw_shown);
  pull16(w, c);
  ran(k, TS_RTS);
  const uint16_t record = page(w, c, CHAINSAW_DP_RECORD);
  wram_w16(w, (uint16_t)(record + ACTOR_COLLIDE_ID), CHAINSAW_COLLIDE_ID);
  c->a = CHAINSAW_HANDLER;
  ldy(c, CHAINSAW_HANDLER_BANK);
  ran(k, TS_CHAINSAW_HANDLER);
  c->pc = CHAINSAW_HANDLER_CALL_PC;
}

// `$81:986F`: the first sleep.
void chainsaw_handled(Wram* w, const Rom* rom, PortCpu* c,
                      ThreadStretchWork* k) {
  (void)rom;
  PORT_COVER(chainsaw_handled);
  set_page(w, c, 0x2c, 0);
  lda(c, CHAINSAW_YIELD_TICKS);
  ran(k, TS_CHAINSAW_FIRST);
  c->pc = CHAINSAW_YIELD_PC;
}

// `$81:D708`: with room, its weight and a record asked for.
void walker_enter(Wram* w, const Rom* rom, PortCpu* c, ThreadStretchWork* k) {
  (void)rom;
  ran(k, TS_BRANCH);
  if (flag(c, PORT_P_C)) {
    PORT_COVER(walker_no_room);
    ran(k, TS_TAKEN);
    c->pc = WALKER_NO_ROOM_PC;
    return;
  }
  PORT_COVER(walker_entered);
  load_up(w, c, WALKER_LOAD);
  push16(w, c, WALKER_MADE_RETURN);
  ran(k, TS_WALKER_ENTER);
  c->pc = WALKER_ALLOC_PC;
}

// `$81:D691`: its record, and its first pictures asked for.
void walker_made(Wram* w, const Rom* rom, PortCpu* c, ThreadStretchWork* k) {
  (void)rom;
  PORT_COVER(walker_made);
  creeper_record(w, c, WALKER_DP_X, WALKER_DP_Y, WALKER_PICTURE,
                 WALKER_PICTURE_BANK);
  set_page(w, c, WALKER_DP_HEALTH, WALKER_HEALTH);
  set_page(w, c, WALKER_DP_PICTURE, 0);
  set_page(w, c, WALKER_DP_PICTURE_LEFT, 0);
  set_page(w, c, WALKER_DP_ENDED, 0);
  set_page(w, c, WALKER_DP_SCORED, 0);
  set_page(w, c, 0x7e, 0);
  lda(c, WALKER_FIRST_PICTURES);
  ran(k, TS_WALKER_MADE);
  c->pc = WALKER_PICTURES_CALL_PC;
}

// `$81:D6E5`: its handler, the way it first walks, and the first sleep.
void walker_shown(Wram* w, const Rom* rom, PortCpu* c, ThreadStretchWork* k) {
  (void)rom;
  PORT_COVER(walker_shown);
  c->a = WALKER_HANDLER;
  ldy(c, WALKER_HANDLER_BANK);
  ran(k, TS_WALKER_SHOWN);
  thread_set_handler(w, c);
  ran(k, TS_SET_HANDLER);
  pull16(w, c);
  ran(k, TS_RTS);
  push16(w, c, WALKER_DRAW_RETURN);
  ran(k, TS_JSR);
  ran(k, TS_WALKER_DRAW);
  draw(w, c, k);
  // One of the four straight ways: up, right, down or left, doubled.
  lda(c, asl16(c, (uint16_t)(c->a & 0x0003u)));
  lda(c, (uint16_t)(asl16(c, c->a) + 2));
  set_page(w, c, WALKER_DP_WAY, c->a);
  lda(c, WALKER_STATE_WALK);
  set_page(w, c, WALKER_DP_STATE, c->a);
  pull16(w, c);
  ran(k, TS_WALKER_WAY);
  set_page(w, c, WALKER_DP_SCORED, 0);
  lda(c, WALKER_SLEEP_FRAMES);
  ran(k, TS_WALKER_FIRST);
  c->pc = WALKER_SLEEP_PC;
}

// `$80:DC1E`: covered, and a tick's sleep in `$80:F354`.
void slimed(Wram* w, const Rom* rom, PortCpu* c, ThreadStretchWork* k) {
  (void)rom;
  PORT_COVER(slimed);
  // What they fire with, the walk's pace and its timer, and `$5A`.
  static const uint8_t CLEARED[] = {0x1e, 0x20, 0x54, 0x56, 0x5a};
  for (unsigned i = 0; i < sizeof CLEARED; i++) set_page(w, c, CLEARED[i], 0);
  c->y = page(w, c, SLIMED_DP_COVER);
  c->x = page(w, c, SLIMED_DP_RECORD);
  wram_w16(w, (uint16_t)(c->y + ACTOR_FLAGS),
           (uint16_t)(wram_r16(w, (uint16_t)(c->x + ACTOR_FLAGS)) |
                      SLIMED_COVER_OVER));
  wram_w16(w, (uint16_t)(c->y + ACTOR_META), SLIMED_COVER_PICTURE);
  wram_w16(w, (uint16_t)(c->y + ACTOR_META_BANK), SLIMED_COVER_BANK);
  wram_w16(w, (uint16_t)(c->y + ACTOR_X), page(w, c, SLIMED_DP_X));
  wram_w16(w, (uint16_t)(c->y + ACTOR_Y),
           (uint16_t)(page(w, c, SLIMED_DP_Y) + 1));
  wram_w16(w, (uint16_t)(c->y + ACTOR_Z), SLIMED_COVER_HEIGHT);
  set_page(w, c, SLIMED_DP_STATE, SLIMED_STATE);
  set_page(w, c, SLIMED_DP_ENDS_BY, SLIMED_ENDS_BY);
  set_page(w, c, SLIMED_DP_TICKS, SLIMED_TICKS);
  push16(w, c, SLIMED_REST_RETURN);
  ran(k, TS_SLIMED);
  ldx(c, page(w, c, SLIMED_DP_PLAYER));
  wram_w16(w, (uint16_t)(W_PLAYER_FLAG + c->x), 0);
  lda(c, 1);
  ran(k, TS_SLIMED_REST);
  c->pc = SLIMED_YIELD_PC;
}

// `$80:F360`, back to the slime's `$80:DC63`: the word cleared again, and
// what the player holds put aside.
void slimed_rested(Wram* w, const Rom* rom, PortCpu* c,
                   ThreadStretchWork* k) {
  (void)rom;
  PORT_COVER(slimed_rested);
  ldx(c, page(w, c, SLIMED_DP_PLAYER));
  wram_w16(w, (uint16_t)(W_PLAYER_FLAG + c->x), 0);
  pull16(w, c);
  ran(k, TS_SLIMED_RESTED);
  lda(c, SLIMED_WEAPON);
  ldy(c, SLIMED_ITEM);
  push16(w, c, SLIMED_HANDS_RETURN);
  player_hands_swap(w, c);
  ran(k, TS_HANDS_SWAP);
  pull16(w, c);
  ran(k, TS_SLIMED_HANDS);
  c->pc = POSE_STAND_BEGIN_PC;
}

// `$80:DD79`: down in the water, and the splash asked for.
void leap_splash(Wram* w, const Rom* rom, PortCpu* c, ThreadStretchWork* k) {
  (void)rom;
  PORT_COVER(leap_splash);
  set_page(w, c, LEAP_DP_X, page(w, c, LEAP_DP_AIM_X));
  set_page(w, c, LEAP_DP_Y, page(w, c, LEAP_DP_AIM_Y));
  c->y = page(w, c, LEAP_DP_RECORD);
  wram_w16(w, (uint16_t)(c->y + LEAP_RECORD_PICTURE_BANK), LEAP_PICTURE_BANK);
  lda(c, LEAP_SPLASH_PICTURES);
  ran(k, TS_LEAP_SPLASH);
  c->pc = LEAP_SPLASH_CALL_PC;
}

void soak_splash(Wram* w, const Rom* rom, PortCpu* c, ThreadStretchWork* k) {
  (void)rom;
  PORT_COVER(soak_splash);
  c->y = page(w, c, LEAP_DP_RECORD);
  wram_w16(w, (uint16_t)(c->y + LEAP_RECORD_PICTURE_BANK), LEAP_PICTURE_BANK);
  lda(c, SOAK_SPLASH_PICTURES);
  ran(k, TS_SOAK_SPLASH);
  c->pc = SOAK_SPLASH_CALL_PC;
}

// The spin's picture for the way it is at, and its sleep.
static void stagger_picture(Wram* w, const Rom* rom, PortCpu* c,
                            ThreadStretchWork* k) {
  c->y = page(w, c, STAGGER_DP_RECORD);
  c->x = page(w, c, STAGGER_DP_WAY);
  wram_w16(w, (uint16_t)(c->y + ACTOR_META),
           rom_word(rom, STAGGER_SPIN_PICTURES + c->x));
  lda(c, STAGGER_SPIN_TICKS);
  ran(k, TS_STAGGER_PICTURE);
  c->pc = STAGGER_SPIN_YIELD_PC;
}

// `$81:9643`.
void stagger_begin(Wram* w, const Rom* rom, PortCpu* c,
                   ThreadStretchWork* k) {
  PORT_COVER(stagger_began);
  c->a = STAGGER_HANDLER;
  c->y = STAGGER_BANK;
  ran(k, TS_STAGGER_HANDLER);
  thread_set_handler(w, c);
  ran(k, TS_SET_HANDLER);
  set_page(w, c, STAGGER_DP_DAZES, STAGGER_DAZES);
  set_page(w, c, STAGGER_DP_4A, STAGGER_DP_4A_VALUE);
  set_page(w, c, STAGGER_DP_WAY,
           (uint16_t)(page(w, c, STAGGER_DP_FACING) - 2));
  const uint16_t record = page(w, c, STAGGER_DP_RECORD);
  const uint16_t flags = wram_r16(w, (uint16_t)(record + ACTOR_FLAGS));
  set_page(w, c, STAGGER_DP_FLAGS, flags);
  wram_w16(w, (uint16_t)(record + ACTOR_FLAGS),
           (uint16_t)(flags & ~STAGGER_FLAG_CLEARED));
  set_page(w, c, STAGGER_DP_SPINS, STAGGER_SPINS);
  ran(k, TS_STAGGER_BEGIN);
  stagger_picture(w, rom, c, k);
}

// `$81:9680`.
void stagger_spin(Wram* w, const Rom* rom, PortCpu* c, ThreadStretchWork* k) {
  set_page(w, c, STAGGER_DP_WAY,
           (uint16_t)((page(w, c, STAGGER_DP_WAY) + 2) & 0x000eu));
  const uint16_t left = (uint16_t)(page(w, c, STAGGER_DP_SPINS) - 1);
  set_page(w, c, STAGGER_DP_SPINS, left);
  ran(k, TS_STAGGER_TURN);
  if (left != 0) {
    PORT_COVER(stagger_spun);
    ran(k, TS_TAKEN);
    stagger_picture(w, rom, c, k);
    return;
  }
  PORT_COVER(stagger_spun_out);
  c->y = page(w, c, STAGGER_DP_RECORD);
  wram_w16(w, (uint16_t)(c->y + ACTOR_FLAGS), page(w, c, STAGGER_DP_FLAGS));
  wram_w16(w, (uint16_t)(c->y + ACTOR_COLLIDE_ID), STAGGER_COLLIDE_DAZED);
  wram_w16(w, (uint16_t)(c->y + ACTOR_META_BANK),
           STAGGER_PICTURE_BANK_DAZED);
  lda(c, STAGGER_SHOWN_LIST);
  ran(k, TS_STAGGER_SHOWN);
  c->pc = STAGGER_SHOWN_CALL_PC;
}

// `$81:96A7`.
void stagger_dazed(Wram* w, const Rom* rom, PortCpu* c,
                   ThreadStretchWork* k) {
  (void)rom;
  set_page(w, c, STAGGER_DP_STATE, STAGGER_RESUME);
  lda(c, STAGGER_DAZE_LIST);
  ran(k, TS_STAGGER_DAZED);
  c->pc = STAGGER_DAZE_CALL_PC;
}

// `$81:96B3`.
void stagger_daze(Wram* w, const Rom* rom, PortCpu* c, ThreadStretchWork* k) {
  (void)rom;
  const uint16_t left = (uint16_t)(page(w, c, STAGGER_DP_DAZES) - 1);
  set_page(w, c, STAGGER_DP_DAZES, left);
  ran(k, TS_STAGGER_DAZE);
  if (!(left & 0x8000u)) {
    PORT_COVER(stagger_dazed_again);
    ran(k, TS_TAKEN);
    lda(c, STAGGER_DAZE_LIST);
    ran(k, TS_LDA_IMM);
    c->pc = STAGGER_DAZE_CALL_PC;
    return;
  }
  PORT_COVER(stagger_dazed_last);
  lda(c, STAGGER_LAST_LIST);
  ran(k, TS_STAGGER_LAST);
  c->pc = STAGGER_LAST_CALL_PC;
}

// `$81:96BE`.
void stagger_over(Wram* w, const Rom* rom, PortCpu* c, ThreadStretchWork* k) {
  (void)rom;
  PORT_COVER(stagger_over);
  c->y = page(w, c, STAGGER_DP_RECORD);
  wram_w16(w, (uint16_t)(c->y + ACTOR_META_BANK),
           STAGGER_OWN_PICTURE_BANK);
  wram_w16(w, (uint16_t)(c->y + ACTOR_META), STAGGER_OWN_PICTURE);
  wram_w16(w, (uint16_t)(c->y + ACTOR_COLLIDE_ID), STAGGER_COLLIDE_OWN);
  set_page(w, c, STAGGER_DP_4A, STAGGER_DP_4A_VALUE);
  c->a = STAGGER_OWN_HANDLER;
  c->y = STAGGER_BANK;
  ran(k, TS_STAGGER_OVER);
  thread_set_handler(w, c);
  ran(k, TS_SET_HANDLER);
  ran(k, TS_STAGGER_BACK);
  c->pc = STAGGER_BACK_PC;
}

// --- a locked door's buzz ----------------------------------------------------

// `$82:DEFB`: one buzzing already, and this one ends. The word is read
// through the data bank, which shows low WRAM.
void door_buzz(Wram* w, const Rom* rom, PortCpu* c, ThreadStretchWork* k) {
  (void)rom;
  lda(c, wram_r16(w, W_DOOR_BUZZING));
  ran(k, TS_DOOR_ASK);
  if (c->a != 0) {
    PORT_COVER(door_buzzing_already);
    ran(k, TS_TAKEN);
    c->pc = DOOR_END_PC;
    return;
  }
  PORT_COVER(door_buzz_begun);
  wram_w16(w, W_DOOR_BUZZING, (uint16_t)(c->a + 1));
  lda(c, DOOR_BUZZ_SFX);
  ran(k, TS_DOOR_BUZZ);
  c->pc = DOOR_BUZZ_SOUND_PC;
}

// `$82:DF1F`.
void door_buzzed(Wram* w, const Rom* rom, PortCpu* c, ThreadStretchWork* k) {
  (void)rom;
  wram_w16(w, W_DOOR_BUZZING, 0);
  ran(k, TS_DOOR_DONE);
  c->pc = DOOR_END_PC;
}

// --- a slime's start ---------------------------------------------------------

// `$81:CCCF`.
void slime_enter(Wram* w, const Rom* rom, PortCpu* c, ThreadStretchWork* k) {
  (void)rom;
  load_up(w, c, SLIME_LOAD);
  ran(k, TS_SLIME_ENTER);
  c->pc = SLIME_BEGIN_CALL_PC;
}

// `$81:CCAC`: its handler, and back from `slime_begin` to the draw.
void slime_shown(Wram* w, const Rom* rom, PortCpu* c, ThreadStretchWork* k) {
  (void)rom;
  c->a = SLIME_HANDLER;
  c->y = SLIME_HANDLER_BANK;
  ran(k, TS_SLIME_HANDLER);
  thread_set_handler(w, c);
  ran(k, TS_SET_HANDLER);
  pull16(w, c);
  ran(k, TS_RTS);
  push16(w, c, SLIME_WAY_RETURN);
  ran(k, TS_JSR);
  c->pc = SLIME_DRAW_PC;
}

// `$81:C9E8`: the draw in A becomes a way, and the thread's first sleep.
void slime_way(Wram* w, const Rom* rom, PortCpu* c, ThreadStretchWork* k) {
  (void)rom;
  const uint16_t way = (uint16_t)((c->a & 0x0003u) * 4 + 2);
  set_page(w, c, SLIME_DP_WAY, way);
  set_c(c, false);  // the `ASL`s' carry
  lda(c, SLIME_STATE_FIRST);
  set_page(w, c, SLIME_DP_STATE, c->a);
  ran(k, TS_SLIME_WAY);
  pull16(w, c);
  ran(k, TS_RTS);
  set_page(w, c, SLIME_DP_22, 0);
  lda(c, SLIME_YIELD_TICKS);
  ran(k, TS_SLIME_FIRST);
  c->pc = SLIME_FIRST_YIELD_PC;
}

// `$82:DE08`: a frame of the two words stepping. The table is in bank $7E.
void bystander_step(Wram* w, const Rom* rom, PortCpu* c,
                    ThreadStretchWork* k) {
  wram_w16(w, W_BYSTANDER_GROWS,
           (uint16_t)(wram_r16(w, W_BYSTANDER_GROWS) + 2));
  c->a = (uint16_t)(wram_r16(w, W_BYSTANDER_SHRINKS) - 1);
  wram_w16(w, W_BYSTANDER_SHRINKS, c->a);
  cmp16(c, c->a, BYSTANDER_SHRUNK);
  ran(k, TS_BYSTANDER_STEP);
  if (c->a != BYSTANDER_SHRUNK) {
    PORT_COVER(bystander_stepped);
    ran(k, TS_TAKEN);
    c->pc = BYSTANDER_WAIT_PC;
    return;
  }
  PORT_COVER(bystander_stepped_last);
  c->x = asl16(c, page(w, c, BYSTANDER_DP_KIND));
  c->x = rom_word(rom, BYSTANDER_WORDS + c->x);
  lda(c, BYSTANDER_WORDS_BANK);
  ran(k, TS_BYSTANDER_SAY);
  c->pc = BYSTANDER_SAY_PC;
}

// `$82:DDA7`, with a player in the box.
void bystander_meets(Wram* w, const Rom* rom, PortCpu* c,
                     ThreadStretchWork* k) {
  (void)rom;
  // `LDA $7E6DB0 : SEC : SBC $0020 : BPL : EOR #$FFFF : INC`.
  set_c(c, true);
  lda(c, sbc16(c, wram_r16(w, W_BYSTANDER_MET_AT), wram_r16(w, W_FRAME_COUNT)));
  ran(k, TS_BYSTANDER_MEETS);
  if (c->a & 0x8000u) {
    lda(c, (uint16_t)((c->a ^ 0xffffu) + 1));
    ran(k, TS_BYSTANDER_NEGATED);
  } else {
    ran(k, TS_TAKEN);
  }
  cmp16(c, c->a, BYSTANDER_MEET_GAP);
  ran(k, TS_BYSTANDER_GAP);
  if (c->a < BYSTANDER_MEET_GAP) {
    PORT_COVER(bystander_too_soon);
    c->pc = BYSTANDER_TOO_SOON_PC;
    return;
  }
  PORT_COVER(bystander_meets);
  ran(k, TS_TAKEN);
  lda(c, wram_r16(w, W_FRAME_COUNT));
  wram_w16(w, W_BYSTANDER_MET_AT, c->a);
  wram_w16(w, W_PLAYER_FLAG, 0);
  wram_w16(w, W_PLAYER_FLAG + 2, 0);
  lda(c, BYSTANDER_MEETS_TICKS);
  ran(k, TS_BYSTANDER_KEPT);
  c->pc = BYSTANDER_MEETS_SLEEP_PC;
}

// `$82:DDCF` and `$82:DDD6`: each player's side of the panel.
void bystander_hushed(Wram* w, const Rom* rom, PortCpu* c,
                      ThreadStretchWork* k) {
  (void)w;
  (void)rom;
  ldx(c, 0x0000);
  ran(k, TS_BYSTANDER_LDX);
  c->pc = BYSTANDER_HUSH_CALL_PC;
}

void bystander_hushed_2(Wram* w, const Rom* rom, PortCpu* c,
                        ThreadStretchWork* k) {
  (void)w;
  (void)rom;
  ldx(c, 0x0002);
  ran(k, TS_BYSTANDER_LDX);
  c->pc = BYSTANDER_HUSH_2_CALL_PC;
}

// `$82:DE31`: the pads kept, and the other of its two pictures.
static void bystander_flips(Wram* w, const Rom* rom, PortCpu* c,
                            ThreadStretchWork* k) {
  lda(c, wram_r16(w, W_JOY_RAW));
  set_page(w, c, BYSTANDER_DP_PADS, c->a);
  lda(c, wram_r16(w, W_JOY_RAW + 2));
  set_page(w, c, BYSTANDER_DP_PADS + 2, c->a);
  lda(c, page(w, c, BYSTANDER_DP_FLIP) ^ 0x0001u);
  set_page(w, c, BYSTANDER_DP_FLIP, c->a);
  ldy(c, asl16(c, c->a));
  ldx(c, page(w, c, BYSTANDER_DP_SLOT));
  lda(c, bus_r16(w, rom, (uint32_t)c->db << 16 |
                             (uint16_t)(page(w, c, BYSTANDER_DP_PICTURE) + c->y)));
  wram_w16(w, (uint16_t)(c->x + ACTOR_META), c->a);
  ran(k, TS_BYSTANDER_FLIP);
  c->pc = BYSTANDER_FLIP_CALL_PC;
}

// `$82:DE2F`, its words up: the first picture of the two next.
void bystander_said(Wram* w, const Rom* rom, PortCpu* c,
                    ThreadStretchWork* k) {
  set_page(w, c, BYSTANDER_DP_FLIP, 0);
  ran(k, TS_BYSTANDER_SAID);
  bystander_flips(w, rom, c, k);
}

// `$82:DEF7`, after a `WAI`: one frame fewer.
void wait_frames(Wram* w, const Rom* rom, PortCpu* c, ThreadStretchWork* k) {
  (void)w;
  (void)rom;
  lda(c, (uint16_t)(c->a - 1));
  ran(k, TS_WAIT_FRAMES);
  if (c->a != 0) {
    PORT_COVER(wait_frames_more);
    ran(k, TS_TAKEN);
    c->pc = WAIT_FRAMES_WAI_PC;
    return;
  }
  PORT_COVER(wait_frames_done);
  c->pc = WAIT_FRAMES_RTL_PC;
}

// `$82:DE56`, ten frames on: neither pad changed, and the pictures go on;
// or one did, and its first picture is put back.
void bystander_looked(Wram* w, const Rom* rom, PortCpu* c,
                      ThreadStretchWork* k) {
  const uint16_t first = wram_r16(w, W_JOY_RAW);
  lda(c, first);
  cmp16(c, first, page(w, c, BYSTANDER_DP_PADS));
  ran(k, TS_BYSTANDER_LOOKED);
  bool same = first == page(w, c, BYSTANDER_DP_PADS);
  if (same) {
    const uint16_t second = wram_r16(w, W_JOY_RAW + 2);
    lda(c, second);
    cmp16(c, second, page(w, c, BYSTANDER_DP_PADS + 2));
    ran(k, TS_BYSTANDER_LOOKED_B);
    same = second == page(w, c, BYSTANDER_DP_PADS + 2);
    if (same) {
      PORT_COVER(bystander_watched);
      ran(k, TS_TAKEN);
      bystander_flips(w, rom, c, k);
      return;
    }
  } else {
    ran(k, TS_TAKEN);
  }
  PORT_COVER(bystander_left_be);
  ldy(c, page(w, c, BYSTANDER_DP_SLOT));
  ldx(c, 0x0000);
  lda(c, bus_r16(w, rom, (uint32_t)c->db << 16 |
                             page(w, c, BYSTANDER_DP_PICTURE)));
  wram_w16(w, (uint16_t)(c->y + ACTOR_META), c->a);
  ran(k, TS_BYSTANDER_DONE);
  c->pc = BYSTANDER_DONE_CALL_PC;
}

// `$82:DD64`, back from what it showed: five ticks, or its end.
void bystander_back(Wram* w, const Rom* rom, PortCpu* c,
                    ThreadStretchWork* k) {
  (void)rom;
  lda(c, page(w, c, BYSTANDER_DP_END));
  ran(k, TS_BYSTANDER_BACK);
  if (c->a == 0) {
    PORT_COVER(bystander_back_slept);
    ran(k, TS_TAKEN);
    lda(c, BYSTANDER_YIELD_TICKS);
    ran(k, TS_LDA_IMM);
    c->pc = BYSTANDER_YIELD_PC;
    return;
  }
  PORT_COVER(bystander_back_ended);
  lda(c, page(w, c, BYSTANDER_DP_SLOT));
  ran(k, TS_BYSTANDER_END);
  c->pc = BYSTANDER_END_PC;
}

// --- the seeker's states that sleep ------------------------------------------

// The loop, from a state's `RTS`: the sleep, or with `$2E` set its end.
static void seeker_return(Wram* w, PortCpu* c, ThreadStretchWork* k) {
  pull16(w, c);
  lda(c, page(w, c, SEEKER_DP_ENDED));
  ran(k, TS_SEEKER_TAIL);
  if (c->a != 0) {
    PORT_COVER(seeker_loop_ended);
    c->pc = SEEKER_FRAME_ENDED_PC;
    return;
  }
  PORT_COVER(seeker_loop_slept);
  ran(k, TS_TAKEN);
  lda(c, page(w, c, SEEKER_DP_SLEEP));
  ran(k, TS_SEEKER_SLEEP);
  c->pc = SEEKER_FRAME_SLEEP_PC;
}

// `$82:EB5E` and `$82:EB74`: the next state.
static void seeker_set(Wram* w, PortCpu* c, uint16_t state,
                       ThreadStretchWork* k) {
  lda(c, state);
  set_page(w, c, SEEKER_DP_STATE, c->a);
  ran(k, TS_SEEKER_SET);
}

// Its record drawn, `LDY $08 : LDA $0000,Y : ORA #$8000 : STA $0000,Y`.
static void seeker_show(Wram* w, PortCpu* c) {
  ldy(c, page(w, c, SEEKER_DP_RECORD));
  lda(c, (uint16_t)(wram_r16(w, (uint16_t)(c->y + ACTOR_FLAGS)) | ACTOR_DRAW));
  wram_w16(w, (uint16_t)(c->y + ACTOR_FLAGS), c->a);
}

// `$82:EA35`: landed from the swoop. Sound `$27`, where the sample set is
// the one with it, and the pictures of landing.
void seeker_land(Wram* w, const Rom* rom, PortCpu* c, ThreadStretchWork* k) {
  lda(c, wram_r16(w, W_APU_SOUNDS));
  cmp16(c, c->a, SEEKER_LAND_SOUNDS);
  ran(k, TS_SEEKER_LAND_ASK);
  if (c->a == SEEKER_LAND_SOUNDS) {
    PORT_COVER(seeker_land_heard);
    lda(c, SEEKER_LAND_SFX);
    ran(k, TS_SEEKER_LAND_SOUND);
    c->pc = SEEKER_LAND_SFX_CALL_PC;
    return;
  }
  PORT_COVER(seeker_land_unheard);
  ran(k, TS_TAKEN);
  seeker_land_sounded(w, rom, c, k);
}

// `$82:EA44`.
void seeker_land_sounded(Wram* w, const Rom* rom, PortCpu* c,
                         ThreadStretchWork* k) {
  (void)w;
  (void)rom;
  lda(c, SEEKER_LAND_PICTURES);
  ran(k, TS_SEEKER_LAND_PICTURES);
  c->pc = SEEKER_LAND_PICTURES_CALL_PC;
}

// `$82:EA4B`: its health, and things that touch it told to its handler
// again. Then a draw.
void seeker_landed(Wram* w, const Rom* rom, PortCpu* c,
                   ThreadStretchWork* k) {
  (void)rom;
  set_page(w, c, SEEKER_DP_HURT, SEEKER_LANDED_HEALTH);
  lda(c, SEEKER_HANDLER);
  ldy(c, SEEKER_HANDLER_BANK);
  ran(k, TS_SEEKER_LANDED);
  thread_set_handler(w, c);
  ran(k, TS_SET_HANDLER);
  c->pc = SEEKER_LAND_DRAW_PC;
}

// `$82:EDFD`: it fires, drawn, from the next frame.
static void seeker_go_fire(Wram* w, PortCpu* c, ThreadStretchWork* k) {
  lda(c, SEEKER_STATE_FIRE);
  set_page(w, c, SEEKER_DP_STATE, c->a);
  seeker_show(w, c);
  ran(k, TS_SEEKER_GO_FIRE);
  seeker_return(w, c, k);
}

// `$82:EA5E`: the draw says whether it hides or darts after firing.
void seeker_land_way(Wram* w, const Rom* rom, PortCpu* c,
                     ThreadStretchWork* k) {
  (void)rom;
  lda(c, (uint16_t)(c->a & SEEKER_AFTER_DART));
  set_page(w, c, SEEKER_DP_AFTER, c->a);
  ran(k, TS_SEEKER_LAND_WAY);
  seeker_go_fire(w, c, k);
}

// `$82:EE29`: a tracker from where it is, sent the way `$22` says.
static void seeker_spawn(Wram* w, PortCpu* c, ThreadStretchWork* k) {
  set_page(w, c, SEEKER_DP_SPAWN_X, page(w, c, SEEKER_DP_X));
  set_page(w, c, SEEKER_DP_SPAWN_Y, page(w, c, SEEKER_DP_Y));
  set_page(w, c, SEEKER_DP_SPAWN_WAY, page(w, c, SEEKER_DP_TRACKER_WAY));
  lda(c, SEEKER_TRACKER_THREAD);
  ldy(c, SEEKER_BANK);
  ran(k, TS_SEEKER_SPAWN);
  c->pc = SEEKER_SPAWN_CALL_PC;
}

// `$82:EE1B`: its firing pictures played, as many trackers as its strength
// leaves room for, the first the way it faces.
void seeker_fired(Wram* w, const Rom* rom, PortCpu* c, ThreadStretchWork* k) {
  (void)rom;
  set_c(c, true);
  uint16_t n = sbc16(c, SEEKER_TRACKERS_FROM, page(w, c, SEEKER_DP_STRENGTH));
  for (int i = 0; i < 5; i++) {  // the `LSR`s
    set_c(c, (n & 1) != 0);
    n = (uint16_t)(n >> 1);
  }
  lda(c, (uint16_t)(n + 1));
  set_page(w, c, SEEKER_DP_TRACKERS, c->a);
  ran(k, TS_SEEKER_FIRED);
  seeker_spawn(w, c, k);
}

// `$82:EE3F`: three ticks' sleep before the next, or after the last its
// last pictures.
void seeker_spawned(Wram* w, const Rom* rom, PortCpu* c,
                    ThreadStretchWork* k) {
  const uint16_t left = (uint16_t)(page(w, c, SEEKER_DP_TRACKERS) - 1);
  set_page(w, c, SEEKER_DP_TRACKERS, left);
  set_nz16(c, left);
  ran(k, TS_SEEKER_SPAWNED);
  if (left != 0) {
    PORT_COVER(seeker_spawns_more);
    lda(c, SEEKER_SPAWN_TICKS);
    ran(k, TS_SEEKER_PAUSE);
    c->pc = SEEKER_SPAWN_SLEEP_PC;
    return;
  }
  PORT_COVER(seeker_spawned_all);
  ran(k, TS_TAKEN);
  c->x = asl16(c, page(w, c, SEEKER_DP_WAY));
  lda(c, rom_word(rom, SEEKER_FIRED_PICTURES + c->x));
  ran(k, TS_SEEKER_LAST);
  c->pc = SEEKER_FIRED_CALL_PC;
}

// `$82:EE4A`: the next tracker a way round from the last, and from the
// first way to the eighth.
void seeker_spawn_again(Wram* w, const Rom* rom, PortCpu* c,
                        ThreadStretchWork* k) {
  (void)rom;
  const uint16_t way = (uint16_t)(page(w, c, SEEKER_DP_TRACKER_WAY) - 2);
  set_page(w, c, SEEKER_DP_TRACKER_WAY, way);
  set_nz16(c, way);
  ran(k, TS_SEEKER_AGAIN);
  if (way != 0) {
    PORT_COVER(seeker_way_on);
    ran(k, TS_TAKEN);
  } else {
    PORT_COVER(seeker_way_wrapped);
    lda(c, (SEEKER_WAYS - 1) * 2);
    set_page(w, c, SEEKER_DP_TRACKER_WAY, c->a);
    ran(k, TS_SEEKER_WRAP);
  }
  seeker_spawn(w, c, k);
}

// `$82:EE62`: after firing, by `$36`, through `$82:E7AF`.
void seeker_next(Wram* w, const Rom* rom, PortCpu* c, ThreadStretchWork* k) {
  (void)rom;
  ldx(c, page(w, c, SEEKER_DP_AFTER));
  ran(k, TS_SEEKER_NEXT);
  switch (c->x) {
    case SEEKER_AFTER_HIDE:
      PORT_COVER(seeker_next_hides);
      seeker_set(w, c, SEEKER_STATE_WATCH_B, k);
      break;
    case SEEKER_AFTER_FIRE:
      PORT_COVER(seeker_next_fires);
      seeker_set(w, c, SEEKER_STATE_WATCH, k);
      break;
    default:
      PORT_COVER(seeker_next_darts);
      lda(c, SEEKER_STATE_HOLD);
      set_page(w, c, SEEKER_DP_STATE, c->a);
      seeker_show(w, c);
      ran(k, TS_SEEKER_SET_HOLD);
      break;
  }
  seeker_return(w, c, k);
}

// `$82:EBA3`: its going played, it is not drawn, its record is put at the
// corner of the level, and it keeps where whoever it watched was.
void seeker_hidden(Wram* w, const Rom* rom, PortCpu* c,
                   ThreadStretchWork* k) {
  (void)rom;
  ldy(c, page(w, c, SEEKER_DP_RECORD));
  lda(c, (uint16_t)(wram_r16(w, (uint16_t)(c->y + ACTOR_FLAGS)) & ~ACTOR_DRAW));
  wram_w16(w, (uint16_t)(c->y + ACTOR_FLAGS), c->a);
  wram_w16(w, (uint16_t)(c->y + ACTOR_META_BANK), SEEKER_PICTURE_BANK);
  wram_w16(w, (uint16_t)(c->y + ACTOR_X), 0);
  wram_w16(w, (uint16_t)(c->y + ACTOR_Y), 0);
  ldx(c, page(w, c, SEEKER_DP_TARGET));
  set_page(w, c, SEEKER_DP_AHEAD_X, wram_r16(w, (uint16_t)(c->x + ACTOR_X)));
  set_page(w, c, SEEKER_DP_AHEAD_Y, wram_r16(w, (uint16_t)(c->x + ACTOR_Y)));
  lda(c, SEEKER_STATE_PLACE);
  set_page(w, c, SEEKER_DP_STATE, c->a);
  ran(k, TS_SEEKER_HIDDEN);
  seeker_return(w, c, k);
}

// `$82:EC61`: back, so things that touch it are told to its handler again.
// It watches.
void seeker_back(Wram* w, const Rom* rom, PortCpu* c, ThreadStretchWork* k) {
  (void)rom;
  ldy(c, page(w, c, SEEKER_DP_RECORD));
  wram_w16(w, (uint16_t)(c->y + ACTOR_COLLIDE_ID), SEEKER_COLLIDE_ID);
  lda(c, SEEKER_HANDLER);
  ldy(c, SEEKER_HANDLER_BANK);
  ran(k, TS_SEEKER_BACK);
  thread_set_handler(w, c);
  ran(k, TS_SET_HANDLER);
  ran(k, TS_SEEKER_BACK_ON);
  seeker_set(w, c, SEEKER_STATE_WATCH, k);
  seeker_return(w, c, k);
}

// --- the seeker's start --------------------------------------------------------

// `$82:EF36`: its place in the level's list, to be marked gone.
void seeker_enter(Wram* w, const Rom* rom, PortCpu* c, ThreadStretchWork* k) {
  (void)rom;
  PORT_COVER(seeker_entered);
  lda(c, page(w, c, SEEKER_DP_GONE));
  ran(k, TS_LDA_DP);
  c->pc = SEEKER_GONE_CALL_PC;
}

// `$82:EF3C`: its weight, and `JSR $EF8B` to its record.
void seeker_gone(Wram* w, const Rom* rom, PortCpu* c, ThreadStretchWork* k) {
  (void)rom;
  load_up(w, c, SEEKER_LOAD);
  push16(w, c, SEEKER_MADE_RETURN);
  ran(k, TS_SEEKER_LOAD);
  c->pc = SEEKER_ALLOC_PC;
}

// `$82:E827`, then the `RTS` of the start's `JSR` and the first sleep: it
// comes at whoever is nearest, from the next frame.
static void seeker_started(Wram* w, PortCpu* c, ThreadStretchWork* k) {
  set_page(w, c, SEEKER_DP_SLEEP, 1);
  set_page(w, c, SEEKER_DP_STATE, SEEKER_STATE_CHASE);
  set_page(w, c, SEEKER_DP_FRAMES, SEEKER_FLAP_FRAMES);
  set_page(w, c, SEEKER_DP_PICTURE,
           (uint16_t)(page(w, c, SEEKER_DP_PICTURE) & 0x0001u));
  ldy(c, page(w, c, SEEKER_DP_RECORD));
  wram_w16(w, (uint16_t)(c->y + ACTOR_FLAGS),
           (uint16_t)(wram_r16(w, (uint16_t)(c->y + ACTOR_FLAGS)) |
                      ACTOR_PRIORITY_TOP));
  lda(c, 0);
  ldy(c, 0);
  ran(k, TS_SEEKER_RESET);
  thread_set_handler(w, c);
  ran(k, TS_SET_HANDLER);
  lda(c, page(w, c, SEEKER_DP_STRENGTH));
  ran(k, TS_SEEKER_ALIVE);
  ran(k, TS_TAKEN);
  pull16(w, c);
  ran(k, TS_RTS);
  lda(c, page(w, c, SEEKER_DP_SLEEP));
  ran(k, TS_SEEKER_SLEEP);
  c->pc = SEEKER_FRAME_SLEEP_PC;
}

// `$82:EF8F`: its record, at the place it was begun at, with its picture
// and its id, drawn; its first turn and its strength; and with the sample
// set that has it, its sound.
void seeker_made(Wram* w, const Rom* rom, PortCpu* c, ThreadStretchWork* k) {
  (void)rom;
  const uint16_t record = c->a;
  set_page(w, c, SEEKER_DP_RECORD, record);
  ldy(c, record);
  const uint16_t x = page(w, c, SEEKER_DP_SPAWN_X);
  set_page(w, c, SEEKER_DP_X, x);
  wram_w16(w, (uint16_t)(record + ACTOR_X), x);
  wram_w16(w, (uint16_t)(record + ACTOR_Z), 0);
  const uint16_t y = page(w, c, SEEKER_DP_SPAWN_Y);
  set_page(w, c, SEEKER_DP_Y, y);
  wram_w16(w, (uint16_t)(record + ACTOR_Y), y);
  wram_w16(w, (uint16_t)(record + ACTOR_META), SEEKER_START_PICTURE);
  wram_w16(w, (uint16_t)(record + ACTOR_META_BANK), SEEKER_PICTURE_BANK);
  wram_w16(w, (uint16_t)(record + ACTOR_THREAD), wram_r16(w, W_SCHED_CUR_TASK));
  wram_w16(w, (uint16_t)(record + ACTOR_COLLIDE_ID), SEEKER_COLLIDE_ID);
  wram_w16(w, (uint16_t)(record + ACTOR_ATTR), SEEKER_START_ATTR);
  wram_w16(w, (uint16_t)(record + ACTOR_FLAGS),
           (uint16_t)(wram_r16(w, (uint16_t)(record + ACTOR_FLAGS)) |
                      ACTOR_DRAW));
  set_page(w, c, SEEKER_DP_TURN, SEEKER_START_TURN);
  set_page(w, c, SEEKER_DP_STRENGTH, SEEKER_START_STRENGTH);
  set_page(w, c, SEEKER_DP_ENDED, 0);
  set_page(w, c, SEEKER_DP_HIT_ID, 0);
  set_page(w, c, SEEKER_DP_FREEZE_HITS, 0);
  set_page(w, c, SEEKER_DP_AFTER, 0);
  lda(c, wram_r16(w, W_APU_SOUNDS));
  cmp16(c, c->a, SEEKER_LAND_SOUNDS);
  ran(k, TS_SEEKER_MADE);
  if (c->a == SEEKER_LAND_SOUNDS) {
    PORT_COVER(seeker_start_heard);
    lda(c, SEEKER_START_SFX);
    ran(k, TS_LDA_IMM);
    c->pc = SEEKER_START_SFX_CALL_PC;
    return;
  }
  PORT_COVER(seeker_start_unheard);
  ran(k, TS_TAKEN);
  seeker_sounded(w, rom, c, k);
}

// `$82:EFED`: `JMP $E827`.
void seeker_sounded(Wram* w, const Rom* rom, PortCpu* c,
                    ThreadStretchWork* k) {
  (void)rom;
  ran(k, TS_SEEKER_START);
  seeker_started(w, c, k);
}

// --- a creature bubbled ------------------------------------------------------

void bubbled(Wram* w, const Rom* rom, PortCpu* c, ThreadStretchWork* k) {
  (void)rom;
  PORT_COVER(bubbled);
  const uint16_t record = page(w, c, WEED_DP_RECORD);
  c->y = record;
  c->x = wram_r16(w, W_SCHED_CUR_TASK);
  // `SEP #$20 : PHA : REP #$20`: the bank a byte.
  lda(c, wram_r16(w, (uint16_t)(W_THREAD_HANDLER_BANK + c->x)));
  push8(w, c, (uint8_t)c->a);
  lda(c, wram_r16(w, (uint16_t)(W_THREAD_HANDLER + c->x)));
  push16(w, c, c->a);
  push16(w, c, wram_r16(w, (uint16_t)(record + ACTOR_META)));
  push16(w, c, wram_r16(w, (uint16_t)(record + ACTOR_META_BANK)));
  push16(w, c, wram_r16(w, (uint16_t)(record + ACTOR_COLLIDE_ID)));
  wram_w16(w, (uint16_t)(record + ACTOR_COLLIDE_ID), BUBBLED_COLLIDE_ID);
  wram_w16(w, (uint16_t)(record + ACTOR_META_BANK), BUBBLED_PICTURE_BANK);
  c->a = BUBBLED_HANDLER;
  c->y = BUBBLED_BANK;
  set_nz16(c, c->y);
  ran(k, TS_BUBBLED);
  c->pc = BUBBLED_HANDLER_CALL_PC;
}

// The handler's bank comes back a byte, into an 8-bit Y.
void bubble_over(Wram* w, const Rom* rom, PortCpu* c, ThreadStretchWork* k) {
  (void)rom;
  PORT_COVER(bubble_over);
  const uint16_t record = page(w, c, WEED_DP_RECORD);
  c->y = record;
  wram_w16(w, (uint16_t)(record + ACTOR_COLLIDE_ID), pull16(w, c));
  wram_w16(w, (uint16_t)(record + ACTOR_META_BANK), pull16(w, c));
  wram_w16(w, (uint16_t)(record + ACTOR_META), pull16(w, c));
  c->a = pull16(w, c);
  c->y = pull8(w, c);
  set_nz8(c, (uint8_t)c->y);
  ran(k, TS_BUBBLE_OVER);
  c->pc = BUBBLE_OVER_JML_PC;
}

// --- the starts of records 36's and 48's things -------------------------------

// `$82:F25D`: its weight, and `JSR $F2EC` to its record.
void breakable_enter(Wram* w, const Rom* rom, PortCpu* c,
                     ThreadStretchWork* k) {
  (void)rom;
  PORT_COVER(breakable_entered);
  load_up(w, c, BREAKABLE_LOAD);
  push16(w, c, BREAKABLE_MADE_RETURN);
  ran(k, TS_BREAKABLE_ENTER);
  c->pc = BREAKABLE_ALLOC_PC;
}

// `$82:F2F0`: its record, where it was begun, drawn and touchable, its first
// picture for two ticks; back from the `JSR`, five hits to take, and to its
// handler. The record keeps the word at `$0008`, whatever that is to it.
void breakable_made(Wram* w, const Rom* rom, PortCpu* c,
                    ThreadStretchWork* k) {
  (void)rom;
  const uint16_t record = c->a;
  set_page(w, c, HITS_WAIT_DP_RECORD, record);
  c->x = record;
  const uint16_t x = page(w, c, 0x00);
  wram_w16(w, (uint16_t)(record + ACTOR_X), x);
  set_page(w, c, BREAKABLE_DP_X, x);
  wram_w16(w, (uint16_t)(record + ACTOR_Z), 0);
  const uint16_t y = page(w, c, 0x02);
  wram_w16(w, (uint16_t)(record + ACTOR_Y), y);
  set_page(w, c, BREAKABLE_DP_Y, y);
  wram_w16(w, (uint16_t)(record + ACTOR_META), BREAKABLE_PICTURE);
  wram_w16(w, (uint16_t)(record + ACTOR_META_BANK), BREAKABLE_PICTURE_BANK);
  wram_w16(w, (uint16_t)(record + ACTOR_THREAD), wram_r16(w, W_SCHED_CUR_TASK));
  wram_w16(w, (uint16_t)(record + ACTOR_COLLIDE_ID), BREAKABLE_COLLIDE_ID);
  wram_w16(w, (uint16_t)(record + ACTOR_FLAGS),
           (uint16_t)(wram_r16(w, (uint16_t)(record + ACTOR_FLAGS)) |
                      ACTOR_DRAW));
  set_page(w, c, HITS_WAIT_DP_PICTURE, 0);
  set_page(w, c, HITS_WAIT_DP_HIT_BY, 0);
  set_page(w, c, HITS_WAIT_DP_LEFT, BREAKABLE_TICKS);
  ran(k, TS_BREAKABLE_MADE);
  pull16(w, c);
  set_page(w, c, HITS_WAIT_DP_HITS, BREAKABLE_HITS);
  c->a = BREAKABLE_HANDLER;
  c->y = BREAKABLE_BANK;
  set_nz16(c, c->y);
  ran(k, TS_BREAKABLE_HANDLER);
  c->pc = BREAKABLE_HANDLER_CALL_PC;
}

// `$81:A455`: its weight and its count, and `JSR $A410` to `record_begin`.
void watcher_enter(Wram* w, const Rom* rom, PortCpu* c, ThreadStretchWork* k) {
  (void)rom;
  PORT_COVER(watcher_entered);
  load_up(w, c, WATCHER_LOAD);
  lda(c, WATCHER_COUNT);
  set_page(w, c, IDLE_WAIT_DP_LEFT, c->a);
  push16(w, c, WATCHER_BEGUN_RETURN);
  ran(k, TS_WATCHER_ENTER);
  c->pc = WATCHER_BEGIN_CALL_PC;
}

// `$81:A414`: its record's picture, drawn over everything and touchable, its
// place kept, and the rest of its page cleared, `$81:A191` too; back from the
// `JSR`, and to its handler.
void watcher_begun(Wram* w, const Rom* rom, PortCpu* c, ThreadStretchWork* k) {
  (void)rom;
  set_page(w, c, IDLE_WAIT_DP_PICTURE, 0);
  const uint16_t record = page(w, c, IDLE_WAIT_DP_RECORD);
  const uint16_t x = page(w, c, 0x00);
  set_page(w, c, IDLE_WAIT_DP_X, x);
  wram_w16(w, (uint16_t)(record + ACTOR_X), x);
  wram_w16(w, (uint16_t)(record + ACTOR_Z), 0);
  const uint16_t y = page(w, c, 0x02);
  set_page(w, c, IDLE_WAIT_DP_Y, y);
  wram_w16(w, (uint16_t)(record + ACTOR_Y), y);
  wram_w16(w, (uint16_t)(record + ACTOR_META), WATCHER_PICTURE);
  wram_w16(w, (uint16_t)(record + ACTOR_META_BANK), WATCHER_PICTURE_BANK);
  wram_w16(w, (uint16_t)(record + ACTOR_FLAGS),
           (uint16_t)(wram_r16(w, (uint16_t)(record + ACTOR_FLAGS)) |
                      WATCHER_FLAGS));
  wram_w16(w, (uint16_t)(record + ACTOR_ATTR), WATCHER_ATTR);
  set_page(w, c, IDLE_WAIT_DP_LEFT, 0);
  set_page(w, c, IDLE_WAIT_DP_PICTURE, 0);
  set_page(w, c, 0x12, 0);
  set_page(w, c, 0x7e, 0);
  set_page(w, c, IDLE_WAIT_DP_GONE, 0);
  ran(k, TS_WATCHER_BEGUN);
  // `$81:A191`.
  set_page(w, c, 0x2e, 0xffff);
  set_page(w, c, 0x36, 0xffff);
  set_page(w, c, 0x3e, 0xffff);
  set_page(w, c, 0x46, 0xffff);
  set_page(w, c, 0x2c, 0);
  ran(k, TS_WATCHER_CLEARS);
  ran(k, TS_RTS);
  pull16(w, c);
  c->a = WATCHER_HANDLER;
  c->y = WATCHER_BANK;
  set_nz16(c, c->y);
  ran(k, TS_WATCHER_HANDLER);
  c->pc = WATCHER_HANDLER_CALL_PC;
}

// --- the decoy's start ---------------------------------------------------------

// `$81:F159`: its weight, and `JSR $F200` to its record.
void decoy_enter(Wram* w, const Rom* rom, PortCpu* c, ThreadStretchWork* k) {
  (void)rom;
  PORT_COVER(decoy_entered);
  load_up(w, c, DECOY_LOAD);
  push16(w, c, DECOY_MADE_RETURN);
  ran(k, TS_DECOY_ENTER);
  c->pc = DECOY_ALLOC_PC;
}

// `$81:F204`: one more out, its record where it was begun, drawn, its
// pictures from the first, a hundred turns, and its first pictures played.
void decoy_made(Wram* w, const Rom* rom, PortCpu* c, ThreadStretchWork* k) {
  (void)rom;
  const uint16_t record = c->a;
  set_page(w, c, DECOY_DP_RECORD, record);
  set_page(w, c, DECOY_DP_SPRITE, record);
  wram_w16(w, W_DECOYS_OUT, (uint16_t)(wram_r16(w, W_DECOYS_OUT) + 1));
  set_c(c, false);
  wram_w16(w, (uint16_t)(record + ACTOR_X), page(w, c, DECOY_DP_X));
  wram_w16(w, (uint16_t)(record + ACTOR_Z), 0);
  wram_w16(w, (uint16_t)(record + ACTOR_Y), page(w, c, DECOY_DP_Y));
  wram_w16(w, (uint16_t)(record + ACTOR_META), DECOY_START_PICTURE);
  wram_w16(w, (uint16_t)(record + ACTOR_META_BANK), DECOY_START_PICTURE_BANK);
  wram_w16(w, (uint16_t)(record + ACTOR_THREAD), wram_r16(w, W_SCHED_CUR_TASK));
  wram_w16(w, (uint16_t)(record + ACTOR_COLLIDE_ID), DECOY_COLLIDE_ID);
  wram_w16(w, (uint16_t)(record + ACTOR_FLAGS),
           (uint16_t)(wram_r16(w, (uint16_t)(record + ACTOR_FLAGS)) |
                      ACTOR_DRAW));
  set_page(w, c, DECOY_DP_PICTURE, 0);
  set_page(w, c, DECOY_DP_HIT, 0);
  set_page(w, c, DECOY_DP_40, 0x0040);
  set_page(w, c, DECOY_DP_TURNS_LEFT, DECOY_TURNS);
  lda(c, DECOY_START_PICTURES);
  ran(k, TS_DECOY_MADE);
  c->pc = DECOY_PICTURES_CALL_PC;
}

// `$81:F166`, back from its start: its first sleep.
void decoy_first(Wram* w, const Rom* rom, PortCpu* c, ThreadStretchWork* k) {
  (void)w;
  (void)rom;
  lda(c, DECOY_FIRST_TICKS);
  ran(k, TS_LDA_IMM);
  c->pc = DECOY_FIRST_SLEEP_PC;
}

// --- a tracker's start and end, and the seeker's puff ------------------------

// `$82:F03E`.
void tracker_enter(Wram* w, const Rom* rom, PortCpu* c,
                   ThreadStretchWork* k) {
  (void)rom;
  load_up(w, c, TRACKER_LOAD);
  ran(k, TS_TRACKER_ENTER);
  c->pc = TRACKER_BEGIN_CALL_PC;
}

// `$82:F04B`: the record `tracker_begin` made, and a tick's sleep.
void tracker_begun(Wram* w, const Rom* rom, PortCpu* c,
                   ThreadStretchWork* k) {
  (void)rom;
  set_page(w, c, TRACKER_DP_RECORD, c->a);
  lda(c, 0x0001);
  ran(k, TS_TRACKER_BEGUN);
  c->pc = TRACKER_SLEEP_PC;
}

// `$82:F05E`: its loop over, its last pictures.
void tracker_ended(Wram* w, const Rom* rom, PortCpu* c,
                   ThreadStretchWork* k) {
  (void)rom;
  ldy(c, page(w, c, TRACKER_DP_RECORD));
  wram_w16(w, (uint16_t)(c->y + ACTOR_META_BANK), TRACKER_END_PICTURE_BANK);
  lda(c, TRACKER_END_PICTURES);
  ran(k, TS_TRACKER_ENDED);
  c->pc = TRACKER_END_CALL_PC;
}

// `$82:F1E6`.
void puff_enter(Wram* w, const Rom* rom, PortCpu* c, ThreadStretchWork* k) {
  (void)rom;
  load_up(w, c, PUFF_LOAD);
  ran(k, TS_PUFF_ENTER);
  c->pc = PUFF_ALLOC_CALL_PC;
}

// `$82:F1F4`: the record in A, at the place it was asked for, drawn.
void puff_made(Wram* w, const Rom* rom, PortCpu* c, ThreadStretchWork* k) {
  (void)rom;
  const uint16_t record = c->a;
  set_page(w, c, PUFF_DP_RECORD, record);
  ldy(c, record);
  wram_w16(w, (uint16_t)(record + ACTOR_X), page(w, c, PUFF_DP_X));
  wram_w16(w, (uint16_t)(record + ACTOR_Z), 0);
  wram_w16(w, (uint16_t)(record + ACTOR_Y), page(w, c, PUFF_DP_Y));
  wram_w16(w, (uint16_t)(record + ACTOR_META), PUFF_PICTURE);
  wram_w16(w, (uint16_t)(record + ACTOR_META_BANK), PUFF_PICTURE_BANK);
  wram_w16(w, (uint16_t)(record + ACTOR_THREAD),
           wram_r16(w, W_SCHED_CUR_TASK));
  wram_w16(w, (uint16_t)(record + ACTOR_COLLIDE_ID), 0);
  lda(c, (uint16_t)(wram_r16(w, (uint16_t)(record + ACTOR_FLAGS)) | ACTOR_DRAW));
  wram_w16(w, (uint16_t)(record + ACTOR_FLAGS), c->a);
  lda(c, PUFF_PICTURES);
  ran(k, TS_PUFF_MADE);
  c->pc = PUFF_PICTURES_CALL_PC;
}

// --- the colour pulse's setup ------------------------------------------------

// `$80:A0EF`: the colour without its green is both where the pulse starts and
// where it turns up again.
void pulse_enter(Wram* w, const Rom* rom, PortCpu* c, ThreadStretchWork* k) {
  (void)rom;
  lda(c, (uint16_t)(wram_r16(w, W_PALCYCLE_PULSE_LEVEL) &
                    PALCYCLE_PULSE_NO_GREEN));
  set_page(w, c, PALCYCLE_PULSE_DP_FLOOR, c->a);
  set_page(w, c, PALCYCLE_PULSE_DP_NOW, c->a);
  lda(c, PALCYCLE_PULSE_STEP);
  set_page(w, c, PALCYCLE_PULSE_DP_STEP, c->a);
  lda(c, PALCYCLE_PULSE_TICKS);
  ran(k, TS_PULSE_ENTER);
  c->pc = PALCYCLE_PULSE_YIELD_PC;
}

// --- the big figure's start, and the saucer's --------------------------------

// `JSL` to a routine that is done here, and its `RTL`: the return is left on
// the stack below as the ROM leaves it.
static void jsl_and_back(Wram* w, PortCpu* c, uint16_t jsl_at) {
  push8(w, c, (uint8_t)(c->pc >> 16));
  push16(w, c, (uint16_t)(jsl_at + 3));
  pull16(w, c);
  pull8(w, c);
}

// --- the monster potion -----------------------------------------------------

// `$80:D9B3`: the change's pictures, by the player.
void monster_pictured(Wram* w, const Rom* rom, PortCpu* c,
                      ThreadStretchWork* k) {
  ldx(c, page(w, c, POSE_DP_PICTURES_ROW));
  lda(c, rom_word(rom, MONSTER_PICTURES + c->x));
  ran(k, TS_MONSTER_PICTURES);
  c->pc = MONSTER_PICTURES_CALL_PC;
}

// `$80:D9BC`, after them: the monster's words, and the swap.
void monster_begun(Wram* w, const Rom* rom, PortCpu* c, ThreadStretchWork* k) {
  (void)rom;
  PORT_COVER(monster_begun);
  set_page(w, c, MONSTER_DP_FRAMES_LEFT, MONSTER_FRAMES);
  set_page(w, c, POSE_DP_PICTURES_SET, 0x0001);
  set_page(w, c, PSN_DP_FIRE_A, 0);
  set_page(w, c, PSN_DP_FIRE_B, 0);
  set_page(w, c, PSN_DP_T3_TAIL, 0);
  set_page(w, c, PSN_DP_DIR_HELD, MONSTER_FACING);
  set_page(w, c, PSN_DP_DIR, 0);
  set_page(w, c, PLAYER_DP_STATE, PLAYER_STATE_MONSTER);
  set_page(w, c, FLASHING_DP_ENDS_BY, MONSTER_ENDS_BY);
  c->a = MONSTER_WEAPON;
  ldy(c, MONSTER_ITEM);
  ran(k, TS_MONSTER_BEGUN);
  c->pc = MONSTER_SWAP_CALL_PC;
}

// --- a player out by the exit -----------------------------------------------

// `$80:FB00`: the player's weight off the level's load, to the `JSR` that
// leaves them untouchable. The `BMI` after is not taken: below nought it
// spins, and `TS_NEEDS_PLAYER_LOAD` turns that down.
void player_left(Wram* w, const Rom* rom, PortCpu* c, ThreadStretchWork* k) {
  (void)rom;
  set_c(c, true);
  lda(c, sbc16(c, wram_r16(w, W_SPAWN_LOAD), PLAYER_LOAD));
  wram_w16(w, W_SPAWN_LOAD, c->a);
  ran(k, TS_LEFT_LOAD);
  ran(k, TS_BRANCH);
  c->pc = PLAYER_LEFT_CALL_PC;
}

// `$80:FAF6`: the state ended, by the routine at `$72`, if it has one.
void player_leaves(Wram* w, const Rom* rom, PortCpu* c, ThreadStretchWork* k) {
  lda(c, page(w, c, FLASHING_DP_ENDS_BY));
  ran(k, TS_LEAVE_ENDS_ASK);
  if (c->a == 0) {
    PORT_COVER(player_leaves_as_is);
    ran(k, TS_TAKEN);
    player_left(w, rom, c, k);
    return;
  }
  // `PEA $FAFF : DEC : PHA : RTS`.
  PORT_COVER(player_leaves_ended);
  push16(w, c, PLAYER_LEAVES_BACK);
  lda(c, (uint16_t)(c->a - 1));
  push16(w, c, c->a);
  ran(k, TS_LEAVE_ENDS);
  c->pc = PLAYER_LEAVES_RTS_PC;
}

// `$80:FB18`, after the rest: their side of the panel.
void player_left_panel(Wram* w, const Rom* rom, PortCpu* c,
                       ThreadStretchWork* k) {
  (void)rom;
  lda(c, page(w, c, PLAYER_DP_INDEX));
  ran(k, TS_LEFT_PANEL);
  c->pc = PLAYER_LEFT_PANEL_CALL_PC;
}

// `$80:FB1E`: a picture of nothing, and the player out.
void player_left_shown(Wram* w, const Rom* rom, PortCpu* c,
                       ThreadStretchWork* k) {
  (void)rom;
  ldy(c, page(w, c, POSE_DP_RECORD));
  lda(c, PLAYER_LEFT_PICTURE);
  wram_w16(w, (uint16_t)(c->y + ACTOR_META), c->a);
  ldx(c, page(w, c, PLAYER_DP_INDEX));
  lda(c, 0x0001);
  wram_w16(w, (uint16_t)(W_PLAYERS_OUT + c->x), c->a);
  lda(c, PLAYER_LEFT_TICKS);
  ran(k, TS_LEFT_SHOWN);
  c->pc = PLAYER_LEFT_SLEEP_PC;
}

// `$80:FB35`: with a record for each player the two words differ from the
// first alone, and the player's is let go. Then the long sleep.
void player_gone(Wram* w, const Rom* rom, PortCpu* c, ThreadStretchWork* k) {
  (void)rom;
  const uint16_t first = wram_r16(w, W_PLAYER_RECORDS);
  set_c(c, false);
  lda(c, adc16(c, first, wram_r16(w, W_PLAYER_RECORDS + 2)));
  cmp16(c, c->a, first);
  ran(k, TS_GONE_RECORDS);
  if (c->a == first) {
    PORT_COVER(player_gone_alone);
    ran(k, TS_TAKEN);
  } else {
    PORT_COVER(player_gone_let_go);
    ldx(c, page(w, c, PLAYER_DP_INDEX));
    lda(c, 0x0000);
    wram_w16(w, (uint16_t)(W_PLAYER_RECORDS + c->x), c->a);
    ran(k, TS_GONE_CLEARED);
  }
  lda(c, PLAYER_GONE_TICKS);
  ran(k, TS_LDA_IMM);
  c->pc = PLAYER_GONE_SLEEP_PC;
}

// `$80:DA54`: the monster ended. With its routine still at `$72` it is the
// exit asking, and there is nothing to show: its words cleared, and to the
// `JSR` that gives the hands back.
void monster_ends(Wram* w, const Rom* rom, PortCpu* c, ThreadStretchWork* k) {
  (void)rom;
  lda(c, page(w, c, FLASHING_DP_ENDS_BY));
  ran(k, TS_MONSTER_ENDS_ASK);
  if (c->a == 0) {
    c->pc = MONSTER_ENDS_SHOWN_PC;
    return;
  }
  PORT_COVER(monster_ends_unshown);
  ran(k, TS_TAKEN);
  set_page(w, c, MONSTER_DP_PUNCH, 0);
  set_page(w, c, POSE_DP_PICTURES_SET, 0);
  set_page(w, c, PLAYER_DP_STATE, 0);
  set_page(w, c, FLASHING_DP_ENDS_BY, 0);
  ran(k, TS_MONSTER_ENDED);
  c->pc = MONSTER_ENDS_CALL_PC;
}

// --- the monster's punch, standing ------------------------------------------

// `JSR $F051` at `jsr_at`: the fist's box. With the cycle at nought it only
// returns; otherwise its four sides and who is punching go to page zero, and
// it stops at the `JSL` that tells those in it.
static void punch_told(Wram* w, const Rom* rom, PortCpu* c,
                       ThreadStretchWork* k, uint16_t jsr_at) {
  push16(w, c, (uint16_t)(jsr_at + 2));
  ran(k, TS_JSR);
  lda(c, page(w, c, POSE_DP_CYCLE));
  ran(k, TS_PUNCH_TOLD_HEAD);
  if (c->a == 0) {
    PORT_COVER(punch_told_nobody);
    ran(k, TS_TAKEN);
    pull16(w, c);
    ran(k, TS_RTS);
    c->pc = (c->pc & 0xff0000u) | (uint16_t)(jsr_at + 3);
    return;
  }
  PORT_COVER(punch_told);
  c->a = page(w, c, PSN_DP_DIR_HELD);
  c->a = asl16(c, asl16(c, c->a));
  ldx(c, c->a);
  const uint32_t boxes = (c->pc & 0xff0000u) | POSE_PUNCH_BOXES;
  static const uint16_t SIDES[] = {W_BOX_LEFT, W_BOX_RIGHT, W_BOX_TOP,
                                   W_BOX_BOTTOM};
  for (int side = 0; side < 4; side++) {
    // `LDA $30 : PHA`, the left, `PLA`, the right; then the same for `$32`.
    if (side % 2 == 0) {
      lda(c, page(w, c, side == 0 ? STEP_DP_X : STEP_DP_Y));
      push16(w, c, c->a);
    } else {
      lda(c, pull16(w, c));
    }
    set_c(c, false);
    lda(c, adc16(c, c->a,
                 rom_word(rom, boxes + (uint16_t)(c->x + 2 * side))));
    wram_w16(w, SIDES[side], c->a);
  }
  ldx(c, page(w, c, POSE_DP_PICTURES_ROW));
  lda(c, rom_word(rom, ((c->pc & 0xff0000u) | POSE_PUNCH_IDS) + c->x));
  wram_w16(w, W_BOX_ID, c->a);
  ran(k, TS_PUNCH_BOX);
  c->pc = PUNCH_TELL_PC;
}

// `$80:EF82`, back from the sound: the first picture's box.
void punch_first(Wram* w, const Rom* rom, PortCpu* c, ThreadStretchWork* k) {
  punch_told(w, rom, c, k, PUNCH_FIRST_JSR);
}

// `$80:EF8F`, four ticks on: the second picture.
void punch_again(Wram* w, const Rom* rom, PortCpu* c, ThreadStretchWork* k) {
  (void)rom;
  lda(c, page(w, c, POSE_DP_PUNCH_PICTURE));
  set_c(c, false);
  lda(c, adc16(c, c->a, PUNCH_NEXT_PICTURE));
  ran(k, TS_PUNCH_AGAIN);
  c->pc = PUNCH_SHOW_CALL_PC;
}

// `$80:EF98`, its picture shown: the cycle at two, and its box.
void punch_second(Wram* w, const Rom* rom, PortCpu* c, ThreadStretchWork* k) {
  lda(c, PUNCH_SECOND_CYCLE);
  set_page(w, c, POSE_DP_CYCLE, c->a);
  ran(k, TS_PUNCH_SECOND);
  punch_told(w, rom, c, k, PUNCH_SECOND_JSR);
}

// `$80:EFAA`, four ticks on: `INC $18`, and the box again.
void punch_third(Wram* w, const Rom* rom, PortCpu* c, ThreadStretchWork* k) {
  const uint16_t cycle = (uint16_t)(page(w, c, POSE_DP_CYCLE) + 1);
  set_page(w, c, POSE_DP_CYCLE, cycle);
  set_nz16(c, cycle);
  ran(k, TS_PUNCH_THIRD);
  punch_told(w, rom, c, k, PUNCH_THIRD_JSR);
}

// `$80:EFB9`, four ticks on: the cycle back to nought, and the pose's `RTS`.
void punch_over(Wram* w, const Rom* rom, PortCpu* c, ThreadStretchWork* k) {
  (void)rom;
  set_page(w, c, POSE_DP_CYCLE, 0);
  ran(k, TS_PUNCH_OVER);
  c->pc = PUNCH_RTS_PC;
}

// --- the fourth's tile, broken open -----------------------------------------

// `$80:EA02`, back from the block, and `$82:DC57`: what the tile leaves, by
// a byte at random.
void fourth_prize(Wram* w, const Rom* rom, PortCpu* c, ThreadStretchWork* k) {
  set_c(c, false);
  ldx(c, adc16(c, page(w, c, WALK_DP_X), FOURTH_PRIZE_ACROSS));
  set_c(c, true);
  ldy(c, sbc16(c, page(w, c, WALK_DP_Y), FOURTH_PRIZE_UP));
  lda(c, page(w, c, WALK_DP_FACING));
  push8(w, c, (uint8_t)(c->pc >> 16));
  push16(w, c, FOURTH_PRIZE_RETURN);
  ran(k, TS_FOURTH_BACK);
  push16(w, c, c->x);
  wram_w16(w, W_FOURTH_FACING, c->a);
  c->pc = FOURTH_PRIZE_DRAW_PC;
  jsl_and_back(w, c, (uint16_t)FOURTH_PRIZE_DRAW_PC);
  ran(k, TS_FOURTH_DRAW);
  draw(w, c, k);
  ldx(c, c->a);
  lda(c, rom_word(rom, FOURTH_PRIZES + c->x));
  ldx(c, pull16(w, c));
  lda(c, c->a & 0x00ffu);
  cmp16(c, c->a, FOURTH_PRIZE_NONE);
  ran(k, TS_FOURTH_PRIZE);
  if (c->a == FOURTH_PRIZE_NONE) {
    PORT_COVER(fourth_prize_none);
    ran(k, TS_TAKEN);
    c->pc = FOURTH_PRIZE_RTL_PC;
    return;
  }
  cmp16(c, c->a, FOURTH_PRIZE_BESIDE);
  ran(k, TS_FOURTH_NOT_NONE);
  if (c->a != FOURTH_PRIZE_BESIDE) {
    PORT_COVER(fourth_prize_item);
    c->pc = FOURTH_PRIZE_ITEM_PC;
    return;
  }
  ran(k, TS_TAKEN);
  // `CMP #6 : BEQ : BCC` and `CMP #$0E : BCC`: not between the two.
  const uint16_t facing = wram_r16(w, W_FOURTH_FACING);
  lda(c, facing);
  cmp16(c, facing, FOURTH_FACING_LOW);
  ran(k, TS_FOURTH_FACING);
  const bool low = facing <= FOURTH_FACING_LOW;
  if (facing != FOURTH_FACING_LOW) ran(k, TS_BRANCH);
  if (low) ran(k, TS_TAKEN);
  if (!low) {
    cmp16(c, facing, FOURTH_FACING_HIGH);
    ran(k, TS_FOURTH_FACING_B);
    if (facing < FOURTH_FACING_HIGH) {
      PORT_COVER(fourth_prize_away);
      ran(k, TS_TAKEN);
      c->pc = FOURTH_PRIZE_RTL_PC;
      return;
    }
  }
  PORT_COVER(fourth_prize_beside);
  set_c(c, true);
  lda(c, sbc16(c, c->x, FOURTH_BESIDE_LEFT));
  set_page(w, c, 0x00, c->a);
  set_c(c, true);
  lda(c, sbc16(c, c->y, FOURTH_BESIDE_UP));
  set_page(w, c, 0x02, c->a);
  c->a = FOURTH_BESIDE_THREAD;
  ldy(c, FOURTH_BESIDE_THREAD_BANK);
  ran(k, TS_FOURTH_BESIDE);
  c->pc = FOURTH_PRIZE_THREAD_PC;
}

// The four words of an upload for `boss_bg_queue_one`, in the order both
// starts store them.
static void upload_words(Wram* w, PortCpu* c, uint16_t src, uint16_t bank) {
  lda(c, src);
  wram_w16(w, W_BOSS_BG_ONE_SRC, c->a);
  lda(c, bank);
  wram_w16(w, W_BOSS_BG_ONE_BANK, c->a);
  lda(c, FIGURE_TILES_BYTES);
  wram_w16(w, W_BOSS_BG_ONE_BYTES, c->a);
  lda(c, FIGURE_TILES_VRAM);
  wram_w16(w, W_BOSS_BG_ONE_VRAM, c->a);
}

// `$82:9569`.
void figure_enter(Wram* w, const Rom* rom, PortCpu* c, ThreadStretchWork* k) {
  (void)rom;
  load_up(w, c, FIGURE_LOAD);
  push16(w, c, FIGURE_START_RETURN);
  ran(k, TS_FIGURE_ENTER);
  c->a = FIGURE_COLOURS;
  ldy(c, FIGURE_COLOURS_BANK);
  ran(k, TS_FIGURE_COLOURS);
  c->pc = FIGURE_COLOURS_CALL_PC;
}

// `$82:94EE`.
void figure_coloured(Wram* w, const Rom* rom, PortCpu* c,
                     ThreadStretchWork* k) {
  (void)rom;
  upload_words(w, c, FIGURE_TILES, FIGURE_TILES_BANK);
  ran(k, TS_FIGURE_COLOURED);
  c->pc = FIGURE_QUEUE_CALL_PC;
}

// `$82:9511`: `JSL $82:80E0`, which is `LDA $1D54 : RTL`, and while that is
// not zero its tiles have not all gone up.
void figure_waited(Wram* w, const Rom* rom, PortCpu* c, ThreadStretchWork* k) {
  (void)rom;
  jsl_and_back(w, c, (uint16_t)c->pc);
  lda(c, wram_r16(w, W_BG_DMA_CURSOR));
  ran(k, TS_FIGURE_WAIT_ASK);
  if (c->a != 0) {
    PORT_COVER(figure_waits);
    ran(k, TS_TAKEN);
    lda(c, 0x0001);
    ran(k, TS_LDA_IMM);
    c->pc = FIGURE_YIELD_PC;
    return;
  }
  PORT_COVER(figure_placed);
  lda(c, FIGURE_PLANE_AT_X);
  wram_w16(w, W_BOSS_PLANE_AT_X, c->a);
  lda(c, FIGURE_PLANE_AT_Y);
  wram_w16(w, W_BOSS_PLANE_AT_Y, c->a);
  lda(c, page(w, c, FIGURE_DP_PLACE_X));
  wram_w16(w, W_BOSS_X, c->a);
  lda(c, page(w, c, FIGURE_DP_PLACE_Y));
  set_page(w, c, BOSS_DP_HOME_Y, c->a);
  wram_w16(w, W_BOSS_Y, c->a);
  set_page(w, c, BOSS_DP_TIMER, 0);
  push16(w, c, FIGURE_PART_RETURNS[0]);
  ran(k, TS_FIGURE_PLACED);
  c->pc = FIGURE_PART_CALL_PC;
}

// `$82:94B8`, with the part's record in A.
void figure_part(Wram* w, const Rom* rom, PortCpu* c, ThreadStretchWork* k) {
  (void)rom;
  const uint16_t record = c->a;
  c->x = record;
  wram_w16(w, (uint16_t)(record + ACTOR_X), 0);
  wram_w16(w, (uint16_t)(record + ACTOR_Z), 0);
  wram_w16(w, (uint16_t)(record + ACTOR_Y), 0);
  wram_w16(w, (uint16_t)(record + ACTOR_META), 0);
  wram_w16(w, (uint16_t)(record + ACTOR_META_BANK), 0);
  wram_w16(w, (uint16_t)(record + ACTOR_COLLIDE_ID), FIGURE_PART_COLLIDE_ID);
  wram_w16(w, (uint16_t)(record + ACTOR_THREAD),
           wram_r16(w, W_SCHED_CUR_TASK));
  lda(c, (uint16_t)(wram_r16(w, (uint16_t)(record + ACTOR_FLAGS)) |
                    FIGURE_PART_FLAGS));
  wram_w16(w, (uint16_t)(record + ACTOR_FLAGS), c->a);
  ran(k, TS_FIGURE_PART);

  const uint16_t back = pull16(w, c);
  int part = 0;
  while (FIGURE_PART_RETURNS[part] != back) part++;
  set_page(w, c, (uint8_t)(BOSS_PARTS_DP_FIRST + part * BOSS_PARTS_DP_STRIDE),
           c->x);
  if (part < FIGURE_PARTS - 1) {
    PORT_COVER(figure_part_made);
    push16(w, c, FIGURE_PART_RETURNS[part + 1]);
    ran(k, TS_FIGURE_PART_KEPT);
    c->pc = FIGURE_PART_CALL_PC;
    return;
  }

  // `$82:9545`, after the fourth.
  PORT_COVER(figure_parts_made);
  set_page(w, c, BOSS_DP_FACING, 0);
  set_page(w, c, BOSS_DP_DEAD, 0);
  set_page(w, c, BOSS_DP_HIT, 0);
  set_page(w, c, BOSS_DP_FLASH_LEFT, 0);
  set_page(w, c, BOSS_DP_STRIDE_LEFT, 0);
  set_page(w, c, BOSS_DP_STRIDE, 0);
  ran(k, TS_FIGURE_PARTS_KEPT);
  // `JSR $94A4`: none of the four spits out.
  push16(w, c, 0x9553);
  lda(c, BOSS_SPIT_NONE);
  for (uint8_t at = 0; at < 4 * 8; at += 8)
    set_page(w, c, (uint8_t)(BOSS_DP_SPIT_RECORD + at), c->a);
  set_page(w, c, BOSS_DP_SPIT, 0);
  set_page(w, c, BOSS_DP_SPITS, 0);
  pull16(w, c);
  ran(k, TS_FIGURE_NO_SPITS);

  c->a = FIGURE_HANDLER;
  ldy(c, FIGURE_HANDLER_BANK);
  ran(k, TS_FIGURE_HANDLER);
  thread_set_handler(w, c);
  ran(k, TS_SET_HANDLER);
  lda(c, FIGURE_3C);
  set_page(w, c, FIGURE_DP_3C, c->a);
  lda(c, BOSS_TEMPER_CALM);
  set_page(w, c, BOSS_DP_TEMPER, c->a);
  pull16(w, c);
  ran(k, TS_FIGURE_CALM);

  push16(w, c, FIGURE_CHOOSE_RETURN);
  ran(k, TS_JSR);
  c->pc = BOSS_CHOOSE_PC;
}

// `$82:873C`.
void saucer_enter(Wram* w, const Rom* rom, PortCpu* c, ThreadStretchWork* k) {
  (void)rom;
  load_up(w, c, SAUCER_LOAD);
  c->a = SAUCER_COLOURS;
  ldy(c, SAUCER_COLOURS_BANK);
  ran(k, TS_SAUCER_ENTER);
  c->pc = SAUCER_COLOURS_CALL_PC;
}

// `$82:8750`: where it is, and the figure `boss_bg_queue` is to draw.
void saucer_coloured(Wram* w, const Rom* rom, PortCpu* c,
                     ThreadStretchWork* k) {
  (void)rom;
  lda(c, page(w, c, SAUCER_DP_PLACE_X));
  wram_w16(w, W_BOSS_X, c->a);
  lda(c, page(w, c, SAUCER_DP_PLACE_Y));
  wram_w16(w, W_BOSS_Y, c->a);
  lda(c, SAUCER_FIGURE);
  wram_w16(w, BOSS_BG_DP_SRC, c->a);
  lda(c, SAUCER_FIGURE_BANK);
  wram_w16(w, BOSS_BG_DP_SRC_BANK, c->a);
  ran(k, TS_SAUCER_COLOURED);
  c->pc = SAUCER_DRAW_CALL_PC;
}

// `$82:876A`.
void saucer_drawn(Wram* w, const Rom* rom, PortCpu* c, ThreadStretchWork* k) {
  (void)rom;
  upload_words(w, c, SAUCER_TILES, FIGURE_TILES_BANK);
  ran(k, TS_SAUCER_DRAWN);
  c->pc = SAUCER_QUEUE_CALL_PC;
}

// `$82:8786`, and `JSR $84CE` as far as its call.
void saucer_queued(Wram* w, const Rom* rom, PortCpu* c, ThreadStretchWork* k) {
  (void)rom;
  lda(c, 0x0001);
  set_page(w, c, SAUCER_DP_LIGHTS_WAIT, c->a);
  set_page(w, c, SAUCER_DP_LIGHTS_PHASE, 0);
  lda(c, 0);
  set_page(w, c, SAUCER_DP_FACING, c->a);
  set_page(w, c, SAUCER_DP_12, c->a);
  set_page(w, c, SAUCER_DP_SHOT_WAIT, c->a);
  set_page(w, c, SAUCER_DP_REACH, 0);
  lda(c, 0xffff);
  set_page(w, c, SAUCER_DP_HATCH, c->a);
  lda(c, SAUCER_FULL_HEALTH);
  set_page(w, c, SAUCER_DP_HEALTH, c->a);
  lda(c, SAUCER_SHOTS);
  set_page(w, c, SAUCER_DP_SHOTS_LEFT, c->a);
  push16(w, c, SAUCER_START_RETURN);
  ran(k, TS_SAUCER_QUEUED);
  lda(c, SAUCER_STATE_HUNT);
  set_page(w, c, SAUCER_DP_STATE, c->a);
  ldx(c, wram_r16(w, W_BOSS_X));
  ldy(c, wram_r16(w, W_BOSS_Y));
  ran(k, TS_SAUCER_NEAREST_ASK);
  c->pc = SAUCER_NEAREST_CALL_PC;
}

// `$82:84DD`, with the nearest record in X: the spot on the side it is on,
// as `port/saucer.c`'s `take_aim` sets it, and back for the first sleep.
void saucer_aimed(Wram* w, const Rom* rom, PortCpu* c, ThreadStretchWork* k) {
  c->y = c->x;
  const uint16_t me = wram_r16(w, W_BOSS_X);
  cmp16(c, me, wram_r16(w, (uint16_t)(c->y + ACTOR_X)));
  const uint16_t side = flag(c, PORT_P_C) ? 2 : 0;
  ldx(c, side);
  set_c(c, false);
  set_page(w, c, SAUCER_DP_AIM_X,
           adc16(c, rom_word(rom, (uint32_t)SAUCER_BANK << 16 |
                                      (uint16_t)(SAUCER_SPOT_ASIDE + side)),
                 me));
  set_c(c, false);
  set_page(w, c, SAUCER_DP_LINE_X,
           adc16(c, rom_word(rom, (uint32_t)SAUCER_BANK << 16 |
                                      (uint16_t)(SAUCER_LINE_ASIDE + side)),
                 me));
  lda(c, 0x0001);
  set_page(w, c, SAUCER_DP_REAIM_WAIT, c->a);
  pull16(w, c);
  ran(k, TS_SAUCER_AIMED);
  lda(c, SAUCER_YIELD_TICKS);
  ran(k, TS_LDA_IMM);
  c->pc = SAUCER_YIELD_PC;
}
