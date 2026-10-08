// $83:9EDB to $83:9F2E  the neighbour who jumps -- see port/jumper.h.

#include "port/jumper.h"

#include "port/coverage.h"
#include "port/oam.h"  // the display record's fields

static uint16_t field(const Wram* w, uint16_t page, uint16_t at) {
  return wram_r16(w, (uint16_t)(page + at));
}

static void lda(PortCpu* c, uint16_t v) {
  c->a = v;
  set_nz16(c, v);
}

void jumper_frame(Wram* w, PortCpu* c, bool down, JumperWork* k) {
  const uint16_t page = c->d;
  lda(c, field(w, page, JUMPER_DP_EVENT));
  k->blocks[JP_EVENT]++;
  if (c->a & 0x8000u) {
    PORT_COVER(jumper_ended);
    k->blocks[JP_TAKEN]++;
    c->pc = JUMPER_ENDED_PC;
    return;
  }

  const uint16_t left = (uint16_t)(field(w, page, JUMPER_DP_FRAMES) - 1);
  wram_w16(w, (uint16_t)(page + JUMPER_DP_FRAMES), left);
  set_nz16(c, left);
  k->blocks[JP_COUNT]++;
  if (left != 0) {
    k->blocks[JP_TAKEN]++;
  } else if (down) {
    // On the ground again, where it can be touched.
    PORT_COVER(jumper_landed);
    c->x = field(w, page, JUMPER_DP_RECORD);
    lda(c, JUMPER_LANDED_ID);
    wram_w16(w, (uint16_t)(c->x + ACTOR_COLLIDE_ID), JUMPER_LANDED_ID);
    k->blocks[JP_LAND]++;
    c->pc = JUMPER_LANDED_PC;
    return;
  } else {
    // The top: the same count again, and this frame is the first down.
    PORT_COVER(jumper_turned);
    wram_w16(w, (uint16_t)(page + JUMPER_DP_FRAMES), JUMPER_FRAMES);
    k->blocks[JP_TURN]++;
    down = true;
  }

  PORT_COVER_IF(down, jumper_fell, jumper_rose);
  c->x = field(w, page, JUMPER_DP_RECORD);
  const uint16_t z = wram_r16(w, (uint16_t)(c->x + ACTOR_Z));
  wram_w16(w, (uint16_t)(c->x + ACTOR_Z), (uint16_t)(down ? z - 1 : z + 1));
  lda(c, 1);
  k->blocks[JP_STEP]++;
  c->pc = down ? JUMPER_DOWN_YIELD_PC : JUMPER_UP_YIELD_PC;
}

void jumper_stand(Wram* w, PortCpu* c, JumperWork* k) {
  const uint16_t page = c->d;
  PORT_COVER(jumper_stood);
  c->y = field(w, page, JUMPER_DP_UNDER);
  wram_w16(w, (uint16_t)(c->y + ACTOR_FLAGS),
           (uint16_t)(wram_r16(w, (uint16_t)(c->y + ACTOR_FLAGS)) |
                      ACTOR_DRAW));
  k->blocks[JP_SHOW]++;
  c->y = field(w, page, JUMPER_DP_RECORD);
  wram_w16(w, (uint16_t)(c->y + ACTOR_META), JUMPER_STAND_PICTURE);
  lda(c, JUMPER_STAND_FRAMES);
  k->blocks[JP_STAND]++;
  c->pc = JUMPER_STAND_YIELD_PC;
}

void jumper_rested(Wram* w, PortCpu* c, JumperWork* k) {
  const uint16_t page = c->d;
  lda(c, field(w, page, JUMPER_DP_EVENT));
  k->blocks[JP_ANY]++;
  if (c->a != 0) {
    PORT_COVER(jumper_disturbed);
    k->blocks[JP_TAKEN]++;
    c->pc = JUMPER_ENDED_PC;
    return;
  }

  // Off the ground, where nothing can touch it.
  PORT_COVER(jumper_jumped);
  c->x = field(w, page, JUMPER_DP_RECORD);
  wram_w16(w, (uint16_t)(c->x + ACTOR_META), JUMPER_AIR_PICTURE);
  wram_w16(w, (uint16_t)(c->x + ACTOR_COLLIDE_ID), 0);
  wram_w16(w, (uint16_t)(page + JUMPER_DP_FRAMES), JUMPER_FRAMES);
  k->blocks[JP_BEGIN]++;
  wram_w16(w, (uint16_t)(c->x + ACTOR_Z),
           (uint16_t)(wram_r16(w, (uint16_t)(c->x + ACTOR_Z)) + 1));
  lda(c, 1);
  k->blocks[JP_STEP]++;
  c->pc = JUMPER_UP_YIELD_PC;
}
