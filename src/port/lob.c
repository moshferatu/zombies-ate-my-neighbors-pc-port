// $81:F98A  the thing thrown in an arc -- see port/lob.h.

#include "port/lob.h"

#include "port/coverage.h"
#include "port/oam.h"  // the display record's fields

static uint16_t field(const Wram* w, uint16_t page, uint16_t at) {
  return wram_r16(w, (uint16_t)(page + at));
}

static void set_field(Wram* w, uint16_t page, uint16_t at, uint16_t v) {
  wram_w16(w, (uint16_t)(page + at), v);
}

static uint16_t table(const Rom* rom, uint16_t at) {
  return rom_word(rom, ((uint32_t)LOB_BANK << 16) | at);
}

bool lob_frame_supported(const Wram* w, uint16_t page) {
  return field(w, page, LOB_DP_WAY) <= LOB_WAY_MAX;
}

void lob_frame(Wram* w, const Rom* rom, PortCpu* c, LobWork* k) {
  const uint16_t page = c->d;
  const uint16_t record = field(w, page, LOB_DP_RECORD);
  c->y = record;

  // `$81:FEEF`: up by the rise, which may be less than nothing.
  set_c(c, false);
  wram_w16(w, (uint16_t)(record + ACTOR_Z),
           adc16(c, field(w, page, LOB_DP_RISE),
                 wram_r16(w, (uint16_t)(record + ACTOR_Z))));
  const uint16_t frames = (uint16_t)(field(w, page, LOB_DP_FRAMES) + 1);
  set_field(w, page, LOB_DP_FRAMES, frames);
  k->blocks[LB_ARC]++;
  if ((frames & field(w, page, LOB_DP_SLOW_MASK)) == 0) {
    PORT_COVER(lob_slowed);
    set_field(w, page, LOB_DP_RISE,
              (uint16_t)(field(w, page, LOB_DP_RISE) - 1));
    k->blocks[LB_ARC_SLOW]++;
  } else {
    k->blocks[LB_TAKEN]++;
  }

  // `$81:FA55`: along.
  c->x = (uint16_t)(field(w, page, LOB_DP_WAY) << 1);
  set_c(c, false);
  wram_w16(w, (uint16_t)(record + ACTOR_X),
           adc16(c, table(rom, (uint16_t)(LOB_STEPS + c->x)),
                 wram_r16(w, (uint16_t)(record + ACTOR_X))));
  set_c(c, false);
  wram_w16(w, (uint16_t)(record + ACTOR_Y),
           adc16(c, table(rom, (uint16_t)(LOB_STEPS + 2 + c->x)),
                 wram_r16(w, (uint16_t)(record + ACTOR_Y))));
  k->blocks[LB_MOVE]++;

  // `$81:FA22`: the next picture, when it is time.
  const uint16_t wait =
      (uint16_t)(field(w, page, LOB_DP_PICTURE_FRAMES) - 1);
  set_field(w, page, LOB_DP_PICTURE_FRAMES, wait);
  k->blocks[LB_PIC_COUNT]++;
  if ((wait & 0x8000u) == 0) {
    k->blocks[LB_TAKEN]++;
  } else {
    set_field(w, page, LOB_DP_PICTURE_FRAMES, LOB_PICTURE_FRAMES);
    uint16_t pictures = LOB_PICTURES_LOW;
    k->blocks[LB_PIC]++;
    if (wram_r16(w, (uint16_t)(record + ACTOR_Z)) >= LOB_HIGH) {
      PORT_COVER(lob_picture_high);
      pictures = LOB_PICTURES_HIGH;
      k->blocks[LB_PIC_HIGH]++;
    } else {
      PORT_COVER(lob_picture_low);
      k->blocks[LB_TAKEN]++;
    }
    set_field(w, page, LOB_DP_PICTURES, pictures);
    const uint16_t picture =
        (uint16_t)((field(w, page, LOB_DP_PICTURE) + 1) & 3);
    set_field(w, page, LOB_DP_PICTURE, picture);
    wram_w16(w, (uint16_t)(record + ACTOR_META),
             table(rom, (uint16_t)(pictures + picture * 2)));
    wram_w16(w, (uint16_t)(record + ACTOR_META_BANK), LOB_PICTURE_BANK);
    c->x = record;
    set_c(c, false);  // the `ASL` of the picture's number
  }

  c->a = wram_r16(w, (uint16_t)(record + ACTOR_Z));
  set_nz16(c, c->a);
  k->blocks[LB_TEST]++;
  if (c->a & 0x8000u) {
    PORT_COVER(lob_landed);
    k->blocks[LB_TAKEN]++;
    c->pc = LOB_LANDED_PC;
    return;
  }
  PORT_COVER(lob_flew);
  c->a = 1;
  set_nz16(c, 1);
  k->blocks[LB_AGAIN]++;
  c->pc = LOB_YIELD_PC;
}
