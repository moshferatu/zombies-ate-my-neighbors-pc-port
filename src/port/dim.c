// Five loops in bank $82 that fade a screen -- see port/dim.h.

#include "port/dim.h"

#include "port/coverage.h"

const DimLoop DIM_LOOPS[DIM_LOOP_COUNT] = {
    [DIM_NAME_OUT] = {0x82ac9bu, 0x82ac9au, 0x82aca6u, false},
    [DIM_CURSOR_OUT] = {0x82b2f2u, 0x82b2f1u, 0x82b2fdu, false},
    [DIM_SCORES_OUT] = {0x82ba37u, 0x82ba36u, 0x82ba42u, false},
    [DIM_CURSOR_IN] = {0x82b566u, 0x82b565u, 0x82b574u, true},
    [DIM_SCORES_IN] = {0x82bae9u, 0x82bae8u, 0x82baf7u, true},
};

bool dim_frame(Wram* w, PortCpu* c, const DimLoop* loop) {
  c->a = wram_r16(w, W_BRIGHTNESS_SHADOW);
  if (loop->lighter)
    cmp16(c, c->a, DIM_FULL);
  else
    set_nz16(c, c->a);
  if (c->a == (loop->lighter ? DIM_FULL : 0)) {
    if (loop->lighter)
      PORT_COVER(dim_light);
    else
      PORT_COVER(dim_dark);
    c->pc = loop->done;
    return true;
  }
  if (loop->lighter) {
    PORT_COVER(dim_lighter);
    c->a++;
  } else {
    PORT_COVER(dim_darker);
    c->a--;
  }
  set_nz16(c, c->a);
  wram_w16(w, W_BRIGHTNESS_SHADOW, c->a);
  c->pc = loop->wai;
  return false;
}
