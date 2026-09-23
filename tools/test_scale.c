// What `scale_plan` promises, asserted over every window size worth trying.
//
// The properties below are the whole of what "sharp pixel upscaling" means, and
// each of them is a thing that was got wrong at least once while writing it:
//
//   * a whole-multiple window must be nearest and must build no intermediate,
//     because a bilinear step there can only soften an exact answer;
//   * `integer` must land on a whole multiple whenever one fits, so every game
//     pixel is the same size;
//   * `sharp` must never magnify with plain nearest — the fractional factor is
//     the artifact, and leaving it unfixed is the bug this mode exists for;
//   * an intermediate must never be *smaller* than the picture drawn from it,
//     or the second step is a bilinear magnification and the mode has produced
//     exactly the blur it exists to avoid. This one only became reachable when
//     aspect correction made the two axes scale by different amounts;
//   * nothing may be off-centre, spill out of the window, or come out the wrong
//     shape, in any mode, at any size, in either aspect.
//
// It is arithmetic with no SDL in it, so it runs anywhere and in no time:
//
//     cc -I src -o test_scale tools/test_scale.c && ./test_scale
//
// or, on Windows, `cl /I src tools\test_scale.c`. The build wires it up as the
// `zamn_test_scale` target.

#include <stdio.h>
#include <stdlib.h>

#include "scale.h"

// The *live* framebuffer, not the whole one: the core blanks sixteen rows top
// and bottom, and everything here is measured against the picture that is left.
#define FB_W 512
#define FB_H 448

static int failures;

// One broken invariant is broken at thousands of sizes, and a wall of identical
// lines buries whatever else is wrong. Print the first twenty and count the
// rest.
#define FAIL_SHOW 20

static void fail(const char* what, int ow, int oh, ScaleMode m, AspectMode a) {
  if (failures < FAIL_SHOW)
    printf("FAIL %-46s  %s/%s at %dx%d\n", what, scale_name(m), aspect_name(a),
           ow, oh);
  else if (failures == FAIL_SHOW)
    printf("... and more; only the first %d are shown.\n", FAIL_SHOW);
  failures++;
}

// The invariants that hold for every mode, every aspect, every size.
static void check_common(ScaleMode m, AspectMode a, int aw, int ah, int ow,
                         int oh, ScalePlan p) {
  if (p.dst.w <= 0 || p.dst.h <= 0) {
    fail("empty rectangle", ow, oh, m, a);
    return;
  }
  if (p.dst.x < 0 || p.dst.y < 0 || p.dst.x + p.dst.w > ow ||
      p.dst.y + p.dst.h > oh)
    fail("picture is outside the window", ow, oh, m, a);
  // Centred, to within the odd pixel that an odd difference leaves over.
  if (abs((ow - p.dst.w) - 2 * p.dst.x) > 1 ||
      abs((oh - p.dst.h) - 2 * p.dst.y) > 1)
    fail("not centred", ow, oh, m, a);

  const int whole_w = ow / FB_W, whole_h = oh / FB_H;
  const int whole = whole_w < whole_h ? whole_w : whole_h;
  // `integer` deliberately ignores the requested aspect — a whole multiple of a
  // 512x448 image is a 512x448-shaped image — so it is exempt from the shape
  // check whenever it actually managed to land on a multiple.
  const bool integer_won = m == SCALE_INTEGER && whole >= 1;
  if (!integer_won) {
    // Cross-multiplied, with a tolerance of one rounding step on each side:
    // `fit` divides in integers, so one axis can be a pixel short of exact.
    const long lhs = (long)p.dst.w * ah, rhs = (long)p.dst.h * aw;
    if (labs(lhs - rhs) > (long)aw + ah)
      fail("wrong aspect ratio", ow, oh, m, a);
  }

  if (p.stage_x < 0 || p.stage_x > SCALE_MAX_STAGE || p.stage_y < 0 ||
      p.stage_y > SCALE_MAX_STAGE)
    fail("intermediate outside its cap", ow, oh, m, a);
  if ((p.stage_x > 0) != (p.stage_y > 0))
    fail("intermediate sized on one axis only", ow, oh, m, a);
  if (p.stage_x > 0) {
    if (!p.linear)
      fail("built an intermediate and then did not shrink from it", ow, oh, m, a);
    // The property that aspect correction made reachable: shrinking from the
    // intermediate is only a shrink if it is at least as large as the target.
    if (p.stage_x * FB_W < p.dst.w || p.stage_y * FB_H < p.dst.h)
      fail("intermediate is smaller than the picture drawn from it", ow, oh, m, a);
  }
}

int main(void) {
  // 1..3000 covers everything from a window smaller than the framebuffer to a
  // 5K display, and crosses every whole multiple on both axes on the way.
  for (int ow = 1; ow <= 3000; ow += 1) {
    for (int oh = 1; oh <= 3000; oh += 7) {  // coprime with the multiples
      for (int ai = 0; ai < ASPECT_MODE_COUNT; ai++) {
        const AspectMode a = (AspectMode)ai;
        int aw = 0, ah = 0;
        aspect_ratio(a, FB_W, FB_H, &aw, &ah);
        for (int mi = 0; mi < SCALE_MODE_COUNT; mi++) {
          const ScaleMode m = (ScaleMode)mi;
          const ScalePlan p =
              scale_plan(m, FB_W, FB_H, aw, ah, ow, oh, true);
          check_common(m, a, aw, ah, ow, oh, p);

          int whole = ow / FB_W;
          if (oh / FB_H < whole) whole = oh / FB_H;

          if (m == SCALE_INTEGER && whole >= 1) {
            if (p.dst.w != FB_W * whole || p.dst.h != FB_H * whole)
              fail("integer mode did not land on a whole multiple", ow, oh, m, a);
            if (p.linear || p.stage_x || p.stage_y)
              fail("integer mode asked for filtering", ow, oh, m, a);
          }

          // A whole multiple on each axis is the case every mode agrees on —
          // and the two multiples need not be equal, which is what lets a 4:3
          // picture be pixel-exact with oblong pixels.
          const bool magnifying = p.dst.w >= FB_W && p.dst.h >= FB_H;
          const bool exact =
              magnifying && p.dst.w % FB_W == 0 && p.dst.h % FB_H == 0;
          if (exact) {
            if (p.stage_x || p.stage_y)
              fail("built an intermediate for a whole multiple", ow, oh, m, a);
            if (p.linear && m != SCALE_LINEAR)
              fail("filtered a whole multiple", ow, oh, m, a);
          }

          // The point of the whole exercise: magnifying by a fraction must
          // never be left to plain nearest.
          if (m == SCALE_SHARP && magnifying && !exact) {
            const int want_x = (p.dst.w + FB_W - 1) / FB_W;
            const int want_y = (p.dst.h + FB_H - 1) / FB_H;
            if (want_x <= SCALE_MAX_STAGE && want_y <= SCALE_MAX_STAGE) {
              if (p.stage_x != want_x || p.stage_y != want_y)
                fail("sharp did not step up to the next whole multiple", ow, oh,
                     m, a);
            } else if (p.stage_x || p.stage_y) {
              fail("built an intermediate past the cap", ow, oh, m, a);
            }
          }
        }
      }
    }
  }

  // A renderer with no render-target support must degrade rather than break.
  for (int ow = 600; ow <= 2000; ow += 37) {
    int aw = 0, ah = 0;
    aspect_ratio(ASPECT_43, FB_W, FB_H, &aw, &ah);
    const ScalePlan p =
        scale_plan(SCALE_SHARP, FB_W, FB_H, aw, ah, ow, ow, false);
    if (p.stage_x || p.stage_y)
      fail("built an intermediate without target support", ow, ow, SCALE_SHARP,
           ASPECT_43);
    check_common(SCALE_SHARP, ASPECT_43, aw, ah, ow, ow, p);
  }

  // ...and the named cases, spelled out, because a sweep proves the properties
  // and these say what the answers actually are.
  struct Case {
    ScaleMode m;
    AspectMode a;
    int ow, oh, x, y, w, h, sx, sy;
    bool linear;
  };
  const struct Case cases[] = {
      // 4K, the resolution fullscreen actually lands on here.
      {SCALE_SHARP, ASPECT_43,     3840, 2160,  480,   0, 2880, 2160, 6, 5, true},
      {SCALE_SHARP, ASPECT_SQUARE, 3840, 2160,  686,   0, 2468, 2160, 5, 5, true},
      // 1080p and 1440p.
      {SCALE_SHARP, ASPECT_43,     1920, 1080,  240,   0, 1440, 1080, 3, 3, true},
      {SCALE_SHARP, ASPECT_43,     2560, 1440,  320,   0, 1920, 1440, 4, 4, true},
      // The laptop panel that is a multiple of nothing.
      {SCALE_SHARP, ASPECT_43,     1366,  768,  171,   0, 1024,  768, 2, 2, true},
      // Pixel-exact 4:3: 7 source widths by 6 source heights is exactly 4:3, so
      // this one needs no filtering at all despite the pixels being oblong.
      // That is the property `stage_x != stage_y` was introduced to allow.
      {SCALE_SHARP, ASPECT_43,     3584, 2688,    0,   0, 3584, 2688, 0, 0, false},
      // Pixel-exact square, for comparison: 4x on both axes.
      {SCALE_SHARP, ASPECT_SQUARE, 2048, 1792,    0,   0, 2048, 1792, 0, 0, false},
      // ...and the same window asked for 4:3, which is exact across and not
      // down, so it stages on both and shrinks only the vertical.
      {SCALE_SHARP, ASPECT_43,     2048, 1792,    0, 128, 2048, 1536, 4, 4, true},
      // `integer` ignores the aspect and letterboxes to whole multiples.
      {SCALE_INTEGER, ASPECT_43,   1920, 1080,  448,  92, 1024,  896, 0, 0, false},
      // Smaller than the source: every mode reduces, and reduces smoothly.
      {SCALE_SHARP, ASPECT_43,      256,  240,    0,  24,  256,  192, 0, 0, true},
      // Past the cap: nearest, and no 10x intermediate.
      {SCALE_SHARP, ASPECT_43,     5000, 5000,    0, 625, 5000, 3750, 0, 0, false},
  };
  for (int i = 0; i < (int)(sizeof cases / sizeof *cases); i++) {
    const struct Case* c = &cases[i];
    int aw = 0, ah = 0;
    aspect_ratio(c->a, FB_W, FB_H, &aw, &ah);
    const ScalePlan p =
        scale_plan(c->m, FB_W, FB_H, aw, ah, c->ow, c->oh, true);
    if (p.dst.x != c->x || p.dst.y != c->y || p.dst.w != c->w ||
        p.dst.h != c->h || p.stage_x != c->sx || p.stage_y != c->sy ||
        p.linear != c->linear) {
      printf("FAIL %s/%s at %dx%d: got %d,%d %dx%d stage %dx%d %s;"
             " want %d,%d %dx%d stage %dx%d %s\n",
             scale_name(c->m), aspect_name(c->a), c->ow, c->oh, p.dst.x, p.dst.y,
             p.dst.w, p.dst.h, p.stage_x, p.stage_y,
             p.linear ? "linear" : "nearest", c->x, c->y, c->w, c->h, c->sx,
             c->sy, c->linear ? "linear" : "nearest");
      failures++;
    }
  }

  // `auto`: the width that fills each display best, at the 448 rows the
  // frontend shows.
  static const struct { AspectMode a; int ow, oh; WideMode want; } wides[] = {
      {ASPECT_43,     3840, 2160, WIDE_16_9},
      {ASPECT_43,     1920, 1080, WIDE_16_9},
      {ASPECT_43,     2560, 1600, WIDE_16_10},
      {ASPECT_43,     1920, 1200, WIDE_16_10},
      {ASPECT_43,     1024,  768, WIDE_OFF},
      {ASPECT_43,     1280, 1024, WIDE_OFF},
      {ASPECT_43,     3440, 1440, WIDE_16_9},   // ultrawide: the widest there is
      {ASPECT_SQUARE, 3840, 2160, WIDE_16_9},
      {ASPECT_SQUARE, 1024,  768, WIDE_16_10},  // 616x448 is nearer 4:3 than 512x448
      {ASPECT_43,        0,    0, WIDE_OFF},
  };
  for (int i = 0; i < (int)(sizeof wides / sizeof *wides); i++) {
    const WideMode got = wide_for_display(wides[i].a, 448, wides[i].ow, wides[i].oh);
    if (got != wides[i].want) {
      printf("FAIL wide_for_display %s at %dx%d: got %s, want %s\n",
             aspect_name(wides[i].a), wides[i].ow, wides[i].oh, wide_name(got),
             wide_name(wides[i].want));
      failures++;
    }
  }
  {
    WideMode w = WIDE_OFF;
    if (!wide_setting_parse("auto", &w) || w != WIDE_AUTO) {
      printf("FAIL the setting does not take auto\n");
      failures++;
    }
    if (wide_parse("auto", &w)) {
      printf("FAIL a width took auto\n");
      failures++;
    }
    if (wide_margin(WIDE_AUTO) != 0) {
      printf("FAIL auto has a margin of its own\n");
      failures++;
    }
  }

  if (failures) {
    printf("\n%d failure%s.\n", failures, failures == 1 ? "" : "s");
    return 1;
  }
  printf("scale_plan: all checks passed.\n");
  return 0;
}
