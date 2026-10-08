// $80:852F  the level's main loop -- see port/mainloop.h.

#include "port/mainloop.h"

#include "port/bodies.h"  // W_NEIGHBOURS_LEFT
#include "port/coverage.h"
#include "port/player_frame.h"  // W_RESCUED

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

MainLoopNext mainloop_leaving_frame(Wram* w, const Rom* rom, uint16_t page,
                                    HudRefreshRegs* hud, uint16_t* playing) {
  hud_refresh(w, rom, page, hud);
  if ((wram_r16(w, W_RESCUED) | wram_r16(w, W_RESCUED + 2)) == 0) {
    PORT_COVER(mainloop_none_rescued);
    return MAINLOOP_NONE_RESCUED;
  }
  if (!anyone_playing(w)) {
    PORT_COVER(mainloop_leaving_nobody);
    return MAINLOOP_NO_PLAYERS;
  }
  const uint16_t out = (uint16_t)(wram_r16(w, W_PLAYERS_OUT) +
                                  wram_r16(w, W_PLAYERS_OUT + 2));
  wram_w16(w, W_MAINLOOP_OUT, out);
  *playing = (uint16_t)(wram_r16(w, W_HUD_PANEL_ON) +
                        wram_r16(w, W_HUD_PANEL_ON + 2));
  if (*playing == out) {
    PORT_COVER(mainloop_all_out);
    return MAINLOOP_ALL_OUT;
  }
  PORT_COVER(mainloop_leaving_on);
  return MAINLOOP_GOES_ON;
}
