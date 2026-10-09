// The game over's blood, red again.
//
// The Super NES game's game over is "GAME OVER" cut out of a curtain of
// *purple* slime that runs down the screen; on the Mega Drive it is red, and
// it is blood. `--red-blood` makes it red here, which is what the ROM hack
// *Bloody Disgusting Edition* (romhacking.net, hack 4306) is for. Nothing of
// that patch is used, or was seen: only its description was to hand.
//
// The curtain is two things (see `widescreen.h` on the mask):
//
//   * **The mask, on BG3.** Its three colours are no palette's: `$80:8B82`,
//     a vblank job `$80:8A78` installs, writes them to CGRAM 25-27 every
//     frame as six immediates -- `$5953` the highlight, `$348A` the field,
//     `$1C26` the outline. `blood_patch_rom` rewrites the six operands in the
//     loaded image (never the file), having checked that each `LDA #` and
//     each operand is what this ROM has.
//   * **The drips, which are sprites**: one metasprite, `$8F:E9A7`, nine
//     pieces of frames `$A63`-`$A65` in sprite palette 5, which use three of
//     its sixteen colours -- 9, 11 and 12, the same three words as the mask.
//     Those three are the purple of other things too -- 183 of the 209
//     frames a metasprite draws in palette 5 use one of them, `$B31`-`$B34`
//     being a spider, and the hack's notes say an earlier version that
//     changed the palette turned the monster the potion makes of a player red
//     as well -- so the palette cannot simply be reddened: a spider seen
//     through the letters would turn red with the curtain. Nor is there red
//     enough in palette 5 to redraw the drips with, which is what editing
//     their tiles comes to (`$189D` and `$2072`, a pillar-box red and a
//     raspberry).
//
// So the drips are recoloured a sprite at a time, in the renderer.
// `blood_frame`, from the frame hook, finds the OAM entries that are drips --
// palette 5, and a tile that the frame cache (`W_FRAME_SLOT`) says holds one
// of the three frames -- and marks them in `VideoPicture.remap_on`; for a marked
// entry `VideoPicture.remap` sends pixel 9, 11 and 12 to CGRAM `$C0`, `$D0` and
// `$E0` instead, and `blood_frame` puts the reds there. Those three are
// colour 0 of sprite palettes 4, 5 and 6, which is transparent: no pixel of
// the picture ever reads them, the game rewrites them with every palette it
// uploads and nothing is lost. (4-6 and not 0-2, because colour maths applies
// to sprites of palettes 4-7 and the drips are of 5.) The reds are made from
// the colours palette 5 holds at that moment, so a palette the game had
// dimmed would dim them too.
//
// The red itself: the larger of red and blue becomes the red, scaled up by
// 13/8, and the green is halved and shared with the blue -- `$348A` (10,4,13)
// is (21,2,2), `$5953` (31,5,6), `$1C26` (11,0,0). The mask's immediates and
// the drips' colours go through the same function, and have to: a drip's
// trunk is the field's colour and hangs from it without a seam.
//
// Nothing here is the game's state but three CGRAM words nobody reads, so a
// movie plays the same with it and without.
#ifndef ZAMN_BLOOD_H
#define ZAMN_BLOOD_H

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "cart.h"
#include "ppu.h"
#include "snes.h"
#include "video/ppu_hook.h"

#include "port/wram.h"

// `$80:8B82`'s six `LDA #imm8`, as offsets into the image: the opcode, then
// the operand. Low byte then high byte of CGRAM 25, 26 and 27.
#define BLOOD_MASK_JOB 0x0b89u
#define BLOOD_MASK_COLOURS 3
static const uint16_t blood_mask_stock[BLOOD_MASK_COLOURS] = {0x5953, 0x348a, 0x1c26};

// The drips: frames `$A63` (the drop), `$A64` and `$A65` (the trunk), drawn
// in sprite palette 5 with pixels 9 (highlight), 11 (field) and 12 (outline).
#define BLOOD_DRIP_FRAME_FIRST 0x0a63
#define BLOOD_DRIP_FRAME_LAST 0x0a65
#define BLOOD_DRIP_PALETTE 5
static const uint8_t blood_drip_pixels[BLOOD_MASK_COLOURS] = {9, 11, 12};
static const uint8_t blood_drip_cgram[BLOOD_MASK_COLOURS] = {0xc0, 0xd0, 0xe0};

static inline uint16_t blood_red(uint16_t c) {
  const int r = c & 0x1f, g = (c >> 5) & 0x1f, b = (c >> 10) & 0x1f;
  const int most = r > b ? r : b;
  int red = (most * 13 + 4) / 8;
  if (red > 31) red = 31;
  return (uint16_t)(red | ((g / 2) << 5) | ((g * 5 / 8) << 10));
}

// The mask's colours, in the loaded image. False, and nothing written, for an
// image whose job is not the one described above.
static inline bool blood_patch_rom(uint8_t* rom, size_t size) {
  if (!rom || size < BLOOD_MASK_JOB + 5u * 2 * BLOOD_MASK_COLOURS) return false;
  for (int i = 0; i < BLOOD_MASK_COLOURS; i++) {
    const uint8_t* lo = rom + BLOOD_MASK_JOB + 10u * (unsigned)i;
    const uint8_t* hi = lo + 5;
    if (lo[0] != 0xa9 || hi[0] != 0xa9) return false;
    if ((uint16_t)(lo[1] | (hi[1] << 8)) != blood_mask_stock[i]) return false;
  }
  for (int i = 0; i < BLOOD_MASK_COLOURS; i++) {
    const uint16_t red = blood_red(blood_mask_stock[i]);
    rom[BLOOD_MASK_JOB + 10u * (unsigned)i + 1] = (uint8_t)red;
    rom[BLOOD_MASK_JOB + 10u * (unsigned)i + 6] = (uint8_t)(red >> 8);
  }
  return true;
}

typedef struct {
  bool on;
  bool marked;       // the last frame marked something, so there is something to clear
  long frames;       // frames with a drip on them, for the test to print
} Blood;

// The OAM tile number a frame is loaded at -- nine bits, bit 8 being the
// attribute word's `$0100` -- or -1 if the cache does not hold it. The table
// of `$80:B647`: slot `s` is tile `(s / 8) * 32 + (s % 8) * 2`.
static inline int blood_frame_tile(const uint8_t* ram, int frame) {
  const uint16_t entry = (uint16_t)(ram[W_FRAME_SLOT + frame * 2] | (ram[W_FRAME_SLOT + frame * 2 + 1] << 8));
  if (entry & 0x8000) return -1;
  const int slot = entry / 2;
  if (slot >= 128) return -1;
  return (slot / 8) * 32 + (slot % 8) * 2;
}

// From the frame hook, after the game's vblank and before the first line.
static inline void blood_frame(Snes* snes, Blood* blood) {
  Ppu* ppu = snes->ppu;
  // The palette is the chip's memory and not its registers: written here.
  const VideoRegisters* reg = video_registers_of(ppu);
  if (!blood->on) return;
  if (blood->marked) {
    for (int s = 0; s < 0x80; s++) video_set_sprite_remapped(ppu, s, false);
    blood->marked = false;
  }
  int tiles[BLOOD_DRIP_FRAME_LAST - BLOOD_DRIP_FRAME_FIRST + 1];
  bool any = false;
  for (int f = BLOOD_DRIP_FRAME_FIRST; f <= BLOOD_DRIP_FRAME_LAST; f++) {
    tiles[f - BLOOD_DRIP_FRAME_FIRST] = blood_frame_tile(snes->ram, f);
    any = any || tiles[f - BLOOD_DRIP_FRAME_FIRST] >= 0;
  }
  if (!any) return;
  for (int s = 0; s < 0x80; s++) {
    const uint16_t attr = reg->oam[s * 2 + 1];
    if (((attr >> 9) & 7) != BLOOD_DRIP_PALETTE) continue;
    if ((reg->oam[s * 2] >> 8) >= 224 && (reg->oam[s * 2] >> 8) < 240) continue;  // parked
    const int tile = attr & 0x1ff;
    for (int k = 0; k <= BLOOD_DRIP_FRAME_LAST - BLOOD_DRIP_FRAME_FIRST; k++)
      if (tiles[k] == tile) {
        video_set_sprite_remapped(ppu, s, true);
        blood->marked = true;
      }
  }
  if (!blood->marked) return;
  blood->frames++;
  uint8_t remap[16] = {0};
  for (int i = 0; i < BLOOD_MASK_COLOURS; i++) {
    remap[blood_drip_pixels[i]] = blood_drip_cgram[i];
    reg->cgram[blood_drip_cgram[i]] =
        blood_red(reg->cgram[0x80 + 16 * BLOOD_DRIP_PALETTE + blood_drip_pixels[i]]);
  }
  video_set_remap(ppu, remap);
}

#endif
