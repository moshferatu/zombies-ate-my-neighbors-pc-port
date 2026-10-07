// $81:F98A  the thing thrown in an arc -- see port/lob.h.

#include "port/lob.h"

#include "port/begin.h"  // RECORD_SEARCHED_FIRST
#include "port/coverage.h"
#include "port/oam.h"  // the display record's fields
#include "port/player_frame.h"  // W_BOX_*

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

// ---------------------------------------------------------------------------
// The rest of the thread
// ---------------------------------------------------------------------------

bool lob_begin(Wram* w, PortCpu* c, uint16_t* record_out) {
  const uint16_t page = c->d;
  set_c(c, false);
  wram_w16(w, W_SPAWN_LOAD, adc16(c, wram_r16(w, W_SPAWN_LOAD), LOB_LOAD));

  SlotAllocRegs slot;
  actor_slot_alloc(w, c->db, &slot);
  if (slot.c) return false;
  PORT_COVER(lob_began);
  const uint16_t record = slot.a;
  *record_out = record;
  // The search for a record subtracts for each it passes over, which leaves
  // overflow clear; taking the first, it leaves the load's.
  if (record != RECORD_SEARCHED_FIRST) set_v(c, false);
  set_c(c, slot.c);
  set_field(w, page, LOB_DP_RECORD, record);
  set_field(w, page, LOB_DP_SPRITE, record);
  wram_w16(w, (uint16_t)(record + ACTOR_X), field(w, page, LOB_DP_PLACE_X));
  wram_w16(w, (uint16_t)(record + ACTOR_Z), LOB_START_HEIGHT);
  wram_w16(w, (uint16_t)(record + ACTOR_Y), field(w, page, LOB_DP_PLACE_Y));
  wram_w16(w, (uint16_t)(record + ACTOR_META), LOB_START_PICTURE);
  wram_w16(w, (uint16_t)(record + ACTOR_META_BANK), LOB_PICTURE_BANK);
  wram_w16(w, (uint16_t)(record + ACTOR_COLLIDE_ID), 0);
  wram_w16(w, (uint16_t)(record + ACTOR_THREAD),
           wram_r16(w, W_SCHED_CUR_TASK));
  wram_w16(w, (uint16_t)(record + ACTOR_FLAGS),
           (uint16_t)(wram_r16(w, (uint16_t)(record + ACTOR_FLAGS)) |
                      LOB_START_FLAGS));
  set_field(w, page, LOB_DP_SLOW_MASK, LOB_START_SLOW_MASK);
  set_field(w, page, LOB_DP_RISE, LOB_START_RISE);
  set_field(w, page, LOB_DP_PICTURE_FRAMES, 0);
  set_field(w, page, LOB_DP_FRAMES, 0);
  c->x = slot.x;
  c->y = record;
  c->a = 1;
  set_nz16(c, c->a);
  c->pc = LOB_YIELD_PC;
  return true;
}

void lob_landed(Wram* w, PortCpu* c) {
  PORT_COVER(lob_came_down);
  const uint16_t record = field(w, c->d, LOB_DP_RECORD);
  wram_w16(w, (uint16_t)(record + ACTOR_Z), 0);
  c->y = record;
  c->a = LOB_SFX_LAND;
  set_nz16(c, c->a);
  c->pc = LOB_LANDED_SOUND_PC;
}

bool lob_burst(Wram* w, const Rom* rom, PortCpu* c, bool again,
               ActorNotifyWork* told) {
  const uint16_t record = field(w, c->d, LOB_DP_RECORD);
  set_c(c, true);
  const uint16_t left =
      sbc16(c, wram_r16(w, (uint16_t)(record + ACTOR_X)), LOB_BURST_REACH);
  wram_w16(w, W_BOX_LEFT, left);
  set_c(c, false);
  wram_w16(w, W_BOX_RIGHT, adc16(c, left, 2 * LOB_BURST_REACH));
  set_c(c, true);
  const uint16_t top =
      sbc16(c, wram_r16(w, (uint16_t)(record + ACTOR_Y)), LOB_BURST_REACH);
  wram_w16(w, W_BOX_TOP, top);
  set_c(c, false);
  wram_w16(w, W_BOX_BOTTOM, adc16(c, top, 2 * LOB_BURST_REACH));
  const uint16_t id =
      table(rom, (uint16_t)(LOB_BURST_IDS + field(w, c->d, LOB_DP_PLAYER)));
  wram_w16(w, W_BOX_ID, id);

  ThreadCallResult tail = {.c = flag(c, PORT_P_C)};
  ActorNotifyRegs regs;
  if (!actor_notify_box_counted(w, rom, id, flag(c, PORT_P_C), &tail, &regs,
                                told))
    return false;
  PORT_COVER_IF(again, lob_burst_again, lob_burst);
  c->x = regs.x;
  c->y = regs.y;
  set_c(c, regs.c);
  c->a = again ? LOB_BURST_PICTURES_REST : 1;
  set_nz16(c, c->a);
  c->pc = again ? LOB_BURST_PLAY_PC : LOB_BURST_YIELD_PC;
  return true;
}

bool lob_end(Wram* w, PortCpu* c) {
  set_c(c, true);
  const uint16_t load = sbc16(c, wram_r16(w, W_SPAWN_LOAD), LOB_LOAD);
  if (load & 0x8000u) return false;
  PORT_COVER(lob_ended);
  wram_w16(w, W_SPAWN_LOAD, load);
  c->a = field(w, c->d, LOB_DP_RECORD);
  set_nz16(c, c->a);
  c->pc = LOB_END_FREE_PC;
  return true;
}
