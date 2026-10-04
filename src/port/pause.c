// $80:89B0  the pause check -- see port/pause.h.

#include "port/pause.h"

#include "port/player.h"  // W_JOY_RAW

uint16_t pause_pad_one(const Wram* w) { return wram_r16(w, W_JOY_RAW); }

bool pause_wanted(const Wram* w) {
  const uint16_t either = pause_pad_one(w) | wram_r16(w, W_JOY_RAW + 2);
  return (either & PAUSE_PAD_START) != 0;
}
