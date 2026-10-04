// $80:852F  the level's main loop -- see port/mainloop.h.

#include "port/mainloop.h"

#include "port/bodies.h"  // W_NEIGHBOURS_LEFT

static bool anyone_playing(const Wram* w) {
  return (wram_r16(w, W_HUD_PANEL_ON) | wram_r16(w, W_HUD_PANEL_ON + 2)) != 0;
}

MainLoopNext mainloop_frame(Wram* w, const Rom* rom, uint16_t page,
                            HudRefreshRegs* hud) {
  hud_refresh(w, rom, page, hud);
  if (!anyone_playing(w)) return MAINLOOP_NO_PLAYERS;
  if (wram_r16(w, W_NEIGHBOURS_LEFT) == 0) return MAINLOOP_NO_NEIGHBOURS;
  return MAINLOOP_GOES_ON;
}
