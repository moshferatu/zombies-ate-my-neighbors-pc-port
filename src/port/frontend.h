// The screens outside a level: their vblank jobs, and one step of the game
// over.
//
//   $83:8255  intro_screen_job       the logo screens, each frame
//   $80:953B  backdrop_drift_job     the backdrop behind the title and menus
//   $82:B1F9  backdrop_slide_job     the backdrop behind a screen of words
//   $80:9A1B  portrait_scroll_job    the two layers behind the portraits
//   $80:8B70  game_over_scroll_job   the game over's third layer, each frame
//   $80:8B82  game_over_colours_job  ...and three of its colours
//   $80:8A58  game_over_fall         its four sprites, a step down
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
// ## The backdrop's slide
//
// Behind a screen of words the third layer slides down and to the right, a
// pixel every fourth frame. `$6A` counts the frames, and on the fourth the
// job moves the scroll's shadow and sends both words to the hardware itself.
//
// ## The portraits
//
// `$80:9A31` draws the two players' portraits on the text layer, and this
// job moves what is behind them: the first layer down a pixel a frame, and
// the third down and across a pixel every fifth. `$5C` counts the five. It
// touches no hardware.
//
// ## The game over
//
// `$80:8A00` is the screen after the last life. The third layer scrolls up
// from a shadow the thread counts down, and one job sends the shadow each
// frame. A second sends three colours, from `$19` on, every frame, and the
// colours are in its instructions. Four sprites come down the screen with
// it: `game_over_fall` moves each a pixel, and two on an odd frame. Their
// records are the four pointers at `$08` on the thread's page.
//
// ## Their contract with the ROM
//
// WRAM as the ROM writes it, and for the jobs that write the hardware the
// ROM's writes in its order. Every job stays queued: carry set.
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
  FE_SEP_SCROLL,  // SEP #$20 : LDA abs : STA abs
  FE_SEC_RTL,     // SEC : RTL
  BS_HEAD,        // $82:B1F9-$B204: the count, and the test for a fourth
  BS_STEP,        // INC $1368 : INC $136A
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

#define BACKDROP_SLIDE_JOB_PC 0x82b1f9u
#define BACKDROP_SLIDE_JOB_RTL_PC 0x82b228u
#define W_BACKDROP_SLIDE_COUNT 0x006au

// `$82:B1F9`. True on a fourth frame, when the layer moved.
bool backdrop_slide_job(Wram* w, HwTrace* t);

#define PORTRAIT_SCROLL_JOB_PC 0x809a1bu
#define PORTRAIT_SCROLL_JOB_RTL_PC 0x809a30u
#define W_BG1_SCROLL_Y 0x1362u
#define W_PORTRAIT_SCROLL_COUNT 0x005cu
#define PORTRAIT_SCROLL_EVERY 5

// `$80:9A1B`. True on a fifth frame, when the third layer moved too.
bool portrait_scroll_job(Wram* w);

#define GAME_OVER_SCROLL_JOB_PC 0x808b70u
#define GAME_OVER_SCROLL_JOB_RTL_PC 0x808b81u
#define GAME_OVER_COLOURS_JOB_PC 0x808b82u
#define GAME_OVER_COLOURS_JOB_RTL_PC 0x808baau
#define GAME_OVER_FALL_PC 0x808a58u
#define GAME_OVER_FALL_RTS_PC 0x808a77u
#define GAME_OVER_SPRITES 4
#define GAME_OVER_DP_SPRITES 0x08  // four pointers to their records
#define GAME_OVER_FIRST_COLOUR 0x19

// `$80:8B70`.
void game_over_scroll_job(const Wram* w, HwTrace* t);
// `$80:8B82`. The last byte it sent is left in A.
uint8_t game_over_colours_job(HwTrace* t);

typedef struct {
  bool twice;      // an odd frame
  uint16_t a, x;   // the frame's low bit, and the last record
  uint16_t last;   // ...whose height this is now, for the flags
} GameOverFallLog;

// Each of the four records is in low WRAM.
bool game_over_fall_supported(const Wram* w, uint16_t d);

// `$80:8A58`. `d` is the thread's page. `log` may be NULL.
void game_over_fall(Wram* w, uint16_t d, GameOverFallLog* log);

#endif
