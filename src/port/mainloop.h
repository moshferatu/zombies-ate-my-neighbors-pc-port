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
// With no neighbours left to save the ROM begins a thread for each player
// and goes round a second loop, `$80:857E`, which `mainloop_leaving_frame`
// is a pass of. It checks for a pause and refreshes the HUD as the first
// does, and asks three questions. Has either player a neighbour to their
// name (`$1F9C`, `$1F9E`)? Is anyone playing? If not, either way, carry set
// and the game is over. Then it adds up `$1FB8` and `$1FBA`, and when that
// is as many as are playing the level is done. I take those two words to be
// who has left by the exit: I have not read what sets them.
//
//     $80:857E  LDA #$0002 : JSL thread_yield
//     $80:8585  JSL pause_check : JSL hud_refresh
//               LDA $1F9C : ORA $1F9E : BEQ $85B5
//               LDA $1E88 : ORA $1E8A : BEQ $85B5
//               CLC : LDA $1FB8 : ADC $1FBA : STA $0038
//               CLC : LDA $1E88 : ADC $1E8A : CMP $0038 : BEQ $85B7
//               BRA $857E
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
#define MAINLOOP_LEAVING_PC 0x808585u
#define MAINLOOP_LEAVING_YIELD_PC 0x808581u  // `JSL thread_yield`, A already 2
#define MAINLOOP_ALL_OUT_PC 0x8085b7u        // the level is done
#define W_PLAYERS_OUT 0x1fb8u   // a word for each player
#define W_MAINLOOP_OUT 0x0038u  // their sum, kept for the compare

typedef enum {
  MAINLOOP_GOES_ON,
  MAINLOOP_NO_PLAYERS,
  MAINLOOP_NO_NEIGHBOURS,
  MAINLOOP_NONE_RESCUED,  // the second loop's: neither player has one
  MAINLOOP_ALL_OUT,       // ...and everyone playing has left
} MainLoopNext;

// One pass, on a frame nobody is pausing on. `page` is the thread's, where
// the HUD keeps which panel's turn it is. `hud` carries X, Y and carry in and
// the refresh's registers out, as `hud_refresh` wants them.
MainLoopNext mainloop_frame(Wram* w, const Rom* rom, uint16_t page,
                            HudRefreshRegs* hud);

// One pass of the second loop, the same way. `playing` is how many are, when
// it got as far as counting them.
MainLoopNext mainloop_leaving_frame(Wram* w, const Rom* rom, uint16_t page,
                                    HudRefreshRegs* hud, uint16_t* playing);

#endif
