// The pause check, `$80:89B0`, which the level's main loop calls every frame.
//
// With Start held on either pad the ROM pauses there and then: it turns the
// volume and the screen down and waits, in the routine itself, for Start to be
// let go, pressed again and let go again. The three waits are `port/hold.h`'s.
// What is round them is the ROM's still.
//
// On every other frame it does nothing. What is left of it is a compare of
// pad one against Select alone whose branch goes to the next instruction
// either way, so the only trace is in the registers: pad one in A, and the
// compare's flags.
//
// Port code: libc only.

#ifndef PORT_PAUSE_H
#define PORT_PAUSE_H

#include <stdbool.h>
#include <stdint.h>

#include "port/wram.h"

#define PAUSE_CHECK_PC 0x8089b0u
#define PAUSE_CHECK_RTL_PC 0x8089ffu

#define PAUSE_PAD_START 0x1000u
#define PAUSE_PAD_SELECT 0x2000u  // what the dead compare is against

// Is Start held on either pad, so that this frame pauses the game?
bool pause_wanted(const Wram* w);

// Pad one as read this frame, which the check leaves in A.
uint16_t pause_pad_one(const Wram* w);

#endif
