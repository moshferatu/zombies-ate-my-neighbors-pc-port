// See `video.h`.

#include "video/video.h"

#include <string.h>

// The last line of a frame without overscan, which is the only kind drawn.
#define LAST_LINE 224

// --- A row of a tile ----------------------------------------------------------
//
// A character is stored as bitplanes: for each of its eight rows, a word
// whose low byte is bit 0 of the row's eight pixels and whose high byte is
// bit 1, and for a sixteen-colour character a second word, eight words on,
// for bits 2 and 3. The leftmost pixel is the top bit of each byte.
//
// `SPREAD` turns one such byte into eight bytes, one per pixel, leftmost
// first, each 0 or 1; the second table is the same for a tile flipped left to
// right. Four of them shifted and ORed are a row's eight pixels at once.
static uint64_t SPREAD[2][256];
static bool spread_made;

static void make_spread(void) {
  for (int b = 0; b < 256; b++) {
    uint8_t plain[8], flipped[8];
    for (int i = 0; i < 8; i++) {
      plain[i] = (uint8_t)((b >> (7 - i)) & 1);
      flipped[i] = (uint8_t)((b >> i) & 1);
    }
    memcpy(&SPREAD[0][b], plain, 8);
    memcpy(&SPREAD[1][b], flipped, 8);
  }
  spread_made = true;
}

// Mode 1: two sixteen-colour backgrounds and a four-colour one.
static int bg_depth(int layer) { return layer == VIDEO_BG3 ? 2 : 4; }

// The eight pixels of the tile row that layer pixel (px, py) is in, leftmost
// first, as palette indices with 0 for transparent; and whether the tile is
// one of those drawn in front. `px` and `py` are ten bits: a background is
// 1024 pixels each way before it comes round.
static uint64_t tile_row(const VideoState* s, int layer, int px, int py, bool* front) {
  const VideoBg* bg = &s->bg[layer];
  const int depth = bg_depth(layer);
  // The map is 32 tiles each way, or two or four such side by side.
  const int tile_bits = bg->big_tiles ? 4 : 3;
  const int next_map = bg->big_tiles ? 0x200 : 0x100;
  int at = bg->map_at + ((((py >> tile_bits) & 0x1f) << 5) | ((px >> tile_bits) & 0x1f));
  if ((px & next_map) && bg->map_wide) at += 0x400;
  if ((py & next_map) && bg->map_high) at += bg->map_wide ? 0x800 : 0x400;
  const uint16_t tile = s->vram[at & 0x7fff];
  const bool flip_x = (tile & 0x4000) != 0, flip_y = (tile & 0x8000) != 0;
  *front = (tile & 0x2000) != 0;
  int character = tile & 0x3ff;
  // A 16x16 tile is four characters: the one named, the next, and the two a
  // row of sixteen below.
  if (bg->big_tiles) {
    if (((px & 8) != 0) != flip_x) character += 1;
    if (((py & 8) != 0) != flip_y) character += 0x10;
  }
  const int row = flip_y ? 7 - (py & 7) : (py & 7);
  const int words = bg->tiles_at + (character & 0x3ff) * 4 * depth + row;
  const uint64_t* spread = SPREAD[flip_x];
  const uint16_t low = s->vram[words & 0x7fff];
  uint64_t pixels = spread[low & 0xff] | (spread[low >> 8] << 1);
  if (depth == 4) {
    const uint16_t high = s->vram[(words + 8) & 0x7fff];
    pixels |= (spread[high & 0xff] << 2) | (spread[high >> 8] << 3);
  }
  // The tile's palette, added to each pixel that is not transparent: a byte
  // that is not zero carries into its top bit when 0x7f is added.
  const uint64_t palette = (uint64_t)(((tile >> 10) & 7) * (depth == 4 ? 16 : 4));
  const uint64_t solid = ((pixels + 0x7f7f7f7f7f7f7f7full) >> 7) & 0x0101010101010101ull;
  return pixels + solid * palette;
}

static bool bg_opaque(const VideoState* s, int layer, int px, int py) {
  bool front;
  uint8_t pixels[8];
  const uint64_t row = tile_row(s, layer, px & 0x3ff, py & 0x3ff, &front);
  memcpy(pixels, &row, 8);
  return pixels[px & 7] != 0;
}

// --- The widened picture ------------------------------------------------------
//
// A policy maps a column of the picture to a column of the layer, or to
// nothing. Each is a handful of runs: columns `x0` to `x1` of the picture
// showing the layer's columns `x0 + shift` on, or all showing one pixel of
// the layer, the column `from_x` on the line `from_line`.

typedef struct {
  int x0, x1;
  bool fixed;
  int shift;
  int from_x, from_line;
} Run;

// The most a policy comes to: `VIDEO_WIDE_CENTRE` with both margins wrapped.
#define MAX_RUNS 8

// The columns that are being asked for, `from` up to `to`, and the runs found
// in them so far. A line of the picture asks for the picture's columns; the
// smoothing asks for a few more either side.
typedef struct {
  int from, to;
  Run run[MAX_RUNS];
  int count;
} Runs;

// The run `a` to `b`, less whatever of it was not asked for.
static Run* run_add(Runs* runs, int a, int b, int shift) {
  if (a < runs->from) a = runs->from;
  if (b > runs->to) b = runs->to;
  if (a >= b || runs->count == MAX_RUNS) return NULL;
  Run* run = &runs->run[runs->count++];
  *run = (Run){.x0 = a, .x1 = b, .shift = shift};
  return run;
}

static void run_add_fixed(Runs* runs, int a, int b, int from_x, int from_line) {
  Run* run = run_add(runs, a, b, 0);
  if (run == NULL) return;
  run->fixed = true;
  run->from_x = from_x;
  run->from_line = from_line;
}

// A line of the frame, for a line that may be a little above or below it: the
// rows the smoothing takes beyond the picture's edge are read with the scroll
// of the nearest line that was drawn.
static int drawn_line(int line) { return line < 1 ? 1 : line > LAST_LINE ? LAST_LINE : line; }

// What `VIDEO_WIDE_AUTO` comes to for a background. The reasons are with the
// emulated PPU's `ppu_wideAuto`; in short, a layer that is a picture composed
// for the console is clipped to it, and one that is a map goes on.
static VideoWide wide_policy(const VideoState* s, int layer) {
  const VideoWide set = (VideoWide)s->wide[layer];
  if (set != VIDEO_WIDE_AUTO) return set;
  if (layer == VIDEO_OBJ) return VIDEO_WIDE_STRETCH;
  if (s->raster[layer]) return VIDEO_WIDE_STRETCH;
  if (s->edge_empty[layer]) return VIDEO_WIDE_CLIP;
  if (s->bg[layer].map_wide) return VIDEO_WIDE_TILE;
  if (s->bg[layer].big_tiles || s->scrolled[layer]) return VIDEO_WIDE_STRETCH;
  return VIDEO_WIDE_CLIP;
}

// `VIDEO_WIDE_SWEEP`: how far right of its place the layer is drawn. The game
// scrolls the layer from 256 down to 0 to sweep it in across the console, and
// the shift runs from the left margin's width leftward to the right margin's
// rightward over the same sweep, so that it crosses the whole picture.
static int sweep_shift(const VideoState* s, int layer, int line) {
  const int hs = s->line_hscroll[layer][line] & 0x3ff;
  const int in = hs > 256 ? 0 : 256 - hs;
  return (in * (s->extra_left + s->extra_right) + 128) / 256 - s->extra_left;
}

// `VIDEO_WIDE_CENTRE`: the last line on which the layer is opaque from column
// 0 to 255, or -1. For the game over's mask that is the foot of its solid
// field, below which its drips hang. Found once for a scroll.
static void centre_last_full(const VideoState* s, VideoCentre* c, int layer, uint16_t hs,
                             uint16_t vs) {
  if (c->last_full_h[layer] == hs && c->last_full_v[layer] == vs && c->last_full[layer] != -2)
    return;
  int full = -1;
  for (int r = LAST_LINE; r >= 1 && full < 0; r--) {
    bool solid = true;
    for (int col = 0; col < 256 && solid; col++) solid = bg_opaque(s, layer, col + hs, r + vs);
    if (solid) full = r;
  }
  c->last_full[layer] = (int16_t)full;
  c->last_full_h[layer] = hs;
  c->last_full_v[layer] = vs;
}

// ...and what one margin of this line is filled with: nothing, the layer's
// own 256 columns again (beside the drips, so the curtain goes on), or one
// pixel of the layer's edge column carried out (beside the field). Found once
// for a line, a side and a scroll.
static void centre_fill(const VideoState* s, VideoCentre* c, int layer, int line, int side) {
  const int edge = side ? 255 : 0;
  const uint16_t hs = s->line_hscroll[layer][line], vs = s->line_vscroll[layer][line];
  centre_last_full(s, c, layer, hs, vs);
  if (c->fill_line[layer][side] == line && c->fill_h[layer][side] == hs &&
      c->fill_v[layer][side] == vs)
    return;
  // How far in from each edge the line is opaque, and where its first opaque
  // pixel from this side is.
  int run_left = 0, run_right = 0, found = -1, from = line;
  while (run_left < 256 && bg_opaque(s, layer, run_left + hs, line + vs)) run_left++;
  while (run_right < 256 - run_left && bg_opaque(s, layer, 255 - run_right + hs, line + vs))
    run_right++;
  if (side ? run_right > 0 : run_left > 0) {
    found = edge;
  } else {
    for (int col = edge, n = 0; n < 256; n++, col += side ? -1 : 1)
      if (bg_opaque(s, layer, col + hs, line + vs)) {
        found = col;
        break;
      }
  }
  // Beside the drips: below the field's foot, with a gap near both edges.
  const bool wrap = found >= 0 && line > c->last_full[layer] && run_left < 16 && run_right < 16;
  if (found >= 0 && !wrap) {
    // The edge column a few lines into the nearest run of opaque pixels at or
    // above this line, which is past a drip's dark outline.
    int run = 0;
    for (int r = line; r >= 1 && run < 4; r--) {
      if (bg_opaque(s, layer, edge + s->line_hscroll[layer][r], r + s->line_vscroll[layer][r])) {
        found = edge;
        from = r;
        run++;
      } else if (run) {
        break;
      }
    }
  }
  c->fill_line[layer][side] = (int16_t)line;
  c->fill_h[layer][side] = hs;
  c->fill_v[layer][side] = vs;
  c->fill_col[layer][side] = (int16_t)found;
  c->fill_from[layer][side] = (int16_t)from;
  c->fill_wrap[layer][side] = wrap;
}

// The 256 columns from `base`, repeated over `a` to `b`.
static void runs_repeat(Runs* runs, int a, int b, int base) {
  if (a < runs->from) a = runs->from;
  if (b > runs->to) b = runs->to;
  int at = base;
  while (at > a) at -= 256;
  for (; at < b; at += 256) run_add(runs, at > a ? at : a, at + 256 < b ? at + 256 : b, -at);
}

// The runs of background `layer` on `line`, over the columns `runs` asks for.
static void bg_runs(const VideoState* s, VideoCentre* centre, int layer, int line, Runs* runs) {
  const int left = runs->from, right = runs->to;
  runs->count = 0;
  if (s->extra_left == 0 && s->extra_right == 0) {
    run_add(runs, left, right, 0);
    return;
  }
  // What a policy reads of the frame's lines it reads of one that was drawn.
  const int drawn = drawn_line(line);
  switch (wide_policy(s, layer)) {
    case VIDEO_WIDE_CLIP:
      run_add(runs, 0, 256, 0);
      return;
    case VIDEO_WIDE_STRETCH:
      run_add(runs, s->clamp_lo, s->clamp_hi + 1, 0);
      return;
    case VIDEO_WIDE_ANCHOR:
      // The left half keeps its distance from the left edge and the right
      // half its distance from the right.
      run_add(runs, left, 128 - s->extra_left, s->extra_left);
      run_add(runs, 128 + s->extra_right, right, -s->extra_right);
      return;
    case VIDEO_WIDE_TILE:
      runs_repeat(runs, left, right, 0);
      return;
    case VIDEO_WIDE_CENTRE_CLIP: {
      const int mid = (s->extra_right - s->extra_left) / 2;
      run_add(runs, mid, mid + 256, -mid);
      return;
    }
    case VIDEO_WIDE_CENTRE: {
      const int mid = (s->extra_right - s->extra_left) / 2;
      run_add(runs, mid, mid + 256, -mid);
      for (int side = 0; side < 2; side++) {
        const int a = side ? mid + 256 : left, b = side ? right : mid;
        if (a >= b) continue;
        centre_fill(s, centre, layer, drawn, side);
        if (centre->fill_col[layer][side] < 0) continue;
        if (centre->fill_wrap[layer][side])
          runs_repeat(runs, a, b, mid);
        else
          run_add_fixed(runs, a, b, centre->fill_col[layer][side],
                        centre->fill_from[layer][side]);
      }
      return;
    }
    case VIDEO_WIDE_SWEEP: {
      const int shift = sweep_shift(s, layer, drawn);
      const int hs = s->line_hscroll[layer][drawn] & 0x3ff;
      // Left of the map's first column is more of the first column.
      if (hs <= 256) {
        run_add_fixed(runs, left, shift - hs, -hs, line);
        run_add(runs, shift - hs, right, -shift);
        return;
      }
      run_add(runs, left, right, -shift);
      return;
    }
    default:
      run_add(runs, left, right, 0);
      return;
  }
}

// --- A background's line ---------------------------------------------------------

// Where a background's two rows go: `back` for its tiles that go behind and
// `front` for those that go in front, each a palette index a column with 0
// for nothing, the first column being the first asked for.
typedef struct {
  uint8_t* back;
  uint8_t* front;
  bool any_back, any_front;
} BgRows;

// Which scroll a line is read with. A line being drawn is read with the
// registers as they stand. The smoothing takes the frame apart when it is
// over, and reads each line with the scroll that was recorded as it was drawn.
typedef enum { SCROLL_NOW, SCROLL_RECORDED } ScrollFrom;

static int scroll_x(const VideoState* s, int layer, int line, ScrollFrom from) {
  return from == SCROLL_NOW ? s->bg[layer].hscroll : s->line_hscroll[layer][drawn_line(line)];
}

static int scroll_y(const VideoState* s, int layer, int line, ScrollFrom from) {
  return from == SCROLL_NOW ? s->bg[layer].vscroll : s->line_vscroll[layer][drawn_line(line)];
}

// One run of a background's line.
static void bg_draw_run(const VideoState* s, int layer, int line, ScrollFrom from,
                        const Runs* runs, const Run* run, BgRows* rows) {
  uint8_t pixels[8];
  bool in_front;
  if (run->fixed) {
    const int px = (run->from_x + scroll_x(s, layer, run->from_line, from)) & 0x3ff;
    const int py = (run->from_line + scroll_y(s, layer, run->from_line, from)) & 0x3ff;
    const uint64_t row = tile_row(s, layer, px, py, &in_front);
    memcpy(pixels, &row, 8);
    memset((in_front ? rows->front : rows->back) + (run->x0 - runs->from), pixels[px & 7],
           (size_t)(run->x1 - run->x0));
    *(in_front ? &rows->any_front : &rows->any_back) = true;
    return;
  }
  const int hs = scroll_x(s, layer, line, from);
  const int py = (line + scroll_y(s, layer, line, from)) & 0x3ff;
  for (int x = run->x0; x < run->x1;) {
    const int px = (x + run->shift + hs) & 0x3ff;
    const uint64_t row = tile_row(s, layer, px, py, &in_front);
    memcpy(pixels, &row, 8);
    int n = 8 - (px & 7);
    if (n > run->x1 - x) n = run->x1 - x;
    memcpy((in_front ? rows->front : rows->back) + (x - runs->from), pixels + (px & 7),
           (size_t)n);
    *(in_front ? &rows->any_front : &rows->any_back) = true;
    x += n;
  }
}

// A background's line over the columns `from` up to `to`. With `draw` false
// only the runs are worked out: for a centred layer that is where its margins
// are searched for, and the emulated PPU searches whether or not the layer is
// on.
static void bg_line(const VideoState* s, VideoCentre* centre, int layer, int line, int from,
                    int to, ScrollFrom scroll, bool draw, BgRows* rows) {
  Runs runs = {.from = from, .to = to};
  bg_runs(s, centre, layer, line, &runs);
  rows->any_back = rows->any_front = false;
  if (!draw) return;
  memset(rows->back, 0, (size_t)(to - from));
  memset(rows->front, 0, (size_t)(to - from));
  for (int i = 0; i < runs.count; i++)
    bg_draw_run(s, layer, line, scroll, &runs, &runs.run[i], rows);
}

bool video_bg_row_declines(const VideoState* s, int layer) {
  if (s->mode != 1 || layer > VIDEO_BG3) return true;
  if (s->extra_left == 0 && s->extra_right == 0) return false;
  return wide_policy(s, layer) == VIDEO_WIDE_CLAMP_EDGE;
}

void video_bg_row(const VideoState* s, VideoCentre* centre, int layer, int line, int from, int to,
                  uint8_t* back, uint8_t* front) {
  BgRows rows = {.back = back, .front = front};
  bg_line(s, centre, layer, line, from, to, SCROLL_RECORDED, true, &rows);
}

// --- One screen -------------------------------------------------------------------

// Mode 1's layers from the front to the back, without BG3's high tiles in
// front of everything and with. Sprites have four priorities and a
// background's tiles two.
typedef struct {
  uint8_t layer, priority;
} Slot;
#define SLOTS 10
static const Slot ORDER[2][SLOTS] = {
    {{VIDEO_OBJ, 3}, {VIDEO_BG1, 1}, {VIDEO_BG2, 1}, {VIDEO_OBJ, 2}, {VIDEO_BG1, 0},
     {VIDEO_BG2, 0}, {VIDEO_OBJ, 1}, {VIDEO_BG3, 1}, {VIDEO_OBJ, 0}, {VIDEO_BG3, 0}},
    {{VIDEO_BG3, 1}, {VIDEO_OBJ, 3}, {VIDEO_BG1, 1}, {VIDEO_BG2, 1}, {VIDEO_OBJ, 2},
     {VIDEO_BG1, 0}, {VIDEO_BG2, 0}, {VIDEO_OBJ, 1}, {VIDEO_OBJ, 0}, {VIDEO_BG3, 0}},
};

typedef struct {
  // Each background's two rows, and whether there is anything in each.
  uint8_t back[3][VIDEO_MAX_WIDTH], front[3][VIDEO_MAX_WIDTH];
  bool any_back[3], any_front[3];
  // The columns the sprites' row is looked at in, and whether it has any.
  int obj_from, obj_to;
  bool any_obj;
} Rows;

// One screen's line: the layers that are `on` painted from the back. `index`
// is each column's palette index, 0 where no layer has anything, and `math`
// whether the layer it came from has colour maths.
static void screen_line(const VideoState* s, const Rows* rows, const bool* on, int width,
                        uint8_t* index, uint8_t* math) {
  memset(index, 0, (size_t)width);
  memset(math, s->math[VIDEO_BACKDROP], (size_t)width);
  const Slot* order = ORDER[s->bg3_front];
  for (int i = SLOTS - 1; i >= 0; i--) {
    const int layer = order[i].layer, priority = order[i].priority;
    if (!on[layer]) continue;
    if (layer == VIDEO_OBJ) {
      if (!rows->any_obj) continue;
      // Only the sprites of the last four palettes take colour maths.
      const bool obj_math = s->math[VIDEO_OBJ];
      for (int c = rows->obj_from; c < rows->obj_to; c++) {
        const uint8_t p = s->obj_priority[c] == priority ? s->obj_pixel[c] : 0;
        index[c] = p ? p : index[c];
        math[c] = p ? (uint8_t)(obj_math && p >= 0xc0) : math[c];
      }
      continue;
    }
    if (!(priority ? rows->any_front : rows->any_back)[layer]) continue;
    const uint8_t* row = priority ? rows->front[layer] : rows->back[layer];
    const uint8_t layer_math = s->math[layer];
    for (int c = 0; c < width; c++) {
      const uint8_t p = row[c];
      index[c] = p ? p : index[c];
      math[c] = p ? layer_math : math[c];
    }
  }
}

// --- Colours ----------------------------------------------------------------------

// Five bits to eight, at a brightness of 0 to 15.
static uint32_t shade(int level, int brightness) {
  return (uint32_t)(((level << 3) | (level >> 2)) * brightness / 15);
}

static uint32_t to_screen(const VideoState* s, int r, int g, int b) {
  return shade(r, s->brightness) << 16 | shade(g, s->brightness) << 8 | shade(b, s->brightness);
}

static uint32_t plain_colour(const VideoState* s, uint16_t colour) {
  return to_screen(s, colour & 0x1f, (colour >> 5) & 0x1f, (colour >> 10) & 0x1f);
}

// A colour with another added to it or taken from it, each part held to its
// five bits.
static uint32_t mathed_colour(const VideoState* s, uint16_t colour, int r2, int g2, int b2) {
  int r = colour & 0x1f, g = (colour >> 5) & 0x1f, b = (colour >> 10) & 0x1f;
  if (s->subtract) {
    r -= r2, g -= g2, b -= b2;
  } else {
    r += r2, g += g2, b += b2;
  }
  r = r < 0 ? 0 : r > 31 ? 31 : r;
  g = g < 0 ? 0 : g > 31 ? 31 : g;
  b = b < 0 ? 0 : b > 31 ? 31 : b;
  return to_screen(s, r, g, b);
}

// The two tables, made again only when something they are made from changes.
static void colours_refresh(Video* v, const VideoState* s) {
  if (v->colours_known && v->brightness == s->brightness && v->fixed_r == s->fixed_r &&
      v->fixed_g == s->fixed_g && v->fixed_b == s->fixed_b && v->subtract == s->subtract &&
      memcmp(v->cgram, s->cgram, sizeof v->cgram) == 0)
    return;
  memcpy(v->cgram, s->cgram, sizeof v->cgram);
  v->brightness = s->brightness;
  v->fixed_r = s->fixed_r, v->fixed_g = s->fixed_g, v->fixed_b = s->fixed_b;
  v->subtract = s->subtract;
  v->colours_known = true;
  for (int i = 0; i < 256; i++) {
    v->plain[i] = plain_colour(s, v->cgram[i]);
    v->mathed[i] = mathed_colour(s, v->cgram[i], s->fixed_r, s->fixed_g, s->fixed_b);
  }
}

// --- The colour window ---------------------------------------------------------

// The columns of the picture one window covers, `lo` to `hi`, none if `lo` is
// past `hi`. On the console that is its two edges. On a widened picture:
// a window right across the console is right across the picture; one a column
// wide on either edge of the console is one the game has parked, and covers
// nothing; and while a layer is anchored to the picture's edges, which is the
// status panel in a level, an edge moves out with the half it is in.
static void window_span(const VideoState* s, int left, int right, int* lo, int* hi) {
  *lo = left, *hi = right;
  if (s->extra_left == 0 && s->extra_right == 0) return;
  if (left == 0 && right == 255) {
    *lo = -s->extra_left, *hi = 255 + s->extra_right;
    return;
  }
  if (left == right && (left == 0 || left == 255)) {
    *lo = 1, *hi = 0;
    return;
  }
  bool anchored = false;
  for (int l = 0; l < 4; l++) anchored |= s->wide[l] == VIDEO_WIDE_ANCHOR;
  if (!anchored) return;
  *lo = left < 128 ? left - s->extra_left : left + s->extra_right;
  *hi = right < 128 ? right - s->extra_left : right + s->extra_right;
}

// Whether each column is inside the colour window.
static void colour_window_line(const VideoState* s, int width, uint8_t* inside) {
  const VideoWindow* w = &s->colour_window;
  if (!w->one && !w->two) {
    memset(inside, 0, (size_t)width);
    return;
  }
  int lo1, hi1, lo2, hi2;
  window_span(s, s->window1_left, s->window1_right, &lo1, &hi1);
  window_span(s, s->window2_left, s->window2_right, &lo2, &hi2);
  for (int c = 0; c < width; c++) {
    const int x = c - s->extra_left;
    const bool one = (x >= lo1 && x <= hi1) != w->one_inverted;
    const bool two = (x >= lo2 && x <= hi2) != w->two_inverted;
    bool in;
    if (!w->two) in = one;
    else if (!w->one) in = two;
    else if (w->logic == 0) in = one || two;
    else if (w->logic == 1) in = one && two;
    else if (w->logic == 2) in = one != two;
    else in = one == two;
    inside[c] = in;
  }
}

// --- A line -----------------------------------------------------------------------

void video_init(Video* v) {
  memset(v, 0, sizeof *v);
  if (!spread_made) make_spread();
}

static bool any_math(const VideoState* s) {
  bool any = false;
  for (int l = 0; l < VIDEO_LAYERS; l++) any |= s->math[l];
  return any;
}

const char* video_declines(const VideoState* s) {
  if (s->blank) return NULL;
  if (s->mode != 1) return "a mode other than 1";
  if (s->pseudo_hires) return "pseudo hi-res";
  if (s->overscan) return "overscan";
  if (s->clip != 0) return "the main screen clipped by the colour window";
  if (s->half && any_math(s)) return "colour maths halved";
  for (int l = VIDEO_BG1; l <= VIDEO_OBJ; l++) {
    if (l == VIDEO_BG4) continue;
    if ((s->main[l] && s->main_windowed[l]) || (s->sub[l] && s->sub_windowed[l]))
      return "a window on a layer";
    if (l != VIDEO_OBJ && s->bg[l].mosaic && s->mosaic_size > 1 && (s->main[l] || s->sub[l]))
      return "mosaic";
  }
  if (s->extra_left == 0 && s->extra_right == 0) return NULL;
  for (int l = VIDEO_BG1; l <= VIDEO_BG3; l++)
    if (wide_policy(s, l) == VIDEO_WIDE_CLAMP_EDGE)
      return "a layer's edge column carried into the margins";
  const VideoWide obj = wide_policy(s, VIDEO_OBJ);
  if (obj != VIDEO_WIDE_STRETCH && obj != VIDEO_WIDE_CLIP)
    return "sprites placed other than with the world or the console";
  return NULL;
}

void video_line(Video* v, const VideoState* s, VideoCentre* centre, int line, uint32_t* out) {
  const int width = video_width(s);
  if (s->blank) {
    memset(out, 0, (size_t)width * sizeof *out);
    return;
  }
  // Is there colour maths on this line at all, and does any of it take the
  // sub screen's colour? Only then is the sub screen looked at.
  const bool maths = any_math(s) && s->prevent != 3;
  const bool second = maths && s->add_sub;

  Rows rows;
  for (int l = VIDEO_BG1; l <= VIDEO_BG3; l++) {
    BgRows bg = {.back = rows.back[l], .front = rows.front[l]};
    bg_line(s, centre, l, line, -s->extra_left, 256 + s->extra_right, SCROLL_NOW,
            s->main[l] || (second && s->sub[l]), &bg);
    rows.any_back[l] = bg.any_back;
    rows.any_front[l] = bg.any_front;
  }
  // The sprites' row is the picture's already. A policy only says which
  // columns of it show.
  int from = -s->extra_left, to = 256 + s->extra_right;
  if (s->extra_left != 0 || s->extra_right != 0) {
    const bool clip = wide_policy(s, VIDEO_OBJ) == VIDEO_WIDE_CLIP;
    const int lo = clip ? 0 : s->clamp_lo, hi = clip ? 256 : s->clamp_hi + 1;
    if (from < lo) from = lo;
    if (to > hi) to = hi;
  }
  rows.obj_from = from + s->extra_left;
  rows.obj_to = to + s->extra_left;
  rows.any_obj = false;
  for (int c = rows.obj_from; c < rows.obj_to && !rows.any_obj; c++)
    rows.any_obj = s->obj_pixel[c] != 0;

  uint8_t index[VIDEO_MAX_WIDTH], math[VIDEO_MAX_WIDTH];
  screen_line(s, &rows, s->main, width, index, math);
  colours_refresh(v, s);
  if (!maths) {
    for (int c = 0; c < width; c++) out[c] = v->plain[index[c]];
    return;
  }
  // Where maths is allowed: everywhere, or one side of the colour window.
  uint8_t allowed[VIDEO_MAX_WIDTH];
  if (s->prevent == 0) {
    memset(allowed, 1, (size_t)width);
  } else {
    colour_window_line(s, width, allowed);
    if (s->prevent == 2)
      for (int c = 0; c < width; c++) allowed[c] = !allowed[c];
  }
  if (!second) {
    for (int c = 0; c < width; c++)
      out[c] = (math[c] & allowed[c]) ? v->mathed[index[c]] : v->plain[index[c]];
    return;
  }
  // With the sub screen: its colour where it has a layer's pixel, and the
  // fixed colour where it has only its backdrop.
  uint8_t sub_index[VIDEO_MAX_WIDTH], sub_math[VIDEO_MAX_WIDTH];
  screen_line(s, &rows, s->sub, width, sub_index, sub_math);
  for (int c = 0; c < width; c++) {
    if (!(math[c] & allowed[c])) {
      out[c] = v->plain[index[c]];
    } else if (sub_index[c] == 0) {
      out[c] = v->mathed[index[c]];
    } else {
      const uint16_t other = s->cgram[sub_index[c]];
      out[c] = mathed_colour(s, s->cgram[index[c]], other & 0x1f, (other >> 5) & 0x1f,
                             (other >> 10) & 0x1f);
    }
  }
}
