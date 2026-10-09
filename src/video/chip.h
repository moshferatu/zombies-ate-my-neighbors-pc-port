// The console's video chip, kept and drawn here.
//
// A `VideoChip` is all of it: the registers (`registers.h`) with the three
// memories they are doors into, what the frontend says of the picture and
// what is noted of each frame (`frame.h`), the sprites of the line being
// drawn, and the picture. Whoever runs the console tells it what happens:
// `video_chip_write` and `video_chip_read` are the game's, the three events
// come once a frame each, and `video_chip_run_line` is a line of the picture
// -- its scrolls and windows noted, its sprites found, the line drawn
// (`video.h`).
//
// The frontend has the chip itself. It reads `registers`, `picture` and
// `frame` by name, says what it has to say of the picture with
// `video_set_margins` and the rest, and takes the picture with
// `video_put_pixels`.
//
// `video.h` draws what this game shows and not all a chip can. A line
// `video_declines`, or whose sprites `video_sprites_declines`, is counted by
// what it was about it, and left to `VideoOther` if there is one: somebody
// with another chip, who is also told what the frontend says as it says it.
// `console.h` is one, with an emulated PPU. With nobody, such a line is
// black and has no sprites. Nothing this game shows is one.
//
// The checksum, when it is asked for, is over every line as it is drawn.
//
// libc only.

#ifndef ZAMN_VIDEO_CHIP_H
#define ZAMN_VIDEO_CHIP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "video/frame.h"
#include "video/registers.h"
#include "video/video.h"

// How a pixel of the picture is handed over, as a 32-bit word: its bytes in
// memory are blue, green, red and one unused, or the unused one first.
typedef enum {
  VIDEO_PIXELS_XRGB,
  VIDEO_PIXELS_RGBX,
} VideoPixels;

// The picture as it is kept: two fields of 239 lines, of which a frame that
// is not interlaced draws one. A column is eight bytes, the pixel twice, so
// a line is handed over twice as wide as the game drew it.
#define VIDEO_ROW_BYTES (VIDEO_MAX_WIDTH * 8)
#define VIDEO_FIELD_ROWS (VIDEO_LINES - 1)

// Something the frontend has said or put, for `VideoOther.said`, and what
// `which` is of it.
typedef enum {
  VIDEO_SAID_MARGINS,
  VIDEO_SAID_WIDE,       // a layer
  VIDEO_SAID_CLAMP,
  VIDEO_SAID_OF_SPRITE,  // a sprite: its place, shift, front or colours
  VIDEO_SAID_REMAP,
  VIDEO_SAID_SCROLL,     // a background
  VIDEO_SAID_SPRITE,     // a sprite, put into OAM
  VIDEO_SAID_VRAM,       // a word's address
  VIDEO_SAID_COLOUR,     // a colour's index
  VIDEO_SAID_PIXEL_FORMAT,
} VideoSaid;

// Somebody with another chip beside this one. Any of the three may be NULL.
typedef struct {
  void* user;
  // The frontend said or put something, which has been done here.
  void (*said)(void* user, VideoSaid what, int which);
  // A line's sprites that are not found here: to be found into `obj_pixel`
  // and `obj_priority`, with the registers' two flags.
  void (*sprites)(void* user, int line);
  // A line that is not drawn here: to be drawn into `video_chip_row`.
  void (*line)(void* user, int line);
} VideoOther;

#define VIDEO_CHIP_REASONS 8

typedef struct {
  Video video;
  VideoRegisters registers;
  VideoMemory memory;
  // What the frontend says of the picture, and what is noted of each frame.
  VideoPicture picture;
  VideoFrame frame;
  // The sprites on the line being drawn, whoever found them: a palette
  // index for each column of the picture, or 0, and that sprite's priority.
  uint8_t obj_pixel[VIDEO_MAX_WIDTH], obj_priority[VIDEO_MAX_WIDTH];
  // The picture, whoever drew it.
  VideoPixels format;
  uint8_t pixels[2 * VIDEO_FIELD_ROWS * VIDEO_ROW_BYTES];
  VideoOther other;

  // What has been done: the game's writes and reads, lines noted as they
  // began, lines whose sprites were found here and lines drawn here.
  long writes, reads, noted_lines, sprite_lines, lines;
  // ...and what was not: lines whose sprites were left, and lines left, with
  // what it was about them.
  long sprite_declined, declined;
  struct {
    const char* why;
    long lines;
  } reason[VIDEO_CHIP_REASONS];
  bool checksummed;
  uint64_t checksum;
  long checksum_lines;
} VideoChip;

// A chip with nothing in it, keeping a checksum of its picture if
// `checksummed`. `video_chip_reset` is the console's reset.
void video_chip_init(VideoChip* chip, bool checksummed);
void video_chip_reset(VideoChip* chip);

// --- For whoever runs the console ---

// The game's: $2100 + `address`, 0 to 0x3f. `line` is the line the beam is
// on, and `drawing` whether the picture is being drawn: see
// `video_frame_write`.
void video_chip_write(VideoChip* chip, uint8_t address, uint8_t value, int line, bool drawing);
uint8_t video_chip_read(VideoChip* chip, uint8_t address, const VideoBus* bus);

// The top of a frame; line 225, where the chip settles whether this frame
// has the 239 lines of overscan, which is what is returned; and the end of
// the picture.
void video_chip_frame_start(VideoChip* chip);
bool video_chip_overscan(VideoChip* chip);
void video_chip_vblank(VideoChip* chip);

// A line of the picture, 1 to 239.
void video_chip_run_line(VideoChip* chip, int line);

// ...which is these four, for somebody who does each beside another chip.
// The line begins: its scrolls and windows are noted.
void video_chip_begin_line(VideoChip* chip, int line);
// Its sprites are found into the two rows, and the registers' two flags
// raised. False if they are not found here: the first row is then empty.
bool video_chip_find_sprites(VideoChip* chip, int line);
// It is drawn into its row from those. NULL, or what it is about the line
// that is not drawn here: its row is then as it was.
const char* video_chip_draw_line(VideoChip* chip, int line);
// Its row is the line as it will be shown: into the checksum.
void video_chip_count_line(VideoChip* chip, int line);

// A line's place in a picture kept as this one is, by the field it is in,
// and the line's row in the field being drawn.
static inline size_t video_row_at(bool even, int line) {
  return (size_t)((line - 1) + (even ? 0 : VIDEO_FIELD_ROWS)) * VIDEO_ROW_BYTES;
}
uint8_t* video_chip_row(VideoChip* chip, int line);

// The colour at a column of a row, 0x00RRGGBB, and whether both of the
// column's pixels are `colour`.
uint32_t video_row_colour(VideoPixels format, const uint8_t* row, int column);
bool video_row_is(VideoPixels format, const uint8_t* row, int column, uint32_t colour);

// --- For the frontend ---

// The picture's width in columns: 256 and both margins.
int video_chip_width(const VideoChip* chip);

// What a line is drawn from: the registers, the picture as it was said and
// the frame as it was noted, with the sprites of the line last drawn.
void video_chip_state(const VideoChip* chip, VideoState* s);

// What the frontend says of the picture: see `VideoPicture`. Each takes
// effect on the next line drawn, so they are said between frames. A margin
// is held to `VIDEO_EXTRA_MAX`; a layer is 0 to 3 for a background and 4 for
// the sprites.
void video_set_margins(VideoChip* chip, int left, int right);
void video_set_wide(VideoChip* chip, int layer, VideoWide policy);
void video_set_clamp(VideoChip* chip, int lo, int hi);
void video_set_sprite_place(VideoChip* chip, int sprite, VideoSpritePlace place);
void video_set_sprite_shift(VideoChip* chip, int sprite, int shift);
void video_set_sprite_front(VideoChip* chip, int sprite, bool front);
void video_set_sprite_remapped(VideoChip* chip, int sprite, bool remapped);
void video_set_remap(VideoChip* chip, const uint8_t remap[16]);

// ...and one thing it puts into a register: a background's scroll, ten bits
// each way, for a layer it draws where the game did not. That is the big
// figure's plane, which the game parks out of sight of a picture narrower
// than the one being drawn. Not a write of the game's, and not noted as one.
void video_set_scroll(VideoChip* chip, int layer, int h, int v);

// ...and three it puts into the memories: see `video_put_sprite`,
// `video_put_vram` and `video_put_colour`. The frontend reads the memories
// through `registers` and writes them only with these, because somebody
// beside the chip may have a set of their own that wants telling.
void video_set_sprite(VideoChip* chip, int sprite, int x, int y, uint16_t word, bool large);
void video_set_vram(VideoChip* chip, uint16_t at, uint16_t word);
void video_set_colour(VideoChip* chip, int index, uint16_t colour);

// The picture of the frame last drawn. `video_output_width` pixels across,
// which is two for each of the picture's columns, and 480 rows, each of the
// frame's lines twice: 224 lines leave sixteen rows of black above and below
// them, and 239 leave two above. Four bytes a pixel, as
// `video_set_pixel_format` last said, and no gap between rows.
void video_set_pixel_format(VideoChip* chip, VideoPixels format);
int video_output_width(const VideoChip* chip);
void video_put_pixels(const VideoChip* chip, uint8_t* pixels);

#endif
