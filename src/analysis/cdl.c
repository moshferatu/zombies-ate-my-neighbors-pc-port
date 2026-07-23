#include "cdl.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool cdl_alloc(Cdl* cdl, uint32_t rom_size) {
  cdl->flags = (uint8_t*)calloc(rom_size, 1);
  cdl->size = rom_size;
  return cdl->flags != NULL;
}

void cdl_free(Cdl* cdl) {
  free(cdl->flags);
  cdl->flags = NULL;
  cdl->size = 0;
}

bool cdl_save(const Cdl* cdl, const char* path) {
  FILE* f = fopen(path, "wb");
  if (!f) return false;
  uint32_t version = 1;
  bool ok = fwrite("ZCDL", 1, 4, f) == 4 &&
            fwrite(&version, 4, 1, f) == 1 &&
            fwrite(&cdl->size, 4, 1, f) == 1 &&
            fwrite(cdl->flags, 1, cdl->size, f) == cdl->size;
  fclose(f);
  return ok;
}

bool cdl_load(Cdl* cdl, const char* path) {
  FILE* f = fopen(path, "rb");
  if (!f) return false;
  char magic[4];
  uint32_t version = 0, size = 0;
  if (fread(magic, 1, 4, f) != 4 || memcmp(magic, "ZCDL", 4) != 0 ||
      fread(&version, 4, 1, f) != 1 || version != 1 ||
      fread(&size, 4, 1, f) != 1 || size == 0 || size > (64u << 20)) {
    fclose(f);
    return false;
  }
  if (!cdl_alloc(cdl, size)) { fclose(f); return false; }
  bool ok = fread(cdl->flags, 1, size, f) == size;
  fclose(f);
  if (!ok) cdl_free(cdl);
  return ok;
}

bool snes_to_rom(uint32_t addr24, uint32_t rom_size, uint32_t* out_offset) {
  uint32_t bank = (addr24 >> 16) & 0xff;
  uint32_t adr = addr24 & 0xffff;
  if (bank == 0x7e || bank == 0x7f) return false;  // WRAM window
  bank &= 0x7f;
  if (adr >= 0x8000 || bank >= 0x40) {
    *out_offset = ((bank << 15) | (adr & 0x7fff)) & (rom_size - 1);
    return true;
  }
  return false;  // banks $00-$3F below $8000: RAM mirror, registers, or open bus
}

uint32_t rom_to_snes(uint32_t offset) {
  return 0x800000 | ((offset >> 15) << 16) | 0x8000 | (offset & 0x7fff);
}

bool snes_to_wram(uint32_t addr24, uint32_t* out_offset) {
  uint32_t bank = (addr24 >> 16) & 0xff;
  uint32_t adr = addr24 & 0xffff;
  if (bank == 0x7e) { *out_offset = adr; return true; }
  if (bank == 0x7f) { *out_offset = 0x10000 | adr; return true; }
  if (adr < 0x2000 && (bank < 0x40 || (bank >= 0x80 && bank < 0xc0))) {
    *out_offset = adr;
    return true;
  }
  return false;
}
