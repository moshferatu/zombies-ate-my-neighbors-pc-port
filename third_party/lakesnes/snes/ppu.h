
#ifndef PPU_H
#define PPU_H

#include <stdint.h>
#include <stdbool.h>

typedef struct Ppu Ppu;

#include "snes.h"
#include "statehandler.h"

// --- Widescreen ------------------------------------------------------- local
//
// The console draws 256 pixels across a scanline. `extraLeft` and `extraRight`
// are how many more this PPU draws either side of those, so a line runs from
// `-extraLeft` to `255 + extraRight`. The game's coordinate system does not
// move: column 0 is still column 0, scroll registers still mean what they
// meant, and what appears in the margins appears because it was always there
// and the hardware had nowhere to put it.
//
// Both zero is the console, and is the default. Every expression below reduces
// to the original one at zero -- the margins only ever enter as `+ 0` or as a
// loop bound that is still 256 -- which is what lets the co-simulation corpus
// go on meaning what it meant.
#define PPU_EXTRA_MAX 128
#define PPU_MAX_WIDTH (256 + 2 * PPU_EXTRA_MAX)
// One row of the pixel buffer, in bytes. Every game pixel is written as two
// side-by-side output pixels of four bytes, because `ppu_handlePixel` fills a
// hi-res row whether or not the mode is hi-res. So this is 8 bytes a game
// pixel, and at zero margins the first 2048 of them are the row this core
// always had.
#define PPU_ROW_BYTES (PPU_MAX_WIDTH * 8)

// How many times a background's horizontal scroll has to change within one
// frame before the layer is read as a raster effect rather than as a scroll.
// Measured over the whole intro and a level: an ordinary layer changes it at
// most four times a frame, and the title logo's per-line sweep changes it on
// every one of the 224 lines. Anywhere in that gap does; this is the middle.
#define PPU_RASTER_WRITES 16

// What a layer does with the margins. The mechanism is here; which layer is
// which is the game's business and is set from outside.
enum {
  // Work it out from the layer, every frame, with no list of screens anywhere.
  //
  // The default answer is the honest one: draw what the hardware would have
  // drawn if the scanline were longer. A background is a tilemap and a scroll,
  // both of which are defined at any x; the console stops at 256 because it
  // runs out of time, not because the map runs out. So `ppu_wideAuto` means
  // `ppu_wideStretch` unless one of two things is true of the layer.
  //
  // The first is that it has *nothing* at the console's edges. A layer whose
  // outermost columns are entirely transparent is a picture that was composed
  // to be seen at one place -- a logo, a card, a screenful of legal text -- and
  // the margins beside it belong to whatever is behind it, which is what the
  // console's own edge column is already showing. That is `ppu_wideClip`, and
  // it is measured in pixels rather than in tilemap words: see
  // `ppu_columnEmpty`.
  //
  // The second is that the map is 64 columns wide while the game maintains only
  // the 32 the console shows. Reading further along that map is reading
  // whatever the VRAM was last used for, which is how a stone wall comes out
  // shredded, so those repeat the 256 columns the game does maintain --
  // `ppu_wideTile`. A layer whose wider map *is* maintained, like a level's
  // scrolling world, is given `ppu_wideStretch` from outside, because for that
  // one the answer is known rather than guessed.
  //
  // The third is a 256-pixel map -- 32 columns of 8-pixel tiles, the width of
  // the console exactly -- that the game has never scrolled sideways. Stretching
  // such a map does not read further along it; there is no further along, so it
  // wraps, and the margins fill with the far end of the same screen. That is
  // right for a layer the game *does* scroll, because then the console is
  // already wrapping it in plain sight and its seam is one an artist has had to
  // make look right: the stone wall drifting behind the LucasArts logo, the
  // wallpaper behind the character select. It is wrong for a layer that has sat
  // still since the screen went up, whose seam nobody has ever seen, and doing
  // it anyway prints the tail of a line of text down the far side of the
  // picture -- which is what the card naming a level did. Those are clipped.
  // See `layerScrolled`: latched per screen rather than tested per frame, so a
  // wall that moves a pixel every fourth frame does not flicker between the two
  // answers, and cleared when the screen is taken down behind a forced blank.
  ppu_wideAuto = 0,
  // Draw it out there the way it is drawn anywhere else. Correct for anything
  // the world scrolls -- the tilemap simply continues.
  ppu_wideStretch,
  // A 256-wide layer that is furniture rather than world: split it down the
  // middle, pin columns 0-127 to the left edge of the widened picture and
  // 128-255 to the right edge, and leave the gap between them transparent. A
  // two-player status panel laid out as two half-width halves lands one half on
  // each edge, which is where a widescreen game would have put them.
  ppu_wideAnchor,
  // Confine it to the console's 256 and let the margins show whatever is
  // behind. The honest answer for a layer whose content stops at the edge.
  ppu_wideClip,
  // Carry the outermost column of the picture out to the edges. Where that
  // column is empty this is `ppu_wideClip` -- a text card on a black field
  // keeps its black -- and where it is not, the margin is that colour rather
  // than a hard cut to the backdrop, which is what a logo centred on a solid
  // panel wants.
  ppu_wideClampEdge,
  // Repeat the console's 256 columns outward, so the margins show the same
  // field again. For a 32-tile tilemap this is what the hardware does anyway --
  // its map wraps every 256 pixels -- and for a wider one it is the point:
  // a 64-column tilemap with only 32 columns ever written has 32 columns of
  // whatever was last in that VRAM, and reading those is how a stone wall comes
  // out shredded. This can only ever show columns the game has actually drawn.
  ppu_wideTile,
};

typedef struct BgLayer {
  uint16_t hScroll;
  uint16_t vScroll;
  bool tilemapWider;
  bool tilemapHigher;
  uint16_t tilemapAdr;
  uint16_t tileAdr;
  bool bigTiles;
  bool mosaicEnabled;
} BgLayer;

typedef struct Layer {
  bool mainScreenEnabled;
  bool subScreenEnabled;
  bool mainScreenWindowed;
  bool subScreenWindowed;
} Layer;

typedef struct WindowLayer {
  bool window1enabled;
  bool window2enabled;
  bool window1inversed;
  bool window2inversed;
  uint8_t maskLogic;
} WindowLayer;

struct Ppu {
  Snes* snes;
  // vram access
  uint16_t vram[0x8000];
  uint16_t vramPointer;
  bool vramIncrementOnHigh;
  uint16_t vramIncrement;
  uint8_t vramRemapMode;
  uint16_t vramReadBuffer;
  // cgram access
  uint16_t cgram[0x100];
  uint8_t cgramPointer;
  bool cgramSecondWrite;
  uint8_t cgramBuffer;
  // oam access
  uint16_t oam[0x100];
  uint8_t highOam[0x20];
  uint8_t oamAdr;
  uint8_t oamAdrWritten;
  bool oamInHigh;
  bool oamInHighWritten;
  bool oamSecondWrite;
  uint8_t oamBuffer;
  // object/sprites
  bool objPriority;
  uint16_t objTileAdr1;
  uint16_t objTileAdr2;
  uint8_t objSize;
  uint8_t objPixelBuffer[PPU_MAX_WIDTH]; // line buffers
  uint8_t objPriorityBuffer[PPU_MAX_WIDTH];
  bool timeOver;
  bool rangeOver;
  bool objInterlace;
  // background layers
  BgLayer bgLayer[4];
  uint8_t scrollPrev;
  uint8_t scrollPrev2;
  uint8_t mosaicSize;
  uint8_t mosaicStartLine;
  // layers
  Layer layer[5];
  // mode 7
  int16_t m7matrix[8]; // a, b, c, d, x, y, h, v
  uint8_t m7prev;
  bool m7largeField;
  bool m7charFill;
  bool m7xFlip;
  bool m7yFlip;
  bool m7extBg;
  // mode 7 internal
  int32_t m7startX;
  int32_t m7startY;
  // windows
  WindowLayer windowLayer[6];
  uint8_t window1left;
  uint8_t window1right;
  uint8_t window2left;
  uint8_t window2right;
  // color math
  uint8_t clipMode;
  uint8_t preventMathMode;
  bool addSubscreen;
  bool subtractColor;
  bool halfColor;
  bool mathEnabled[6];
  uint8_t fixedColorR;
  uint8_t fixedColorG;
  uint8_t fixedColorB;
  // settings
  bool forcedBlank;
  uint8_t brightness;
  uint8_t mode;
  bool bg3priority;
  bool evenFrame;
  bool pseudoHires;
  bool overscan;
  bool frameOverscan; // if we are overscanning this frame (determined at 0,225)
  bool interlace;
  bool frameInterlace; // if we are interlacing this frame (determined at start vblank)
  bool directColor;
  // latching
  uint16_t hCount;
  uint16_t vCount;
  bool hCountSecond;
  bool vCountSecond;
  bool countersLatched;
  uint8_t ppu1openBus;
  uint8_t ppu2openBus;
  // pixel buffer (xbgr)
  // times 2 for even and odd frame
  uint8_t pixelBuffer[PPU_ROW_BYTES * 239 * 2];
  uint8_t pixelOutputFormat;
  // widescreen: margins in game pixels, and what each layer does with them
  int extraLeft;
  int extraRight;
  uint8_t layerWide[5];
  // ...whether each background is empty at both edges, recomputed once a frame
  uint8_t layerEdgeEmpty[4];
  // ...whether the game has scrolled it sideways since this screen was put up,
  // and the scroll it was at last frame, which is how that is noticed
  uint8_t layerScrolled[4];
  uint16_t lastHScroll[4];
  // ...whether its scroll is rewritten *while the frame is being drawn*, which
  // is a raster effect and not a scroll at all. `hScrollWrites` counts the
  // changes between one frame start and the next; a layer the game scrolls
  // normally is written once or twice a frame and one drawn a line at a time is
  // written for every line, so PPU_RASTER_WRITES sits in the wide gap between.
  // Latched and cleared exactly like `layerScrolled`, and for the same reason.
  uint8_t layerRaster[4];
  uint8_t hScrollWrites[4];
  // ...and the columns of the picture that have any world in them at all, so
  // that a margin running off the end of a map shows the backdrop rather than
  // whatever the tilemap ring was last used for
  int wideClampLo;
  int wideClampHi;
};

enum { ppu_pixelOutputFormatXBGR = 0, ppu_pixelOutputFormatBGRX = 1 };

Ppu* ppu_init(Snes* snes);
void ppu_free(Ppu* ppu);
void ppu_reset(Ppu* ppu);
void ppu_handleState(Ppu* ppu, StateHandler* sh);
bool ppu_checkOverscan(Ppu* ppu);
void ppu_handleVblank(Ppu* ppu);
void ppu_handleFrameStart(Ppu* ppu);
void ppu_runLine(Ppu* ppu, int line);
uint8_t ppu_read(Ppu* ppu, uint8_t adr);
void ppu_write(Ppu* ppu, uint8_t adr, uint8_t val);
void ppu_putPixels(Ppu* ppu, uint8_t* pixels);
void ppu_setPixelOutputFormat(Ppu* ppu, int pixelOutputFormat);
// Widen the picture. `left` and `right` are game pixels, each clamped to
// `PPU_EXTRA_MAX`; 0,0 restores the console. Takes effect on the next line
// drawn, so call it between frames.
void ppu_setWidescreen(Ppu* ppu, int left, int right);
// What one of the five layers (0-3 background, 4 objects) does with the
// margins: one of the `ppu_wide*` values above.
void ppu_setLayerWide(Ppu* ppu, int layer, int policy);
// Is this background layer's tilemap 64 tiles across rather than 32? Asked from
// outside because it is the one piece of PPU state that says whether the game
// is showing a scrolling world or a fixed screen.
bool ppu_bgTilemapWider(const Ppu* ppu, int layer);
// Is this background layer switched on to the main screen? The companion to the
// question above, and needed with it: a tilemap's width outlives the screen
// that asked for it, so the two together say what one of them cannot.
bool ppu_bgOnMainScreen(const Ppu* ppu, int layer);
// Restrict the stretched layers to these columns of the picture, in game pixels
// with 0 the console's left edge. Anything outside shows the backdrop. Wide
// open unless something knows better; the anchored layers ignore it, because a
// status panel is not in the world and does not end where the world does.
void ppu_setWideClamp(Ppu* ppu, int lo, int hi);
// One tilemap word, straight in. For filling the parts of a scrolling tilemap
// that the game maintains only as far as its own 256 columns.
void ppu_writeVramWord(Ppu* ppu, uint16_t wordAdr, uint16_t val);
// One OAM entry, straight in: `x` is nine bits, `y` eight, `tileAttr` the
// second word as the game composes it. For the sprites a game drops because
// they are outside the console's 256 and inside the widened picture.
void ppu_setSprite(Ppu* ppu, int slot, int x, int y, uint16_t tileAttr,
                   bool large);
// The first OAM entry at or after `from` that is parked off the bottom of the
// screen, or 128 if there is none. Where those dropped sprites can go without
// disturbing one the game placed.
int ppu_freeSprite(const Ppu* ppu, int from);
// The picture's current size across, in game pixels: 256 plus both margins.
int ppu_gameWidth(const Ppu* ppu);
// ...and in *output* pixels, which is two per game pixel, so the row pitch
// `ppu_putPixels` writes is this times four bytes.
int ppu_outputWidth(const Ppu* ppu);

#endif
