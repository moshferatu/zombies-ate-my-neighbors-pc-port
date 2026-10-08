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
//   $80:8AD3  game_over_sprite_begin ...and the start of each
//   $80:9847  portrait_sprites_begin the two sprites of the portraits' screen
//   $80:8A11  game_over_frame        the game over's thread, a frame of
//   $80:8A30                         each of its two loops
//   $80:9A52  portrait_copy          a player's portrait, into the text map
//   $83:802D  intro_fill             the logo screens' tilemap
//   $83:8241  intro_colours_copy     ...and colours for them
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
#include "port/cpu.h"
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

// --- $80:8A11 and $80:8A30  the game over's thread ---------------------------
//
// The thread has two loops, a frame each time round. The first moves the
// third layer's shadow up a pixel and then the sprites down, until the
// shadow is under `$FF00`. The second moves the sprites and then the shadow,
// until it is under `$FE00`. Each is a stretch here, from its wake to its
// yield or to the instruction after its loop.
#define GAME_OVER_FRAME_PC 0x808a11u
#define GAME_OVER_FRAME_YIELD_PC 0x808a0du  // `JSL thread_yield`, A already 1
#define GAME_OVER_FRAME_END_PC 0x808a1fu
#define GAME_OVER_FRAME_2_PC 0x808a30u
#define GAME_OVER_FRAME_2_YIELD_PC 0x808a2cu
#define GAME_OVER_FRAME_2_END_PC 0x808a3eu
#define GAME_OVER_FIRST_UNTIL 0xff00u
#define GAME_OVER_SECOND_UNTIL 0xfe00u

typedef struct {
  GameOverFallLog fall;
  bool again;  // it left by its yield
} GameOverFrameLog;

void game_over_frame(Wram* w, PortCpu* c, bool second, GameOverFrameLog* log);

// --- $80:8AD3  one of the game over's four sprites, begun ---------------------
//
// X is twice which. A record is taken and its pointer kept at `$08,X` on
// the thread's page. Its place is from two tables, across at `$80:8BAB` and
// down at `$80:8BB3`, and the places down are all above the screen. The
// record is in the screen's space and not the level's. Then the thread is
// told to call nothing when something touches one.
#define GAME_OVER_SPRITE_BEGIN_PC 0x808ad3u
#define GAME_OVER_SPRITE_BEGIN_RTS_PC 0x808b16u
#define GAME_OVER_SPRITE_XS 0x808babu
#define GAME_OVER_SPRITE_YS 0x808bb3u
#define GAME_OVER_SPRITE_PICTURE 0xe9a7u
#define FRONTEND_PICTURE_BANK 0x008fu
#define GAME_OVER_SPRITE_FLAGS 0xc028u

bool game_over_sprite_begin_supported(uint16_t x);
// False with no record free, which the ROM does not test for.
bool game_over_sprite_begin(Wram* w, const Rom* rom, PortCpu* c,
                            uint16_t* record_out);

// --- $80:9847  the two sprites of the portraits' screen ----------------------
//
// Two records, kept at `$50` and `$52` on the thread's page, at 72 and 192
// across and 80 down the screen, both with the picture at `$8F:E82D`. I
// have not looked at what it is. It ends where the handler's call comes
// back, at `$80:98A9`.
#define PORTRAIT_SPRITES_BEGIN_PC 0x809847u
#define PORTRAIT_SPRITES_BEGUN_PC 0x8098a9u
#define PORTRAIT_DP_SPRITES 0x50
#define PORTRAIT_SPRITE_LEFT_X 0x0048
#define PORTRAIT_SPRITE_RIGHT_X 0x00c0
#define PORTRAIT_SPRITE_Y 0x0050
#define PORTRAIT_SPRITE_PICTURE 0xe82du
#define PORTRAIT_SPRITE_FLAGS 0xc000u

// False with fewer than two records free. `records` are the two it took.
bool portrait_sprites_begin(Wram* w, PortCpu* c, uint16_t records[2]);

// --- $80:9A52  a portrait ----------------------------------------------------
//
// One player's portrait, 13 tiles across and 16 down, copied from the
// cartridge into the text map. X is twice the side the player is on, which
// picks the picture, and Y picks the place: 0 for the left, 2 for the right.
// The two far pointers it copies through are left on the caller's page at
// `$58` and `$5B`.
#define PORTRAIT_COPY_PC 0x809a52u
#define PORTRAIT_COPY_RTS_PC 0x809a8fu
// Two tables, read through the caller's data bank.
#define PORTRAIT_PICTURES 0x9aa4u  // a far pointer in four bytes, for each
#define PORTRAIT_PLACES 0x9aacu    // an offset into the text map, for each
#define PORTRAIT_DP_FROM 0x58
#define PORTRAIT_DP_TO 0x5b
#define PORTRAIT_MAP_AT 0x6502u
#define PORTRAIT_ROWS 16
#define PORTRAIT_ROW_BYTES 0x1a
#define PORTRAIT_MAP_ROW 0x0040u

typedef struct {
  uint16_t a;
  bool c, v;  // the last row's add
} PortraitCopyRegs;

// One of the two pictures and one of the two places.
bool portrait_copy_supported(uint16_t x, uint16_t y);

void portrait_copy(Wram* w, const Rom* rom, uint16_t page, uint8_t db,
                   uint16_t x, uint16_t y, PortraitCopyRegs* out);

// --- $83:802D and $83:8241  the logo screens' start --------------------------
//
// `$83:8000` shows the logo screens. It begins by making a tilemap in the
// scratch buffer at `$7E:8000`: 98 bytes of it from the cartridge, tile 2 to
// the end of its first kilobyte, and a second kilobyte of zeroes, which it
// clears as the reset clears, a zero word copied onto itself a byte along.
// `intro_fill` is that, from the `REP` after the PPU's registers are set to
// the instruction that begins to send it.
//
// `$83:8241` copies colours from the cartridge to the ones shown: A bytes
// of them, from Y, to the X'th byte.
#define INTRO_FILL_PC 0x83802du
#define INTRO_FILL_END_PC 0x838060u
#define INTRO_BANK 0x83u
#define INTRO_FILL_AT 0x8000u         // in bank `$7E`
#define INTRO_FILL_HEAD 0x8382acu
#define INTRO_FILL_HEAD_BYTES 0x0062u
#define INTRO_FILL_BLANK_END 0x0400u  // tile 2 up to here
#define INTRO_FILL_END 0x0800u        // ...and zeroes up to here
#define INTRO_BLANK_TILE 0x0002u

void intro_fill(Wram* w, const Rom* rom);

#define INTRO_COLOURS_COPY_PC 0x838241u
#define INTRO_COLOURS_COPY_RTS_PC 0x838253u
#define INTRO_DP_COUNT 0x44

typedef struct {
  uint16_t a, x, y;
} IntroColoursRegs;

// An even count that is not zero, from the cartridge, to colours that are
// there.
bool intro_colours_copy_supported(uint16_t a, uint16_t x, uint16_t y);

void intro_colours_copy(Wram* w, const Rom* rom, uint16_t page, uint16_t a,
                        uint16_t x, uint16_t y, IntroColoursRegs* out);

#endif
