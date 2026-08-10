// Where the framebuffer goes on the screen, and how it gets there.
//
// Split out of `main_sdl.c` because it is the one part of presentation that is
// arithmetic rather than API calls, and arithmetic can be checked: `scale_plan`
// takes an output size and returns a rectangle and two decisions, with no SDL
// types anywhere in it. `tools/test_scale.c` asserts the properties that matter
// — every game pixel the same size, nothing off-centre, no intermediate built
// when it could only do harm — over a sweep of window sizes.
//
// ## What "sharp" is defending against
//
// The core hands over 512x480: 224 scanlines doubled into rows 16..463 with
// black above and below, and every game pixel written twice across, since
// `ppu_handlePixel` fills a 512-wide row whether the mode is hi-res or not. So
// on this game one game pixel is a 2x2 block, and sharpness is a property of
// that block — it has to be the same size everywhere on screen, with edges that
// stay edges.
//
// **Nearest-neighbour alone does not give that**, which is worth stating
// because reaching for nearest and stopping is the usual mistake. Scale 512
// wide into a 1000-pixel window and the factor is 1.953: nearest has to drop
// one source pixel in every 21, so most game pixels land 2 screen pixels wide
// and some land 1. On a moving sprite that narrow column travels across it, and
// the shimmer is what makes a nearest-scaled emulator look worse than a blurry
// one. The artifact is the fractional factor, not the filter.
//
// Three ways out, and they trade against each other, so the frontend offers all
// three and `--filter` picks:
//
//   * `integer` — only ever scale by a whole number and letterbox the rest.
//     Uniform by construction, and it costs real estate: 1920x1080 fits 2x of a
//     512x480 image and leaves 42% of the height black, because 3x needs 1440.
//
//   * `sharp` (default) — nearest up to the next whole multiple offscreen, then
//     one bilinear step from there down to whatever actually fits. The bilinear
//     step only ever *shrinks*, and it shrinks something already blocky, so the
//     softening is confined to a sub-pixel seam at each block edge rather than
//     spread over the whole image. Uniform blocks, and the window is filled.
//
//   * `linear` — one bilinear step straight from 512x480. This is what "not
//     sharp" looks like, kept so the difference can be seen rather than argued.
//
// ## Aspect ratio
//
// The core hands over 512x480, but only rows 16..463 are picture: `ppu_putPixels`
// zeroes sixteen rows top and bottom when the game is not in overscan, and this
// one is not. Scaling all 480 rows spends 6.7% of the screen enlarging black and
// then letterboxes *that*, so the caller crops to the live 512x448 and the sizes
// below are the cropped ones.
//
// What shape those 448 rows should be shown in is a separate question, and the
// answer is not "square". A SNES pixel is not square: the console puts 256
// pixels across a frame that a television showed at 4:3, so the picture is
// composed for 4:3 and square pixels make it 8:7 — noticeably narrow, and 11%
// less screen than it should have. snes9x reports 4:3 to RetroArch for exactly
// this reason, which is why the same game looks wider there.
//
// So `scale_plan` takes the intended display aspect as a ratio, and the shape of
// the source no longer decides the shape of the output. This does fight sharp
// upscaling, and the fight is the interesting part: at 4:3 the horizontal and
// vertical magnifications are different numbers, so a single whole-multiple
// intermediate cannot serve both axes. `stage_x` and `stage_y` are therefore
// separate, and the intermediate is a whole multiple *per axis* — which also
// buys something the square-pixel version could not have: an output that is a
// whole multiple of the source on each axis independently is pixel-exact even
// though its pixels are oblong, so 4:3 is not automatically the blurry choice.

#ifndef ZAMN_SCALE_H
#define ZAMN_SCALE_H

#include <stdbool.h>
#include <string.h>

typedef enum {
  SCALE_SHARP,    // nearest to a whole multiple, then one bilinear step down
  SCALE_INTEGER,  // nearest, largest whole multiple that fits, letterboxed
  SCALE_LINEAR,   // straight bilinear from the source size
  SCALE_MODE_COUNT,  // what F2 cycles through; keep last
} ScaleMode;

// How large an offscreen target `sharp` will build, as a multiple of the
// framebuffer. At 8x that is 4096x3840 and 63 MB, already far past the point
// where the bilinear step is visible; beyond it the mode falls back to plain
// nearest, which at that magnification is what it would have looked like.
// It doubles as the cap on `--scale`, so that a typo cannot open a window
// 32,768 pixels wide that has to be killed from a task manager.
#define SCALE_MAX_STAGE 8

// How the picture should be shaped on screen, independent of how many pixels
// the source happens to have.
typedef enum {
  ASPECT_43,      // what a CRT showed, and what snes9x reports to RetroArch
  ASPECT_SQUARE,  // square pixels: the source's own shape, 8:7 once cropped
  ASPECT_MODE_COUNT,  // what F3 cycles through; keep last
} AspectMode;

static inline const char* aspect_name(AspectMode m) {
  return m == ASPECT_SQUARE ? "square" : "4:3";
}

static inline bool aspect_parse(const char* s, AspectMode* out) {
  if (!strcmp(s, "4:3"))    { *out = ASPECT_43;     return true; }
  if (!strcmp(s, "square")) { *out = ASPECT_SQUARE; return true; }
  if (!strcmp(s, "1:1"))    { *out = ASPECT_SQUARE; return true; }
  return false;
}

// The ratio to hand `scale_plan`, given the source's own dimensions.
static inline void aspect_ratio(AspectMode m, int sw, int sh, int* aw, int* ah) {
  if (m == ASPECT_SQUARE) { *aw = sw; *ah = sh; return; }
  *aw = 4; *ah = 3;
}

typedef struct { int x, y, w, h; } ScaleRect;

typedef struct {
  ScaleRect dst;  // where the picture lands, in output pixels
  // 0 to copy the source straight to `dst`; otherwise the whole multiple of the
  // source to draw nearest into first and then shrink from. Per axis, because
  // aspect correction magnifies the two axes by different amounts and one
  // number cannot describe both.
  int stage_x, stage_y;
  // The filter for the final copy — whichever that is. False means nearest,
  // and nearest is only ever chosen where it is exactly right.
  bool linear;
} ScalePlan;

static inline const char* scale_name(ScaleMode m) {
  switch (m) {
    case SCALE_INTEGER: return "integer";
    case SCALE_LINEAR:  return "linear";
    default:            return "sharp";
  }
}

static inline bool scale_parse(const char* s, ScaleMode* out) {
  if (!strcmp(s, "sharp"))   { *out = SCALE_SHARP;   return true; }
  if (!strcmp(s, "integer")) { *out = SCALE_INTEGER; return true; }
  if (!strcmp(s, "linear"))  { *out = SCALE_LINEAR;  return true; }
  return false;
}

// `sw`x`sh` of framebuffer onto `ow`x`oh` of window. `can_target` is whether the
// renderer can draw into a texture at all; without it `sharp` has nowhere to
// put the intermediate and degrades to the nearest it would otherwise improve.
//
// All integer arithmetic, deliberately: "is this fit an exact multiple" has to
// be an exact question, and a double that lands on 1.9999999 would build an
// intermediate to fix a factor that was already whole. Nothing overflows — the
// products are at most a screen dimension times 512, and they are done in long.
static inline ScalePlan scale_plan(ScaleMode mode, int sw, int sh, int aw,
                                   int ah, int ow, int oh, bool can_target) {
  ScalePlan p;
  p.stage_x = p.stage_y = 0;
  p.linear = false;
  p.dst.x = p.dst.y = p.dst.w = p.dst.h = 0;
  if (sw <= 0 || sh <= 0 || ow <= 0 || oh <= 0) return p;
  if (aw <= 0 || ah <= 0) { aw = sw; ah = sh; }

  // The largest rectangle of the *intended* shape that fits — which is not the
  // source's shape once aspect correction is asked for.
  ScaleRect fit;
  if ((long)ow * ah <= (long)oh * aw) {  // the window is the narrower shape
    fit.w = ow;
    fit.h = (int)((long)ow * ah / aw);
  } else {
    fit.h = oh;
    fit.w = (int)((long)oh * aw / ah);
  }
  // A window narrower than the aspect step rounds the other axis to nothing —
  // 1 pixel wide gives 1 * 448 / 512 = 0 rows — and a zero-sized rectangle is
  // not something a caller should have to special-case. Found by the sweep in
  // `tools/test_scale.c` rather than by thinking of it, which is the argument
  // for sweeping degenerate sizes at all.
  if (fit.w < 1) fit.w = 1;
  if (fit.h < 1) fit.h = 1;

  p.dst = fit;
  // `integer` means whole multiples of the source on both axes, which forces
  // square pixels and therefore ignores the requested aspect. That is the
  // honest reading of the request: a whole multiple of a 512x448 image is a
  // 512x448-shaped image, and stretching one to 4:3 would put it back exactly
  // where the fractional factors it exists to avoid live.
  int whole = ow / sw;
  if (oh / sh < whole) whole = oh / sh;
  if (mode == SCALE_INTEGER && whole >= 1) {
    p.dst.w = sw * whole;
    p.dst.h = sh * whole;
  }
  p.dst.x = (ow - p.dst.w) / 2;
  p.dst.y = (oh - p.dst.h) / 2;

  // Magnifying by a whole number on each axis needs no help from anybody:
  // nearest reproduces every block at the same size and a bilinear step could
  // only soften it. The two multiples need not be *equal* — a 6-wide, 5-tall
  // block is still uniform across the picture — which is what lets aspect
  // correction be pixel-exact rather than automatically soft.
  const bool magnifying = p.dst.w >= sw && p.dst.h >= sh;
  const bool exact = magnifying && p.dst.w % sw == 0 && p.dst.h % sh == 0;
  if (exact || (mode == SCALE_INTEGER && whole >= 1)) return p;

  // Below 1:1 on either axis there is nothing to be sharp about — the picture
  // is being *reduced*, every mode is throwing pixels away, and bilinear throws
  // them away better. `integer` lands here too when the window is too small to
  // hold even one whole copy, because showing the middle of an unscaled frame
  // through a letterbox would be worse than showing all of a shrunken one.
  if (!magnifying) {
    p.linear = true;
    return p;
  }

  if (mode == SCALE_LINEAR) {
    p.linear = true;
    return p;
  }

  // `sharp`, magnifying, and not by a whole number on at least one axis: go up
  // to the next whole multiple on each axis with nearest, and come back down
  // with one bilinear step. Rounding *up* is what makes the second step a
  // shrink; a stage smaller than the picture drawn from it would be a bilinear
  // magnification, which is the blur this mode exists to avoid.
  if (mode == SCALE_SHARP && can_target) {
    const int sx = (p.dst.w + sw - 1) / sw;
    const int sy = (p.dst.h + sh - 1) / sh;
    if (sx <= SCALE_MAX_STAGE && sy <= SCALE_MAX_STAGE) {
      p.stage_x = sx;
      p.stage_y = sy;
      p.linear = true;
    }
  }
  // ...and if the renderer cannot hold an intermediate, or the magnification is
  // past anything one could improve, `p` is left as a plain nearest copy. That
  // is a degradation and not a failure: it is exactly what asking for nearest
  // would have given.
  return p;
}

#endif
