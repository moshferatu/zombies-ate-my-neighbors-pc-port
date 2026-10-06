// The logo screens, between their waits.
//
// `$83:8000` shows the logo screens from one long run of code: eight loops,
// each waiting for a vblank with `WAI` and then doing a frame's work. The
// job `intro_screen_job` (`port/frontend.h`) puts what they change on the
// screen. This is each loop's frame, from the instruction after its `WAI` to
// the next `WAI`, or to the first instruction that is not the loop's.
//
//     $83:8102  slide   every other frame the tile at `$3C` steps 6, 8, 10,
//                       12 and round; every frame the first layer's shadow
//                       goes 8 left, until it is at zero
//     $83:8135  cycle   every fourth frame sixteen shown colours move down
//                       one place, three times
//     $83:815A  rise    the first layer's shadow 2 up, until it is at `$300`
//     $83:8184  sweep   two rows of ten colours move down one place, each
//                       row's first going to its end, ten times
//     $83:81CC  sweep 2 every other frame a third row does, ten times
//     $83:81F2  flash   the first shown colour from black to white, a step a
//                       frame of each of its three fives of bits
//     $83:8217  hold    255 frames
//     $83:821E  fade    the brightness shadow down to zero
//
// Each begins with `JSR $8254`, which is an `RTS` and nothing else.
//
// Port code: libc only.

#ifndef PORT_LOGO_H
#define PORT_LOGO_H

#include <stdint.h>

#include "port/cpu.h"
#include "port/wram.h"

typedef enum {
  LOGO_SLIDE,
  LOGO_CYCLE,
  LOGO_RISE,
  LOGO_SWEEP,
  LOGO_SWEEP_2,
  LOGO_FLASH,
  LOGO_HOLD,
  LOGO_FADE,
  LOGO_STAGES
} LogoStage;

// Where each begins, after its `WAI`, and its `WAI`.
#define LOGO_SLIDE_PC 0x838102u
#define LOGO_SLIDE_WAI_PC 0x838101u
#define LOGO_CYCLE_PC 0x838135u
#define LOGO_CYCLE_WAI_PC 0x838134u
#define LOGO_RISE_PC 0x83815au
#define LOGO_RISE_WAI_PC 0x838159u
#define LOGO_RISE_END_PC 0x83816eu     // `JSL` for a sound, A already `$31`
#define LOGO_SWEEP_PC 0x838184u
#define LOGO_SWEEP_WAI_PC 0x838183u
#define LOGO_SWEEP_END_PC 0x8381b9u
#define LOGO_SWEEP_2_PC 0x8381ccu
#define LOGO_SWEEP_2_WAI_PC 0x8381cau  // two of them
#define LOGO_FLASH_PC 0x8381f2u
#define LOGO_FLASH_WAI_PC 0x8381f1u
#define LOGO_HOLD_PC 0x838217u
#define LOGO_HOLD_WAI_PC 0x838216u
#define LOGO_FADE_PC 0x83821eu
#define LOGO_FADE_WAI_PC 0x83821du
#define LOGO_FADE_END_PC 0x838229u

// On page zero, where the routine puts its direct page.
#define LOGO_DP_STEP 0x38      // the flash's
#define LOGO_DP_SCRATCH 0x3a
#define LOGO_DP_TILE 0x3c      // `INTRO_SCREEN_DP_TILE`
#define LOGO_DP_FRAMES 0x42
#define LOGO_DP_TIMES 0x44
#define LOGO_DP_OTHER 0x46     // flips every frame of the slide
#define W_LOGO_SCROLL_X 0x1360u
#define W_LOGO_SCROLL_Y 0x1362u
#define W_LOGO_SCROLL_2_X 0x1364u
#define W_LOGO_BRIGHTNESS 0x136cu  // `W_BRIGHTNESS_SHADOW`
// The shown colours, in bank `$7E`: `PALETTE_SHOWN_AT` in `port/dma.h`.
#define LOGO_COLOURS 0x5628u

enum {
  LG_RTS,
  LG_1A,  // $8102-$810D
  LG_1B,  // $810E-$8118
  LG_1C,  // $8119-$811D
  LG_1D,  // $811E-$8129
  LG_1E,  // $812A-$8133
  LG_2A,  // $8135-$8140
  LG_2B,  // $8141-$8145
  LG_2W,  // $8146-$8154
  LG_2C,  // $8155-$8158
  LG_3A,  // $815A-$816A
  LG_3B,  // $816B-$816D
  LG_4A,  // $8184-$8193
  LG_4W,  // $8194-$81AA
  LG_4C,  // $81AB-$81B8
  LG_5A,  // $81CC-$81D6
  LG_5W,  // $81D7-$81E5
  LG_5C,  // $81E6-$81EE
  LG_5D,  // $81EF-$81F0
  LG_6A,  // $81F2-$8212
  LG_6B,  // $8213-$8215
  LG_7A,  // $8217-$821C
  LG_8A,  // $821E-$8228
  LG_TAKEN,
  LG_BLOCK_COUNT
};

typedef struct {
  uint16_t blocks[LG_BLOCK_COUNT];
} LogoWork;

void logo_frame(Wram* w, PortCpu* c, LogoStage stage, LogoWork* k);

#endif
