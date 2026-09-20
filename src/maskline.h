// The line along the bottom of the screen as the game over begins.
//
// The game over's mask is a second tilemap for BG3, scrolled down over the
// level (`$80:8A00`; see `widescreen.h`). `$80:8A78` sets it up a vblank job
// at a time, one `WAI` between each:
//
//     $80:8A87  LDA #$8B17   the mask's map, to video memory at $4800
//     $80:8A92  LDA #$8B2B   BG3's map register to $4800, and the tiles
//     $80:8A9D  LDA #$8B48   the rest of the map
//     $80:8AA8  LDA #$8B70   the job that writes BG3's scroll from `$136A`,
//                            every frame from here on; `$136A` is -32
//     $80:8AB3  LDA #$8B82   the mask's three colours
//
// So for two frames BG3 is the mask's map with the *panel's* scroll, which is
// 0. The mask is rows 28 to 55 of its map and nothing above, and at scroll 0
// the screen's 224 lines end one line into row 28: the top edge of the mask's
// field, all the way across the last line of the picture, in the status
// panel's colours (its `$0078`, an orange). It is seen on the second of the
// two frames, row 28 being where "the rest of the map" starts (`$4B80`). Then
// the scroll job moves the map 32 lines up and out of sight, and the mask
// comes down from the top as it is meant to. The console draws that line too; a
// television of 1993 had it in its overscan, and a picture that shows all 224
// lines has a flash along its bottom edge.
//
// `maskline_fix` exchanges the operands of the second and the fourth `LDA`, in
// the cartridge's copy of the image: the scroll job is running before BG3 is
// given the mask's map, so the map is never seen at the panel's scroll. It
// costs nothing to have the scroll early, because there is nothing to scroll:
// by then the panel's own map is blank from corner to corner (every word of it
// tile 0, three frames before the change). The same five jobs run on the same
// five frames, so nothing about the game's timing moves.
#ifndef ZAMN_MASKLINE_H
#define ZAMN_MASKLINE_H

#include <stdbool.h>
#include <stdint.h>

#include "cart.h"

#define MASKLINE_LDA_MAP 0x0a92u     // $80:8A92  LDA #$8B2B
#define MASKLINE_LDA_SCROLL 0x0aa8u  // $80:8AA8  LDA #$8B70

// False, and nothing written, for an image whose two `LDA`s are not those.
static inline bool maskline_fix(Cart* cart) {
  if (!cart || !cart->rom || cart->romSize < MASKLINE_LDA_SCROLL + 3) return false;
  uint8_t* a = cart->rom + MASKLINE_LDA_MAP;
  uint8_t* b = cart->rom + MASKLINE_LDA_SCROLL;
  if (a[0] != 0xa9 || b[0] != 0xa9 || a[2] != 0x8b || b[2] != 0x8b) return false;
  if (a[1] == 0x70 && b[1] == 0x2b) return true;  // done already
  if (a[1] != 0x2b || b[1] != 0x70) return false;
  a[1] = 0x70;
  b[1] = 0x2b;
  return true;
}

#endif
