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
//   * nothing may be off-centre, spill out of the window, or come out the wrong
//     shape, in any mode, at any size.
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

#define FB_W 512
#define FB_H 480

static int failures;

// One broken invariant is broken at thousands of sizes, and a wall of identical
// lines buries whatever else is wrong. Print the first twenty and count the
// rest.
#define FAIL_SHOW 20

static void fail(const char* what, int ow, int oh, ScaleMode m) {
  if (failures < FAIL_SHOW)
    printf("FAIL %-46s  %s at %dx%d\n", what, scale_name(m), ow, oh);
  else if (failures == FAIL_SHOW)
    printf("... and more; only the first %d are shown.\n", FAIL_SHOW);
  failures++;
}

// The invariants that hold for every mode at every size.
static void check_common(ScaleMode m, int ow, int oh, ScalePlan p) {
  if (p.dst.w <= 0 || p.dst.h <= 0) {
    fail("empty rectangle", ow, oh, m);
    return;
  }
  if (p.dst.x < 0 || p.dst.y < 0 ||
      p.dst.x + p.dst.w > ow || p.dst.y + p.dst.h > oh)
    fail("picture is outside the window", ow, oh, m);
  // Centred, to within the odd pixel that an odd difference leaves over.
  if (abs((ow - p.dst.w) - 2 * p.dst.x) > 1 ||
      abs((oh - p.dst.h) - 2 * p.dst.y) > 1)
    fail("not centred", ow, oh, m);
  // The right shape: 512x480 is 16:15, and the rounding on one axis is at most
  // a pixel, so cross-multiplying may be off by at most one row's worth.
  long lhs = (long)p.dst.w * FB_H, rhs = (long)p.dst.h * FB_W;
  if (labs(lhs - rhs) > FB_W)
    fail("wrong aspect ratio", ow, oh, m);
  if (p.stage < 0 || p.stage > SCALE_MAX_STAGE)
    fail("intermediate outside its cap", ow, oh, m);
  if (p.stage > 0 && !p.linear)
    fail("built an intermediate and then did not shrink from it", ow, oh, m);
}

int main(void) {
  // 1..3000 covers everything from a window smaller than the framebuffer to a
  // 5K display, and crosses every whole multiple on both axes on the way.
  for (int ow = 1; ow <= 3000; ow += 1) {
    for (int oh = 1; oh <= 3000; oh += 7) {  // coprime with the multiples
      for (int mi = 0; mi < SCALE_MODE_COUNT; mi++) {
        ScaleMode m = (ScaleMode)mi;
        ScalePlan p = scale_plan(m, FB_W, FB_H, ow, oh, true);
        check_common(m, ow, oh, p);

        int whole = ow / FB_W;
        if (oh / FB_H < whole) whole = oh / FB_H;

        if (m == SCALE_INTEGER && whole >= 1) {
          if (p.dst.w != FB_W * whole || p.dst.h != FB_H * whole)
            fail("integer mode did not land on a whole multiple", ow, oh, m);
          if (p.linear || p.stage)
            fail("integer mode asked for filtering", ow, oh, m);
        }

        // An exact fit is the case every mode should agree on.
        if (whole >= 1 && p.dst.w == FB_W * whole && p.dst.h == FB_H * whole) {
          if (p.stage)
            fail("built an intermediate for a whole multiple", ow, oh, m);
          if (p.linear && m != SCALE_LINEAR)
            fail("filtered a whole multiple", ow, oh, m);
        }

        // The point of the whole exercise: magnifying by a fraction must never
        // be left to plain nearest.
        if (m == SCALE_SHARP && whole >= 1 &&
            (p.dst.w != FB_W * whole || p.dst.h != FB_H * whole)) {
          if (whole + 1 <= SCALE_MAX_STAGE) {
            if (p.stage != whole + 1)
              fail("sharp did not step up to the next whole multiple", ow, oh, m);
            if (p.dst.w > FB_W * p.stage || p.dst.h > FB_H * p.stage)
              fail("intermediate is smaller than the picture drawn from it",
                   ow, oh, m);
          } else if (p.stage) {
            fail("built an intermediate past the cap", ow, oh, m);
          }
        }
      }
    }
  }

  // A renderer with no render-target support must degrade rather than break.
  for (int ow = 600; ow <= 2000; ow += 37) {
    ScalePlan p = scale_plan(SCALE_SHARP, FB_W, FB_H, ow, ow, false);
    if (p.stage) fail("built an intermediate without target support", ow, ow,
                      SCALE_SHARP);
    check_common(SCALE_SHARP, ow, ow, p);
  }

  // ...and the named cases, spelled out, because a sweep proves the properties
  // and these say what the answers actually are.
  struct { ScaleMode m; int ow, oh, x, y, w, h, stage; bool linear; } want[] = {
      // Exactly 1x and exactly 2x: nearest, no intermediate, no letterbox.
      {SCALE_SHARP,    512,  480,   0,   0,  512,  480, 0, false},
      {SCALE_SHARP,   1024,  960,   0,   0, 1024,  960, 0, false},
      {SCALE_INTEGER, 1024,  960,   0,   0, 1024,  960, 0, false},
      // 1080p: 2x fits, 3x needs 1440. `integer` letterboxes to 1024x960 and
      // `sharp` fills the height and steps down from 3x.
      {SCALE_INTEGER, 1920, 1080, 448,  60, 1024,  960, 0, false},
      {SCALE_SHARP,   1920, 1080, 384,   0, 1152, 1080, 3, true},
      {SCALE_LINEAR,  1920, 1080, 384,   0, 1152, 1080, 0, true},
      // Smaller than the framebuffer: every mode reduces, and reduces smoothly.
      {SCALE_SHARP,    256,  240,   0,   0,  256,  240, 0, true},
      {SCALE_INTEGER,  256,  240,   0,   0,  256,  240, 0, true},
      // Past the cap: nearest, and no 10x intermediate. A square window is
      // width-constrained for a 16:15 picture, so this letterboxes top and
      // bottom rather than left and right — which is what the first draft of
      // this row got backwards.
      {SCALE_SHARP,   5000, 5000,   0, 156, 5000, 4687, 0, false},
  };
  for (int i = 0; i < (int)(sizeof want / sizeof *want); i++) {
    ScalePlan p = scale_plan(want[i].m, FB_W, FB_H, want[i].ow, want[i].oh, true);
    if (p.dst.x != want[i].x || p.dst.y != want[i].y || p.dst.w != want[i].w ||
        p.dst.h != want[i].h || p.stage != want[i].stage ||
        p.linear != want[i].linear) {
      printf("FAIL %s at %dx%d: got %d,%d %dx%d stage %d %s;"
             " want %d,%d %dx%d stage %d %s\n",
             scale_name(want[i].m), want[i].ow, want[i].oh, p.dst.x, p.dst.y,
             p.dst.w, p.dst.h, p.stage, p.linear ? "linear" : "nearest",
             want[i].x, want[i].y, want[i].w, want[i].h, want[i].stage,
             want[i].linear ? "linear" : "nearest");
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
