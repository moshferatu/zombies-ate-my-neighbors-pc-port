// The picture taken apart, so that its parts can be put back a fraction of a
// tick from where the game left them.
//
// `src/smooth.h` moves the PPU's sprites and scroll registers and has the PPU
// draw the frame again. That has two limits that turned out to be the whole
// problem. The PPU draws in whole pixels, so a thing that moves a pixel a tick
// -- the camera on most levels, a walker, the film strips on the character
// select -- cannot be shown anywhere between one pixel and the next, and at
// 240 Hz it steps once and stands still for three refreshes exactly as it did
// at 60. Worse, things moving at different speeds round to different phases:
// a two-pixel move steps at the first and third picture of a tick and a
// one-pixel move at the second, so a player walking across a scrolling floor
// is shown moving *against* the floor and then with it, four times a tick.
// That is the jitter. And a redraw costs the PPU three milliseconds, three
// times a tick, which at a four-millisecond refresh is the whole budget.
//
// So this does not ask the PPU to draw the picture. It asks the PPU what the
// picture is made of -- one plane of pixels per background and priority, the
// backdrop, and every sprite as its own little bitmap -- once per tick, and
// hands the frontend a *draw list*: put this plane here, that sprite there,
// this one added on top. The frontend draws the list with the GPU onto a
// target several times the console's size, so "a quarter of a pixel to the
// left" is a whole pixel of the target and the eye sees a thing move by a
// quarter of a pixel. Building the list costs microseconds, which is what lets
// the pictures land on every refresh with time to spare.
//
// ## What can be drawn this way, and what cannot
//
// The console composes a pixel from the topmost opaque layer at that point,
// with colour maths applied to some layers, clipped by windows, in a handful
// of modes. Drawing layers back to front with alpha reproduces the topmost
// rule exactly. Fixed-colour maths and brightness are a per-layer palette, so
// they are baked into the planes. Adding the sub screen to a layer is an
// additive draw of the sub screen's one layer over it, which is exact where
// that layer is opaque and is how the character select's film strips are
// blended onto the wallpaper. What is *not* expressible as a draw list --
// mode 7, mosaic, hi-res, a window that clips to black, a sub screen with
// more than one layer on it, anything written mid-frame -- makes the frame
// `layered = false`, and the frontend shows the PPU's own picture of it for
// every refresh, as the plain loop would. `tools/test_layers.c` measures over
// the movie corpus how often that is, and that the draw list drawn at the
// console's own size, unmoved, is the frame the PPU drew.
//
// ## Moving the parts
//
// A background moves by the change in its scroll since the last tick, and a
// sprite by the change in its *actor's* position: the port's sprite pass
// leaves beside the OAM buffer which display record each entry came from and
// where that record was drawn (`sprite_oam_owners`), so every piece of a
// zombie moves with the zombie and two zombies close together do not trade
// pieces. When the pass ran in the ROM instead (`--stock`), a sprite is
// matched to its predecessor by looks, with `smooth_match`. A move further
// than `SMOOTH_SNAP` is a cut and is not eased, as before.
//
// ## Evening the motion out
//
// The game keeps positions in whole pixels and moves things at speeds that
// are not: the player walks a pixel and a half a tick, which comes out as 2,
// 1, 2, 1; a zombie giving chase is moved on every other tick, 2, 0, 2, 0.
// At sixty pictures a second that is what motion looks like. Eased straight
// from one tick to the next it is a speed that changes by a third -- or
// stops dead -- thirty times a second, which the pictures in between make
// *visible*: the floor shimmers under a walking player and a chasing zombie
// stutters, and on a diagonal both axes do it at once.
//
// Averaging two ticks cures that and shows everything half a tick late, which
// was tried and was felt. What is wanted is the steady line the uneven steps
// stand either side of, *now*, not the midpoint of where things have been.
// The steps of 2, 1, 2, 1 land a quarter of a pixel ahead of that line after
// the long one and a quarter behind after the short, and in general by
// `(d - q) / 4`, where `d` is this tick's move and `q` the one before. So by
// default (`even`) a thing is drawn that far back from where it is:
//
//     shown(t) = p(t) - (d - q) / 4  =  (3 p(t) + 2 p(t-1) - p(t-2)) / 4
//
// which is exactly `p(t)` for anything moving steadily -- no delay -- and
// exactly nothing for a two-tick alternation, the one pattern the game makes.
// What it costs is that a change of speed is a quarter answered late: a
// thing that stops from 2 a tick is shown half a pixel past where it stopped
// for one tick and then where it is. A change of more than `LAYERS_EVEN_MAX`
// is a jump and not a rounding, and is left alone.
//
// Some things the game moves only every few ticks: the LucasArts screen's
// textured backdrop a pixel diagonally every fourth tick, the character
// select's wallpaper every fifth, the title's backdrop round a circle eleven
// pixels at a time every fourth. Eased from tick to tick that is a step over
// one tick and three or four standing still -- motion at fifteen pictures a
// second, whatever the display does. A background that has stepped at the
// same interval twice running (`LAYERS_STEP_MIN` to `LAYERS_STEP_MAX` ticks
// apart) is taken to be stepping, and each step is *spread*: shown over the
// ticks up to the next, arriving exactly as it lands (`stepK`, `stepI`).
// That shows the backdrop a few ticks late, which on a backdrop nobody
// steers is not felt, and it is only backgrounds: a sprite's motion is the
// game's, at its rate. Ends at once on a step that breaks the interval, or on
// the screen changing.
//
// Twice running is what a level asks (`world`), where the background is the
// view and follows the player: standing five ticks and then walking is not a
// step, and spreading it would be the view answering late. Outside a level
// nothing is steered, and waiting for two intervals was a fifth of a second
// of stepping at the start of every one of those screens, seen each time.
// There a move after a rest of the right length is taken for a step at once,
// unless the interval before it is known and was another. Not the first move
// since the screen went up, though (`fresh`): its rest is only how long the
// picture has been there, and the card naming a level rests two ticks in
// sight and then scrolls sixteen pixels a tick -- spread, its first tick
// crawled and its second leapt. The first step of a screen is drawn as it
// comes, teaches no interval, and the second is spread.
//
// Except on BG3, where even that is a step too many to show. Outside a level
// BG3 is the backdrop of the LucasArts screen, the title and the character
// select, and the game moves it one way only: on a counter, every fourth tick
// (`$80:938E`, `$80:953B`) or every fifth (`$80:9A1B`). So its first move
// since the screen went up is a step whatever came before it, and is spread
// over `LAYERS_STEP_FIRST` ticks at least -- the rest before it is the
// picture's age, not the interval -- and those screens are smooth from their
// first picture.
//
// It is a header of static inline functions with no SDL in it. The draw list
// is data, `layers_render` draws it in software for the tests, and
// `src/present.h` draws the same list with the renderer.

#ifndef ZAMN_LAYERS_H
#define ZAMN_LAYERS_H

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "ppu.h"
#include "smooth.h"

// The picture, in game pixels: the console's 224 lines, and as many columns
// as the PPU has been widened to.
#define LAYERS_LINES 224
// Room around every plane for a tick's worth of movement, so a layer eased
// past its own edge has something there. Nothing moves further than a snap.
#define LAYERS_MARGIN SMOOTH_SNAP
#define LAYERS_PLANE_W (PPU_MAX_WIDTH + 2 * LAYERS_MARGIN)
#define LAYERS_PLANE_H (LAYERS_LINES + 2 * LAYERS_MARGIN)
// Planes: two priorities for each of four backgrounds, and the backdrop --
// and a twin of each with the fixed-colour maths applied, used only when the
// maths is gated by the colour window (`mathGated`): the plain plane is
// drawn whole and the twin on top of it, clipped to where the window allows
// maths. Baking the window into one plane would work unmoved and then move
// the window with the layer; the survivor radar is a box that stays put
// while the world scrolls under it.
#define LAYERS_PLANES 18
#define LAYERS_BACKDROP 8
#define LAYERS_PLANE_OF(layer, prio) ((layer) * 2 + (prio))
#define LAYERS_MATHED(plane) ((plane) + 9)
// Where the maths window allows maths, as rectangles of the picture: the
// edges of a window are per line, so a box is one rectangle, and a window
// that moved on more lines than this is too busy to draw this way.
#define LAYERS_MAX_MATH_RECTS 16
// The sprite atlas: 128 cells in a 16 by 8 grid, each `cell` pixels square,
// `cell` being the larger of the two sprite sizes the PPU is set to.
#define LAYERS_SPRITES 128
#define LAYERS_ATLAS_COLS 16
#define LAYERS_ATLAS_ROWS 8
#define LAYERS_CELL_MAX 64
#define LAYERS_ATLAS_W (LAYERS_ATLAS_COLS * LAYERS_CELL_MAX)
#define LAYERS_ATLAS_H (LAYERS_ATLAS_ROWS * LAYERS_CELL_MAX)
// The PPU's own picture of the frame, kept for the frames that cannot be drawn
// as layers: its output width at two bytes... four bytes a pixel, line doubled.
#define LAYERS_FB_W (PPU_MAX_WIDTH * 2)
#define LAYERS_FB_H 480
// A draw list is at most every plane, every sprite drawn six times (once
// wrapped down, and each of those three times across for a centred one),
// and a sub-screen pass after each of the planes it can follow.
// ...and a background eased line by line (`lineEase`) is a strip a line, of
// each of its two planes.
#define LAYERS_MAX_OPS (LAYERS_PLANES * 3 + LAYERS_SPRITES * 18 + LAYERS_MAX_MATH_RECTS * 9 + \
                        4 * 2 * LAYERS_LINES)

typedef enum {
  LAYERS_BLEND_COPY = 0,  // alpha test: opaque pixels replace, clear ones do not
  LAYERS_BLEND_ADD,       // opaque pixels are added, saturating
  LAYERS_BLEND_SUB,       // ...subtracted, saturating at zero
} LayersBlend;

// A rectangle of the picture, in game pixels.
typedef struct { int x, y, w, h; } LayersRect;

typedef enum {
  LAYERS_OP_PLANE = 0,
  LAYERS_OP_SPRITE,
} LayersOpKind;

// One thing to draw. `src` is in the source's own pixels -- a plane or the
// atlas -- and `dst` is on the target, in units of a game pixel divided by
// the scale the list was built for, so a plane eased a quarter of a pixel
// left on a target four times the console's size is at `dx = -1`.
typedef struct {
  uint8_t kind;   // LayersOpKind
  uint8_t blend;  // LayersBlend
  uint8_t plane;  // LAYERS_OP_PLANE: which plane
  uint8_t slot;   // LAYERS_OP_SPRITE: which cell
  int sx, sy, sw, sh;
  int dx, dy, dw, dh;
  // Drawn only inside this rectangle of the target, if `clipped`.
  bool clipped;
  int cx, cy, cw, ch;
  // For an additive or subtractive op: the index of the op it is confined to,
  // or -1. The console adds the sub screen only where the topmost pixel is a
  // maths layer's, so the sub screen is added right after such a layer is
  // drawn and only where that layer put a pixel -- see `layers_render` for
  // the arithmetic and `present_layers` for the renderer's way of doing it.
  int mask;
} LayersOp;

typedef struct {
  int16_t x, y;        // screen position, as the PPU reads it
  uint8_t w, h;        // both the sprite's size
  uint8_t prio;        // 0..3, the OAM attribute's
  bool math;           // palettes 4-7: the sprites colour maths applies to
  uint8_t place;       // where it goes in a widened picture, `Ppu.spritePlace`
  bool drawn;          // false for a parked or empty one
  // Where this sprite was a tick ago, in whole pixels, if that is known:
  // `known` is false for one that has just appeared or whose predecessor could
  // not be found, and such a sprite is drawn where it is.
  bool known;
  int16_t px, py;
  // For `even`: how far back from where it is this sprite is drawn at the end
  // of this tick, in quarters of a pixel (`c`), and was at the end of the last
  // (`b`), which is where this tick's pictures start from.
  int8_t cx, cy, bx, by;
  // The move of the thing this sprite belongs to: its record's origin where
  // it has one, the sprite itself where not. What a placement is judged by
  // (`LAYERS_LINK_BEFORE`), as a piece's own move has the animation in it.
  int8_t mx, my;
  bool placed;  // paired, and then not eased for having been placed
  // Which display record the pass drew it from, and from what origin, when
  // the port's pass ran -- see `SpriteOamOwners`.
  int16_t rec, ox, oy;
} LayersSprite;

// Everything about one tick's picture that the between-tick pictures are
// drawn from. Large -- the planes are the size of the picture and its margins
// nine times over -- so callers keep two on the heap and alternate.
typedef struct {
  bool valid;      // captured at all
  bool layered;    // ...and expressible as a draw list; else `fb` is the frame
  const char* why; // ...and if not, in a few words, why
  bool dark;       // forced blank or brightness 0: nothing to ease toward
  // The brightness to apply to the finished picture, of 15 -- 15 when it is
  // in the planes already, which is nearly always. See `layers_dim_late`.
  int dim;
  int width;       // game pixels across, `ppu_gameWidth`
  int extraLeft;   // ...of which this many are left of the console's column 0
  int mode;
  bool bg3prio;
  // Per background: drawn on the main screen, the sub screen; the scroll in
  // force on the first line; and whether the scroll was rewritten during the
  // frame, in which case the plane holds the raster effect and is not eased.
  bool main[4], sub[4], raster[4];
  uint16_t scrollX[4], scrollY[4];
  // Set by `layers_link`: may this frame's pictures be eased back toward the
  // frame before it, and by how much each background's scroll moved since
  // then -- zero for one that cannot be eased (a raster effect, a cut).
  // Kept here rather than read from the other frame when the list is built,
  // because by then the other frame is being overwritten by the next tick.
  bool ease;
  int dScrollX[4], dScrollY[4];
  // A widened picture does not always put the console's column 0 at the same
  // place: at the ends of a map the two margins trade width, so the picture's
  // origin moves while the game's coordinates do not (`widescreen_frame`).
  // `dExtraLeft` is how far it moved since the last tick, and everything
  // placed in the console's coordinates -- sprites, and every background
  // that is not pinned to the picture's edges (`anchored`) -- moved on the
  // screen by that much as well as by whatever the game moved it.
  int dExtraLeft;
  // In a level, as `widescreen.h` tells it: BG2 a 64-column map and on the
  // main screen. The backgrounds are the view there -- see `stepK`.
  bool world;
  bool anchored[4];
  // For `even`, per background as per sprite: how far back from where it is
  // it is drawn at the end of this tick and was at the end of the last, in
  // quarters of a pixel. Zero where the tick before was not eased.
  int cScrollX[4], cScrollY[4], bScrollX[4], bScrollY[4];
  // For a background stepping every few ticks -- see "Evening the motion
  // out": ticks since it last moved, the interval between its last two
  // moves, and the step being spread, if any: the move, over `stepK` ticks,
  // of which this is tick `stepI` (from 0). `stepK` is 0 when none.
  int still[4], lastGap[4];
  // ...and whether it has not moved since the screen went up.
  bool fresh[4];
  int stepDX[4], stepDY[4], stepK[4], stepI[4];
  // A background whose *horizontal* scroll alone is rewritten down the frame
  // -- the title's logo, which the game waves with a sine a line (`$80:9570`,
  // an HDMA table at `$7E:8000`) -- is a raster effect that can still be
  // eased: its plane holds each line as that line's scroll left it, and a
  // line drawn a little to one side is that line at a scroll a little
  // different. So each line is a background of its own: `lineX` its scroll
  // (index = line, 1 up), and from `layers_link`, `lineEase` whether the
  // layer is drawn that way this tick, `dLineX` each line's move, and the
  // stepping state of "Evening the motion out" a line at a time -- the title
  // rewrites the top third of its table every tick and the rest only every
  // fourth, so most of the logo moves at 15 Hz in steps of up to 9 pixels.
  bool rasterX[4];
  bool lineEase[4];
  int16_t lineX[4][LAYERS_LINES + 1];
  int8_t dLineX[4][LAYERS_LINES + 1];
  uint8_t lineStill[4][LAYERS_LINES + 1], lineGap[4][LAYERS_LINES + 1];
  uint8_t lineFresh[4][LAYERS_LINES + 1];
  int8_t lineStepD[4][LAYERS_LINES + 1];
  uint8_t lineStepK[4][LAYERS_LINES + 1], lineStepI[4][LAYERS_LINES + 1];
  int linesMoved, linesSpread;  // for the test: lines eased, lines mid-spread
  // How the sprites were paired with their predecessors, for the test:
  // by the nearest piece of the same record, by the record's origin alone,
  // by looks (no record), or not at all.
  int linkNear, linkOrigin, linkLooks, linkNone;
  // ...and how many paired sprites were then not eased after all, for having
  // been put somewhere rather than moved (`LAYERS_JUMP_MAX`).
  int linkJump;
  // ...and how many pieces were taken back by their record's move instead of
  // their own, so that the record stays in one piece.
  int linkTogether;
  // Which planes have anything in them, so an empty one is neither uploaded
  // nor drawn.
  bool planeUsed[LAYERS_PLANES];
  // The maths gated by the colour window (`preventMathMode`): the twins are
  // in use and are drawn inside these rectangles, in picture pixels.
  bool mathGated;
  int mathRects;
  LayersRect mathRect[LAYERS_MAX_MATH_RECTS];
  // ...and the rectangles as they are shown under `even`: held from the
  // last tick where they differ from it by no more than a few lines. This
  // was written for the radar's box, which began on line 49, 50, 51 or 52
  // from one tick to the next, and the game was blamed for it. It was not
  // the game: the box is HDMA off a table in ROM and does not move, and the
  // substitution was losing scanlines of HDMA (`burn_slice` in
  // `cosim/cosim.c`). With that mended nothing is known to wander and this
  // holds nothing; it stays for a window that really is moved by hand. Set
  // by `layers_link`.
  LayersRect mathShown[LAYERS_MAX_MATH_RECTS];
  int mathHeld;  // how many of them were held this tick, for the test
  // Colour maths: which of the main-screen layers (0-3 backgrounds, 4 sprites
  // with palettes 4-7, 5 backdrop) get the sub screen added or subtracted, and
  // which one background the sub screen consists of. Fixed-colour maths is
  // baked into the planes and does not appear here.
  bool subAdd;      // the sub screen is added (or subtracted) to `mathMain[]`
  bool subSubtract;
  bool mathMain[6];
  int subLayer;     // the one background on the sub screen, or -1
  // Sprites, in OAM order.
  LayersSprite spr[LAYERS_SPRITES];
  int cell;         // the atlas cell size this tick
  bool ownersFresh; // `rec`/`ox`/`oy` above are this tick's
  uint16_t oam[LAYERS_SPRITES * 2];
  uint8_t highOam[LAYERS_SPRITES / 4];
  // The pixels. Planes are RGBA, row-major, `LAYERS_PLANE_W` wide: plane
  // column `c` is picture column `c - LAYERS_MARGIN - extraLeft`, plane row
  // `r` is line `r - LAYERS_MARGIN + 1`. The atlas likewise, cell `s` at
  // column `s % 16`, row `s / 16`, each `cell` pixels square.
  uint8_t plane[LAYERS_PLANES][LAYERS_PLANE_H][LAYERS_PLANE_W][4];
  uint8_t atlas[LAYERS_ATLAS_H][LAYERS_ATLAS_W][4];
  // The PPU's own output of this frame, `ppu_putPixels` layout, `fbWidth`
  // pixels across and `LAYERS_FB_H` rows.
  int fbWidth;
  uint8_t fb[LAYERS_FB_W * LAYERS_FB_H * 4];
} LayersFrame;

// ---------------------------------------------------------------------------
// Colours
// ---------------------------------------------------------------------------

// The PPU's expansion of a five-bit channel, scaled by brightness -- the same
// arithmetic `ppu_handlePixel` does, so a plane drawn unmoved is the frame.
static inline uint8_t layers_channel(int c, int brightness) {
  if (c < 0) c = 0;
  if (c > 31) c = 31;
  return (uint8_t)(((c << 3) | (c >> 2)) * brightness / 15);
}

// Whether brightness is left out of the planes and applied to the finished
// picture instead (`LayersFrame.dim`). The console adds the sub screen in five
// bits, clamps, and then applies brightness; planes dimmed beforehand and then
// added clamp at white instead of at the dimmed white, and come out too bright
// wherever the sum clamps. So a frame that adds the sub screen while faded is
// composed at full brightness and dimmed whole, which is the console's order.
// (It used to be refused -- "sub screen in a fade" -- and the whole of the
// character select's fade-in was the PPU's picture, a quarter of a second of
// sixty pictures a second at the start of a screen that is smooth after.)
static inline bool layers_dim_late(const Ppu* ppu) {
  if (ppu->brightness >= 15 || !ppu->addSubscreen) return false;
  for (int l = 0; l < 6; l++)
    if (ppu->mathEnabled[l]) return true;
  return false;
}

// A CGRAM colour as RGBA, with fixed-colour maths applied if `math`.
static inline void layers_rgba(const Ppu* ppu, int index, bool math,
                               uint8_t out[4]) {
  const int brightness = layers_dim_late(ppu) ? 15 : ppu->brightness;
  const uint16_t color = ppu->cgram[index & 0xff];
  int r = color & 0x1f, g = (color >> 5) & 0x1f, b = (color >> 10) & 0x1f;
  if (math) {
    if (ppu->subtractColor) {
      r -= ppu->fixedColorR; g -= ppu->fixedColorG; b -= ppu->fixedColorB;
    } else {
      r += ppu->fixedColorR; g += ppu->fixedColorG; b += ppu->fixedColorB;
    }
    // Halved before it is clamped, as the PPU does it; a negative halves to
    // a negative and clamps to nothing either way.
    if (ppu->halfColor) {
      r = r < 0 ? -((-r) >> 1) : r >> 1;
      g = g < 0 ? -((-g) >> 1) : g >> 1;
      b = b < 0 ? -((-b) >> 1) : b >> 1;
    }
  }
  out[0] = layers_channel(r, brightness);
  out[1] = layers_channel(g, brightness);
  out[2] = layers_channel(b, brightness);
  out[3] = 255;
}

// ---------------------------------------------------------------------------
// Capture: one tick's picture, taken apart
// ---------------------------------------------------------------------------

// The main screen's stack, from `layersPerMode`/`prioritysPerMode` in the
// PPU, for the modes a draw list can express: 0, 1 and 3, plus 1 with BG3 in
// front. Front to back, as the PPU walks them; the list builder walks them
// backwards.
static inline int layers_stack(int mode, bool bg3prio, int* layer, int* prio) {
  static const int L0[] = {4, 0, 1, 4, 0, 1, 4, 2, 3, 4, 2, 3};
  static const int P0[] = {3, 1, 1, 2, 0, 0, 1, 1, 1, 0, 0, 0};
  static const int L1[] = {4, 0, 1, 4, 0, 1, 4, 2, 4, 2};
  static const int P1[] = {3, 1, 1, 2, 0, 0, 1, 1, 0, 0};
  static const int L8[] = {2, 4, 0, 1, 4, 0, 1, 4, 4, 2};
  static const int P8[] = {1, 3, 1, 1, 2, 0, 0, 1, 0, 0};
  static const int L3[] = {4, 0, 4, 1, 4, 0, 4, 1};
  static const int P3[] = {3, 1, 2, 1, 1, 0, 0, 0};
  const int* l; const int* p; int n;
  if (mode == 0) { l = L0; p = P0; n = 12; }
  else if (mode == 1 && !bg3prio) { l = L1; p = P1; n = 10; }
  else if (mode == 1) { l = L8; p = P8; n = 10; }
  else if (mode == 3) { l = L3; p = P3; n = 8; }
  else return 0;
  for (int i = 0; i < n; i++) { layer[i] = l[i]; prio[i] = p[i]; }
  return n;
}

// Why a frame cannot be a draw list, or NULL if it can. Every reason is a
// feature of the console the list has no op for; each is counted by the test
// so that "how often" is a measured number and not a guess.
static inline const char* layers_unexpressible(const Ppu* ppu) {
  if (ppu->forcedBlank) return "forced blank";
  if (ppu->midFrameWrite) return "written mid-frame";
  if (ppu->mode != 0 && ppu->mode != 1 && ppu->mode != 3) return "mode";
  if (ppu->pseudoHires || ppu->interlace || ppu->frameOverscan) return "hires/interlace/overscan";
  if (ppu->directColor) return "direct colour";
  if (ppu->clipMode != 0) return "colour window clips";
  // The window edges moved during the frame: fine for the maths gate, which
  // is read per line, and for nothing else, which is read as the frame ended.
  if (ppu->windowRaster) {
    for (int l = 0; l < 4; l++)
      if ((ppu->layer[l].mainScreenEnabled && ppu->layer[l].mainScreenWindowed) ||
          (ppu->layer[l].subScreenEnabled && ppu->layer[l].subScreenWindowed))
        return "layer window written mid-frame";
    if (ppu->layer[4].mainScreenWindowed || ppu->layer[4].subScreenWindowed)
      return "sprite window written mid-frame";
  }
  for (int l = 0; l < 4; l++)
    if (ppu->bgLayer[l].mosaicEnabled && ppu->mosaicSize > 1 &&
        (ppu->layer[l].mainScreenEnabled || ppu->layer[l].subScreenEnabled))
      return "mosaic";
  // A layer on both screens with different windowing would need two planes.
  for (int l = 0; l < 4; l++)
    if (ppu->layer[l].mainScreenEnabled && ppu->layer[l].subScreenEnabled &&
        ppu->layer[l].mainScreenWindowed != ppu->layer[l].subScreenWindowed)
      return "windowed differently on each screen";
  bool anyMath = false;
  for (int l = 0; l < 6; l++) anyMath |= ppu->mathEnabled[l];
  if (anyMath && ppu->addSubscreen) {
    // The sub screen is drawn as one additive layer, so it must be one layer.
    int subs = 0;
    for (int l = 0; l < 4; l++) subs += ppu->layer[l].subScreenEnabled ? 1 : 0;
    if (ppu->layer[4].subScreenEnabled) return "sprites on the sub screen";
    if (subs > 1) return "two layers on the sub screen";
    // Where the sub screen is clear the console adds the fixed colour instead;
    // an additive draw adds nothing there.
    if (subs == 1 && (ppu->fixedColorR || ppu->fixedColorG || ppu->fixedColorB))
      return "sub screen with a fixed colour";
    if (ppu->halfColor) return "half colour with the sub screen";
    // (In a fade the planes are left at full brightness and the picture is
    // dimmed whole -- `layers_dim_late`.)
    // ...and the maths window would have to gate the additive draw per column.
    if (ppu->preventMathMode != 0) return "sub screen through a window";
  }
  return NULL;
}

// Where the frame's bytes of RGB sit in `LayersFrame.fb`: `ppu_putPixels`
// doubles every line and every pixel, and packs XRGB as B, G, R, X.
static inline const uint8_t* layers_fb_pixel(const LayersFrame* f, int x, int y) {
  return &f->fb[(((size_t)(y * 2 + 16)) * f->fbWidth + (size_t)x * 2) * 4];
}

// One sprite's cell in the atlas, decoded the way `ppu_evaluateSprites` does,
// with its palette -- and its maths, if it has one -- baked in.
static inline void layers_sprite_cell(LayersFrame* f, const Ppu* ppu, int slot,
                                      int size, bool mathAllowedAll) {
  const int index = slot * 2;
  const uint16_t attr = ppu->oam[index + 1];
  const int tile = attr & 0xff;
  const int palette = (attr & 0xe00) >> 9;
  const bool hFlip = (attr & 0x4000) != 0, vFlip = (attr & 0x8000) != 0;
  const uint16_t objAdr = (attr & 0x100) ? ppu->objTileAdr2 : ppu->objTileAdr1;
  // Palettes 4-7 are the sprites colour maths applies to (the PPU's "layer
  // 4"; 0-3 are its "layer 6").
  const bool math = palette >= 4 && ppu->mathEnabled[4] && !ppu->addSubscreen;
  const int cx = (slot % LAYERS_ATLAS_COLS) * f->cell;
  const int cy = (slot / LAYERS_ATLAS_COLS) * f->cell;
  const int x0 = f->spr[slot].x, y0 = f->spr[slot].y;
  for (int row = 0; row < size; row++) {
    const int srow = vFlip ? size - 1 - row : row;
    for (int col = 0; col < size; col += 8) {
      const int usedCol = hFlip ? size - 1 - col : col;
      const uint8_t usedTile =
          (uint8_t)((((tile >> 4) + (srow / 8)) << 4) | (((tile & 0xf) + (usedCol / 8)) & 0xf));
      const uint16_t plane1 = ppu->vram[(objAdr + usedTile * 16 + (srow & 7)) & 0x7fff];
      const uint16_t plane2 = ppu->vram[(objAdr + usedTile * 16 + 8 + (srow & 7)) & 0x7fff];
      for (int px = 0; px < 8; px++) {
        const int shift = hFlip ? px : 7 - px;
        int pixel = (plane1 >> shift) & 1;
        pixel |= ((plane1 >> (8 + shift)) & 1) << 1;
        pixel |= ((plane2 >> shift) & 1) << 2;
        pixel |= ((plane2 >> (8 + shift)) & 1) << 3;
        uint8_t* out = f->atlas[cy + row][cx + col + px];
        if (pixel == 0) { out[0] = out[1] = out[2] = out[3] = 0; continue; }
        const bool m = math && (mathAllowedAll || ppu_mathAllowedAt((Ppu*)ppu, x0 + col + px, y0 + row + 1));
        // `Ppu.objRemap`: a sprite in colours of the frontend's (the game
        // over's drips under `--red-blood`), as `ppu_evaluateSprites` has it.
        const int colour = ppu->objRemapOn[slot] && ppu->objRemap[pixel]
                               ? ppu->objRemap[pixel] : 0x80 + 16 * palette + pixel;
        layers_rgba(ppu, colour, m, out);
      }
    }
  }
}

// Sprites hide each other by OAM index and nothing else: where two overlap,
// the console shows the lower-numbered one, and *its* priority is what is
// then weighed against the backgrounds -- so a sprite of priority 3 behind
// (in OAM order) a sprite of priority 2 is hidden by it, and where a
// background lies between the two priorities, hidden by that too. The list
// draws each priority in its own slot and cannot say that, so the hidden
// pixels are taken out of the hinder sprite's cell here, at this tick's
// positions. Between ticks the two move a fraction of a pixel apart and the
// hole may sit a fraction off; on the tick itself it is exact, which is what
// the test measures. Sprites of the same priority need nothing: drawing the
// lower index last is the rule already.
static inline void layers_occlude(LayersFrame* f) {
  for (int i = 1; i < LAYERS_SPRITES; i++) {
    LayersSprite* a = &f->spr[i];
    if (!a->drawn) continue;
    const int acx = (i % LAYERS_ATLAS_COLS) * f->cell, acy = (i / LAYERS_ATLAS_COLS) * f->cell;
    for (int j = 0; j < i; j++) {
      const LayersSprite* b = &f->spr[j];
      if (!b->drawn || b->prio == a->prio) continue;
      const int bcx = (j % LAYERS_ATLAS_COLS) * f->cell, bcy = (j / LAYERS_ATLAS_COLS) * f->cell;
      // A sprite's y is eight bits and wraps: one at 250 shows its bottom
      // rows at the top of the picture, where it can overlap one at 2. So
      // each is tried where it is and 256 lines up; only the pair of
      // placements that meet within the picture erase anything.
      for (int wa = 0; wa < 2; wa++) {
        for (int wb = 0; wb < 2; wb++) {
          const int ay = a->y - wa * 256, by = b->y - wb * 256;
          const int x0 = a->x > b->x ? a->x : b->x;
          int y0 = ay > by ? ay : by;
          const int x1 = (a->x + a->w < b->x + b->w) ? a->x + a->w : b->x + b->w;
          int y1 = (ay + a->h < by + b->h) ? ay + a->h : by + b->h;
          if (y0 < 0) y0 = 0;
          if (y1 > LAYERS_LINES) y1 = LAYERS_LINES;
          if (x0 >= x1 || y0 >= y1) continue;
          for (int y = y0; y < y1; y++) {
            for (int x = x0; x < x1; x++) {
              const uint8_t* bp = f->atlas[bcy + (y - by)][bcx + (x - b->x)];
              if (bp[3] == 0) continue;
              uint8_t* ap = f->atlas[acy + (y - ay)][acx + (x - a->x)];
              ap[0] = ap[1] = ap[2] = ap[3] = 0;
            }
          }
        }
      }
    }
  }
}

// Take the picture apart. `ownerRec`/`ownerOx`/`ownerOy` are the port's
// table (`sprite_oam_owners`) if its pass ran *this tick*, else NULL: the
// caller knows, from the table's serial, and this does not. Always fills
// `fb`; fills the planes and sprites only when the frame can be a draw list.
// `ppu` is not const because the PPU's own lookups are not, but nothing in
// it is changed.
static inline void layers_capture(LayersFrame* f, Ppu* ppu,
                                  const int16_t* ownerRec, const int16_t* ownerOx,
                                  const int16_t* ownerOy) {
  f->valid = true;
  f->width = ppu_gameWidth(ppu);
  f->extraLeft = ppu->extraLeft;
  f->world = ppu->bgLayer[1].tilemapWider && ppu->layer[1].mainScreenEnabled;
  f->fbWidth = ppu_outputWidth(ppu);
  ppu_putPixels(ppu, f->fb);
  f->mode = ppu->mode;
  f->bg3prio = ppu->bg3priority;
  f->dark = ppu->forcedBlank || ppu->brightness == 0;
  f->dim = layers_dim_late(ppu) ? ppu->brightness : 15;
  f->why = layers_unexpressible(ppu);
  f->layered = f->why == NULL;
  memcpy(f->oam, ppu->oam, sizeof f->oam);
  memcpy(f->highOam, ppu->highOam, sizeof f->highOam);
  f->ownersFresh = ownerRec != NULL;
  if (!f->layered) return;

  // Maths, as the list needs to know it.
  bool anyMath = false;
  for (int l = 0; l < 6; l++) { f->mathMain[l] = ppu->mathEnabled[l]; anyMath |= ppu->mathEnabled[l]; }
  f->subAdd = anyMath && ppu->addSubscreen;
  f->subSubtract = ppu->subtractColor;
  f->subLayer = -1;
  for (int l = 0; l < 4; l++) if (ppu->layer[l].subScreenEnabled) f->subLayer = l;
  // Fixed-colour maths only when the sub screen is not in it; and if maths is
  // gated by a window nowhere, the per-column test can be skipped.
  const bool fixedMath = anyMath && !ppu->addSubscreen;
  const bool allowedEverywhere = ppu->preventMathMode == 0;
  // Gated by the window: the plain plane and its mathed twin, and the
  // rectangles where the twin shows. Found first, so that a window too busy
  // for the list is known before the planes are built.
  f->mathGated = fixedMath && !allowedEverywhere;
  f->mathRects = 0;
  if (f->mathGated && f->layered) {
    // Each line's columns with maths allowed as runs; a run that continues
    // the same columns as the line above extends that rectangle.
    for (int line = 1; line <= LAYERS_LINES && f->layered; line++) {
      // Columns left of the console's 0 are negative, so "no run open" is a
      // flag and not a sentinel column.
      int runStart = 0;
      bool inRun = false;
      for (int x = -ppu->extraLeft; x <= 256 + ppu->extraRight; x++) {
        const bool m = x < 256 + ppu->extraRight && ppu_mathAllowedAt(ppu, x, line);
        if (m && !inRun) { runStart = x; inRun = true; }
        if (!m && inRun) {
          const int px = runStart + ppu->extraLeft, pw = x - runStart, py = line - 1;
          int r = 0;
          for (; r < f->mathRects; r++)
            if (f->mathRect[r].x == px && f->mathRect[r].w == pw &&
                f->mathRect[r].y + f->mathRect[r].h == py)
              break;
          if (r < f->mathRects) f->mathRect[r].h++;
          else if (f->mathRects < LAYERS_MAX_MATH_RECTS) {
            f->mathRect[f->mathRects].x = px; f->mathRect[f->mathRects].y = py;
            f->mathRect[f->mathRects].w = pw; f->mathRect[f->mathRects].h = 1;
            f->mathRects++;
          } else {
            f->why = "maths window too busy";
            f->layered = false;
            break;
          }
          inRun = false;
        }
      }
    }
  }

  // The backgrounds, and the backdrop as a plane of its own so that the
  // maths window can vary it by column like anything else.
  const int last = ppu->frameOverscan ? 239 : LAYERS_LINES;
  const int x0 = -ppu->extraLeft - LAYERS_MARGIN;
  const int x1 = 256 + ppu->extraRight + LAYERS_MARGIN;
  memset(f->planeUsed, 0, sizeof f->planeUsed);
  for (int l = 0; l < 4; l++) {
    f->main[l] = ppu->layer[l].mainScreenEnabled;
    f->anchored[l] = ppu->layerWide[l] == ppu_wideAnchor;
    f->sub[l] = ppu->layer[l].subScreenEnabled;
    f->scrollX[l] = ppu->lineHScroll[l][1];
    f->scrollY[l] = ppu->lineVScroll[l][1];
    f->raster[l] = false;
    for (int y = 2; y <= last; y++)
      if (ppu->lineHScroll[l][y] != f->scrollX[l] || ppu->lineVScroll[l][y] != f->scrollY[l]) {
        f->raster[l] = true;
        break;
      }
    // ...across only, and the lines can be eased one by one -- see `lineX`.
    f->rasterX[l] = f->raster[l] && !ppu->frameOverscan;
    for (int y = 1; y <= LAYERS_LINES; y++) {
      if (ppu->lineVScroll[l][y] != f->scrollY[l]) f->rasterX[l] = false;
      f->lineX[l][y] = (int16_t)((ppu->lineHScroll[l][y] - ppu_layerShiftX(ppu, l, y)) & 0x3ff);
    }
    // A layer the widened picture draws shifted (`ppu_wideSweep`) moves by its
    // scroll and by the shift's change, and it is the motion this is kept for.
    f->scrollX[l] = (uint16_t)((f->scrollX[l] - ppu_layerShiftX(ppu, l, 1)) & 0x3ff);
    if (!f->main[l] && !(f->sub[l] && f->subAdd)) continue;
    const bool math = fixedMath && f->mathMain[l] && f->main[l];
    const bool onSubOnly = !f->main[l];
    for (int line = 1 - LAYERS_MARGIN; line <= LAYERS_LINES + LAYERS_MARGIN; line++) {
      const int r = line - 1 + LAYERS_MARGIN;
      for (int x = x0; x < x1; x++) {
        const int c = x + ppu->extraLeft + LAYERS_MARGIN;
        int prio = 0;
        const int pixel = ppu_layerPixel(ppu, l, x, line, onSubOnly, &prio);
        uint8_t* out0 = f->plane[LAYERS_PLANE_OF(l, 0)][r][c];
        uint8_t* out1 = f->plane[LAYERS_PLANE_OF(l, 1)][r][c];
        out0[0] = out0[1] = out0[2] = out0[3] = 0;
        out1[0] = out1[1] = out1[2] = out1[3] = 0;
        uint8_t* tw0 = f->plane[LAYERS_MATHED(LAYERS_PLANE_OF(l, 0))][r][c];
        uint8_t* tw1 = f->plane[LAYERS_MATHED(LAYERS_PLANE_OF(l, 1))][r][c];
        if (math && f->mathGated) {
          tw0[0] = tw0[1] = tw0[2] = tw0[3] = 0;
          tw1[0] = tw1[1] = tw1[2] = tw1[3] = 0;
        }
        if (pixel == 0) continue;
        // Maths everywhere is baked into the plane; maths through a window
        // goes into the twin, and the plane stays plain.
        layers_rgba(ppu, pixel, math && allowedEverywhere, prio ? out1 : out0);
        f->planeUsed[LAYERS_PLANE_OF(l, prio)] = true;
        if (math && f->mathGated) {
          layers_rgba(ppu, pixel, true, prio ? tw1 : tw0);
          f->planeUsed[LAYERS_MATHED(LAYERS_PLANE_OF(l, prio))] = true;
        }
      }
    }
  }
  {
    const bool math = fixedMath && f->mathMain[5];
    uint8_t plain[4], mathed[4];
    layers_rgba(ppu, 0, false, plain);
    layers_rgba(ppu, 0, true, mathed);
    for (int r = 0; r < LAYERS_PLANE_H; r++) {
      for (int x = x0; x < x1; x++) {
        const int c = x + ppu->extraLeft + LAYERS_MARGIN;
        memcpy(f->plane[LAYERS_BACKDROP][r][c], math && allowedEverywhere ? mathed : plain, 4);
        if (math && f->mathGated) memcpy(f->plane[LAYERS_MATHED(LAYERS_BACKDROP)][r][c], mathed, 4);
      }
    }
    f->planeUsed[LAYERS_BACKDROP] = true;
    if (math && f->mathGated) f->planeUsed[LAYERS_MATHED(LAYERS_BACKDROP)] = true;
  }

  // The sprites.
  f->cell = ppu_spriteSize(ppu, 0);
  for (int s = 0; s < LAYERS_SPRITES; s++) {
    const int size = ppu_spriteSize(ppu, s);
    if (size > f->cell) f->cell = size;
  }
  if (f->cell > LAYERS_CELL_MAX) f->cell = LAYERS_CELL_MAX;
  const bool spritesOn = ppu->layer[4].mainScreenEnabled;
  for (int s = 0; s < LAYERS_SPRITES; s++) {
    LayersSprite* sp = &f->spr[s];
    const int size = ppu_spriteSize(ppu, s);
    sp->x = (int16_t)ppu_spriteXOf(ppu, s);
    sp->y = (int16_t)(ppu->oam[s * 2] >> 8);
    sp->w = sp->h = (uint8_t)size;
    sp->prio = (uint8_t)((ppu->oam[s * 2 + 1] & 0x3000) >> 12);
    sp->math = ((ppu->oam[s * 2 + 1] & 0xe00) >> 9) >= 4;
    sp->place = ppu->spritePlace[s];
    sp->known = false;
    sp->px = sp->py = 0;
    sp->rec = f->ownersFresh ? ownerRec[s] : -1;
    sp->ox = f->ownersFresh ? ownerOx[s] : 0;
    sp->oy = f->ownersFresh ? ownerOy[s] : 0;
    // On screen at all? The PPU's y is eight bits and its row test wraps, so
    // a sprite at 250 shows its bottom rows at the top of the picture; one
    // parked at 224..240 shows nothing. Off the right edge it is dropped by
    // `ppu_spriteX`'s wrap, which the reading above already applied.
    const bool onY = sp->y < LAYERS_LINES || sp->y + size > 256;
    const bool onX = sp->x > -size - ppu->extraLeft && sp->x < 256 + ppu->extraRight;
    sp->drawn = spritesOn && onY && onX;
    if (sp->drawn) layers_sprite_cell(f, ppu, s, size, allowedEverywhere);
  }
  layers_occlude(f);
}

// Whether the owner table the last picture was taken apart with still says
// whose the sprites are: `rec` is that table (the game's, before anything is
// added to it) and `last` that picture.
//
// A tick the game cannot finish in a frame leaves a frame on which its pass did
// not run, so there is no table for the picture after it -- and the one after
// *that* has nothing to be paired with by record either. Both fell back to
// pairing by looks, a piece at a time, which is how a record comes apart at
// the joins (`layers_link`, at the vote): two ticks of seams for every slow
// one, in exactly the busy moments that have the most to show them on. But
// the picture after a tick that did not finish is the picture before it. The
// console is sent the same sprites again, and if every entry the table
// accounts for is still what it was, the table still accounts for them.
static inline bool layers_owners_stand(const LayersFrame* last, const Ppu* ppu, const int16_t* rec) {
  if (!last->valid) return false;
  for (int s = 0; s < LAYERS_SPRITES; s++) {
    if (rec[s] < 0) continue;
    const int bit = (s & 3) * 2;
    if (ppu->oam[s * 2] != last->oam[s * 2] || ppu->oam[s * 2 + 1] != last->oam[s * 2 + 1] ||
        ((ppu->highOam[s >> 2] ^ last->highOam[s >> 2]) >> bit) & 3)
      return false;
  }
  return true;
}

// ---------------------------------------------------------------------------
// Where things were a tick ago
// ---------------------------------------------------------------------------

// A scroll's change since the last tick, in its ten-bit ring, or 0 if it is
// too far to be a move.
static inline int layers_delta(int from, int to, int bits) {
  const int d = smooth_wrap(to - from, bits);
  return d > SMOOTH_SNAP || d < -SMOOTH_SNAP ? 0 : d;
}

// How far back from where it is `even` draws a thing that moved `d` this tick
// and `q` the tick before, in quarters of a pixel -- see "Evening the motion
// out". Nothing for a change of speed too large to be a rounding.
#define LAYERS_EVEN_MAX 4
// How much a sprite's move may differ from its last before it is a placement
// and not a move -- see `LAYERS_LINK_BEFORE`.
#define LAYERS_JUMP_MAX 6
// How far a maths-window rectangle's top or bottom may wander from tick to
// tick and still be the same rectangle, held where it was -- see `mathShown`.
#define LAYERS_WINDOW_WANDER 3
// A background that moves every this many ticks, twice running, is stepping,
// and its steps are spread over the interval -- see "Evening the motion
// out". Not two: a two-tick alternation is what `even` is for, and a delay of
// two ticks on the world would be felt.
#define LAYERS_STEP_MIN 3
#define LAYERS_STEP_MAX 8
#define LAYERS_STEP_FIRST 4
#define LAYERS_BACKDROP_BG 2
static inline int layers_even(int d, int q) {
  const int c = d - q;
  return c > LAYERS_EVEN_MAX || c < -LAYERS_EVEN_MAX ? 0 : c;
}

// What `cur` needs to know about `prev` to be eased back toward it: whether
// it can be at all, how far each background scrolled, and where each sprite
// was -- by record when the port's pass ran on both ticks, by looks otherwise.
// Called on the main thread between ticks, which is the only time both
// frames stand still.
static inline void layers_link(LayersFrame* cur, const LayersFrame* prev) {
  cur->ease = prev->valid && prev->layered && cur->layered && !prev->dark &&
              !cur->dark && prev->width == cur->width;
  const bool prevEased = cur->ease && prev->ease;
  for (int l = 0; l < 4; l++) {
    cur->dScrollX[l] = cur->dScrollY[l] = 0;
    cur->cScrollX[l] = cur->cScrollY[l] = cur->bScrollX[l] = cur->bScrollY[l] = 0;
    if (!cur->ease || cur->raster[l] || prev->raster[l]) continue;
    cur->dScrollX[l] = layers_delta(prev->scrollX[l], cur->scrollX[l], 10);
    cur->dScrollY[l] = layers_delta(prev->scrollY[l], cur->scrollY[l], 10);
  }
  cur->dExtraLeft = cur->ease ? cur->extraLeft - prev->extraLeft : 0;
  // The maths window's rectangles, held where they wander -- see `mathShown`.
  cur->mathHeld = 0;
  for (int r = 0; r < cur->mathRects; r++) {
    cur->mathShown[r] = cur->mathRect[r];
    if (!cur->ease || !prev->mathGated) continue;
    for (int q = 0; q < prev->mathRects; q++) {
      const int dy = cur->mathRect[r].y - prev->mathShown[q].y;
      const int db = cur->mathRect[r].y + cur->mathRect[r].h -
                     (prev->mathShown[q].y + prev->mathShown[q].h);
      if (cur->mathRect[r].x != prev->mathShown[q].x || cur->mathRect[r].w != prev->mathShown[q].w)
        continue;
      if (dy > LAYERS_WINDOW_WANDER || dy < -LAYERS_WINDOW_WANDER ||
          db > LAYERS_WINDOW_WANDER || db < -LAYERS_WINDOW_WANDER)
        continue;
      cur->mathShown[r] = prev->mathShown[q];
      cur->mathHeld++;
      break;
    }
  }
  // `even`, for the backgrounds: each one's move on the picture -- its scroll
  // less the origin's, as `layers_list` has it -- against its move before.
  if (prevEased)
    for (int l = 0; l < 4; l++) {
      if (cur->raster[l] || prev->raster[l]) continue;
      const int dx = cur->dScrollX[l] - (cur->anchored[l] ? 0 : cur->dExtraLeft);
      const int qx = prev->dScrollX[l] - (prev->anchored[l] ? 0 : prev->dExtraLeft);
      cur->cScrollX[l] = layers_even(dx, qx);
      cur->cScrollY[l] = layers_even(cur->dScrollY[l], prev->dScrollY[l]);
      cur->bScrollX[l] = prev->cScrollX[l];
      cur->bScrollY[l] = prev->cScrollY[l];
    }
  // A background stepping every few ticks, and the step being spread.
  for (int l = 0; l < 4; l++) {
    cur->still[l] = cur->lastGap[l] = 0;
    cur->stepDX[l] = cur->stepDY[l] = cur->stepK[l] = cur->stepI[l] = 0;
    cur->fresh[l] = !cur->ease;
    if (!cur->ease || cur->raster[l] || prev->raster[l]) continue;
    const int dx = cur->dScrollX[l] - (cur->anchored[l] ? 0 : cur->dExtraLeft);
    const int dy = cur->dScrollY[l];
    if (dx == 0 && dy == 0) {
      cur->still[l] = prev->still[l] + 1;
      cur->lastGap[l] = prev->lastGap[l];
      cur->fresh[l] = prev->fresh[l];
      if (prev->stepK[l] && prev->stepI[l] + 1 < prev->stepK[l]) {
        cur->stepDX[l] = prev->stepDX[l];
        cur->stepDY[l] = prev->stepDY[l];
        cur->stepK[l] = prev->stepK[l];
        cur->stepI[l] = prev->stepI[l] + 1;
      }
      continue;
    }
    const bool decor = !cur->world && !prev->world;
    const bool first = prev->fresh[l];
    const bool backdrop = decor && l == LAYERS_BACKDROP_BG;
    int gap = prev->still[l] + 1;
    if (first && backdrop && gap < LAYERS_STEP_FIRST) gap = LAYERS_STEP_FIRST;
    cur->still[l] = 0;
    cur->lastGap[l] = first ? 0 : gap;
    const int lastGap = prev->lastGap[l];
    const bool known = lastGap >= LAYERS_STEP_MIN && lastGap <= LAYERS_STEP_MAX;
    if (gap >= LAYERS_STEP_MIN && gap <= LAYERS_STEP_MAX &&
        (gap == lastGap || (decor && !known && (!first || backdrop))) &&
        dx <= LAYERS_MARGIN && dx >= -LAYERS_MARGIN && dy <= LAYERS_MARGIN && dy >= -LAYERS_MARGIN) {
      cur->stepDX[l] = dx;
      cur->stepDY[l] = dy;
      cur->stepK[l] = gap;
      cur->stepI[l] = 0;
    }
  }
  // The same a line at a time, for a background that is a raster effect
  // across only -- see `lineX`. Not one with the sub screen added to it or
  // made of it, nor one with a twin: those are drawn against the op of a
  // whole plane.
  cur->linesMoved = cur->linesSpread = 0;
  for (int l = 0; l < 4; l++) {
    const bool lines = cur->ease && cur->rasterX[l] && prev->rasterX[l] &&
                       cur->scrollY[l] == prev->scrollY[l] && !cur->mathGated &&
                       !(cur->subAdd && (cur->subLayer == l || cur->mathMain[l]));
    cur->lineEase[l] = lines;
    const bool decor = !cur->world && !prev->world;
    for (int y = 1; y <= LAYERS_LINES; y++) {
      cur->dLineX[l][y] = 0;
      cur->lineStill[l][y] = cur->lineGap[l][y] = 0;
      cur->lineStepD[l][y] = 0;
      cur->lineStepK[l][y] = cur->lineStepI[l][y] = 0;
      cur->lineFresh[l][y] = !(lines && prev->lineEase[l]);
      if (!lines) continue;
      const int d = layers_delta(prev->lineX[l][y], cur->lineX[l][y], 10);
      cur->dLineX[l][y] = (int8_t)d;
      if (d == 0) {
        const int still = prev->lineStill[l][y] + 1;
        cur->lineStill[l][y] = (uint8_t)(still > 250 ? 250 : still);
        cur->lineGap[l][y] = prev->lineGap[l][y];
        cur->lineFresh[l][y] = prev->lineEase[l] ? prev->lineFresh[l][y] : 1;
        if (prev->lineStepK[l][y] && prev->lineStepI[l][y] + 1 < prev->lineStepK[l][y]) {
          cur->lineStepD[l][y] = prev->lineStepD[l][y];
          cur->lineStepK[l][y] = prev->lineStepK[l][y];
          cur->lineStepI[l][y] = (uint8_t)(prev->lineStepI[l][y] + 1);
          cur->linesSpread++;
        }
        continue;
      }
      cur->linesMoved++;
      const bool first = !prev->lineEase[l] || prev->lineFresh[l][y];
      const int gap = prev->lineStill[l][y] + 1;
      cur->lineGap[l][y] = (uint8_t)(first ? 0 : gap);
      const int lastGap = prev->lineGap[l][y];
      const bool known = lastGap >= LAYERS_STEP_MIN && lastGap <= LAYERS_STEP_MAX;
      if (gap >= LAYERS_STEP_MIN && gap <= LAYERS_STEP_MAX &&
          (gap == lastGap || (decor && !known && !first))) {
        cur->lineStepD[l][y] = (int8_t)d;
        cur->lineStepK[l][y] = (uint8_t)gap;
        cur->linesSpread++;
      }
    }
  }
  // For a sprite whose predecessor was sprite `j` of the last tick, once
  // `px`, `py` are set: whether it *moved* there at all, and `even`.
  //
  // A sprite that walked from where it was is eased; one that was put
  // somewhere is not, and the difference is whether this move continues the
  // last. The survivor radar shows every survivor with one sprite, placed on
  // a different survivor each tick -- the console's way of drawing six
  // markers with one entry -- and eased between them that marker swept the
  // box. A move that differs from the move before by more than
  // `LAYERS_JUMP_MAX` on either axis is a placement, and the sprite is drawn
  // where it is; a walker's moves differ by a pixel or two, a turn at 2 a
  // tick by four. One with no move before to compare with is eased if its
  // move is within the same bound of standing still. The move judged is the
  // actor's (`mx`, `my`), not the piece's, which has the animation in it.
  #define LAYERS_LINK_BEFORE(c, j)                                             \
    do {                                                                       \
      const LayersSprite* b_ = &prev->spr[j];                                  \
      const bool had_ = prevEased && b_->known;                                \
      const int dx_ = (c)->x - (c)->px + cur->dExtraLeft, dy_ = (c)->y - (c)->py; \
      const int qx_ = had_ ? b_->x - b_->px + prev->dExtraLeft : 0;            \
      const int qy_ = had_ ? b_->y - b_->py : 0;                               \
      const int jx_ = (c)->mx - (had_ ? b_->mx : 0);                           \
      const int jy_ = (c)->my - (had_ ? b_->my : 0);                           \
      if (jx_ > LAYERS_JUMP_MAX || jx_ < -LAYERS_JUMP_MAX ||                   \
          jy_ > LAYERS_JUMP_MAX || jy_ < -LAYERS_JUMP_MAX) {                   \
        (c)->known = false;                                                    \
        (c)->placed = true;                                                     \
        cur->linkJump++;                                                       \
      } else if (had_) {                                                       \
        (c)->cx = (int8_t)layers_even(dx_, qx_);                               \
        (c)->cy = (int8_t)layers_even(dy_, qy_);                               \
        (c)->bx = b_->cx;                                                      \
        (c)->by = b_->cy;                                                      \
      }                                                                        \
    } while (0)
  cur->linkNear = cur->linkOrigin = cur->linkLooks = cur->linkNone = cur->linkJump = 0;
  cur->linkTogether = 0;
  const bool byRecord = cur->ownersFresh && prev->ownersFresh;
  // For each sprite paired by its record: the first of the last tick's
  // sprites of that record, which stands for the record's last move.
  int16_t firstOf[LAYERS_SPRITES];
  for (int i = 0; i < LAYERS_SPRITES; i++) {
    LayersSprite* c = &cur->spr[i];
    firstOf[i] = -1;
    c->known = false;
    c->cx = c->cy = c->bx = c->by = c->mx = c->my = 0;
    c->placed = false;
    if (!c->drawn) continue;
    if (byRecord && c->rec >= 0) {
      // The record says which actor this piece belongs to, and the actor's
      // origin says how far the actor moved. But a piece is not eased from
      // "where it is now, less the actor's move": an animation frame places
      // its pieces at their own offsets from the origin, and a walker whose
      // frame changed has pieces a pixel or two from where the last frame's
      // were. Eased from the origin's delta alone, such a piece lands a pixel
      // off at the start of the tick and jumps -- once per animation frame,
      // on every walker, which is a jitter. So the piece is eased from where
      // the *nearest piece of the same actor* actually was, with the origin's
      // move taken out of the comparison so that a fast actor still finds its
      // own pieces; the origin's delta is the fallback for a piece that has
      // no near predecessor, such as one that has just come into view.
      int expectX = 0, expectY = 0, best = -1, bestDist = 0;
      bool any = false;
      for (int j = 0; j < LAYERS_SPRITES; j++) {
        const LayersSprite* p = &prev->spr[j];
        if (p->rec != c->rec || !p->drawn) continue;
        if (!any) {
          const int dx = c->ox - p->ox, dy = c->oy - p->oy;
          if (dx > SMOOTH_SNAP || dx < -SMOOTH_SNAP || dy > SMOOTH_SNAP || dy < -SMOOTH_SNAP) break;
          c->mx = (int8_t)dx;
          c->my = (int8_t)dy;
          expectX = c->x - dx;
          expectY = c->y - dy;
          firstOf[i] = (int16_t)j;
          any = true;
        }
        const int d = smooth_abs(p->x - expectX) + smooth_abs(p->y - expectY);
        if (best < 0 || d < bestDist) {
          best = j;
          bestDist = d;
        }
      }
      if (any) {
        c->known = true;
        if (best >= 0 && bestDist <= 4) {
          c->px = prev->spr[best].x;
          c->py = prev->spr[best].y;
          cur->linkNear++;
        } else {
          c->px = (int16_t)expectX;
          c->py = (int16_t)expectY;
          cur->linkOrigin++;
        }
        // (What it moved by before, and `even`, are the record's and not the
        // piece's: below, once every piece of it has been found.)
        continue;
      }
    }
    if (byRecord) {
      // No record, or none it had a tick ago. With the picture widened the
      // first is the things lying on the ground in the margins with no actor
      // behind them -- the items, the weapons and the neighbours, drawn from
      // the level's list by `src/widescreen.h` into whatever OAM entries are
      // free and from whatever VRAM slot it could borrow, so neither the
      // entry nor the tile number says which is which. (The pieces the pass
      // dropped for being outside the console's 256 are put back there too,
      // and those do have a record: `ws_owners`.) What says which is which is
      // where it is: such a thing moves by the camera, a few pixels, so it is
      // the nearest of the last tick's sprites of the same size, palette and
      // flip -- the recordless ones, and the ones whose record has gone from
      // this tick altogether, because the thing on the ground is given a
      // record as the camera comes up to it and loses it as the camera leaves
      // (`object_spawner_body`), and is the same thing in the same place.
      int best = -1, bestDist = 0;
      const uint16_t look = (uint16_t)(cur->oam[i * 2 + 1] & 0xfe00);
      for (int j = 0; j < LAYERS_SPRITES; j++) {
        const LayersSprite* p = &prev->spr[j];
        if (!p->drawn || p->w != c->w) continue;
        if (p->rec >= 0) {
          if (c->rec >= 0) continue;
          bool still = false;
          for (int k = 0; k < LAYERS_SPRITES && !still; k++)
            still = cur->spr[k].drawn && cur->spr[k].rec == p->rec;
          if (still) continue;
        }
        if ((uint16_t)(prev->oam[j * 2 + 1] & 0xfe00) != look) continue;
        const int d = smooth_abs(p->x - c->x) + smooth_abs(p->y - c->y);
        if (d > 8) continue;
        if (best < 0 || d < bestDist) {
          best = j;
          bestDist = d;
        }
      }
      if (best >= 0) {
        c->known = true;
        c->px = prev->spr[best].x;
        c->py = prev->spr[best].y;
        c->mx = (int8_t)(c->x - c->px + cur->dExtraLeft);
        c->my = (int8_t)(c->y - c->py);
        LAYERS_LINK_BEFORE(c, best);
        cur->linkLooks++;
      } else {
        cur->linkNone++;
      }
      continue;
    }
    const SmoothSprite sm = smooth_sprite(cur->oam, cur->highOam, i);
    const int j = smooth_match(prev->oam, prev->highOam, &sm, i);
    if (j < 0) { cur->linkNone++; continue; }
    const SmoothSprite p = smooth_sprite(prev->oam, prev->highOam, j);
    c->known = true;
    c->px = (int16_t)(c->x - smooth_wrap(sm.x - p.x, 9));
    c->py = (int16_t)(c->y - smooth_wrap(sm.y - p.y, 8));
    c->mx = (int8_t)(c->x - c->px + cur->dExtraLeft);
    c->my = (int8_t)(c->y - c->py);
    LAYERS_LINK_BEFORE(c, j);
    cur->linkLooks++;
  }
  // **A record's pieces go back together or the thing comes apart.** Each
  // piece above was taken back to where *its* predecessor was, which is right
  // for a piece and wrong for a body: when a frame of animation moves the
  // pieces by different amounts -- a blob rearing up to strike, its top going
  // one way and its foot staying put -- two pieces that join on both ticks do
  // not join in the pictures between them, and the ground shows through the
  // join. `even` did the same on a smaller scale, a quarter of a pixel between
  // pieces whose last moves had differed, which at nine times the console's
  // size is a line two pixels thick. Reported in play-testing as horizontal
  // seams across the slimes of level 9 while they attack.
  //
  // So the record votes: the move most of its pieces made is the move all of
  // them are taken back by (the origin's breaking a tie), and the pieces that
  // did something else are where the new frame puts them from the first
  // picture of the tick, which is when the console changes the frame too. A
  // walker whose whole frame shifts by a pixel still has every piece eased
  // from where it was -- the jitter the nearest-piece rule was written for --
  // because there the vote is unanimous. And the move before, `even` and
  // whether this is a placement are judged once, against one sprite of the
  // last tick's, and are the same for every piece.
  if (byRecord)
    for (int i = 0; i < LAYERS_SPRITES; i++) {
      if (firstOf[i] < 0) continue;
      const int rec = cur->spr[i].rec, before = firstOf[i];
      int moveX = 0, moveY = 0, votes = 0;
      for (int k = i; k < LAYERS_SPRITES; k++) {
        const LayersSprite* a = &cur->spr[k];
        if (firstOf[k] < 0 || a->rec != rec) continue;
        const int dx = a->x - a->px, dy = a->y - a->py;
        int n = 0;
        for (int m = i; m < LAYERS_SPRITES; m++) {
          const LayersSprite* b = &cur->spr[m];
          if (firstOf[m] >= 0 && b->rec == rec && b->x - b->px == dx && b->y - b->py == dy) n++;
        }
        const bool origin = dx == a->mx && dy == a->my;
        if (n > votes || (n == votes && origin)) {
          votes = n;
          moveX = dx;
          moveY = dy;
        }
      }
      for (int k = i; k < LAYERS_SPRITES; k++) {
        LayersSprite* a = &cur->spr[k];
        if (firstOf[k] < 0 || a->rec != rec) continue;
        firstOf[k] = -1;
        if (a->x - a->px != moveX || a->y - a->py != moveY) cur->linkTogether++;
        a->px = (int16_t)(a->x - moveX);
        a->py = (int16_t)(a->y - moveY);
        LAYERS_LINK_BEFORE(a, before);
      }
    }
  #undef LAYERS_LINK_BEFORE
}

// ---------------------------------------------------------------------------
// The draw list
// ---------------------------------------------------------------------------

// The part of a move of `d` not yet made `num/den` of the way through the
// tick, on a target `scale` times the console: rounded to nearest, half away
// from zero, so it never overshoots either end and is exactly 0 at the end.
static inline int layers_part(int d, int num, int den, int scale) {
  if (den <= 0 || num >= den) return 0;
  if (num <= 0) return d * scale;
  const int scaled = d * (den - num) * scale;
  return scaled >= 0 ? (scaled + den / 2) / den : -((-scaled + den / 2) / den);
}

// The same for `even`: the picture runs from where the last tick's ended,
// `b` quarters of a pixel back from where the thing then was, to `c` quarters
// back from where it is now. Odd in (d, b, c) like `layers_part`, so that a
// sprite standing on a background gets the background's own offset and the
// two cannot part by a rounding.
static inline int layers_part_even(int d, int b, int c, int num, int den, int scale) {
  if (den <= 0) return 0;
  if (num > den) num = den;
  if (num < 0) num = 0;
  const int scaled = ((den - num) * (4 * d + b) + num * c) * scale;
  const int by = 4 * den;
  return scaled >= 0 ? (scaled + by / 2) / by : -((-scaled + by / 2) / by);
}

static inline int layers_back(int d, int b, int c, int num, int den, int scale, bool even) {
  return even ? layers_part_even(d, b, c, num, den, scale) : layers_part(d, num, den, scale);
}

// How far back a background is drawn whose step `d` is being spread over `k`
// ticks, `num / den` of the way through tick `i` of them: the part of the
// step not yet shown, `(k - i - num / den) / k` of it, in target pixels.
static inline int layers_spread(int d, int k, int i, int num, int den, int scale) {
  if (den <= 0 || k <= 0) return 0;
  if (num > den) num = den;
  if (num < 0) num = 0;
  const int scaled = d * ((k - i) * den - num) * scale;
  const int by = k * den;
  return scaled >= 0 ? (scaled + by / 2) / by : -((-scaled + by / 2) / by);
}

// The ops that draw `cur` `num/den` of the way from the tick before it, on a
// target `sx` by `sy` times the console; at `num == den` and not `even`,
// `cur` exactly as it stands. Returns the number of ops, or 0 if `cur` is not
// layered.
static inline int layers_list(const LayersFrame* cur, int num, int den, int sx,
                              int sy, bool even, LayersOp* ops) {
  if (!cur->valid || !cur->layered) return 0;
  const bool ease = cur->ease && (even || num < den);
  int n = 0;
  int layer[12], prio[12];
  const int depth = layers_stack(cur->mode, cur->bg3prio, layer, prio);
  const int W = cur->width, H = LAYERS_LINES, M = LAYERS_MARGIN;

  // A scroll that grew by d moved the content by -d; the content is shown
  // where it is now, moved back by the part of that not yet made.
  int offX[4], offY[4];
  for (int l = 0; l < 4; l++) {
    // ...less however far the picture's own origin moved under it, for a
    // layer that lives in the console's coordinates.
    const int dOrigin = cur->anchored[l] ? 0 : cur->dExtraLeft;
    if (ease && even && cur->stepK[l]) {
      // A step being spread over the ticks to the next -- see `stepK`.
      offX[l] = layers_spread(cur->stepDX[l], cur->stepK[l], cur->stepI[l], num, den, sx);
      offY[l] = layers_spread(cur->stepDY[l], cur->stepK[l], cur->stepI[l], num, den, sy);
    } else {
      offX[l] = ease ? layers_back(cur->dScrollX[l] - dOrigin, cur->bScrollX[l], cur->cScrollX[l], num, den, sx, even) : 0;
      offY[l] = ease ? layers_back(cur->dScrollY[l], cur->bScrollY[l], cur->cScrollY[l], num, den, sy, even) : 0;
    }
    // No further than the margin the plane was drawn with, which a move just
    // short of a cut and a pixel of `even` could otherwise exceed.
    if (offX[l] > M * sx) offX[l] = M * sx;
    if (offX[l] < -M * sx) offX[l] = -M * sx;
    if (offY[l] > M * sy) offY[l] = M * sy;
    if (offY[l] < -M * sy) offY[l] = -M * sy;
  }

  // A plane, eased by (ox, oy) target pixels.
  #define LAYERS_PLANE_OP(p, ox, oy, bl, mk)                                   \
    do {                                                                       \
      LayersOp* o = &ops[n++];                                                 \
      o->kind = LAYERS_OP_PLANE; o->blend = (uint8_t)(bl);                     \
      o->plane = (uint8_t)(p); o->slot = 0; o->mask = (mk);                    \
      o->sx = 0; o->sy = 0; o->sw = W + 2 * M; o->sh = H + 2 * M;              \
      o->dx = -M * sx + (ox); o->dy = -M * sy + (oy);                          \
      o->dw = o->sw * sx; o->dh = o->sh * sy;                                  \
      o->clipped = false; o->cx = o->cy = o->cw = o->ch = 0;                   \
    } while (0)
  // The mathed twin of plane `p`, over it, inside each rectangle the window
  // allows maths in.
  #define LAYERS_TWIN_OPS(p, ox, oy)                                           \
    do {                                                                       \
      if (cur->mathGated && cur->planeUsed[LAYERS_MATHED(p)])                  \
        for (int r_ = 0; r_ < cur->mathRects; r_++) {                          \
          LAYERS_PLANE_OP(LAYERS_MATHED(p), ox, oy, LAYERS_BLEND_COPY, -1);    \
          LayersOp* o = &ops[n - 1];                                           \
          o->clipped = true;                                                   \
          o->cx = (even ? cur->mathShown : cur->mathRect)[r_].x * sx;         \
          o->cy = (even ? cur->mathShown : cur->mathRect)[r_].y * sy;         \
          o->cw = (even ? cur->mathShown : cur->mathRect)[r_].w * sx;         \
          o->ch = (even ? cur->mathShown : cur->mathRect)[r_].h * sy;         \
        }                                                                      \
    } while (0)
  // The sub screen, added on top of the maths layer drawn by op `mk`.
  #define LAYERS_SUB_OPS(mk)                                                   \
    do {                                                                       \
      /* Taken before anything is emitted: `n` moves as ops are added. */      \
      const int mask_ = (mk);                                                  \
      if (cur->subAdd && cur->subLayer >= 0) {                                 \
        const int sl = cur->subLayer;                                          \
        const int bl = cur->subSubtract ? LAYERS_BLEND_SUB : LAYERS_BLEND_ADD; \
        for (int p = 0; p < 2; p++)                                            \
          if (cur->planeUsed[LAYERS_PLANE_OF(sl, p)])                          \
            LAYERS_PLANE_OP(LAYERS_PLANE_OF(sl, p), offX[sl], offY[sl], bl, mask_); \
      }                                                                        \
    } while (0)

  // Plane `p` of background `l` a line at a time -- see `lineX`: each line
  // back by the part of its own move not yet made, lines that come out the
  // same drawn as one strip. (`layers_link` keeps this to a layer with no
  // twin and no sub screen, so there is nothing to draw against it.)
  #define LAYERS_LINE_OPS(p, l)                                                \
    do {                                                                       \
      int from_ = 1, offFrom_ = 0;                                             \
      for (int y_ = 1; y_ <= H + 1; y_++) {                                    \
        int off_ = 0;                                                          \
        if (y_ <= H) {                                                         \
          off_ = even && cur->lineStepK[l][y_]                                 \
                     ? layers_spread(cur->lineStepD[l][y_], cur->lineStepK[l][y_], \
                                     cur->lineStepI[l][y_], num, den, sx)      \
                     : layers_part(cur->dLineX[l][y_], num, den, sx);          \
          if (off_ > M * sx) off_ = M * sx;                                    \
          if (off_ < -M * sx) off_ = -M * sx;                                  \
        }                                                                      \
        if (y_ == 1) offFrom_ = off_;                                          \
        if (y_ <= H && off_ == offFrom_) continue;                             \
        LAYERS_PLANE_OP(p, offFrom_, 0, LAYERS_BLEND_COPY, -1);                \
        LayersOp* o = &ops[n - 1];                                             \
        o->sy = from_ - 1 + M; o->sh = y_ - from_;                             \
        o->dy = (from_ - 1) * sy; o->dh = o->sh * sy;                          \
        from_ = y_; offFrom_ = off_;                                           \
      }                                                                        \
    } while (0)

  LAYERS_PLANE_OP(LAYERS_BACKDROP, 0, 0, LAYERS_BLEND_COPY, -1);
  LAYERS_TWIN_OPS(LAYERS_BACKDROP, 0, 0);
  if (cur->mathMain[5]) LAYERS_SUB_OPS(n - 1);
  for (int i = depth - 1; i >= 0; i--) {
    const int l = layer[i], p = prio[i];
    if (l < 4) {
      if (!cur->main[l] || !cur->planeUsed[LAYERS_PLANE_OF(l, p)]) continue;
      if (ease && cur->lineEase[l]) {
        LAYERS_LINE_OPS(LAYERS_PLANE_OF(l, p), l);
        continue;
      }
      LAYERS_PLANE_OP(LAYERS_PLANE_OF(l, p), offX[l], offY[l], LAYERS_BLEND_COPY, -1);
      LAYERS_TWIN_OPS(LAYERS_PLANE_OF(l, p), offX[l], offY[l]);
      if (cur->mathMain[l]) LAYERS_SUB_OPS(n - 1);
    } else {
      // Sprites of this priority, the lowest OAM index drawn last so that it
      // lands in front, as the PPU's line buffer has it.
      for (int s = LAYERS_SPRITES - 1; s >= 0; s--) {
        const LayersSprite* sp = &cur->spr[s];
        if (!sp->drawn || sp->prio != p) continue;
        int ox = 0, oy = 0;
        if (ease && sp->known) {
          ox = -layers_back(sp->x - sp->px + cur->dExtraLeft, sp->bx, sp->cx, num, den, sx, even);
          oy = -layers_back(sp->y - sp->py, sp->by, sp->cy, num, den, sy, even);
        }
        // Once where it is, and once 256 lines up for the wrap. The PPU
        // evaluates sprites for `line - 1`, so a sprite at OAM y lands on
        // picture row y, one below where a background's line 1 lands. And a
        // sprite placed with a centred layer (`ppu_spriteCentred`) is drawn
        // where it is, clipped to the layer's own 256 columns, and again 256
        // columns either side, clipped to that margin, as the PPU draws it;
        // the clip stays where the margin is while the sprite is eased.
        const bool centred = sp->place == ppu_spriteCentred && W != 256;
        const int extraRight = W - 256 - cur->extraLeft;
        const int mid = (extraRight - cur->extraLeft) / 2 + cur->extraLeft;
        for (int wrap = 0; wrap < 2; wrap++) {
          const int y = sp->y - wrap * 256;
          if (y + sp->h <= 0 || y >= H) continue;
          for (int k = centred ? -1 : 0; k <= (centred ? 1 : 0); k++) {
            // The sprite's left edge and the clip, in columns of the picture
            // from its left edge.
            const int x = sp->x + k * 256 + cur->extraLeft;
            const int lo = !centred ? 0 : k < 0 ? 0 : k > 0 ? mid + 256 : mid;
            const int hi = !centred ? W : k < 0 ? mid : k > 0 ? W : mid + 256;
            if (x + sp->w <= lo || x >= hi) continue;
            LayersOp* o = &ops[n++];
            o->kind = LAYERS_OP_SPRITE; o->blend = LAYERS_BLEND_COPY;
            o->plane = 0; o->slot = (uint8_t)s; o->mask = -1;
            o->sx = (s % LAYERS_ATLAS_COLS) * cur->cell;
            o->sy = (s / LAYERS_ATLAS_COLS) * cur->cell;
            o->sw = sp->w; o->sh = sp->h;
            o->dx = x * sx + ox;
            o->dy = y * sy + oy;
            o->dw = sp->w * sx; o->dh = sp->h * sy;
            o->clipped = centred;
            o->cx = lo * sx; o->cy = 0; o->cw = (hi - lo) * sx; o->ch = H * sy;
            // A sprite in palettes 4-7 is a maths layer of its own.
            if (cur->mathMain[4] && sp->math) LAYERS_SUB_OPS(n - 1);
          }
        }
      }
    }
  }
  #undef LAYERS_SUB_OPS
  #undef LAYERS_TWIN_OPS
  #undef LAYERS_PLANE_OP
  return n;
}

// ---------------------------------------------------------------------------
// Drawing the list in software
// ---------------------------------------------------------------------------

// The source pixel op `o` puts at target pixel (x, y), or NULL if none: the
// nearest-neighbour mapping the renderer will apply.
static inline const uint8_t* layers_source(const LayersFrame* f, const LayersOp* o,
                                           int x, int y) {
  if (x < o->dx || x >= o->dx + o->dw || y < o->dy || y >= o->dy + o->dh) return NULL;
  const int srcPitch = o->kind == LAYERS_OP_PLANE ? LAYERS_PLANE_W : LAYERS_ATLAS_W;
  const uint8_t* src = o->kind == LAYERS_OP_PLANE ? &f->plane[o->plane][0][0][0]
                                                  : &f->atlas[0][0][0];
  const int sy = o->sy + (int)((long)(y - o->dy) * o->sh / o->dh);
  const int sx = o->sx + (int)((long)(x - o->dx) * o->sw / o->dw);
  return src + ((size_t)sy * srcPitch + sx) * 4;
}

// Draw `ops` onto an RGB target `tw` by `th` (three bytes a pixel, row-major)
// built for the scale the list was made at: nearest-neighbour, exactly as the
// renderer will. For the tests, and for anyone without a GPU.
static inline void layers_render(const LayersFrame* f, const LayersOp* ops, int n,
                                 uint8_t* rgb, int tw, int th) {
  memset(rgb, 0, (size_t)tw * th * 3);
  for (int i = 0; i < n; i++) {
    const LayersOp* o = &ops[i];
    const LayersOp* mask = o->mask >= 0 ? &ops[o->mask] : NULL;
    int y0 = o->dy < 0 ? 0 : o->dy, y1 = o->dy + o->dh > th ? th : o->dy + o->dh;
    int x0 = o->dx < 0 ? 0 : o->dx, x1 = o->dx + o->dw > tw ? tw : o->dx + o->dw;
    if (o->clipped) {
      if (x0 < o->cx) x0 = o->cx;
      if (y0 < o->cy) y0 = o->cy;
      if (x1 > o->cx + o->cw) x1 = o->cx + o->cw;
      if (y1 > o->cy + o->ch) y1 = o->cy + o->ch;
    }
    for (int y = y0; y < y1; y++) {
      uint8_t* out = rgb + ((size_t)y * tw + x0) * 3;
      for (int x = x0; x < x1; x++, out += 3) {
        const uint8_t* p = layers_source(f, o, x, y);
        if (p[3] == 0) continue;
        if (mask) {
          const uint8_t* m = layers_source(f, mask, x, y);
          if (!m || m[3] == 0) continue;
        }
        if (o->blend == LAYERS_BLEND_COPY) {
          out[0] = p[0]; out[1] = p[1]; out[2] = p[2];
        } else if (o->blend == LAYERS_BLEND_ADD) {
          for (int c = 0; c < 3; c++) { int v = out[c] + p[c]; out[c] = (uint8_t)(v > 255 ? 255 : v); }
        } else {
          for (int c = 0; c < 3; c++) { int v = out[c] - p[c]; out[c] = (uint8_t)(v < 0 ? 0 : v); }
        }
      }
    }
  }
  if (f->dim < 15)
    for (size_t i = 0; i < (size_t)tw * th * 3; i++) rgb[i] = (uint8_t)(rgb[i] * f->dim / 15);
}

#endif
