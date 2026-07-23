// Code/Data Log: one flag byte per ROM byte, recording what the CPU actually
// did with that byte during a traced run. This is the backbone of Phase 1 —
// it separates executable code from data tables, which is what makes an
// automated disassembly trustworthy.
//
// File format (little-endian):
//   char  magic[4]  = "ZCDL"
//   u32   version   = 1
//   u32   rom_size
//   u8    flags[rom_size]

#ifndef CDL_H
#define CDL_H

#include <stdbool.h>
#include <stdint.h>

enum {
  CDL_CODE     = 0x01,  // first byte of an executed instruction
  CDL_OPERAND  = 0x02,  // operand byte of an executed instruction
  CDL_DATA     = 0x04,  // read as data by the CPU or sourced by a DMA
  CDL_SUB      = 0x08,  // target of a JSR/JSL
  CDL_JUMP     = 0x10,  // target of a JMP/JML/branch
  CDL_M16      = 0x20,  // executed at least once with M=0 (16-bit accumulator)
  CDL_X16      = 0x40,  // executed at least once with X=0 (16-bit index)
  CDL_CONFLICT = 0x80,  // executed with *both* widths — disassembly is ambiguous
};

typedef struct {
  uint8_t* flags;
  uint32_t size;
} Cdl;

bool cdl_alloc(Cdl* cdl, uint32_t rom_size);
void cdl_free(Cdl* cdl);
bool cdl_save(const Cdl* cdl, const char* path);
bool cdl_load(Cdl* cdl, const char* path);

// LoROM address translation. `rom_size` must be a power of two, as the core's
// cart mapping assumes.
//
// snes_to_rom: 24-bit SNES address -> ROM offset. False if the address does
// not map to cartridge ROM (RAM, registers, or an unmapped hole).
bool snes_to_rom(uint32_t addr24, uint32_t rom_size, uint32_t* out_offset);

// rom_to_snes: ROM offset -> the canonical SNES address for it.
//
// ZAMN enables FastROM ($420D bit 0) during init and immediately does
// `JML $8080B6`, so every byte it executes is fetched through the $80-$BF
// mirror. Reporting addresses in that mirror makes tool output line up with
// the trace log and with the ROM's own JSL/JML operands.
uint32_t rom_to_snes(uint32_t offset);

// WRAM address translation: 24-bit SNES address -> 0..0x1FFFF WRAM offset,
// covering both the $7E/$7F windows and the $0000-$1FFF low-bank mirror.
bool snes_to_wram(uint32_t addr24, uint32_t* out_offset);

#endif
