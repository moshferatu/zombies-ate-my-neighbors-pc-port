// What is said of the picture from outside, and what is noted of a frame as
// it is drawn.
//
// `registers.h` is the chip as the game sees it. A line is drawn from two
// more things, and neither is the game's.
//
// `VideoPicture` is what the frontend says: how far the picture goes on past
// the console's 256 columns, what each layer does out there, and for each
// sprite where it goes, whether it is drawn in front of the rest, and whether
// it is drawn in colours of the frontend's. The console is all of it at zero.
//
// `VideoFrame` is what is noted while a frame is drawn, by whoever runs the
// frame: where each background was scrolled to and where the windows' edges
// were as each line began, and whether the game wrote anything else to the
// chip on the way down. A frame can be taken apart afterwards from the chip
// as it stands at its end and these (`src/layers.h`); without them it could
// not, because a game that draws a raster effect leaves only the last line's
// scroll in the registers.
//
// It also keeps, from frame to frame, what `VIDEO_WIDE_AUTO` is worked out
// from: whether the game has scrolled a background sideways since its screen
// was put up, whether it rewrites that scroll line by line, and whether the
// background has nothing at the console's two edges.
//
// libc only.

#ifndef ZAMN_VIDEO_FRAME_H
#define ZAMN_VIDEO_FRAME_H

#include <stdbool.h>
#include <stdint.h>

#include "video/registers.h"
#include "video/video.h"

typedef struct {
  // Columns either side of the console's 256, each 0 to `VIDEO_EXTRA_MAX`.
  int extra_left, extra_right;
  uint8_t wide[5];  // a `VideoWide` for each background and for the sprites
  // The columns of the picture a stretched layer has anything in: where the
  // level's map begins and ends. Wide open off a level.
  int clamp_lo, clamp_hi;
  // Per sprite.
  uint8_t place[VIDEO_SPRITES];  // a `VideoSpritePlace`
  int16_t shift[VIDEO_SPRITES];  // columns along from where the game put it
  bool front[VIDEO_SPRITES];     // found before every sprite that is not
  bool remap_on[VIDEO_SPRITES];  // drawn in `remap`'s colours
  // For a sprite with `remap_on`: the palette index each of its sixteen pixel
  // values is drawn in, or 0 for the one its own palette gives.
  uint8_t remap[16];
} VideoPicture;

// How many times in one frame a background's scroll across has to change
// before it is read as drawn a line at a time. A layer the game scrolls
// changes it four times a frame at most, and the title's logo on every line.
#define VIDEO_RASTER_WRITES 16

typedef struct {
  // As each line began, indexed by line, 1 to 239.
  uint16_t line_hscroll[4][VIDEO_LINES], line_vscroll[4][VIDEO_LINES];
  uint8_t line_window[VIDEO_LINES][4];  // window 1's left and right, then 2's

  // Whether the windows' edges were written while the picture was being
  // drawn. A frame that did only that can still be taken apart, from
  // `line_window`.
  bool window_raster;
  // ...and whether anything else was, other than a scroll: the brightness,
  // the colour maths, a memory. Such a frame cannot be, because only its end
  // is known. The first such write, and how many there were.
  bool mid_frame_write;
  uint8_t mid_frame_address;
  uint16_t mid_frame_line;
  int mid_frame_writes;

  // What `VIDEO_WIDE_AUTO` is worked out from, per background. `scrolled`
  // and `raster` are kept for as long as a screen stands, and cleared when
  // the screen goes dark; `edge_empty` is looked at once a frame, at its top.
  bool edge_empty[4], scrolled[4], raster[4];
  uint16_t last_hscroll[4];   // where it was scrolled to at the last frame's top
  uint8_t hscroll_writes[4];  // times its scroll across has changed this frame

  // The centred layers' margins, as the frame's lines found them.
  VideoCentre centre;
} VideoFrame;

// The console's picture: no margins, every layer left to be worked out.
void video_picture_init(VideoPicture* p);
// What the console's reset takes back: each sprite's `front` and `remap_on`,
// and `remap`. They are said again every frame. The picture's width is whose
// ever is showing it, and stays.
void video_picture_reset(VideoPicture* p);

void video_frame_init(VideoFrame* f);

// A write of the game's, $2100 + `address`: done to the registers, and noted.
// `line` is the line the beam is on, and `drawing` whether the picture is
// being drawn, which is on a line after the frame's first and before its
// picture ends.
void video_frame_write(VideoFrame* f, VideoRegisters* r, uint8_t address, uint8_t value, int line,
                       bool drawing);

// The top of a frame, after `video_registers_frame_start`.
void video_frame_start(VideoFrame* f, const VideoRegisters* r, const VideoPicture* p);
// A line of the picture begins: where the scrolls and the windows are.
void video_frame_line(VideoFrame* f, const VideoRegisters* r, int line);

// Their share of what a line is drawn from. With `video_registers_state` that
// is all of it but the line's sprites.
void video_frame_state(const VideoFrame* f, const VideoPicture* p, VideoState* s);

#endif
