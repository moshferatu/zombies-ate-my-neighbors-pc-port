// Two vblank jobs of the screens outside a level.
//
//   $83:8255  intro_screen_job    the logo screens, each frame
//   $80:953B  backdrop_drift_job  the backdrop behind the title and menus
//
// ## The intro's screen
//
// The logo screens change their colours and slide their layers from a
// thread, and this job puts all of it on the screen at once: the first 64
// shown colours, the scroll of the first two layers from their shadow at
// `$7E:1360`, and one byte of the tilemap at VRAM `$4030`, from `$3C`.
//
// Like the jobs in `port/dma.h` it records its writes and does not make them.
//
// ## The backdrop's drift
//
// The picture behind the title and the menus is the third layer, and it
// wanders. A table in the cartridge, `$83:9391`, holds forty steps of a
// closed path, a word across and a word down, and every fourth frame this
// job adds the next to the layer's scroll shadow.
//
// It touches no hardware. The shadow is sent by another job.
//
// ## Their contract with the ROM
//
// WRAM as the ROM writes it, and for the first the ROM's writes in its
// order. Both stay queued: carry set.
//
// Port code: libc only.

#ifndef PORT_FRONTEND_H
#define PORT_FRONTEND_H

#include <stdbool.h>
#include <stdint.h>

#include "assets/rom.h"
#include "port/dma.h"
#include "port/hw.h"
#include "port/wram.h"

#define INTRO_SCREEN_JOB_PC 0x838255u
#define INTRO_SCREEN_JOB_RTL_PC 0x8382abu
#define INTRO_SCREEN_COLOUR_BYTES 0x0080u
#define INTRO_SCREEN_TILE_AT 0x4030u  // a VRAM word address
#define INTRO_SCREEN_DP_TILE 0x3c

#define BACKDROP_DRIFT_JOB_PC 0x80953bu
#define BACKDROP_DRIFT_JOB_RTL_PC 0x80956fu
#define BACKDROP_DRIFT_PATH 0x839391u
#define BACKDROP_DRIFT_PATH_BYTES 0x00a0u  // forty steps of two words
#define W_BACKDROP_DRIFT_AT 0x1f88u        // which step is next, in bytes
#define W_FRAME_COUNT 0x0020u
#define W_BG3_SCROLL_X 0x1368u
#define W_BG3_SCROLL_Y 0x136au

// The intro job's own runs, numbered on from `port/dma.h`'s.
enum {
  IS_VMAIN = DMA_BLOCK_COUNT,  // SEP #$20 : LDA #$80 : STA $2115
  IS_SCROLL,  // LDA $136x : STA $210x
  IS_VADDR,   // REP #$30 : LDA #$4030 : STA $2116
  IS_TILE,    // SEP #$20 : LDA $3C : STA $2118
  IS_TAIL,    // REP #$20 : SEC : RTL
  FRONTEND_BLOCK_COUNT
};

// `$83:8255`. It leaves `$40` and the tile's byte in A, and the colours'
// address and length in X and Y.
void intro_screen_job(const Wram* w, HwTrace* t);

typedef struct {
  bool moved;    // a fourth frame
  bool wrapped;  // ...and the last step of the path
  uint16_t a, x;
  bool n, z, v;
} BackdropDriftLog;

// Not a step that is off the path.
bool backdrop_drift_job_supported(const Wram* w);

// `$80:953B`. `x` is X as the job found it. `log` may be NULL.
void backdrop_drift_job(Wram* w, const Rom* rom, uint16_t x,
                        BackdropDriftLog* log);

#endif
