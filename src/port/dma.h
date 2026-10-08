// Sending memory to the picture hardware, and six vblank jobs that do
// little else.
//
//   $80:C872  dma_to_cgram        bytes of colours, from the first colour on
//   $80:C8B8  dma_to_vram         bytes of tiles or of a tilemap, to an address
//   $80:A084  palette_job         the level's 256 colours
//   $80:A09E  background_job      the background's 128, from their second copy
//   $82:D88C  tile_anim_job       the animated tiles that changed this frame
//   $80:C34A  hud_upload_job      the HUD's four rows, from their shadow
//   $82:8308  colours_112_job     sixteen colours, from the 112th on
//   $80:9F62  vram_clear_job      a kilobyte of VRAM zeroed, a vblank at a time
//   $82:B82E  text_map_job        the words a screen has printed, two rows
//   $82:B9B6                      ...of sixteen: the same job again
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
#define HUD_UPLOAD_JOB_PC 0x80c34au
#define HUD_UPLOAD_JOB_RTL_PC 0x80c378u
#define COLOURS_112_JOB_PC 0x828308u
#define COLOURS_112_JOB_RTL_PC 0x828336u
#define VRAM_CLEAR_JOB_PC 0x809f62u
#define VRAM_CLEAR_JOB_RTL_PC 0x809f98u  // of two: the other is at `$9F9A`
#define COLOURS_112_ASK_PC 0x828163u
#define COLOURS_112_ASK_RTL_PC 0x82816eu
#define VRAM_WIPE_PC 0x809fb0u
#define VRAM_WIPE_RTL_PC 0x809fdcu
#define COLOURS_JOB_PC 0x809fdfu
#define COLOURS_JOB_RTL_PC 0x80a036u
#define TEXT_MAP_JOB_PC 0x82b82eu
#define TEXT_MAP_JOB_RTL_PC 0x82b849u
#define TEXT_LINES_JOB_PC 0x82b9b6u   // the same instructions
#define TEXT_LINES_JOB_RTL_PC 0x82b9d1u

// The HUD's shadow, four rows of 32 words less 32, and where it goes.
#define HUD_SHADOW_AT 0x5f36u
#define HUD_SHADOW_BYTES 0x00c0u
#define HUD_VRAM_AT 0x6440u
// The sixteen colours `$82:8308` sends, in the level's table, and the colour
// they start at.
#define COLOURS_112_AT 0x5508u
#define COLOURS_112_BYTES 0x0020u
#define COLOURS_112_FIRST 0x70u
// `vram_clear_job`: the word address it has reached, a zero word in the
// cartridge to send over and over, and how much it sends a vblank.
#define W_VRAM_CLEAR_AT 0x00c8u
#define VRAM_CLEAR_SOURCE 0x9f9bu
#define VRAM_CLEAR_SOURCE_BANK 0x80u
#define VRAM_CLEAR_BYTES 0x0800u
#define VRAM_CLEAR_WORDS 0x0400u
// `vram_wipe`: from where in VRAM, how much, and a zero word of its own.
#define VRAM_WIPE_AT 0x2000u
#define VRAM_WIPE_BYTES 0xc000u
#define VRAM_WIPE_SOURCE 0x9fddu
// `colours_job`: the first 112 colours, and the 128 of the sprites'.
#define COLOURS_LOW_BYTES 0x00e0u
#define COLOURS_HIGH_FIRST 0x80u
#define COLOURS_HIGH_BYTES 0x0100u

// `text_map_job`: the map `port/text.h` prints into, how much of it goes,
// the layer's map in VRAM, and which kilobyte of that the text is on.
#define TEXT_MAP_JOB_AT 0x6502u       // `TEXT_MAP_AT`
#define TEXT_MAP_JOB_BYTES 0x0800u
#define TEXT_MAP_JOB_VRAM 0x6800u
#define W_TEXT_MAP_JOB_PAGE 0x1e8eu   // `W_TEXT_JOB_BYTE`

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
  DMA_IMM16,     // LDA #imm16 : STA abs
  DMA_SEP_IMM,   // SEP #$20 : LDA #imm : STA abs
  DMA_REP_IMM16, // REP #$30 : LDA #imm16 : STA abs
  DMA_REP_RTL,   // REP #$20 : CLC : RTL
  DMA_FLAG_RTL,  // SEC or CLC, and RTL
  HJ_TAIL,       // REP #$30 : REP #$31 : RTL
  VC_HEAD,       // LDA $C8 : STA $2116
  VC_STEP,       // CLC : ADC #$0400 : STA $C8 : LDA #$1809 : STA $4300
  VC_TEST,       // REP #$20 : BIT $C8 : BMI
  TMJ_HEAD,      // $B82E-$B846, as far as the `JSL`'s own cycles
  TMJ_TAIL,      // PLA : CLC : RTL
  TPJ_HEAD,      // $80:AC55 LDA $ED : BNE
  TPJ_COUNT,     // REP #$30 : LDX $D0 : BEQ
  TPJ_FIRST,     // DEX : DEX : LDA $1C92,X : STA $2116
  TPJ_LOAD,      // LDA abs,X : STA abs
  TPJ_NEXT,      // DEX : DEX : BPL
  TPJ_TAIL,      // STZ $D0 : CLC : RTL
  LR_HEAD,       // $80:88A9 six STZ : SEP #$30 : LDA #$00 : STA $210D
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

// `$80:C34A`, which `hud_refresh` queues when the HUD has changed
// (`port/hud.h`). It runs once: carry clear. A is left `$6401`, the high byte
// of the VRAM address over the 1 that started the transfer.
void hud_upload_job(HwTrace* t);

// `$82:8308`, which `$82:8163` queues: the level's colours 112 to 127 again,
// which is the eighth of the background's palettes. It runs once: carry
// clear, and 1 in A.
void colours_112_job(HwTrace* t);

// `$82:8163`, a job of the other queue, the one that runs after the
// picture: it puts `colours_112_job` on the vblank's. It runs once: carry
// clear. Returns the slot `vbl_queue_a_add` gave it, or -1 with that queue
// full.
int colours_112_ask(Wram* w);

// `$80:9F62`, which `$80:9F9D` queues and waits on: a kilobyte of VRAM zeroed
// from the word address at `$C8`, which it moves on. It stays queued, carry
// set, until that address reaches `$8000`: thirty-two vblanks from zero for
// the whole of VRAM. Returns the address it left. A is left `$0801`.
uint16_t vram_clear_job(Wram* w, HwTrace* t);

// `$80:9FB0`, which the screens between levels call once `vram_clear_job`
// is through: VRAM from the word address `$2000` up zeroed in one transfer,
// which is three quarters of it. A is left `$C001`. Carry is not touched.
void vram_wipe(HwTrace* t);

// `$80:9FDF`, a job of the vblank's: the level's colours sent in two
// transfers, the first 112 and then the sprites' 128. Colours 112 to 127 are
// `colours_112_job`'s to send. It runs once: carry clear. A is left `$0101`.
void colours_job(HwTrace* t);

// `$82:B82E`, which a screen in bank `$82` queues when it has printed: the
// first kilobyte of the text's map sent to the layer's map in VRAM, at the
// kilobyte `$1E8E` says. That is two rows of the thirty-two a map has for
// each of its sixteen lines of text. `$82:B9B6` is the same instructions,
// queued by the printer of several lines. It runs once: carry clear. A is
// left `$007E`, and the address in VRAM, which it returns, in X.
uint16_t text_map_job(const Wram* w, HwTrace* t);

#endif
