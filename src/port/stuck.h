// A player stuck fast, and shaking to get free.
//
// The player's thread runs one of eight state handlers every frame,
// `$80:D1EA  LDX $70 : JMP ($D1EF,X)`. State `$0C` is `$80:D465`, which
// `$80:DC1E` puts a player in. Of the corpus only the three levels with
// slimes reach it, and what it shows over the player is what a slime's glob
// lands as. This is a frame of it as readable C, from where its opening
// `JSR floor_effect` comes back:
//
//   $80:D468  stuck   a frame of being stuck
//
// ## What being stuck is
//
// **It lasts `$186` frames**, which `$80:DC1E` puts in the countdown at
// `$56`, and nothing the player holds does anything but turn them.
//
// **Shaking gets out sooner.** A frame that holds right when the last held
// left, or left when the last held right, is a shake. The third ends it:
// the countdown is cut to one.
//
// **It hurts.** Whenever the player's hurt timer has run out they are told
// they were hit, and the timer starts again from `$B0`.
//
// **What covers them wobbles**, between two pictures by the walk cycle's
// second bit. It is drawn through the display record that otherwise shows
// the weapon in their hand.
//
// ## What is the ROM's
//
// The frame the countdown reaches zero on, which goes to `$80:DC70` and puts
// the player back as they were. `stuck_supported` says which that is.
//
// ## Its contract with the ROM
//
// It writes WRAM exactly as the ROM does, and leaves A, X, Y, carry, zero and
// negative as the ROM does at the `RTS`.
//
// Port code: libc only.

#ifndef PORT_STUCK_H
#define PORT_STUCK_H

#include <stdbool.h>
#include <stdint.h>

#include "assets/rom.h"
#include "port/wram.h"

// The table of pictures is in bank `$80`, which is the player thread's data
// bank.
#define STUCK_BANK 0x80u

#define STUCK_PC 0x80d468u
#define STUCK_RTS_PC 0x80d4e4u

// Fields on the player's page.
#define STUCK_DP_RECORD 0x08       // the player's display record
#define STUCK_DP_COVER 0x0a        // ...and the one that shows what covers them
#define STUCK_DP_PLAYER 0x0e       // which player, doubled
#define STUCK_DP_POSE_TIMER 0x16
#define STUCK_DP_CYCLE 0x18        // the walk cycle
#define STUCK_DP_BUTTONS 0x1a
#define STUCK_DP_FIRE_A 0x1e       // cleared: nothing fires
#define STUCK_DP_FIRE_B 0x20
#define STUCK_DP_HELD 0x24         // the direction held, doubled
#define STUCK_DP_FACING 0x26       // ...and the last that was one
#define STUCK_DP_EVENT 0x50        // "you were hit"
#define STUCK_DP_HURT_TIMER 0x52   // negative once it has run out
#define STUCK_DP_FRAMES_LEFT 0x56  // the countdown
#define STUCK_DP_SHAKES 0x5a

// For the harness, and only for it: what happened, which is what it takes to
// price the ROM's instructions, and the registers at the `RTS`.
typedef struct {
  bool hurt;           // the hurt timer had run out
  bool held_right;     // right is held...
  bool held_left;      // ...or left
  bool shook;          // ...and the other was, last frame
  bool third_shake;    // the shakes came to three
  bool turned;         // a direction is held
  bool timer_ran;      // the pose timer was not zero
  bool counting;       // the countdown was not zero
  uint16_t a, x, y;
  bool n, z, c;
} StuckLog;

// Is this frame the port's? Not the one the countdown ends on. It only
// looks.
bool stuck_supported(const Wram* w, uint16_t page);

// One frame, for the player whose page is `page`. `log` may be NULL.
void stuck(Wram* w, const Rom* rom, uint16_t page, StuckLog* log);

#endif
