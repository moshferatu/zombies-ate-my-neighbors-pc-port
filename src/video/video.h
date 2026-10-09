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
// The registers are kept by `registers.h`, which fills its share of one.
// `frame.h` fills the rest: the widened picture and what the frontend has
// said of each sprite, and where the scrolls and the windows were on each
// line of the frame.
//
// A line's sprites are found from OAM by `video_sprites`, into a row of the
// picture's width that `video_line` is then given: a pixel and a priority for
// each column.
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
// the emulated PPU's `ppu_wide*`, which `console.c` checks.
typedef enum {
  VIDEO_WIDE_AUTO,         // worked out from the layer
  VIDEO_WIDE_STRETCH,      // the map goes on
  VIDEO_WIDE_ANCHOR,       // its halves at the picture's two edges
  VIDEO_WIDE_CLIP,         // the console's 256 and nothing beside them
  VIDEO_WIDE_CLAMP_EDGE,   // its edge columns carried out
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

// Where a sprite goes when the picture is widened. The values are the
// emulated PPU's `ppu_sprite*`, which `console.c` checks.
typedef enum {
  VIDEO_SPRITE_WORLD,     // the console's coordinates, on past its edges
  VIDEO_SPRITE_ANCHORED,  // as far in from the picture's edge as from the console's
  VIDEO_SPRITE_CENTRED,   // with a centred layer, and again in each of its margins
} VideoSpritePlace;

#define VIDEO_SPRITES 128

// The sprites: OAM as the game left it, the registers that say how to read
// it, and what the frontend has said of each sprite.
typedef struct {
  // Two words a sprite. The first is X's low eight bits and, above them, Y.
  // The second is the character, the name table's bit, three bits of
  // palette, two of priority and the two flips.
  const uint16_t* oam;
  // Two bits a sprite, four sprites a byte: X's ninth bit, and whether it is
  // the larger of the two sizes.
  const uint8_t* high_oam;
  uint8_t sizes;         // which two sizes, 0 to 7
  uint8_t first;         // the sprite looked at first, and so drawn in front
  uint16_t tiles_at[2];  // the characters' word address in VRAM, per name table
  bool interlace;        // half-height sprites (not found here)
  // Per sprite. `front`: found before every sprite that is not, whatever its
  // number. `remap_on`: its pixels are looked up in `remap`.
  const bool* front;
  const bool* remap_on;
  const uint8_t* place;  // a `VideoSpritePlace`
  const int16_t* shift;  // columns along from where the game put it
  // For a sprite with `remap_on`: the palette index each of its sixteen
  // pixel values is drawn in, or 0 for the one its own palette gives.
  const uint8_t* remap;
} VideoObj;

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

  VideoObj obj;
  // The sprites on this line, as `video_sprites` or the PPU found them: for
  // each column of the picture a palette index, or 0, and that sprite's
  // priority, 0 to 3.
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
  // ...and where the two windows' edges were: the first's left and right,
  // then the second's.
  const uint8_t (*line_window)[4];
} VideoState;

// `VIDEO_WIDE_CENTRE`'s margins, per background and side: where on the layer
// a margin is filled from on the line last asked about, found once for the
// line and the scroll; and the last line the layer is opaque right across,
// found once for a scroll. See the policy in `video.c`.
//
// It is kept by whoever calls, and not in `Video`: `frame.h` keeps the one a
// frame's lines are drawn with, and the smoothing reads the frame with a copy.
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

// More sprites on a line than the console has time for: more than 32 of
// them, or more than 34 tiles' worth. The game can read both back.
enum {
  VIDEO_SPRITES_RANGE_OVER = 1,
  VIDEO_SPRITES_TIME_OVER = 2,
};

// Find the sprites on line `line` of the frame, 1 to 224: for each of the
// picture's `video_width` columns a palette index into `pixel`, 0 where there
// is none, and where there is one its sprite's priority into `priority`,
// whose other columns are left as they were. Returns which of the two limits
// the line went over. `s->obj_pixel` and `obj_priority` are not read.
// `video_sprites_declines` is true where the PPU would find something else.
bool video_sprites_declines(const VideoState* s);
int video_sprites(const VideoState* s, int line, uint8_t* pixel, uint8_t* priority);

// A sprite's side in pixels, 8 to 64: the smaller or the larger of the two
// sizes the register picks.
int video_obj_size(const VideoObj* o, int sprite);

// The column of the picture a sprite's left edge is drawn at, which on the
// console is its X. A widened picture moves the point where X comes round,
// and a sprite placed with an anchored or a centred layer goes where that
// layer's column of the same number went.
int video_sprite_x(const VideoState* s, int sprite);

// How far right of its place on the console background `layer` was drawn on
// `line` of the frame: `VIDEO_WIDE_SWEEP`'s shift, and 0 under every other
// policy. Whoever follows the layer from one frame to the next wants its
// scroll less this.
int video_sweep_shift(const VideoState* s, int layer, int line);

// Whether colour maths was allowed at column `x` of the picture on `line` of
// the frame, by the colour window as its edges were on that line. Whether a
// layer has maths at all is `VideoState.math`. A line above or below the
// picture is read as the nearest line that was drawn.
bool video_math_allowed(const VideoState* s, int x, int line);

// One background's line of a frame that has been drawn, for taking the frame
// apart (`src/layers.h`): the columns `from` up to `to` of the picture, which
// may run past its edges, on a line that may be a little above or below it,
// read with the scroll that was recorded as the nearest line was drawn. Each
// column's palette index, or 0, goes to `back` if its tile is one that goes
// behind and to `front` if it goes in front, and 0 to the other.
//
// No window is applied. `video_bg_row_declines` is true for a layer this does
// not read: any outside mode 1, and the fourth, which mode 1 has not got.
bool video_bg_row_declines(const VideoState* s, int layer);
void video_bg_row(const VideoState* s, VideoCentre* centre, int layer, int line, int from, int to,
                  uint8_t* back, uint8_t* front);

#endif
