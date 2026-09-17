// What `src/smooth.h` promises, checked without a PPU or a display.
//
// Each of these is a property a wrong version got wrong while it was written,
// or would have:
//
//   * a step never moves backwards, and the last step always arrives -- a
//     sprite shown at 25%, 50%, 75% and then *not quite* at its real place
//     would jitter once a tick, forever;
//   * a move further than a tick can carry a thing is a cut, and a cut is not
//     eased -- a spawn interpolated is a sprite sliding across the screen;
//   * the rings wrap: nine bits for x, eight for y, ten for a scroll, so a
//     sprite leaving at the right edge and a camera crossing 1023 both take
//     the short way round;
//   * a sprite is matched by what it looks like, so a slot shift -- two actors
//     crossing in depth -- does not ease each one toward the other's old place;
//   * the same slot is still trusted when the tile changed and the position
//     did not move much, because that is what an animation frame is;
//   * the display lock knows 240 Hz from 144.
//
// It is arithmetic with no SDL in it:
//
//     cc -I src -o test_smooth tools/test_smooth.c && ./test_smooth
//
// The build wires it up as the `zamn_test_smooth` target.

#include <stdio.h>
#include <stdlib.h>

#include "pace.h"
#include "smooth.h"

static int failures;

static void check(bool ok, const char* what, int a, int b) {
  if (ok) return;
  printf("FAIL %s  (%d vs %d)\n", what, a, b);
  failures++;
}

// --- smooth_wrap / smooth_step ---------------------------------------------

static void test_wrap(void) {
  check(smooth_wrap(255, 8) == -1, "255 in eight bits is -1", smooth_wrap(255, 8), -1);
  check(smooth_wrap(-255, 8) == 1, "-255 in eight bits is 1", smooth_wrap(-255, 8), 1);
  check(smooth_wrap(511, 9) == -1, "511 in nine bits is -1", smooth_wrap(511, 9), -1);
  check(smooth_wrap(4 - 1020, 10) == 8, "1020 to 4 is +8 in ten bits", smooth_wrap(4 - 1020, 10), 8);
  check(smooth_wrap(-256, 9) == -256, "-256 stays -256 in nine bits", smooth_wrap(-256, 9), -256);
  check(smooth_wrap(256, 9) == -256, "256 is -256 in nine bits", smooth_wrap(256, 9), -256);
}

static void test_step_arrives_monotonically(void) {
  // Every start, every move a tick can make, every number of steps a display
  // is likely to want: the sequence never goes backwards and ends at `to`.
  for (int bits = 8; bits <= 10; bits++) {
    for (int from = 0; from < (1 << bits); from += 7) {
      for (int d = -SMOOTH_SNAP; d <= SMOOTH_SNAP; d++) {
        const int to = (from + d) & ((1 << bits) - 1);
        for (int den = 1; den <= 8; den++) {
          int last = from;
          for (int num = 1; num <= den; num++) {
            const int v = smooth_step(from, to, bits, num, den);
            // Progress along the short way round, as a signed offset from `from`.
            const int prog = smooth_wrap(v - from, bits);
            const int lastProg = smooth_wrap(last - from, bits);
            const bool forward = d >= 0 ? prog >= lastProg : prog <= lastProg;
            const bool within = d >= 0 ? prog <= d : prog >= d;
            if (!forward || !within) {
              check(false, "step went backwards or overshot", v, to);
              return;
            }
            // Within half a unit of the exact fraction.
            const int twice = 2 * prog * den - 2 * d * num;
            if (twice > den || twice < -den) {
              check(false, "step is more than half a unit off", v, to);
              return;
            }
            last = v;
          }
          const int end = smooth_step(from, to, bits, den, den);
          if (((end - to) & ((1 << bits) - 1)) != 0) {
            check(false, "last step did not arrive", end, to);
            return;
          }
        }
      }
    }
  }
}

static void test_step_rounding(void) {
  // A two-pixel move in four: 0.5 rounds away from zero, so 1, 1, 2, 2.
  check(smooth_step(10, 12, 8, 1, 4) == 11, "2px at 1/4 is 1", smooth_step(10, 12, 8, 1, 4), 11);
  check(smooth_step(10, 12, 8, 2, 4) == 11, "2px at 2/4 is 1", smooth_step(10, 12, 8, 2, 4), 11);
  check(smooth_step(10, 12, 8, 3, 4) == 12, "2px at 3/4 is 2", smooth_step(10, 12, 8, 3, 4), 12);
  check(smooth_step(10, 12, 8, 4, 4) == 12, "2px at 4/4 is 2", smooth_step(10, 12, 8, 4, 4), 12);
  // ...and the same going left, mirrored exactly.
  check(smooth_step(12, 10, 8, 1, 4) == 11, "-2px at 1/4 is -1", smooth_step(12, 10, 8, 1, 4), 11);
  check(smooth_step(12, 10, 8, 3, 4) == 10, "-2px at 3/4 is -2", smooth_step(12, 10, 8, 3, 4), 10);
  // A one-pixel move in four lands on the second half.
  check(smooth_step(5, 6, 8, 1, 4) == 5, "1px at 1/4 is 0", smooth_step(5, 6, 8, 1, 4), 5);
  check(smooth_step(5, 6, 8, 2, 4) == 6, "1px at 2/4 is 1", smooth_step(5, 6, 8, 2, 4), 6);
  // No move is no move.
  check(smooth_step(7, 7, 8, 1, 4) == 7, "no move stays", smooth_step(7, 7, 8, 1, 4), 7);
}

static void test_step_snaps(void) {
  const int far = SMOOTH_SNAP + 1;
  for (int num = 1; num <= 4; num++) {
    check(smooth_step(0, far, 8, num, 4) == far, "a cut is not eased", smooth_step(0, far, 8, num, 4), far);
    check(smooth_step(far, 0, 8, num, 4) == 0, "a cut back is not eased", smooth_step(far, 0, 8, num, 4), 0);
  }
  // Exactly the threshold is still a move.
  check(smooth_step(0, SMOOTH_SNAP, 8, 2, 4) == SMOOTH_SNAP / 2, "the threshold itself eases", smooth_step(0, SMOOTH_SNAP, 8, 2, 4), SMOOTH_SNAP / 2);
  // Parked off the bottom to on screen is a cut by a screen's height.
  check(smooth_step(0xe8, 100, 8, 2, 4) == 100, "parked to live is a cut", smooth_step(0xe8, 100, 8, 2, 4), 100);
}

static void test_step_wraps(void) {
  // A scroll crossing 1023: 1022 to 2 is +4, half of which is 1024, which is 0.
  check((smooth_step(1022, 2, 10, 2, 4) & 0x3ff) == 0, "scroll wraps through 1023", smooth_step(1022, 2, 10, 2, 4) & 0x3ff, 0);
  check((smooth_step(1022, 2, 10, 1, 4) & 0x3ff) == 1023, "scroll at 1/4 through 1023", smooth_step(1022, 2, 10, 1, 4) & 0x3ff, 1023);
  // A sprite leaving at the right: 254 to -254 (258 in nine bits) is +4.
  check(smooth_step(254, -254, 9, 2, 4) == 256, "sprite x wraps at 255", smooth_step(254, -254, 9, 2, 4), 256);
  // A sprite whose top is above the screen: y 254 (-2) to 2 is +4.
  check((smooth_step(254, 2, 8, 2, 4) & 0xff) == 0, "sprite y wraps at 255", smooth_step(254, 2, 8, 2, 4) & 0xff, 0);
}

// --- smooth_oam --------------------------------------------------------------

typedef struct {
  uint16_t oam[SMOOTH_SPRITES * 2];
  uint8_t high[SMOOTH_SPRITES / 4];
} Oam;

static void oam_clear(Oam* o) {
  // Everything parked at y=$E8, the way `ppu_freeSprite` expects.
  for (int i = 0; i < SMOOTH_SPRITES; i++) {
    o->oam[i * 2] = 0xe800;
    o->oam[i * 2 + 1] = 0;
  }
  memset(o->high, 0, sizeof o->high);
}

static void oam_put(Oam* o, int i, int x, int y, uint16_t attr, bool large) {
  o->oam[i * 2] = (uint16_t)((x & 0xff) | ((y & 0xff) << 8));
  o->oam[i * 2 + 1] = attr;
  o->high[i >> 2] &= (uint8_t)~(3 << ((i & 3) * 2));
  o->high[i >> 2] |= (uint8_t)((((x >> 8) & 1) | (large ? 2 : 0)) << ((i & 3) * 2));
}

static void expect_sprite(const Oam* o, int i, int x, int y, uint16_t attr,
                          bool large, const char* what) {
  const SmoothSprite s = smooth_sprite(o->oam, o->high, i);
  check(s.x == x, what, s.x, x);
  check(s.y == y, what, s.y, y);
  check(s.attr == attr, what, s.attr, attr);
  check(s.large == large, what, s.large, large);
}

static void test_oam_roundtrip(void) {
  Oam o;
  oam_clear(&o);
  oam_put(&o, 5, -200, 30, 0x1234, true);
  oam_put(&o, 6, 300 - 512, 250, 0x4321, false);
  expect_sprite(&o, 5, -200, 30, 0x1234, true, "nine-bit x with the high bit set");
  expect_sprite(&o, 6, -212, 250, 0x4321, false, "nine-bit x past 255 reads negative");
  // Through smooth_oam at the end of a tick, nothing changes.
  Oam out;
  smooth_oam(o.oam, o.high, o.oam, o.high, out.oam, out.high, 4, 4);
  check(memcmp(&out, &o, sizeof o) == 0, "t=1 from itself is the identity", 0, 0);
}

static void test_oam_moves(void) {
  Oam prev, cur, out;
  oam_clear(&prev);
  oam_clear(&cur);
  // A walker moving right two pixels and down one, tile unchanged.
  oam_put(&prev, 0, 100, 50, 0x0a00, true);
  oam_put(&cur, 0, 102, 51, 0x0a00, true);
  smooth_oam(prev.oam, prev.high, cur.oam, cur.high, out.oam, out.high, 2, 4);
  expect_sprite(&out, 0, 101, 51, 0x0a00, true, "walker at half a tick");
  // Everything parked stays parked, untouched.
  expect_sprite(&out, 1, 0, 0xe8, 0, false, "a parked sprite is left alone");
  // In place, so that the output can be the current table itself.
  smooth_oam(prev.oam, prev.high, cur.oam, cur.high, cur.oam, cur.high, 2, 4);
  expect_sprite(&cur, 0, 101, 51, 0x0a00, true, "walker at half a tick, in place");
}

static void test_oam_slot_shift(void) {
  // Two actors that swapped slots because they crossed in depth. Each must be
  // eased from its *own* old place, not from the other's.
  Oam prev, cur, out;
  oam_clear(&prev);
  oam_clear(&cur);
  oam_put(&prev, 0, 100, 100, 0x1111, false);
  oam_put(&prev, 1, 50, 50, 0x2222, false);
  oam_put(&cur, 0, 52, 50, 0x2222, false);
  oam_put(&cur, 1, 102, 100, 0x1111, false);
  smooth_oam(prev.oam, prev.high, cur.oam, cur.high, out.oam, out.high, 2, 4);
  expect_sprite(&out, 0, 51, 50, 0x2222, false, "shifted sprite eased from itself");
  expect_sprite(&out, 1, 101, 100, 0x1111, false, "other shifted sprite eased from itself");
}

static void test_oam_same_tile_twice(void) {
  // A row of the same tile: each is matched to the nearest, which is the one
  // that was in its place -- and since they look alike, either would do.
  Oam prev, cur, out;
  oam_clear(&prev);
  oam_clear(&cur);
  for (int i = 0; i < 4; i++) oam_put(&prev, i, 40 + i * 8, 60, 0x0777, false);
  for (int i = 0; i < 4; i++) oam_put(&cur, i, 42 + i * 8, 60, 0x0777, false);
  smooth_oam(prev.oam, prev.high, cur.oam, cur.high, out.oam, out.high, 2, 4);
  for (int i = 0; i < 4; i++)
    expect_sprite(&out, i, 41 + i * 8, 60, 0x0777, false, "a row of one tile eases as a row");
}

static void test_oam_animation(void) {
  // Same slot, different tile, barely moved: an animation frame, and it eases.
  Oam prev, cur, out;
  oam_clear(&prev);
  oam_put(&prev, 3, 80, 80, 0x0100, true);
  oam_clear(&cur);
  oam_put(&cur, 3, 82, 80, 0x0101, true);
  smooth_oam(prev.oam, prev.high, cur.oam, cur.high, out.oam, out.high, 2, 4);
  expect_sprite(&out, 3, 81, 80, 0x0101, true, "animation frame eases in its slot");
  // Same slot, different tile, and far away: something else is in that slot
  // now, and it is drawn where it is.
  oam_clear(&cur);
  oam_put(&cur, 3, 200, 20, 0x0101, true);
  smooth_oam(prev.oam, prev.high, cur.oam, cur.high, out.oam, out.high, 2, 4);
  expect_sprite(&out, 3, 200, 20, 0x0101, true, "a different sprite in the slot is not eased");
}

static void test_oam_spawn(void) {
  // Parked last tick, on screen this tick: drawn where it is, every step.
  Oam prev, cur, out;
  oam_clear(&prev);
  oam_clear(&cur);
  oam_put(&cur, 7, 120, 90, 0x0303, false);
  for (int num = 1; num <= 4; num++) {
    smooth_oam(prev.oam, prev.high, cur.oam, cur.high, out.oam, out.high, num, 4);
    expect_sprite(&out, 7, 120, 90, 0x0303, false, "a spawned sprite is not eased");
  }
}

static void test_oam_edge_wrap(void) {
  // Leaving at the right edge: 254 to -254 is a four-pixel move to the right.
  Oam prev, cur, out;
  oam_clear(&prev);
  oam_clear(&cur);
  oam_put(&prev, 2, 254, 100, 0x0505, true);
  oam_put(&cur, 2, -254, 100, 0x0505, true);
  smooth_oam(prev.oam, prev.high, cur.oam, cur.high, out.oam, out.high, 2, 4);
  expect_sprite(&out, 2, -256, 100, 0x0505, true, "a sprite leaving right wraps to -256");
}

// --- smooth_lines --------------------------------------------------------------

static void test_lines(void) {
  uint16_t prev[240], cur[240], out[240];
  // A plain scroll: one value on every line, eased as one value.
  for (int i = 0; i < 240; i++) { prev[i] = 300; cur[i] = 301; }
  smooth_lines(prev, cur, out, 240, 1, 4);
  for (int i = 0; i < 240; i++) check(out[i] == 300, "1px scroll at 1/4", out[i], 300);
  smooth_lines(prev, cur, out, 240, 2, 4);
  for (int i = 0; i < 240; i++) check(out[i] == 301, "1px scroll at 2/4", out[i], 301);
  // A raster sweep whose every line moved by two, eased line by line.
  for (int i = 0; i < 240; i++) { prev[i] = (uint16_t)i; cur[i] = (uint16_t)(i + 2); }
  smooth_lines(prev, cur, out, 240, 2, 4);
  for (int i = 0; i < 240; i++) check(out[i] == i + 1, "sweep eased per line", out[i], i + 1);
  // A new screen: every line a long way from where it was, and nothing eased.
  for (int i = 0; i < 240; i++) { prev[i] = 0; cur[i] = 512; }
  smooth_lines(prev, cur, out, 240, 1, 4);
  for (int i = 0; i < 240; i++) check(out[i] == 512, "a new screen is not eased", out[i], 512);
  // In place.
  for (int i = 0; i < 240; i++) { prev[i] = 1022; cur[i] = 2; }
  smooth_lines(prev, cur, cur, 240, 2, 4);
  for (int i = 0; i < 240; i++) check(cur[i] == 0, "scroll wraps in place", cur[i], 0);
}

// --- pace_lock_k --------------------------------------------------------------

static void test_lock(void) {
  check(pace_lock_k(240, PACE_FPS_NTSC) == 4, "240 Hz is four per frame", pace_lock_k(240, PACE_FPS_NTSC), 4);
  check(pace_lock_k(120, PACE_FPS_NTSC) == 2, "120 Hz is two per frame", pace_lock_k(120, PACE_FPS_NTSC), 2);
  check(pace_lock_k(60, PACE_FPS_NTSC) == 1, "60 Hz is one per frame", pace_lock_k(60, PACE_FPS_NTSC), 1);
  check(pace_lock_k(144, PACE_FPS_NTSC) == 0, "144 Hz does not lock", pace_lock_k(144, PACE_FPS_NTSC), 0);
  check(pace_lock_k(50, PACE_FPS_NTSC) == 0, "50 Hz does not lock NTSC", pace_lock_k(50, PACE_FPS_NTSC), 0);
  check(pace_lock_k(100, PACE_FPS_PAL) == 2, "100 Hz is two per PAL frame", pace_lock_k(100, PACE_FPS_PAL), 2);
  check(pace_lock_k(0, PACE_FPS_NTSC) == 0, "no display does not lock", pace_lock_k(0, PACE_FPS_NTSC), 0);
  // ...and the period still agrees with what it locks to.
  const double p240 = pace_period_ms(240, PACE_FPS_NTSC);
  check(p240 > 16.66 && p240 < 16.67, "240 Hz period is 4/240", (int)(p240 * 1000), 16667);
  const double p144 = pace_period_ms(144, PACE_FPS_NTSC);
  check(p144 > 16.63 && p144 < 16.64, "144 Hz falls back to the console", (int)(p144 * 1000), 16639);
}

int main(void) {
  test_wrap();
  test_step_arrives_monotonically();
  test_step_rounding();
  test_step_snaps();
  test_step_wraps();
  test_oam_roundtrip();
  test_oam_moves();
  test_oam_slot_shift();
  test_oam_same_tile_twice();
  test_oam_animation();
  test_oam_spawn();
  test_oam_edge_wrap();
  test_lines();
  test_lock();
  if (failures) {
    printf("%d failure%s\n", failures, failures == 1 ? "" : "s");
    return 1;
  }
  printf("smooth: all checks passed\n");
  return 0;
}
