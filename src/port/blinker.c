// $83:9C94  a thing of two pictures, turn about -- see port/blinker.h.

#include "port/blinker.h"

#include "port/coverage.h"
#include "port/oam.h"  // the display record's fields

void blinker_frame(Wram* w, const Rom* rom, PortCpu* c, BlinkerWork* k) {
  const uint16_t page = c->d;
  const uint16_t which =
      (uint16_t)(wram_r16(w, (uint16_t)(page + BLINKER_DP_WHICH)) ^ 1);
  wram_w16(w, (uint16_t)(page + BLINKER_DP_WHICH), which);
  c->x = asl16(c, asl16(c, which));
  c->y = ACTOR_META;
  wram_w16(w,
           (uint16_t)(wram_r16(w, (uint16_t)(page + BLINKER_DP_RECORD)) +
                      ACTOR_META),
           rom_word(rom, BLINKER_PICTURES + c->x));
  const uint16_t frames = rom_word(rom, BLINKER_PICTURES + 2 + c->x);
  wram_w16(w, (uint16_t)(page + BLINKER_DP_FRAMES), frames);
  c->a = wram_r16(w, (uint16_t)(page + BLINKER_DP_TOLD));
  set_nz16(c, c->a);
  k->blocks[BK_TURN]++;
  if (c->a != 0) {
    PORT_COVER(blinker_told);
    c->pc = BLINKER_TOLD_PC;
    return;
  }
  PORT_COVER(blinker_turned);
  k->blocks[BK_TAKEN]++;
  c->a = frames;
  set_nz16(c, c->a);
  k->blocks[BK_AGAIN]++;
  c->pc = BLINKER_SLEEP_PC;
}
