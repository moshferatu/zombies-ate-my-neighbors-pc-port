// Pictures between the game's frames -- the first attempt, and what is kept.
//
// **Superseded by `src/layers.h`**, which draws the pictures as layers on a
// target several times the console's size, so that they can move by a fraction
// of a pixel. This file's `smooth_oam`/`smooth_lines` move the PPU's own
// sprites and scroll in whole pixels, and a whole pixel turned out to be the
// problem: see the header of `layers.h`. What is still used is the ring
// arithmetic (`smooth_wrap`) and `smooth_match`, which `layers_link` falls
// back on to pair a sprite with its predecessor when the port's sprite pass
// did not run and there is no record to pair it by. The account below is
// left as written, because the reasons it gives for interpolating rather than
// guessing still hold.
//
// The game moves at 60.0988 ticks a second and nothing in it can move faster:
// a thread sleeps for a whole number of ticks, a walker covers one pixel a tick
// and takes a second step on the ticks a mask picks out, the camera drifts a
// pixel a tick after the players. A display refreshing at 240 Hz shows each of
// those pictures four times, and the three repeats between one tick and the
// next are refreshes the console never had anything to put on. This puts
// something on them.
//
// ## What is interpolated, and what cannot be
//
// A frame of this game is a function of the PPU's state at the end of it: the
// game writes VRAM, OAM and the scroll registers in vblank and nowhere else,
// the PPU records the one thing it does draw a line at a time (see
// `Ppu.lineHScroll`), and `ppu_renderFrame` can draw the frame again from that.
// So a picture *between* two frames is the second frame drawn with its scroll
// and its sprites moved part of the way back toward where they were in the
// first. Everything else -- which tiles, which palette, which animation frame,
// what the text says -- is the second frame's. Position is the only thing a
// tick moves by a small amount, so position is the only thing worth easing.
//
// The picture is the one the game made a tick ago plus a fraction of a tick,
// which is to say it is shown late by up to three refreshes at 240 Hz. That is
// the price of interpolating rather than guessing, and it is the reason this
// is a toggle.
//
// ## Matching a sprite to itself
//
// The OAM is rebuilt every tick by the sprite pass, which walks the actors in
// depth order, so a slot holds the same actor from one tick to the next only
// while nothing in front of it in that order changed. Two actors crossing in y,
// or one whose current animation frame takes a different number of sprites,
// shift every slot after them. So a sprite is matched to its predecessor by
// what it looks like rather than by where it sits: the previous frame's sprite
// with the same tile word, within a few slots and within `SMOOTH_SNAP` pixels.
// Failing that, the same slot is trusted if it is within `SMOOTH_SNAP` -- an
// animation that changed the tile of a sprite that stayed put -- and failing
// that the sprite is drawn where it is now. A sprite that is parked off screen
// one tick and on screen the next fails both tests by a screen's height, which
// is what makes the snap threshold a spawn detector as well as a cut detector.
//
// ## Wrapping
//
// Every coordinate here wraps: OAM x is nine bits, y eight, a scroll ten. A
// sprite walking off the right edge goes from 255 to -256 in the PPU's reading,
// and the difference between the two is +1, not -511. So differences are taken
// in the coordinate's own ring and a step is added to the old value in that
// ring; the caller masks the result back down to its width.
//
// It is a header of static inline functions with no SDL and no PPU in it: the
// caller passes the arrays in, from whatever machine state it holds. That keeps
// it testable (`tools/test_smooth.c`) and keeps the frontend's threading out of
// the arithmetic.

#ifndef ZAMN_SMOOTH_H
#define ZAMN_SMOOTH_H

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

// The most a thing may move in one tick and still be the same thing moving.
// The fastest walker in the game covers two pixels a tick and the camera one,
// so a sprite crosses the screen at three; a shot is faster and still well
// inside this. Anything further is a cut -- a spawn, a teleport, a new screen
// -- and a cut interpolated is a sprite sliding across the screen to where it
// was put.
#define SMOOTH_SNAP 16
// How far either side of a sprite's own slot its predecessor is looked for.
// A crossing shifts the slots after it by one actor's worth of sprites, which
// is a handful; a whole screen of eight-sprite actors shifting is not a case
// worth the compare cost of covering.
#define SMOOTH_MATCH_WINDOW 8
// The PPU's OAM: 128 sprites of two words, and 32 bytes of high bits.
#define SMOOTH_SPRITES 128

// `d` taken into the signed ring of `bits` bits: 255 in eight bits is -1.
static inline int smooth_wrap(int d, int bits) {
  const int full = 1 << bits;
  const int half = full >> 1;
  d &= full - 1;
  return d >= half ? d - full : d;
}

// `from` moved `num/den` of the way to `to`, in the ring of `bits` bits, or
// `to` itself if the two are further apart than a tick can move a thing. The
// step is rounded to nearest, half away from zero, so a two-pixel move shown
// in four steps goes 1, 1, 2, 2 -- never backwards, and always arriving.
// Not masked: a caller packing nine bits into a byte and a bit wants the
// signed value, and one storing ten bits wants `& 0x3ff`.
static inline int smooth_step(int from, int to, int bits, int num, int den) {
  const int d = smooth_wrap(to - from, bits);
  if (d > SMOOTH_SNAP || d < -SMOOTH_SNAP) return to;
  if (den <= 0 || num >= den) return to;
  if (num <= 0) return from;
  const int scaled = d * num;
  const int step = scaled >= 0 ? (scaled + den / 2) / den
                               : -((-scaled + den / 2) / den);
  return from + step;
}

// One OAM entry, unpacked: `x` signed nine-bit, `y` eight-bit as stored,
// `attr` the second word, `large` the size bit from the high table.
typedef struct {
  int x, y;
  uint16_t attr;
  bool large;
} SmoothSprite;

static inline SmoothSprite smooth_sprite(const uint16_t* oam,
                                         const uint8_t* high, int i) {
  SmoothSprite s;
  const int hi = high[i >> 2] >> ((i & 3) * 2);
  s.x = (oam[i * 2] & 0xff) | ((hi & 1) << 8);
  if (s.x >= 256) s.x -= 512;
  s.y = oam[i * 2] >> 8;
  s.attr = oam[i * 2 + 1];
  s.large = (hi >> 1) & 1;
  return s;
}

static inline int smooth_abs(int v) { return v < 0 ? -v : v; }

// Is `p` within a tick's move of `c`, in both rings?
static inline bool smooth_near(const SmoothSprite* p, const SmoothSprite* c) {
  return smooth_abs(smooth_wrap(c->x - p->x, 9)) <= SMOOTH_SNAP &&
         smooth_abs(smooth_wrap(c->y - p->y, 8)) <= SMOOTH_SNAP;
}

// The previous frame's slot that held sprite `i` of the current frame, by the
// rule in the header comment, or -1 if nothing did.
static inline int smooth_match(const uint16_t* prevOam, const uint8_t* prevHigh,
                               const SmoothSprite* c, int i) {
  int best = -1, bestCost = 0;
  int lo = i - SMOOTH_MATCH_WINDOW, hi = i + SMOOTH_MATCH_WINDOW;
  if (lo < 0) lo = 0;
  if (hi > SMOOTH_SPRITES - 1) hi = SMOOTH_SPRITES - 1;
  for (int j = lo; j <= hi; j++) {
    const SmoothSprite p = smooth_sprite(prevOam, prevHigh, j);
    if (p.attr != c->attr || p.large != c->large) continue;
    if (!smooth_near(&p, c)) continue;
    // Nearest wins; between two equally near, the nearer slot. The slot term
    // is scaled so that it can only ever break a tie in distance.
    const int cost = (smooth_abs(smooth_wrap(c->x - p.x, 9)) +
                      smooth_abs(smooth_wrap(c->y - p.y, 8))) * 32 +
                     smooth_abs(j - i);
    if (best < 0 || cost < bestCost) {
      best = j;
      bestCost = cost;
    }
  }
  if (best >= 0) return best;
  const SmoothSprite same = smooth_sprite(prevOam, prevHigh, i);
  return smooth_near(&same, c) ? i : -1;
}

// The current frame's OAM with every sprite moved `num/den` of the way from
// where it was a tick ago. `outOam` may alias `curOam` and `outHigh` may alias
// `curHigh`; neither may alias the previous frame's. The tile words and size
// bits are the current frame's throughout.
static inline void smooth_oam(const uint16_t* prevOam, const uint8_t* prevHigh,
                              const uint16_t* curOam, const uint8_t* curHigh,
                              uint16_t* outOam, uint8_t* outHigh, int num,
                              int den) {
  uint8_t high[SMOOTH_SPRITES / 4];
  memset(high, 0, sizeof high);
  for (int i = 0; i < SMOOTH_SPRITES; i++) {
    const SmoothSprite c = smooth_sprite(curOam, curHigh, i);
    int x = c.x, y = c.y;
    const int j = smooth_match(prevOam, prevHigh, &c, i);
    if (j >= 0) {
      const SmoothSprite p = smooth_sprite(prevOam, prevHigh, j);
      x = smooth_step(p.x, c.x, 9, num, den);
      y = smooth_step(p.y, c.y, 8, num, den);
    }
    outOam[i * 2] = (uint16_t)((x & 0xff) | ((y & 0xff) << 8));
    outOam[i * 2 + 1] = c.attr;
    high[i >> 2] |= (uint8_t)((((x >> 8) & 1) | (c.large ? 2 : 0))
                              << ((i & 3) * 2));
  }
  memcpy(outHigh, high, sizeof high);
}

// A table of ten-bit scroll values -- one background's, one per line -- moved
// `num/den` of the way from a tick ago, line by line. For a layer the game
// scrolls this is one value repeated; for a raster sweep it is the sweep,
// eased. `out` may alias `cur`.
static inline void smooth_lines(const uint16_t* prev, const uint16_t* cur,
                                uint16_t* out, int n, int num, int den) {
  for (int i = 0; i < n; i++)
    out[i] = (uint16_t)(smooth_step(prev[i], cur[i], 10, num, den) & 0x3ff);
}

#endif
