// Vblank jobs: the routines that write the PPU while the picture is not being
// drawn.
//
// Their work is register writes, so they are ported as traces (`port/hw.h`):
// each records the runs of its own instructions and every byte it stores to a
// register, and the harness prices the runs and makes the writes on the ROM's
// cycles. Everything else about them is ordinary: they read WRAM, write a word
// or two of it, and return with the registers as the ROM leaves them.
//
// Three are queue A jobs, reached by the dispatcher's `RTL` and returning to
// `$80:8401` with carry set to stay queued. `vram_queue_flush` is called by the
// NMI itself.
//
// Port code: libc only.

#ifndef PORT_VBLANK_H
#define PORT_VBLANK_H

#include <stdint.h>

#include "port/cpu.h"
#include "port/hw.h"
#include "port/wram.h"

// The runs, for the harness to price. Each ends where a store's write begins,
// so a store's own opcode and operand fetches are in the run before its write.
enum {
  // Shared by both queue walks.
  VBL_STORE,    // one absolute store's three fetches
  VBL_TAKEN,    // a taken branch's extra cycle
  VBL_LOAD,     // LDA table,X : STA reg
  VBL_GO,       // SEP #$20 : LDA #$01 : STA $420B
  VBL_NEXT,     // REP #$20 : INX : INX : CPX dp : BNE

  // $80:9E7B vram_queue_flush
  VQ_HEAD,      // BIT $26 : BVS, not taken
  VQ_BUSY,      // ...taken, SEC : RTL
  VQ_EMPTY,     // LDA $CE : BEQ taken, SEC : CLC : RTL
  VQ_START,     // LDA $CE : BEQ : LDA #$1801 : STA $4300
  VQ_FIRST,     // LDX #$0000
  VQ_VMAIN,     // SEP #$20 : LDA $1C14,X : STA $2115
  VQ_SIZE,      // REP #$20 : LDA $1C44,X : STA $4305
  VQ_DONE,      // STZ $26 ... SEC : CLC : RTL

  // $80:B947 sprite_upload_flush
  SU_HEAD,      // LDA $7C : BEQ : LDA #$1801 : STA $4300
  SU_EMPTY,     // LDA $7C : BEQ taken
  SU_VMAIN,     // SEP #$20 : LDA #$80 : STA $2115
  SU_INIT,      // REP #$20 : LDY #$0040 : LDX #$0000
  SU_VADDR2,    // REP #$20 : LDA $16DE,X : ORA #$0100 : STA $2116
  SU_CLEAR,     // STZ $7C
  SU_OAM_IMM,   // LDA #imm : STA reg, four of them
  SU_OAM_LAST,  // REP #$20 : LDA #$8000 : STA $2102
  SU_TAIL,      // SEC : RTL

  // $80:9E3E bg2_scroll_job
  B2_H,         // LDY $1B66 : TYA : XBA : SEP #$30 : STY $210F
  B2_V,         // REP #$30 : LDY $1B68 : TYA : XBA : SEP #$30 : STY $2110
  B2_BASE,      // REP #$30 : LDA $1B7E : XBA : ORA #1 : SEP : SEP : STA $2108
  B2_TAIL,      // REP #$30 : RTL

  // $82:8209 camera_scroll_job
  CS_DX,        // LDA $1B6A : SEC : SBC $1E6E : CMP #$0100 : BCC
  CS_NEG,       // CMP #$FF01 (or #$FF21) : BCC
  CS_WRITE,     // TAY : XBA : SEP #$30 : STY $210D
  CS_DY,        // REP #$30 : LDA $1B6C : SEC : SBC $1E70 : CMP #$0100 : BCC
  CS_PARK,      // SEP #$20 : LDA #$01 : STZ $210D
  CS_TAIL,      // REP #$30 : SEC : RTL

  // $80:9BFC scroll_shadow_job
  SS_FIRST,     // SEP #$20 : LDA $1360 : STA $210D
  SS_NEXT,      // LDA $1361.. : STA $210D..
  SS_TAIL,      // REP #$20 : SEC : RTL

  // $82:81C9 boss_bg_dma
  BB_HEAD,      // SEP #$20 : LDA #$80 : STA $2115
  BB_MODE,      // REP #$30 : LDA #$1801 : STA $4300
  BB_COUNT,     // LDX $1D54 : BEQ
  BB_EMPTY,     // ...taken, CLC : RTL
  BB_FIRST,     // DEX : DEX
  BB_NEXT,      // REP #$20 : DEX : DEX : BPL
  BB_DONE,      // STZ $1D54 : CLC : RTL

  // $80:9ED0 vram_send_job
  VS_MODE,      // LDA #$1801 : STA $4300
  VS_VMAIN,     // SEP #$20 : LDA #$80 : STA $2115
  VS_SOURCE,    // REP #$30 : LDX $C2 : STX $4302
  VS_LOAD,      // LDA $C4 : STA $4304, and LDY $C8 : STY $2116
  VS_TEST,      // LDA $C6 : CMP #$0400 : BCC
  VS_BEQ,       // BEQ
  VS_MORE,      // SBC #$0400 : STA $C6 : LDA #$0400 : STA $4305
  VS_STEP,      // $9F02-$9F12: both addresses moved on, and STA $420B
  VS_MORE_TAIL, // REP #$20 : SEC : RTL
  VS_DONE,      // REP #$20 : STZ $C6 : CLC : RTL

  VBL_BLOCK_COUNT
};

// --- $80:9E7B vram_queue_flush ----------------------------------------------
//
// The VRAM transfer queue, `W_VRAM_QUEUE_*`, sent by DMA channel 0 one entry
// at a time. `$26` bit 14 holds it off for a frame, with carry set; an empty
// queue returns with carry clear. A queue sent is emptied and the tilemap
// arena starts over at `$4B28`, `$0900` long.
void vram_queue_flush(Wram* w, PortCpu* c, HwTrace* t);

// --- $80:B947 sprite_upload_flush -------------------------------------------
//
// The sprite frames `sprite_frame_tile` queued, each 16x16 frame sent as two
// rows of 64 bytes, then all of OAM from `$7E:13BE`, `$220` bytes. Stays
// queued.
void sprite_upload_flush(Wram* w, PortCpu* c, HwTrace* t);

// --- $80:9E3E bg2_scroll_job --------------------------------------------------
//
// BG2's scroll from the camera's sub-tile remainder, `$1B66`/`$1B68`, and its
// tilemap's address from `$1B7E`, 32x32 at `$7800`. Stays queued.
#define W_BG2_TILEMAP_VRAM W_TILEMAP_VRAM_BASE
void bg2_scroll_job(Wram* w, PortCpu* c, HwTrace* t);

// --- $82:8209 camera_scroll_job ----------------------------------------------
//
// BG1's scroll, for the big figure drawn on it: the camera less the plane's
// origin in the world, `$1E6E`/`$1E70`. An offset the tilemap would show
// wrapped round, x outside -255..255 or y outside -223..255, parks both
// scrolls at `$0100`, off the picture. The x scroll is written before y is
// looked at, so a y out of range parks x a second time. Stays queued.
#define W_BOSS_PLANE_X 0x1e6e
#define W_BOSS_PLANE_Y 0x1e70
void camera_scroll_job(Wram* w, PortCpu* c, HwTrace* t);

// --- $80:9BFC scroll_shadow_job -----------------------------------------------
//
// The scroll shadow, `$7E:1360-$136B`, into BG1-BG3's scroll registers, each
// written twice as the hardware takes it. Stays queued.
#define W_SCROLL_SHADOW 0x1360
void scroll_shadow_job(Wram* w, PortCpu* c, HwTrace* t);

// --- $82:81C9 boss_bg_dma -------------------------------------------------------
//
// The tilemap rows `port/bossbg.h` queues, `W_BG_DMA_*`, sent last entry first,
// and the queue emptied. A one-shot: it returns with carry clear.
void boss_bg_dma(Wram* w, PortCpu* c, HwTrace* t);

// --- $82:AEB4 bg1_vscroll_job -------------------------------------------------
//
// BG1's scroll down, from its two bytes of the scroll shadow. It is queued
// every frame while a level's name comes down, and runs once: carry clear.
#define BG1_VSCROLL_JOB_PC 0x82aeb4u
#define BG1_VSCROLL_JOB_RTL_PC 0x82aec5u
void bg1_vscroll_job(Wram* w, PortCpu* c, HwTrace* t);

// --- $80:9ED0 vram_send_job ---------------------------------------------------
//
// Bytes to VRAM, a kilobyte a vblank: `$C6` of them from `$C4:$C2`, to the
// word address at `$C8`. It moves all three on and stays queued, carry set,
// while more than a kilobyte is left. What is left then it sends, and runs
// no more.
#define VRAM_SEND_JOB_PC 0x809ed0u
#define VRAM_SEND_JOB_RTL_PC 0x809f18u  // of two: the other is at `$9F28`
#define W_VRAM_SEND_SOURCE 0x00c2u
#define W_VRAM_SEND_BANK 0x00c4u
#define W_VRAM_SEND_LEFT 0x00c6u
#define W_VRAM_SEND_AT 0x00c8u
#define VRAM_SEND_BYTES 0x0400u
void vram_send_job(Wram* w, PortCpu* c, HwTrace* t);

#endif
