// The text layer's tile map.
//
// The words on a screen are tiles on a layer of their own, and the game
// keeps that layer's map in WRAM and copies it to the PPU when it changes:
// 32 rows of 32 tiles, a word each, at `$7E:6502`. A screen that is about to
// write on it blanks it first:
//
//   $82:AD44  text_map_clear   every tile of it to zero
//
// ## Its contract with the ROM
//
// The ROM stores one zero word and then copies the map onto itself a byte
// along, with `MVN`, which leaves the zero in every byte. The registers come
// back as the copy leaves them: A at `$FFFF`, X on the map's last byte and Y
// one past it. The data bank is put back.
//
// Port code: libc only.

#ifndef PORT_TEXTMAP_H
#define PORT_TEXTMAP_H

#include <stdint.h>

#include "port/wram.h"

#define TEXT_MAP_CLEAR_PC 0x82ad44u

#define W_TEXT_MAP 0x6502u
#define TEXT_MAP_BYTES 0x0800u  // 32 rows of 32 words

void text_map_clear(Wram* w);

#endif
