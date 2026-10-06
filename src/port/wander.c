// $81:D948  a thing looking for somewhere near to go -- see port/wander.h.

#include "port/wander.h"

#include "port/coverage.h"
#include "port/oam.h"  // the display record's fields
#include "port/rng.h"

static uint16_t field(const Wram* w, const PortCpu* c, uint16_t at) {
  return wram_r16(w, (uint16_t)(c->d + at));
}

static void set_field(Wram* w, const PortCpu* c, uint16_t at, uint16_t v) {
  wram_w16(w, (uint16_t)(c->d + at), v);
}

// A draw of 0 to 47 less 24, four pixels to each, from the word at `place`,
// which is read after the draw. The draw takes whatever carry was left.
static uint16_t drawn_near(Wram* w, PortCpu* c, WanderWork* k,
                           uint16_t place) {
  RngResult r;
  rng_next(w, flag(c, PORT_P_C), &r);
  if (r.v)
    k->draws_twice++;
  else
    k->draws_once++;
  set_c(c, true);
  const uint16_t off =
      sbc16(c, (uint16_t)(r.a & WANDER_DRAW_MASK), WANDER_DRAW_HALF);
  const uint16_t pixels = asl16(c, asl16(c, off));
  set_c(c, false);
  return adc16(c, pixels, wram_r16(w, place));
}

static bool tile_will_do(const Wram* w, PortCpu* c, WanderWork* k) {
  TileAttrsRegs tile;
  tile_attrs_at_pixel(w, c->x, c->y, &tile);
  k->tiles++;
  k->overflow_unknown = true;
  set_c(c, tile.c);
  c->a = (uint16_t)(tile.a & WANDER_TILE_BIT);
  set_nz16(c, c->a);
  return c->a != 0;
}

// One draw of a spot. True when it will do.
static bool try_spot(Wram* w, PortCpu* c, WanderWork* k) {
  const uint16_t record = field(w, c, WANDER_DP_RECORD);
  const uint16_t x = drawn_near(w, c, k, (uint16_t)(record + ACTOR_X));
  set_field(w, c, WANDER_DP_X, x);
  const uint16_t y = drawn_near(w, c, k, (uint16_t)(record + ACTOR_Y));
  set_field(w, c, WANDER_DP_Y, y);
  k->overflow_unknown = false;

  BoundsRegs edge;
  terrain_out_of_bounds(w, x, y, &edge);
  k->bounds_exits[edge.exit]++;
  c->a = edge.a;
  c->x = x;
  c->y = y;
  set_c(c, edge.c);
  k->blocks[WA_DRAW]++;
  if (edge.c) {
    PORT_COVER(wander_off_the_level);
    k->blocks[WA_TAKEN]++;
    return false;
  }

  const bool here = tile_will_do(w, c, k);
  k->blocks[WA_TILE]++;
  if (!here) {
    PORT_COVER(wander_tile_wrong);
    k->blocks[WA_TAKEN]++;
    return false;
  }

  set_c(c, false);
  c->y = adc16(c, y, WANDER_BELOW);
  const bool below = tile_will_do(w, c, k);
  k->blocks[WA_BELOW]++;
  if (!below) {
    PORT_COVER(wander_tile_below_wrong);
    return false;
  }
  k->blocks[WA_TAKEN]++;
  return true;
}

void wander_pick(Wram* w, PortCpu* c, WanderWork* k) {
  const uint16_t wanted = field(w, c, WANDER_DP_WANTED);
  c->a = wanted;
  cmp16(c, wanted, field(w, c, WANDER_DP_HAD));
  k->blocks[WA_HEAD]++;
  c->pc = WANDER_RTS_PC;
  if (wanted == field(w, c, WANDER_DP_HAD)) {
    PORT_COVER(wander_content);
    return;
  }
  k->blocks[WA_TAKEN]++;

  uint16_t left = WANDER_TRIES;
  set_field(w, c, WANDER_DP_TRIES_LEFT, left);
  k->blocks[WA_TRIES]++;
  for (;;) {
    if (try_spot(w, c, k)) {
      PORT_COVER(wander_found);
      c->a = wanted;
      set_nz16(c, c->a);
      set_field(w, c, WANDER_DP_HAD, wanted);
      k->blocks[WA_FOUND]++;
      c->pc = WANDER_FOUND_PC;
      return;
    }
    left--;
    set_field(w, c, WANDER_DP_TRIES_LEFT, left);
    set_nz16(c, left);
    k->blocks[WA_NEXT]++;
    if (left == 0) break;
    k->blocks[WA_TAKEN]++;
  }
  PORT_COVER(wander_gave_up);
  k->blocks[WA_GIVE_UP]++;
}
