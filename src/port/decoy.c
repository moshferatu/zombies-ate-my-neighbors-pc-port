// $81:F16D  a thing put down that the monsters go for -- see port/decoy.h.

#include "port/decoy.h"

#include "port/coverage.h"

static uint16_t field(const Wram* w, const PortCpu* c, uint16_t at) {
  return wram_r16(w, (uint16_t)(c->d + at));
}

static void set_field(Wram* w, const PortCpu* c, uint16_t at, uint16_t v) {
  wram_w16(w, (uint16_t)(c->d + at), v);
}

void decoy_frame(Wram* w, const Rom* rom, PortCpu* c, DecoyWork* k) {
  TileAttrsRegs tile;
  tile_attrs_at_pixel(w, field(w, c, DECOY_DP_X), field(w, c, DECOY_DP_Y),
                      &tile);
  c->a = tile.a;
  cmp16(c, c->a, DECOY_TILE_ENDS);
  if (c->a == DECOY_TILE_ENDS) {
    PORT_COVER(decoy_on_an_ending_tile);
    k->end = DECOY_ON_AN_ENDING_TILE;
    c->pc = DECOY_ENDED_PC;
    return;
  }
  if (c->a & DECOY_TILE_MOVES) {
    k->declined = true;
    return;
  }

  uint16_t picture = (uint16_t)(field(w, c, DECOY_DP_PICTURE) + 1);
  cmp16(c, picture, DECOY_PICTURE_COUNT);
  if (picture >= DECOY_PICTURE_COUNT) {
    PORT_COVER(decoy_sounded);
    ApuSfxRegs sound;
    apu_play_sfx(w, DECOY_SFX, c->d, &sound);
    k->played = true;
    picture = 0;
  }
  set_field(w, c, DECOY_DP_PICTURE, picture);
  c->x = asl16(c, picture);
  c->y = field(w, c, DECOY_DP_RECORD);
  wram_w16(w, (uint16_t)(c->y + ACTOR_META),
           rom_word(rom, DECOY_PICTURES + c->x));

  c->a = field(w, c, DECOY_DP_HIT);
  set_nz16(c, c->a);
  c->pc = DECOY_ENDED_PC;
  if (c->a & 0x8000u) {
    PORT_COVER(decoy_hit);
    k->end = DECOY_HIT;
    return;
  }
  const uint16_t left = (uint16_t)(field(w, c, DECOY_DP_TURNS_LEFT) - 1);
  set_field(w, c, DECOY_DP_TURNS_LEFT, left);
  set_nz16(c, left);
  if (left == 0) {
    PORT_COVER(decoy_ran_out);
    k->end = DECOY_RAN_OUT;
    return;
  }
  PORT_COVER(decoy_stood);
  c->a = DECOY_FRAMES;
  set_nz16(c, c->a);
  c->pc = DECOY_SLEEP_PC;
}
