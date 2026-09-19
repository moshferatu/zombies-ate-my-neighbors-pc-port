
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "ppu.h"
#include "snes.h"
#include "statehandler.h"

// array for layer definitions per mode:
//   0-7: mode 0-7; 8: mode 1 + l3prio; 9: mode 7 + extbg

//   0-3; layers 1-4; 4: sprites; 5: nonexistent
static const int layersPerMode[10][12] = {
  {4, 0, 1, 4, 0, 1, 4, 2, 3, 4, 2, 3},
  {4, 0, 1, 4, 0, 1, 4, 2, 4, 2, 5, 5},
  {4, 0, 4, 1, 4, 0, 4, 1, 5, 5, 5, 5},
  {4, 0, 4, 1, 4, 0, 4, 1, 5, 5, 5, 5},
  {4, 0, 4, 1, 4, 0, 4, 1, 5, 5, 5, 5},
  {4, 0, 4, 1, 4, 0, 4, 1, 5, 5, 5, 5},
  {4, 0, 4, 4, 0, 4, 5, 5, 5, 5, 5, 5},
  {4, 4, 4, 0, 4, 5, 5, 5, 5, 5, 5, 5},
  {2, 4, 0, 1, 4, 0, 1, 4, 4, 2, 5, 5},
  {4, 4, 1, 4, 0, 4, 1, 5, 5, 5, 5, 5}
};

static const int prioritysPerMode[10][12] = {
  {3, 1, 1, 2, 0, 0, 1, 1, 1, 0, 0, 0},
  {3, 1, 1, 2, 0, 0, 1, 1, 0, 0, 5, 5},
  {3, 1, 2, 1, 1, 0, 0, 0, 5, 5, 5, 5},
  {3, 1, 2, 1, 1, 0, 0, 0, 5, 5, 5, 5},
  {3, 1, 2, 1, 1, 0, 0, 0, 5, 5, 5, 5},
  {3, 1, 2, 1, 1, 0, 0, 0, 5, 5, 5, 5},
  {3, 1, 2, 1, 0, 0, 5, 5, 5, 5, 5, 5},
  {3, 2, 1, 0, 0, 5, 5, 5, 5, 5, 5, 5},
  {1, 3, 1, 1, 2, 0, 0, 1, 0, 0, 5, 5},
  {3, 2, 1, 1, 0, 0, 0, 5, 5, 5, 5, 5}
};

static const int layerCountPerMode[10] = {
  12, 10, 8, 8, 8, 8, 6, 5, 10, 7
};

static const int bitDepthsPerMode[10][4] = {
  {2, 2, 2, 2},
  {4, 4, 2, 5},
  {4, 4, 5, 5},
  {8, 4, 5, 5},
  {8, 2, 5, 5},
  {4, 2, 5, 5},
  {4, 5, 5, 5},
  {8, 5, 5, 5},
  {4, 4, 2, 5},
  {8, 7, 5, 5}
};

static const int spriteSizes[8][2] = {
  {8, 16}, {8, 32}, {8, 64}, {16, 32},
  {16, 64}, {32, 64}, {16, 32}, {16, 32}
};

static void ppu_handlePixel(Ppu* ppu, int x, int y);
static int ppu_getPixel(Ppu* ppu, int x, int y, bool sub, int* r, int* g, int* b);
static uint16_t ppu_getOffsetValue(Ppu* ppu, int col, int row);
static int ppu_getPixelForBgLayer(Ppu* ppu, int x, int y, int layer, bool priority);
static void ppu_handleOPT(Ppu* ppu, int layer, int* lx, int* ly);
static void ppu_calculateMode7Starts(Ppu* ppu, int y);
static int ppu_getPixelForMode7(Ppu* ppu, int x, int layer, bool priority);
static bool ppu_getWindowState(Ppu* ppu, int layer, int x);
static bool ppu_getWindowStateOnLine(Ppu* ppu, int layer, int x, int line);
static void ppu_evaluateSprites(Ppu* ppu, int line);
static uint16_t ppu_getVramRemap(Ppu* ppu);
static bool ppu_wideMapX(Ppu* ppu, int layer, int* x, int* y);
static bool ppu_windowTest(const Ppu* ppu, int x, int left, int right);
static int ppu_spriteX(const Ppu* ppu, uint8_t index);

Ppu* ppu_init(Snes* snes) {
  Ppu* ppu = malloc(sizeof(Ppu));
  ppu->snes = snes;
  ppu_setPixelOutputFormat(ppu, ppu_pixelOutputFormatBGRX);
  // Set here and not in `ppu_reset`, because the width of the picture belongs
  // to whoever is displaying it and a game resetting itself is not a reason to
  // take it back to 256.
  ppu->extraLeft = 0;
  ppu->extraRight = 0;
  for(int i = 0; i < 5; i++) ppu->layerWide[i] = ppu_wideAuto;
  memset(ppu->spritePlace, 0, sizeof(ppu->spritePlace));
  for(int i = 0; i < 4; i++) for(int j = 0; j < 2; j++) {
    ppu->centreFillCol[i][j] = -1; ppu->centreFillLine[i][j] = -1; ppu->centreFillFrom[i][j] = -1;
    ppu->centreFillWrap[i][j] = 0; ppu->centreLastFull[i] = -2;
    ppu->centreLastFullH[i] = ppu->centreLastFullV[i] = 0;
    ppu->centreFillH[i][j] = ppu->centreFillV[i][j] = 0;
  }
  for(int i = 0; i < 4; i++) {
    ppu->layerEdgeEmpty[i] = 0;
    ppu->layerScrolled[i] = 0;
    ppu->lastHScroll[i] = 0;
    ppu->layerRaster[i] = 0;
    ppu->hScrollWrites[i] = 0;
  }
  ppu->wideClampLo = -PPU_EXTRA_MAX;
  ppu->wideClampHi = 255 + PPU_EXTRA_MAX;
  memset(ppu->lineHScroll, 0, sizeof(ppu->lineHScroll));
  memset(ppu->lineVScroll, 0, sizeof(ppu->lineVScroll));
  ppu->midFrameWrite = false;
  ppu->midFrameWrites = 0;
  ppu->windowRaster = false;
  return ppu;
}

void ppu_free(Ppu* ppu) {
  free(ppu);
}

void ppu_reset(Ppu* ppu) {
  memset(ppu->vram, 0, sizeof(ppu->vram));
  ppu->vramPointer = 0;
  ppu->vramIncrementOnHigh = false;
  ppu->vramIncrement = 1;
  ppu->vramRemapMode = 0;
  ppu->vramReadBuffer = 0;
  memset(ppu->cgram, 0, sizeof(ppu->cgram));
  ppu->cgramPointer = 0;
  ppu->cgramSecondWrite = false;
  ppu->cgramBuffer = 0;
  memset(ppu->oam, 0, sizeof(ppu->oam));
  memset(ppu->highOam, 0, sizeof(ppu->highOam));
  ppu->oamAdr = 0;
  ppu->oamAdrWritten = 0;
  ppu->oamInHigh = false;
  ppu->oamInHighWritten = false;
  ppu->oamSecondWrite = false;
  ppu->oamBuffer = 0;
  ppu->objPriority = false;
  ppu->objTileAdr1 = 0;
  ppu->objTileAdr2 = 0;
  ppu->objSize = 0;
  memset(ppu->objPixelBuffer, 0, sizeof(ppu->objPixelBuffer));
  memset(ppu->objPriorityBuffer, 0, sizeof(ppu->objPriorityBuffer));
  memset(ppu->objRemapOn, 0, sizeof(ppu->objRemapOn));
  memset(ppu->objRemap, 0, sizeof(ppu->objRemap));
  ppu->timeOver = false;
  ppu->rangeOver = false;
  ppu->objInterlace = false;
  for(int i = 0; i < 4; i++) {
    ppu->bgLayer[i].hScroll = 0;
    ppu->bgLayer[i].vScroll = 0;
    ppu->bgLayer[i].tilemapWider = false;
    ppu->bgLayer[i].tilemapHigher = false;
    ppu->bgLayer[i].tilemapAdr = 0;
    ppu->bgLayer[i].tileAdr = 0;
    ppu->bgLayer[i].bigTiles = false;
    ppu->bgLayer[i].mosaicEnabled = false;
  }
  ppu->scrollPrev = 0;
  ppu->scrollPrev2 = 0;
  ppu->mosaicSize = 1;
  ppu->mosaicStartLine = 1;
  for(int i = 0; i < 5; i++) {
    ppu->layer[i].mainScreenEnabled = false;
    ppu->layer[i].subScreenEnabled = false;
    ppu->layer[i].mainScreenWindowed = false;
    ppu->layer[i].subScreenWindowed = false;
  }
  memset(ppu->m7matrix, 0, sizeof(ppu->m7matrix));
  ppu->m7prev = 0;
  ppu->m7largeField = false;
  ppu->m7charFill = false;
  ppu->m7xFlip = false;
  ppu->m7yFlip = false;
  ppu->m7extBg = false;
  ppu->m7startX = 0;
  ppu->m7startY = 0;
  for(int i = 0; i < 6; i++) {
    ppu->windowLayer[i].window1enabled = false;
    ppu->windowLayer[i].window2enabled = false;
    ppu->windowLayer[i].window1inversed = false;
    ppu->windowLayer[i].window2inversed = false;
    ppu->windowLayer[i].maskLogic = 0;
  }
  ppu->window1left = 0;
  ppu->window1right = 0;
  ppu->window2left = 0;
  ppu->window2right = 0;
  ppu->clipMode = 0;
  ppu->preventMathMode = 0;
  ppu->addSubscreen = false;
  ppu->subtractColor = false;
  ppu->halfColor = false;
  memset(ppu->mathEnabled, 0, sizeof(ppu->mathEnabled));
  ppu->fixedColorR = 0;
  ppu->fixedColorG = 0;
  ppu->fixedColorB = 0;
  ppu->forcedBlank = true;
  ppu->brightness = 0;
  ppu->mode = 0;
  ppu->bg3priority = false;
  ppu->evenFrame = false;
  ppu->pseudoHires = false;
  ppu->overscan = false;
  ppu->frameOverscan = false;
  ppu->interlace = false;
  ppu->frameInterlace = false;
  ppu->directColor = false;
  ppu->hCount = 0;
  ppu->vCount = 0;
  ppu->hCountSecond = false;
  ppu->vCountSecond = false;
  ppu->countersLatched = false;
  ppu->ppu1openBus = 0;
  ppu->ppu2openBus = 0;
  memset(ppu->pixelBuffer, 0, sizeof(ppu->pixelBuffer));
}

void ppu_handleState(Ppu* ppu, StateHandler* sh) {
  sh_handleBools(sh,
    &ppu->vramIncrementOnHigh, &ppu->cgramSecondWrite, &ppu->oamInHigh, &ppu->oamInHighWritten, &ppu->oamSecondWrite,
    &ppu->objPriority, &ppu->timeOver, &ppu->rangeOver, &ppu->objInterlace, &ppu->m7largeField, &ppu->m7charFill,
    &ppu->m7xFlip, &ppu->m7yFlip, &ppu->m7extBg, &ppu->addSubscreen, &ppu->subtractColor, &ppu->halfColor,
    &ppu->mathEnabled[0], &ppu->mathEnabled[1], &ppu->mathEnabled[2], &ppu->mathEnabled[3], &ppu->mathEnabled[4],
    &ppu->mathEnabled[5], &ppu->forcedBlank, &ppu->bg3priority, &ppu->evenFrame, &ppu->pseudoHires, &ppu->overscan,
    &ppu->frameOverscan, &ppu->interlace, &ppu->frameInterlace, &ppu->directColor, &ppu->hCountSecond, &ppu->vCountSecond,
    &ppu->countersLatched, NULL
  );
  sh_handleBytes(sh,
    &ppu->vramRemapMode, &ppu->cgramPointer, &ppu->cgramBuffer, &ppu->oamAdr, &ppu->oamAdrWritten, &ppu->oamBuffer,
    &ppu->objSize, &ppu->scrollPrev, &ppu->scrollPrev2, &ppu->mosaicSize, &ppu->mosaicStartLine, &ppu->m7prev,
    &ppu->window1left, &ppu->window1right, &ppu->window2left, &ppu->window2right, &ppu->clipMode, &ppu->preventMathMode,
    &ppu->fixedColorR, &ppu->fixedColorG, &ppu->fixedColorB, &ppu->brightness, &ppu->mode,
    &ppu->ppu1openBus, &ppu->ppu2openBus, NULL
  );
  sh_handleWords(sh,
    &ppu->vramPointer, &ppu->vramIncrement, &ppu->vramReadBuffer, &ppu->objTileAdr1, &ppu->objTileAdr2,
    &ppu->hCount, &ppu->vCount, NULL
  );
  sh_handleWordsS(sh,
    &ppu->m7matrix[0], &ppu->m7matrix[1], &ppu->m7matrix[2], &ppu->m7matrix[3], &ppu->m7matrix[4], &ppu->m7matrix[5],
    &ppu->m7matrix[6], &ppu->m7matrix[7], NULL
  );
  sh_handleIntsS(sh, &ppu->m7startX, &ppu->m7startY, NULL);
  for(int i = 0; i < 4; i++) {
    sh_handleBools(sh,
      &ppu->bgLayer[i].tilemapWider, &ppu->bgLayer[i].tilemapHigher, &ppu->bgLayer[i].bigTiles,
      &ppu->bgLayer[i].mosaicEnabled, NULL
    );
    sh_handleWords(sh,
      &ppu->bgLayer[i].hScroll, &ppu->bgLayer[i].vScroll, &ppu->bgLayer[i].tilemapAdr, &ppu->bgLayer[i].tileAdr, NULL
    );
  }
  for(int i = 0; i < 5; i++) {
    sh_handleBools(sh,
      &ppu->layer[i].mainScreenEnabled, &ppu->layer[i].subScreenEnabled, &ppu->layer[i].mainScreenWindowed,
      &ppu->layer[i].subScreenWindowed, NULL
    );
  }
  for(int i = 0; i < 6; i++) {
    sh_handleBools(sh,
      &ppu->windowLayer[i].window1enabled, &ppu->windowLayer[i].window1inversed, &ppu->windowLayer[i].window2enabled,
      &ppu->windowLayer[i].window2inversed, NULL
    );
    sh_handleBytes(sh, &ppu->windowLayer[i].maskLogic, NULL);
  }
  sh_handleWordArray(sh, ppu->vram, 0x8000);
  sh_handleWordArray(sh, ppu->cgram, 0x100);
  sh_handleWordArray(sh, ppu->oam, 0x100);
  sh_handleByteArray(sh, ppu->highOam, 0x20);
  sh_handleByteArray(sh, ppu->objPixelBuffer, 256);
  sh_handleByteArray(sh, ppu->objPriorityBuffer, 256);
}

bool ppu_checkOverscan(Ppu* ppu) {
  // called at (0,225)
  ppu->frameOverscan = ppu->overscan; // set if we have a overscan-frame
  return ppu->frameOverscan;
}

void ppu_handleVblank(Ppu* ppu) {
  // called either right after ppu_checkOverscan at (0,225), or at (0,240)
  if(!ppu->forcedBlank) {
    ppu->oamAdr = ppu->oamAdrWritten;
    ppu->oamInHigh = ppu->oamInHighWritten;
    ppu->oamSecondWrite = false;
  }
  ppu->frameInterlace = ppu->interlace; // set if we have a interlaced frame
}

// The tilemap word covering layer coordinates (x, y) -- the same address
// `ppu_getPixelForBgLayer` works out, without the pixel.
static uint16_t ppu_tilemapWord(const Ppu* ppu, int layer, int x, int y) {
  bool wideTiles = ppu->bgLayer[layer].bigTiles || ppu->mode == 5 || ppu->mode == 6;
  int tileBitsX = wideTiles ? 4 : 3;
  int tileHighBitX = wideTiles ? 0x200 : 0x100;
  int tileBitsY = ppu->bgLayer[layer].bigTiles ? 4 : 3;
  int tileHighBitY = ppu->bgLayer[layer].bigTiles ? 0x200 : 0x100;
  x &= 0x3ff;
  y &= 0x3ff;
  uint16_t adr = ppu->bgLayer[layer].tilemapAdr + (((y >> tileBitsY) & 0x1f) << 5 | ((x >> tileBitsX) & 0x1f));
  if((x & tileHighBitX) && ppu->bgLayer[layer].tilemapWider) adr += 0x400;
  if((y & tileHighBitY) && ppu->bgLayer[layer].tilemapHigher) adr += ppu->bgLayer[layer].tilemapWider ? 0x800 : 0x400;
  return ppu->vram[adr & 0x7fff];
}

// Is this character all zeroes -- eight rows of nothing, whatever palette the
// tilemap word that names it asks for? Colour zero is transparent on a
// background layer, so a character of nothing but zeroes draws nothing.
static bool ppu_charEmpty(const Ppu* ppu, int layer, int tileNum, int bitDepth) {
  const int words = 4 * bitDepth;
  const uint16_t base = (uint16_t)(ppu->bgLayer[layer].tileAdr + (tileNum & 0x3ff) * words);
  for(int i = 0; i < words; i++) {
    if(ppu->vram[(base + i) & 0x7fff] != 0) return false;
  }
  return true;
}

// Does this layer draw anything at all in the tilemap column under screen
// pixel `sx`?
//
// This is the question that decides what a layer does with the margins, and it
// is deliberately about *pixels* rather than about tilemap words. Two different
// blank tiles -- the same character in two palettes -- are two different words
// and the same nothing, and the game uses both: the LucasArts logo layer pads
// its edges with palette 2's blank tile and the legal screen pads its edges
// with palette 7's. A test that compared words would call those layers textured
// and repeat them into the margins, which is how a screenful of legal text came
// to be printed three times.
static bool ppu_columnEmpty(const Ppu* ppu, int layer, int sx) {
  int actMode = ppu->mode == 1 && ppu->bg3priority ? 8 : ppu->mode;
  const int bitDepth = bitDepthsPerMode[actMode][layer];
  // 5 and 7 are the table's markers for a layer this mode does not have.
  if(bitDepth != 2 && bitDepth != 4 && bitDepth != 8) return false;
  const bool big = ppu->bgLayer[layer].bigTiles;
  const int step = big ? 16 : 8;
  const int x = sx + ppu->bgLayer[layer].hScroll;
  const int top = ppu->bgLayer[layer].vScroll;
  // Every tile row the 224-line display touches, plus one for the row the
  // scroll leaves half on screen.
  for(int y = top; y < top + 224 + step; y += step) {
    const int n = ppu_tilemapWord(ppu, layer, x, y) & 0x3ff;
    if(!ppu_charEmpty(ppu, layer, n, bitDepth)) return false;
    // A 16x16 tilemap entry is four characters: the named one, the one after
    // it, and the two a row of sixteen below.
    if(big && (!ppu_charEmpty(ppu, layer, n + 1, bitDepth) ||
               !ppu_charEmpty(ppu, layer, n + 0x10, bitDepth) ||
               !ppu_charEmpty(ppu, layer, n + 0x11, bitDepth))) return false;
  }
  return true;
}

void ppu_handleFrameStart(Ppu* ppu) {
  // called at (0, 0)
  ppu->mosaicStartLine = 1;
  ppu->rangeOver = false;
  ppu->timeOver = false;
  ppu->evenFrame = !ppu->evenFrame;
  ppu->midFrameWrite = false;
  ppu->midFrameWrites = 0;
  ppu->windowRaster = false;
  // Once a frame, and only when there are margins to fill: which of the
  // backgrounds have nothing at the console's edges, and so nothing to say
  // beyond them. Done here because the answer has to hold for the whole frame
  // -- a layer that changed its mind halfway down would tear -- and because by
  // line 0 this frame's tilemap uploads are in, having gone up during the
  // vblank just past.
  // Which layers the game is scrolling. Latched for as long as the screen
  // stands, and cleared when it is taken down -- every screen in this game is
  // built behind a forced blank or a faded-out one -- so the answer is "has
  // this layer moved on this screen" rather than "did it move since last
  // frame". Tracked whether or not there are margins, because the answer has to
  // be right on the first frame after widescreen is switched on.
  for(int i = 0; i < 4; i++) {
    if(ppu->forcedBlank || ppu->brightness == 0) {
      ppu->layerScrolled[i] = 0;
    } else if(ppu->bgLayer[i].hScroll != ppu->lastHScroll[i]) {
      // Horizontally, and only horizontally: it is the vertical seam of the
      // tilemap that the margins would repeat, and a card whose text slides up
      // the screen has never crossed it. The level-name card is exactly that --
      // `hScroll` nailed to 0 while `vScroll` runs -- and reading its motion as
      // permission to wrap printed the end of its last line down both sides.
      ppu->layerScrolled[i] = 1;
    }
    ppu->lastHScroll[i] = ppu->bgLayer[i].hScroll;
    if(ppu->forcedBlank || ppu->brightness == 0) ppu->layerRaster[i] = 0;
    else if(ppu->hScrollWrites[i] >= PPU_RASTER_WRITES) ppu->layerRaster[i] = 1;
    ppu->hScrollWrites[i] = 0;
  }
  if(ppu->extraLeft != 0 || ppu->extraRight != 0) {
    for(int i = 0; i < 4; i++) {
      ppu->layerEdgeEmpty[i] = (ppu->mode != 7 &&
                                ppu->layerWide[i] == ppu_wideAuto &&
                                ppu_columnEmpty(ppu, i, 0) &&
                                ppu_columnEmpty(ppu, i, 255));
    }
  }
}

void ppu_runLine(Ppu* ppu, int line) {
  // called for lines 1-224/239
  // Where the backgrounds are scrolled to as this line is drawn -- the value
  // in force now, whether it was set in vblank or a moment ago by HDMA.
  if(line >= 0 && line < PPU_LINES) {
    for(int i = 0; i < 4; i++) {
      ppu->lineHScroll[i][line] = ppu->bgLayer[i].hScroll;
      ppu->lineVScroll[i][line] = ppu->bgLayer[i].vScroll;
    }
    ppu->lineWindow[line][0] = ppu->window1left;
    ppu->lineWindow[line][1] = ppu->window1right;
    ppu->lineWindow[line][2] = ppu->window2left;
    ppu->lineWindow[line][3] = ppu->window2right;
  }
  // evaluate sprites
  memset(ppu->objPixelBuffer, 0, (size_t)ppu_gameWidth(ppu));
  if(!ppu->forcedBlank) ppu_evaluateSprites(ppu, line - 1);
  // actual line
  if(ppu->mode == 7) ppu_calculateMode7Starts(ppu, line);
  // Widescreen runs the same loop over a wider range of the same coordinate:
  // column 0 is where it always was, and the margins are columns the console
  // computed the scroll for and then had no time to draw.
  for(int x = -ppu->extraLeft; x < 256 + ppu->extraRight; x++) {
    ppu_handlePixel(ppu, x, line);
  }
}

void ppu_setPixelOutputFormat(Ppu* ppu, int pixelOutputFormat) {
  ppu->pixelOutputFormat = pixelOutputFormat;
}

void ppu_renderFrame(Ppu* ppu, const uint16_t (*hScroll)[PPU_LINES],
                     const uint16_t (*vScroll)[PPU_LINES]) {
  // The same lines the machine drew, in the same order, into the same rows:
  // `ppu_handlePixel` picks the half of the buffer from `evenFrame`, which
  // has not changed since the frame started, and `ppu_putPixels` reads the
  // same half back. Sprites are evaluated against the OAM as it stands, so a
  // caller that has moved them sees them moved.
  const int last = ppu->frameOverscan ? 239 : 224;
  for(int line = 1; line <= last; line++) {
    for(int i = 0; i < 4; i++) {
      if(hScroll) ppu->bgLayer[i].hScroll = hScroll[i][line];
      if(vScroll) ppu->bgLayer[i].vScroll = vScroll[i][line];
    }
    ppu_runLine(ppu, line);
  }
}

bool ppu_frameStatic(const Ppu* ppu) {
  return !ppu->midFrameWrite && ppu->mode != 7;
}

int ppu_layerPixel(Ppu* ppu, int layer, int x, int line, bool sub, int* priority) {
  int layerX = x, layerLine = line;
  if(!ppu_wideMapX(ppu, layer, &layerX, &layerLine)) return 0;
  const bool windowed = sub ? ppu->layer[layer].subScreenWindowed
                            : ppu->layer[layer].mainScreenWindowed;
  const int last = ppu->frameOverscan ? 239 : 224;
  const int l = layerLine < 1 ? 1 : layerLine > last ? last : layerLine;
  if(windowed && ppu_getWindowStateOnLine(ppu, layer, layerX, l)) return 0;
  const int lx = layerX + ppu->lineHScroll[layer][l];
  const int ly = layerLine + ppu->lineVScroll[layer][l];
  int pixel = ppu_getPixelForBgLayer(ppu, lx & 0x3ff, ly & 0x3ff, layer, false);
  *priority = 0;
  if(pixel == 0) {
    pixel = ppu_getPixelForBgLayer(ppu, lx & 0x3ff, ly & 0x3ff, layer, true);
    *priority = 1;
  }
  return pixel;
}

int ppu_layerShiftX(const Ppu* ppu, int layer, int line) {
  if(layer < 0 || layer > 3 || ppu->layerWide[layer] != ppu_wideSweep) return 0;
  if(ppu->extraLeft == 0 && ppu->extraRight == 0) return 0;
  // How much of the map has been swept in, of 256.
  const int hs = ppu->lineHScroll[layer][line] & 0x3ff;
  const int in = hs > 256 ? 0 : 256 - hs;
  return (in * (ppu->extraLeft + ppu->extraRight) + 128) / 256 - ppu->extraLeft;
}

bool ppu_columnEmptyAt(const Ppu* ppu, int layer, int sx) {
  return ppu_columnEmpty(ppu, layer, sx);
}

bool ppu_columnFilledAt(const Ppu* ppu, int layer, int sx) {
  if(layer < 0 || layer > 3) return false;
  int actMode = ppu->mode == 1 && ppu->bg3priority ? 8 : ppu->mode;
  const int bitDepth = bitDepthsPerMode[actMode][layer];
  if(bitDepth != 2 && bitDepth != 4 && bitDepth != 8) return false;
  const bool big = ppu->bgLayer[layer].bigTiles;
  const int step = big ? 16 : 8;
  const int x = sx + ppu->bgLayer[layer].hScroll;
  const int top = ppu->bgLayer[layer].vScroll;
  for(int y = top; y < top + 224 + step; y += step) {
    const int n = ppu_tilemapWord(ppu, layer, x, y) & 0x3ff;
    if(ppu_charEmpty(ppu, layer, n, bitDepth)) return false;
  }
  return true;
}

bool ppu_mathAllowedAt(Ppu* ppu, int x, int line) {
  const int last = ppu->frameOverscan ? 239 : 224;
  const int l = line < 1 ? 1 : line > last ? last : line;
  const bool cw = ppu_getWindowStateOnLine(ppu, 5, x, l);
  return !(ppu->preventMathMode == 3 || (ppu->preventMathMode == 2 && cw) ||
           (ppu->preventMathMode == 1 && !cw));
}

bool ppu_clippedAt(Ppu* ppu, int x) {
  const bool cw = ppu_getWindowState(ppu, 5, x);
  return ppu->clipMode == 3 || (ppu->clipMode == 2 && cw) ||
         (ppu->clipMode == 1 && !cw);
}

int ppu_spriteSize(const Ppu* ppu, int slot) {
  const int index = slot * 2;
  return spriteSizes[ppu->objSize][(ppu->highOam[index >> 3] >> ((index & 7) + 1)) & 1];
}

int ppu_spriteXOf(const Ppu* ppu, int slot) {
  return ppu_spriteX(ppu, (uint8_t)(slot * 2));
}

static void ppu_handlePixel(Ppu* ppu, int x, int y) {
  int r = 0, r2 = 0;
  int g = 0, g2 = 0;
  int b = 0, b2 = 0;
  if(!ppu->forcedBlank) {
    int mainLayer = ppu_getPixel(ppu, x, y, false, &r, &g, &b);
    bool colorWindowState = ppu_getWindowState(ppu, 5, x);
    if(
      ppu->clipMode == 3 ||
      (ppu->clipMode == 2 && colorWindowState) ||
      (ppu->clipMode == 1 && !colorWindowState)
    ) {
      r = 0;
      g = 0;
      b = 0;
    }
    int secondLayer = 5; // backdrop
    bool mathEnabled = mainLayer < 6 && ppu->mathEnabled[mainLayer] && !(
      ppu->preventMathMode == 3 ||
      (ppu->preventMathMode == 2 && colorWindowState) ||
      (ppu->preventMathMode == 1 && !colorWindowState)
    );
    if((mathEnabled && ppu->addSubscreen) || ppu->pseudoHires || ppu->mode == 5 || ppu->mode == 6) {
      secondLayer = ppu_getPixel(ppu, x, y, true, &r2, &g2, &b2);
    }
    // TODO: subscreen pixels can be clipped to black as well
    // TODO: math for subscreen pixels (add/sub sub to main)
    if(mathEnabled) {
      if(ppu->subtractColor) {
        r -= (ppu->addSubscreen && secondLayer != 5) ? r2 : ppu->fixedColorR;
        g -= (ppu->addSubscreen && secondLayer != 5) ? g2 : ppu->fixedColorG;
        b -= (ppu->addSubscreen && secondLayer != 5) ? b2 : ppu->fixedColorB;
      } else {
        r += (ppu->addSubscreen && secondLayer != 5) ? r2 : ppu->fixedColorR;
        g += (ppu->addSubscreen && secondLayer != 5) ? g2 : ppu->fixedColorG;
        b += (ppu->addSubscreen && secondLayer != 5) ? b2 : ppu->fixedColorB;
      }
      if(ppu->halfColor && (secondLayer != 5 || !ppu->addSubscreen)) {
        r >>= 1;
        g >>= 1;
        b >>= 1;
      }
      if(r > 31) r = 31;
      if(g > 31) g = 31;
      if(b > 31) b = 31;
      if(r < 0) r = 0;
      if(g < 0) g = 0;
      if(b < 0) b = 0;
    }
    if(!(ppu->pseudoHires || ppu->mode == 5 || ppu->mode == 6)) {
      r2 = r; g2 = g; b2 = b;
    }
  }
  int row = (y - 1) + (ppu->evenFrame ? 0 : 239);
  // The buffer is indexed from the left edge of the *picture*, which widescreen
  // moves left of column 0. At zero margins this is `x` and the row is the
  // 2048 bytes it always was.
  const int col = x + ppu->extraLeft;
  ppu->pixelBuffer[row * PPU_ROW_BYTES + col * 8 + 0 + ppu->pixelOutputFormat] = ((b2 << 3) | (b2 >> 2)) * ppu->brightness / 15;
  ppu->pixelBuffer[row * PPU_ROW_BYTES + col * 8 + 1 + ppu->pixelOutputFormat] = ((g2 << 3) | (g2 >> 2)) * ppu->brightness / 15;
  ppu->pixelBuffer[row * PPU_ROW_BYTES + col * 8 + 2 + ppu->pixelOutputFormat] = ((r2 << 3) | (r2 >> 2)) * ppu->brightness / 15;
  ppu->pixelBuffer[row * PPU_ROW_BYTES + col * 8 + 4 + ppu->pixelOutputFormat] = ((b << 3) | (b >> 2)) * ppu->brightness / 15;
  ppu->pixelBuffer[row * PPU_ROW_BYTES + col * 8 + 5 + ppu->pixelOutputFormat] = ((g << 3) | (g >> 2)) * ppu->brightness / 15;
  ppu->pixelBuffer[row * PPU_ROW_BYTES + col * 8 + 6 + ppu->pixelOutputFormat] = ((r << 3) | (r >> 2)) * ppu->brightness / 15;
}

static int ppu_getPixel(Ppu* ppu, int x, int y, bool sub, int* r, int* g, int* b) {
  // figure out which color is on this location on main- or subscreen, sets it in r, g, b
  // returns which layer it is: 0-3 for bg layer, 4 or 6 for sprites (depending on palette), 5 for backdrop
  int actMode = ppu->mode == 1 && ppu->bg3priority ? 8 : ppu->mode;
  actMode = ppu->mode == 7 && ppu->m7extBg ? 9 : actMode;
  int layer = 5;
  int pixel = 0;
  for(int i = 0; i < layerCountPerMode[actMode]; i++) {
    int curLayer = layersPerMode[actMode][i];
    int curPriority = prioritysPerMode[actMode][i];
    // Which column of this layer belongs at this column of the picture. The
    // two differ only for a layer that does not stretch, and such a layer has
    // columns it declines outright -- the gap in the middle of an anchored one.
    int layerX = x, layerY = y;
    if(!ppu_wideMapX(ppu, curLayer, &layerX, &layerY)) continue;
    bool layerActive = false;
    if(!sub) {
      layerActive = ppu->layer[curLayer].mainScreenEnabled && (
        !ppu->layer[curLayer].mainScreenWindowed || !ppu_getWindowState(ppu, curLayer, layerX)
      );
    } else {
      layerActive = ppu->layer[curLayer].subScreenEnabled && (
        !ppu->layer[curLayer].subScreenWindowed || !ppu_getWindowState(ppu, curLayer, layerX)
      );
    }
    if(layerActive) {
      if(curLayer < 4) {
        // bg layer
        int lx = layerX;
        int ly = layerY;
        if(ppu->bgLayer[curLayer].mosaicEnabled && ppu->mosaicSize > 1) {
          lx -= lx % ppu->mosaicSize;
          ly -= (ly - ppu->mosaicStartLine) % ppu->mosaicSize;
        }
        if(ppu->mode == 7) {
          pixel = ppu_getPixelForMode7(ppu, lx, curLayer, curPriority);
        } else {
          lx += ppu->bgLayer[curLayer].hScroll;
          if(ppu->mode == 5 || ppu->mode == 6) {
            lx *= 2;
            lx += (sub || ppu->bgLayer[curLayer].mosaicEnabled) ? 0 : 1;
            if(ppu->interlace) {
              ly *= 2;
              ly += (ppu->evenFrame || ppu->bgLayer[curLayer].mosaicEnabled) ? 0 : 1;
            }
          }
          ly += ppu->bgLayer[curLayer].vScroll;
          if(ppu->mode == 2 || ppu->mode == 4 || ppu->mode == 6) {
            ppu_handleOPT(ppu, curLayer, &lx, &ly);
          }
          pixel = ppu_getPixelForBgLayer(
            ppu, lx & 0x3ff, ly & 0x3ff,
            curLayer, curPriority
          );
        }
      } else {
        // get a pixel from the sprite buffer
        pixel = 0;
        const int oc = layerX + ppu->extraLeft;
        if(ppu->objPriorityBuffer[oc] == curPriority) pixel = ppu->objPixelBuffer[oc];
      }
    }
    if(pixel > 0) {
      layer = curLayer;
      break;
    }
  }
  if(ppu->directColor && layer < 4 && bitDepthsPerMode[actMode][layer] == 8) {
    *r = ((pixel & 0x7) << 2) | ((pixel & 0x100) >> 7);
    *g = ((pixel & 0x38) >> 1) | ((pixel & 0x200) >> 8);
    *b = ((pixel & 0xc0) >> 3) | ((pixel & 0x400) >> 8);
  } else {
    uint16_t color = ppu->cgram[pixel & 0xff];
    *r = color & 0x1f;
    *g = (color >> 5) & 0x1f;
    *b = (color >> 10) & 0x1f;
  }
  if(layer == 4 && pixel < 0xc0) layer = 6; // sprites with palette color < 0xc0
  return layer;
}

static void ppu_handleOPT(Ppu* ppu, int layer, int* lx, int* ly) {
  int x = *lx;
  int y = *ly;
  int column = 0;
  if(ppu->mode == 6) {
    column = ((x - (x & 0xf)) - ((ppu->bgLayer[layer].hScroll * 2) & 0xfff0)) >> 4;
  } else {
    column = ((x - (x & 0x7)) - (ppu->bgLayer[layer].hScroll & 0xfff8)) >> 3;
  }
  if(column > 0) {
    // fetch offset values from layer 3 tilemap
    int valid = layer == 0 ? 0x2000 : 0x4000;
    uint16_t hOffset = ppu_getOffsetValue(ppu, column - 1, 0);
    uint16_t vOffset = 0;
    if(ppu->mode == 4) {
      if(hOffset & 0x8000) {
        vOffset = hOffset;
        hOffset = 0;
      }
    } else {
      vOffset = ppu_getOffsetValue(ppu, column - 1, 1);
    }
    if(ppu->mode == 6) {
      // TODO: not sure if correct
      if(hOffset & valid) *lx = (((hOffset & 0x3f8) + (column * 8)) * 2) | (x & 0xf);
    } else {
      if(hOffset & valid) *lx = ((hOffset & 0x3f8) + (column * 8)) | (x & 0x7);
    }
    // TODO: not sure if correct for interlace
    if(vOffset & valid) *ly = (vOffset & 0x3ff) + (y - ppu->bgLayer[layer].vScroll);
  }
}

static uint16_t ppu_getOffsetValue(Ppu* ppu, int col, int row) {
  int x = col * 8 + ppu->bgLayer[2].hScroll;
  int y = row * 8 + ppu->bgLayer[2].vScroll;
  int tileBits = ppu->bgLayer[2].bigTiles ? 4 : 3;
  int tileHighBit = ppu->bgLayer[2].bigTiles ? 0x200 : 0x100;
  uint16_t tilemapAdr = ppu->bgLayer[2].tilemapAdr + (((y >> tileBits) & 0x1f) << 5 | ((x >> tileBits) & 0x1f));
  if((x & tileHighBit) && ppu->bgLayer[2].tilemapWider) tilemapAdr += 0x400;
  if((y & tileHighBit) && ppu->bgLayer[2].tilemapHigher) tilemapAdr += ppu->bgLayer[2].tilemapWider ? 0x800 : 0x400;
  return ppu->vram[tilemapAdr & 0x7fff];
}

static int ppu_getPixelForBgLayer(Ppu* ppu, int x, int y, int layer, bool priority) {
  // figure out address of tilemap word and read it
  bool wideTiles = ppu->bgLayer[layer].bigTiles || ppu->mode == 5 || ppu->mode == 6;
  int tileBitsX = wideTiles ? 4 : 3;
  int tileHighBitX = wideTiles ? 0x200 : 0x100;
  int tileBitsY = ppu->bgLayer[layer].bigTiles ? 4 : 3;
  int tileHighBitY = ppu->bgLayer[layer].bigTiles ? 0x200 : 0x100;
  uint16_t tilemapAdr = ppu->bgLayer[layer].tilemapAdr + (((y >> tileBitsY) & 0x1f) << 5 | ((x >> tileBitsX) & 0x1f));
  if((x & tileHighBitX) && ppu->bgLayer[layer].tilemapWider) tilemapAdr += 0x400;
  if((y & tileHighBitY) && ppu->bgLayer[layer].tilemapHigher) tilemapAdr += ppu->bgLayer[layer].tilemapWider ? 0x800 : 0x400;
  uint16_t tile = ppu->vram[tilemapAdr & 0x7fff];
  // check priority, get palette
  if(((bool) (tile & 0x2000)) != priority) return 0; // wrong priority
  int paletteNum = (tile & 0x1c00) >> 10;
  // figure out position within tile
  int row = (tile & 0x8000) ? 7 - (y & 0x7) : (y & 0x7);
  int col = (tile & 0x4000) ? (x & 0x7) : 7 - (x & 0x7);
  int tileNum = tile & 0x3ff;
  if(wideTiles) {
    // if unflipped right half of tile, or flipped left half of tile
    if(((bool) (x & 8)) ^ ((bool) (tile & 0x4000))) tileNum += 1;
  }
  if(ppu->bgLayer[layer].bigTiles) {
    // if unflipped bottom half of tile, or flipped upper half of tile
    if(((bool) (y & 8)) ^ ((bool) (tile & 0x8000))) tileNum += 0x10;
  }
  // read tiledata, ajust palette for mode 0
  int bitDepth = bitDepthsPerMode[ppu->mode][layer];
  if(ppu->mode == 0) paletteNum += 8 * layer;
  // plane 1 (always)
  int paletteSize = 4;
  uint16_t plane1 = ppu->vram[(ppu->bgLayer[layer].tileAdr + ((tileNum & 0x3ff) * 4 * bitDepth) + row) & 0x7fff];
  int pixel = (plane1 >> col) & 1;
  pixel |= ((plane1 >> (8 + col)) & 1) << 1;
  // plane 2 (for 4bpp, 8bpp)
  if(bitDepth > 2) {
    paletteSize = 16;
    uint16_t plane2 = ppu->vram[(ppu->bgLayer[layer].tileAdr + ((tileNum & 0x3ff) * 4 * bitDepth) + 8 + row) & 0x7fff];
    pixel |= ((plane2 >> col) & 1) << 2;
    pixel |= ((plane2 >> (8 + col)) & 1) << 3;
  }
  // plane 3 & 4 (for 8bpp)
  if(bitDepth > 4) {
    paletteSize = 256;
    uint16_t plane3 = ppu->vram[(ppu->bgLayer[layer].tileAdr + ((tileNum & 0x3ff) * 4 * bitDepth) + 16 + row) & 0x7fff];
    pixel |= ((plane3 >> col) & 1) << 4;
    pixel |= ((plane3 >> (8 + col)) & 1) << 5;
    uint16_t plane4 = ppu->vram[(ppu->bgLayer[layer].tileAdr + ((tileNum & 0x3ff) * 4 * bitDepth) + 24 + row) & 0x7fff];
    pixel |= ((plane4 >> col) & 1) << 6;
    pixel |= ((plane4 >> (8 + col)) & 1) << 7;
  }
  // return cgram index, or 0 if transparent, palette number in bits 10-8 for 8-color layers
  return pixel == 0 ? 0 : paletteSize * paletteNum + pixel;
}

static void ppu_calculateMode7Starts(Ppu* ppu, int y) {
  // expand 13-bit values to signed values
  int hScroll = ((int16_t) (ppu->m7matrix[6] << 3)) >> 3;
  int vScroll = ((int16_t) (ppu->m7matrix[7] << 3)) >> 3;
  int xCenter = ((int16_t) (ppu->m7matrix[4] << 3)) >> 3;
  int yCenter = ((int16_t) (ppu->m7matrix[5] << 3)) >> 3;
  // do calculation
  int clippedH = hScroll - xCenter;
  int clippedV = vScroll - yCenter;
  clippedH = (clippedH & 0x2000) ? (clippedH | ~1023) : (clippedH & 1023);
  clippedV = (clippedV & 0x2000) ? (clippedV | ~1023) : (clippedV & 1023);
  if(ppu->bgLayer[0].mosaicEnabled && ppu->mosaicSize > 1) {
    y -= (y - ppu->mosaicStartLine) % ppu->mosaicSize;
  }
  uint8_t ry = ppu->m7yFlip ? 255 - y : y;
  ppu->m7startX = (
    ((ppu->m7matrix[0] * clippedH) & ~63) +
    ((ppu->m7matrix[1] * ry) & ~63) +
    ((ppu->m7matrix[1] * clippedV) & ~63) +
    (xCenter << 8)
  );
  ppu->m7startY = (
    ((ppu->m7matrix[2] * clippedH) & ~63) +
    ((ppu->m7matrix[3] * ry) & ~63) +
    ((ppu->m7matrix[3] * clippedV) & ~63) +
    (yCenter << 8)
  );
}

static int ppu_getPixelForMode7(Ppu* ppu, int x, int layer, bool priority) {
  uint8_t rx = ppu->m7xFlip ? 255 - x : x;
  int xPos = (ppu->m7startX + ppu->m7matrix[0] * rx) >> 8;
  int yPos = (ppu->m7startY + ppu->m7matrix[2] * rx) >> 8;
  bool outsideMap = xPos < 0 || xPos >= 1024 || yPos < 0 || yPos >= 1024;
  xPos &= 0x3ff;
  yPos &= 0x3ff;
  if(!ppu->m7largeField) outsideMap = false;
  uint8_t tile = outsideMap ? 0 : ppu->vram[(yPos >> 3) * 128 + (xPos >> 3)] & 0xff;
  uint8_t pixel = outsideMap && !ppu->m7charFill ? 0 : ppu->vram[tile * 64 + (yPos & 7) * 8 + (xPos & 7)] >> 8;
  if(layer == 1) {
    if(((bool) (pixel & 0x80)) != priority) return 0;
    return pixel & 0x7f;
  }
  return pixel;
}

// One window's range test at a screen column.
//
// Widescreen: a window covering the console's entire line is how a game says
// "everywhere", and everywhere on a wider line is wider. Nothing else moves.
//
// The temptation is to treat each edge separately -- to say that a window
// starting at 0 starts at the left of the screen, whatever its other end is
// doing -- and it is wrong in a way that is easy to ship and hard to see. This
// game turns colour maths off by pointing both windows at the single column
// `0..0` and asking for maths everywhere *outside* them. Read 0 as "the left
// edge" and that degenerate window becomes 43 pixels wide, so the whole left
// margin of every level got the fixed colour subtracted from it and came out a
// shade darker than the picture it was supposed to be continuing.
static bool ppu_windowTest(const Ppu* ppu, int x, int left, int right) {
  if(ppu->extraLeft != 0 || ppu->extraRight != 0) {
    if(left == 0 && right == 255) {
      return x >= -ppu->extraLeft && x <= 255 + ppu->extraRight;
    }
    // ...and a window one column wide sitting on either edge of the console is
    // a window that has been parked. This game switches colour maths off by
    // pointing both of them at column `0..0` and asking for maths inside them,
    // which on the console leaves maths applying to column 0 and nothing else:
    // one column of fixed colour subtracted, at the extreme left of a picture
    // that no television showed the extreme left of. Widen the picture and that
    // column is 43 pixels in from the edge, in plain view, as a dark line down
    // the left of every level. Read as parked it goes away, and nothing else
    // moves: no window the game aims at anything keeps both ends on one column
    // of the screen edge.
    if(left == right && (left == 0 || left == 255)) return false;
    // A window the game aims at something is aimed at the console's 256, and
    // in a level the something is the status panel, which `ppu_wideAnchor`
    // has split and pinned to the two edges of the wider picture. The window
    // goes with it: an edge in the left half is that many columns in from the
    // picture's left edge, one in the right half that many from its right --
    // exactly where the anchored layer's own columns went. Without this the
    // survivor radar's dimmed box sat 43 columns to the right of its frame.
    // Off a level nothing is anchored and nothing is windowed but the parked
    // pair above, so the console's own coordinates stand.
    bool anchored = false;
    for(int l = 0; l < 4; l++) anchored |= ppu->layerWide[l] == ppu_wideAnchor;
    if(anchored) {
      const int l2 = left < 128 ? left - ppu->extraLeft : left + ppu->extraRight;
      const int r2 = right < 128 ? right - ppu->extraLeft : right + ppu->extraRight;
      return x >= l2 && x <= r2;
    }
  }
  return x >= left && x <= right;
}

// Where `layer` reads from, given a column of the picture that may be outside
// the console's 256, and whether it has anything to say there at all. See the
// `ppu_wide*` policies in the header.
static bool ppu_wideMapX(Ppu* ppu, int layer, int* x, int* y) {
  if(ppu->extraLeft == 0 && ppu->extraRight == 0) return true;
  int policy = ppu->layerWide[layer];
  if(policy == ppu_wideAuto) {
    if(layer > 3) {
      // Objects have no tilemap to ask about. In a level they are world things
      // and the margins are simply more of the same world, which is what
      // `ppu_wideStretch` says; on a fixed screen they are composed for the
      // console's 256 and whoever set the policy will have said `ppu_wideClip`.
      policy = ppu_wideStretch;
    } else if(ppu->layerRaster[layer]) {
      // A layer whose horizontal scroll is rewritten many times while the frame
      // is being drawn is not scrolling: it is being drawn a line at a time,
      // and each line is a window on the map at its own offset. Every question
      // below reads one scroll for the whole frame and would answer for the
      // wrong line, so they are not asked. Continuing to read along each line's
      // own map is both the honest answer and the only one that means anything
      // -- see the title logo, which sweeps in on a per-line scroll and is
      // twice as wide as the console.
      policy = ppu_wideStretch;
    } else if(ppu->layerEdgeEmpty[layer]) {
      // Nothing at the edge of the console means nothing beyond it. The margins
      // show whatever is behind this layer, which is what the console's own
      // edge column is already showing.
      policy = ppu_wideClip;
    } else if(ppu->mode != 7 && ppu->bgLayer[layer].tilemapWider) {
      // A 64-column map, of which the game maintains the 32 the console shows:
      // the other 32 hold whatever that VRAM was last used for, and reading
      // them is how a stone wall comes out shredded. Repeat the 32 it does
      // maintain. (A level's world is 64 columns *and* maintained, and is given
      // `ppu_wideStretch` from outside rather than reaching this.)
      policy = ppu_wideTile;
    } else if(ppu->mode == 7 || ppu->bgLayer[layer].bigTiles ||
              ppu->layerScrolled[layer]) {
      // Otherwise draw what the hardware would have drawn if the line were
      // longer, which is honest in three cases and only three. A map of 16x16
      // tiles is 512 pixels across, so the margins are real map the console had
      // no room for. Mode 7 is a transform, defined at any x. And a 256-pixel
      // map the game *scrolls* is one the console already wraps in plain sight,
      // so its seam is one an artist has had to make look right -- the stone
      // wall drifting diagonally behind the LucasArts logo, the wallpaper
      // behind the character select.
      policy = ppu_wideStretch;
    } else {
      // What is left is a 256-pixel map that has not moved since the screen
      // went up. Its seam has never been seen by anyone, and repeating it
      // prints the tail of a line of text down the far side of the picture --
      // which is exactly what the card naming a level did. Clip.
      policy = ppu_wideClip;
    }
  }
  switch(policy) {
    case ppu_wideClip:
      return *x >= 0 && *x < 256;
    case ppu_wideClampEdge:
      if(*x < 0) *x = 0;
      if(*x > 255) *x = 255;
      return true;
    case ppu_wideCentre: {
      // The picture runs from -extraLeft to 255 + extraRight; its middle is
      // half their difference right of the console's, and the layer goes
      // there with it. Rounded toward the left as the picture's own width is.
      *x -= (ppu->extraRight - ppu->extraLeft) / 2;
      if(*x >= 0 && *x <= 255) return true;
      // Beyond the layer's edge. The mask is a solid field with the letters
      // cut out of its upper part and a row of drips hanging from its foot,
      // and its margins are drawn in two ways. Beside the field and the
      // letters the margin is the field: the edge column's pixel, carried out
      // as `ppu_wideClampEdge` carries it, or where that pixel is a gap, the
      // edge column a few lines into the nearest run of opaque pixels at or
      // above -- past a drip's outline, which is one pixel thick and dark.
      // Beside the drips the margin is more drips: the layer's own 256
      // columns repeated, as a 32-tile map repeats on the hardware, so that
      // the curtain goes on to the edge of the picture instead of turning
      // into a slab. A line is beside the drips when it is below the last
      // line on which the layer is opaque all the way across (the foot of
      // the field) and is a gap within 16 columns of both edges -- the
      // letters, which are also below a solid line now and then as the mask
      // scrolls, keep more field than that at both sides. A line with
      // nothing opaque on it at all is below the mask, and shows what is
      // behind. One search per line and side, cached by the line and its
      // scroll; the foot of the field once per scroll.
      const int side = *x < 0 ? 0 : 1;
      const int edge = side ? 255 : 0;
      const int last = ppu->frameOverscan ? 239 : 224;
      const int l = *y < 1 ? 1 : *y > last ? last : *y;
      const uint16_t hs = ppu->lineHScroll[layer][l], vs = ppu->lineVScroll[layer][l];
      if(ppu->centreLastFullH[layer] != hs || ppu->centreLastFullV[layer] != vs ||
         ppu->centreLastFull[layer] == -2) {
        int full = -1;
        for(int r = last; r >= 1 && full < 0; r--) {
          const int ry = (r + vs) & 0x3ff;
          bool solid = true;
          for(int c = 0; c < 256 && solid; c++) {
            const int rx = (c + hs) & 0x3ff;
            solid = ppu_getPixelForBgLayer(ppu, rx, ry, layer, false) ||
                    ppu_getPixelForBgLayer(ppu, rx, ry, layer, true);
          }
          if(solid) full = r;
        }
        ppu->centreLastFull[layer] = (int16_t)full;
        ppu->centreLastFullH[layer] = hs;
        ppu->centreLastFullV[layer] = vs;
      }
      if(ppu->centreFillLine[layer][side] != l || ppu->centreFillH[layer][side] != hs ||
         ppu->centreFillV[layer][side] != vs) {
        const int ly = (l + vs) & 0x3ff;
        // How far in from each edge the line is opaque, and whether it has
        // anything opaque at all.
        int runL = 0, runR = 0, found = -1, from = l;
        while(runL < 256) {
          const int lx = (runL + hs) & 0x3ff;
          if(!(ppu_getPixelForBgLayer(ppu, lx, ly, layer, false) ||
               ppu_getPixelForBgLayer(ppu, lx, ly, layer, true))) break;
          runL++;
        }
        while(runR < 256 - runL) {
          const int lx = (255 - runR + hs) & 0x3ff;
          if(!(ppu_getPixelForBgLayer(ppu, lx, ly, layer, false) ||
               ppu_getPixelForBgLayer(ppu, lx, ly, layer, true))) break;
          runR++;
        }
        if(side ? runR > 0 : runL > 0) found = edge;
        else
          for(int c = side ? 255 : 0, n = 0; n < 256; n++, c += side ? -1 : 1) {
            const int lx = (c + hs) & 0x3ff;
            if(ppu_getPixelForBgLayer(ppu, lx, ly, layer, false) ||
               ppu_getPixelForBgLayer(ppu, lx, ly, layer, true)) { found = c; break; }
          }
        const bool wrap = found >= 0 && l > ppu->centreLastFull[layer] && runL < 16 && runR < 16;
        if(found >= 0 && !wrap) {
          int run = 0;
          for(int r = l; r >= 1 && run < 4; r--) {
            const int rx = (edge + ppu->lineHScroll[layer][r]) & 0x3ff;
            const int ry = (r + ppu->lineVScroll[layer][r]) & 0x3ff;
            if(ppu_getPixelForBgLayer(ppu, rx, ry, layer, false) ||
               ppu_getPixelForBgLayer(ppu, rx, ry, layer, true)) { found = edge; from = r; run++; }
            else if(run) break;
          }
        }
        ppu->centreFillLine[layer][side] = (int16_t)l;
        ppu->centreFillH[layer][side] = hs;
        ppu->centreFillV[layer][side] = vs;
        ppu->centreFillCol[layer][side] = (int16_t)found;
        ppu->centreFillFrom[layer][side] = (int16_t)from;
        ppu->centreFillWrap[layer][side] = wrap;
      }
      if(ppu->centreFillCol[layer][side] < 0) return false;
      if(ppu->centreFillWrap[layer][side]) { *x &= 255; return true; }
      *x = ppu->centreFillCol[layer][side];
      *y = ppu->centreFillFrom[layer][side];
      return true;
    }
    case ppu_wideTile:
      // Two's complement does the wrap for negatives as well, which is the
      // whole reason the console's width is a power of two.
      *x &= 255;
      return true;
    case ppu_wideSweep: {
      const int last = ppu->frameOverscan ? 239 : 224;
      const int l = *y < 1 ? 1 : *y > last ? last : *y;
      *x -= ppu_layerShiftX(ppu, layer, l);
      // Left of the map's first column is more of the first column.
      const int hs = ppu->lineHScroll[layer][l] & 0x3ff;
      if(hs <= 256 && *x + hs < 0) *x = -hs;
      return true;
    }
    case ppu_wideStretch:
      // The only policy the world clamp applies to. A layer that is being
      // continued into the margins can only be continued as far as there is
      // something to continue; past that the picture is outside the map and
      // the backdrop is the honest thing to show.
      return *x >= ppu->wideClampLo && *x <= ppu->wideClampHi;
    case ppu_wideAnchor:
      // The left half keeps its distance from the left edge and the right half
      // its distance from the right, so the two move apart by exactly the width
      // that was added and nothing inside either half is stretched or moved.
      if(*x < 128 - ppu->extraLeft) { *x += ppu->extraLeft; return true; }
      if(*x > 127 + ppu->extraRight) { *x -= ppu->extraRight; return true; }
      return false;
    default:
      return true;
  }
}

static bool ppu_getWindowState(Ppu* ppu, int layer, int x) {
  if(!ppu->windowLayer[layer].window1enabled && !ppu->windowLayer[layer].window2enabled) {
    return false;
  }
  if(ppu->windowLayer[layer].window1enabled && !ppu->windowLayer[layer].window2enabled) {
    bool test = ppu_windowTest(ppu, x, ppu->window1left, ppu->window1right);
    return ppu->windowLayer[layer].window1inversed ? !test : test;
  }
  if(!ppu->windowLayer[layer].window1enabled && ppu->windowLayer[layer].window2enabled) {
    bool test = ppu_windowTest(ppu, x, ppu->window2left, ppu->window2right);
    return ppu->windowLayer[layer].window2inversed ? !test : test;
  }
  bool test1 = ppu_windowTest(ppu, x, ppu->window1left, ppu->window1right);
  bool test2 = ppu_windowTest(ppu, x, ppu->window2left, ppu->window2right);
  if(ppu->windowLayer[layer].window1inversed) test1 = !test1;
  if(ppu->windowLayer[layer].window2inversed) test2 = !test2;
  switch(ppu->windowLayer[layer].maskLogic) {
    case 0: return test1 || test2;
    case 1: return test1 && test2;
    case 2: return test1 != test2;
    case 3: return test1 == test2;
  }
  return false;
}

// The same, with the edges the two windows had on `line` rather than the
// ones they have now -- which differ only in a frame that moved them.
static bool ppu_getWindowStateOnLine(Ppu* ppu, int layer, int x, int line) {
  const uint8_t w1l = ppu->window1left, w1r = ppu->window1right;
  const uint8_t w2l = ppu->window2left, w2r = ppu->window2right;
  ppu->window1left = ppu->lineWindow[line][0];
  ppu->window1right = ppu->lineWindow[line][1];
  ppu->window2left = ppu->lineWindow[line][2];
  ppu->window2right = ppu->lineWindow[line][3];
  const bool state = ppu_getWindowState(ppu, layer, x);
  ppu->window1left = w1l; ppu->window1right = w1r;
  ppu->window2left = w2l; ppu->window2right = w2r;
  return state;
}

// A sprite's X, out of the 8 bits in OAM and the 9th in high OAM.
//
// Nine bits is a range of 512 and the console spends it as -256..255, so an X
// of 256 or more is a sprite hanging off the *left* edge. Widescreen has to
// move that wrap point: with a right margin of 71 the columns 256..326 are now
// on screen, and a sprite there must appear there rather than 512 pixels to the
// left of it. Everything past the widened right edge still wraps, which is what
// keeps a sprite walking off the left edge doing so.
static int ppu_spriteX(const Ppu* ppu, uint8_t index) {
  int x = ppu->oam[index] & 0xff;
  x |= ((ppu->highOam[index >> 3] >> (index & 7)) & 1) << 8;
  const int place = ppu->spritePlace[index >> 1];
  if(place == ppu_spriteCentred) {
    // A sprite laid out over a centred layer: the console's own wrap, since
    // the layer's columns are the console's 256 and the margins repeat
    // them (`ppu_evaluateSprites` draws the copies), then the layer's
    // shift, which is `ppu_wideCentre`'s.
    if(x > 255) x -= 512;
    return x + (ppu->extraRight - ppu->extraLeft) / 2;
  }
  if(x > 255 + ppu->extraRight) x -= 512;
  // An anchored sprite: with the picture widened, where `ppu_wideAnchor`
  // puts a layer's column of the same number.
  if(place == ppu_spriteAnchored && (ppu->extraLeft != 0 || ppu->extraRight != 0))
    x += x < 128 ? -ppu->extraLeft : ppu->extraRight;
  return x;
}

static void ppu_evaluateSprites(Ppu* ppu, int line) {
  // TODO: rectangular sprites, wierdness with sprites at -256
  uint8_t index = ppu->objPriority ? (ppu->oamAdr & 0xfe) : 0;
  int spritesFound = 0;
  int tilesFound = 0;
  const int width = ppu_gameWidth(ppu);
  // The ceilings of 32 sprites and 34 tile slivers a line are a statement about
  // how much fetching time a scanline had, and a wider line is proportionally
  // more of it. Holding them at the console's numbers would make widescreen
  // *drop* sprites the console managed to draw, which is the opposite of what
  // widening is for. At zero margins these are 32 and 34 exactly.
  const int spriteLimit = 32 * width / 256;
  const int tileLimit = 34 * width / 256;
  uint8_t foundSprites[32 * PPU_MAX_WIDTH / 256] = {};
  // ...and for each, how far it is shifted and which picture columns it is
  // clipped to (`foundLo` to `foundHi`, exclusive, in the console's
  // coordinates): a sprite placed with a centred layer is found up to three
  // times, 256 columns apart, each clipped to the layer's columns or to one
  // margin -- see `ppu_spriteCentred`.
  int16_t foundShift[32 * PPU_MAX_WIDTH / 256] = {};
  int16_t foundLo[32 * PPU_MAX_WIDTH / 256] = {};
  int16_t foundHi[32 * PPU_MAX_WIDTH / 256] = {};
  const int mid = (ppu->extraRight - ppu->extraLeft) / 2;
  bool over = false;
  // iterate over oam to find sprites in range
  for(int i = 0; i < 128 && !over; i++) {
    uint8_t y = ppu->oam[index] >> 8;
    // check if the sprite is on this line and get the sprite size
    uint8_t row = line - y;
    int spriteSize = spriteSizes[ppu->objSize][(ppu->highOam[index >> 3] >> ((index & 7) + 1)) & 1];
    int spriteHeight = ppu->objInterlace ? spriteSize / 2 : spriteSize;
    if(row < spriteHeight) {
      // in y-range, get the x location, using the high bit as well
      int x = ppu_spriteX(ppu, index);
      const bool centred = ppu->spritePlace[index >> 1] == ppu_spriteCentred &&
                           (ppu->extraLeft != 0 || ppu->extraRight != 0);
      for(int k = centred ? -1 : 0; k <= (centred ? 1 : 0); k++) {
        const int sx = x + k * 256;
        const int lo = !centred ? -ppu->extraLeft : k < 0 ? -ppu->extraLeft : k > 0 ? mid + 256 : mid;
        const int hi = !centred ? 256 + ppu->extraRight : k < 0 ? mid : k > 0 ? 256 + ppu->extraRight : mid + 256;
        // if in x-range, record
        if(sx + spriteSize <= lo || (centred && sx >= hi)) continue;
        // break if we found 32 sprites already
        spritesFound++;
        if(spritesFound > spriteLimit) {
          ppu->rangeOver = true;
          spritesFound = spriteLimit;
          over = true;
          break;
        }
        foundSprites[spritesFound - 1] = index;
        foundShift[spritesFound - 1] = (int16_t)(k * 256);
        foundLo[spritesFound - 1] = (int16_t)lo;
        foundHi[spritesFound - 1] = (int16_t)hi;
      }
    }
    index += 2;
  }
  // iterate over found sprites backwards to fetch max 34 tile slivers
  for(int i = spritesFound; i > 0; i--) {
    index = foundSprites[i - 1];
    const int lo = foundLo[i - 1], hi = foundHi[i - 1];
    uint8_t y = ppu->oam[index] >> 8;
    uint8_t row = line - y;
    int spriteSize = spriteSizes[ppu->objSize][(ppu->highOam[index >> 3] >> ((index & 7) + 1)) & 1];
    int x = ppu_spriteX(ppu, index) + foundShift[i - 1];
    if(x > -spriteSize - ppu->extraLeft) {
      // update row according to obj-interlace
      if(ppu->objInterlace) row = row * 2 + (ppu->evenFrame ? 0 : 1);
      // get some data for the sprite and y-flip row if needed
      int tile = ppu->oam[index + 1] & 0xff;
      int palette = (ppu->oam[index + 1] & 0xe00) >> 9;
      bool hFlipped = ppu->oam[index + 1] & 0x4000;
      if(ppu->oam[index + 1] & 0x8000) row = spriteSize - 1 - row;
      // fetch all tiles in x-range
      for(int col = 0; col < spriteSize; col += 8) {
        if(col + x > -8 - ppu->extraLeft && col + x < 256 + ppu->extraRight) {
          // break if we found > 34 8*1 slivers already
          tilesFound++;
          if(tilesFound > tileLimit) {
            ppu->timeOver = true;
            break;
          }
          // figure out which tile this uses, looping within 16x16 pages, and get it's data
          int usedCol = hFlipped ? spriteSize - 1 - col : col;
          uint8_t usedTile = (((tile >> 4) + (row / 8)) << 4) | (((tile & 0xf) + (usedCol / 8)) & 0xf);
          uint16_t objAdr = (ppu->oam[index + 1] & 0x100) ? ppu->objTileAdr2 : ppu->objTileAdr1;
          uint16_t plane1 = ppu->vram[(objAdr + usedTile * 16 + (row & 0x7)) & 0x7fff];
          uint16_t plane2 = ppu->vram[(objAdr + usedTile * 16 + 8 + (row & 0x7)) & 0x7fff];
          // go over each pixel
          for(int px = 0; px < 8; px++) {
            int shift = hFlipped ? px : 7 - px;
            int pixel = (plane1 >> shift) & 1;
            pixel |= ((plane1 >> (8 + shift)) & 1) << 1;
            pixel |= ((plane2 >> shift) & 1) << 2;
            pixel |= ((plane2 >> (8 + shift)) & 1) << 3;
            // draw it in the buffer if there is a pixel here, and inside
            // the clip
            int screenCol = col + x + px + ppu->extraLeft;
            if(pixel > 0 && screenCol >= 0 && screenCol < width && col + x + px >= lo &&
               col + x + px < hi) {
              // zamn: `objRemap`, a sprite in colours of the frontend's
              ppu->objPixelBuffer[screenCol] = ppu->objRemapOn[index >> 1] && ppu->objRemap[pixel]
                  ? ppu->objRemap[pixel] : 0x80 + 16 * palette + pixel;
              ppu->objPriorityBuffer[screenCol] = (ppu->oam[index + 1] & 0x3000) >> 12;
            }
          }
        }
      }
      if(tilesFound > tileLimit) break; // break out of sprite-loop if max tiles found
    }
  }
}

static uint16_t ppu_getVramRemap(Ppu* ppu) {
  uint16_t adr = ppu->vramPointer;
  switch(ppu->vramRemapMode) {
    case 0: return adr;
    case 1: return (adr & 0xff00) | ((adr & 0xe0) >> 5) | ((adr & 0x1f) << 3);
    case 2: return (adr & 0xfe00) | ((adr & 0x1c0) >> 6) | ((adr & 0x3f) << 3);
    case 3: return (adr & 0xfc00) | ((adr & 0x380) >> 7) | ((adr & 0x7f) << 3);
  }
  return adr;
}

uint8_t ppu_read(Ppu* ppu, uint8_t adr) {
  switch(adr) {
    case 0x04: case 0x14: case 0x24:
    case 0x05: case 0x15: case 0x25:
    case 0x06: case 0x16: case 0x26:
    case 0x08: case 0x18: case 0x28:
    case 0x09: case 0x19: case 0x29:
    case 0x0a: case 0x1a: case 0x2a: {
      return ppu->ppu1openBus;
    }
    case 0x34:
    case 0x35:
    case 0x36: {
      int result = ppu->m7matrix[0] * (ppu->m7matrix[1] >> 8);
      ppu->ppu1openBus = (result >> (8 * (adr - 0x34))) & 0xff;
      return ppu->ppu1openBus;
    }
    case 0x37: {
      // TODO: only when ppulatch is set
      ppu->hCount = ppu->snes->hPos / 4;
      ppu->vCount = ppu->snes->vPos;
      ppu->countersLatched = true;
      return ppu->snes->openBus;
    }
    case 0x38: {
      uint8_t ret = 0;
      if(ppu->oamInHigh) {
        ret = ppu->highOam[((ppu->oamAdr & 0xf) << 1) | ppu->oamSecondWrite];
        if(ppu->oamSecondWrite) {
          ppu->oamAdr++;
          if(ppu->oamAdr == 0) ppu->oamInHigh = false;
        }
      } else {
        if(!ppu->oamSecondWrite) {
          ret = ppu->oam[ppu->oamAdr] & 0xff;
        } else {
          ret = ppu->oam[ppu->oamAdr++] >> 8;
          if(ppu->oamAdr == 0) ppu->oamInHigh = true;
        }
      }
      ppu->oamSecondWrite = !ppu->oamSecondWrite;
      ppu->ppu1openBus = ret;
      return ret;
    }
    case 0x39: {
      uint16_t val = ppu->vramReadBuffer;
      if(!ppu->vramIncrementOnHigh) {
        ppu->vramReadBuffer = ppu->vram[ppu_getVramRemap(ppu) & 0x7fff];
        ppu->vramPointer += ppu->vramIncrement;
      }
      ppu->ppu1openBus = val & 0xff;
      return val & 0xff;
    }
    case 0x3a: {
      uint16_t val = ppu->vramReadBuffer;
      if(ppu->vramIncrementOnHigh) {
        ppu->vramReadBuffer = ppu->vram[ppu_getVramRemap(ppu) & 0x7fff];
        ppu->vramPointer += ppu->vramIncrement;
      }
      ppu->ppu1openBus = val >> 8;
      return val >> 8;
    }
    case 0x3b: {
      uint8_t ret = 0;
      if(!ppu->cgramSecondWrite) {
        ret = ppu->cgram[ppu->cgramPointer] & 0xff;
      } else {
        ret = ((ppu->cgram[ppu->cgramPointer++] >> 8) & 0x7f) | (ppu->ppu2openBus & 0x80);
      }
      ppu->cgramSecondWrite = !ppu->cgramSecondWrite;
      ppu->ppu2openBus = ret;
      return ret;
    }
    case 0x3c: {
      uint8_t val = 0;
      if(ppu->hCountSecond) {
        val = ((ppu->hCount >> 8) & 1) | (ppu->ppu2openBus & 0xfe);
      } else {
        val = ppu->hCount & 0xff;
      }
      ppu->hCountSecond = !ppu->hCountSecond;
      ppu->ppu2openBus = val;
      return val;
    }
    case 0x3d: {
      uint8_t val = 0;
      if(ppu->vCountSecond) {
        val = ((ppu->vCount >> 8) & 1) | (ppu->ppu2openBus & 0xfe);
      } else {
        val = ppu->vCount & 0xff;
      }
      ppu->vCountSecond = !ppu->vCountSecond;
      ppu->ppu2openBus = val;
      return val;
    }
    case 0x3e: {
      uint8_t val = 0x1; // ppu1 version (4 bit)
      val |= ppu->ppu1openBus & 0x10;
      val |= ppu->rangeOver << 6;
      val |= ppu->timeOver << 7;
      ppu->ppu1openBus = val;
      return val;
    }
    case 0x3f: {
      uint8_t val = 0x3; // ppu2 version (4 bit)
      val |= ppu->snes->palTiming << 4; // ntsc/pal
      val |= ppu->ppu2openBus & 0x20;
      val |= ppu->countersLatched << 6;
      val |= ppu->evenFrame << 7;
      ppu->countersLatched = false; // TODO: only when ppulatch is set
      ppu->hCountSecond = false;
      ppu->vCountSecond = false;
      ppu->ppu2openBus = val;
      return val;
    }
    default: {
      return ppu->snes->openBus;
    }
  }
}

void ppu_write(Ppu* ppu, uint8_t adr, uint8_t val) {
  // A write while a line is being drawn -- the same test `snes_runCycle`
  // uses to decide whether a line is -- to anything but a scroll register
  // ($210D-$2114, which are recorded per line). See `midFrameWrite`.
  if(!ppu->snes->inVblank && ppu->snes->vPos > 0 && adr >= 0x26 && adr <= 0x29)
    ppu->windowRaster = true;
  else if(!ppu->snes->inVblank && ppu->snes->vPos > 0 && (adr < 0x0d || adr > 0x14)) {
    if(!ppu->midFrameWrite) {
      ppu->midFrameAdr = adr;
      ppu->midFrameLine = ppu->snes->vPos;
      ppu->midFrameWrites = 0;
    }
    ppu->midFrameWrite = true;
    ppu->midFrameWrites++;
  }
  switch(adr) {
    case 0x00: {
      // TODO: oam address reset when written on first line of vblank, (and when forced blank is disabled?)
      ppu->brightness = val & 0xf;
      ppu->forcedBlank = val & 0x80;
      break;
    }
    case 0x01: {
      ppu->objSize = val >> 5;
      ppu->objTileAdr1 = (val & 7) << 13;
      ppu->objTileAdr2 = ppu->objTileAdr1 + (((val & 0x18) + 8) << 9);
      break;
    }
    case 0x02: {
      ppu->oamAdr = val;
      ppu->oamAdrWritten = ppu->oamAdr;
      ppu->oamInHigh = ppu->oamInHighWritten;
      ppu->oamSecondWrite = false;
      break;
    }
    case 0x03: {
      ppu->objPriority = val & 0x80;
      ppu->oamInHigh = val & 1;
      ppu->oamInHighWritten = ppu->oamInHigh;
      ppu->oamAdr = ppu->oamAdrWritten;
      ppu->oamSecondWrite = false;
      break;
    }
    case 0x04: {
      if(ppu->oamInHigh) {
        ppu->highOam[((ppu->oamAdr & 0xf) << 1) | ppu->oamSecondWrite] = val;
        if(ppu->oamSecondWrite) {
          ppu->oamAdr++;
          if(ppu->oamAdr == 0) ppu->oamInHigh = false;
        }
      } else {
        if(!ppu->oamSecondWrite) {
          ppu->oamBuffer = val;
        } else {
          ppu->oam[ppu->oamAdr++] = (val << 8) | ppu->oamBuffer;
          if(ppu->oamAdr == 0) ppu->oamInHigh = true;
        }
      }
      ppu->oamSecondWrite = !ppu->oamSecondWrite;
      break;
    }
    case 0x05: {
      ppu->mode = val & 0x7;
      ppu->bg3priority = val & 0x8;
      ppu->bgLayer[0].bigTiles = val & 0x10;
      ppu->bgLayer[1].bigTiles = val & 0x20;
      ppu->bgLayer[2].bigTiles = val & 0x40;
      ppu->bgLayer[3].bigTiles = val & 0x80;
      break;
    }
    case 0x06: {
      // TODO: mosaic line reset specifics
      ppu->bgLayer[0].mosaicEnabled = val & 0x1;
      ppu->bgLayer[1].mosaicEnabled = val & 0x2;
      ppu->bgLayer[2].mosaicEnabled = val & 0x4;
      ppu->bgLayer[3].mosaicEnabled = val & 0x8;
      ppu->mosaicSize = (val >> 4) + 1;
      ppu->mosaicStartLine = ppu->snes->vPos;
      break;
    }
    case 0x07:
    case 0x08:
    case 0x09:
    case 0x0a: {
      ppu->bgLayer[adr - 7].tilemapWider = val & 0x1;
      ppu->bgLayer[adr - 7].tilemapHigher = val & 0x2;
      ppu->bgLayer[adr - 7].tilemapAdr = (val & 0xfc) << 8;
      break;
    }
    case 0x0b: {
      ppu->bgLayer[0].tileAdr = (val & 0xf) << 12;
      ppu->bgLayer[1].tileAdr = (val & 0xf0) << 8;
      break;
    }
    case 0x0c: {
      ppu->bgLayer[2].tileAdr = (val & 0xf) << 12;
      ppu->bgLayer[3].tileAdr = (val & 0xf0) << 8;
      break;
    }
    case 0x0d: {
      ppu->m7matrix[6] = ((val << 8) | ppu->m7prev) & 0x1fff;
      ppu->m7prev = val;
      // fallthrough to normal layer BG-HOFS
    }
    case 0x0f:
    case 0x11:
    case 0x13: {
      // Counted as well as stored, because how *often* within one frame a
      // background's horizontal scroll changes is the difference between a
      // layer that is scrolling and a layer being drawn a line at a time. See
      // `layerRaster`.
      const int hsLayer = (adr - 0xd) / 2;
      const uint16_t hsNew = ((val << 8) | (ppu->scrollPrev & 0xf8) | (ppu->scrollPrev2 & 0x7)) & 0x3ff;
      if(hsNew != ppu->bgLayer[hsLayer].hScroll && ppu->hScrollWrites[hsLayer] < 255)
        ppu->hScrollWrites[hsLayer]++;
      ppu->bgLayer[hsLayer].hScroll = hsNew;
      ppu->scrollPrev = val;
      ppu->scrollPrev2 = val;
      break;
    }
    case 0x0e: {
      ppu->m7matrix[7] = ((val << 8) | ppu->m7prev) & 0x1fff;
      ppu->m7prev = val;
      // fallthrough to normal layer BG-VOFS
    }
    case 0x10:
    case 0x12:
    case 0x14: {
      ppu->bgLayer[(adr - 0xe) / 2].vScroll = ((val << 8) | ppu->scrollPrev) & 0x3ff;
      ppu->scrollPrev = val;
      break;
    }
    case 0x15: {
      if((val & 3) == 0) {
        ppu->vramIncrement = 1;
      } else if((val & 3) == 1) {
        ppu->vramIncrement = 32;
      } else {
        ppu->vramIncrement = 128;
      }
      ppu->vramRemapMode = (val & 0xc) >> 2;
      ppu->vramIncrementOnHigh = val & 0x80;
      break;
    }
    case 0x16: {
      ppu->vramPointer = (ppu->vramPointer & 0xff00) | val;
      ppu->vramReadBuffer = ppu->vram[ppu_getVramRemap(ppu) & 0x7fff];
      break;
    }
    case 0x17: {
      ppu->vramPointer = (ppu->vramPointer & 0x00ff) | (val << 8);
      ppu->vramReadBuffer = ppu->vram[ppu_getVramRemap(ppu) & 0x7fff];
      break;
    }
    case 0x18: {
      // TODO: vram access during rendering (also cgram and oam)
      uint16_t vramAdr = ppu_getVramRemap(ppu);
      ppu->vram[vramAdr & 0x7fff] = (ppu->vram[vramAdr & 0x7fff] & 0xff00) | val;
      if(!ppu->vramIncrementOnHigh) ppu->vramPointer += ppu->vramIncrement;
      break;
    }
    case 0x19: {
      uint16_t vramAdr = ppu_getVramRemap(ppu);
      ppu->vram[vramAdr & 0x7fff] = (ppu->vram[vramAdr & 0x7fff] & 0x00ff) | (val << 8);
      if(ppu->vramIncrementOnHigh) ppu->vramPointer += ppu->vramIncrement;
      break;
    }
    case 0x1a: {
      ppu->m7largeField = val & 0x80;
      ppu->m7charFill = val & 0x40;
      ppu->m7yFlip = val & 0x2;
      ppu->m7xFlip = val & 0x1;
      break;
    }
    case 0x1b:
    case 0x1c:
    case 0x1d:
    case 0x1e: {
      ppu->m7matrix[adr - 0x1b] = (val << 8) | ppu->m7prev;
      ppu->m7prev = val;
      break;
    }
    case 0x1f:
    case 0x20: {
      ppu->m7matrix[adr - 0x1b] = ((val << 8) | ppu->m7prev) & 0x1fff;
      ppu->m7prev = val;
      break;
    }
    case 0x21: {
      ppu->cgramPointer = val;
      ppu->cgramSecondWrite = false;
      break;
    }
    case 0x22: {
      if(!ppu->cgramSecondWrite) {
        ppu->cgramBuffer = val;
      } else {
        ppu->cgram[ppu->cgramPointer++] = (val << 8) | ppu->cgramBuffer;
      }
      ppu->cgramSecondWrite = !ppu->cgramSecondWrite;
      break;
    }
    case 0x23:
    case 0x24:
    case 0x25: {
      ppu->windowLayer[(adr - 0x23) * 2].window1inversed = val & 0x1;
      ppu->windowLayer[(adr - 0x23) * 2].window1enabled = val & 0x2;
      ppu->windowLayer[(adr - 0x23) * 2].window2inversed = val & 0x4;
      ppu->windowLayer[(adr - 0x23) * 2].window2enabled = val & 0x8;
      ppu->windowLayer[(adr - 0x23) * 2 + 1].window1inversed = val & 0x10;
      ppu->windowLayer[(adr - 0x23) * 2 + 1].window1enabled = val & 0x20;
      ppu->windowLayer[(adr - 0x23) * 2 + 1].window2inversed = val & 0x40;
      ppu->windowLayer[(adr - 0x23) * 2 + 1].window2enabled = val & 0x80;
      break;
    }
    case 0x26: {
      ppu->window1left = val;
      break;
    }
    case 0x27: {
      ppu->window1right = val;
      break;
    }
    case 0x28: {
      ppu->window2left = val;
      break;
    }
    case 0x29: {
      ppu->window2right = val;
      break;
    }
    case 0x2a: {
      ppu->windowLayer[0].maskLogic = val & 0x3;
      ppu->windowLayer[1].maskLogic = (val >> 2) & 0x3;
      ppu->windowLayer[2].maskLogic = (val >> 4) & 0x3;
      ppu->windowLayer[3].maskLogic = (val >> 6) & 0x3;
      break;
    }
    case 0x2b: {
      ppu->windowLayer[4].maskLogic = val & 0x3;
      ppu->windowLayer[5].maskLogic = (val >> 2) & 0x3;
      break;
    }
    case 0x2c: {
      ppu->layer[0].mainScreenEnabled = val & 0x1;
      ppu->layer[1].mainScreenEnabled = val & 0x2;
      ppu->layer[2].mainScreenEnabled = val & 0x4;
      ppu->layer[3].mainScreenEnabled = val & 0x8;
      ppu->layer[4].mainScreenEnabled = val & 0x10;
      break;
    }
    case 0x2d: {
      ppu->layer[0].subScreenEnabled = val & 0x1;
      ppu->layer[1].subScreenEnabled = val & 0x2;
      ppu->layer[2].subScreenEnabled = val & 0x4;
      ppu->layer[3].subScreenEnabled = val & 0x8;
      ppu->layer[4].subScreenEnabled = val & 0x10;
      break;
    }
    case 0x2e: {
      ppu->layer[0].mainScreenWindowed = val & 0x1;
      ppu->layer[1].mainScreenWindowed = val & 0x2;
      ppu->layer[2].mainScreenWindowed = val & 0x4;
      ppu->layer[3].mainScreenWindowed = val & 0x8;
      ppu->layer[4].mainScreenWindowed = val & 0x10;
      break;
    }
    case 0x2f: {
      ppu->layer[0].subScreenWindowed = val & 0x1;
      ppu->layer[1].subScreenWindowed = val & 0x2;
      ppu->layer[2].subScreenWindowed = val & 0x4;
      ppu->layer[3].subScreenWindowed = val & 0x8;
      ppu->layer[4].subScreenWindowed = val & 0x10;
      break;
    }
    case 0x30: {
      ppu->directColor = val & 0x1;
      ppu->addSubscreen = val & 0x2;
      ppu->preventMathMode = (val & 0x30) >> 4;
      ppu->clipMode = (val & 0xc0) >> 6;
      break;
    }
    case 0x31: {
      ppu->subtractColor = val & 0x80;
      ppu->halfColor = val & 0x40;
      for(int i = 0; i < 6; i++) {
        ppu->mathEnabled[i] = val & (1 << i);
      }
      break;
    }
    case 0x32: {
      if(val & 0x80) ppu->fixedColorB = val & 0x1f;
      if(val & 0x40) ppu->fixedColorG = val & 0x1f;
      if(val & 0x20) ppu->fixedColorR = val & 0x1f;
      break;
    }
    case 0x33: {
      ppu->interlace = val & 0x1;
      ppu->objInterlace = val & 0x2;
      ppu->overscan = val & 0x4;
      ppu->pseudoHires = val & 0x8;
      ppu->m7extBg = val & 0x40;
      break;
    }
    default: {
      break;
    }
  }
}

int ppu_gameWidth(const Ppu* ppu) {
  return 256 + ppu->extraLeft + ppu->extraRight;
}

int ppu_outputWidth(const Ppu* ppu) {
  return ppu_gameWidth(ppu) * 2;
}

void ppu_setWidescreen(Ppu* ppu, int left, int right) {
  if(left < 0) left = 0;
  if(right < 0) right = 0;
  if(left > PPU_EXTRA_MAX) left = PPU_EXTRA_MAX;
  if(right > PPU_EXTRA_MAX) right = PPU_EXTRA_MAX;
  ppu->extraLeft = left;
  ppu->extraRight = right;
}

void ppu_setLayerWide(Ppu* ppu, int layer, int policy) {
  if(layer < 0 || layer > 4) return;
  ppu->layerWide[layer] = (uint8_t) policy;
}

bool ppu_bgTilemapWider(const Ppu* ppu, int layer) {
  if(layer < 0 || layer > 3) return false;
  return ppu->bgLayer[layer].tilemapWider;
}

bool ppu_bgOnMainScreen(const Ppu* ppu, int layer) {
  if(layer < 0 || layer > 3) return false;
  return ppu->layer[layer].mainScreenEnabled;
}

void ppu_setSprite(Ppu* ppu, int slot, int x, int y, uint16_t tileAttr,
                   bool large) {
  if(slot < 0 || slot >= 128) return;
  const int i = slot * 2;
  ppu->oam[i] = (uint16_t)(((y & 0xff) << 8) | (x & 0xff));
  ppu->oam[i + 1] = tileAttr;
  const int bit = i & 7;
  const uint8_t mask = (uint8_t)(3 << bit);
  const uint8_t val = (uint8_t)((((x >> 8) & 1) << bit) | ((large ? 1 : 0) << (bit + 1)));
  ppu->highOam[i >> 3] = (uint8_t)((ppu->highOam[i >> 3] & ~mask) | val);
}

int ppu_freeSprite(const Ppu* ppu, int from) {
  for(int slot = from; slot < 128; slot++) {
    const int y = ppu->oam[slot * 2] >> 8;
    // $E0..$F0 is the gap between the bottom of the screen and the rows a
    // sprite hanging off the top would use, so nothing the game draws lands
    // there and anything that does is parked.
    if(y >= 0xe0 && y <= 0xf0) return slot;
  }
  return 128;
}

void ppu_setWideClamp(Ppu* ppu, int lo, int hi) {
  ppu->wideClampLo = lo;
  ppu->wideClampHi = hi;
}

void ppu_writeVramWord(Ppu* ppu, uint16_t wordAdr, uint16_t val) {
  ppu->vram[wordAdr & 0x7fff] = val;
}

void ppu_putPixels(Ppu* ppu, uint8_t* pixels) {
  // The destination is packed at whatever width the picture currently is, and
  // the source row starts at its own byte 0 -- `ppu_handlePixel` already shifted
  // the leftmost column there. At zero margins both pitches are 2048.
  const int dpitch = ppu_outputWidth(ppu) * 4;
  for(int y = 0; y < (ppu->frameOverscan ? 239 : 224); y++) {
    int dest = y * 2 + (ppu->frameOverscan ? 2 : 16);
    int y1 = y, y2 = y + 239;
    if(!ppu->frameInterlace) {
      y1 = y + (ppu->evenFrame ? 0 : 239);
      y2 = y1;
    }
    memcpy(pixels + (dest * dpitch), &ppu->pixelBuffer[y1 * PPU_ROW_BYTES], dpitch);
    memcpy(pixels + ((dest + 1) * dpitch), &ppu->pixelBuffer[y2 * PPU_ROW_BYTES], dpitch);
  }
  // clear top 2 lines, and following 14 and last 16 lines if not overscanning
  memset(pixels, 0, dpitch * 2);
  if(!ppu->frameOverscan) {
    memset(pixels + (2 * dpitch), 0, dpitch * 14);
    memset(pixels + (464 * dpitch), 0, dpitch * 16);
  }
}
