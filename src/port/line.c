// $81:D443  a step along a line -- see port/line.h.

#include "port/line.h"

#include "port/coverage.h"
#include "port/oam.h"  // the display record's fields

bool line_step_supported(const Wram* w, uint16_t page) {
  return wram_r16(w, (uint16_t)(page + LINE_DP_WHOLE)) != 0;
}

// One axis: the sum takes its part, and gives up a step for each whole.
static uint16_t along(Wram* w, PortCpu* c, LineWork* k, uint16_t sum,
                      uint16_t place, uint16_t step) {
  const uint16_t whole = wram_r16(w, (uint16_t)(c->d + LINE_DP_WHOLE));
  for (;;) {
    cmp16(c, sum, whole);
    k->blocks[LN_TEST]++;
    if (sum < whole) break;
    PORT_COVER(line_stepped);
    c->x = sum;
    set_c(c, false);
    wram_w16(w, (uint16_t)(c->d + place),
             adc16(c, wram_r16(w, (uint16_t)(c->d + place)),
                   wram_r16(w, (uint16_t)(c->d + step))));
    set_c(c, true);
    sum = sbc16(c, sum, whole);
    k->blocks[LN_STEP]++;
  }
  k->blocks[LN_TAKEN]++;
  return sum;
}

void line_step(Wram* w, PortCpu* c, LineWork* k) {
  const uint16_t d = c->d;
  PORT_COVER(line_moved);
  set_c(c, false);
  uint16_t sum = adc16(c, wram_r16(w, (uint16_t)(d + LINE_DP_SUM_X)),
                       wram_r16(w, (uint16_t)(d + LINE_DP_PART_X)));
  k->blocks[LN_HEAD]++;
  wram_w16(w, (uint16_t)(d + LINE_DP_SUM_X),
           along(w, c, k, sum, LINE_DP_X, LINE_DP_STEP_X));

  set_c(c, false);
  sum = adc16(c, wram_r16(w, (uint16_t)(d + LINE_DP_SUM_Y)),
              wram_r16(w, (uint16_t)(d + LINE_DP_PART_Y)));
  k->blocks[LN_MID]++;
  wram_w16(w, (uint16_t)(d + LINE_DP_SUM_Y),
           along(w, c, k, sum, LINE_DP_Y, LINE_DP_STEP_Y));

  c->y = wram_r16(w, (uint16_t)(d + LINE_DP_RECORD));
  wram_w16(w, (uint16_t)(c->y + ACTOR_X),
           wram_r16(w, (uint16_t)(d + LINE_DP_X)));
  c->a = wram_r16(w, (uint16_t)(d + LINE_DP_Y));
  set_nz16(c, c->a);
  wram_w16(w, (uint16_t)(c->y + ACTOR_Y), c->a);
  k->blocks[LN_TAIL]++;
  c->pc = LINE_STEP_RTS_PC;
}
