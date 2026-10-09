// zamn_test_video [frames] [seed]
//
// `src/video` against the emulated PPU, on frames no game would make.
//
// A movie checks the lines the game draws, which are a few hundred kinds of
// line drawn millions of times. This checks the kinds it does not draw: an
// emulated PPU is filled with noise -- VRAM, the palette, OAM, the scrolls,
// which layers are on, the colour maths, the colour window, the margins of a
// widened picture and what each layer does with them -- and every line of the
// frame has its sprites found both ways, is drawn both ways, and each is
// compared. What `video_declines` is left to the
// PPU, and a share of the frames are made to have something it declines, so
// that a line it should have declined and drew wrong shows up as a line that
// differs.
//
// There is no console here and nothing is written through a register. The
// noise is put straight into the PPU, and the chip takes what is in the PPU
// (`video_beside_take`) before a line is drawn from it.
//
// Then the frame is read back as the smoothing reads it (`video_bg_row`): a
// few rows of each background, some of them above or below the picture and
// all of them running past its edges, each against `ppu_layerPixel` a column
// at a time.
//
// No ROM and no window. Exits 1 if any line differed.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ppu.h"
#include "video/beside.h"

static uint64_t rng_state;

static uint32_t rnd(void) {
  rng_state ^= rng_state << 13;
  rng_state ^= rng_state >> 7;
  rng_state ^= rng_state << 17;
  return (uint32_t)(rng_state >> 16);
}

static int below(int n) { return (int)(rnd() % (uint32_t)n); }
static bool one_in(int n) { return below(n) == 0; }

// A window edge: any column, or one of the values the widened picture reads
// specially.
static uint8_t edge(void) {
  static const uint8_t SPECIAL[] = {0, 255, 127, 128};
  return one_in(3) ? SPECIAL[below(4)] : (uint8_t)rnd();
}

static void noise(Ppu* ppu) {
  // VRAM: noise, with stretches of nothing in it so that tiles have
  // transparent rows and maps have empty tiles.
  for (int i = 0; i < 0x8000;) {
    const int n = 1 + below(48);
    const bool nothing = one_in(3);
    for (int k = 0; k < n && i < 0x8000; k++, i++) ppu->vram[i] = nothing ? 0 : (uint16_t)rnd();
  }
  for (int i = 0; i < 0x100; i++) ppu->cgram[i] = (uint16_t)(rnd() & 0x7fff);
  for (int i = 0; i < 0x100; i++) ppu->oam[i] = (uint16_t)rnd();
  for (int i = 0; i < 0x20; i++) ppu->highOam[i] = (uint8_t)rnd();
  // Sprites at random are spread too thin for a line ever to have more than
  // it has time for. One frame in four has them all on the console's columns
  // and in a band of lines, some or all of them.
  if (one_in(4)) {
    const int band = below(256), crowd = 16 << below(4);
    for (int i = 0; i < crowd; i++)
      ppu->oam[i * 2] = (uint16_t)(((band + below(48)) & 0xff) << 8 | (rnd() & 0xff));
    for (int i = 0; i < 0x20; i++) ppu->highOam[i] &= 0xaa;
  }
  const int obsel = below(256);
  ppu->objSize = (uint8_t)(obsel >> 5);
  ppu->objTileAdr1 = (uint16_t)((obsel & 7) << 13);
  ppu->objTileAdr2 = (uint16_t)(ppu->objTileAdr1 + (((obsel & 0x18) + 8) << 9));
  ppu->objPriority = one_in(4);
  ppu->objInterlace = one_in(40);
  ppu->oamAdr = (uint8_t)rnd();
  for (int i = 0; i < 0x80; i++) {
    ppu->objFront[i] = one_in(40);
    ppu->objRemapOn[i] = one_in(40);
    ppu->spritePlace[i] = (uint8_t)(one_in(8) ? below(3) : 0);
    ppu->spriteShift[i] = (int16_t)(one_in(30) ? below(64) - 32 : 0);
  }
  for (int i = 0; i < 16; i++) ppu->objRemap[i] = (uint8_t)(one_in(2) ? rnd() : 0);

  ppu->forcedBlank = one_in(20);
  ppu->brightness = (uint8_t)below(16);
  ppu->mode = (uint8_t)(one_in(20) ? below(8) : 1);
  ppu->bg3priority = one_in(2);
  ppu->pseudoHires = one_in(40);
  ppu->mosaicSize = (uint8_t)(one_in(20) ? 1 + below(16) : 1);
  const bool layer_windows = one_in(20);
  for (int i = 0; i < 4; i++) {
    BgLayer* bg = &ppu->bgLayer[i];
    bg->hScroll = (uint16_t)(rnd() & 0x3ff);
    bg->vScroll = (uint16_t)(rnd() & 0x3ff);
    bg->tilemapWider = one_in(2);
    bg->tilemapHigher = one_in(2);
    bg->tilemapAdr = (uint16_t)((rnd() & 0xfc) << 8);
    bg->tileAdr = (uint16_t)((rnd() & 0xf) << 12);
    bg->bigTiles = one_in(3);
    bg->mosaicEnabled = one_in(3);
  }
  for (int i = 0; i < 5; i++) {
    ppu->layer[i].mainScreenEnabled = !one_in(4);
    ppu->layer[i].subScreenEnabled = one_in(2);
    ppu->layer[i].mainScreenWindowed = layer_windows && one_in(2);
    ppu->layer[i].subScreenWindowed = layer_windows && one_in(2);
  }
  for (int i = 0; i < 6; i++) {
    WindowLayer* w = &ppu->windowLayer[i];
    w->window1enabled = one_in(2);
    w->window2enabled = one_in(2);
    w->window1inversed = one_in(2);
    w->window2inversed = one_in(2);
    w->maskLogic = (uint8_t)below(4);
  }
  ppu->window1left = edge();
  ppu->window1right = edge();
  ppu->window2left = edge();
  ppu->window2right = edge();
  const bool maths = !one_in(3);
  for (int i = 0; i < 6; i++) ppu->mathEnabled[i] = maths && one_in(2);
  ppu->addSubscreen = one_in(3);
  ppu->subtractColor = one_in(2);
  ppu->halfColor = one_in(20);
  ppu->fixedColorR = (uint8_t)below(32);
  ppu->fixedColorG = (uint8_t)below(32);
  ppu->fixedColorB = (uint8_t)below(32);
  ppu->preventMathMode = (uint8_t)below(4);
  ppu->clipMode = (uint8_t)(one_in(20) ? below(4) : 0);

  // The widened picture: the console a third of the time, else margins of
  // the frontend's sizes or of any size, the two not always the same.
  static const int MARGIN[] = {35, 43, 71, 96, 192};
  int left = 0, right = 0;
  if (!one_in(3)) {
    left = one_in(2) ? MARGIN[below(5)] : below(PPU_EXTRA_MAX + 1);
    right = one_in(2) ? left : one_in(2) ? MARGIN[below(5)] : below(PPU_EXTRA_MAX + 1);
  }
  ppu_setWidescreen(ppu, left, right);
  static const uint8_t BG_POLICY[] = {ppu_wideAuto, ppu_wideAuto,   ppu_wideStretch,
                                      ppu_wideAnchor, ppu_wideClip, ppu_wideCentre,
                                      ppu_wideTile,   ppu_wideSweep, ppu_wideCentreClip};
  for (int i = 0; i < 4; i++)
    ppu->layerWide[i] = one_in(30) ? ppu_wideClampEdge : BG_POLICY[below(9)];
  static const uint8_t OBJ_POLICY[] = {ppu_wideAuto, ppu_wideStretch, ppu_wideClip};
  ppu->layerWide[4] = one_in(30) ? (uint8_t)below(9) : OBJ_POLICY[below(3)];
  if (one_in(2))
    ppu_setWideClamp(ppu, -PPU_EXTRA_MAX, 255 + PPU_EXTRA_MAX);
  else
    ppu_setWideClamp(ppu, below(300) - PPU_EXTRA_MAX, 150 + below(300));
}

// How far past the picture's edges a row is read, as `src/layers.h` reads it.
#define ROW_MARGIN 16

static long rows_read, rows_differing;

// Some rows of each background of the frame just drawn. A centred layer's
// margins are searched for with a copy of what the frame's lines found, as
// `src/layers.h` reads them.
static void read_rows(Ppu* ppu, const VideoChip* chip) {
  const VideoCentre* found = &chip->frame.centre;
  VideoState s;
  video_chip_state(chip, &s);
  const int from = -ppu->extraLeft - ROW_MARGIN, to = 256 + ppu->extraRight + ROW_MARGIN;
  for (int layer = 0; layer < 3; layer++) {
    if (video_bg_row_declines(&s, layer) || ppu->layer[layer].mainScreenWindowed) continue;
    for (int k = 0; k < 12; k++) {
      const int line = below(224 + 2 * ROW_MARGIN) + 1 - ROW_MARGIN;
      uint8_t back[PPU_MAX_WIDTH + 2 * ROW_MARGIN], front[PPU_MAX_WIDTH + 2 * ROW_MARGIN];
      VideoCentre centre = *found;
      video_bg_row(&s, &centre, layer, line, from, to, back, front);
      bool same = true;
      for (int x = from; x < to && same; x++) {
        int priority = 0;
        const int pixel = ppu_layerPixel(ppu, layer, x, line, false, &priority);
        same = back[x - from] == (priority ? 0 : pixel) &&
               front[x - from] == (priority ? pixel : 0);
      }
      rows_read++;
      if (!same) rows_differing++;
    }
  }
}

int main(int argc, char** argv) {
  const int frames = argc > 1 ? atoi(argv[1]) : 400;
  const uint64_t seed = argc > 2 ? strtoull(argv[2], NULL, 0) : 1;
  Ppu* ppu = ppu_init(NULL);
  static VideoBeside beside;
  ppu_reset(ppu);
  video_beside_install(&beside, ppu, VIDEO_CHECK, true);
  long failed_frame = -1;
  for (int f = 0; f < frames; f++) {
    // Each frame's noise from its own number, so one frame can be made again.
    rng_state = (seed + (uint64_t)f) * 0x9e3779b97f4a7c15ull | 1;
    noise(ppu);
    // What the PPU works out at the top of a frame, then some of it at
    // random: random maps are seldom empty at their edges.
    ppu_handleFrameStart(ppu);
    for (int i = 0; i < 4; i++) {
      if (one_in(2)) ppu->layerEdgeEmpty[i] = one_in(2);
      ppu->layerScrolled[i] = one_in(2);
      ppu->layerRaster[i] = one_in(6);
    }
    video_beside_take(&beside);
    // A fifth of the frames move a scroll or a window on the way down.
    const bool raster = one_in(5);
    for (int line = 1; line <= 224; line++) {
      if (raster && one_in(3)) {
        BgLayer* bg = &ppu->bgLayer[below(3)];
        if (one_in(2)) bg->hScroll = (uint16_t)(rnd() & 0x3ff);
        else bg->vScroll = (uint16_t)(rnd() & 0x3ff);
        if (one_in(4)) ppu->window1left = edge();
        if (one_in(4)) ppu->window1right = edge();
        video_beside_take(&beside);
      }
      ppu_runLine(ppu, line);
    }
    read_rows(ppu, &beside.console.chip);
    const bool differing = beside.differing || beside.sprite_differing ||
                           beside.notes_differing || rows_differing;
    if (differing && failed_frame < 0) failed_frame = f;
  }
  printf("%d frames of noise, seed %llu.\n", frames, (unsigned long long)seed);
  bool ok = video_beside_report(&beside, stdout);
  printf("  %ld rows read back, %ld differ from the PPU's.\n", rows_read, rows_differing);
  ok = ok && rows_differing == 0;
  if (!ok)
    printf("  That frame is number %ld here: zamn_test_video %ld %llu ends on it.\n", failed_frame,
           failed_frame + 1, (unsigned long long)seed);
  ppu_free(ppu);
  return ok ? 0 : 1;
}
