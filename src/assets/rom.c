#include "assets/rom.h"

#include <stddef.h>

const uint8_t* rom_ptr(const Rom* rom, uint32_t addr24, uint32_t* out_avail) {
  uint32_t bank = (addr24 >> 16) & 0xff;
  uint32_t addr = addr24 & 0xffff;
  if (addr < 0x8000) return NULL;  // low half of a LoROM bank is not cartridge

  uint32_t off = (bank & 0x7f) * 0x8000u + (addr - 0x8000u);
  if (off >= rom->size) return NULL;

  uint32_t avail = 0x10000u - addr;  // to the bank wrap
  if (avail > rom->size - off) avail = rom->size - off;
  if (out_avail) *out_avail = avail;
  return rom->data + off;
}

bool rom_has(const Rom* rom, uint32_t addr24, uint32_t len) {
  uint32_t avail = 0;
  return rom_ptr(rom, addr24, &avail) != NULL && avail >= len;
}

uint16_t rom_word(const Rom* rom, uint32_t addr24) {
  uint32_t avail = 0;
  const uint8_t* p = rom_ptr(rom, addr24, &avail);
  if (!p || avail < 2) return 0;
  return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}
