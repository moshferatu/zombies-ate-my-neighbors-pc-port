// The level's main loop, `$80:8528`: the thread that runs while a level is
// being played.
//
// Every second tick it checks for a pause, refreshes one player's HUD panel,
// and asks two questions. Is anyone still playing? If not it returns with
// carry set, and the game is over. Are there neighbours left to save? If not
// it goes on to end the level. Otherwise it yields and comes round again.
//
//     $80:8528  LDA #$0002 : JSL thread_yield
//     $80:852F  JSL pause_check : JSL hud_refresh
//               LDA $1E88 : ORA $1E8A : BEQ $85B5
//               LDA $1D52 : BNE $8528
//
// This is one pass of it, from where `thread_yield` returns. A frame somebody
// pauses on is the ROM's, since the pause waits inside the check itself (see
// `port/pause.h`).
//
// Port code: libc only.

#ifndef PORT_MAINLOOP_H
#define PORT_MAINLOOP_H

#include <stdint.h>

#include "assets/rom.h"
#include "port/hud.h"
#include "port/wram.h"

#define MAINLOOP_FRAME_PC 0x80852fu
#define MAINLOOP_YIELD_PC 0x80852bu          // `JSL thread_yield`, A already 2
#define MAINLOOP_NO_PLAYERS_PC 0x8085b5u     // `SEC : RTL`
#define MAINLOOP_NO_NEIGHBOURS_PC 0x808544u  // the level's ending
#define MAINLOOP_YIELD_TICKS 2

typedef enum {
  MAINLOOP_GOES_ON,
  MAINLOOP_NO_PLAYERS,
  MAINLOOP_NO_NEIGHBOURS,
} MainLoopNext;

// One pass, on a frame nobody is pausing on. `page` is the thread's, where
// the HUD keeps which panel's turn it is. `hud` carries X, Y and carry in and
// the refresh's registers out, as `hud_refresh` wants them.
MainLoopNext mainloop_frame(Wram* w, const Rom* rom, uint16_t page,
                            HudRefreshRegs* hud);

#endif
