// Booting to the title without the logos: the game's own way of doing it.
//
// The main thread (`$80:84B1`) is a loop -- set up, the opening, a game, the
// game over, and round again -- and the opening (`$80:9126`) shows the logos
// only the first time round:
//
//     $80:9136  A5 7C      LDA $7C        ; been here before?
//     $80:9138  D0 18      BNE $9152      ; then straight to the title
//     $80:913A  E6 7C      INC $7C
//     $80:913C  ...                       ; the region check, Konami,
//                                         ; LucasArts, the story screen
//     $80:9152  ...                       ; the title
//
// which is how a game over comes back to the title and not to Konami. The
// five calls between are presentation and nothing else: each decompresses its
// own pictures into video memory, fades up, waits and fades out, and the
// title sets up everything it uses for itself -- it has to, since the other
// way into it is from a level.
//
// `intro_bypass` makes the `BNE` a `BRA` in the cartridge's copy of the image
// (never the file), so the first time round is like every other. What is left
// of the boot is what the game does with the screen off before any picture:
// clearing memory (17 frames), sending the sound driver to the audio
// processor (64), and two more uploads of music and samples (45 and 89) --
// 222 frames to the first lit frame of the title, where it was 1,050 with
// Start mashed through the logos.
//
// The branch stays patched. The flag it tests is then never set, and if the
// byte were put back the logos would play after the first game over.
#ifndef ZAMN_SKIPINTRO_H
#define ZAMN_SKIPINTRO_H

#include <stdbool.h>
#include <stdint.h>

#include "cart.h"

// `$80:9136` as an offset into the image: `A5 7C D0 18`.
#define INTRO_FLAG_TEST 0x1136u

// False, and nothing written, for an image whose `$80:9136` is not that test.
static inline bool intro_bypass(Cart* cart) {
  if (!cart || !cart->rom || cart->romSize < INTRO_FLAG_TEST + 4) return false;
  uint8_t* at = cart->rom + INTRO_FLAG_TEST;
  if (at[0] != 0xa5 || at[1] != 0x7c || (at[2] != 0xd0 && at[2] != 0x80) || at[3] != 0x18) return false;
  at[2] = 0x80;  // BRA
  return true;
}

#endif
