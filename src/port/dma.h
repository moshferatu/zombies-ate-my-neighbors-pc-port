// Sending memory to the picture hardware, and three vblank jobs that do
// little else.
//
//   $80:C872  dma_to_cgram        bytes of colours, from the first colour on
//   $80:C8B8  dma_to_vram         bytes of tiles or of a tilemap, to an address
//   $80:A084  palette_job         the level's 256 colours
//   $80:A09E  background_job      the background's 128, from their second copy
//   $82:D88C  tile_anim_job       the animated tiles that changed this frame
//
// Each fills in DMA channel 0 and starts it. None of them writes the machine:
// like every routine in `port/hw.h`'s scheme it records what it stored, in
// the ROM's order, between the runs of its own instructions, and the harness
// makes the writes on the ROM's cycles.
//
// ## The colours
//
// A level's colours are kept at `$7E:5428`, the background's 128 and then the
// sprites'. The background's are kept a second time at `$7E:5628`, and that
// copy is the one the level's colour animations write, so it is the one shown.
// `palette_job` sends the first table whole; `background_job` sends the copy.
//
// ## The animated tiles
//
// `port/bodies.h`'s `level_tile_anim` steps up to eight tiles through their
// frames and sets a bit for each one that changed. This job is the other
// half. For each bit, last slot first, it does two things:
//
// **It gives the tile its frame's attributes.** A level's tiles each have a
// word that says what they are to walk on, and a tile that animates between
// water and ground has to change that as well as its picture. The word comes
// from the level's own table, through the pointer at `$BE`, and goes into the
// working copy the collision reads, through the pointer at `$BA`.
//
// **It sends the frame's 32 bytes** to the tile's place in VRAM.
//
// The bits are the top eight of a word, shifted out one at a time, so the
// word is zero when the job is done.
//
// ## Its contract with the ROM
//
// WRAM is written as the ROM writes it, and the trace is the ROM's writes in
// its order. The registers each leaves are in the notes on each below.
//
// Port code: libc only.

#ifndef PORT_DMA_H
#define PORT_DMA_H

#include <stdbool.h>
#include <stdint.h>

#include "assets/rom.h"
#include "port/hw.h"
#include "port/wram.h"

#define DMA_TO_CGRAM_PC 0x80c872u
#define DMA_TO_CGRAM_RTL_PC 0x80c891u
#define DMA_TO_VRAM_PC 0x80c8b8u
#define DMA_TO_VRAM_RTL_PC 0x80c8dbu
#define PALETTE_JOB_PC 0x80a084u
#define PALETTE_JOB_RTL_PC 0x80a092u
#define BACKGROUND_JOB_PC 0x80a09eu
#define BACKGROUND_JOB_RTL_PC 0x80a0acu
#define TILE_ANIM_JOB_PC 0x82d88cu
#define TILE_ANIM_JOB_RTL_PC 0x82d8cau

// The colours, as the level has them, and the background's second copy.
#define PALETTE_AT 0x5428u
#define PALETTE_BYTES 0x0200u
#define PALETTE_SHOWN_AT 0x5628u
#define PALETTE_SHOWN_BYTES 0x0100u
#define PALETTE_BANK 0x7eu

// The animated tiles: eight slots, a word each.
#define TILE_ANIM_SLOTS 8
#define TILE_ANIM_BYTES 0x0020u       // a tile's picture
#define W_TILE_ANIM_VRAM 0x6db4u      // where the tile is in VRAM
#define W_TILE_ANIM_SOURCE 0x6dc4u    // where its frame's picture is
#define W_TILE_ANIM_ATTR 0x6dd4u      // the tile's number, doubled
#define W_TILE_ANIM_FRAME_ATTR 0x6de4u  // the frame's, doubled
#define W_TILE_ANIM_CHANGED 0x1f56u   // a bit a slot, the top eight
#define W_TILE_ANIM_SOURCE_BANK 0x1e82u
// The level's table of attributes, a 24-bit pointer on page zero. The working
// copy's is `W_TILE_ATTRS`.
#define W_TILE_ATTRS_LEVEL 0x00beu

// The runs of the ROM's instructions between the writes, for the harness to
// price. A run ends where a store's write begins.
enum {
  DMA_STORE,     // STx abs, up to its write
  DMA_IMM,       // LDA #imm : STA abs
  DMA_TAKEN,     // a branch taken
  DMA_RTL,       // REP #$20 : RTL
  DC_HEAD,       // $C872 REP #$10 : SEP #$20 : STA $4304
  DV_HEAD,       // $C8B8 REP #$30 : STA $4302
  DV_BANK,       // LDA $04,S : SEP #$20 : STA $4304
  PJ_HEAD,       // LDA # : LDX # : LDY # : JSL
  PJ_TAIL,       // CLC : RTL
  TJ_HEAD,       // $D88C SEP #$20 : LDA #$80 : STA $2115
  TJ_START,      // REP #$20 : LDX #$000E
  TJ_TEST,       // ASL $1F56 : BCC
  TJ_SEND,       // $D89D-$D8BF, the level's attributes read from the cartridge
  TJ_SENT,       // PLA : PLX
  TJ_NEXT,       // DEX : DEX : BPL
  TJ_TAIL,       // CLC : RTL
  DMA_BLOCK_COUNT
};

// `$80:C872`. `bytes` from `bank:at` to the colours, starting at the first.
// It leaves 1 in A's low byte and A's high byte, X and Y as they were.
void dma_to_cgram(HwTrace* t, uint8_t bank, uint16_t at, uint16_t bytes);

// `$80:C8B8`. `bytes` from `bank:at` to VRAM at `vram`, a word address. The
// ROM's caller pushes the bank as a word; it leaves 1 in A's low byte and
// that word's high byte in A's.
void dma_to_vram(HwTrace* t, uint8_t bank, uint16_t at, uint16_t vram,
                 uint16_t bytes);

// `$80:A084` and `$80:A09E`. Both run once: carry clear. They leave 1 in A,
// and the table's address and length in X and Y.
void palette_job(HwTrace* t);
void background_job(HwTrace* t);

// Can `tile_anim_job` take this one? Only with the level's attributes in the
// cartridge, where every level has them, and the working copy in WRAM.
bool tile_anim_job_supported(const Wram* w);

// `$82:D88C`. Returns how many tiles it sent. It runs once: carry clear. X
// is left at `$FFFE`; with a tile sent, A is the word at `$1E82` and Y is 32.
int tile_anim_job(Wram* w, const Rom* rom, HwTrace* t);

#endif
