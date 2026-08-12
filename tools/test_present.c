// Does `sharp` actually come out sharp?
//
// `zamn_test_scale` proves the arithmetic — the right rectangle, the right
// decisions. It cannot prove the thing those decisions exist for, because that
// depends on SDL doing what the plan asked: nearest *into* the intermediate and
// bilinear *out* of it. Get those two the wrong way round and every check in
// that file still passes, while the picture is a blurred mess. So this one runs
// the real `present_draw` against a software renderer, reads the pixels back,
// and measures them.
//
// The measure is **purity**: what share of the output pixels are exactly one of
// the colours that went in, rather than a blend of two. It separates the three
// modes cleanly and needs no reference image:
//
//   * an exact whole multiple is 100% pure in every mode but `linear` — there
//     is nothing to interpolate between when samples land on pixel centres;
//   * `sharp` at a fractional size is mostly pure, because only the seam
//     between two blocks gets blended;
//   * `linear` at the same size is mostly blended, which is what it is for.
//
// Purity is the discriminator, and the run-width check below is only a sanity
// test — worth being clear about, because the obvious framing is wrong. Plain
// nearest at a fractional factor produces runs of N and N+1, and so does
// `sharp`; what differs is that nearest's boundary *jumps* a whole pixel from
// one block to the next while sharp's is a fixed soft seam, so the block
// centres advance evenly. That difference shows up as purity and not as run
// width, which is why the fallback path — `sharp` on a renderer that cannot
// hold an intermediate, which is exactly plain nearest — is measured here too
// and asserted to look different from `sharp` proper.
//
// Software renderer throughout, so there is no GPU and no display: it runs on a
// build machine. `SDL_VIDEODRIVER=dummy` is set from inside.

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL.h>

#include "present.h"

#define SRC_W 512
#define SRC_H 480
// The live rectangle, matching what the core actually produces: rows 16..463
// are picture and the rest is blanked. `present_draw` must scale only this, and
// `COL_DEAD` below is how that gets checked rather than assumed.
#define SRC_TOP 16
#define SRC_LIVE_H 448

// Two colours, laid out the way the PPU lays out this game: every game pixel is
// a 2x2 block, because `ppu_handlePixel` writes each one twice across and
// `ppu_putPixels` copies each row twice down.
//
// `SDL_PIXELFORMAT_RGBX8888` packs a pixel as `R<<24 | G<<16 | B<<8 | X`, so the
// **ignored byte is the low one** and the mask that keeps the colour is
// `0xFFFFFF00`. Masking `0x00FFFFFF` instead — the habit from ARGB — keeps
// green, blue and the padding while throwing red away, which made every
// comparison here fail and every mode read 0% pure.
#define COL_A 0x20408000u
#define COL_B 0xE0C0A000u
#define COL_MASK 0xFFFFFF00u
// Painted into the rows the core blanks. It is deliberately not black, so that
// "the dead rows were cropped" and "the dead rows were drawn but happened to be
// the same colour as the letterbox" are distinguishable — which they would not
// be if this were 0.
#define COL_DEAD 0x00FF0000u
#define BLOCK 2

static int failures;

static void fail(const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  printf("FAIL ");
  vprintf(fmt, ap);
  printf("\n");
  va_end(ap);
  failures++;
}

// A checkerboard of 2x2 blocks, straight into the streaming texture.
static void fill_source(Present* p) {
  void* pixels; int pitch;
  if (SDL_LockTexture(p->frame, NULL, &pixels, &pitch) != 0) {
    fail("cannot lock the source texture: %s", SDL_GetError());
    return;
  }
  for (int y = 0; y < SRC_H; y++) {
    uint32_t* row = (uint32_t*)((uint8_t*)pixels + (size_t)y * pitch);
    const bool live = y >= SRC_TOP && y < SRC_TOP + SRC_LIVE_H;
    for (int x = 0; x < SRC_W; x++) {
      if (!live) { row[x] = COL_DEAD; continue; }
      // The checkerboard is phased from the top of the *live* area, so a
      // cropping error shifts it and the block-width checks notice.
      const int ly = y - SRC_TOP;
      row[x] = (((x / BLOCK) + (ly / BLOCK)) & 1) ? COL_A : COL_B;
    }
  }
  SDL_UnlockTexture(p->frame);
}

// Everything the renderer drew, as 32-bit pixels.
static uint32_t* readback(SDL_Renderer* ren, int w, int h) {
  uint32_t* px = (uint32_t*)malloc((size_t)w * h * 4);
  if (!px) return NULL;
  if (SDL_RenderReadPixels(ren, NULL, SDL_PIXELFORMAT_RGBX8888, px, w * 4) != 0) {
    fail("read back failed: %s", SDL_GetError());
    free(px);
    return NULL;
  }
  return px;
}

// The share of pixels inside `rect` that are exactly one of the source colours.
// Black is excluded because the letterbox is not part of the picture.
static double purity(const uint32_t* px, int w, ScaleRect r) {
  long pure = 0, total = 0;
  for (int y = r.y; y < r.y + r.h; y++) {
    for (int x = r.x; x < r.x + r.w; x++) {
      uint32_t c = px[(size_t)y * w + x] & COL_MASK;
      total++;
      if (c == COL_A || c == COL_B) pure++;
    }
  }
  return total ? (double)pure / (double)total : 0.0;
}

// The row inside `r` with the most pure pixels in it.
//
// Which row is sampled is not a detail. Vertical scaling blends too, so a row
// picked arbitrarily — the middle one, say — can land squarely on a seam
// between two source rows and come back with almost nothing pure in it. The
// first version of this file did exactly that and reported "blocks 0..0",
// which made the uniformity check below pass by having nothing to check. A row
// that landed on the middle of a block is the one that has something to say
// about block widths.
static int best_row(const uint32_t* px, int w, ScaleRect r) {
  int best = r.y;
  long best_pure = -1;
  for (int y = r.y; y < r.y + r.h; y++) {
    long pure = 0;
    for (int x = r.x; x < r.x + r.w; x++) {
      uint32_t c = px[(size_t)y * w + x] & COL_MASK;
      if (c == COL_A || c == COL_B) pure++;
    }
    if (pure > best_pure) { best_pure = pure; best = y; }
  }
  return best;
}

// Walk one row and collect the width of each run of a pure source colour,
// ignoring the blended seams between them. Returns the narrowest and widest
// interior run — the first and last touch the edge of the picture and are
// legitimately short.
static void run_widths(const uint32_t* px, int w, ScaleRect r, int row,
                       int* lo, int* hi) {
  *lo = 1 << 30;
  *hi = 0;
  int run = 0;
  uint32_t prev = 0;
  int seen = 0;
  for (int x = r.x; x < r.x + r.w; x++) {
    uint32_t c = px[(size_t)row * w + x] & COL_MASK;
    bool is_pure = (c == COL_A || c == COL_B);
    if (is_pure && c == prev) {
      run++;
      continue;
    }
    // A run ended. Count it unless it was the first (clipped by the edge).
    if (run > 0 && seen++ > 0 && x < r.x + r.w - 1) {
      if (run < *lo) *lo = run;
      if (run > *hi) *hi = run;
    }
    run = is_pure ? 1 : 0;
    prev = is_pure ? c : 0;
  }
  if (*hi == 0) { *lo = 0; }
}

// One mode at one window size: draw it, read it back, and hand the caller the
// numbers.
static bool measure_ex(ScaleMode mode, int ow, int oh, bool allow_target,
                       double* out_purity, int* out_lo, int* out_hi,
                       int* out_stage) {
  SDL_Window* win = SDL_CreateWindow("t", 0, 0, ow, oh, SDL_WINDOW_HIDDEN);
  if (!win) { fail("no window: %s", SDL_GetError()); return false; }
  SDL_Renderer* ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
  if (!ren) { fail("no renderer: %s", SDL_GetError()); SDL_DestroyWindow(win); return false; }

  Present p;
  const SDL_Rect live = {0, SRC_TOP, SRC_W, SRC_LIVE_H};
  if (!present_init(&p, ren, SRC_W, SRC_H, live, mode, ASPECT_SQUARE)) {
    fail("present_init: %s", SDL_GetError());
    SDL_DestroyRenderer(ren); SDL_DestroyWindow(win);
    return false;
  }
  // Pretending the renderer cannot hold an intermediate is how the degraded
  // path gets exercised; it is the only difference between `sharp` and plain
  // nearest, so it is the comparison that says what the mode buys.
  if (!allow_target) p.can_target = false;
  fill_source(&p);
  present_draw(&p);

  int aw = 0, ah = 0;
  aspect_ratio(ASPECT_SQUARE, SRC_W, SRC_LIVE_H, &aw, &ah);
  ScalePlan plan =
      scale_plan(mode, SRC_W, SRC_LIVE_H, aw, ah, ow, oh, p.can_target);
  *out_stage = plan.stage_x;

  bool ok = false;
  uint32_t* px = readback(ren, ow, oh);
  if (px) {
    *out_purity = purity(px, ow, plan.dst);
    run_widths(px, ow, plan.dst, best_row(px, ow, plan.dst), out_lo, out_hi);
    // The blanked rows must reach the screen nowhere, in any mode, at any size.
    // A bilinear step can only mix colours that were drawn, so a single trace of
    // `COL_DEAD` anywhere in the output means the crop was not applied.
    for (int i = 0; i < ow * oh; i++) {
      if ((px[i] & COL_MASK) == (COL_DEAD & COL_MASK)) {
        fail("blanked source rows reached the screen (%s at %dx%d)",
             scale_name(mode), ow, oh);
        break;
      }
    }
    free(px);
    ok = true;
  }

  present_free(&p);
  SDL_DestroyRenderer(ren);
  SDL_DestroyWindow(win);
  return ok;
}

static bool measure(ScaleMode mode, int ow, int oh, double* out_purity,
                    int* out_lo, int* out_hi, int* out_stage) {
  return measure_ex(mode, ow, oh, true, out_purity, out_lo, out_hi, out_stage);
}

// Mean brightness of the whole output after a dim of `amount`.
//
// `present_dim` is the quit gesture's only feedback, and its interesting failure
// is not "nothing happens" but "everything happens at once": with the blend mode
// left at the renderer's default, every amount from 1 to 255 paints opaque
// black, so the fade becomes a cut and looks fine in any single frame. A mean
// taken at three amounts separates those two behaviours in one number.
static bool measure_dim(int amount, double* out_mean) {
  const int ow = SRC_W, oh = SRC_LIVE_H;
  SDL_Window* win = SDL_CreateWindow("t", 0, 0, ow, oh, SDL_WINDOW_HIDDEN);
  if (!win) { fail("no window: %s", SDL_GetError()); return false; }
  SDL_Renderer* ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
  if (!ren) { fail("no renderer: %s", SDL_GetError()); SDL_DestroyWindow(win); return false; }
  Present p;
  const SDL_Rect live = {0, SRC_TOP, SRC_W, SRC_LIVE_H};
  bool ok = false;
  if (!present_init(&p, ren, SRC_W, SRC_H, live, SCALE_INTEGER, ASPECT_SQUARE)) {
    fail("present_init: %s", SDL_GetError());
  } else {
    fill_source(&p);
    present_draw(&p);
    present_dim(&p, amount);
    uint32_t* px = readback(ren, ow, oh);
    if (px) {
      double sum = 0.0;
      for (int i = 0; i < ow * oh; i++)
        sum += ((px[i] >> 24) & 0xff) + ((px[i] >> 16) & 0xff) +
               ((px[i] >> 8) & 0xff);
      *out_mean = sum / (3.0 * ow * oh);
      free(px);
      ok = true;
    }
    present_free(&p);
  }
  SDL_DestroyRenderer(ren);
  SDL_DestroyWindow(win);
  return ok;
}

// `argc`/`argv` rather than `void`: on Windows SDL redefines `main` as its own
// `SDL_main`, which is declared with a parameter list, and a `void` definition
// disagrees with it (MSVC C4026).
int main(int argc, char** argv) {
  (void)argc;
  (void)argv;
  SDL_SetHint(SDL_HINT_VIDEODRIVER, "dummy");
  if (SDL_Init(SDL_INIT_VIDEO) != 0) {
    printf("SDL_Init failed: %s\n", SDL_GetError());
    return 1;
  }

  double pur; int lo, hi, stage;

  // --- an exact whole multiple -------------------------------------------
  // 2x. Nothing to interpolate, so nearest must reproduce the source exactly
  // and every block must be exactly 2 * BLOCK wide.
  if (measure(SCALE_SHARP, SRC_W * 2, SRC_LIVE_H * 2, &pur, &lo, &hi, &stage)) {
    if (stage != 0) fail("sharp built an intermediate at an exact 2x");
    if (pur < 0.9999)
      fail("sharp at exact 2x is only %.1f%% pure — it should be untouched",
           100.0 * pur);
    if (lo != BLOCK * 2 || hi != BLOCK * 2)
      fail("sharp at exact 2x has blocks %d..%d wide, want %d", lo, hi, BLOCK * 2);
    printf("  exact 2x, sharp     purity %6.2f%%  blocks %d..%d\n",
           100.0 * pur, lo, hi);
  }

  // --- a fractional size, all three modes ---------------------------------
  // 1000x1000 fits 1000x937 of picture: a factor of 1.953, the case that makes
  // plain nearest shimmer.
  const int OW = 1000, OH = 1000;
  double pur_sharp = 0, pur_linear = 0, pur_int = 0;
  int lo_sharp = 0, hi_sharp = 0;

  if (measure(SCALE_SHARP, OW, OH, &pur_sharp, &lo_sharp, &hi_sharp, &stage)) {
    if (stage != 2) fail("sharp did not step up to 2x at %dx%d (got %d)", OW, OH, stage);
    printf("  %dx%d, sharp      purity %6.2f%%  blocks %d..%d\n",
           OW, OH, 100.0 * pur_sharp, lo_sharp, hi_sharp);
    // ...and there must *be* blocks to have measured, or the check below is
    // agreeing with itself. This is the assertion that the first draft was
    // missing, and it is the one that would have caught the seam row.
    if (hi_sharp < 2)
      fail("no measurable blocks in the purest row — the uniformity check"
           " below has nothing to check");
    // The point of the mode: one block width, not two.
    if (hi_sharp - lo_sharp > 1)
      fail("sharp left blocks of uneven width (%d..%d) — that is the shimmer it"
           " exists to remove", lo_sharp, hi_sharp);
    // And it must really have blended the seams, or it is nearest wearing a
    // different name.
    if (pur_sharp > 0.999)
      fail("sharp is 100%% pure at a fractional size — the bilinear step did"
           " not happen");
    if (pur_sharp < 0.4)
      fail("sharp is only %.1f%% pure — the blocks are not surviving",
           100.0 * pur_sharp);
  }

  if (measure(SCALE_LINEAR, OW, OH, &pur_linear, &lo, &hi, &stage)) {
    if (stage != 0) fail("linear built an intermediate");
    printf("  %dx%d, linear     purity %6.2f%%  blocks %d..%d\n",
           OW, OH, 100.0 * pur_linear, lo, hi);
  }

  if (measure(SCALE_INTEGER, OW, OH, &pur_int, &lo, &hi, &stage)) {
    if (stage != 0) fail("integer built an intermediate");
    if (pur_int < 0.9999)
      fail("integer is only %.1f%% pure — it should never filter",
           100.0 * pur_int);
    if (lo != BLOCK || hi != BLOCK)
      fail("integer at 1x has blocks %d..%d wide, want %d", lo, hi, BLOCK);
    printf("  %dx%d, integer    purity %6.2f%%  blocks %d..%d\n",
           OW, OH, 100.0 * pur_int, lo, hi);
  }

  // The comparison that catches the filters being swapped: if `sharp` fed the
  // intermediate with bilinear, or shrank from it with nearest, it would look
  // like one of the other two rather than sitting between them.
  if (pur_sharp <= pur_linear)
    fail("sharp (%.1f%%) is no purer than linear (%.1f%%) — check which filter"
         " goes on which step", 100.0 * pur_sharp, 100.0 * pur_linear);

  // ...and the other end of the same argument. `sharp` on a renderer that
  // cannot hold an intermediate degrades to exactly plain nearest, which is
  // 100% pure and is *not* what the mode is for. If these two agree, the
  // intermediate is doing nothing and the whole mode is decorative.
  double pur_fallback = 0;
  if (measure_ex(SCALE_SHARP, OW, OH, false, &pur_fallback, &lo, &hi, &stage)) {
    printf("  %dx%d, no target  purity %6.2f%%  blocks %d..%d  (plain nearest)\n",
           OW, OH, 100.0 * pur_fallback, lo, hi);
    if (stage != 0)
      fail("built an intermediate with no render-target support");
    if (pur_fallback < 0.9999)
      fail("the fallback filtered something — it is meant to be plain nearest");
    if (pur_fallback - pur_sharp < 0.1)
      fail("sharp (%.1f%%) is indistinguishable from plain nearest (%.1f%%) —"
           " the intermediate is not being used",
           100.0 * pur_sharp, 100.0 * pur_fallback);
  }

  // The quit fade.
  {
    double m0 = 0, m_half = 0, m_full = 0;
    if (measure_dim(0, &m0) && measure_dim(128, &m_half) &&
        measure_dim(255, &m_full)) {
      printf("  quit fade           mean brightness %.1f -> %.1f -> %.1f"
             " (dim 0, 128, 255)\n", m0, m_half, m_full);
      if (m0 <= 1.0)
        fail("the undimmed pattern is already black (%.1f) — nothing to fade",
             m0);
      if (m_full > 0.5)
        fail("a full dim left the picture at %.1f, want black", m_full);
      // The point of the whole check: half must be *half*, not black. A blend
      // mode left unset makes this equal to the line above and nothing else in
      // this file would notice.
      if (m_half < 0.25 * m0 || m_half > 0.75 * m0)
        fail("a half dim gave %.1f from %.1f, want about half — the fade is a"
             " cut, not a fade", m_half, m0);
    }
  }

  SDL_Quit();
  if (failures) {
    printf("\n%d failure%s.\n", failures, failures == 1 ? "" : "s");
    return 1;
  }
  printf("present_draw: all checks passed.\n");
  return 0;
}
