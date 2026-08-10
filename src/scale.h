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
// Aspect ratio is left where it was: square pixels, letterboxed to fit, which
// is what `SDL_RenderSetLogicalSize` was doing before any of this existed.
// Correcting to the SNES's real 8:7 pixel aspect is a different change with a
// different argument, and one that fights this one — 4:3 needs a fractional
// horizontal factor by definition.

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

typedef struct { int x, y, w, h; } ScaleRect;

typedef struct {
  ScaleRect dst;  // where the picture lands, in output pixels
  // 0 to copy the framebuffer straight to `dst`; otherwise the multiple of the
  // framebuffer to draw nearest into first, and then shrink from.
  int stage;
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
static inline ScalePlan scale_plan(ScaleMode mode, int sw, int sh, int ow,
                                   int oh, bool can_target) {
  ScalePlan p;
  p.stage = 0;
  p.linear = false;
  p.dst.x = p.dst.y = p.dst.w = p.dst.h = 0;
  if (sw <= 0 || sh <= 0 || ow <= 0 || oh <= 0) return p;

  // The largest rectangle of the source's shape that fits.
  ScaleRect fit;
  if ((long)ow * sh <= (long)oh * sw) {  // the window is the narrower shape
    fit.w = ow;
    fit.h = (int)((long)ow * sh / sw);
  } else {
    fit.h = oh;
    fit.w = (int)((long)oh * sw / sh);
  }
  // A window narrower than the aspect step rounds the other axis to nothing —
  // 1 pixel wide gives 1 * 480 / 512 = 0 rows — and a zero-sized rectangle is
  // not something a caller should have to special-case. Found by the sweep in
  // `tools/test_scale.c` rather than by thinking of it, which is the argument
  // for sweeping degenerate sizes at all.
  if (fit.w < 1) fit.w = 1;
  if (fit.h < 1) fit.h = 1;

  // The whole multiple at or below that, and whether the fit already is one.
  int whole = ow / sw;
  if (oh / sh < whole) whole = oh / sh;
  const bool exact = whole >= 1 && fit.w == sw * whole && fit.h == sh * whole;

  p.dst = fit;
  if (mode == SCALE_INTEGER && whole >= 1) {
    p.dst.w = sw * whole;
    p.dst.h = sh * whole;
  }
  p.dst.x = (ow - p.dst.w) / 2;
  p.dst.y = (oh - p.dst.h) / 2;

  // An exact multiple needs no help from anybody: nearest reproduces every
  // block at the same size, and a bilinear step could only soften it. This is
  // the case `integer` mode arranges on purpose and the other two get for free
  // whenever the window happens to be the right size.
  if (exact || (mode == SCALE_INTEGER && whole >= 1)) return p;

  // Below 1:1 there is nothing to be sharp about — the picture is being
  // *reduced*, every mode is throwing pixels away, and bilinear throws them
  // away better. `integer` lands here too when the window is too small to hold
  // even one whole copy, because showing the middle of an unscaled frame
  // through a letterbox would be worse than showing all of a shrunken one.
  if (whole < 1) {
    p.linear = true;
    return p;
  }

  if (mode == SCALE_LINEAR) {
    p.linear = true;
    return p;
  }

  // `sharp`, magnifying, and not by a whole number: go up to the next whole
  // multiple with nearest and come back down with one bilinear step.
  if (mode == SCALE_SHARP && can_target && whole + 1 <= SCALE_MAX_STAGE) {
    p.stage = whole + 1;
    p.linear = true;
  }
  // ...and if the renderer cannot hold an intermediate, or the magnification is
  // past anything one could improve, `p` is left as a plain nearest copy. That
  // is a degradation and not a failure: it is exactly what asking for nearest
  // would have given.
  return p;
}

#endif
