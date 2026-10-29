// The port's sound in a LakeSnes console, in place of its APU: the console
// catches it up and puts the game's port accesses through it, takes the
// frame's audio from it, and saves it in a state. (`src/sound` itself knows
// nothing of LakeSnes; this is the one place the two meet.)
//
// A state saved with one cannot be loaded into a console with the other: the
// sound's part of it is laid out differently, and so is a different size.

#ifndef SOUND_CONSOLE_H
#define SOUND_CONSOLE_H

#include "snes.h"
#include "sound/player.h"
#include "sound/sound.h"

// From here on `snes` makes its sound with `s`, which is reset, and whose
// program is `lead`, a player with the console's settings (`player_lead`).
// Both must outlive it (or be detached with `snes_setSound(snes, NULL)`).
// A state keeps both.
void sound_attach(Snes* snes, Sound* s, Player* lead);

#endif
