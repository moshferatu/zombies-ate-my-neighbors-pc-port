// The cartridge, for `zamn`: the ROM where the console's bus finds it.
//
// It is what LakeSnes's `cart.h` declares. The game's cartridge is LoROM,
// a megabyte with no RAM and no chip, and that is the mapping here: each bank
// has 32 KB of ROM at $8000-$FFFF, mirrored at $80-$FF, and banks $40-$7D
// and $C0-$FF have it at $0000-$7FFF too. Battery RAM, which this game has
// none of, would be at $0000-$7FFF in banks $70-$7D and $F0-$FF. A ROM that
// is not a LoROM for NTSC is not this game's, and is refused as it is loaded.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cart.h"
#include "snes.h"

enum { CART_NONE = 0, CART_LOROM = 1 };

Cart* cart_init(Snes* snes) {
  Cart* cart = calloc(1, sizeof(Cart));
  if (cart) cart->snes = snes;
  return cart;
}

void cart_free(Cart* cart) {
  free(cart->rom);
  free(cart->ram);
  free(cart);
}

// The RAM keeps what it has: it would be battery-backed.
void cart_reset(Cart* cart) { (void)cart; }

// The kind and sizes, saved; or, loading, whether a state's match.
bool cart_handleTypeState(Cart* cart, StateHandler* sh) {
  uint8_t type = cart->type;
  uint32_t rom_size = cart->romSize;
  uint32_t ram_size = cart->ramSize;
  sh_handleBytes(sh, &type, NULL);
  sh_handleInts(sh, &rom_size, &ram_size, NULL);
  return type == cart->type && rom_size == cart->romSize &&
         ram_size == cart->ramSize;
}

void cart_handleState(Cart* cart, StateHandler* sh) {
  if (cart->ram) sh_handleByteArray(sh, cart->ram, (int)cart->ramSize);
}

void cart_load(Cart* cart, int type, uint8_t* rom, int romSize, int ramSize) {
  free(cart->rom);
  free(cart->ram);
  cart->type = (uint8_t)type;
  cart->rom = malloc((size_t)romSize);
  cart->romSize = (uint32_t)romSize;
  if (cart->rom) memcpy(cart->rom, rom, (size_t)romSize);
  cart->ram = ramSize > 0 ? calloc(1, (size_t)ramSize) : NULL;
  cart->ramSize = (uint32_t)ramSize;
}

bool cart_handleBattery(Cart* cart, bool save, uint8_t* data, int* size) {
  if (save) {
    *size = (int)cart->ramSize;
    if (data && cart->ram) memcpy(data, cart->ram, cart->ramSize);
    return true;
  }
  if (*size != (int)cart->ramSize) return false;
  if (cart->ram) memcpy(cart->ram, data, cart->ramSize);
  return true;
}

// Where battery RAM would be, by LoROM's rule, or -1.
static long ram_at(const Cart* cart, uint8_t bank, uint16_t adr) {
  const bool ram_bank = (bank >= 0x70 && bank < 0x7e) || bank >= 0xf0;
  if (!ram_bank || adr >= 0x8000 || cart->ramSize == 0) return -1;
  return (long)((((uint32_t)(bank & 0xf) << 15) | adr) & (cart->ramSize - 1));
}

uint8_t cart_read(Cart* cart, uint8_t bank, uint16_t adr) {
  if (cart->type != CART_LOROM) return cart->snes->openBus;
  const long ram = ram_at(cart, bank, adr);
  if (ram >= 0) return cart->ram[ram];
  bank &= 0x7f;
  if (adr >= 0x8000 || bank >= 0x40)
    return cart->rom[(((uint32_t)bank << 15) | (adr & 0x7fff)) &
                     (cart->romSize - 1)];
  return cart->snes->openBus;
}

void cart_write(Cart* cart, uint8_t bank, uint16_t adr, uint8_t val) {
  if (cart->type != CART_LOROM) return;
  const long ram = ram_at(cart, bank, adr);
  if (ram >= 0) cart->ram[ram] = val;
}

// The header of a LoROM cartridge, at the end of its first 32 KB.
enum {
  HEADER_AT = 0x7fc0,
  HEADER_CHIPS = 0x16,    // low nibble: chips, and RAM if any
  HEADER_RAM_SIZE = 0x18, // 1 KB << this
  HEADER_REGION = 0x19,
  HEADER_RESET = 0x3c,
  COPIER_HEADER = 0x200,
};

// The cartridge into the console, which is then reset. A copier's header
// of 512 bytes is skipped. A ROM is made a power of two long by mirroring its
// last part, as a cartridge's address lines do. It must be a LoROM for NTSC,
// as this game's is.
bool snes_loadRom(Snes* snes, const uint8_t* data, int length) {
  if ((length & 0x7fff) == COPIER_HEADER) {
    data += COPIER_HEADER;
    length -= COPIER_HEADER;
  }
  if (length < 0x8000) {
    printf("Failed to load rom: rom too small (%d bytes)\n", length);
    return false;
  }
  const uint8_t* h = data + HEADER_AT;
  const uint16_t reset = (uint16_t)(h[HEADER_RESET] | h[HEADER_RESET + 1] << 8);
  if (reset < 0x8000) {
    printf("Failed to load rom: not a LoROM cartridge\n");
    return false;
  }
  const uint8_t region = h[HEADER_REGION];
  if ((region >= 0x2 && region <= 0xc) || region == 0x11) {
    printf("Failed to load rom: a cartridge for PAL; this game is the NTSC one\n");
    return false;
  }
  int size = 0x8000;
  while (size < length) size *= 2;
  uint8_t* rom = malloc((size_t)size);
  if (!rom) return false;
  memcpy(rom, data, (size_t)length);
  for (int part = 1, end = length; end != size; part *= 2) {
    if (end & part) {
      memcpy(rom + end, rom + end - part, (size_t)part);
      end += part;
    }
  }
  const int ram = (h[HEADER_CHIPS] & 0xf) > 0 ? 0x400 << h[HEADER_RAM_SIZE] : 0;
  cart_load(snes->cart, CART_LOROM, rom, size, ram);
  free(rom);
  snes_reset(snes, true);
  snes->palTiming = false;
  return true;
}
