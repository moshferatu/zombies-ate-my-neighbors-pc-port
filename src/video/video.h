// The picture, drawn here and not by an emulated PPU.
//
// The console's video chip is asked for a picture one dot at a time, and the
// emulator answers it that way: for each of a scanline's dots, for each layer
// in front-to-back order, find the tile under the dot, read its bits, stop at
// the first that is not transparent. That is what the hardware does and it is
// most of what this program spends its time on. A profile of a level at
// 16:9 had 43% of everything in it.
//
// A scanline is not really made of dots, though. A background is a row of
// tiles, eight pixels of each read from two or four words of VRAM, and the
// layers are stacked in a fixed order. So this draws a line the way the game
// describes it:
//
//   1. each background's line, a tile at a time, into one row of pixels for
//      its tiles that go behind and one for its tiles that go in front;
//   2. the rows painted over each other from the back, with the sprites' row
//      between them where the mode puts it;
//   3. each column's colour looked up, with the colour maths the game asked
//      for, in a table that is rebuilt only when the palette changes.
//
// ## What it draws, and what it leaves
//
// It draws what this game uses, which is little of what the chip can do. Over
// the 54 movies of the corpus every line the game shows is mode 1, with no
// mosaic, no hi-res, no interlace, no window on any layer, and the colour
// window used only to say where colour maths applies. `video_declines` names
// the first thing about a line that is outside that, and the caller leaves
// such a line to the emulator. Nothing in the corpus is declined.
//
// The widened picture (`extra_left`, `extra_right` and a policy per layer,
// see `third_party/lakesnes/snes/ppu.h` for what each policy is for) is drawn
// the same way: a policy says which column of a layer each column of the
// picture shows, and every one of them is a few stretches of consecutive
// columns, so a line is still a few runs of whole tiles.
//
// ## What it is drawn from
//
// `VideoState` is what a line is drawn from: VRAM, the palette, and the
// registers as the game last set them. It names nothing of the emulator's.
// `src/video/ppu_hook.c` fills one from the emulated PPU, which is where the
// registers still live while the ROM's code is what writes them.
//
// The sprites of a line are not found here yet. The state carries them as the
// PPU found them, a pixel and a priority for each column.
//
// libc only.

#ifndef ZAMN_VIDEO_H
#define ZAMN_VIDEO_H

#include <stdbool.h>
#include <stdint.h>

// The most columns either side of the console's 256, and so the widest line.
#define VIDEO_EXTRA_MAX 192
#define VIDEO_MAX_WIDTH (256 + 2 * VIDEO_EXTRA_MAX)
// Rows a per-line record has: lines 1 to 239, indexed by line.
#define VIDEO_LINES 240

enum {
  VIDEO_BG1,
  VIDEO_BG2,
  VIDEO_BG3,
  VIDEO_BG4,
  VIDEO_OBJ,
  VIDEO_BACKDROP,
  VIDEO_LAYERS,
};

// What a layer does with the margins of a widened picture. The values are
// the emulated PPU's `ppu_wide*`, which `ppu_hook.c` checks.
typedef enum {
  VIDEO_WIDE_AUTO,         // worked out from the layer
  VIDEO_WIDE_STRETCH,      // the map goes on
  VIDEO_WIDE_ANCHOR,       // its halves at the picture's two edges
  VIDEO_WIDE_CLIP,         // the console's 256 and nothing beside them
  VIDEO_WIDE_CLAMP_EDGE,   // its edge columns carried out (not drawn here)
  VIDEO_WIDE_CENTRE,       // its 256 at the picture's middle, margins filled
  VIDEO_WIDE_TILE,         // its 256 repeated
  VIDEO_WIDE_SWEEP,        // shifted as it is swept in
  VIDEO_WIDE_CENTRE_CLIP,  // its 256 at the picture's middle
} VideoWide;

typedef struct {
  uint16_t hscroll, vscroll;
  uint16_t map_at;    // the tilemap's word address in VRAM
  uint16_t tiles_at;  // ...and the characters'
  bool map_wide, map_high;  // 64 tiles across, or down
  bool big_tiles;           // 16x16
  bool mosaic;
} VideoBg;

// One of the two windows' part in a layer's window, and how the two combine.
typedef struct {
  bool one, two;
  bool one_inverted, two_inverted;
  uint8_t logic;  // 0 or, 1 and, 2 xor, 3 xnor
} VideoWindow;

typedef struct {
  const uint16_t* vram;   // 0x8000 words
  const uint16_t* cgram;  // 256 colours, five bits each of red, green, blue

  bool blank;          // forced blank: the line is black
  uint8_t brightness;  // 0 to 15
  uint8_t mode;
  bool bg3_front;      // mode 1: BG3's high tiles in front of everything
  bool pseudo_hires;
  bool overscan;
  uint8_t mosaic_size;
  VideoBg bg[4];
  // Which layers are on each screen, and which of those a window hides part
  // of. Indexed by `VIDEO_BG1` to `VIDEO_OBJ`.
  bool main[5], sub[5];
  bool main_windowed[5], sub_windowed[5];

  // Colour maths: for which layers' pixels, with what, and where.
  bool math[VIDEO_LAYERS];
  bool add_sub;     // the sub screen is the other colour, where it has one
  bool subtract;
  bool half;
  uint8_t fixed_r, fixed_g, fixed_b;  // the other colour otherwise
  uint8_t prevent;  // 0 never, 1 outside the colour window, 2 inside, 3 always
  uint8_t clip;     // the main screen forced black, likewise
  VideoWindow colour_window;
  uint8_t window1_left, window1_right, window2_left, window2_right;

  // The sprites on this line, as the PPU found them: for each column of the
  // picture a palette index, or 0, and that sprite's priority, 0 to 3.
  const uint8_t* obj_pixel;
  const uint8_t* obj_priority;

  // The widened picture: the line runs from `-extra_left` to
  // `255 + extra_right`. Both zero is the console.
  int extra_left, extra_right;
  uint8_t wide[5];  // a `VideoWide` per layer
  // What `VIDEO_WIDE_AUTO` is worked out from, per background.
  bool edge_empty[4];  // nothing at either edge of the console
  bool raster[4];      // its scroll is rewritten line by line
  bool scrolled[4];    // the game has scrolled it sideways on this screen
  // The columns a stretched layer has anything in.
  int clamp_lo, clamp_hi;
  // Where each background was scrolled to as each line of this frame was
  // drawn, this line included.
  const uint16_t (*line_hscroll)[VIDEO_LINES];
  const uint16_t (*line_vscroll)[VIDEO_LINES];
} VideoState;

// `VIDEO_WIDE_CENTRE`'s margins, per background and side: where on the layer
// a margin is filled from on the line last asked about, found once for the
// line and the scroll; and the last line the layer is opaque right across,
// found once for a scroll. See the policy in `video.c`.
//
// It is kept by whoever calls, and not in `Video`, because the emulated PPU
// keeps the same thing for the lines it draws and the two must agree.
typedef struct {
  int16_t fill_col[4][2], fill_from[4][2];
  int16_t fill_line[4][2];
  uint16_t fill_h[4][2], fill_v[4][2];
  uint8_t fill_wrap[4][2];
  int16_t last_full[4];
  uint16_t last_full_h[4], last_full_v[4];
} VideoCentre;

typedef struct {
  // The colours of the palette as they go to the screen, plain and with the
  // fixed colour's maths done, and what they were made from.
  uint32_t plain[256], mathed[256];
  uint16_t cgram[256];
  uint8_t brightness, fixed_r, fixed_g, fixed_b;
  bool subtract;
  bool colours_known;
} Video;

void video_init(Video* v);

// The picture's width in columns: 256 and both margins.
static inline int video_width(const VideoState* s) {
  return 256 + s->extra_left + s->extra_right;
}

// NULL if `video_line` draws this line as the PPU does; otherwise what it is
// about the line that it does not draw.
const char* video_declines(const VideoState* s);

// Draw line `line`, 1 to 224, of the frame: `video_width` colours into `out`,
// each 0x00RRGGBB with the brightness applied. Only for a line
// `video_declines` has nothing to say about.
void video_line(Video* v, const VideoState* s, VideoCentre* centre, int line, uint32_t* out);

// One background's line of a frame that has been drawn, for taking the frame
// apart (`src/layers.h`): the columns `from` up to `to` of the picture, which
// may run past its edges, on a line that may be a little above or below it,
// read with the scroll that was recorded as the nearest line was drawn. Each
// column's palette index, or 0, goes to `back` if its tile is one that goes
// behind and to `front` if it goes in front, and 0 to the other.
//
// No window is applied. `video_bg_row_declines` is true for a layer this does
// not read as the PPU does.
bool video_bg_row_declines(const VideoState* s, int layer);
void video_bg_row(const VideoState* s, VideoCentre* centre, int layer, int line, int from, int to,
                  uint8_t* back, uint8_t* front);

#endif
