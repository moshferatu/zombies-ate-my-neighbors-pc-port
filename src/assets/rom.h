// LoROM address mapping.
//
// The port loads every asset out of the user's ROM at runtime, using the same
// SNES addresses the game's own code uses, so it needs the cartridge's address
// map. ZAMN is plain LoROM with no coprocessor: bank `$xx`, address
// `$8000-$FFFF` is ROM offset `(xx & $7F) * $8000 + (addr - $8000)`. All of the
// game's code runs from the `$80-$BF` FastROM mirror, so that is how addresses
// are written throughout this codebase.
//
// Port code: libc only.

#ifndef ASSETS_ROM_H
#define ASSETS_ROM_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
  const uint8_t* data;
  uint32_t size;
} Rom;

// A pointer to `addr24`, plus how many bytes can be read from it before the
// 65816 would wrap to `$0000` of the same bank. The game's pointers are 16-bit
// increments inside one bank, so that wrap — not the end of the ROM file — is
// the real limit on a run of data.
//
// Returns NULL if `addr24` is not a cartridge address (the low half of a
// LoROM bank is WRAM and hardware registers, not ROM) or lies past the end of
// this ROM image.
const uint8_t* rom_ptr(const Rom* rom, uint32_t addr24, uint32_t* out_avail);

// True if `len` bytes can be read at `addr24` without crossing the bank wrap.
bool rom_has(const Rom* rom, uint32_t addr24, uint32_t len);

// The little-endian word at `addr24`, or 0 if it is not readable.
uint16_t rom_word(const Rom* rom, uint32_t addr24);

#endif
