// $80:AC7E and $80:88A9  the layers set up for a level -- see
// port/layers_setup.h.

#include "port/layers_setup.h"

#include "port/coverage.h"
#include "port/dma.h"     // the runs' numbers
#include "port/vblank.h"  // W_SCROLL_SHADOW

// The registers.
#define REG_BGMODE 0x2105u
#define REG_BG1SC 0x2107u   // a layer's map: where, and how big
#define REG_BG2SC 0x2108u
#define REG_BG3SC 0x2109u
#define REG_BG12NBA 0x210bu  // the first two layers' tiles: where
#define REG_BG34NBA 0x210cu
#define REG_BG1HOFS 0x210du  // the first of six scroll registers
#define REG_TM 0x212cu       // what is on the main screen

#define MODE_1_BG3_FRONT 0x09u
#define MAP_6800_64_BY_64 0x6bu
#define MAP_7000 0x70u
#define MAP_6400 0x64u
#define TILES_5000_AND_2000 0x25u
#define TILES_4000 0x44u
#define MAIN_THREE_LAYERS_AND_SPRITES 0x17u

// `first_run` is the run before the first store: one with a `SEP` in it at
// `$80:AC7E`, and a plain `LDA #` where `$80:88A9` comes to these.
static void setup(HwTrace* t, int first_run) {
  hw_run(t, first_run);
  hw_w8(t, REG_BGMODE, MODE_1_BG3_FRONT);
  hw_run(t, DMA_IMM);
  hw_w8(t, REG_BG12NBA, TILES_5000_AND_2000);
  hw_run(t, DMA_IMM);
  hw_w8(t, REG_BG1SC, MAP_6800_64_BY_64);
  hw_run(t, DMA_IMM);
  hw_w8(t, REG_BG2SC, MAP_7000);
  hw_run(t, DMA_IMM);
  hw_w8(t, REG_BG3SC, MAP_6400);
  hw_run(t, DMA_IMM);
  hw_w8(t, REG_BG34NBA, TILES_4000);
  hw_run(t, DMA_RTL);
}

void layers_setup(HwTrace* t) {
  PORT_COVER(layers_set_up);
  setup(t, DMA_SEP_IMM);
}

void layers_reset(Wram* w, HwTrace* t) {
  PORT_COVER(layers_reset);
  for (int i = 0; i < LAYERS_SCROLL_WORDS; i++)
    wram_w16(w, (uint16_t)(W_SCROLL_SHADOW + 2 * i), 0);
  // A scroll register takes two writes, the low byte and then the high.
  for (int i = 0; i < LAYERS_SCROLL_WORDS; i++) {
    hw_run(t, i == 0 ? LR_HEAD : DMA_STORE);
    hw_w8(t, (uint16_t)(REG_BG1HOFS + i), 0);
    hw_run(t, DMA_STORE);
    hw_w8(t, (uint16_t)(REG_BG1HOFS + i), 0);
  }
  hw_run(t, DMA_IMM);
  hw_w8(t, REG_TM, MAIN_THREE_LAYERS_AND_SPRITES);
  setup(t, DMA_IMM);
}
