
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
// Rows a frame can have, including the overscan ones. `ppu_runLine` is called
// for lines 1..224 or 1..239, so a per-line table is indexed straight by line
// and row 0 goes unused.
#define PPU_LINES 240

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
  // A 256-wide layer that is a whole screen rather than furniture at its two
  // edges: the game over's mask, "GAME OVER" cut out of a solid field. Put
  // its middle at the middle of the widened picture, wherever the console's
  // 256 fall in it -- at the end of a map the two margins are not the same
  // width, and drawn in the console's place the mask sat off to one side --
  // and carry its outermost columns out to the edges as `ppu_wideClampEdge`
  // does, so the field reaches the whole picture.
  ppu_wideCentre,
  // Repeat the console's 256 columns outward, so the margins show the same
  // field again. For a 32-tile tilemap this is what the hardware does anyway --
  // its map wraps every 256 pixels -- and for a wider one it is the point:
  // a 64-column tilemap with only 32 columns ever written has 32 columns of
  // whatever was last in that VRAM, and reading those is how a stone wall comes
  // out shredded. This can only ever show columns the game has actually drawn.
  ppu_wideTile,
  // A layer that is swept across the console from the left: its map begins at
  // column 0, and the game scrolls it from 256 down to 0, so that the map's
  // first 256 columns come in from the left edge and what is at column 256 --
  // the Konami logo's star, with the line behind it -- leaves by the right one.
  // The sweep is the console's width long and the picture is wider, so drawn
  // in place the star would appear inside the picture and stop short of its
  // edge. Instead the layer is drawn shifted by an amount that runs from the
  // left margin's width leftward, with nothing swept in, to the right
  // margin's width rightward, with all of it: the star crosses the whole
  // picture in the time it crossed the console. And left of the map's column
  // 0, where the console never looks, the map is read as column 0 -- the line
  // goes on to the picture's edge, as it went on to the console's.
  ppu_wideSweep,
};

// Where a sprite goes when the picture is widened -- `Ppu.spritePlace`.
enum {
  // With the world: the console's own coordinates, and past the console's
  // edges where the picture is wider.
  ppu_spriteWorld = 0,
  // With an anchored layer: drawn where `ppu_wideAnchor` draws a column of
  // the same number, that many in from the picture's edge.
  ppu_spriteAnchored,
  // With a centred layer: drawn where `ppu_wideCentre` draws a column of the
  // same number, clipped to the layer's own 256 columns, and again 256
  // columns either side, clipped to that margin -- the policy repeats the
  // layer's columns in its margins, and a sprite laid out over them is
  // repeated with them.
  ppu_spriteCentred,
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
  // zamn: a sprite drawn in colours of the frontend's. For an OAM entry with
  // `objRemapOn` set, a pixel whose `objRemap` is not zero takes that CGRAM
  // index in place of its palette's. Both are the frontend's to set, from the
  // frame hook, and are all zero otherwise (`src/blood.h` is the one user).
  bool objRemapOn[0x80];
  uint8_t objRemap[16];
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
  // ...and per sprite, where it goes (`ppu_spriteWorld` and the others
  // above). Set from outside for the sprites of a screen-space record: the
  // survivor radar's markers, which the game lays out over the status panel,
  // and the game over's hanging drips, which it lays out over the mask.
  uint8_t spritePlace[128];
  // ...whether each background is empty at both edges, recomputed once a frame
  uint8_t layerEdgeEmpty[4];
  // ...and for `ppu_wideCentre`, per background and side, where its margin
  // is filled from on the line last asked about: a column and a line of the
  // layer, or a column of -1 for a line with nothing to fill from. One
  // search per line and side, keyed by the line and the scroll it was
  // searched at. See the policy's case in `ppu_wideMapX`.
  int16_t centreFillCol[4][2], centreFillFrom[4][2];
  int16_t centreFillLine[4][2];
  uint16_t centreFillH[4][2], centreFillV[4][2];
  uint8_t centreFillWrap[4][2];
  // ...and the last line of the frame on which the layer is opaque from
  // column 0 to 255 -- the foot of the mask's solid field, below which its
  // drips hang -- or -1 for none; found once per scroll.
  int16_t centreLastFull[4];
  uint16_t centreLastFullH[4], centreLastFullV[4];
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
  // --- Drawing a frame again -------------------------------------------
  //
  // Where each background was scrolled to as each line of the frame was drawn.
  // A game that scrolls sets a layer's scroll once, in vblank, and every line
  // reads the same value; one drawing a raster effect -- the title logo's
  // sweep -- rewrites it before every line, and the picture is a function of
  // all 224 values rather than of the last one. Recording them as they are
  // used is what lets `ppu_renderFrame` redraw either kind of frame, and lets a
  // frontend move either kind a fraction of a tick: see `src/smooth.h`.
  uint16_t lineHScroll[4][PPU_LINES];
  uint16_t lineVScroll[4][PPU_LINES];
  // ...and where the two windows' edges were ($2126-$2129, as W1L W1R W2L
  // W2R) as each line was drawn, for the same reason: a window is a pair of
  // columns, and a game that wants a box moves the edges on the lines where
  // the box begins and ends -- this one's survivor radar does, through the
  // colour-maths window, for as long as the radar is up.
  uint8_t lineWindow[PPU_LINES][4];
  // Whether the window edges were rewritten while the picture was being
  // drawn. Kept apart from `midFrameWrite` because a frame that only did that
  // *can* be drawn again, from the per-line edges, where nothing but the
  // maths gate looks at them.
  bool windowRaster;
  // ...and whether anything *other* than a scroll register was written while
  // the picture was being drawn. Scroll is recorded line by line, so a scroll
  // rewritten mid-frame -- by HDMA or by a CPU loop -- is a raster effect like
  // any other, and the window edges are recorded the same way (`lineWindow`,
  // `windowRaster`); brightness, colour math, VRAM or OAM written mid-frame
  // are not recorded, and a frame that had them cannot be drawn again from
  // its end state. Two things in this game do it: the HDMA on the map screen
  // and a vblank so full that the NMI is still uploading, behind forced
  // blank, when the first lines are due -- which the console shows as a black
  // band at the top of the picture. Cleared at the top of each frame.
  bool midFrameWrite;
  // ...and which register it was, on which line, how many times -- so that a
  // frame a frontend cannot draw again can say what it was that stopped it.
  uint8_t midFrameAdr;
  uint16_t midFrameLine;
  int midFrameWrites;
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
// Draw the whole picture again from the PPU's state as it stands now -- every
// line `ppu_runLine` drew this frame, into the same half of the pixel buffer,
// so `ppu_putPixels` afterwards hands out the redrawn picture. `hScroll` and
// `vScroll`, if given, are per-layer, per-line scroll tables in the shape of
// `lineHScroll`, applied before each line is drawn; NULL keeps the registers
// as they are. Only faithful for a frame `ppu_frameStatic` says it can be.
void ppu_renderFrame(Ppu* ppu, const uint16_t (*hScroll)[PPU_LINES],
                     const uint16_t (*vScroll)[PPU_LINES]);
// Was the frame just drawn a function of the state the PPU is in at the end
// of it, plus the per-line scroll record? False if something the record does
// not cover was written while the picture was being drawn, or in mode 7,
// whose scroll lives in the matrix registers and is not recorded.
bool ppu_frameStatic(const Ppu* ppu);
// --- Taking the picture apart ------------------------------------------
//
// What `src/layers.h` needs to draw the frame as separate layers: one
// background's pixel at a column of the picture on a given line, as the PPU
// would have read it -- through the widescreen mapping, the layer's window and
// the scroll in force on that line -- with which of the two priorities the
// tile carries; whether colour maths is allowed at a column; and a sprite's
// size and nine-bit x. Nothing here draws.
//
// `line` may lie outside 1..224: a margin above or below the picture is read
// with the scroll of the nearest drawn line, which is what a layer eased a
// few pixels past its edge needs. Returns the CGRAM index, 0 for transparent.
int ppu_layerPixel(Ppu* ppu, int layer, int x, int line, bool sub, int* priority);
// Colour maths, as gated by the colour window and the prevent mode, at column
// `x` of the picture. Whether a *layer* has maths on is `mathEnabled[]`.
// `line` as `ppu_layerPixel` takes it: the window edges are the ones in force
// on that line.
bool ppu_mathAllowedAt(Ppu* ppu, int x, int line);
// Whether column `sx` of background `layer` (0-255, at the layer's current
// scroll) has no opaque pixel on any line of the frame -- `ppu_columnEmpty`
// for callers outside: the status panel has nothing in the middle columns and
// the game over's mask has something in all of them.
bool ppu_columnEmptyAt(const Ppu* ppu, int layer, int sx);
// ...and whether it has something in every tile down that column: the edge of
// a field, as against the edge of a card with something written on it.
bool ppu_columnFilledAt(const Ppu* ppu, int layer, int sx);
// How far right of its place on the console background `layer` is drawn on
// `line` -- `ppu_wideSweep`'s shift, and 0 for every other policy. A caller
// following the layer's motion from one frame to the next wants its scroll
// less this.
int ppu_layerShiftX(const Ppu* ppu, int layer, int line);
// The colour window's clip-to-black, likewise.
bool ppu_clippedAt(Ppu* ppu, int x);
int ppu_spriteSize(const Ppu* ppu, int slot);
int ppu_spriteXOf(const Ppu* ppu, int slot);
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
