// The two cheats that reach into the port. See `src/cheats.h`, which is where
// all six are described and the only thing that sets these.
//
// Most of a cheat is a byte of the loaded image or a word of WRAM, and the
// port neither knows nor cares. These are the exceptions: routines the port
// has taken over, so that changing the cartridge's copy changes nothing in a
// native run. Both are false unless a frontend says otherwise, so `verify`,
// the corpus and every test see the routines the ROM has.

#ifndef PORT_CHEAT_H
#define PORT_CHEAT_H

#include <stdbool.h>

typedef struct {
  // `player_collide`: id `$0A` does not queue `$80:F9D0`. (Every other way a
  // collision hurts a player asks the recovery timer first, and the frontend
  // holds that.)
  bool invincible;
  // `victim_collide` and `victim_a264_collide`: only the two players and `$FF`
  // are ids a neighbour reacts to.
  bool neighbors;
} PortCheats;

extern PortCheats port_cheats;  // `port/collide.c`

#endif  // PORT_CHEAT_H
