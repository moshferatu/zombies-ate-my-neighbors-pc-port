// A level's scene begun -- see port/level_scene.h.

#include "port/level_scene.h"

#include "port/collide.h"
#include "port/coverage.h"
#include "port/thread.h"
#include "port/walk.h"

void scene_cache_ask(PortCpu* c) {
  PORT_COVER(scene_cache_ask);
  c->a = SPRITE_FRAMES_AT;
  c->y = SPRITE_FRAMES_BANK;
  set_nz16(c, c->y);
  c->pc = SCENE_CACHE_INIT_PC;
}

void scene_job_ask(PortCpu* c) {
  PORT_COVER(scene_job_ask);
  c->a = SECOND_LAYER_JOB;
  c->y = CAMERA_JOBS_BANK;
  set_nz16(c, c->y);
  c->pc = SCENE_JOB_QUEUE_PC;
}

void camera_reset(Wram* w) {
  PORT_COVER(camera_reset);
  wram_w16(w, W_TILEMAP_ARENA_LEFT, TILEMAP_ARENA_BYTES);
  wram_w16(w, W_TILEMAP_ARENA_NEXT, TILEMAP_ARENA_AT);
  wram_w16(w, W_CAMERA_X, 0);
  wram_w16(w, W_CAMERA_Y, 0);
  wram_w16(w, W_CAMERA_SUB_X, 0);
  wram_w16(w, W_CAMERA_SUB_Y, 0);
  wram_w16(w, W_TILEMAP_CURSOR_X, 0);
  wram_w16(w, W_TILEMAP_CURSOR_Y, 0);
  wram_w16(w, W_VRAM_QUEUE_COUNT, 0);
  wram_w16(w, W_RENDER_FLAGS, 0);
}

void camera_jobs_queue(Wram* w, CameraJobs* did) {
  PORT_COVER(camera_jobs_queue);
  did->flush_slot = vbl_queue_a_add(w, VRAM_QUEUE_FLUSH_JOB, CAMERA_JOBS_BANK);
  did->second_slot = vbl_queue_b_add(w, CAMERA_SECOND_JOB, CAMERA_JOBS_BANK);
}

void walk_squares_clear(Wram* w) {
  PORT_COVER(walk_squares_clear);
  for (int at = WALK_SQUARES_LAST; at >= 0; at -= 2)
    wram_w16(w, (uint32_t)(W_WALK_SQUARES + at), 0xffffu);
}

void players_seed(Wram* w, bool seeded[2]) {
  for (int player = 0; player < 2; player++) {
    const uint16_t x = (uint16_t)(player * 2);
    seeded[player] = wram_r16(w, (uint16_t)(W_HUD_PANEL_ON + x)) != 0;
    if (!seeded[player]) {
      PORT_COVER(players_seed_absent);
      continue;
    }
    PORT_COVER(players_seed_playing);
    wram_w16(w, (uint16_t)(W_PLAYER_HEALTH + x), PLAYER_START_HEALTH);
    wram_w16(w, (uint16_t)(W_PLAYER_CAPPED_COUNT + x), PLAYER_START_LIVES);
    wram_w16(w, (uint16_t)(W_PLAYER_WEAPON + x), 0);
    wram_w16(w, (uint16_t)(W_PLAYER_ITEM + x), PLAYER_START_ITEM);
    // The first player's turn falls through into the second's stores.
    for (int whose = player; whose < 2; whose++) {
      const uint16_t at = (uint16_t)(whose * W_PLAYER_INVENTORY_STRIDE);
      wram_w16(w, (uint16_t)(W_PLAYER_INVENTORY + at), PLAYER_START_SHOTS);
      wram_w16(w, (uint16_t)(W_PLAYER_KITS + at), 1);
    }
  }
  wram_w16(w, W_SEEDED_FOURS, 4);
  wram_w16(w, W_SEEDED_FOURS + 2, 4);
}

static uint16_t level_asked_for(const Wram* w) {
  const uint16_t again = wram_r16(w, W_LEVEL_AGAIN);
  return again ? again : wram_r16(w, W_LEVEL_NUMBER);
}

bool level_record_find_supported(const Wram* w) {
  return level_asked_for(w) < 0x3fffu;
}

uint16_t level_record_find(Wram* w, const Rom* rom, uint16_t page,
                           uint16_t* index, bool* again) {
  *index = (uint16_t)(level_asked_for(w) << 1);
  *again = wram_r16(w, W_LEVEL_AGAIN) != 0;
  if (*again) {
    PORT_COVER(level_record_again);
    wram_w16(w, W_LEVEL_AGAIN, 0);
    wram_w16(w, W_LEVEL_NUMBER, (uint16_t)(wram_r16(w, W_LEVEL_NUMBER) - 1));
  } else {
    PORT_COVER(level_record_next);
  }
  const uint16_t record = rom_word(rom, LEVEL_RECORD_TABLE + *index);
  wram_w16(w, (uint16_t)(page + PLAYERS_DP_LEVEL), record);
  return record;
}

bool players_begin_supported(const Wram* w, uint16_t page) {
  const uint16_t level = wram_r16(w, (uint16_t)(page + PLAYERS_DP_LEVEL));
  return level >= 0x8000u && level <= 0xffffu - LEVEL_PLAYER_START_Y - 5;
}

void players_begin(Wram* w, const Rom* rom, uint16_t page, PlayersBegun* did) {
  for (int player = 0; player < 2; player++) {
    did->slot[player] = -1;
    did->playing[player] =
        wram_r16(w, (uint16_t)(W_HUD_PANEL_ON + player * 2)) != 0;
    if (!did->playing[player]) {
      PORT_COVER(players_begin_absent);
      continue;
    }
    PORT_COVER(players_begin_playing);
    const uint32_t start = LEVEL_RECORDS_BANK + player * 4u +
                           wram_r16(w, (uint16_t)(page + PLAYERS_DP_LEVEL));
    wram_w16(w, (uint16_t)(page + 0x00),
             rom_word(rom, start + LEVEL_PLAYER_START_X));
    wram_w16(w, (uint16_t)(page + 0x02),
             rom_word(rom, start + LEVEL_PLAYER_START_Y));
    wram_w16(w, (uint16_t)(page + 0x04), (uint16_t)player);
    wram_w16(w, (uint16_t)(page + 0x06), (uint16_t)player);
    did->slot[player] =
        thread_spawn_low(w, rom, PLAYER_THREAD, PLAYER_THREAD_BANK, page);
  }
}
