// $80:F366-$80:F451  small things a player's changes of state are made of
// -- see port/player_small.h.

#include "port/player_small.h"

#include "port/coverage.h"
#include "port/oam.h"
#include "port/terrain.h"
#include "port/thread.h"

static uint16_t field(const Wram* w, const PortCpu* c, uint16_t at) {
  return wram_r16(w, (uint16_t)(c->d + at));
}

static void set_field(Wram* w, const PortCpu* c, uint16_t at, uint16_t v) {
  wram_w16(w, (uint16_t)(c->d + at), v);
}

void player_untouchable(Wram* w, PortCpu* c) {
  PORT_COVER(player_untouchable);
  c->x = field(w, c, PLAYER_SMALL_DP_RECORD);
  set_nz16(c, c->x);
  wram_w16(w, (uint16_t)(c->x + ACTOR_COLLIDE_ID), 0);
  c->pc = PLAYER_UNTOUCHABLE_RTS_PC;
}

void player_touchable(Wram* w, const Rom* rom, PortCpu* c) {
  PORT_COVER(player_touchable);
  c->x = field(w, c, PLAYER_SMALL_DP_NUMBER);
  c->y = field(w, c, PLAYER_SMALL_DP_RECORD);
  c->a = rom_word(rom, PLAYER_SMALL_IDS + c->x);
  set_nz16(c, c->a);
  wram_w16(w, (uint16_t)(c->y + ACTOR_COLLIDE_ID), c->a);
  c->pc = PLAYER_TOUCHABLE_RTS_PC;
}

void player_unhandled(Wram* w, PortCpu* c) {
  PORT_COVER(player_unhandled);
  set_field(w, c, PLAYER_SMALL_DP_UNTOLD, 0);
  c->a = 0;
  c->y = 0;
  thread_set_handler(w, c);
  c->pc = PLAYER_UNHANDLED_RTS_PC;
}

void player_handled(Wram* w, PortCpu* c) {
  PORT_COVER(player_handled);
  c->a = PLAYER_SMALL_HANDLER;
  c->y = PLAYER_SMALL_BANK;
  thread_set_handler(w, c);
  c->pc = PLAYER_HANDLED_RTS_PC;
}

void player_hands_swap(Wram* w, PortCpu* c) {
  PORT_COVER(player_hands_swapped);
  push16(w, c, c->a);
  c->x = field(w, c, PLAYER_SMALL_DP_PLAYER);
  wram_w16(w, (uint16_t)(W_PLAYER_WEAPON_KEPT + c->x),
           wram_r16(w, (uint16_t)(W_PLAYER_WEAPON + c->x)));
  wram_w16(w, (uint16_t)(W_PLAYER_ITEM_KEPT + c->x),
           wram_r16(w, (uint16_t)(W_PLAYER_ITEM + c->x)));
  wram_w16(w, (uint16_t)(W_PLAYER_WEAPON + c->x), pull16(w, c));
  c->a = c->y;
  set_nz16(c, c->a);
  wram_w16(w, (uint16_t)(W_PLAYER_ITEM + c->x), c->a);
  c->pc = PLAYER_HANDS_SWAP_RTS_PC;
}

void player_hands_back(Wram* w, PortCpu* c) {
  PORT_COVER(player_hands_back);
  c->x = field(w, c, PLAYER_SMALL_DP_PLAYER);
  wram_w16(w, (uint16_t)(W_PLAYER_WEAPON + c->x),
           wram_r16(w, (uint16_t)(W_PLAYER_WEAPON_KEPT + c->x)));
  c->a = wram_r16(w, (uint16_t)(W_PLAYER_ITEM_KEPT + c->x));
  set_nz16(c, c->a);
  wram_w16(w, (uint16_t)(W_PLAYER_ITEM + c->x), c->a);
  c->pc = PLAYER_HANDS_BACK_RTS_PC;
}

// One axis: how far, in A, and which way, in X.
static bool span_axis(Wram* w, PortCpu* c, uint16_t point, uint16_t place) {
  c->x = 1;
  set_c(c, true);
  c->a = sbc16(c, field(w, c, point), field(w, c, place));
  if ((c->a & 0x8000u) == 0) return false;
  c->a = (uint16_t)(0 - c->a);
  c->x = 0xffff;
  set_nz16(c, c->x);
  return true;
}

void player_span(Wram* w, PortCpu* c, PlayerSpan* did) {
  PORT_COVER(player_spanned);
  did->back_x = span_axis(w, c, PLAYER_SPAN_DP_POINT_X, PLAYER_SPAN_DP_X);
  set_field(w, c, PLAYER_SPAN_DP_FAR_X, c->a);
  set_field(w, c, PLAYER_SPAN_DP_WAY_X, c->x);
  did->back_y = span_axis(w, c, PLAYER_SPAN_DP_POINT_Y, PLAYER_SPAN_DP_Y);
  set_field(w, c, PLAYER_SPAN_DP_FAR_Y, c->a);
  set_field(w, c, PLAYER_SPAN_DP_WAY_Y, c->x);
  set_field(w, c, PLAYER_SPAN_DP_CLEAR_A, 0);
  set_field(w, c, PLAYER_SPAN_DP_CLEAR_B, 0);
  c->pc = PLAYER_SPAN_RTS_PC;
}

bool weapon_away(Wram* w, PortCpu* c) {
  c->pc = WEAPON_AWAY_RTS_PC;
  c->a = field(w, c, TILE_SEARCH_DP_STATE);
  cmp16(c, c->a, WEAPON_AWAY_KEEPS);
  if (c->a == WEAPON_AWAY_KEEPS) {
    PORT_COVER(weapon_away_kept);
    return false;
  }
  PORT_COVER(weapon_away_hidden);
  c->y = field(w, c, WEAPON_AWAY_DP_WEAPON);
  c->a = (uint16_t)(wram_r16(w, (uint16_t)(c->y + ACTOR_FLAGS)) & ~ACTOR_DRAW);
  set_nz16(c, c->a);
  wram_w16(w, (uint16_t)(c->y + ACTOR_FLAGS), c->a);
  return true;
}

FiredWait fired_wait(Wram* w, PortCpu* c) {
  c->x = field(w, c, PLAYER_SMALL_DP_PLAYER);
  c->a = wram_r16(w, (uint16_t)(W_FIRED_WAIT_HELD + c->x));
  cmp16(c, c->a, field(w, c, FIRED_WAIT_DP_HELD));
  c->pc = FIRED_WAIT_RTS_PC;
  if (c->a != field(w, c, FIRED_WAIT_DP_HELD)) {
    PORT_COVER(fired_wait_moved);
    return FIRED_WAIT_MOVED;
  }
  const uint16_t left = (uint16_t)(field(w, c, FIRED_WAIT_DP_LEFT) - 1);
  set_field(w, c, FIRED_WAIT_DP_LEFT, left);
  set_nz16(c, left);
  if (left == 0) {
    PORT_COVER(fired_wait_done);
    return FIRED_WAIT_DONE;
  }
  PORT_COVER(fired_wait_on);
  c->a = 1;
  set_nz16(c, c->a);
  c->pc = FIRED_WAIT_YIELD_PC;
  return FIRED_WAIT_ON;
}

static uint16_t list_word(const Rom* rom, uint16_t at) {
  return rom_word(rom, ((uint32_t)PLAYER_SMALL_BANK << 16) | at);
}

bool tile_search_supported(const Wram* w, const Rom* rom, uint16_t page) {
  if (wram_r16(w, (uint16_t)(page + TILE_SEARCH_DP_STATE)) ==
      TILE_SEARCH_RESTING)
    return true;
  const uint16_t facing = wram_r16(w, (uint16_t)(page + TILE_SEARCH_DP_FACING));
  if (facing > TILE_SEARCH_FACING_MAX || (facing & 1u)) return false;
  const uint16_t list = rom_word(rom, TILE_SEARCH_LISTS + facing);
  if (list < 0x8000u || list > 0xfe00u) return false;
  const uint16_t tiles = list_word(rom, list);
  return tiles != 0 && tiles <= TILE_SEARCH_MOST;
}

// A tile's middle, in pixels: `ASL` three times, and four more.
static uint16_t tile_middle(PortCpu* c, uint16_t tile) {
  const uint16_t pixels = asl16(c, asl16(c, asl16(c, tile)));
  set_c(c, false);
  return adc16(c, pixels, 4);
}

void tile_search(Wram* w, const Rom* rom, PortCpu* c, TileSearch* did) {
  *did = (TileSearch){0};
  c->a = field(w, c, TILE_SEARCH_DP_STATE);
  cmp16(c, c->a, TILE_SEARCH_RESTING);
  if (c->a == TILE_SEARCH_RESTING) {
    PORT_COVER(tile_search_resting);
    did->resting = true;
    set_c(c, false);
    c->pc = TILE_SEARCH_RESTING_RTS_PC;
    return;
  }

  const uint16_t col = (uint16_t)(field(w, c, PLAYER_SPAN_DP_X) >> 3);
  set_field(w, c, TILE_SEARCH_DP_COL, col);
  const uint16_t row =
      (uint16_t)((uint16_t)(field(w, c, PLAYER_SPAN_DP_Y) - TILE_SEARCH_ABOVE) >>
                 3);
  set_field(w, c, TILE_SEARCH_DP_ROW, row);
  const uint16_t list =
      rom_word(rom, TILE_SEARCH_LISTS + field(w, c, TILE_SEARCH_DP_FACING));
  uint16_t left = list_word(rom, list);
  set_field(w, c, TILE_SEARCH_DP_LEFT, left);

  uint16_t at = (uint16_t)(list + 2);
  for (;;) {
    set_field(w, c, TILE_SEARCH_DP_AT, at);
    TerrainRegs tile;
    terrain_tile_bit3(w, (uint16_t)(list_word(rom, at) + col),
                      (uint16_t)(list_word(rom, (uint16_t)(at + 2)) + row),
                      &tile);
    did->looked++;
    c->x = tile.x;
    if (tile.blocked) break;
    set_c(c, false);
    at = adc16(c, at, 4);
    c->a = at;
    c->y = at;
    left--;
    set_field(w, c, TILE_SEARCH_DP_LEFT, left);
    set_nz16(c, left);
    if (left == 0) {
      PORT_COVER(tile_search_none);
      set_c(c, false);
      c->pc = TILE_SEARCH_NONE_RTS_PC;
      return;
    }
  }

  PORT_COVER(tile_search_found);
  did->found = true;
  set_c(c, false);
  set_field(w, c, PLAYER_SPAN_DP_POINT_X,
            tile_middle(c, adc16(c, list_word(rom, at), col)));
  set_c(c, false);
  set_field(w, c, PLAYER_SPAN_DP_POINT_Y,
            tile_middle(c, adc16(c, list_word(rom, (uint16_t)(at + 2)), row)));
  player_span(w, c, &did->span);
  c->y = at;
  set_c(c, true);
  c->pc = TILE_SEARCH_FOUND_RTS_PC;
}
